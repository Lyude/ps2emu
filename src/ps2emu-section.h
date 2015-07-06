/*
 * ps2emu-section.h
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

#ifndef __PS2EMU_SECTION_H__
#define __PS2EMU_SECTION_H__

#include <glib.h>

typedef enum {
    SECTION_TYPE_INIT,
    SECTION_TYPE_MAIN,
    SECTION_TYPE_ERROR = -1,
} LogSectionType;

LogSectionType section_type_from_line(const gchar *line,
                                      GError **error);

#endif /* !__PS2EMU_SECTION_H__ */
