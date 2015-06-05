/*
 * ps2emu-port.h
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

#ifndef __PS2EMU_PORT_H__
#define __PS2EMU_PORT_H__

#include <glib.h>
#include <glib-object.h>

typedef struct {
    gchar   *name;
    gushort  irq;
} PS2Port;

gchar * ps2_port_to_string(const PS2Port *port)
G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

#endif /* !__PS2EMU_PORT_H__ */
