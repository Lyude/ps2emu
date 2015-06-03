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

#include "misc.h"

typedef enum {
    PS2_INPUT_UNEXPECTED_EOF
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
    g_autoptr(gchar) search_str = g_strdup_printf("%s: ", module_name);
    gchar *current_line;
    GIOStatus rc;

    while ((rc = g_io_channel_read_line(input_channel, &current_line, NULL,
                                        NULL, error)) == G_IO_STATUS_NORMAL) {
        *start_pos = strstr(current_line, search_str);
        if (!*start_pos) {
            g_free(current_line);
            continue;
        }

        /* Move the start position after the initial 'i8042: ' */
        *start_pos += strlen(search_str);
        *output = current_line;

        break;
    }

    return rc;
}

static gint get_keyboard_irq(GIOChannel *input_channel,
                             GError **error) {
    GIOStatus rc;
    g_autoptr(gchar) current_line;
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

static gboolean record(GError **error) {
    GIOChannel *input_channel = g_io_channel_new_file(input_path, "r", error);
    gint keyboard_irq;

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
