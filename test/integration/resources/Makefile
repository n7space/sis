CC = sparc-rtems6-gcc

BUILD_DIR = build

RTEMS_API = 6
RTEMS_CPU = sparc
RTEMS_BSP = gr712rc-qual-only

RTEMS_ROOT = /opt/rtems-6-sparc-gr712rc-smp-4
PKG_CONFIG = $(RTEMS_ROOT)/lib/pkgconfig/$(RTEMS_CPU)-rtems$(RTEMS_API)-$(RTEMS_BSP).pc

DEPFLAGS = -MT $@ -MD -MP -MF $(basename $@).d
WARNFLAGS = -Wall -Wextra
OPTFLAGS = -O2 -g -ffunction-sections -fdata-sections

ABI_FLAGS = $(shell pkg-config --cflags $(PKG_CONFIG))
LDFLAGS = $(shell pkg-config --libs $(PKG_CONFIG))
CFLAGS = $(DEPFLAGS) $(WARNFLAGS) $(ABI_FLAGS) $(OPTFLAGS)

CCLINK = $(CC) $(CFLAGS) -Wl,-Map,${BUILD_DIR}/$(basename $@).map

SRC_UARTS = init_uarts.c
OBJECTS_UARTS = $(patsubst %.c,$(BUILD_DIR)/%.o, $(SRC_UARTS))

all: rtems_uarts

rtems_uarts: $(OBJECTS_UARTS)
	$(CCLINK) $(OBJECTS_UARTS) ${LDFLAGS} -o ${BUILD_DIR}/$@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf ${BUILD_DIR}

.PHONY: clean

.DEFAULT_GOAL := all