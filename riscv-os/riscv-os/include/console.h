#pragma once

#include "types.h"

// 基础控制台输出接口，用于向串口终端逐字符或批量写入字符
void console_putc(char c);
void console_puts(const char *s);
void console_write(const char *s, int len);

// ANSI 控制序列封装，可用于清屏和光标定位等终端控制操作
void clear_screen(void);
void goto_xy(int x, int y);
void clear_line(void);
void printf_color(int color, const char *fmt, ...);

// 文件系统通过 devsw 表访问的控制台设备读写回调
int consolewrite(int user_src, uint64 src, int n);
int consoleread(int user_dst, uint64 dst, int n);
