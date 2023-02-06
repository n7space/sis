SIS_NAME = sis
SIS_VERSION = 2.29.1

CC = gcc
G++ = g++

AR = ar

CFLAGS := -O3
LDFLAGS = -lm
CONFIG = -DHAVE_CONFIG_H 
DEFS = -DFAST_UART

BUILD_DIR = build
SRC_DIR = src
UNIT_TEST_DIR = test/unit
INTEGRATION_TEST_DIR = test/integration

RTEMS_APP_DIR = /opt/rtems-6-sparc-gr712rc-smp-4/src/example/b-gr712rc-qual-only/app.exe
