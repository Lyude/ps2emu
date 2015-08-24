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

#ifndef __PS2EMU_LOG_H__
#define __PS2EMU_LOG_H__

#include <glib.h>

#include "ps2emu-misc.h"

#define PS2_KEYBOARD_PORT 0

typedef enum {
    PS2_EVENT_TYPE_COMMAND,
    PS2_EVENT_TYPE_PARAMETER,
    PS2_EVENT_TYPE_RETURN,
    PS2_EVENT_TYPE_KBD_DATA,
    PS2_EVENT_TYPE_INTERRUPT
} PS2EventType;

typedef enum {
    PS2_PORT_KBD,
    PS2_PORT_AUX
} PS2Port;

typedef struct {
    time_t        time;
    PS2EventType  type;
    guchar        data;
    PS2Port       origin;
    const gchar  *original_line;
} PS2Event;

typedef enum {
    LINE_TYPE_EVENT       = 'E',
    LINE_TYPE_SECTION     = 'S',
    LINE_TYPE_DEVICE_TYPE = 'T',
    LINE_TYPE_NOTE        = 'N',
    LINE_TYPE_INVALID     = -1
} LogLineType;

typedef struct {
    LogLineType type;
    union {
        PS2Event *ps2_event;
        gchar    *note;
    };
} LogLine;

typedef struct {
    GList   *init_section;
    GList   *main_section;

    PS2Port  port;
} ParsedLog;

typedef enum {
    SECTION_TYPE_INIT,
    SECTION_TYPE_MAIN,
    SECTION_TYPE_ERROR = -1,
} LogSectionType;

LogLineType log_get_line_type(gchar *line,
                              gchar **message_start,
                              GError **error);

void ps2_event_free(PS2Event *event);

gchar * ps2_event_to_string(PS2Event *event,
                            time_t start_time)
G_GNUC_WARN_UNUSED_RESULT G_GNUC_MALLOC;

PS2Event * ps2_event_from_line(const gchar *str,
                               int log_version,
                               GError **error)
G_GNUC_WARN_UNUSED_RESULT G_GNUC_MALLOC;

LogSectionType log_get_section_type_from_line(const gchar *line,
                                              GError **error);

ParsedLog *log_parse(GIOChannel *input_channel,
                     int log_version,
                     GError **error);

#endif /* !__PS2EMU_LOG_H__ */
