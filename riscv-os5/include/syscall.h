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
#define SYS_time    11
#define SYS_symlink 12
#define SYS_set_crash_stage 13
#define SYS_recover_log 14
#define SYS_clear_cache 15
#define SYS_exec 16
#define SYS_dup 17
#define SYS_mknod 18
#define SYS_chdir 19
#define SYS_ticks   20
#define SYS_getpriority 21
#define SYS_klog_dump 22
#define SYS_klog_set_threshold 23
#define SYS_sleep 24

#ifndef __ASSEMBLER__
// 系统调用总入口，由内核陷入处理流程调用
void syscall(void);
#include "types.h"
int get_syscall_arg(int n, long *arg);
int argint(int n, int *ip);
int  fetchaddr(uint64, uint64*);
int  fetchstr(uint64, char*, int);
int argaddr(int n, uint64 *ip);
int argstr(int n, char *buf, int max);
int check_user_ptr(const void *ptr, int size);
int check_user_ptr_rw(const void *ptr, int size, int write);
#endif
