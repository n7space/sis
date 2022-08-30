#include <assert.h>
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

#define APBUART_FLAG_MASK 0x01
#define APBUART_REGISTER_TYPE_MASK 0xFF
#define APBUART_ADDR_MASK 0xFFFF
#define APBUART_CONTROL_REGISTER_WRITE_MASK 0xEBF

#define APBUART_DATA_REGISTER_ADDRESS 0x00
#define APBUART_STATUS_REGISTER_ADDRESS 0x04
#define APBUART_CONTROL_REGISTER_ADDRESS 0x08

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


#define UART_FLUSH_TIME	  5000
#define UART_TX_TIME	1000
#define UART_RX_TIME	1000

/* Status Register definition, taken from GR712RC documentation:
    (0)       DR - Data ready: Indicates that new data is available in the receiver holding register.
    (1)       TS - Transmitter shift register empty: Indicates that the transmitter shift register is empty.
    (2)       TE - Transmitter FIFO empty: Indicates that the transmitter FIFO is empty.
    (3)       BR - Break received: Indicates that a BREAK has been received.
    (4)       OV - Overrun: Indicates that one or more character have been lost due to overrun.
    (5)       PE - Parity error: Indicates that a parity error was detected.
    (6)       FE - Framing error: Indicates that a framing error was detected.
    (7)       TH - Transmitter FIFO half-full: Indicates that the FIFO is less than half-full.
    (8)       RH - Receiver FIFO half-full: Indicates that at least half of the FIFO is holding data.
    (9)       TF - Transmitter FIFO full: Indicates that the Transmitter FIFO is full.
    (10)      RF - Receiver FIFO full: Indicates that the Receiver FIFO is full.
    (20 - 25) TCNT - Transmitter FIFO count: Shows the number of data frames in the transmitter FIFO.
    (26 - 31) RCNT - Receiver FIFO count: Shows the number of data frames in the receiver FIFO.
*/
typedef enum {APBUART_DR = 0, APBUART_TS, APBUART_TE, APBUART_BR, APBUART_OV, APBUART_PE, APBUART_FE, APBUART_TH, 
              APBUART_RH, APBUART_TF, APBUART_RF, APBUART_TCNT, APBUART_RCNT} apbuart_status_register_flags;

/* Control Register definition, taken from GR712RC documentation:
    (0)     RE - Receiver enable: If set, enables the receiver.
    (1)     TE - Transmitter enable: If set, enables the transmitter.
    (2)     RI - Receiver interrupt enable: If set, interrupts are generated when a frame is received.
    (3)     TI - Transmitter interrupt enable: If set, interrupts are generated when a frame is transmitted.
    (4)     PS - Parity select: Selects parity polarity (0 = even parity, 1 = odd parity) (when implemented).
    (5)     PE - Parity enable: If set, enables parity generation and checking (when implemented).
    (7)     LB - Loop back: If set, loop back mode will be enabled.
    (9)     TF - Transmitter FIFO interrupt enable: When set, Transmitter FIFO level interrupts are enabled.
    (10)    RF - Receiver FIFO interrupt enable: When set, Receiver FIFO level interrupts are enabled.
    (11)    DB - FIFO debug mode enable: When set, it is possible to read and write the FIFO debug register.
    (31)    FA - FIFOs available: Set to 1, read-only. Receiver and transmitter FIFOs are available.
*/
typedef enum {APBUART_RE = 0, APBUART_CTRL_TE, APBUART_RI, APBUART_TI, APBUART_PS, APBUART_CTRL_PE, APBUART_LB = 7,
              APBUART_CRTL_TF = 9, APBUART_CTRL_RF, APBUART_DB, APBUART_FA = 31} apbuart_control_register_flags;

typedef struct
{
    int32_t device_descriptor;
    int device_open;
    char device_path[DEVICE_PATH_SIZE];
} uart_device;

typedef struct
{
    FILE *file;
    int descriptor;
    char buffer[APBUART_BUFFER_SIZE];
    int buffer_size;
    int buffer_index;
} io_stream;

typedef struct 
{
    struct termios io_ctrl;
    struct termios io_ctrl_old;
} termios_io;

typedef struct
{
    io_stream in;
    io_stream out;
    uart_device device;
    termios_io termios;
} uart_io;

typedef struct
{
    int irq;
    uint32_t address;
    uart_io uart_io;
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

void apbuart_close_port (apbuart_type *uart);

size_t apbuart_read_data(int read_descriptor, void *data_buffer, size_t data_size);
size_t apbuart_read_event(apbuart_type *uart);
size_t apbuart_fast_read_event(apbuart_type *uart);

size_t apbuart_write_data(int write_descriptor, void *data_buffer, size_t data_size);
size_t apbuart_write_event(apbuart_type *uart);
size_t apbuart_fast_write_event(apbuart_type *uart);
size_t apbuart_fast_write_to_uart_buffer(apbuart_type *uart, uint32_t *data);

apbuart_type *get_uart_by_address (uint32_t address);
apbuart_type *get_uart_by_irq (uint8_t irq);

uint32_t apbuart_get_flag(uint32_t apbuart_register, uint32_t flag);
void apbuart_set_flag(uint32_t *apbuart_register, uint32_t flag);
void apbuart_reset_flag(uint32_t *apbuart_register, uint32_t flag);

extern apbuart_type uarts[APBUART_NUM];
extern int uart_dumbio;
extern int uart_nouartrx;
extern int uart_sis_verbose;
extern int uart_tty_setup;
