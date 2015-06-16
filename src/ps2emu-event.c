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
#include <string.h>
#include <glib.h>

gchar * ps2_event_to_string(PS2Event *event,
                            time_t time) {
    gchar *event_str,
          *comment;
    gchar origin,
          direction;

    if (event->type == PS2_EVENT_TYPE_KBD_DATA ||
        (event->type == PS2_EVENT_TYPE_INTERRUPT &&
         event->port == PS2_KEYBOARD_PORT))
        origin = 'K';
    else
        origin = 'A';

    if (event->type == PS2_EVENT_TYPE_INTERRUPT)
        direction = 'R'; /* received */
    else
        direction = 'S'; /* sent */

    /* Find the first paranthesis in the original message from dmesg, and
     * include that as a comment with the line */
    comment = g_strdup(strstr(event->original_line, "("));
    g_strchomp(comment);

    event_str = g_strdup_printf("%-10ld %c %c %.2hhx # %s",
                                time, origin, direction, event->data, comment);
    g_free(comment);

    return event_str;
}
