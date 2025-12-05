#pragma once

#include "spinlock.h"

struct proc;

// 睡眠锁：结合自旋锁与进程睡眠的高层锁机制
// 适用于需要在持锁期间执行较长耗时操作的场景，避免忙等浪费 CPU。
struct sleeplock {
    char *name;              // 锁名称，便于调试
    struct spinlock lk;      // 自旋锁，用于保护 sleeplock 自身状态
    int locked;              // 锁状态：1 表示已上锁，0 表示未上锁
    struct proc *owner;      // 当前持有锁的进程（仅用于调试）
};

void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int holdingsleep(struct sleeplock *lk);
