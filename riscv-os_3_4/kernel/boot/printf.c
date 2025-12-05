#include <stdarg.h>
#include "types.h"
#include "console.h"

static char digits[] = "0123456789abcdef";

static void print_number(int num, int base, int sign) {
    char buf[32];
    int i;
    unsigned int x;

    // 处理 INT_MIN
    if(sign && (sign=(num < 0))) {
        x = (unsigned int)(-(long long)num); // 防止溢出
    } else {
        x = (unsigned int)num;
    }

    i=0;
    // 取余并存入buf
    do {
        buf[i++] = digits[x % base];
    } while((x /= base) != 0);

    // 负数补负号
    if(sign)
        buf[i++] = '-';

    // 逆序输出
    while(--i >= 0)
        console_putc(buf[i]);
}

static void printptr(uint64 x)
{
  int i;
  console_putc('0');
  console_putc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    console_putc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

int printf(const char *fmt, ...)
{
  va_list ap; //可变参数列表指针
  int i, cx, c0, c1, c2;
  char *s;

  va_start(ap, fmt); //初始化可变参数列表
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){
      console_putc(cx);
      continue;
    }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;
    if(c0 == 'd'){
      print_number(va_arg(ap, int), 10, 1); //有符号十进制
    } else if(c0 == 'l' && c1 == 'd'){
      print_number(va_arg(ap, uint64), 10, 1); //长整型
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      print_number(va_arg(ap, uint64), 10, 1); //长长整型
      i += 2;
    } else if(c0 == 'u'){
      print_number(va_arg(ap, uint32), 10, 0); //无符号
    } else if(c0 == 'l' && c1 == 'u'){
      print_number(va_arg(ap, uint64), 10, 0); 
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      print_number(va_arg(ap, uint64), 10, 0);
      i += 2;
    } else if(c0 == 'x'){
      print_number(va_arg(ap, uint32), 16, 0); //十六进制
    } else if(c0 == 'l' && c1 == 'x'){
      print_number(va_arg(ap, uint64), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      print_number(va_arg(ap, uint64), 16, 0);
      i += 2;
    } else if(c0 == 'p'){
      printptr(va_arg(ap, uint64)); //指针地址
    } else if(c0 == 'c'){
      console_putc(va_arg(ap, uint)); //直接输出字符
    } else if(c0 == 's'){ //字符串
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        console_putc(*s);
    } else if(c0 == '%'){ //特别的，输出%
      console_putc('%');
    } else if(c0 == 0){ //结束
      break;
    } else {
      console_putc('%');
      console_putc(c0);
    }

  }
  va_end(ap);
  return 0;
}

// 彩色输出
void printf_color(int color, const char *fmt, ...) {
    va_list ap;
    printf("\033[%dm", color); // 设置颜色
    va_start(ap, fmt);
    printf(fmt, ap);
    va_end(ap);
    printf("\033[0m");        // 恢复默认颜色
}

void panic(char *s)
{
  printf("panic: ");
  printf("%s\n", s);
  for(;;)
    ;
}