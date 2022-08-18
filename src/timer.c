#include "timer.h"

void
gptimer_update(gp_timer_core *core, gp_timer *timers, uint32_t timers_size)
{
  if (core->scaler_register > 0)
  {
    core->scaler_register--;
  }
  else {
    core->scaler_register = core->scaler_reload_register;
    for (size_t i = 0; i < timers_size; i++)
    {
      gptimer_timer_update(&timers[i]);
    }
  }
}

void
gptimer_timer_update(gp_timer *timer)
{
  if (gptimer_get_flag(timer->control_register, GPT_EN))
  {
    if (gptimer_get_flag(timer->control_register, GPT_LD))
    {
      timer->counter_value_register = timer->reload_value_register;
      gptimer_reset_flag(&timer->control_register, GPT_LD);
    }
    else if (gptimer_get_flag(timer->control_register, GPT_CH))
    {
      if (*timer->timer_chain_underflow_ptr > 0)
      {
        *timer->timer_chain_underflow_ptr = 0;
        gptimer_decrement(timer);
      }
    }
    else {
      gptimer_decrement(timer);
    }
  }
}

void
gptimer_decrement(gp_timer *timer)
{
  if (timer->counter_value_register > 0)
  {
    timer->counter_value_register--;
  }
  else
  {
    timer->timer_underflow = 1;

    if (gptimer_get_flag(timer->control_register, GPT_RS))
    {
      timer->counter_value_register = timer->reload_value_register;
    }
    else {
      timer->counter_value_register--;
      gptimer_reset_flag(&timer->control_register, GPT_EN);
    }

    if(gptimer_get_flag(timer->control_register, GPT_IE))
    {
      gptimer_set_flag(&timer->control_register, GPT_IP);
    }
  }
}

void
gptimer_apbctrl1_timer_reset()
{
  gptimer1.core.scaler_register = GPTIMER_APBCTRL1_SCALER_REGISTER_INIT_VALUE;
  gptimer1.core.scaler_reload_register = GPTIMER_APBCTRL1_SCALER_RELOAD_REGISTER_INIT_VALUE;
  gptimer1.core.configuration_register = GPTIMER_APBCTRL1_CONFIGURATION_REGISTER_INIT_VALUE;
  
  gptimer1.timers[0].counter_value_register = 0;
  gptimer1.timers[0].reload_value_register = 0;
  gptimer1.timers[0].control_register = 0;
  gptimer1.timers[0].timer_underflow = 0;
  gptimer1.timers[0].timer_chain_underflow_ptr = NULL;

  gptimer1.timers[1].counter_value_register = 0;
  gptimer1.timers[1].reload_value_register = 0;
  gptimer1.timers[1].control_register = 0;
  gptimer1.timers[1].timer_underflow = 0;
  gptimer1.timers[1].timer_chain_underflow_ptr = &gptimer1.timers[0].timer_underflow;

  gptimer1.timers[2].counter_value_register = 0;
  gptimer1.timers[2].reload_value_register = 0;
  gptimer1.timers[2].control_register = 0;
  gptimer1.timers[2].timer_underflow = 0;
  gptimer1.timers[2].timer_chain_underflow_ptr = &gptimer1.timers[1].timer_underflow;

  gptimer1.timers[3].counter_value_register = GPTIMER4_COUNTER_VALUE_REGISTER_INIT_VALUE;
  gptimer1.timers[3].reload_value_register = GPTIMER4_RELOAD_VALUE_REGISTER_INIT_VALUE;
  gptimer1.timers[3].control_register = GPTIMER4_CONTROL_REGISTER_INIT_VALUE;
  gptimer1.timers[3].timer_underflow = 0;
  gptimer1.timers[3].timer_chain_underflow_ptr = &gptimer1.timers[2].timer_underflow;
}

void
gptimer_apbctrl2_timer_reset()
{
  gptimer2.core.scaler_register = GPTIMER_APBCTRL2_SCALER_REGISTER_INIT_VALUE;
  gptimer2.core.scaler_reload_register = GPTIMER_APBCTRL2_SCALER_RELOAD_REGISTER_INIT_VALUE;
  gptimer2.core.configuration_register = GPTIMER_APBCTRL2_CONFIGURATION_REGISTER_INIT_VALUE;
  gptimer2.core.timer_latch_configuration_register = GPTIMER_APBCTRL2_LATCH_CONFIGURATION_REGISTER_INIT_VALUE;
  
  gptimer2.timers[0].counter_value_register = 0;
  gptimer2.timers[0].reload_value_register = 0;
  gptimer2.timers[0].control_register = 0;
  gptimer2.timers[0].latch_register = 0;
  gptimer2.timers[0].timer_underflow = 0;
  gptimer2.timers[0].timer_chain_underflow_ptr = NULL;

  gptimer2.timers[1].counter_value_register = 0;
  gptimer2.timers[1].reload_value_register = 0;
  gptimer2.timers[1].control_register = 0;
  gptimer2.timers[1].latch_register = 0;
  gptimer2.timers[1].timer_underflow = 0;
  gptimer2.timers[1].timer_chain_underflow_ptr = &gptimer2.timers[0].timer_underflow;
}

