#include "uart.h"   // 声明 uart_puts
#include "printf.h"  // 新增

void start(void)
{
    uart_puts("Hello OS\n");

    clear_screen();  // 先清屏
    printf("Hello OS! Number: %d, Hex: 0x%x, String: %s, Char: %c, Pointer: %p, Percent: %%\n",
           42, 0xdeadbeef, "World", 'X', (void *)0x80000000);
    printf("Negative: %d, Zero: %d, INT_MIN: %d, NULL str: %s\n",
           -123, 0, -2147483648LL, (char *)0);

    while (1) {
        asm volatile("wfi");  // 低功耗死循环
    }
}