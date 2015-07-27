/*
 * ps2emu-record.c
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <glib.h>
#include <signal.h>
#include <linux/limits.h>

#include "ps2emu-section.h"
#include "ps2emu-misc.h"
#include "ps2emu-event.h"

typedef struct {
    GQuark type;

    time_t dmesg_time;

    union {
        PS2Event event;
        gint64 start_time;
    };
} LogMsgParseResult;

static gboolean record_kbd;
static gboolean record_aux;
static gchar *output_file = "ps2emu_record.txt";
static GIOChannel *output_channel;

static gint64 start_time = 0;
static time_t dmesg_start_time = 0;

static GHashTable *ports;

#define I8042_OUTPUT  (g_quark_from_static_string("i8042: "))
#define PS2EMU_OUTPUT (g_quark_from_static_string("ps2emu: "))

#define I8042_DEV_DIR "/sys/devices/platform/i8042/"

#define PS2EMU_INIT_TIMEOUT_SECS 5

static GIOStatus get_next_module_line(GIOChannel *input_channel,
                                      GQuark *match,
                                      gchar **output,
                                      gchar **start_pos,
                                      GError **error) {
    static const gchar *search_strings[] = { "i8042: ", "ps2emu: " };
    int index;
    gchar *current_line;
    GIOStatus rc;

    while ((rc = g_io_channel_read_line(input_channel, &current_line, NULL,
                                        NULL, error)) == G_IO_STATUS_NORMAL) {
        for (index = 0; index < G_N_ELEMENTS(search_strings); index++) {
            *start_pos = strstr(current_line, search_strings[index]);
            if (*start_pos)
                break;
        }
        if (*start_pos)
            break;

        g_free(current_line);
    }

    if (rc != G_IO_STATUS_NORMAL) {
        return rc;
    }

    /* Move the start position after the initial 'i8042: ' */
    *start_pos += strlen(search_strings[index]);
    *output = current_line;

    *match = g_quark_from_static_string(search_strings[index]);

    return rc;
}

static gboolean parse_normal_event(const gchar *start_pos,
                                   PS2Event *event,
                                   GError **error) {
    gchar *type_str = NULL;
    gchar **type_str_args = NULL;
    int type_str_argc,
        parsed_count,
        port;

    errno = 0;
    parsed_count = sscanf(start_pos,
                          "[%*d] %hhx %*1[-<]%*1[->] i8042 (%m[^)])\n",
                          &event->data, &type_str);

    if (errno != 0 || parsed_count != 2)
        return FALSE;

    type_str_args = g_strsplit(type_str, ",", 0);

    if (strcmp(type_str_args[0], "interrupt") == 0) {
        event->type = PS2_EVENT_TYPE_INTERRUPT;

        type_str_argc = g_strv_length(type_str_args);
        if (type_str_argc < 3) {
            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Got interrupt event, but had less arguments then "
                        "expected");
            goto error;
        }

        errno = 0;
        port = strtol(type_str_args[1], NULL, 10);
        if (errno != 0) {
            g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                        "Failed to parse port number from interrupt event: "
                        "%s\n",
                        strerror(errno));
            goto error;
        }

        event->origin = (port == PS2_KEYBOARD_PORT) ?
            PS2_EVENT_ORIGIN_KEYBOARD : PS2_EVENT_ORIGIN_AUX;
    }
    else if (strcmp(type_str, "command") == 0)
        event->type = PS2_EVENT_TYPE_COMMAND;
    else if (strcmp(type_str, "parameter") == 0)
        event->type = PS2_EVENT_TYPE_PARAMETER;
    else if (strcmp(type_str, "return") == 0)
        event->type = PS2_EVENT_TYPE_RETURN;
    else if (strcmp(type_str, "kbd-data") == 0)
        event->type = PS2_EVENT_TYPE_KBD_DATA;

    event->original_line = start_pos;

    g_free(type_str);
    g_strfreev(type_str_args);

    return TRUE;

error:
    g_free(type_str);
    g_strfreev(type_str_args);

    return FALSE;
}

