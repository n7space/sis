include ../../definitions.mk

CPPUTEST_HOME = /opt/cpputest
CPPUTEST_INCL = -I$(CPPUTEST_HOME)/include
CPPUTEST_LIB = -L$(CPPUTEST_HOME)/lib -lCppUTest -lCppUTestExt

LIBSIS = ../../$(BUILD_DIR)/$(SRC_DIR)/libsis.a
TESTS_BUILD_DIR = ../../$(BUILD_DIR)/$(UNIT_TEST_DIR)

SRC = main.cc $(wildcard ./**/*.cc)
INCL = $(addprefix -I,$(sort $(dir $(wildcard ./../../$(SRC_DIR)/*.h))))

TESTS := $(patsubst %.cc,$(TESTS_BUILD_DIR)/%.o, $(SRC))
STATIC_LIBS = -Bstatic $(LIBSIS)

CCLINK = $(G++) $(CONFIG) $(DEFS) $(CFLAGS) -Wl,-Map,$(TESTS_BUILD_DIR)/$(basename $@).map

all: test

test: $(TESTS)
	$(CCLINK) $(TESTS) $(STATIC_LIBS) $(CPPUTEST_LIB) $(LDFLAGS) -o $(TESTS_BUILD_DIR)/$@

$(TESTS_BUILD_DIR):
	mkdir -p $(addprefix $(TESTS_BUILD_DIR)/,$(sort $(dir $(SRC))))

$(TESTS_BUILD_DIR)/%.o: %.cc | $(TESTS_BUILD_DIR)
	$(G++) $(DEFS) $(CFLAGS) $(INCL) $(CPPUTEST_INCL) -c -o $@ $<

check:
	./$(TESTS_BUILD_DIR)/test -c

clean:
	rm -rf $(TESTS) $(TESTS_BUILD_DIR)
.PHONY: clean

.DEFAULT_GOAL := all