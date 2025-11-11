#include "uart.h"
#define UART0 0x10000000UL

static inline void mmio_write(unsigned long addr, unsigned char val) {
    *(volatile unsigned char *)addr = val;
}

static inline unsigned char mmio_read(unsigned long addr) {
    return *(volatile unsigned char *)addr;
}

/* 等待 THRE 置位，然后发送 */
void uart_putc(char c) {
    while ((mmio_read(UART0 + 0x05) & (1 << 5)) == 0)
        ;                   /* 轮询等待 */
    mmio_write(UART0 + 0, c);  /* 偏移 0 = THR */
}

/* 字符串输出 */
void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}