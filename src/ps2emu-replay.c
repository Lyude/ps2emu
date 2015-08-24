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

#include "ps2emu-log.h"
#include "ps2emu-misc.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <errno.h>
#include <linux/serio.h>
#include <userio.h>

#define PS2EMU_MIN_EVENT_DELAY (0.5 * G_USEC_PER_SEC)

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
    static gboolean sync_warning_printed = FALSE;
    GIOStatus rc;

    rc = g_io_channel_read_chars(userio_channel, (gchar*)&data,
                                 sizeof(event->data), &count, error);

    if (rc != G_IO_STATUS_NORMAL)
        return FALSE;

    if (verbose && event->data == data)
        printf("Receive\t<- %.2hhx\n", data);
    else if (event->data != data) {
        fprintf(stderr, "Expected %.2hhx, received %.2hhx\n",
                event->data, data);

        if (!sync_warning_printed) {
            fprintf(stderr,
                    "The device has gone out of sync with the recording, "
                    "playback from this point forward will probably fail.\n");
            sync_warning_printed = TRUE;
        }
    }

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
    ParsedLog *log;
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

    log_version = log_parse_version(input_channel, &error);
    if (log_version > PS2EMU_LOG_VERSION) {
        g_set_error(&error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                    "Log version is too new (found %d, we only support up to "
                    "%d)", log_version, PS2EMU_LOG_VERSION);
        goto error;
    }

    log = log_parse(input_channel, log_version, &error);
    if (!log)
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

    port_type = (log->port == PS2_PORT_KBD) ? SERIO_8042_XL : SERIO_8042;
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
        if (!replay_line_list(userio_channel, log->main_section, 0, 0, verbose,
                              &error))
            goto error;
    } else {
        printf("Replaying initialization sequence...\n");
        if (!replay_line_list(userio_channel, log->init_section, 0, 0, verbose,
                              &error))
            goto error;

        printf("Device initialized\n");

        if (!no_events) {
            /* Sleep for half a second so we don't throw the driver out of sync */
            g_usleep(event_delay);

            printf("Replaying event sequence...\n");
            if (!replay_line_list(userio_channel, log->main_section, max_wait,
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
