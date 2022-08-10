include ../../definitions.mk

SIS_APP_DIR = ../../$(BUILD_DIR)/$(SRC_DIR)
RESOURCES_DIR = resources
UARTS_TEST_DIR = ${RESOURCES_DIR}/build/rtems_uarts

OUTPUT_FILE = $(SIS_APP_DIR)/system.output

UART_FILES = uart1 uart2 uart3 uart4 uart5 uart6
UART_RECEIVE_MSG = Readme!
UART_TRANSMIT_MSG = Data transmission via uart successful.

TESTS = init_test uarts_test

check: ${TESTS}

.SILENT :
	${TESTS}

.PHONY :
	${TESTS}
	check

##############################INTEGRATION TESTS################################

init_test:
	echo "Run example RTEMS application..."
	./$(SIS_APP_DIR)/$(SIS_NAME)-$(SIS_VERSION) -dumbio -uart1 stdio -r $(RTEMS_APP_DIR) > $(OUTPUT_FILE)
	grep -q "Hello, world!" $(OUTPUT_FILE)
	rm -f $(OUTPUT_FILE)

uarts_test:
	echo "Run UART data transmission test..."
	$(MAKE) -C $(RESOURCES_DIR) rtems_uarts
	touch ${UART_FILES} && echo ${UART_RECEIVE_MSG} | tee ${UART_FILES}
	./$(SIS_APP_DIR)/$(SIS_NAME)-$(SIS_VERSION) $(subst :, ,$(join $(patsubst %,-%:,${UART_FILES}), ${UART_FILES})) -r $(UARTS_TEST_DIR)
	for i in ${UART_FILES} ; do \
		grep -q "${UART_TRANSMIT_MSG}" $$i ; \
	done
	$(MAKE) -C $(RESOURCES_DIR) clean
	rm -f ${UART_FILES}


