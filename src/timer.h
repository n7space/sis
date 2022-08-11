#include <stdint.h>

#pragma once

#define GPT_TIMERS_VALUE 4
#define GPT_IRQ_VALUE 8

#define GPTIMER_BASE_ADDRESS 0x80000300
#define GPTIMER1_START_ADDRESS 0x80000310
#define GPTIMER2_START_ADDRESS 0x80000320
#define GPTIMER3_START_ADDRESS 0x80000330
#define GPTIMER4_START_ADDRESS 0x80000340

#define SCALER_REGISTER_INIT_VALUE 0xFFFF
#define SCALER_RELOAD_REGISTER_INIT_VALUE 0xFFFF
#define CONFIGURATION_REGISTER_INIT_VALUE 0x0044

#define GPTIMER4_COUNTER_VALUE_REGISTER_INIT_VALUE 0xFFFF
#define GPTIMER4_RELOAD_VALUE_REGISTER_INIT_VALUE 0xFFFF
#define GPTIMER4_CONTROL_REGISTER_INIT_VALUE 0x0009

/* Control Register definition, taken from GR712RC documentation:
    (0) EN - Enable: Enable the timer.
    (1) RS - Restart: If set, the timer counter value register is reloaded with the value of the reload register when the timer underflows.
    (2) LD - Load: Load value from the timer reload register to the timer counter value register.
    (3) IE - Interrupt Enable: If set the timer signals interrupt when it underflows.
    (4) IP - Interrupt Pending: The core sets this bit to ‘1’ when an interrupt is signalled. This bit remains ‘1’ until cleared by writing ‘0’ to this bit.
    (5) CH - Chain: Chain with preceding timer. If set for timer n, timer n will be decremented each time when timer (n-1) underflows.
    (6) DH - Debug Halt: Value of GPTI.DHALT signal which is used to freeze counters (e.g. when a system is in debug mode). Read-only.
*/
enum gptimer_control_register {EN = 0, RS, LD, IE, IP, CH, DH};

typedef struct
{
    uint32_t counter_value_register;
    uint32_t reload_value_register;
    uint32_t control_register;
} gp_timer;

/*
    Configuration Register definition, taken from GR712RC documentation:
    (0 - 2) TIMERS - Number of implemented timers. Set to 4. Read-only
    (3 - 7) IRQ - Interrupt ID of first timer. Set to 8. Read-only.
    (8)     SI - Separate interrupts. Reads ‘1’ to indicate the timer unit generates separate interrupts for each timer.
    (9)     DF - Disable timer freeze. If set the timer unit can not be freezed, otherwise timers will halt when the processor enters debug mode.
*/
enum gptimer_configuration_register {TIMERS = 0, IRQ = 3, SI = 8, DF = 9};

typedef struct
{
    uint64_t scaler_start_time;
    uint32_t scaler_register;
    uint32_t scaler_reload_register;
    uint32_t configuration_register;
    gp_timer timers[4];
} gp_timer_unit;

extern gp_timer_unit gptimer_unit;


void gptimer_set_value(uint32_t *data, int flag);
uint32_t gptimer_get_value(uint32_t *data, int flag);
