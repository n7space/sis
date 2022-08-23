#include "uartdef.h"

int
uart_reset(apbuart_type *uart)
{
  uart->uart_io.out.buffer_size = 0;
  uart->uart_io.in.buffer_size = 0;
  uart->uart_io.in.buffer_index = 0;
  apbuart_set_flag(&uart->status_register, APBUART_TS);
  apbuart_set_flag(&uart->status_register, APBUART_TE);
}

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

size_t
apbuart_read_byte(int read_descriptor, void *data_buffer, size_t data_size)
{
  size_t result = 0;

  if (!(uart_dumbio || uart_nouartrx))
  {
    result = read (read_descriptor, data_buffer, data_size);
  }

  return result;
}

size_t
apbuart_read_event(apbuart_type *uart)
{
  size_t result = 0;

  if (uart != NULL)
  {
    if (uart->device.device_open)
    {
      result = apbuart_read_byte (uart->uart_io.in.descriptor, uart->uart_io.in.buffer, 1);
    }

    if (result > 0)
    {
      if (apbuart_get_flag(uart->status_register, APBUART_DR))
      {
        apbuart_set_flag(&uart->status_register, APBUART_OV);
      }
      apbuart_set_flag(&uart->status_register, APBUART_DR);
    }
    else
    {
      apbuart_set_flag(&uart->status_register, APBUART_TS);
    }
  }

  return result;
}

size_t
apbuart_fast_read_event(apbuart_type *uart)
{
  size_t result = 0;

  if (uart != NULL && !apbuart_get_flag(uart->status_register, APBUART_DR))
  {
    if (uart->uart_io.in.buffer_index < uart->uart_io.in.buffer_size - 1)
    {
      uart->uart_io.in.buffer_index++;
      result = sizeof (uart->uart_io.in.buffer[uart->uart_io.in.buffer_index]);
      apbuart_set_flag(&uart->status_register, APBUART_DR);
    }
    else
    {
      uart->uart_io.in.buffer_index = 0;
      uart->uart_io.in.buffer_size = uart->device.device_open
        ? apbuart_read_byte (uart->uart_io.in.descriptor, uart->uart_io.in.buffer, APBUART_BUFFER_SIZE)
        : 0;

      if (uart->uart_io.in.buffer_index < uart->uart_io.in.buffer_size - 1)
      {
        result = sizeof (uart->uart_io.in.buffer[uart->uart_io.in.buffer_index]);
        apbuart_set_flag(&uart->status_register, APBUART_DR);
      }
      else {
        apbuart_set_flag(&uart->status_register, APBUART_TS);
      }
    }
  }

  return result;
}
