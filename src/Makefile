include ../definitions.mk

SIS_BUILD_DIR = ../$(BUILD_DIR)/$(SRC_DIR)

SRC = $(wildcard ./*.c)
SIS_SRC = $(filter sis.c,$(SRC))
LIB_SRC = $(filter-out sis.c,$(SRC))
INCL = $(addprefix -I,$(sort $(dir $(wildcard ./*.h))))
OBJECTS := $(patsubst %.c,$(SIS_BUILD_DIR)/%.o, $(LIB_SRC))
STATIC_LIBS = -Bstatic $(SIS_BUILD_DIR)/libsis.a

all: sis

sis: $(SIS_SRC) libsis
	$(CC) $(DEFS) $(CFLAGS) -o $(SIS_BUILD_DIR)/$(SIS_NAME)-$(SIS_VERSION) $(STATIC_LIBS) $(SIS_SRC) $(LDFLAGS)

libsis: $(OBJECTS)
	$(AR) -crsv $(SIS_BUILD_DIR)/$@.a $(OBJECTS)

$(SIS_BUILD_DIR):
	mkdir -p $(SIS_BUILD_DIR)

$(SIS_BUILD_DIR)/%.o: %.c | $(SIS_BUILD_DIR)
	$(CC) $(CONFIG) $(DEFS) $(INCL) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(SIS_BUILD_DIR)/$(SIS_NAME)-$(SIS_VERSION)

.PHONY: clean

.DEFAULT_GOAL := sis
