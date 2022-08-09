/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Copyright (C) 2020 embedded brains GmbH (http://www.embedded-brains.de)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define UART_TE 0x04
#define UART_DR 0x01
#define DATA_MASK 0xFF

#define MAX_RECEIVE_MSG_SIZE 20

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
}

static void Init( rtems_task_argument arg )
{
  (void) arg;

  uarts_init();

  char *send_msg = "Data transmitted successfully via uart.\n";
  size_t send_msg_size = strlen(send_msg);

  char receive_msg[MAX_RECEIVE_MSG_SIZE];
  size_t receive_msg_size = 0;

  for (size_t i = 0; i < UARTS_SIZE; i++)
  {
    while (uarts[i]->status & UART_DR) {
      if (receive_msg_size <= MAX_RECEIVE_MSG_SIZE)
      {
        char data = uarts[i]->data & DATA_MASK;
        receive_msg[receive_msg_size++] = data;
      }
      else
      {
        return;
      }
    }

    if (receive_msg_size == 0)
    {
      return;
    }
    
    for (size_t j = 0; j < send_msg_size; j++)
    {
      while (!(uarts[i]->status & UART_TE));
      uarts[i]->data = send_msg[j];
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