uint32_t
gptimer_read_core_register(gp_timer_core *core, uint32_t address)
{
  uint32_t result = 0;

  switch (address)
  {
    case GPTIMER_SCALER_VALUE_REGISTER_ADDRESS:
    {
      result = gptimer1.core.scaler_register;
      break;
    }
    case GPTIMER_SCALER_RELOAD_VALUE_REGISTER_ADDRESS:
    {
      result = gptimer1.core.scaler_reload_register;
      break;
    }
    case GPTIMER_CONFIGURATION_REGISTER_ADDRESS:
    {
      result = gptimer1.core.configuration_register;
      break;
    }
    default:
    {
      break;
    }
  }

  return result;
}

void
gptimer_apbctrl1_write_core_register(uint32_t address, uint32_t * data)
{
  switch (address)
  {
    case GPTIMER_SCALER_VALUE_REGISTER_ADDRESS:
    {
      break;
    }
    case GPTIMER_SCALER_RELOAD_VALUE_REGISTER_ADDRESS:
    {
      if (gptimer1.core.scaler_reload_register != (*data) & GPTIMER_APBCTRL1_SCALER_REGISTER_WRITE_MASK)
      {
        gptimer1.core.scaler_reload_register = (*data) & GPTIMER_APBCTRL1_SCALER_REGISTER_WRITE_MASK;
      }
      break;
    }
    case GPTIMER_CONFIGURATION_REGISTER_ADDRESS:
    {
      gptimer1.core.configuration_register = (*data) & GPTIMER_APBCTRL1_CONFIGURATION_REGISTER_WRITE_MASK + GPTIMER_APBCTRL1_CONFIGURATION_REGISTER_INIT_VALUE;
      break;
    }
    default:
    {
      break;
    }
  }
}

void
gptimer_apbctrl2_write_core_register(uint32_t address, uint32_t * data)
{
  switch (address)
  {
    case GPTIMER_SCALER_VALUE_REGISTER_ADDRESS:
    {
      break;
    }
    case GPTIMER_SCALER_RELOAD_VALUE_REGISTER_ADDRESS:
    {
      if (gptimer2.core.scaler_reload_register != (*data) & GPTIMER_APBCTRL2_SCALER_REGISTER_WRITE_MASK)
      {
        gptimer2.core.scaler_reload_register = (*data) & GPTIMER_APBCTRL2_SCALER_REGISTER_WRITE_MASK;
      }
      break;
    }
    case GPTIMER_CONFIGURATION_REGISTER_ADDRESS:
    {
      gptimer2.core.configuration_register = (*data) & GPTIMER_APBCTRL2_CONFIGURATION_REGISTER_WRITE_MASK + GPTIMER_APBCTRL2_CONFIGURATION_REGISTER_INIT_VALUE;
      break;
    }
    case GPTIMER_LATCH_CONFIGURATION_REGISTER_ADDRESS:
    {
      gptimer2.core.timer_latch_configuration_register = *data;
      break;
    }
    default:
    {
      break;
    }
  }
}

uint32_t
gptimer_read_timer_register(gp_timer *timer, uint32_t address)
{
  uint32_t result = 0;

  switch (address & GPTIMER_TIMERS_REGISTERS_MASK)
  {
    case GPTIMER_TIMER_COUNTER_VALUE_REGISTER_ADDRESS:
    {
      result = timer->counter_value_register;
      break;
    }
    case GPTIMER_TIMER_RELOAD_VALUE_REGISTER_ADDRESS:
    {
      result = timer->reload_value_register;
      break;
    }
    case GPTIMER_TIMER_CONTROL_REGISTER_ADDRESS:
    {
      result = timer->counter_value_register;
      break;
    }
    case GPTIMER_TIMER_LATCH_REGISTER_ADDRESS:
    {
      result = timer->latch_register;
      break;
    }
    default:
    {
      break;
    }
  }

  return result;
}

void
gptimer_write_timer_register(gp_timer *timer, uint32_t address, uint32_t * data)
{
  switch (address & GPTIMER_TIMERS_REGISTERS_MASK)
  {
    case GPTIMER_TIMER_COUNTER_VALUE_REGISTER_ADDRESS:
    {
      timer->counter_value_register = (*data);
      break;
    }
    case GPTIMER_TIMER_RELOAD_VALUE_REGISTER_ADDRESS:
    {
      timer->reload_value_register = (*data);
      break;
    }
    case GPTIMER_TIMER_CONTROL_REGISTER_ADDRESS:
    {
      timer->control_register = (*data) & GPTIMER_CONTROL_REGISTER_WRITE_MASK;
      break;
    }
    case GPTIMER_TIMER_LATCH_REGISTER_ADDRESS:
    {
      timer->latch_register = timer->counter_value_register;
      break;
    }
    default:
    {
      break;
    }
  }
}

uint32_t
gptimer_get_flag(uint32_t gpt_register, uint32_t flag)
{
  return (gpt_register >> flag) & GPTIMER_FLAG_MASK;
}

void
gptimer_set_flag(uint32_t *gpt_register, uint32_t flag)
{
  *gpt_register |= (GPTIMER_FLAG_MASK << flag);
}

void
gptimer_reset_flag(uint32_t *gpt_register, uint32_t flag)
{
  *gpt_register &= ~(GPTIMER_FLAG_MASK << flag);
}
