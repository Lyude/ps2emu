/*
 * ps2emu-section.c
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

#include "ps2emu-section.h"
#include "ps2emu-misc.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

LogSectionType section_type_from_line(const gchar *line,
                                      GError **error) {
    int parsed_count;
    gchar *section_string;
    LogSectionType type;

    errno = 0;
    parsed_count = sscanf(line, "%ms\n", &section_string);
    if (errno || parsed_count != 1) {
        g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                            "Invalid section line");
        return -1;
    }

    if (strcmp(section_string, "Init") == 0)
        type = SECTION_TYPE_INIT;
    else if (strcmp(section_string, "Main") == 0)
        type = SECTION_TYPE_MAIN;
    else {
        g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                    "Invalid section type `%s`", section_string);

        g_free(section_string);
        return -1;
    }

    g_free(section_string);
    return type;
}


