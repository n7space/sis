include ../../definitions.mk

SIS_APP_DIR = ../../$(BUILD_DIR)/$(SRC_DIR)
OUTPUT_FILE = $(SIS_APP_DIR)/system.output

TESTS = init_test

check: ${TESTS}

.SILENT :
	${TESTS}

.PHONY :
	${TESTS}
	check

##############################INTEGRATION TESTS################################

init_test:
	echo "Run example RTEMS application..."
	./$(SIS_APP_DIR)/$(SIS_NAME)-$(SIS_VERSION) -dumbio -r $(RTEMS_APP_DIR) > $(OUTPUT_FILE)
	grep -q "Hello, world!" $(OUTPUT_FILE)
	rm -f $(OUTPUT_FILE)
