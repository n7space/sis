include ../definitions.mk

CFLAGS := -O3
INCL = -I.
LDFLAGS = -lm -lreadline
CONFIG = -DHAVE_CONFIG_H 
DEFS = -DFAST_UART

SIS_BUILD_DIR = ../$(BUILD_DIR)/$(SRC_DIR)

SRC = $(wildcard ./*.c)
HEADERS = $(wildcard ./*.h)
OBJECTS := $(patsubst %.c,$(SIS_BUILD_DIR)/%.o, $(SRC))

all: sis

sis: $(OBJECTS)
	$(CC) $(DEFS) $(CFLAGS) -o $(SIS_BUILD_DIR)/$(SIS_NAME)-$(SIS_VERSION) $(OBJECTS) $(LDFLAGS)

$(SIS_BUILD_DIR):
	mkdir -p $(SIS_BUILD_DIR)

$(SIS_BUILD_DIR)/%.o: %.c | $(SIS_BUILD_DIR)
	$(CC) $(CONFIG) $(DEFS) $(INCL) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(SIS_BUILD_DIR)/$(SIS_NAME)-$(SIS_VERSION)

.PHONY: clean

.DEFAULT_GOAL := sis
