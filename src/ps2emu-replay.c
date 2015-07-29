/*
 * ps2emu-replay.c
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
#include "ps2emu-misc.h"
#include "ps2emu-line.h"
#include "ps2emu-section.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <errno.h>
#include <linux/serio.h>
#include <userio.h>

#define PS2EMU_MIN_EVENT_DELAY (0.5 * G_USEC_PER_SEC)

typedef struct {
    LineType type;
    union {
        PS2Event *ps2_event;
        gchar *note;
    };
} LogLine;

static GList *event_list;

static GList *init_event_list;
static GList *main_event_list;

static PS2Port replay_device_type = PS2_PORT_AUX;

static GIOStatus send_userio_cmd(GIOChannel *userio_channel,
                                 guint8 type,
                                 guint8 data,
                                 GError **error) {
    GIOStatus rc;
    struct userio_cmd cmd = {
        .type = type,
        .data = data,
    };

    rc = g_io_channel_write_chars(userio_channel, (gchar*)&cmd, sizeof(cmd),
                                  NULL, error);
    return rc;
}

static gboolean simulate_interrupt(GIOChannel *userio_channel,
                                   time_t start_time,
                                   time_t offset,
                                   PS2Event *event,
                                   gboolean verbose,
                                   GError **error) {
    time_t current_time;
    GIOStatus rc;

    current_time = g_get_monotonic_time() - start_time + offset;
    if (current_time < event->time)
        g_usleep(event->time - current_time);

    if (verbose)
        printf("Send\t-> %.2hhx\n", event->data);

    rc = send_userio_cmd(userio_channel, USERIO_CMD_SEND_INTERRUPT,
                         event->data, error);
    if (rc != G_IO_STATUS_NORMAL)
        return FALSE;

    return TRUE;
}

static gboolean simulate_receive(GIOChannel *userio_channel,
                                 PS2Event *event,
                                 gboolean verbose,
                                 GError **error) {
    guchar data;
    gsize count;
    GIOStatus rc;

    rc = g_io_channel_read_chars(userio_channel, (gchar*)&data,
                                 sizeof(event->data), &count, error);

    if (rc != G_IO_STATUS_NORMAL)
        return FALSE;

    if (verbose && event->data == data)
        printf("Receive\t<- %.2hhx\n", data);
    else if (event->data != data)
        printf("Expected %.2hhx, received %2hhx\n", event->data, data);

    return TRUE;
}

static gboolean replay_line_list(GIOChannel *userio_channel,
                                 GList *event_list,
                                 time_t max_wait,
                                 time_t note_delay,
                                 gboolean verbose,
                                 GError **error) {
    LogLine *log_line;
    const time_t start_time = g_get_monotonic_time();
    long offset = 0;

    for (GList *l = event_list; l != NULL; l = l->next) {
        log_line = l->data;

        if (log_line->type == LINE_TYPE_NOTE) {
            printf("User note: %s\n",
                   log_line->note);

            g_usleep(note_delay);
            offset -= note_delay;

            continue;
        }

        if (max_wait && l->prev) {
            LogLine *last_line = l->prev->data;
            time_t wait_time =
                log_line->ps2_event->time - last_line->ps2_event->time;

            /* If necessary, time-travel to the future */
            if (wait_time > max_wait)
                offset += wait_time - max_wait;
        }

        if (log_line->ps2_event->type == PS2_EVENT_TYPE_INTERRUPT) {
            if (!simulate_interrupt(userio_channel, start_time, offset,
                                    log_line->ps2_event, verbose, error))
                return FALSE;
        } else {
            if (!simulate_receive(userio_channel, log_line->ps2_event, verbose,
                                  error))
                return FALSE;
        }
    }

    return TRUE;
}

static gboolean parse_events(GIOChannel *input_channel,
                             int log_version,
                             GError **error) {
    gchar *line;
    LineType line_type;
    LogLine *log_line;
    PS2Event *event;
    LogSectionType section_type;
    gchar *msg_start;
    GList **event_list_dest;
    GIOStatus rc;

    while ((rc = g_io_channel_read_line(input_channel, &line, NULL, NULL,
                                        error)) == G_IO_STATUS_NORMAL) {
        g_strchug(line);

        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (log_version < 1) {
            line_type = LINE_TYPE_EVENT;
            msg_start = line;
            event_list_dest = &event_list;
        } else
            line_type = get_line_type(line, &msg_start, error);

        switch (line_type) {
            case LINE_TYPE_DEVICE_TYPE:
                switch (msg_start[0]) {
                    case 'K':
                        replay_device_type = PS2_PORT_KBD;
                        break;
                    case 'A':
                        replay_device_type = PS2_PORT_AUX;
                        break;
                    default:
                        g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                                    "Invalid device type '%c'\n", msg_start[0]);
                        return FALSE;
                }

                break;
            case LINE_TYPE_EVENT:
                event = ps2_event_from_line(msg_start, log_version, error);

                if (!event) {
                    if (!*error)
                        continue;
                    else
                        return FALSE;
                }

                log_line = g_slice_alloc(sizeof(LogLine));
                *log_line = (LogLine) {
                    .type = line_type,
                    .ps2_event = event,
                };

                *event_list_dest = g_list_prepend(*event_list_dest, log_line);
                break;
            case LINE_TYPE_SECTION:
                section_type = section_type_from_line(msg_start, error);

                switch (section_type) {
                    case SECTION_TYPE_INIT:
                        event_list_dest = &init_event_list;
                        break;
                    case SECTION_TYPE_MAIN:
                        event_list_dest = &main_event_list;
                        break;
                    case SECTION_TYPE_ERROR:
                        return FALSE;
                }
                break;
            case LINE_TYPE_NOTE:
                /* Remove the newline character from the end of the note */
                g_strchomp(msg_start);

                if (strlen(msg_start) == 0) {
                    g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                                        "Note is empty");
                    return FALSE;
                }

                log_line = g_slice_alloc(sizeof(LogLine));
                *log_line = (LogLine) {
                    .type = line_type,
                    .note = g_strdup(msg_start),
                };

                *event_list_dest = g_list_prepend(*event_list_dest, log_line);
                break;
            case LINE_TYPE_INVALID:
                return FALSE;
        }
    }
    if (rc != G_IO_STATUS_EOF)
        return FALSE;

    if (log_version >= 1) {
        if (init_event_list)
            init_event_list = g_list_reverse(init_event_list);
        if (main_event_list)
            main_event_list = g_list_reverse(main_event_list);
    } else
        event_list = g_list_reverse(event_list);

    return TRUE;
}

