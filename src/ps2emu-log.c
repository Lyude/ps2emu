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

#include "ps2emu-log.h"
#include "ps2emu-misc.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib.h>

#define LINE_TYPE_LENGTH sizeof("X:")

void ps2_event_free(PS2Event *event) {
    g_slice_free(PS2Event, event);
}

gchar * ps2_event_to_string(PS2Event *event,
                            time_t time) {
    gchar *event_str,
          *comment;
    gchar direction;

    if (event->type == PS2_EVENT_TYPE_INTERRUPT ||
        event->type == PS2_EVENT_TYPE_RETURN)
        direction = 'R'; /* received */
    else
        direction = 'S'; /* sent */

    /* Find the first paranthesis in the original message from dmesg, and
     * include that as a comment with the line */
    comment = g_strdup(strstr(event->original_line, "("));
    g_strchomp(comment);

    event_str = g_strdup_printf("E: %-10ld %c %.2hhx # %s\n",
                                time, direction, event->data, comment);
    g_free(comment);

    return event_str;
}

PS2Event * ps2_event_from_line(const gchar *str,
                               int log_version,
                               GError **error) {
    gchar const *str_start = &str[strspn(str, " \t")];
    int parsed_count;
    char origin_char,
         direction_char;
    PS2Event *new_event = NULL;

    if (*str_start == '#')
        return NULL;

    new_event = g_slice_alloc(sizeof(PS2Event));

    errno = 0;

    /* In the first log version, we originally specified the origin device each
     * event was coming from. This ended up being uneccessary, and as of log
     * version 1 we no longer record this */
    if (log_version == 0) {
        parsed_count = sscanf(str_start, "%ld %c %c %hhx",
                              &new_event->time, &origin_char, &direction_char,
                              &new_event->data);
        if (errno != 0 || parsed_count != 4) {
            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Invalid event line '%s'", str);
            goto error;
        }

        if (origin_char == 'K')
            new_event->origin = PS2_PORT_KBD;
        else if (origin_char == 'A')
            new_event->origin = PS2_PORT_AUX;
        else {
            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Invalid event origin '%c' from '%s'", origin_char,
                        str);
            goto error;
        }
    } else {
        parsed_count = sscanf(str_start, "%ld %c %hhx",
                              &new_event->time, &direction_char,
                              &new_event->data);
        if (errno != 0 || parsed_count != 3) {
            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Invalid event line '%s'", str);
            goto error;
        }
    }

    if (direction_char == 'S')
        new_event->type = PS2_EVENT_TYPE_PARAMETER;
    /* It might also be a return, but the serio port doesn't care either way */
    else if (direction_char == 'R')
        new_event->type = PS2_EVENT_TYPE_INTERRUPT;
    else {
        g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                    "Invalid event direction '%c' from '%s'",
                    direction_char, str);
        goto error;
    }

    return new_event;

error:
    if (new_event)
        ps2_event_free(new_event);

    return NULL;
}

LogLineType log_get_line_type(gchar *line,
                              gchar **message_start,
                              GError **error) {
    LogLineType type;
    gchar type_char;
    int parse_count;

    errno = 0;
    parse_count = sscanf(line, "%1c:", &type_char);
    if (errno || parse_count != 1)
        return -1;

    switch (type_char) {
        case LINE_TYPE_EVENT:
        case LINE_TYPE_SECTION:
        case LINE_TYPE_DEVICE_TYPE:
        case LINE_TYPE_NOTE:
            type = type_char;
            break;
        default:
            *message_start = NULL;

            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Invalid line type `%1c`", type_char);
            return -1;
    }

    *message_start = line + LINE_TYPE_LENGTH;

    return type;
}

