SIS_NAME := sis
SIS_VERSION := 2.29
SIS_DIR := sis
BUILD_DIR=../build
CC = gcc
CFLAGS := -O3
INCL = -I.
LDFLAGS = -lm -lreadline
CONFIG = -DHAVE_CONFIG_H 
DEFS = -DFAST_UART
SRC = elf.c \
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
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/$(SIS_DIR)/%.o, $(SRC))

all: sis

sis: $(OBJECTS)
	$(CC) $(DEFS) $(CFLAGS) -o $(BUILD_DIR)/$(SIS_NAME)-$(SIS_VERSION) $(OBJECTS) $(LDFLAGS)

$(BUILD_DIR)/$(SIS_DIR):
	mkdir -p $(BUILD_DIR)/$(SIS_DIR)

$(BUILD_DIR)/sis/%.o: %.c | $(BUILD_DIR)/$(SIS_DIR)
	$(CC) $(CONFIG) $(DEFS) $(INCL) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(BUILD_DIR)/$(SIS_NAME)-$(SIS_VERSION)

.PHONY: clean

.DEFAULT_GOAL := all
