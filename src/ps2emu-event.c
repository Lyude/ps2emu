/*
 * ps2emu-event.c
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

#include "ps2emu-event.h"

#include <stdio.h>
#include <glib.h>

gchar * ps2_event_to_string(PS2Event *event) {
    gchar *event_str;

    if (event->type == PS2_EVENT_TYPE_INTERRUPT) {
        event_str = g_strdup_printf("%-10ld %-9s %.2hhx %2hd %2hd",
                                    event->time,
                                    ps2_event_type_to_string(event->type),
                                    event->data, event->port, event->irq);
    }
    else {
        event_str = g_strdup_printf("%-10ld %-9s %.2hhx",
                                    event->time,
                                    ps2_event_type_to_string(event->type),
                                    event->data);
    }

    return event_str;
}