static gboolean parse_record_start_marker(const gchar *start_pos,
                                          gint64 *start_time) {
    gint parsed_count;

    errno = 0;
    parsed_count = sscanf(start_pos,
                          "Start recording %ld\n",
                          start_time);

    if (errno != 0 || parsed_count != 1)
        return FALSE;

    return TRUE;
}

static GIOStatus parse_next_message(GIOChannel *input_channel,
                                    LogMsgParseResult *res,
                                    GError **error) {
    gchar *start_pos;
    gchar *current_line = NULL;
    gint parsed_count;
    GIOStatus rc;

    while ((rc = get_next_module_line(input_channel, &res->type, &current_line,
                                      &start_pos, error)) ==
            G_IO_STATUS_NORMAL) {
        if (res->type == I8042_OUTPUT) {
            if (parse_normal_event(start_pos, &res->event, error))
                break;

            if (*error)
                goto fail;
        }
        else if (res->type == PS2EMU_OUTPUT) {
            if (parse_record_start_marker(start_pos, &res->start_time))
                break;
        }

        g_free(current_line);
    }

    if (rc != G_IO_STATUS_NORMAL)
        return rc;

    /* Parse the time value at the beginning of the message */
    errno = 0;
    parsed_count = sscanf(current_line, "%*d,%*d,%ld", &res->dmesg_time);
    if (parsed_count != 1 || errno != 0) {
        g_set_error(error, PS2EMU_ERROR, PS2EMU_ERROR_INPUT,
                    "Invalid/no time value received: %s", strerror(errno));

        rc = G_IO_STATUS_ERROR;
    }

    res->type = res->type;

fail:
    g_free(current_line);

    return rc;
}

static GIOStatus process_event(PS2Event *event,
                               time_t time,
                               GError **error) {
    static gboolean ignoring_events = FALSE;
    gchar *event_str = NULL;
    GIOStatus rc;

    /* Any commands that we receive with any of the port numbers are just part
     * of the i8042 probing, and can't be forwarded over serio in the relay
     * module. Ignore all data we read starting from commands like this, until
     * we reach a different command */
    if (!ignoring_events) {
        if (event->type == PS2_EVENT_TYPE_COMMAND &&
            g_hash_table_contains(ports, GUINT_TO_POINTER(event->data))) {

            ignoring_events = TRUE;
            return G_IO_STATUS_NORMAL;
        }
    }
    else {
        if (event->type == PS2_EVENT_TYPE_COMMAND &&
            !g_hash_table_contains(ports, GUINT_TO_POINTER(event->data))) {

            ignoring_events = FALSE;
        }
        else
            return G_IO_STATUS_NORMAL;
    }

    /* Filter out all commands that have made it to this point. With i8042's
     * debug output, commands just mark the recepient of the message which we
     * don't need (with the AUX port anyway), and can't use with serio */
    if (event->type == PS2_EVENT_TYPE_COMMAND)
        return G_IO_STATUS_NORMAL;

    /* The logic here is that we can only get two types of events from a
     * keyboard, kbd-data and interrupt. No other device sends kbd-data, so we
     * can judge if an event comes from a keyboard or not solely based off that.
     * With interrupts, we can tell if the interrupt is coming from the keyboard
     * or not by comparing the port number of the event to that of the KBD
     * port */
    if (!record_kbd) {
        if (event->type == PS2_EVENT_TYPE_INTERRUPT &&
            event->origin == PS2_EVENT_ORIGIN_KEYBOARD)
            return G_IO_STATUS_NORMAL;

        if (event->type == PS2_EVENT_TYPE_KBD_DATA)
            return G_IO_STATUS_NORMAL;
    }

    if (!record_aux) {
        if (event->type == PS2_EVENT_TYPE_INTERRUPT) {
            if (event->origin == PS2_EVENT_ORIGIN_AUX)
                return G_IO_STATUS_NORMAL;
        }
        else if (event->type != PS2_EVENT_TYPE_KBD_DATA)
            return G_IO_STATUS_NORMAL;
    }

    if (!dmesg_start_time)
        dmesg_start_time = time;

    event_str = ps2_event_to_string(event, time - dmesg_start_time);
    rc = g_io_channel_write_chars(output_channel, event_str, -1, NULL, error);

    g_free(event_str);

    return rc;
}

