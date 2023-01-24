/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <rtems.h>

#define MAX_TLS_SIZE RTEMS_ALIGN_UP( 64, RTEMS_TASK_STORAGE_ALIGNMENT )

#define TASK_ATTRIBUTES RTEMS_DEFAULT_ATTRIBUTES

#define TASK_STORAGE_SIZE \
  RTEMS_TASK_STORAGE_SIZE( \
    MAX_TLS_SIZE + RTEMS_MINIMUM_STACK_SIZE, \
    TASK_ATTRIBUTES \
  )

#define UART0_BASE_ADDRESS 0x80000100
#define UART1_BASE_ADDRESS 0x80100100

#define UART_TS 0x02
#define UART_DR 0x01
#define DATA_MASK 0xFF

#define UART_RE 0x01
#define UART_TE 0x02

#define MAX_RECEIVE_MSG_SIZE 20

#define WAIT_FOR_UART_DELAY 10000

typedef volatile struct
{
    uint32_t data;
    uint32_t status;
    uint32_t control;
    uint32_t clkscl;
    uint32_t debug;
} *UartRegister;

static char get_char(UartRegister uart)
{
    while (1)
    {
        if (uart->status & UART_DR) {
            char data = uart->data & DATA_MASK;
            return data;
        }
    }
}

static void send_char(UartRegister uart, char msg)
{
    while (1)
    {
        if (uart->status & UART_TS)
        {
            uart->data = msg;
            return;
        }
    }
}

static void Init( rtems_task_argument arg )
{
    (void) arg;

    UartRegister uart = (UartRegister) UART0_BASE_ADDRESS;
    uart->control = (uint32_t) (UART_RE | UART_TE); 

    char data = get_char(uart);

    if(data == 'x') {
        send_char(uart, 'y');
    } else {
        send_char(uart, data);
    }
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 3

#define CONFIGURE_MICROSECONDS_PER_TICK 1000

#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 0

#define CONFIGURE_DISABLE_NEWLIB_REENTRANCY

#define CONFIGURE_APPLICATION_DISABLE_FILESYSTEM

#define CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE MAX_TLS_SIZE

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT_TASK_ATTRIBUTES TASK_ATTRIBUTES

#define CONFIGURE_INIT_TASK_INITIAL_MODES RTEMS_DEFAULT_MODES

#define CONFIGURE_INIT_TASK_CONSTRUCT_STORAGE_SIZE TASK_STORAGE_SIZE

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
