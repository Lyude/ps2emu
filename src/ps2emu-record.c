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
#include "ps2emu-port.h"

typedef enum {
    PS2_ERROR_INPUT,
    PS2_ERROR_NO_KBD_DEFINITION_FOUND
} PS2Error;

typedef struct {
    GQuark type;

    union {
        PS2Event event;
        PS2Port port;
    };
} LogMsgParseResult;

static char *input_path = "/dev/kmsg";

static gboolean record_kbd;
static gboolean record_aux;

static gshort kbd_irq = -1;

#define I8042_OUTPUT (g_quark_from_static_string("i8042: "))
#define SERIO_OUTPUT (g_quark_from_static_string("serio: "))

static void log_msg_result_clear(LogMsgParseResult *res) {
    if (res->type == SERIO_OUTPUT)
        g_free(res->port.name);
}

static GIOStatus get_next_module_line(GIOChannel *input_channel,
                                      GQuark *match,
                                      gchar **output,
                                      gchar **start_pos,
                                      GError **error) {
    static const gchar *search_strings[] = { "i8042: ", "serio: " };
    int index;
    gchar *current_line;
    GIOStatus rc;

    while ((rc = g_io_channel_read_line(input_channel, &current_line, NULL,
                                        NULL, error)) == G_IO_STATUS_NORMAL) {
        for (index = 0; index < G_N_ELEMENTS(search_strings); index++) {
            *start_pos = strstr(current_line, search_strings[index]);
            if (*start_pos)
                break;
        }
        if (*start_pos)
            break;

        g_free(current_line);
    }

    if (rc != G_IO_STATUS_NORMAL) {
        return rc;
    }

    /* Move the start position after the initial 'i8042: ' */
    *start_pos += strlen(search_strings[index]);
    *output = current_line;

    *match = g_quark_from_static_string(search_strings[index]);

    return rc;
}