static gboolean write_to_char_dev(const gchar *cdev,
                                  GError **error,
                                  const gchar *format,
                                  ...) {
    GIOChannel *channel = g_io_channel_new_file(cdev, "w", error);
    GIOStatus rc;
    gchar *data = NULL;
    gsize data_len,
          bytes_written;
    va_list args;

    if (!channel) {
        g_prefix_error(error, "While opening %s: ", cdev);

        goto error;
    }

    va_start(args, format);
    data = g_strdup_vprintf(format, args);
    va_end(args);

    data_len = strlen(data);

    rc = g_io_channel_write_chars(channel, data, data_len, &bytes_written,
                                  error);
    if (rc != G_IO_STATUS_NORMAL) {
        g_prefix_error(error, "While writing to %s: ", cdev);

        goto error;
    }

    g_io_channel_unref(channel);
    g_free(data);
    return TRUE;

error:
    if (channel)
        g_io_channel_unref(channel);

    g_free(data);

    return FALSE;
}

static gboolean get_i8042_io_ports(GError **error) {
    GIOChannel *io_ports_file = g_io_channel_new_file("/proc/ioports", "r",
                                                      error);
    gchar *line;
    GIOStatus rc;

    ports = g_hash_table_new(g_direct_hash, g_direct_equal);

    if (!io_ports_file)
        return FALSE;

    for (rc = g_io_channel_read_line(io_ports_file, &line, NULL, NULL, error);
         rc == G_IO_STATUS_NORMAL;
         rc = g_io_channel_read_line(io_ports_file, &line, NULL, NULL, error)) {
        guint min, max;
        gint parsed_count;
        gchar *device_name;

        errno = 0;
        parsed_count = sscanf(line, "%x-%x : %m[^\n]\n",
                              &min, &max, &device_name);
        if (parsed_count != 3 || errno != 0 ||
            strcmp(device_name, "keyboard") != 0)
            goto next;

        for (int i = min; i <= max; i++) {
            g_hash_table_insert(ports, GUINT_TO_POINTER(i),
                                GUINT_TO_POINTER(i));
        }
next:
        g_free(line);
    }

    g_io_channel_unref(io_ports_file);

    return TRUE;
}

static inline void disable_i8042_debugging() {
    g_warn_if_fail(write_to_char_dev("/sys/module/i8042/parameters/debug",
                                     NULL, "0\n"));
}

static void exit_on_interrupt() {
    disable_i8042_debugging();

    exit(0);
}

