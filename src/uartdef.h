#include <sys/file.h>
#include <termios.h>

#define UART_NUM 6
#define UART_BUFFER_SIZE 1024
#define UART_START_ADDRESS_MASK 0x80F00F00

#define UART0_START_ADDRESS 0x80000100
#define UART1_START_ADDRESS 0x80100100
#define UART2_START_ADDRESS 0x80100200
#define UART3_START_ADDRESS 0x80100300
#define UART4_START_ADDRESS 0x80100400
#define UART5_START_ADDRESS 0x80100500

#define UART0_IRQ 2
#define UART1_IRQ 17
#define UART2_IRQ 18
#define UART3_IRQ 19
#define UART4_IRQ 20
#define UART5_IRQ 21

typedef struct
{
    FILE *file;
    int descriptor;
    char buffer[UART_BUFFER_SIZE];
    int buffer_size_cnt;
    int buffer_size_index;
    char holding_register;
    char data;
} IO_STREAM;

typedef struct
{
    int irq;
    uint32 address;
    uint32 mask;
    struct termios io_ctrl;
    struct termios io_ctrl_old;
    IO_STREAM in_stream;
    IO_STREAM out_stream;
    int32 device_descriptor;
    int device_open;
    char device[128];
    uint32 status_register;
} UART_DEF;

static UART_DEF uarts[UART_NUM];

int uart_init (UART_DEF *uart);
int uart_reset (UART_DEF *uart);
int uart_read (UART_DEF *uart, uint32 * data);
int uart_write (UART_DEF *uart, uint32 * data, uint32 sz);
int uart_add (UART_DEF *uart);

UART_DEF *get_uart_by_address (uint32 address);
UART_DEF *get_uart_by_irq (int irq);
