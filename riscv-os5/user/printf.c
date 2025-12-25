#include "user.h"
#include <stdarg.h>

// 基础格式化输出支持，拆分自 ulib.c 便于维护

static const char digits[] = "0123456789abcdef";

#define PUTC_BUFFER_SIZE 128
static char putc_buffer[PUTC_BUFFER_SIZE];
static int putc_buffer_len = 0;
static int putc_buffer_fd = -1;

static int putc_flush(void)
{
    if(putc_buffer_len == 0)
        return 0;
    int fd = putc_buffer_fd;
    int len = putc_buffer_len;
    putc_buffer_len = 0;
    putc_buffer_fd = -1;
    int r = write(fd, putc_buffer, len);
    if(r != len)
        return -1;
    return 0;
}

// 基础输出：写入单个字符
static int putc(int fd, char c)
{
    if(fd < 0)
        return -1;

    if(putc_buffer_fd != -1 && putc_buffer_fd != fd) {
        if(putc_flush() < 0)
            return -1;
    }

    if(putc_buffer_fd == -1)
        putc_buffer_fd = fd;

    putc_buffer[putc_buffer_len++] = c;
    if(putc_buffer_len == PUTC_BUFFER_SIZE || c == '\n') {
        if(putc_flush() < 0)
            return -1;
    }
    return 1;
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

        char c0 = fmt[++i] & 0xff;
        if(c0 == 0) break;
        
        char c1 = 0, c2 = 0;
        if(fmt[i+1]) c1 = fmt[i+1] & 0xff;
        if(fmt[i+2]) c2 = fmt[i+2] & 0xff;

        int w = 0;
        
        if(c0 == 'd'){
            w = printint(fd, va_arg(ap, int), 10, 1);
        } else if(c0 == 'u'){
            w = printint(fd, va_arg(ap, unsigned int), 10, 0);
        } else if(c0 == 'x'){
            w = printint(fd, va_arg(ap, unsigned int), 16, 0);
        } else if(c0 == 'p'){
            w = printptr(fd, va_arg(ap, uint64_t));
        } else if(c0 == 'c'){
            if(putc(fd, (char)va_arg(ap, int)) < 0)
                return -1;
            w = 1;
        } else if(c0 == 's'){
            w = prints(fd, va_arg(ap, const char *));
        } else if(c0 == '%'){
            if(putc(fd, '%') < 0)
                return -1;
            w = 1;
        } 
        // 参考内核版本的 %lu 处理
        else if(c0 == 'l' && c1 == 'u'){
            // 关键修改：使用 uint64 而不是 unsigned long
            w = printint(fd, va_arg(ap, uint64_t), 10, 0);
            i += 1;
        } else if(c0 == 'l' && c1 == 'd'){
            w = printint(fd, va_arg(ap, uint64_t), 10, 1);
            i += 1;
        } else if(c0 == 'l' && c1 == 'x'){
            w = printint(fd, va_arg(ap, uint64_t), 16, 0);
            i += 1;
        } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
            w = printint(fd, va_arg(ap, uint64_t), 10, 0);
            i += 2;
        } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
            w = printint(fd, va_arg(ap, uint64_t), 10, 1);
            i += 2;
        } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
            w = printint(fd, va_arg(ap, uint64_t), 16, 0);
            i += 2;
        } else {
            if(putc(fd, '%') < 0 || putc(fd, c0) < 0)
                return -1;
            w = 2;
        }

        if(w < 0)
            return -1;
        written += w;
    }

    if(putc_flush() < 0)
        return -1;

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