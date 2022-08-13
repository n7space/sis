#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

#pragma once

#define DEVICE_PATH_SIZE 128

#define APBUART_NUM 6
#define APBUART_BUFFER_SIZE 1024
#define APB_START 0x80000000

#define APBUART_ADDR_MASK 0xFFFF
#define APBUART_REGISTER_TYPE_MASK 0xFF

#define APBUART_DATA_REGISTER_ADDRESS 0x00
#define APBUART_STATUS_REGISTER_ADDRESS 0x04
#define APBUART_CONTROL_REGISTER_ADDRESS 0x08

#define APBUART0_START_ADDRESS 0x80000100
#define APBUART1_START_ADDRESS 0x80100100
#define APBUART2_START_ADDRESS 0x80100200
#define APBUART3_START_ADDRESS 0x80100300
#define APBUART4_START_ADDRESS 0x80100400
#define APBUART5_START_ADDRESS 0x80100500

#define APBUART_STATUS_REG_DATA_READY 0x01
#define APBUART_STATUS_REG_TRANSMITTER_SHIFT_REG_EMPTY 0x02
#define APBUART_STATUS_REG_TRANSMITTER_FIFO_EMPTY 0x04
#define APBUART_STATUS_REG_OVERRUN 0x10

#define APBUART_CONTROL_REG_RECEIVER_ENABLE 0x01
#define APBUART_CONTROL_REG_TRANSMITTER_ENABLE 0x02
#define APBUART_CONTROL_REG_RECEIVER_IRQ_ENABLE 0x04

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
    int buffer_size;
    int buffer_index;
    char holding_register;
    char data;
} io_stream;

typedef struct
{
    int irq;
    uint32_t address;
    uint32_t mask;
    struct termios io_ctrl;
    struct termios io_ctrl_old;
    io_stream in_stream;
    io_stream out_stream;
    int32_t device_descriptor;
    int device_open;
    char device_path[DEVICE_PATH_SIZE];
    uint32_t status_register;
    uint32_t control_register;
} apbuart_type;

int uart_init (apbuart_type *uart);
int uart_reset (apbuart_type *uart);
int uart_read (apbuart_type *uart, uint32_t addr, uint32_t * data);
int uart_write (apbuart_type *uart, uint32_t addr, uint32_t * data, uint32_t sz);
int uart_add (apbuart_type *uart);

int uart_init_stdio(apbuart_type *uart);
int uart_restore_stdio(apbuart_type *uart);

apbuart_type *get_uart_by_address (uint32_t address);
apbuart_type *get_uart_by_irq (int irq);

extern apbuart_type uarts[APBUART_NUM];
