/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <rtems.h>
#include <rtems/bspIo.h>

#define MAX_TLS_SIZE RTEMS_ALIGN_UP( 64, RTEMS_TASK_STORAGE_ALIGNMENT )

#define TASK_ATTRIBUTES RTEMS_DEFAULT_ATTRIBUTES

#define TASK_STORAGE_SIZE \
  RTEMS_TASK_STORAGE_SIZE( \
    MAX_TLS_SIZE + RTEMS_MINIMUM_STACK_SIZE, \
    TASK_ATTRIBUTES \
  )

#define UARTS_SIZE 6

#define UART0_BASE_ADDRESS 0x80000100
#define UART1_BASE_ADDRESS 0x80100100
#define UART2_BASE_ADDRESS 0x80100200
#define UART3_BASE_ADDRESS 0x80100300
#define UART4_BASE_ADDRESS 0x80100400
#define UART5_BASE_ADDRESS 0x80100500

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

UartRegister uarts[UARTS_SIZE];

static void uarts_init()
{
  uarts[0] = (UartRegister) UART0_BASE_ADDRESS;
  uarts[1] = (UartRegister) UART1_BASE_ADDRESS;
  uarts[2] = (UartRegister) UART2_BASE_ADDRESS;
  uarts[3] = (UartRegister) UART3_BASE_ADDRESS;
  uarts[4] = (UartRegister) UART4_BASE_ADDRESS;
  uarts[5] = (UartRegister) UART5_BASE_ADDRESS;

  for (size_t i = 0; i < UARTS_SIZE; i++)
  {
    uarts[i]->control = (uint32_t) (UART_RE | UART_TE); 
  }
}

static void Init( rtems_task_argument arg )
{
  (void) arg;

  uarts_init();

  const char *send_msg = "Data transmission via uart successful.\n";
  const char *expected_received_msg = "Readme!\n";

  size_t send_msg_size = strlen(send_msg);

  for (size_t i = 0; i < UARTS_SIZE; i++)
  {
    char receive_msg[MAX_RECEIVE_MSG_SIZE];
    size_t receive_msg_size = 0;

    for (int data_size = 0; data_size < MAX_RECEIVE_MSG_SIZE; data_size++)
    {
      int receive_delay = 0;
      while (receive_delay < WAIT_FOR_UART_DELAY)
      {
        if (uarts[i]->status & UART_DR)
        {
          char data = uarts[i]->data & DATA_MASK;
          receive_msg[receive_msg_size++] = data;
          break;
        }
        else
        {
          receive_delay++;
        }
      }
    }

    if (strcmp(expected_received_msg, receive_msg) == 0)
    {
      for (size_t j = 0; j < send_msg_size; j++)
      {
        int transmit_delay = 0;
        while (transmit_delay < WAIT_FOR_UART_DELAY)
        {
          if (uarts[i]->status & UART_TS)
          {
            uarts[i]->data = send_msg[j];
            break;
          }
          else
          {
            transmit_delay++;
          }
        }
      }
    }
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
