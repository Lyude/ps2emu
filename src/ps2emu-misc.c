/*
 * ps2emu-misc.c
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

#include <glib.h>

#include "ps2emu-misc.h"
#include "config.h"

gboolean print_version(const gchar *option_name,
                       const gchar *value,
                       gpointer data,
                       GError **error) {
    printf("ps2emu userspace tools v" VERSION "\n");
    exit(0);
}
