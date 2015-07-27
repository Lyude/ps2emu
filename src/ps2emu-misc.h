/*
 * common-cleanup.h
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

#ifndef __PS2EMU_MISC_H__
#define __PS2EMU_MISC_H__

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#define PS2EMU_ERROR (g_quark_from_static_string("ps2emu-error"))
#define PS2EMU_LOG_VERSION 1

typedef enum {
    PS2EMU_ERROR_INPUT,
    PS2EMU_ERROR_NO_EVENTS,
    PS2EMU_ERROR_MISC
} PS2Error;

gboolean print_version(const gchar *option_name,
                       const gchar *value,
                       gpointer data,
                       GError **error)
G_GNUC_NORETURN;

static inline void exit_on_bad_argument(GOptionContext *option_context,
                                        gboolean print_help,
                                        const gchar *format,
                                        ...)
G_GNUC_NORETURN;

static inline void exit_on_bad_argument(GOptionContext *option_context,
                                        gboolean print_help,
                                        const gchar *format,
                                        ...) {
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");

    if (print_help) {
        fprintf(stderr, "%s",
                g_option_context_get_help(option_context, FALSE, NULL));
    }

    exit(1);
}

#endif /* !__PS2EMU_MISC_H__ */
