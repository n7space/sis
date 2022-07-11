SRC_DIR = src
BUILD_DIR = build
INTEGRATION_TEST_DIR = test/integration

all: sis

sis: 
	$(MAKE) -C $(SRC_DIR) sis

check: sis
	$(MAKE) -C $(INTEGRATION_TEST_DIR) check

clean:
	$(MAKE) -C $(SRC_DIR) clean
	rm -rf $(BUILD_DIR)

.PHONY: clean

.DEFAULT_GOAL := all