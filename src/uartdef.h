#include <sys/file.h>
#include <termios.h>

#pragma once

#define APBUART_NUM 6
#define APBUART_BUFFER_SIZE 1024
#define APB_START 0x80000000
#define APBUART_ADDR_MASK 0xFFFF
#define APBUART_REGISTER_TYPE_MASK 0xFF

#define APBUART0_START_ADDRESS 0x80000100
#define APBUART1_START_ADDRESS 0x80100100
#define APBUART2_START_ADDRESS 0x80100200
#define APBUART3_START_ADDRESS 0x80100300
#define APBUART4_START_ADDRESS 0x80100400
#define APBUART5_START_ADDRESS 0x80100500

#define APBUART0_IRQ 2
#define APBUART1_IRQ 17
#define APBUART2_IRQ 18
#define APBUART3_IRQ 19
#define APBUART4_IRQ 20
#define APBUART5_IRQ 21

typedef struct
{
    FILE *file;
    int descriptor;
    char buffer[APBUART_BUFFER_SIZE];
    int buffer_size_cnt;
    int buffer_size_index;
    char holding_register;
    char data;
} io_stream;

typedef struct
{
    int irq;
    uint32 address;
    uint32 mask;
    struct termios io_ctrl;
    struct termios io_ctrl_old;
    io_stream in_stream;
    io_stream out_stream;
    int32 device_descriptor;
    int device_open;
    char device_path[128];
    uint32 status_register;
    uint32 control_register;
} apbuart_type;

static apbuart_type uarts[APBUART_NUM];

int uart_init (apbuart_type *uart);
int uart_reset (apbuart_type *uart);
int uart_read (apbuart_type *uart, uint32 addr, uint32 * data);
int uart_write (apbuart_type *uart, uint32 addr, uint32 * data, uint32 sz);
int uart_add (apbuart_type *uart);

int uart_init_stdio(apbuart_type *uart);
int uart_restore_stdio(apbuart_type *uart);

apbuart_type *get_uart_by_address (uint32 address);
apbuart_type *get_uart_by_irq (int irq);
