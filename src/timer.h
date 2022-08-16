#include <stdint.h>
#include <stddef.h>

#pragma once

#define GPTIMER_APBCTRL1_ADDRESS     0x80000300
#define GPTIMER_APBCTRL2_ADDRESS     0x80100600

#define CORE_OFFSET 0x00
#define GPTIMER1_OFFSET 0x10
#define GPTIMER2_OFFSET 0x20
#define GPTIMER3_OFFSET 0x30
#define GPTIMER4_OFFSET 0x40

#define GPTIMER_SCALER_VALUE_REGISTER_ADDRESS 0x00
#define GPTIMER_SCALER_RELOAD_VALUE_REGISTER_ADDRESS 0x04
#define GPTIMER_CONFIGURATION_REGISTER_ADDRESS 0x08

#define GPTIMER_TIMER_COUNTER_VALUE_REGISTER_ADDRESS 0x00
#define GPTIMER_TIMER_RELOAD_VALUE_REGISTER_ADDRESS 0x04
#define GPTIMER_TIMER_CONTROL_REGISTER_ADDRESS 0x08
#define GPTIMER_TIMER_LATCH_REGISTER_ADDRESS 0x0C

#define GPTIMER_SCALER_REGISTER_INIT_VALUE 0xFFFF
#define GPTIMER_SCALER_RELOAD_REGISTER_INIT_VALUE 0xFFFF
#define GPTIMER_CONFIGURATION_REGISTER_INIT_VALUE 0x0044

#define GPTIMER4_COUNTER_VALUE_REGISTER_INIT_VALUE 0xFFFF
#define GPTIMER4_RELOAD_VALUE_REGISTER_INIT_VALUE 0xFFFF
#define GPTIMER4_CONTROL_REGISTER_INIT_VALUE 0x0009

#define GPTIMER_ADDRESS_MASK 0xFFFF
#define GPTIMER_REGISTERS_MASK 0xFF
#define GPTIMER_OFFSET_MASK 0xF0
#define GPTIMER_TIMERS_REGISTERS_MASK 0x0F
#define GPTIMER_FLAG_MASK 0x01
#define GPTIMER_SCALER_REGISTER_WRITE_MASK 0xFFFF
#define GPTIMER_CONFIGURATION_REGISTER_WRITE_MASK 0x180
#define GPTIMER_CONTROL_REGISTER_WRITE_MASK 0x2F

#define GPTIMER_APBCTRL1_SIZE 4
#define GPTIMER_APBCTRL2_SIZE 2

#define GPTIMER_INTERRUPT_BASE_NR 8

/* Control Register definition, taken from GR712RC documentation:
    (0) EN - Enable: Enable the timer.
    (1) RS - Restart: If set, the timer counter value register is reloaded with the value of the reload register when the timer underflows.
    (2) LD - Load: Load value from the timer reload register to the timer counter value register.
    (3) IE - Interrupt Enable: If set the timer signals interrupt when it underflows.
    (4) IP - Interrupt Pending: The core sets this bit to ‘1’ when an interrupt is signalled. This bit remains ‘1’ until cleared by writing ‘0’ to this bit.
    (5) CH - Chain: Chain with preceding timer. If set for timer n, timer n will be decremented each time when timer (n-1) underflows.
    (6) DH - Debug Halt: Value of GPTI.DHALT signal which is used to freeze counters (e.g. when a system is in debug mode). Read-only.
*/
typedef enum {GPT_EN = 0, GPT_RS, GPT_LD, GPT_IE, GPT_IP, GPT_CH, GPT_DH} gptimer_control_register;

typedef struct
{
    uint32_t counter_value_register;
    uint32_t reload_value_register;
    uint32_t control_register;
    uint32_t latch_register;
    uint32_t *timer_chain_underflow_ptr;
    uint32_t timer_underflow;
} gp_timer;

/*
    Configuration Register definition, taken from GR712RC documentation:
    (0 - 2) TIMERS - Number of implemented timers. Set to 4. Read-only
    (3 - 7) IRQ - Interrupt ID of first timer. Set to 8. Read-only.
    (8)     SI - Separate interrupts. Reads ‘1’ to indicate the timer unit generates separate interrupts for each timer.
    (9)     DF - Disable timer freeze. If set the timer unit can not be freezed, otherwise timers will halt when the processor enters debug mode.
*/
typedef enum {GPT_TIMERS = 0, GPT_IRQ = 3, GPT_SI = 8, GPT_DF = 9} gptimer_configuration_register;

typedef struct
{
    uint32_t scaler_register;
    uint32_t scaler_reload_register;
    uint32_t configuration_register;
    uint32_t timer_latch_configuration_register;
} gp_timer_core;

typedef struct
{
    gp_timer_core core;
    gp_timer timers[GPTIMER_APBCTRL1_SIZE];
} gp_timer_apbctrl1;

typedef struct
{
    gp_timer_core core;
    gp_timer timers[GPTIMER_APBCTRL2_SIZE];
} gp_timer_apbctrl2;

extern gp_timer_apbctrl1 gptimer1;
extern gp_timer_apbctrl2 gptimer2;

void gptimer_apbctrl1_update();
void gptimer_apbctrl2_update();
void gptimer_timer_update(gp_timer *timer);
void gptimer_decrement(gp_timer *timer);
void gptimer_apbctrl1_timer_reset();
void gptimer_apbctrl2_timer_reset();

uint32_t gptimer_read_core_register(gp_timer_core *core, uint32_t address);
void gptimer_write_core_register(gp_timer_core *core, uint32_t address, uint32_t * data);

uint32_t gptimer_read_timer_register(gp_timer *timer, uint32_t address);
void gptimer_write_timer_register(gp_timer *timer, uint32_t address, uint32_t * data);

uint32_t gptimer_get_flag(uint32_t gpt_register, uint32_t flag);
void gptimer_set_flag(uint32_t *gpt_register, uint32_t flag);
void gptimer_reset_flag(uint32_t *gpt_register, uint32_t flag);