static gboolean parse_normal_event(const gchar *start_pos,
                                   PS2Event *event,
                                   GError **error) {
    __label__ error;
    gchar *type_str = NULL;
    gchar **type_str_args = NULL;
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
            g_set_error(error, PS2EMU_ERROR, PS2_ERROR_INPUT,
                        "Got interrupt event, but had less arguments then "
                        "expected");
            goto error;
        }

        errno = 0;
        event->irq = strtol(type_str_args[2], NULL, 10);
        if (errno != 0) {
            g_set_error(error, PS2EMU_ERROR, PS2_ERROR_INPUT,
                        "Failed to parse IRQ from interrupt event: %s\n",
                        strerror(errno));
            goto error;
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

    g_free(type_str);
    g_strfreev(type_str_args);

    return TRUE;

error:
    g_free(type_str);
    g_strfreev(type_str_args);

    return FALSE;
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

static gboolean parse_port_definition(const gchar *start_pos,
                                      PS2Port *port,
                                      GError **error) {
    gchar *name = NULL;
    gint parsed_count;
    gushort irq;

    errno = 0;
    parsed_count = sscanf(start_pos,
                          "i8042 %ms port at %*x,%*x irq %hd\n",
                          &name, &irq);

    if (errno != 0 || parsed_count != 2) {
        g_free(name);
        return FALSE;
    }

    *port = (PS2Port) {
        .irq = irq,
        .name = name
    };

    return TRUE;
}

static GIOStatus parse_next_message(GIOChannel *input_channel,
                                    LogMsgParseResult *res,
                                    GError **error) {
    __label__ fail;
    gchar *start_pos;
    gchar *current_line = NULL;
    GIOStatus rc;

    while ((rc = get_next_module_line(input_channel, &res->type, &current_line,
                                      &start_pos, error)) ==
            G_IO_STATUS_NORMAL) {
        if (res->type == I8042_OUTPUT) {
            if (parse_normal_event(start_pos, &res->event, error))
                break;

            if (*error)
                return rc;

            if (parse_interrupt_without_data(start_pos, &res->event, error))
                break;

            if (*error)
                goto fail;
        }
        else if (res->type == SERIO_OUTPUT) {
            if (parse_port_definition(start_pos, &res->port, error))
                break;

            if (*error)
                goto fail;
        }

        g_free(current_line);
    }

    if (rc != G_IO_STATUS_NORMAL)
        return rc;

    res->type = res->type;

fail:
    g_free(current_line);

    return rc;
}

static gboolean process_event(PS2Event *event,
                              GError **error) {
    gchar *event_str = NULL;

    /* The port information for the KBD port always comes before the actual
     * interrupts start, if we're starting to get interrupts and we haven't
     * actually figured out what the IRQ for the KBD port is, that means it's no
     * longer in the buffer, so we can't reliably record anything */
     if (kbd_irq == -1 && event->type == PS2_EVENT_TYPE_INTERRUPT) {
        g_set_error_literal(error, PS2EMU_ERROR,
                            PS2_ERROR_NO_KBD_DEFINITION_FOUND,
                            "Interrupts received before KBD port definition");
        return FALSE;
    }

    /* The logic here is that we can only get two types of events from a
     * keyboard, kbd-data and interrupt. No other device sends kbd-data, so we
     * can judge if an event comes from a keyboard or not solely based off that.
     * With interrupts, we can tell if the interrupt is coming from the keyboard
     * or not by comparing the IRQ of the event to that of the KBD port */
    if (!record_kbd) {
        if (event->type == PS2_EVENT_TYPE_INTERRUPT &&
            event->irq == kbd_irq)
            return TRUE;

        if (event->type == PS2_EVENT_TYPE_KBD_DATA)
            return TRUE;
    }

    if (!record_aux) {
        if (event->type == PS2_EVENT_TYPE_INTERRUPT) {
            if (event->irq != kbd_irq)
                return TRUE;
        }
        else if (event->type != PS2_EVENT_TYPE_KBD_DATA)
            return TRUE;
    }

    event_str = ps2_event_to_string(event);
    printf("%s\n", event_str);

    g_free(event_str);

    return TRUE;
}

static void process_new_port(PS2Port *port) {
    gchar *port_str = ps2_port_to_string(port);

    if (strcmp(port->name, "KBD") == 0)
        kbd_irq = port->irq;

    printf("%s\n", port_str);

    g_free(port_str);
}

static gboolean record(GError **error) {
    __label__ log_msg_error;
    GIOChannel *input_channel = g_io_channel_new_file(input_path, "r", error);
    LogMsgParseResult res;
    GIOStatus rc;

    if (!input_channel)
        return FALSE;

    while ((rc = parse_next_message(input_channel, &res, error)) ==
           G_IO_STATUS_NORMAL) {
        if (res.type == I8042_OUTPUT) {
            if (!process_event(&res.event, error))
                goto log_msg_error;
        }
        else /* res.type == SERIO_OUTPUT */
            process_new_port(&res.port);

        log_msg_result_clear(&res);
    }

    return TRUE;

log_msg_error:
    log_msg_result_clear(&res);

    return FALSE;
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
        gchar *cmdline = NULL;

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

        g_free(cmdline);
    }

    g_option_context_free(main_context);

    rc = record(&error);
    if (!rc) {
        fprintf(stderr, "Error: %s\n",
                error->message);

        if (g_error_matches(error, PS2EMU_ERROR,
                            PS2_ERROR_NO_KBD_DEFINITION_FOUND)) {
            fprintf(stderr,
                "Usually, this error is caused by having the system run for\n"
                "too long before rebooting. Normally this isn't an issue,\n"
                "however the kernel's buffer is limited, and messages can be\n"
                "lost after a certain amount of messages are posted to it.\n"
                "This results in the beginning of the PS/2 data to be thrown\n"
                "out, and that data is needed for this tool to work properly.\n"
                "\n"
                "Usually, rebooting your computer with `i8042.debug=1` and\n"
                "trying to use this tool again will fix the problem.\n");
        }

        return 1;
    }
}
