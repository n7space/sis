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

size_t
apbuart_read_data(int read_descriptor, void *data_buffer, size_t data_size)
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
    if (uart->device.device_open && apbuart_get_flag(uart->control_register, APBUART_RE))
    {
      result = apbuart_read_data (uart->uart_io.in.descriptor, uart->uart_io.in.buffer, 1);
    }

    if (result > 0)
    {
      if (apbuart_get_flag(uart->status_register, APBUART_DR))
      {
        apbuart_set_flag(&uart->status_register, APBUART_OV);
      }
      apbuart_set_flag(&uart->status_register, APBUART_DR);
    }
  }

  return result;
}

size_t
apbuart_fast_read_event(apbuart_type *uart)
{
  size_t result = 0;

  if (uart->device.device_open)
  {
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
          ? apbuart_read_data (uart->uart_io.in.descriptor, uart->uart_io.in.buffer, APBUART_BUFFER_SIZE)
          : 0;

        if (uart->uart_io.in.buffer_index < uart->uart_io.in.buffer_size - 1)
        {
          result = sizeof (uart->uart_io.in.buffer[uart->uart_io.in.buffer_index]);
          apbuart_set_flag(&uart->status_register, APBUART_DR);
        }
      }
    }
  }

  return result;
}

size_t apbuart_write_data(int write_descriptor, void *data_buffer, size_t data_size)
{
  size_t result = write (write_descriptor, data_buffer, data_size);

  return result;
}

size_t apbuart_write_event(apbuart_type *uart)
{
  size_t result = 0;

  if (uart != NULL)
  {
    if (uart->device.device_open && apbuart_get_flag(uart->control_register, APBUART_TE))
    {
      if (uart->uart_io.out.buffer_size > 0)
      {
        result = apbuart_write_data (uart->uart_io.out.descriptor, uart->uart_io.out.buffer, 1);
        uart->uart_io.out.buffer_size = 0;
        apbuart_set_flag(&uart->status_register, APBUART_TS);
      }
    }
  }

  return result;
}

size_t apbuart_fast_write_event(apbuart_type *uart)
{
  size_t result = 0;

  if (uart != NULL)
  {
    if (uart->device.device_open)
    {
      while (uart->uart_io.out.buffer_index < uart->uart_io.out.buffer_size)
      {
        result += apbuart_write_data (uart->uart_io.out.descriptor, &uart->uart_io.out.buffer[uart->uart_io.out.buffer_index++], 1);
      }
      
      if (result == 0)
      {
        apbuart_set_flag(&uart->status_register, APBUART_TS);
      }

      uart->uart_io.out.buffer_size = 0;
      uart->uart_io.out.buffer_index = 0;
    }
  }

  return result;
}

size_t apbuart_fast_write_to_uart_buffer(apbuart_type *uart, uint32_t *data)
{
  size_t result = 0;

  unsigned char c = (unsigned char) *data;

  if (uart->uart_io.out.buffer_size < APBUART_BUFFER_SIZE)
  {
    uart->uart_io.out.buffer[uart->uart_io.out.buffer_size++] = c;
  }
  else
  {
    result = apbuart_fast_write_event (uart);
    uart->uart_io.out.buffer[uart->uart_io.out.buffer_size++] = c;
  }
  
  return result;
}
