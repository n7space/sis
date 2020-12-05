if LINENOISE
 LN_SRC=linenoise.c
else
 LN_SRC=
endif

bin_PROGRAMS = sis
sis_SOURCES = erc32.c grlib.c leon3.c exec.c func.c help.c \
	      sparc.c riscv.c leon2.c sis.c interf.c remote.c elf.c \
	      greth.c tap.c gr740.c rv32.c \
	      $(LN_SRC)

AM_CFLAGS = -DFAST_UART
sis_LDADD = -lm @READLINE@ @LIBADD@
EXTRA_DIST = elf.h grlib.h linenoise.h riscv.h sis.h sparc.h rv32dtb.h

rv32dtb.h: $(abs_srcdir)/rv32.dts
	dtc -O dtb $(abs_srcdir)/rv32.dts -o rv32.dtb
	xxd --include rv32.dtb > rv32dtb.h

info_TEXINFOS = sis.texi

.PHONY: v
v:	version.texi
