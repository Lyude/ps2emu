/*
 * ps2emu-event.h
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

#ifndef __PS2EMU_EVENT_H__
#define __PS2EMU_EVENT_H__

#include <glib.h>

#define PS2_KEYBOARD_PORT 0

typedef enum {
    PS2_EVENT_TYPE_COMMAND,
    PS2_EVENT_TYPE_PARAMETER,
    PS2_EVENT_TYPE_RETURN,
    PS2_EVENT_TYPE_KBD_DATA,
    PS2_EVENT_TYPE_INTERRUPT
} PS2EventType;

typedef struct {
    PS2EventType  type;
    guchar        data;
    enum {
        PS2_EVENT_ORIGIN_KEYBOARD,
        PS2_EVENT_ORIGIN_AUX
    } origin;
    const gchar  *original_line;
} PS2Event;

gchar * ps2_event_to_string(PS2Event *event,
                            time_t start_time)
G_GNUC_WARN_UNUSED_RESULT G_GNUC_MALLOC;

#endif /* !__PS2EMU_EVENT_H__ */
