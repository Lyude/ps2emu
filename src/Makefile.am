AM_CFLAGS = -std=gnu11 $(GLIB_CFLAGS) -Wall -I$(top_srcdir)/ps2emu-kmod
LIBS = $(GLIB_LIBS) $(GLIB_LDFLAGS)

sbin_PROGRAMS = ps2emu-record \
                ps2emu-replay

ps2emu_record_SOURCES = ps2emu-record.c \
                        ps2emu-log.c    \
                        ps2emu-misc.c

ps2emu_replay_SOURCES = ps2emu-replay.c \
                        ps2emu-log.c    \
                        ps2emu-misc.c
