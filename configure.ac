AC_INIT([sis], [2.14])
AC_CONFIG_SRCDIR([sis.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CHECK_HEADERS([fcntl.h stddef.h stdlib.h string.h sys/time.h unistd.h])
AC_CONFIG_FILES([Makefile])

AC_CHECK_LIB(readline, readline, READLINE=-lreadline,
	AC_ERROR([the required "readline" library is missing]), $READLINE)
AC_SUBST(READLINE)

AC_OUTPUT

