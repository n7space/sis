#include "uart/uart_read_uart.h"

int
main (void)
{
    if(!readUart()) {
        return 1;
    }
    return 0;
}