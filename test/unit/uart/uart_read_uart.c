#include "uart_read_uart.h"

void test() {
    int test = exec_cmd("test");
    printf("test: %i\n", test);
}