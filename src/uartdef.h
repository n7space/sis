#include <sys/file.h>
#include <termios.h>

#define UART_NUM 6
#define UART_BUFFER_SIZE 1024

typedef struct
{
    FILE *file;
    int descriptor;
    char buffer[UART_BUFFER_SIZE];
    int buffer_size_cnt;
    int buffer_size_index;
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

UART_DEF *get_uart_by_irq (int irq);
