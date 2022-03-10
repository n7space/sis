PROJECT = sis
CC := gcc
CFLAGS := -O3
INCL = -I.
LDFLAGS = -lm -lreadline
CONFIG = -DHAVE_CONFIG_H 
DEFS = -DFAST_UART
SOURCES = elf.c \
		  erc32.c \
		  exec.c \
		  func.c \
		  gr740.c \
		  greth.c \
		  grlib.c \
		  help.c \
		  interf.c \
		  leon2.c \
		  leon3.c \
		  linenoise.c \
		  remote.c \
		  riscv.c \
		  rv32.c \
		  sis.c \
		  sparc.c \
		  tap.c
HEADERS = config.h \
		  elf.h \
		  grlib.h \
		  linenoise.h \
		  riscv.h \
		  rv32dtb.h \
		  sis.h \
		  sparc.h
OBJECTS := $(patsubst %.c,%.o, $(SOURCES))

all: $(OBJECTS)
	$(CC) $(DEFS) $(CFLAGS) -o $(PROJECT) $(OBJECTS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CONFIG) $(DEFS) $(INCL) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(PROJECT)

.PHONY: clean

.DEFAULT_GOAL := all
