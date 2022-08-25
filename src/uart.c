#include "uart.h"

int
uart_init(apbuart_type *uart)
{
  int result = 1;

  uart->uart_io.in.descriptor = -1;
  uart->uart_io.out.descriptor = -1;

  if (strcmp (uart->uart_io.device.device_path, "stdio") == 0)
  {
    uart->uart_io.in.file = stdin;
    uart->uart_io.out.file = stdout;
  }
  else
  {
    if (strcmp (uart->uart_io.device.device_path, "") != 0)
    {
      if ((uart->uart_io.device.device_descriptor = open (uart->uart_io.device.device_path, O_RDWR | O_NONBLOCK | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO)) < 0)
      {
        printf ("Warning, couldn't open output device %s\n", uart->uart_io.device.device_path);
      }
      else
      {
        if (uart_sis_verbose)
        {
          printf ("serial port on %s\n", uart->uart_io.device.device_path);
        }
        uart->uart_io.in.file = fdopen (uart->uart_io.device.device_descriptor, "r+");
        uart->uart_io.out.file = uart->uart_io.in.file;
        setbuf (uart->uart_io.out.file, NULL);
        uart->uart_io.device.device_open = 1;
        result = 0;
      }
    }
  }

  if (uart->uart_io.in.file)
  {
    uart->uart_io.in.descriptor = fileno ( uart->uart_io.in.file);
  }
    
  if (uart->uart_io.in.descriptor == 0)
  {
    if (uart_sis_verbose)
    {
      printf ("serial port %x on stdin/stdout\n", uart->address);
    }

    if (!uart_dumbio)
      {
#ifdef HAVE_TERMIOS_H
        tcgetattr (uart->uart_io.in.descriptor, &uart->uart_io.termios.io_ctrl);
        if (uart_tty_setup)
        {
          uart->uart_io.termios.io_ctrl_old = uart->uart_io.termios.io_ctrl;
          uart->uart_io.termios.io_ctrl.c_lflag &= ~(ICANON | ECHO);
          uart->uart_io.termios.io_ctrl.c_cc[VMIN] = 0;
          uart->uart_io.termios.io_ctrl.c_cc[VTIME] = 0;
        }
#endif
      }
    uart->uart_io.device.device_open = 1;
    result = 0;
  }

  if (uart->uart_io.out.file)
    {
      uart->uart_io.out.descriptor = fileno (uart->uart_io.out.file);
      if (!uart_dumbio && uart_tty_setup && uart->uart_io.out.descriptor == 1)
      {
        setbuf (uart->uart_io.out.file, NULL);
      }
    }

  uart->uart_io.out.buffer_size = 0;

  return result;
}

int
uart_reset(apbuart_type *uart)
{
  uart->uart_io.out.buffer_size = 0;
  uart->uart_io.in.buffer_size = 0;
  uart->uart_io.in.buffer_index = 0;
  apbuart_set_flag(&uart->status_register, APBUART_TS);
  apbuart_set_flag(&uart->status_register, APBUART_TE);
}

int
uart_init_stdio(apbuart_type *uart)
{
  int result = 1;

  if (uart != NULL) {
    if (uart_dumbio)
    {
      result = 0;
    }
    else
    {
#ifdef HAVE_TERMIOS_H
      if (uart->uart_io.in.descriptor == 0 && uart->uart_io.device.device_open)
      {
        tcsetattr (0, TCSANOW, &uart->uart_io.termios.io_ctrl);
        tcflush (uart->uart_io.in.descriptor, TCIFLUSH);
        result = 0;
      }
#endif
    }
  }

  return result;
}

int
uart_restore_stdio(apbuart_type *uart)
{
  int result = 1;

  if (uart != NULL)
  {
    if (uart_dumbio)
    {
      result = 0;
    }
    else
    {
#ifdef HAVE_TERMIOS_H
      if (uart->uart_io.in.descriptor == 0 && uart->uart_io.device.device_open && tty_setup)
      {
        tcsetattr (0, TCSANOW, &uart->uart_io.termios.io_ctrl_old);
        result = 0;
      }
#endif
    }
  }

  return result;
}

void
apbuart_close_port (apbuart_type *uart)
{
  if (uart != NULL)
  {
    if (uart->uart_io.device.device_open && uart->uart_io.out.file != stdin)
    {
      fclose (uarts->uart_io.out.file); 
    }
  }
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
    if (uart->uart_io.device.device_open && apbuart_get_flag(uart->control_register, APBUART_RE))
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

  if (uart->uart_io.device.device_open)
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
        uart->uart_io.in.buffer_size = uart->uart_io.device.device_open
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
    if (uart->uart_io.device.device_open && apbuart_get_flag(uart->control_register, APBUART_TE))
    {
      if (uart->uart_io.out.buffer_size > 0)
      {
        off_t descriptor_offset;
        if (apbuart_get_flag (uart->control_register, APBUART_LB))
        {
          descriptor_offset = lseek (uart->uart_io.out.descriptor, 0, SEEK_CUR);
        }

        result = apbuart_write_data (uart->uart_io.out.descriptor, uart->uart_io.out.buffer, 1);
        uart->uart_io.out.buffer_size = 0;

        if (result != 0 && apbuart_get_flag (uart->control_register, APBUART_LB))
        {
          lseek (uart->uart_io.in.descriptor, descriptor_offset, SEEK_SET);
        }

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
    if (uart->uart_io.device.device_open)
    {
      off_t descriptor_offset;
      if (apbuart_get_flag (uart->control_register, APBUART_LB))
      {
        descriptor_offset = lseek (uart->uart_io.out.descriptor, 0, SEEK_CUR);
      }

      while (uart->uart_io.out.buffer_index < uart->uart_io.out.buffer_size)
      {
        result += apbuart_write_data (uart->uart_io.out.descriptor, &uart->uart_io.out.buffer[uart->uart_io.out.buffer_index++], 1);
      }

      if (result != 0 && apbuart_get_flag (uart->control_register, APBUART_LB))
      {
        lseek (uart->uart_io.in.descriptor, descriptor_offset, SEEK_SET);
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