static gboolean enable_i8042_debugging(GError **error) {
    GDir *devices_dir = NULL;
    struct sigaction sigaction_struct;

    devices_dir = g_dir_open(I8042_DEV_DIR, 0, error);
    if (!devices_dir) {
        g_prefix_error(error, "While opening " I8042_DEV_DIR ": ");

        goto error;
    }

    /* Detach the devices before we do anything, this prevents potential race
     * conditions */
    for (gchar const *dir_name = g_dir_read_name(devices_dir);
         dir_name != NULL && *error == NULL;
         dir_name = g_dir_read_name(devices_dir)) {
        gchar *file_name;

        if (!g_str_has_prefix(dir_name, "serio"))
            continue;

        file_name = g_strconcat(I8042_DEV_DIR, dir_name, "/", "drvctl", NULL);
        if (!write_to_char_dev(file_name, error, "none")) {
            g_free(file_name);
            goto error;
        }

        g_free(file_name);
    }
    if (*error)
        goto error;

    /* We mark when the recording starts, so that we can separate this recording
     * from other recordings ran during this session */
    start_time = g_get_monotonic_time();

    if (!write_to_char_dev("/dev/kmsg", error, "ps2emu: Start recording %ld\n",
                           start_time))
        goto error;

    /* Enable the debugging output for i8042 */
    if (!write_to_char_dev("/sys/module/i8042/parameters/debug", error, "1\n"))
        goto error;

    /* Reattach the devices */
    g_dir_rewind(devices_dir);
    for (gchar const *dir_name = g_dir_read_name(devices_dir);
         dir_name != NULL && *error == NULL;
         dir_name = g_dir_read_name(devices_dir)) {
        gchar *file_name;

        if (!g_str_has_prefix(dir_name, "serio"))
            continue;

        file_name = g_strconcat(I8042_DEV_DIR, dir_name, "/", "drvctl", NULL);
        if (!write_to_char_dev(file_name, error, "rescan")) {
            g_free(file_name);
            goto error;
        }

        g_free(file_name);
    }
    if (*error)
        goto error;

    g_dir_close(devices_dir);

    /* Disable debugging when this application quits */
    memset(&sigaction_struct, 0, sizeof(sigaction_struct));
    sigaction_struct.sa_handler = exit_on_interrupt;

    g_warn_if_fail(sigaction(SIGINT, &sigaction_struct, NULL) == 0);
    g_warn_if_fail(sigaction(SIGTERM, &sigaction_struct, NULL) == 0);
    g_warn_if_fail(sigaction(SIGHUP, &sigaction_struct, NULL) == 0);

    return TRUE;

error:
    if (devices_dir)
        g_dir_close(devices_dir);

    return FALSE;
}

typedef struct {
    time_t last_check_time;
    time_t *last_event_time;
} InitTimeoutCheckerArgs;

gboolean init_timeout_checker(void *data) {
    InitTimeoutCheckerArgs *args = data;

    if ((*args->last_event_time - args->last_check_time) / G_USEC_PER_SEC <
        PS2EMU_INIT_TIMEOUT_SECS)
        return G_SOURCE_CONTINUE;

    dmesg_start_time = 0;

    /* We can just rely on the main recording function failing if this
     * happens to fail */
    g_io_channel_write_chars(output_channel, "S: Main\n", -1, NULL, NULL);
    g_io_channel_flush(output_channel, NULL);

    printf("The first stage of the recording has completed, you may now use "
           "your computer normally.\n");

    return G_SOURCE_REMOVE;
}

typedef struct {
    LogMsgParseResult *res;
    gboolean *ret;
    GError **error;
} DmesgEventHandlerArgs;

static gboolean dmesg_event_handler(GIOChannel *source,
                                    GIOCondition condition,
                                    void *data) {
    DmesgEventHandlerArgs *args = data;
    GIOStatus rc;

    switch (condition) {
        case G_IO_IN:
            while ((rc = parse_next_message(source, args->res, args->error)) ==
                   G_IO_STATUS_NORMAL) {
                if (args->res->type != I8042_OUTPUT)
                    continue;

                rc = process_event(&args->res->event, args->res->dmesg_time,
                                   args->error);
                if (rc != G_IO_STATUS_NORMAL)
                    goto error;
            }
            if (rc != G_IO_STATUS_AGAIN)
                goto error;

            break;
        default:
            break;
    }

    return TRUE;

error:
    *args->ret = FALSE;
    return FALSE;
}

static GIOStatus write_to_channel(GIOChannel *output_channel,
                                  GError **error,
                                  const gchar *format,
                                  ...)
G_GNUC_PRINTF(3, 4);

static GIOStatus write_to_channel(GIOChannel *output_channel,
                                  GError **error,
                                  const gchar *format,
                                  ...) {
    gchar *output_line;
    GIOStatus rc;
    va_list args;

    va_start(args, format);
    output_line = g_strdup_vprintf(format, args);
    va_end(args);

    rc = g_io_channel_write_chars(output_channel, output_line,
                                  strlen(output_line), NULL, error);

    g_free(output_line);

    return rc;
}

static inline gboolean change_directory(const gchar *path,
                                        GError **error) {
    int rc;

    rc = chdir(path);
    if (rc) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "When changing directory to %s: %s", path, strerror(errno));
        return FALSE;
    }

    return TRUE;
}

