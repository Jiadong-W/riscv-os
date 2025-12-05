#pragma once

#include "types.h"
#include "fs.h"

#define NOFILE 16
#define NFILE  100
#define NDEV   10
#define CONSOLE 1

struct pipe;

// 文件引用类型枚举，用于区分管道、普通文件与设备
#define FD_NONE   0
#define FD_PIPE   1
#define FD_INODE  2
#define FD_DEVICE 3

// 全局文件表项，使用引用计数在进程之间共享
struct file {
    int type;            // FD_* 常量之一，表明当前引用的资源类型
    int ref;             // 引用计数，0 表示条目空闲
    char readable;       // 是否允许读
    char writable;       // 是否允许写
    struct pipe *pipe;  // FD_PIPE: 指向管道对象
    struct inode *ip;   // FD_INODE/FD_DEVICE: 指向底层 inode
    uint32 off;         // 当前读写偏移（仅对 inode 生效）
    short major;        // 设备主编号，FD_DEVICE 时用于索引 devsw
};

struct devsw {
    int (*read)(int, uint64, int);   // 设备读回调
    int (*write)(int, uint64, int);  // 设备写回调
};

// 主设备号到读写函数的映射表，由各设备驱动在启动阶段注册
extern struct devsw devsw[];

// 文件表生命周期管理接口
void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int filestat(struct file *f, uint64 addr);
int fileread(struct file *f, uint64 addr, int n);
int filewrite(struct file *f, uint64 addr, int n);
