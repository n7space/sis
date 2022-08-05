#include "uartdef.h"

apbuart_type uarts[APBUART_NUM];

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
get_uart_by_irq (int irq)
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
