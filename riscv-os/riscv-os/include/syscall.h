#pragma once

// 系统调用编号定义，需与用户态约定保持一致
#define SYS_exit    0
#define SYS_getpid  1
#define SYS_fork    2
#define SYS_wait    3
#define SYS_kill    4
#define SYS_write   5
#define SYS_read    6
#define SYS_open    7
#define SYS_close   8
#define SYS_unlink  9
#define SYS_sbrk    10

#ifndef __ASSEMBLER__
// 系统调用总入口，由内核陷入处理流程调用
void syscall(void);
#include "types.h"
int get_syscall_arg(int n, long *arg);
int argint(int n, int *ip);
int argaddr(int n, uint64 *ip);
int argstr(int n, char *buf, int max);
int check_user_ptr(const void *ptr, int size);
int check_user_ptr_rw(const void *ptr, int size, int write);
#endif