static int parse_log_version(GIOChannel *input_channel,
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

gint main(gint argc,
          gchar *argv[]) {
    GOptionContext *main_context =
        g_option_context_new("<event_log> - replay PS/2 devices");
    GIOChannel *input_channel,
               *userio_channel;
    GIOStatus rc;
    int log_version;
    time_t max_wait = 0,
           event_delay = 0,
           note_delay = 0;
    GError *error = NULL;
    gboolean no_events = FALSE,
             keep_running = FALSE,
             verbose = FALSE;
    __u8 port_type;

    GOptionEntry options[] = {
        { "version", 'V', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          print_version, "Show the version of the application", NULL },
        { "verbose", 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
          &verbose, "Be more verbose when replaying events", NULL },
        { "no-events", 'n', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
          &no_events, "Don't replay events, just initialize the device", NULL },
        { "keep-running", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
          &keep_running, "Don't exit immediately after replay finishes", NULL },
        { "max-wait", 'w', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
          &max_wait, "Don't wait for longer then n seconds between events",
          "n", },
        { "event-delay", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
          &event_delay, "Wait n seconds after init before playing events",
          "n" },
        { "note-delay", 'D', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
          &note_delay, "Wait n seconds after printing a user note",
          "n" },
        { 0 }
    };

    g_option_context_add_main_entries(main_context, options, NULL);
    g_option_context_set_help_enabled(main_context, TRUE);
    g_option_context_set_description(main_context,
        "Replays a PS/2 device using any log created with ps2emu-record\n");

    if (!g_option_context_parse(main_context, &argc, &argv, &error))
        exit_on_bad_argument(main_context, TRUE, error->message);

    if (argc < 2)
        exit_on_bad_argument(main_context, FALSE,
                             "No filename specified! Use --help for more "
                             "information");

    max_wait *= G_USEC_PER_SEC;
    event_delay = event_delay * G_USEC_PER_SEC + PS2EMU_MIN_EVENT_DELAY;
    note_delay *= G_USEC_PER_SEC;

    input_channel = g_io_channel_new_file(argv[1], "r", &error);
    if (!input_channel) {
        g_prefix_error(&error, "While opening %s: ", argv[1]);
        goto error;
    }

    log_version = parse_log_version(input_channel, &error);
    if (log_version > PS2EMU_LOG_VERSION) {
        g_set_error(&error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                    "Log version is too new (found %d, we only support up to "
                    "%d)", log_version, PS2EMU_LOG_VERSION);
        goto error;
    }

    if (!parse_events(input_channel, log_version, &error))
        goto error;

    g_io_channel_unref(input_channel);

    userio_channel = g_io_channel_new_file("/dev/userio", "r+", &error);
    if (!userio_channel) {
        g_prefix_error(&error, "While opening /dev/userio: ");
        goto error;
    }

    rc = g_io_channel_set_encoding(userio_channel, NULL, &error);
    if (rc != G_IO_STATUS_NORMAL) {
        g_prefix_error(&error, "While opening /dev/userio: ");
        goto error;
    }
    g_io_channel_set_buffered(userio_channel, FALSE);

    port_type = (replay_device_type == PS2_PORT_KBD) ?
        SERIO_8042_XL : SERIO_8042;
    rc = send_userio_cmd(userio_channel, USERIO_CMD_SET_PORT_TYPE, port_type,
                         &error);
    if (rc != G_IO_STATUS_NORMAL) {
        g_prefix_error(&error, "While setting port type on /dev/userio: ");
        goto error;
    }

    rc = send_userio_cmd(userio_channel, USERIO_CMD_REGISTER, 0, &error);
    if (rc != G_IO_STATUS_NORMAL) {
        g_prefix_error(&error, "While starting device on /dev/userio: ");
        goto error;
    }

    if (log_version == 0) {
        replay_device_type = PS2_PORT_AUX;

        if (!replay_line_list(userio_channel, event_list, 0, 0, verbose,
                              &error))
            goto error;
    } else {
        printf("Replaying initialization sequence...\n");
        if (!replay_line_list(userio_channel, init_event_list, 0, 0, verbose,
                              &error))
            goto error;

        printf("Device initialized\n");

        if (!no_events) {
            /* Sleep for half a second so we don't throw the driver out of sync */
            g_usleep(event_delay);

            printf("Replaying event sequence...\n");
            if (!replay_line_list(userio_channel, main_event_list, max_wait,
                                  note_delay, verbose, &error))
                goto error;
        }

        if (keep_running)
            pause();
    }

    return 0;

error:
    fprintf(stderr, "Error: %s\n", error->message);

    return 1;
}
