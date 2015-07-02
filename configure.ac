AC_INIT([ps2emu], [0.0], [thatslyude@gmail.com], [ps2emu])
AM_INIT_AUTOMAKE([foreign -Wall])

# Check for programs
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

AM_SILENT_RULES([yes])

PKG_CHECK_MODULES([GLIB], [glib-2.0])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
