#include <stdint.h>

#pragma once

#define GPT_TIMERS_VALUE 4
#define GPT_IRQ_VALUE 8

/* Control Register definition, taken from GR712RC documentation:
    (0) EN - Enable: Enable the timer.
    (1) RS - Restart: If set, the timer counter value register is reloaded with the value of the reload register when the timer underflows.
    (2) LD - Load: Load value from the timer reload register to the timer counter value register.
    (3) IE - Interrupt Enable: If set the timer signals interrupt when it underflows.
    (4) IP - Interrupt Pending: The core sets this bit to ‘1’ when an interrupt is signalled. This bit remains ‘1’ until cleared by writing ‘0’ to this bit.
    (5) CH - Chain: Chain with preceding timer. If set for timer n, timer n will be decremented each time when timer (n-1) underflows.
    (6) DH - Debug Halt: Value of GPTI.DHALT signal which is used to freeze counters (e.g. when a system is in debug mode). Read-only.
*/

typedef struct 
{
    uint8_t GPT_EN;
    uint8_t GPT_RS;
    uint8_t GPT_LD;
    uint8_t GPT_IE;
    uint8_t GPT_IP;
    uint8_t GPT_CH;
    uint8_t GPT_DH;
} control_register;

typedef struct
{
    uint32_t address;
    uint32_t counter_value_register;
    uint32_t reload_value_register;
    control_register control;
} gp_timer;

/*
    Configuration Register definition, taken from GR712RC documentation:
    (0 - 2) TIMERS - Number of implemented timers. Set to 4. Read-only
    (3 - 7) IRQ - Interrupt ID of first timer. Set to 8. Read-only.
    (8)     SI - Separate interrupts. Reads ‘1’ to indicate the timer unit generates separate interrupts for each timer.
    (9)     DF - Disable timer freeze. If set the timer unit can not be freezed, otherwise timers will halt when the processor enters debug mode.
*/

typedef struct 
{
    uint8_t GPT_TIMERS;
    uint8_t GPT_IRQ;
    uint8_t GPT_SI;
    uint8_t GPT_DF;
} configuration_register;

typedef struct
{
    uint32_t address;
    uint32_t scaler_value;
    uint32_t scaler_reload_value;
    configuration_register configuration;
    gp_timer timers[4];
} gp_timer_unit;

extern gp_timer_unit gptimer_unit;
