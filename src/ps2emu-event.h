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

typedef enum {
    PS2_EVENT_TYPE_COMMAND,
    PS2_EVENT_TYPE_PARAMETER,
    PS2_EVENT_TYPE_RETURN,
    PS2_EVENT_TYPE_KBD_DATA,
    PS2_EVENT_TYPE_INTERRUPT
} PS2EventType;

typedef struct {
    time_t       time;
    PS2EventType type;
    gboolean     has_data;
    guchar       data;
    gushort      port;
    gushort      irq;
} PS2Event;

static inline gchar* ps2_event_type_to_string(PS2EventType type) {
    gchar *type_str;

    switch (type) {
        case PS2_EVENT_TYPE_COMMAND:
            type_str = "Command";
            break;
        case PS2_EVENT_TYPE_PARAMETER:
            type_str = "Parameter";
            break;
        case PS2_EVENT_TYPE_RETURN:
            type_str = "Return";
            break;
        case PS2_EVENT_TYPE_KBD_DATA:
            type_str = "Kbd-data";
            break;
        case PS2_EVENT_TYPE_INTERRUPT:
            type_str = "Interrupt";
            break;
    }

    return type_str;
}

gchar * ps2_event_to_string(PS2Event *event)
G_GNUC_WARN_UNUSED_RESULT G_GNUC_MALLOC;

#endif /* !__PS2EMU_EVENT_H__ */
