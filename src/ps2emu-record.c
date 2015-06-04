/*
 * ps2emu-record.c
 * Copyright (C) 2015 Red Hat
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <glib.h>
#include <glib-object.h>

#include "misc.h"
#include "ps2emu-event.h"

typedef enum {
    PS2_INPUT_UNEXPECTED_EOF,
    PS2_INPUT_ERROR
} PS2Error;

static char *input_path = "/dev/kmsg";

static gboolean record_kbd;
static gboolean record_aux;

/* Skips to the next line printed by a module. We do this by just searching for
 * the module name, followed by a ": "
 */
static GIOStatus get_next_module_line(GIOChannel *input_channel,
                                      const gchar *module_name,
                                      gchar **output,
                                      gchar **start_pos,
                                      GError **error) {
    g_autofree gchar *search_str = g_strdup_printf("%s: ", module_name);
    g_autofree gchar *current_line;
    GIOStatus rc;

    while ((rc = g_io_channel_read_line(input_channel, &current_line, NULL,
                                        NULL, error)) == G_IO_STATUS_NORMAL) {
        *start_pos = strstr(current_line, search_str);
        if (*start_pos)
            break;

        g_clear_pointer(&current_line, g_free);
    }

    if (rc != G_IO_STATUS_NORMAL)
        return rc;

    /* Move the start position after the initial 'i8042: ' */
    *start_pos += strlen(search_str);
    *output = g_steal_pointer(&current_line);

    return rc;
}

static gint get_keyboard_irq(GIOChannel *input_channel,
                             GError **error) {
    GIOStatus rc;
    g_autofree gchar* current_line;
    gchar *start_pos;
    gint irq;

    while ((rc = get_next_module_line(input_channel, "serio", &current_line,
                                      &start_pos, error))) {
        /* Attempt to scan the line with sscanf, if we fail then the line isn't
         * telling us where the keyboard port is */
        errno = 0;

        sscanf(start_pos, "i8042 KBD port at %*x, %*x irq %d\n", &irq);

        if (errno == 0)
            return irq;

        g_clear_pointer(&current_line, g_free);
    }

    return 0;
}

static gboolean parse_normal_event(const gchar *start_pos,
                                   PS2Event *event,
                                   GError **error) {
    g_autofree gchar *type_str = NULL;
    g_auto(GStrv) type_str_args = NULL;
    int type_str_argc,
        parsed_count;

    errno = 0;
    parsed_count = sscanf(start_pos,
                          "[%ld] %hhx %*1[-<]%*1[->] i8042 (%m[^)])\n",
                          &event->time, &event->data, &type_str);

    if (errno != 0 || parsed_count != 3)
        return FALSE;

    type_str_args = g_strsplit(type_str, ",", 0);

    if (strcmp(type_str_args[0], "interrupt") == 0) {
        event->type = PS2_EVENT_TYPE_INTERRUPT;

        type_str_argc = g_strv_length(type_str_args);
        if (type_str_argc < 3) {
            g_set_error(error, PS2EMU_ERROR, PS2_INPUT_ERROR,
                        "Got interrupt event, but had less arguments then "
                        "expected");
            return FALSE;
        }

        errno = 0;
        event->irq = strtol(type_str_args[2], NULL, 10);
        if (errno != 0) {
            g_set_error(error, PS2EMU_ERROR, PS2_INPUT_ERROR,
                        "Failed to parse IRQ from interrupt event: %s\n",
                        strerror(errno));
            return FALSE;
        }
    }
    else if (strcmp(type_str, "command") == 0)
        event->type = PS2_EVENT_TYPE_COMMAND;
    else if (strcmp(type_str, "parameter") == 0)
        event->type = PS2_EVENT_TYPE_PARAMETER;
    else if (strcmp(type_str, "return") == 0)
        event->type = PS2_EVENT_TYPE_RETURN;
    else if (strcmp(type_str, "kbd-data") == 0)
        event->type = PS2_EVENT_TYPE_KBD_DATA;

    event->has_data = TRUE;

    return TRUE;
}

static gboolean parse_interrupt_without_data(const gchar *start_pos,
                                             PS2Event *event,
                                             GError **error) {
    int parsed_count;

    errno = 0;
    parsed_count = sscanf(start_pos,
                          "[%ld] Interrupt %hd, without any data\n",
                          &event->time, &event->irq);

    if (errno != 0 || parsed_count != 2)
        return FALSE;

    event->type = PS2_EVENT_TYPE_INTERRUPT;
    event->has_data = FALSE;

    return TRUE;
}

static GIOStatus get_next_event(GIOChannel *input_channel,
                                PS2Event *event,
                                GError **error) {
    gchar *start_pos;
    g_autofree gchar *current_line = NULL;
    GIOStatus rc;

    while ((rc = get_next_module_line(input_channel, "i8042", &current_line,
                                      &start_pos, error)) ==
            G_IO_STATUS_NORMAL) {
        if (parse_normal_event(start_pos, event, error))
            break;

        if (*error)
            return rc;

        if (parse_interrupt_without_data(start_pos, event, error))
            break;

        if (*error)
            return rc;

        g_clear_pointer(&current_line, g_free);
    }

    if (rc != G_IO_STATUS_NORMAL)
        return rc;

    return rc;
}

