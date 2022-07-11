SIS_NAME = sis
SIS_VERSION = 2.29

SRC_DIR = src
BUILD_DIR = ../../build
RTEMS_APP_DIR = /opt/rtems-6-sparc-gr712rc-smp-4/src/example/b-gr712rc-qual-only/app.exe
RESOURCES_DIR = resources/expectedResult

EXPECTED := "$(shell cat $(RESOURCES_DIR))"
RESULT := "$(shell ./$(BUILD_DIR)/$(SRC_DIR)/$(SIS_NAME)-$(SIS_VERSION) -dumbio -r $(RTEMS_APP_DIR))"
TESTS = run_rtems

check: ${TESTS}

.SILENT :
	${TESTS}

.PHONY :
	${TESTS}
	check

##############################INTEGRATION TESTS################################

run_rtems:
	echo "Run example RTEMS application..."
ifeq ($(EXPECTED), $(RESULT))
	echo "Success."
else
	$(error "RTEMS application output is different than expected.")
endif
