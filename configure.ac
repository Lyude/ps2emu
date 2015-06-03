AC_INIT([ps2emu], [0.0], [cpaul@redhat.com], [ps2emu])
AM_INIT_AUTOMAKE([foreign -Wall])
AC_CONFIG_MACRO_DIR([m4])

# Check for programs
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

AX_GCC_VAR_ATTRIBUTE(cleanup)

AM_SILENT_RULES([yes])

PKG_CHECK_MODULES([GLIB], [glib-2.0])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