ParsedLog *log_parse(GIOChannel *input_channel,
                     int log_version,
                     GError **error) {
    gchar *line;
    LogLineType line_type;
    LogLine *log_line;
    PS2Event *event;
    LogSectionType section_type;
    gchar *msg_start;
    GList **section_dest;
    ParsedLog *parsed_log;
    GIOStatus rc;

    parsed_log = g_new0(ParsedLog, 1);

    /* We can't reliably play anything back from older logs except for
     * touchpads, so just automatically set the port type to AUX */
    if (log_version < 1)
        parsed_log->port = PS2_PORT_AUX;

    while ((rc = g_io_channel_read_line(input_channel, &line, NULL, NULL,
                                        error)) == G_IO_STATUS_NORMAL) {
        g_strchug(line);

        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (log_version < 1) {
            line_type = LINE_TYPE_EVENT;
            msg_start = line;
            section_dest = &parsed_log->main_section;
        } else
            line_type = log_get_line_type(line, &msg_start, error);

        switch (line_type) {
            case LINE_TYPE_DEVICE_TYPE:
                switch (msg_start[0]) {
                    case 'K':
                        parsed_log->port = PS2_PORT_KBD;
                        break;
                    case 'A':
                        parsed_log->port = PS2_PORT_AUX;
                        break;
                    default:
                        g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                                    "Invalid device type '%c'\n", msg_start[0]);
                        goto error;
                }

                break;
            case LINE_TYPE_EVENT:
                event = ps2_event_from_line(msg_start, log_version, error);

                if (!event) {
                    if (!*error)
                        continue;
                    else
                        goto error;
                }

                log_line = g_slice_alloc(sizeof(LogLine));
                *log_line = (LogLine) {
                    .type = line_type,
                    .ps2_event = event,
                };

                *section_dest = g_list_prepend(*section_dest, log_line);
                break;
            case LINE_TYPE_SECTION:
                section_type = log_get_section_type_from_line(msg_start, error);

                switch (section_type) {
                    case SECTION_TYPE_INIT:
                        section_dest = &parsed_log->init_section;
                        break;
                    case SECTION_TYPE_MAIN:
                        section_dest = &parsed_log->main_section;
                        break;
                    case SECTION_TYPE_ERROR:
                        goto error;
                }
                break;
            case LINE_TYPE_NOTE:
                /* Remove the newline character from the end of the note */
                g_strchomp(msg_start);

                if (strlen(msg_start) == 0) {
                    g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                                        "Note is empty");
                    goto error;
                }

                log_line = g_slice_alloc(sizeof(LogLine));
                *log_line = (LogLine) {
                    .type = line_type,
                    .note = g_strdup(msg_start),
                };

                *section_dest = g_list_prepend(*section_dest, log_line);
                break;
            case LINE_TYPE_INVALID:
                goto error;
        }
    }
    if (rc != G_IO_STATUS_EOF)
        goto error;

    if (log_version >= 1) {
        if (parsed_log->init_section)
            parsed_log->init_section = g_list_reverse(parsed_log->init_section);
    }

    if (parsed_log->main_section)
        parsed_log->main_section = g_list_reverse(parsed_log->main_section);

    return parsed_log;

error:
    if (parsed_log->init_section)
        g_list_free_full(parsed_log->init_section, g_free);
    if (parsed_log->main_section)
        g_list_free_full(parsed_log->main_section, g_free);

    g_free(parsed_log);
    return NULL;
}

gint log_parse_version(GIOChannel *input_channel,
                       GError **error) {
    gchar *line = NULL;
    int log_version,
        parse_count;
    GIOStatus rc;

    rc = g_io_channel_read_line(input_channel, &line, NULL, NULL, error);
    if (rc != G_IO_STATUS_NORMAL) {
        if (rc == G_IO_STATUS_EOF) {
            g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_NO_EVENTS,
                                "Reached unexpected EOF");
        }

        goto error;
    }

    errno = 0;
    parse_count = sscanf(line, "# ps2emu-record V%d\n", &log_version);
    if (parse_count == 0 || errno != 0) {
        g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                            "Invalid log file version");
        goto error;
    }

    g_free(line);
    return log_version;

error:
    g_free(line);
    return -1;
}

LogSectionType log_get_section_type_from_line(const gchar *line,
                                              GError **error) {
    int parsed_count;
    gchar *section_string;
    LogSectionType type;

    errno = 0;
    parsed_count = sscanf(line, "%ms\n", &section_string);
    if (errno || parsed_count != 1) {
        g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                            "Invalid section line");
        return -1;
    }

    if (strcmp(section_string, "Init") == 0)
        type = SECTION_TYPE_INIT;
    else if (strcmp(section_string, "Main") == 0)
        type = SECTION_TYPE_MAIN;
    else {
        g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                    "Invalid section type `%s`", section_string);

        g_free(section_string);
        return -1;
    }

    g_free(section_string);
    return type;
}
