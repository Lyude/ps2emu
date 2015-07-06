/*
 * ps2emu-line.h
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

#ifndef __PS2EMU_LINE_H__
#define __PS2EMU_LINE_H__

#include <glib.h>

typedef enum {
    LINE_TYPE_EVENT   = 'E',
    LINE_TYPE_SECTION = 'S',
    LINE_TYPE_INVALID = -1
} LineType;

LineType get_line_type(gchar *line,
                       gchar **message_start,
                       GError **error);

#endif /* !__PS2EMU_LINE_H__ */
