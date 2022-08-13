#include "timer.h"

void gptimer_apbctrl1_update()
{
  if (gptimer1.core.scaler_register > 0)
  {
    gptimer1.core.scaler_register--;
  }
  else {
    gptimer1.core.scaler_register = gptimer1.core.scaler_reload_register;
    for (size_t i = 0; i < GPTIMER_APBCTRL1_SIZE; i++)
    {
      gptimer_timer_update(&gptimer1.timers[i]);
    }
  }
}

// TODO: do wywalenia i zastąpienia eventem gptimer_apbctrl1_update
void gptimer_scaler_update (uint32_t timestamp, gp_timer_core *core)
{
  core->scaler_register = core->scaler_register - ((timestamp - core->scaler_start_time) % (core->scaler_reload_register + 1));
}

void gptimer_timer_update(gp_timer *timer)
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
      // TODO: i tu muszę jakoś to wymyślić, bo muszę wiedzieć że teraz jestem przy timerze nr n, i sprawdzić, czy timer numer n-1 się przekręcił, jak tak to decrementować
    }
    else {
      gptimer_decrement(timer);
    }
  }

  // read chain bit >> if chain: check if chained timer reloads; decrement timer if it does HALFLY DONE
}

void gptimer_decrement(gp_timer *timer)
{
  if (timer->counter_value_register > 0)
  {
    timer->counter_value_register--;
  }
  else
  {
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

uint32_t gptimer_read_core_register(gp_timer_core *core, uint32_t address)
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

uint32_t gptimer_read_timer_register(gp_timer *timer, uint32_t address)
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

uint32_t gptimer_get_flag(uint32_t gpt_register, uint32_t flag)
{
  return (gpt_register >> flag) & 0x1;
}

void gptimer_set_flag(uint32_t *gpt_register, uint32_t flag)
{
  *gpt_register |= (0x1 << flag);
}

void gptimer_reset_flag(uint32_t *gpt_register, uint32_t flag)
{
  *gpt_register &= ~(0x1 << flag);
}
