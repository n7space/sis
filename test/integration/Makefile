include ../../definitions.mk

SIS_APP_DIR = ../../${BUILD_DIR}/${SRC_DIR}
RESOURCES_DIR = resources
TEST_BUILD_DIR = ${RESOURCES_DIR}/build
UARTS_TEST = rtems_uarts
TIMERS_TEST = rtems_timers
UART_BIDIRECTIONAL_TEST = rtems_uart_bidirectional

OUTPUT_FILE = ${SIS_APP_DIR}/system.output

UART_FILES := uart1 uart2 uart3 uart4 uart5 uart6
UART_FILES_RUN_PARAMS = $(subst :, ,$(join $(patsubst %,-%:,${UART_FILES}), ${UART_FILES}))
UART_RECEIVE_MSG = Readme!
UART_TRANSMIT_MSG = Data transmission via uart successful.

TESTS = init_test uarts_test timers_test uart_bidirectional_test

check: ${TESTS}

.SILENT :
	${TESTS}

.PHONY :
	${TESTS}
	check

##############################INTEGRATION TESTS################################

init_test:
	echo "Run example RTEMS application..."
	./${SIS_APP_DIR}/${SIS_NAME}-${SIS_VERSION} -dumbio -uart1 stdio -r ${RTEMS_APP_DIR} > ${OUTPUT_FILE}
	grep -q "Hello, world!" ${OUTPUT_FILE}
	rm -f ${OUTPUT_FILE}

uarts_test:
	echo "Run UART data transmission test..."
	$(MAKE) -C ${RESOURCES_DIR} rtems_uarts
	touch ${UART_FILES} && echo ${UART_RECEIVE_MSG} | tee ${UART_FILES}
	./${SIS_APP_DIR}/${SIS_NAME}-${SIS_VERSION} ${UART_FILES_RUN_PARAMS} -r ${TEST_BUILD_DIR}/${UARTS_TEST}
	for uart_file in ${UART_FILES} ; do \
		grep -q "${UART_TRANSMIT_MSG}" $$uart_file ; \
	done
	$(MAKE) -C ${RESOURCES_DIR} clean
	rm -f ${UART_FILES}

timers_test:
	echo "Run Timers test..."
	$(MAKE) -C ${RESOURCES_DIR} rtems_timers
	./${SIS_APP_DIR}/${SIS_NAME}-${SIS_VERSION} -dumbio -uart1 stdio -r ${TEST_BUILD_DIR}/${TIMERS_TEST} > ${OUTPUT_FILE}
	grep -q "Success" ${OUTPUT_FILE}
	$(MAKE) -C ${RESOURCES_DIR} clean
	rm -f ${OUTPUT_FILE}

uart_bidirectional_test:
	echo "Run UART bidirectional test..."
	$(MAKE) -C ${RESOURCES_DIR} rtems_uart_bidirectional
	socat -x pty,raw,echo=0,link=uart1 pty,raw,echo=0,link=remote_uart & echo $$! > socat.PID
	sleep 1
	echo x > remote_uart
	timeout --kill-after 1s 5s ./${SIS_APP_DIR}/${SIS_NAME}-${SIS_VERSION} -d 10 -freq 100 -m 1 -v -uart1 uart1 -leon3 -r ${TEST_BUILD_DIR}/${UART_BIDIRECTIONAL_TEST}
	-timeout --kill-after 1s 1s cat remote_uart > test.log
	grep -q "y" test.log
	rm test.log
	if [ -e socat.PID ]; then \
		kill -TERM $$(cat socat.PID) || true ; \
		rm socat.PID ; \
	fi;
