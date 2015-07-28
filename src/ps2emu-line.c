/*
 * ps2emu-line.c
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

#include "ps2emu-line.h"
#include "ps2emu-misc.h"

#include <stdio.h>
#include <errno.h>
#include <glib.h>

#define LINE_TYPE_LENGTH sizeof("X:")

LineType get_line_type(gchar *line,
                       gchar **message_start,
                       GError **error) {
    LineType type;
    gchar type_char;
    int parse_count;

    errno = 0;
    parse_count = sscanf(line, "%1c:", &type_char);
    if (errno || parse_count != 1)
        return -1;

    switch (type_char) {
        case LINE_TYPE_EVENT:
        case LINE_TYPE_SECTION:
        case LINE_TYPE_DEVICE_TYPE:
        case LINE_TYPE_NOTE:
            type = type_char;
            break;
        default:
            *message_start = NULL;

            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Invalid line type `%1c`", type_char);
            return -1;
    }

    *message_start = line + LINE_TYPE_LENGTH;

    return type;
}

