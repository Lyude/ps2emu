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
#include "misc.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib.h>

gchar * ps2_event_to_string(PS2Event *event,
                            time_t time) {
    gchar *event_str,
          *comment;
    gchar origin,
          direction;

    if (event->type == PS2_EVENT_TYPE_KBD_DATA ||
        (event->type == PS2_EVENT_TYPE_INTERRUPT &&
         event->origin == PS2_EVENT_ORIGIN_KEYBOARD))
        origin = 'K';
    else
        origin = 'A';

    if (event->type == PS2_EVENT_TYPE_INTERRUPT ||
        event->type == PS2_EVENT_TYPE_RETURN)
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

PS2Event * ps2_event_from_line(const gchar *str,
                               GError **error) {
    gchar const *str_start = &str[strspn(str, " \t")];
    int parsed_count;
    char origin_char,
         direction_char;
    PS2Event *new_event = NULL;

    if (*str_start == '#')
        return FALSE;

    new_event = g_slice_alloc(sizeof(PS2Event));

    errno = 0;
    parsed_count = sscanf(str_start, "%ld %c %c %hhx",
                          &new_event->time, &origin_char, &direction_char,
                          &new_event->data);
    if (errno != 0 || parsed_count != 4) {
        g_set_error(error, PS2EMU_ERROR, PS2_ERROR_INPUT,
                    "Invalid event line '%s'", str);
        goto error;
    }

    if (origin_char == 'K')
        new_event->origin = PS2_EVENT_ORIGIN_KEYBOARD;
    else if (origin_char == 'A')
        new_event->origin = PS2_EVENT_ORIGIN_AUX;
    else {
        g_set_error(error, PS2EMU_ERROR, PS2_ERROR_INPUT,
                    "Invalid event origin '%c' from '%s'", origin_char, str);
        goto error;
    }

    if (direction_char == 'S')
        new_event->type = PS2_EVENT_TYPE_PARAMETER;
    /* It might also be a return, but the serio port doesn't care either way */
    else if (direction_char == 'R')
        new_event->type = PS2_EVENT_TYPE_INTERRUPT;
    else {
        g_set_error(error, PS2EMU_ERROR, PS2_ERROR_INPUT,
                    "Invalid event direction '%c' from '%s'",
                    direction_char, str);
        goto error;
    }

    return new_event;

error:
    if (new_event)
        g_slice_free(PS2Event, new_event);

    return NULL;
}
