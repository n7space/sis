include ../../definitions.mk

SIS_APP_DIR = ../../$(BUILD_DIR)/$(SRC_DIR)

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
	./$(SIS_APP_DIR)/$(SIS_NAME)-$(SIS_VERSION) -dumbio -r $(RTEMS_APP_DIR)
	grep -q "Hello, world!" $(SIS_APP_DIR)/uart_file_80000100
	rm -f $(OUTPUT_FILE)
