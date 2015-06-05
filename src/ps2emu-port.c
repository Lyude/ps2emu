/*
 * ps2emu-port.c
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

#include "ps2emu-port.h"

#include <glib.h>

gchar * ps2_port_to_string(const PS2Port *port) {
    gchar *ps2_port_str;

    ps2_port_str = g_strdup_printf("%-10s %-12s %2.2hd",
                                   "Port", port->name, port->irq);

    return ps2_port_str;
}
