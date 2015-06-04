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

static GIOStatus get_next_event(GIOChannel *input_channel,
                                PS2Event *event,
                                GError **error) {
    gchar *start_pos;
    g_autofree gchar *current_line = NULL;
    g_autofree gchar *type_str = NULL;
    g_auto(GStrv) type_str_args = NULL;
    int type_str_argc,
        parsed_count;
    GIOStatus rc;

    while ((rc = get_next_module_line(input_channel, "i8042", &current_line,
                                      &start_pos, error)) ==
            G_IO_STATUS_NORMAL) {
        errno = 0;

        parsed_count = sscanf(start_pos,
                              "[%ld] %hhx %*1[-<]%*1[->] i8042 (%m[^)])\n",
                              &event->time, &event->data, &type_str);

        if (errno == 0 && parsed_count == 3)
            break;

        g_clear_pointer(&current_line, g_free);
    }

    if (rc != G_IO_STATUS_NORMAL)
        return rc;

    type_str_args = g_strsplit(type_str, ",", 0);

    if (strcmp(type_str_args[0], "interrupt") == 0) {
        event->type = PS2_EVENT_TYPE_INTERRUPT;

        type_str_argc = g_strv_length(type_str_args);
        if (type_str_argc < 3) {
            g_set_error(error, PS2EMU_ERROR, PS2_INPUT_ERROR,
                        "Got interrupt event, but had less arguments then "
                        "expected");
            return rc;
        }

        errno = 0;
        event->irq = strtol(type_str_args[2], NULL, 10);
        if (errno != 0) {
            g_set_error(error, PS2EMU_ERROR, PS2_INPUT_ERROR,
                        "Failed to parse IRQ from interrupt event: %s\n",
                        strerror(errno));
            return rc;
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

    if (rc != G_IO_STATUS_NORMAL)
        return rc;

    return rc;
}

static gchar* event_type_to_string(PS2EventType type) {
    gchar *type_str;

    switch (type) {
        case PS2_EVENT_TYPE_COMMAND:
            type_str = "Command";
            break;
        case PS2_EVENT_TYPE_PARAMETER:
            type_str = "Parameter";
            break;
        case PS2_EVENT_TYPE_RETURN:
            type_str = "Return";
            break;
        case PS2_EVENT_TYPE_KBD_DATA:
            type_str = "Kbd-data";
            break;
        case PS2_EVENT_TYPE_INTERRUPT:
            type_str = "Interrupt";
            break;
    }

    return type_str;
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

    printf("Keyboard IRQ: %d\n", keyboard_irq);

    while ((rc = get_next_event(input_channel, &event, error)) ==
            G_IO_STATUS_NORMAL) {
        if (*error)
            return FALSE;

        printf("Time: %ld\n"
               "\tType: %s\n"
               "\tData: %hhx\n",
               event.time, event_type_to_string(event.type), event.data);
        if (event.type == PS2_EVENT_TYPE_INTERRUPT)
            printf("\tIRQ: %d\n", event.irq);
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    GOptionContext *main_context =
        g_option_context_new("[file] - record PS/2 devices");
    gboolean rc;
    GError *error = NULL;

    g_option_context_set_help_enabled(main_context, TRUE);
    g_option_context_set_summary(main_context,
        "Allows the recording of all of the commands going in/out of a PS/2\n"
        "port, so that they may later be replayed using a virtual PS/2\n"
        "controller on another person's machine.\n"
        "\n"
        "If [file] is not specified, ps2emu-record will read from the kernel\n"
        "log (/dev/kmsg).");

    rc = g_option_context_parse(main_context, &argc, &argv, &error);
    if (!rc) {
        fprintf(stderr, "Invalid options: %s\n", error->message);
        exit(1);
    }

    if (argc > 1)
        input_path = argv[1];

    g_option_context_free(main_context);

    rc = record(&error);
    if (!rc) {
        fprintf(stderr, "Error: %s\n",
                error->message);
        return 1;
    }
}
