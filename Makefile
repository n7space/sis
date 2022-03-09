CC := /usr/bin/gcc
CFLAGS := -O3
INCL = -I.
LDFLAGS = -lm -lreadline
CONFIG = -DHAVE_CONFIG_H 
DEFS = -DFAST_UART
SOURCES = elf.c  erc32.c  exec.c  func.c  gr740.c \
		  greth.c  grlib.c  help.c  interf.c  leon2.c \
		  leon3.c  linenoise.c  remote.c  riscv.c \
		  rv32.c sis.c  sparc.c  tap.c
HEADERS = config.h  elf.h  grlib.h  linenoise.h \
		  riscv.h  rv32dtb.h  sis.h  sparc.h
OBJECTS := $(patsubst %.c,%.o, $(SOURCES))
DEPDIR = .deps

include ./$(DEPDIR)/elf.Po
include ./$(DEPDIR)/erc32.Po
include ./$(DEPDIR)/exec.Po
include ./$(DEPDIR)/func.Po
include ./$(DEPDIR)/gr740.Po
include ./$(DEPDIR)/greth.Po
include ./$(DEPDIR)/grlib.Po
include ./$(DEPDIR)/help.Po
include ./$(DEPDIR)/interf.Po
include ./$(DEPDIR)/leon2.Po
include ./$(DEPDIR)/leon3.Po
include ./$(DEPDIR)/linenoise.Po
include ./$(DEPDIR)/remote.Po
include ./$(DEPDIR)/riscv.Po
include ./$(DEPDIR)/rv32.Po
include ./$(DEPDIR)/sis.Po
include ./$(DEPDIR)/sparc.Po
include ./$(DEPDIR)/tap.Po

%.o: %.c
	$(CC) $(CONFIG) $(DEFS) $(INCL) $(CFLAGS) -MT $@ -MD -MP -MF $(patsubst %.o,$(DEPDIR)/%.Tpo,$@) -c -o $@ $<
	mv -f $(patsubst %.o,$(DEPDIR)/%.Tpo,$@) $(patsubst %.o,$(DEPDIR)/%.Po,$@)

all: $(OBJECTS)
	$(CC) $(DEFS) $(CFLAGS) -o sis $(OBJECTS) $(LDFLAGS)

clean:
	rm -f $(OBJECTS) sis

.PHONY: clean

.DEFAULT_GOAL := all
