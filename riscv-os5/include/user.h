#ifndef USER_H
#define USER_H

#include <stdarg.h>
#include "fcntl.h"

#define SBRK_ERROR ((char *)-1)

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

enum {
    KLOG_LEVEL_ERROR = 0,
    KLOG_LEVEL_WARN  = 1,
    KLOG_LEVEL_INFO  = 2,
    KLOG_LEVEL_DEBUG = 3,
};

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
int symlink(const char *target, const char *linkpath);
int dup(int fd);
int mknod(const char *path, int major, int minor, int type);
int chdir(const char *path);

int exec(const char *path, char *const argv[]);

void *sbrk(int increment);
uint64_t get_time(void);
uint64_t get_ticks(void);
int sleep(int ticks);
int get_priority_level(void);
int set_crash_stage(int stage);
int recover_log(void);
int clear_cache(void);
int klog_dump(void);
int klog_set_threshold(int record_level, int console_level);

// 基础格式化输出接口
int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
int vprintf(int fd, const char *fmt, va_list ap);

char* strchr(const char*, char c);
char* gets(char*, int max);
void* memset(void*, int, unsigned int);
int strlen(const char *s);


// 运行一组基础系统调用测试，验证参数传递与安全处理
void test_basic_syscalls(void);
void test_parameter_passing(void);
void test_security(void);
static int test_filesystem_integrity(void);
// 系统调用性能简单基准
void test_syscall_performance(void);

// 最近一次系统调用的错误码，若接口返回 -1 可进一步读取
extern int errno;

void* malloc(unsigned int);
void free(void*);

#endif
