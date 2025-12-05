#include "user.h"
#include <stdarg.h>

// 基础格式化输出支持，拆分自 ulib.c 便于维护

static const char digits[] = "0123456789ABCDEF";

// 基础输出：写入单个字符
static int putc(int fd, char c)
{
    return write(fd, &c, 1);
}

// 输出有符号/无符号整数，返回写入的字节数
static int printint(int fd, long long xx, int base, int sign)
{
    char buf[32];
    int i = 0;
    int neg = 0;
    unsigned long long x;

    if(sign && xx < 0) {
        neg = 1;
        x = (unsigned long long)(-xx);
    } else {
        x = (unsigned long long)xx;
    }

    do {
        buf[i++] = digits[x % (unsigned long long)base];
        x /= (unsigned long long)base;
    } while(x != 0);

    if(neg) {
        buf[i++] = '-';
    }

    int written = 0;
    while(i > 0) {
        if(putc(fd, buf[--i]) < 0)
            return -1;
        written++;
    }
    return written;
}

// 输出指针（0x 前缀 + 固定宽度十六进制）
static int printptr(int fd, uint64_t x)
{
    int written = 0;
    if(putc(fd, '0') < 0 || putc(fd, 'x') < 0)
        return -1;
    written += 2;

    for(int i = 0; i < (int)(sizeof(uint64_t) * 2); i++, x <<= 4) {
        char c = digits[x >> (sizeof(uint64_t) * 8 - 4)];
        if(putc(fd, c) < 0)
            return -1;
        written++;
    }
    return written;
}

// 输出字符串，忽略 NULL 指针（替换为 "(null)"）
static int prints(int fd, const char *s)
{
    const char *str = s ? s : "(null)";
    int written = 0;
    while(*str) {
        if(putc(fd, *str++) < 0)
            return -1;
        written++;
    }
    return written;
}

// 迷你版 vprintf：支持 %d/%u/%x/%p/%c/%s/%%
int vprintf(int fd, const char *fmt, va_list ap)
{
    int written = 0;

    for(size_t i = 0; fmt[i]; i++) {
        if(fmt[i] != '%') {
            if(putc(fd, fmt[i]) < 0)
                return -1;
            written++;
            continue;
        }

        char c = fmt[++i];
        if(c == '\0')
            break;

        int w = 0;
        switch(c) {
        case 'd':
            w = printint(fd, va_arg(ap, int), 10, 1);
            break;
        case 'u':
            w = printint(fd, va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':
            w = printint(fd, va_arg(ap, unsigned int), 16, 0);
            break;
        case 'p':
            w = printptr(fd, va_arg(ap, uint64_t));
            break;
        case 'c':
            if(putc(fd, (char)va_arg(ap, int)) < 0)
                return -1;
            w = 1;
            break;
        case 's':
            w = prints(fd, va_arg(ap, const char *));
            break;
        case '%':
            if(putc(fd, '%') < 0)
                return -1;
            w = 1;
            break;
        default:
            if(putc(fd, '%') < 0 || putc(fd, c) < 0)
                return -1;
            w = 2;
            break;
        }

        if(w < 0)
            return -1;
        written += w;
    }

    return written;
}

int fprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fd, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(1, fmt, ap);
    va_end(ap);
    return ret;
}
