/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
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

#define CONFIGURE_INIT_TASK_ATTRIBUTES (RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT)

#define CONFIGURE_INIT_TASK_INITIAL_MODES (RTEMS_DEFAULT_MODES | RTEMS_INTERRUPT_LEVEL(0))

#define CONFIGURE_INIT_TASK_CONSTRUCT_STORAGE_SIZE TASK_STORAGE_SIZE

#define CONFIGURE_INIT

#define TIMER1_CORE_ADDRESS 0x80000300U
#define TIMER2_CORE_ADDRESS 0x80100600U
#define TIMER_BASE_ADDRESS 0x10U
#define TIMER_SIZE 0x10U
#define TIMER1_SCALER_RELOAD_VALUE 0x3U
#define TIMER2_SCALER_RELOAD_VALUE 0x2U
#define TIMER_COUNTER_UNDERFLOW_VALUE -1U
#define TIMER_COUNTER_START_VALUE 0xFFFFFFFU
#define TIMER_ENABLE 0x01U

#define TIMER1_SIZE 4
#define TIMER2_SIZE 2

#define TIMEOUT 1000000000
#define ACCEPTABLE_DIFFERENCE 0.05f

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

struct TimerStatus
{
  int expiredTimerCount;
  int expirationTime;
};

Timer1Register timer1;
Timer2Register timer2;

void sendMsg (const char *msg)
{
  for (size_t i = 0; i < strlen(msg); i++) {
    rtems_putc(msg[i]);
  }
}

void checkTimersWorking (volatile TimerUnit *timers, int timersSize, struct TimerStatus *timerStatus, uint32_t timestamp)
{
  if (timerStatus->expirationTime == 0) {
    timerStatus->expiredTimerCount = 0;
    for (int i = 0; i < timersSize; i++) {
      if (timers[i]->timerCounter == TIMER_COUNTER_UNDERFLOW_VALUE) {
        timerStatus->expiredTimerCount++;
      }
    }

    if (timerStatus->expiredTimerCount == timersSize) {
      timerStatus->expirationTime = timestamp;
    }
  }
}

static void timerInit ()
{
  timer1.core = (CoreUnit) TIMER1_CORE_ADDRESS;
  timer1.core->reloadScaler = TIMER1_SCALER_RELOAD_VALUE;
  for (size_t i = 0; i < TIMER1_SIZE; i++) {
    timer1.timers[i] = (TimerUnit) (TIMER1_CORE_ADDRESS + TIMER_BASE_ADDRESS + (i * TIMER_SIZE));
    timer1.timers[i]->timerCounter = TIMER_COUNTER_START_VALUE;
    timer1.timers[i]->timerControl = TIMER_ENABLE;
  }

  timer2.core = (CoreUnit) TIMER2_CORE_ADDRESS;
  timer2.core->reloadScaler = TIMER2_SCALER_RELOAD_VALUE;
  for (size_t i = 0; i < TIMER2_SIZE; i++) {
    timer2.timers[i] = (TimerUnit) (TIMER2_CORE_ADDRESS + TIMER_BASE_ADDRESS + (i * TIMER_SIZE));
    timer2.timers[i]->timerCounter = TIMER_COUNTER_START_VALUE;
    timer2.timers[i]->timerControl = TIMER_ENABLE;
  }
}

static void Init( rtems_task_argument arg )
{
  (void) arg;

  struct TimerStatus timer1Status = {.expirationTime = 0, .expiredTimerCount = 0};
  struct TimerStatus timer2Status = {.expirationTime = 0, .expiredTimerCount = 0};

  uint32_t timeout = 0;

  timerInit ();

  while (((timer1Status.expiredTimerCount != TIMER1_SIZE) || (timer2Status.expiredTimerCount != TIMER2_SIZE)) && (timeout != TIMEOUT)) {
    checkTimersWorking (timer1.timers, TIMER1_SIZE, &timer1Status, timeout);
    checkTimersWorking (timer2.timers, TIMER2_SIZE, &timer2Status, timeout);
    timeout++;
  }

  bool timersEndsInProperOrder = false;
  float scalerDifferenceFactor = (float) TIMER1_SCALER_RELOAD_VALUE / (float) TIMER2_SCALER_RELOAD_VALUE;
  float expirationDifferenceFactor = (float) timer1Status.expirationTime / (float) timer2Status.expirationTime;

  if (fabs(scalerDifferenceFactor - expirationDifferenceFactor) < scalerDifferenceFactor * ACCEPTABLE_DIFFERENCE) {
    timersEndsInProperOrder = true;
  }

  if ((timer1Status.expiredTimerCount == TIMER1_SIZE) && (timer2Status.expiredTimerCount == TIMER2_SIZE) && timersEndsInProperOrder) {
    sendMsg("Success\n");
  } else {
    sendMsg("Failed\n");
  }
}

#include <rtems/confdefs.h>
