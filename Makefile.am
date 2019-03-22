bin_PROGRAMS = sis
sis_SOURCES = erc32.c float.c grlib.c leon3.c exec.c func.c help.c \
	      sparc.c riscv.c leon2.c sis.c interf.c remote.c elf.c

AM_CFLAGS = -DFAST_UART
sis_LDADD = -lm -lreadline

info_TEXINFOS = sis.texi

.PHONY: v
v:	version.texi 
