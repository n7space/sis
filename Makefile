SRC_DIR = src
BUILD_DIR = build

all: sis

sis: 
	$(MAKE) -C $(SRC_DIR) sis

clean:
	$(MAKE) -C $(SRC_DIR) clean
	rm -rf $(BUILD_DIR)

.PHONY: clean

.DEFAULT_GOAL := all