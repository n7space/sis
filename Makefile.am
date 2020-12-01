if LINENOISE
 LN_SRC=linenoise.c
else
 LN_SRC=
endif

bin_PROGRAMS = sis
sis_SOURCES = erc32.c grlib.c leon3.c exec.c func.c help.c \
	      sparc.c riscv.c leon2.c sis.c interf.c remote.c elf.c \
	      greth.c tap.c gr740.c \
	      $(LN_SRC)

AM_CFLAGS = -DFAST_UART
sis_LDADD = -lm @READLINE@ @LIBADD@
EXTRA_DIST = elf.h grlib.h linenoise.h riscv.h sis.h sparc.h

info_TEXINFOS = sis.texi

.PHONY: v
v:	version.texi
