include definitions.mk

all: sis test

sis: 
	$(MAKE) -C $(SRC_DIR) sis

libsis:
	$(MAKE) -C $(SRC_DIR) libsis

test: libsis
	$(MAKE) -C $(UNIT_TEST_DIR) test

check: sis test
	$(MAKE) -C $(UNIT_TEST_DIR) check
	$(MAKE) -C $(INTEGRATION_TEST_DIR) check

clean:
	$(MAKE) -C $(SRC_DIR) clean
	rm -rf $(BUILD_DIR)

.PHONY: clean

.DEFAULT_GOAL := all