static gboolean write_version_info(GIOChannel *output_channel,
                                   GError **error) {
    gchar *version;

    if (!g_file_get_contents("/proc/version", &version, NULL, error))
        return FALSE;

    write_to_channel(output_channel, error, "# Kernel Info: %s", version);

    g_free(version);

    return !(*error);
}

static gboolean write_input_device_info(GIOChannel *output_channel,
                                        const gchar *path,
                                        GError **error) {
    gchar *last_wd = getcwd(g_malloc(PATH_MAX), PATH_MAX),
          *input_dev_path,
          *device_name,
          *device_port;
    GDir *input_dir;

    if (!change_directory(path, error))
        goto out1;

    /* If the subdirectory "input" exists, there's probably an input device
     * attached to this i8042 port */
    input_dir = g_dir_open("input", 0, error);
    if (!input_dir) {
        if (g_error_matches(*error, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
            g_error_matches(*error, G_FILE_ERROR, G_FILE_ERROR_NOTDIR))
            g_clear_error(error);

        goto out2;
    }

    /* Find the directory containing the information on the device */
    input_dev_path = NULL;
    for (const gchar *dir_name = g_dir_read_name(input_dir);
         dir_name != NULL && *error == NULL;
         dir_name = g_dir_read_name(input_dir)) {

        if (g_str_has_prefix(dir_name, "input")) {
            input_dev_path = g_build_filename("input", dir_name, NULL);
            break;
        }
    }
    if (!input_dev_path)
        goto out3;

    if (!g_file_get_contents("description", &device_port, NULL, error))
        goto out4;

    if (!change_directory(input_dev_path, error))
        goto out5;

    if (!g_file_get_contents("name", &device_name, NULL, error))
        goto out5;

    g_strstrip(device_port);
    g_strstrip(device_name);

    write_to_channel(output_channel, error, "#    \"%s\" on %s\n",
                     device_name, device_port);

      g_free(device_name);
out5: g_free(device_port);
out4: g_free(input_dev_path);
out3: g_dir_close(input_dir);

out2: change_directory(last_wd, error);
out1: g_free(last_wd);

    return !(*error);
}

static gboolean write_device_summary(GIOChannel *output_channel,
                                     GError **error) {
    gchar *device_path,
          *child_device_path;
    GDir *devices_dir,
         *device_dir;

    if (!write_to_channel(output_channel, error, "# Device listing:\n"))
        return FALSE;

    devices_dir = g_dir_open(I8042_DEV_DIR, 0, error);
    if (!devices_dir) {
        g_prefix_error(error, "While opening " I8042_DEV_DIR ": ");

        return FALSE;
    }

    for (const gchar *dir_name = g_dir_read_name(devices_dir);
         dir_name != NULL && *error == NULL;
         dir_name = g_dir_read_name(devices_dir)) {
        if (!g_str_has_prefix(dir_name, "serio"))
            continue;

        device_path = g_build_filename(I8042_DEV_DIR, dir_name, NULL);
        if (!write_input_device_info(output_channel, device_path, error))
            goto out;

        /* Check for children on the PS/2 device */
        device_dir = g_dir_open(device_path, 0, error);
        if (!device_dir)
            goto out;

        for (const gchar *dir_name = g_dir_read_name(device_dir);
             dir_name != NULL && *error == NULL;
             dir_name = g_dir_read_name(device_dir)) {
            if (!g_str_has_prefix(dir_name, "serio"))
                continue;

            child_device_path = g_build_filename(device_path, dir_name, NULL);
            write_input_device_info(output_channel, child_device_path, error);

            g_free(child_device_path);

            if (*error)
                goto out;
        }
out:

        g_dir_close(device_dir);
        g_free(device_path);
    }

    write_to_channel(output_channel, error, "#\n");

    g_dir_close(devices_dir);

    return !(*error);
}

static gboolean write_machine_summary(GIOChannel *output_channel,
                                      GError **error) {
    gchar *last_wd = getcwd(g_malloc(PATH_MAX), PATH_MAX),
          *sys_vendor = NULL,
          *product_name = NULL,
          *product_version = NULL,
          *bios_vendor = NULL,
          *bios_date = NULL,
          *bios_version = NULL;

    if (!change_directory("/sys/class/dmi/id", error))
        return FALSE;

    if (!g_file_get_contents("sys_vendor", &sys_vendor, NULL, error) ||
        !g_file_get_contents("product_name", &product_name, NULL, error) ||
        !g_file_get_contents("product_version", &product_version, NULL, error) ||
        !g_file_get_contents("bios_vendor", &bios_vendor, NULL, error) ||
        !g_file_get_contents("bios_date", &bios_date, NULL, error) ||
        !g_file_get_contents("bios_version", &bios_version, NULL, error))
        goto out;

    write_to_channel(output_channel,
                     error,
                     "# Manufacturer: %s"
                     "# Product Name: %s"
                     "# Version: %s"
                     "# BIOS Vendor: %s"
                     "# BIOS Date: %s"
                     "# BIOS Version: %s"
                     "#\n",
                     sys_vendor, product_name, product_version, bios_vendor,
                     bios_date, bios_version);

out:
    g_free(sys_vendor);
    g_free(product_name);
    g_free(product_version);
    g_free(bios_vendor);
    g_free(bios_date);
    g_free(bios_version);

    change_directory(last_wd, error);
    g_free(last_wd);

    return !(*error);
}

static gboolean write_info(GIOChannel *output_channel,
                           GError **error) {
    if (!write_version_info(output_channel, error) ||
        !write_machine_summary(output_channel, error) ||
        !write_device_summary(output_channel, error))
        return FALSE;

    return TRUE;
}

static gboolean record(GError **error) {
    GIOChannel *input_channel;
    LogMsgParseResult res;
    InitTimeoutCheckerArgs timeout_checker_args;
    DmesgEventHandlerArgs dmesg_event_handler_args;
    GIOStatus rc;
    gboolean ret = TRUE;

    if (!write_to_channel(output_channel, error, "S: Init\n"))
        return FALSE;

    input_channel = g_io_channel_new_file("/dev/kmsg", "r", error);
    if (!input_channel)
        return FALSE;

    while ((rc = parse_next_message(input_channel, &res, error)) ==
           G_IO_STATUS_NORMAL) {
        if (res.type == I8042_OUTPUT)
            continue;
        else if (res.start_time >= start_time)
            break;
    }
    if (rc != G_IO_STATUS_NORMAL) {
        g_set_error_literal(error, PS2EMU_ERROR, PS2EMU_ERROR_NO_EVENTS,
                            "Reached EOF of /dev/kmsg and got no events");
        return FALSE;
    }

    rc = g_io_channel_set_flags(input_channel, G_IO_FLAG_NONBLOCK, error);
    if (rc != G_IO_STATUS_NORMAL)
        return FALSE;

    dmesg_event_handler_args = (DmesgEventHandlerArgs) {
        .res = &res,
        .error = error,
        .ret = &ret,
    };
    g_io_add_watch(input_channel, G_IO_IN | G_IO_ERR | G_IO_HUP,
                   dmesg_event_handler, &dmesg_event_handler_args);

    timeout_checker_args = (InitTimeoutCheckerArgs) {
        .last_check_time = g_get_monotonic_time() - start_time,
        .last_event_time = &res.dmesg_time,
    };
    g_timeout_add_seconds_full(G_PRIORITY_HIGH, PS2EMU_INIT_TIMEOUT_SECS,
                               init_timeout_checker, &timeout_checker_args,
                               NULL);

    g_main_loop_run(g_main_loop_new(NULL, FALSE));

    return ret;
}

int main(int argc, char *argv[]) {
    GOptionContext *main_context =
        g_option_context_new("[output_file] record PS/2 devices");
    gboolean rc;
    GError *error = NULL;
    gchar *record_kbd_str = NULL,
          *record_aux_str = NULL;

    GOptionEntry options[] = {
        { "record-kbd", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
          &record_kbd_str,
          "Enable recording of the KBD (keyboard) port, disabled by default",
          "<yes|no>" },
        { "record-aux", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
          &record_aux_str,
          "Enable recording of the AUX (auxillary, usually the port used for "
          "cursor devices) port, enabled by default",
          "<yes|no>" },
        { "version", 'V', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          print_version,
          "Show the version of the application", NULL },
        { 0 }
    };

    g_option_context_add_main_entries(main_context, options, NULL);
    g_option_context_set_help_enabled(main_context, TRUE);
    g_option_context_set_description(main_context,
        "Allows the recording of all of the commands going in/out of a PS/2\n"
        "port, so that they may later be replayed using a virtual PS/2\n"
        "controller on another person's machine.\n"
        "\n"
        "By default, ps2emu-record does not record keyboard input. This is\n"
        "is because recording the user's keyboard input has the consequence\n"
        "of potentially recording sensitive information, such as a user's\n"
        "password (since the user usually needs to type their password into\n"
        "their keyboard to log in). If you need to record keyboard input,\n"
        "please read the documentation for this tool first.\n");

    rc = g_option_context_parse(main_context, &argc, &argv, &error);
    if (!rc) {
        exit_on_bad_argument(main_context, TRUE,
            "Invalid options: %s", error->message);
    }

    if (argc > 1)
        output_file = argv[1];

    output_channel = g_io_channel_new_file(output_file, "w+", &error);
    if (!output_channel) {
        fprintf(stderr, "Couldn't open `%s`: %s\n", output_file,
                error->message);
        exit(1);
    }

    /* Don't record the keyboard if the user didn't explicitly enable it */
    if (!record_kbd_str || strcasecmp(record_kbd_str, "no") == 0)
        record_kbd = FALSE;
    else if (strcasecmp(record_kbd_str, "yes") == 0)
        record_kbd = TRUE;
    else {
        exit_on_bad_argument(main_context, TRUE,
            "Invalid value for --record-kbd: `%s`", record_kbd_str);
    }

    /* Record the AUX port unless the user explicitly disables it */
    if (!record_aux_str || strcasecmp(record_aux_str, "yes") == 0)
        record_aux = TRUE;
    else if (strcasecmp(record_aux_str, "no") == 0)
        record_aux = FALSE;
    else {
        exit_on_bad_argument(main_context, TRUE,
            "Invalid value for --record-aux: `%s`", record_aux_str);
    }

    /* Throw an error if recording of both KBD and AUX is disabled */
    if (!record_kbd && !record_aux)
        exit_on_bad_argument(main_context, FALSE, "Nothing to record!");

    if (!get_i8042_io_ports(&error)) {
        fprintf(stderr,
                "Failed to read /proc/ioports: %s\n",
                error->message);
        exit(1);
    }

    printf("====== ATTENTION! ======\n"
           "ps2emu-record will soon start recording your device. During the\n"
           "first stage of this recording, it is VERY IMPORTANT that you do\n"
           "not at all touch your mouse or keyboard. Doing so may potentially\n"
           "contaminate the recording. This first stage only lasts a couple\n"
           "of seconds at most, and ps2emu-record will notify you when it is\n"
           "okay to touch your mouse and/or keyboard again.\n"
           "Press any key to continue...\n");
    getchar();
    printf("Recording has started, please don't touch your mouse or "
           "keyboard...\n");

    /* Write the header for the recording */
    if (!write_to_channel(output_channel, &error, "# ps2emu-record V%d\n",
                          PS2EMU_LOG_VERSION))
        goto out;

    if (!write_info(output_channel, &error))
        goto out;

    if (!enable_i8042_debugging(&error)) {
        fprintf(stderr,
                "Failed to enable i8042 debugging: %s\n",
                error->message);
        exit(1);
    }

    g_option_context_free(main_context);

    rc = record(&error);

out:
    disable_i8042_debugging();
    if (error) {
        fprintf(stderr, "Error: %s\n",
                error->message);
    }

    return (rc) ? -1 : 0;
}
