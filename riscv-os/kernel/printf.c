#include "printf.h"
#include "stdarg.h"  // 假设 gcc 支持
#include "uart.h"    // 用于 uart_putc


// 辅助函数：输出单个字符 (简化 consputc 为 uart_putc)
static void putc(char c) {
    uart_putc(c);
}

// 数字转换 (非递归，处理负数和进制)
static void printint(long long xx, int base, int sign) {
    static char digits[] = "0123456789abcdef";
    char buf[32];  // 足够 64位
    int i = 0;
    unsigned long long x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        putc(buf[i]);
}

// 指针输出 (0x + 十六进制)
static void printptr(unsigned long long x) {
    putc('0');
    putc('x');
    printint(x, 16, 0);
}

// 格式化输出
int printf(const char *fmt, ...) {
    va_list ap;
    int i, c;
    char *s;

    va_start(ap, fmt);
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            putc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        switch (c) {
        case 'd':
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            printint(va_arg(ap, int), 16, 0);
            break;
        case 'p':
            printptr(va_arg(ap, unsigned long long));
            break;
        case 's':
            if ((s = va_arg(ap, char *)) == 0)
                s = "(null)";
            for (; *s; s++)
                putc(*s);
            break;
        case 'c':
            putc(va_arg(ap, int));
            break;
        case '%':
            putc('%');
            break;
        default:
            putc('%');
            putc(c);
            break;
        }
    }
    va_end(ap);
    return 0;  // 可以返回输出长度，但简化
}

// 清屏：ANSI 序列
void clear_screen(void) {
    uart_puts("\033[2J\033[H");
}