#include "uart.h"   // 声明 uart_puts

void start(void)
{
    uart_puts("Hello OS\n");
    while (1) {
        asm volatile("wfi");  // 低功耗死循环
    }
}