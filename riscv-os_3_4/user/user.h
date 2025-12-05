#ifndef USER_H
#define USER_H

#include <stdarg.h>
#include "fcntl.h"

//
// 用户态标准库最小化声明，提供：
//   - 常用基础类型别名，避免依赖宿主 libc。
//   - 面向应用的系统调用包装函数声明。
//   - errno 全局变量，用于报告最近一次系统调用错误。
//

typedef unsigned long size_t;    // 与内核保持一致的无符号长度类型
typedef unsigned long uint64_t;  // 64 位无符号整型

#ifndef NULL
#define NULL ((void*)0)
#endif

// 结束当前进程，status 为退出码；不返回
int exit(int status) __attribute__((noreturn));

// 获取当前进程的 PID
int getpid(void);

// 复制当前进程，父进程返回子 PID，子进程返回 0
int fork(void);

// 等待任意子进程结束，可选地写回其退出码
int wait(int *status);

// 结束指定 PID 的进程
int kill(int pid);

// 写入字节流（当前仅支持标准输出/错误）
int write(int fd, const void *buf, int len);
// 读取字节流
int read(int fd, void *buf, int len);

// 打开/关闭文件
int open(const char *path, int mode);
int close(int fd);
int unlink(const char *path);

void *sbrk(int increment);

// 基础格式化输出接口
int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
int vprintf(int fd, const char *fmt, va_list ap);

// 运行一组基础系统调用测试，验证参数传递与安全处理
void test_basic_syscalls(void);
void test_parameter_passing(void);
void test_security(void);
void test_filesystem_integrity(void);
// 系统调用性能简单基准
void test_syscall_performance(void);

// 最近一次系统调用的错误码，若接口返回 -1 可进一步读取
extern int errno;

#endif