static gboolean record(GError **error) {
    GIOChannel *input_channel = g_io_channel_new_file(input_path, "r", error);
    gint keyboard_irq;
    PS2Event event;
    GIOStatus rc;

    if (!input_channel)
        return FALSE;

    keyboard_irq = get_keyboard_irq(input_channel, error);
    if (!keyboard_irq) {
        g_set_error(error, PS2EMU_ERROR, PS2_INPUT_UNEXPECTED_EOF,
                    "Reached unexpected EOF while trying to determine keyboard "
                    "IRQ");

        return FALSE;
    }

    while ((rc = get_next_event(input_channel, &event, error)) ==
           G_IO_STATUS_NORMAL) {
        /* The logic here is that we can only get two types of events from a
         * keyboard, kbd-data and interrupt. No other device sends kbd-data, so
         * we can judge if an event comes from a keyboard or not solely based
         * off that. With interrupts, we can tell if the interrupt is coming
         * from the keyboard or not by comparing the IRQ of the event to that of
         * the keyboard */
        if (!record_kbd) {
            if (event.type == PS2_EVENT_TYPE_INTERRUPT &&
                event.irq == keyboard_irq)
                continue;

            if (event.type == PS2_EVENT_TYPE_KBD_DATA)
                continue;
        }

        if (!record_aux) {
            if (event.type == PS2_EVENT_TYPE_INTERRUPT) {
                if (event.irq != keyboard_irq)
                    continue;
            }
            else if (event.type != PS2_EVENT_TYPE_KBD_DATA)
                continue;
        }

        printf("%s\n", event_to_string(&event));
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    GOptionContext *main_context =
        g_option_context_new("[file] - record PS/2 devices");
    gboolean rc;
    GError *error = NULL;
    gchar *record_kbd_str = NULL,
          *record_aux_str = NULL;

    GOptionEntry options[] = {
        { "record-kbd", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
          &record_kbd_str,
          "Enable recording of the KBD (keyboard) port, disabled by default",
          "<yes|no>" },
        { "record-aux", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
          &record_aux_str,
          "Enable recording of the AUX (auxillary, usually the port used for "
          "cursor devices) port, enabled by default",
          "<yes|no>" },
        { 0 }
    };

    g_option_context_add_main_entries(main_context, options, NULL);
    g_option_context_set_help_enabled(main_context, TRUE);
    g_option_context_set_description(main_context,
        "Allows the recording of all of the commands going in/out of a PS/2\n"
        "port, so that they may later be replayed using a virtual PS/2\n"
        "controller on another person's machine.\n"
        "\n"
        "If [file] is not specified, ps2emu-record will read from the kernel\n"
        "log (/dev/kmsg).\n"
        "\n"
        "By default, ps2emu-record does not record keyboard input. This is\n"
        "is because recording the user's keyboard input has the consequence\n"
        "of potentially recording sensitive information, such as a user's\n"
        "password (since the user usually needs to type their password into\n"
        "their keyboard to log in). If you need to record keyboard input,\n"
        "please read the documentation for this tool first.\n");

    rc = g_option_context_parse(main_context, &argc, &argv, &error);
    if (!rc) {
        exit_on_bad_argument(main_context, TRUE,
            "Invalid options: %s", error->message);
    }

    /* Don't record the keyboard if the user didn't explicitly enable it */
    if (!record_kbd_str || strcasecmp(record_kbd_str, "no") == 0)
        record_kbd = FALSE;
    else if (strcasecmp(record_kbd_str, "yes") == 0)
        record_kbd = TRUE;
    else {
        exit_on_bad_argument(main_context, TRUE,
            "Invalid value for --record-kbd: `%s`", record_kbd_str);
    }

    /* Record the AUX port unless the user explicitly disables it */
    if (!record_aux_str || strcasecmp(record_aux_str, "yes") == 0)
        record_aux = TRUE;
    else if (strcasecmp(record_aux_str, "no") == 0)
        record_aux = FALSE;
    else {
        exit_on_bad_argument(main_context, TRUE,
            "Invalid value for --record-aux: `%s`", record_aux_str);
    }

    /* Throw an error if recording of both KBD and AUX is disabled */
    if (!record_kbd && !record_aux)
        exit_on_bad_argument(main_context, FALSE, "Nothing to record!");

    if (argc > 1)
        input_path = argv[1];
    else {
        g_autofree gchar *cmdline = NULL;

        /* If we're reading from /dev/kmsg, we won't get anything useful if the
         * i8042.debug=1 parameter isn't passed to the kernel on boot. To help a
         * potentially confused user, warn them if i8042.debug=1 isn't on and
         * they're trying to read from the kernel log */
        if (g_file_get_contents("/proc/cmdline", &cmdline, NULL, &error)) {
            if (!strstr(cmdline, "i8042.debug=1")) {
                g_warning("You're trying to record PS/2 events from the kernel "
                          "log, but you didn't boot with `i8042.debug=1` "
                          "added to your kernel boot parameters. As a result, "
                          "it is very unlikely this application will work "
                          "properly. Please reboot your computer with this "
                          "option enabled.");
            }
        }
    }

    g_option_context_free(main_context);

    rc = record(&error);
    if (!rc) {
        fprintf(stderr, "Error: %s\n",
                error->message);
        return 1;
    }
}
