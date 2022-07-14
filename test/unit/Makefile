include ../../definitions.mk

LIBSIS = ../../$(BUILD_DIR)/$(SRC_DIR)/libsis.a
TESTS_BUILD_DIR = ../../$(BUILD_DIR)/$(TEST_DIR)

SRC = main.c $(wildcard ./**/*.c)
TEST_HEADERS = $(wildcard ./**/*.h)
INCL = $(addprefix -I,$(sort $(dir $(TEST_HEADERS))))

TESTS := $(patsubst %.c,$(TESTS_BUILD_DIR)/%.o, $(SRC))
STATIC_LIBS = -Bstatic $(LIBSIS)

CCLINK = $(CC) $(CONFIG) $(DEFS) $(CFLAGS) -Wl,-Map,$(TESTS_BUILD_DIR)/$(basename $@).map

all: test

test: $(TESTS)
	$(CCLINK) $(TESTS) $(STATIC_LIBS) $(LDFLAGS) -o $(TESTS_BUILD_DIR)/$@

$(TESTS_BUILD_DIR):
	mkdir -p $(addprefix $(TESTS_BUILD_DIR)/,$(sort $(dir $(TEST_HEADERS))))

$(TESTS_BUILD_DIR)/%.o: %.c | $(TESTS_BUILD_DIR)
	$(CC) $(DEFS) $(CFLAGS) $(INCL) -c -o $@ $<

clean:
	rm -rf $(TESTS) $(TESTS_BUILD_DIR)
.PHONY: clean

.DEFAULT_GOAL := all
