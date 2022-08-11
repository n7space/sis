#include "timer.h"

void gptimer_scaler_update (uint32_t timestamp, gp_timer_core *core)
{
  core->scaler_register = core->scaler_register - ((timestamp - core->scaler_start_time) % (core->scaler_reload_register + 1));
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
      result = 0;
      break;
    }
    case GPTIMER_TIMER_RELOAD_VALUE_REGISTER_ADDRESS:
    {
      result = 0;
      break;
    }
    case GPTIMER_TIMER_CONTROL_REGISTER_ADDRESS:
    {
      result = 0;
      break;
    }
    case GPTIMER_TIMER_LATCH_REGISTER_ADDRESS:
    {
      result = 0;
      break;
    }
    default:
    {
      break;
    }
  }

  return result;
}
