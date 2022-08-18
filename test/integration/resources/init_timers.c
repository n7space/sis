/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <rtems.h>
#include <rtems/bspIo.h>
#include <bsp/irq.h>

#define IRQMP_BASE 0x80000200U

#define IRQ_LEVEL(n) (((IRQMP_BASE) >> n) | 1U)

#define MAX_TLS_SIZE RTEMS_ALIGN_UP( 64, RTEMS_TASK_STORAGE_ALIGNMENT )

#define TASK_ATTRIBUTES RTEMS_DEFAULT_ATTRIBUTES

#define TASK_STORAGE_SIZE \
  RTEMS_TASK_STORAGE_SIZE( \
    MAX_TLS_SIZE + RTEMS_MINIMUM_STACK_SIZE, \
    RTEMS_DEFAULT_ATTRIBUTES \
  )

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 3

#define CONFIGURE_MINIMUM_TASKS_WITH_USER_PROVIDED_STORAGE CONFIGURE_MAXIMUM_TASKS

#define CONFIGURE_MICROSECONDS_PER_TICK 1000

#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 0

#define CONFIGURE_DISABLE_NEWLIB_REENTRANCY

#define CONFIGURE_APPLICATION_DISABLE_FILESYSTEM

#define CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE MAX_TLS_SIZE

#define CONFIGURE_INTERRUPT_STACK_SIZE RTEMS_ALIGN_UP((4 * CPU_STACK_MINIMUM_SIZE) + CONTEXT_FP_SIZE, CPU_STACK_ALIGNMENT)

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_DEFAULT_ATTRIBUTES

#define CONFIGURE_INIT_TASK_INITIAL_MODES (RTEMS_DEFAULT_MODES | RTEMS_INTERRUPT_LEVEL(0))

#define CONFIGURE_INIT_TASK_CONSTRUCT_STORAGE_SIZE TASK_STORAGE_SIZE

#define CONFIGURE_INIT

#define TIMER1_CORE_ADDRESS 0x80000300U
#define TIMER2_CORE_ADDRESS 0x80100600U
#define TIMER_BASE_ADDRESS 0x10U
#define TIMER_SIZE 0x10U
#define TIMEOUT 100000
#define TIMER_COUNTER_UNDERFLOW_VALUE 0xFFFFFFFFU

#define TIMER1_SIZE 4
#define TIMER2_SIZE 2
#define ALL_TIMERS_SIZE (TIMER1_SIZE + TIMER2_SIZE)

typedef volatile struct
{
  uint32_t scaler;
  uint32_t reloadScaler;
  uint32_t configuration;
} *CoreUnit;

typedef volatile struct
{
  uint32_t timerCounter;
  uint32_t timerReloadCounter;
  uint32_t timerControl;
} *TimerUnit;

typedef volatile struct
{
  CoreUnit core;
  TimerUnit timers[TIMER1_SIZE];
} Timer1Register;

typedef volatile struct
{
  CoreUnit core;
  TimerUnit timers[TIMER2_SIZE];
} Timer2Register;

Timer1Register timer1;
Timer2Register timer2;

void sendMsg (const char *msg)
{
  for (size_t i = 0; i < strlen(msg); i++) {
    rtems_putc(msg[i]);
  }
}

static void timerInit()
{
  timer1.core = (CoreUnit) TIMER1_CORE_ADDRESS;
  for (size_t i = 0; i < TIMER1_SIZE; i++) {
    timer1.timers[i] = (TimerUnit) (TIMER1_CORE_ADDRESS + TIMER_BASE_ADDRESS + (i * TIMER_SIZE));
    timer1.timers[i]->timerCounter = 0xFF;
    timer1.timers[i]->timerControl = 0x01;
  }

  timer2.core = (CoreUnit) TIMER2_CORE_ADDRESS;
  for (size_t i = 0; i < TIMER2_SIZE; i++) {
    timer2.timers[i] = (TimerUnit) (TIMER2_CORE_ADDRESS + TIMER_BASE_ADDRESS + (i * TIMER_SIZE));
    timer2.timers[i]->timerCounter = 0xFF;
    timer2.timers[i]->timerControl = 0x01;
  }
}

static void Init( rtems_task_argument arg )
{
  (void) arg;

  timerInit();

  int timersUnderflowed = 0;
  int timeout = 0;
  while ((timersUnderflowed != ALL_TIMERS_SIZE) && (timeout != TIMEOUT)) {
    timersUnderflowed = 0;
    for (size_t i = 0; i < TIMER1_SIZE; i++) {
      if (timer1.timers[i]->timerCounter == TIMER_COUNTER_UNDERFLOW_VALUE) {
        timersUnderflowed++;
      }
    }

    for (size_t i = 0; i < TIMER2_SIZE; i++) {
      if (timer2.timers[i]->timerCounter == TIMER_COUNTER_UNDERFLOW_VALUE) {
        timersUnderflowed++;
      }
    }

    timeout++;
  }

  if (timersUnderflowed == ALL_TIMERS_SIZE) {
    sendMsg("Success\n");
  } else {
    sendMsg("Failed\n");
  }
}

#include <rtems/confdefs.h>
