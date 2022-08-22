#include "uartdef.h"

apbuart_type *
get_uart_by_address (uint32_t address)
{
  apbuart_type *result;

  uint32_t uart_address = (address | APB_START) & ~(APBUART_REGISTER_TYPE_MASK);

  switch(uart_address)
  {
    case APBUART0_START_ADDRESS:
      result = &uarts[0];
      break;
    case APBUART1_START_ADDRESS:
      result = &uarts[1];
      break;
    case APBUART2_START_ADDRESS:
      result = &uarts[2];
      break;
    case APBUART3_START_ADDRESS:
      result = &uarts[3];
      break;
    case APBUART4_START_ADDRESS:
      result = &uarts[4];
      break;
    case APBUART5_START_ADDRESS:
      result = &uarts[5];
      break;
    default:
      result = NULL;
  }

  return result;
}

apbuart_type *
get_uart_by_irq (uint8_t irq)
{
  apbuart_type *result;

  switch(irq)
  {
    case APBUART0_IRQ:
      result = &uarts[0];
      break;
    case APBUART1_IRQ:
      result = &uarts[1];
      break;
    case APBUART2_IRQ:
      result = &uarts[2];
      break;
    case APBUART3_IRQ:
      result = &uarts[3];
      break;
    case APBUART4_IRQ:
      result = &uarts[4];
      break;
    case APBUART5_IRQ:
      result = &uarts[5];
      break;
    default:
      result = NULL;
  }

  return result;
}

uint32_t
apbuart_get_flag(uint32_t apbuart_register, uint32_t flag)
{
  return (apbuart_register >> flag) & APBUART_FLAG_MASK;
}

void
apbuart_set_flag(uint32_t *apbuart_register, uint32_t flag)
{
  *apbuart_register |= (APBUART_FLAG_MASK << flag);
}

void
apbuart_reset_flag(uint32_t *apbuart_register, uint32_t flag)
{
  *apbuart_register &= ~(APBUART_FLAG_MASK << flag);
}

uint32_t
apbuart_get_fifo_count(uint32_t apbuart_status_register, apbuart_fifo_direction flag)
{
  uint32_t result = 0;

  switch (flag)
  {
    case APBUART_FIFO_TRANSMITTER:
    {
      result = (apbuart_status_register >> APBUART_TCNT) & APBUART_FIFO_STATUS_MASK;
      break;
    }
    case APBUART_FIFO_RECEIVER:
    {
      result = (apbuart_status_register >> APBUART_RCNT) & APBUART_FIFO_STATUS_MASK;
      break;
    }
    default:
    {}
  }

  return result;
}

void
apbuart_set_fifo_count(uint32_t apbuart_status_register, apbuart_fifo_direction flag, uint32_t value)
{
  switch (flag)
  {
    case APBUART_FIFO_TRANSMITTER:
    {
      apbuart_status_register &= (uint32_t) ~(APBUART_FIFO_STATUS_MASK << APBUART_TCNT);
      apbuart_status_register |= ((value & APBUART_FIFO_STATUS_MASK) << APBUART_TCNT);
      break;
    }
    case APBUART_FIFO_RECEIVER:
    {
      apbuart_status_register &= (uint32_t) ~(APBUART_FIFO_STATUS_MASK << APBUART_RCNT);
      apbuart_status_register |= ((value & APBUART_FIFO_STATUS_MASK) << APBUART_RCNT);
      break;
    }
    default:
    {}
  }
}
