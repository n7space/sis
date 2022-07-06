SIS_NAME := sis
SIS_VERSION := 2.29

CC = gcc

CFLAGS := -O3
INCL = -I.
LDFLAGS = -lm -lreadline
CONFIG = -DHAVE_CONFIG_H 
DEFS = -DFAST_UART

BUILD_DIR = ../build
SRC_DIR = src

SRC = $(wildcard ./*.c)
HEADERS = $(wildcard ./*.h)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/$(SRC_DIR)/%.o, $(SRC))

all: sis

sis: $(OBJECTS)
	$(CC) $(DEFS) $(CFLAGS) -o $(BUILD_DIR)/$(SRC_DIR)/$(SIS_NAME)-$(SIS_VERSION) $(OBJECTS) $(LDFLAGS)

$(BUILD_DIR)/$(SRC_DIR):
	mkdir -p $(BUILD_DIR)/$(SRC_DIR)

$(BUILD_DIR)/$(SRC_DIR)/%.o: %.c | $(BUILD_DIR)/$(SRC_DIR)
	$(CC) $(CONFIG) $(DEFS) $(INCL) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(BUILD_DIR)/$(SRC_DIR)/$(SIS_NAME)-$(SIS_VERSION)

.PHONY: clean

.DEFAULT_GOAL := sis
