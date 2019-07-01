AC_INIT([sis], [2.16])
AC_CONFIG_SRCDIR([sis.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CHECK_HEADERS([fcntl.h stddef.h stdlib.h string.h sys/time.h unistd.h termios.h])
AC_CHECK_LIB(readline, readline,
	[AC_DEFINE([HAVE_READLINE], [1], ["readline found"]) READLINE=-lreadline] linenoise=false,
	[linenoise=true], $READLINE)
AC_SUBST(READLINE)
AM_CONDITIONAL([LINENOISE], [test x$linenoise = xtrue])
AC_SUBST(LINENOISE)
AC_CONFIG_FILES([Makefile])

AC_ARG_ENABLE([l1cache], [  --enable-l1cache	  enable L1 cache emulation])
AS_IF([test "x$enable_l1cache" = "xyes"],
  [AC_DEFINE([ENABLE_L1CACHE], [1], ["enable L1 cache emulation"])])

AC_C_BIGENDIAN

AC_OUTPUT
