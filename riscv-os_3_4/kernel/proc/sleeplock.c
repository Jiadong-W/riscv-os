#include "types.h"
#include "proc.h"
#include "sleeplock.h"
#include "spinlock.h"

// 初始化睡眠锁：设置名称，并初始化内部自旋锁
void initsleeplock(struct sleeplock *lk, char *name)
{
    lk->name = name;
    lk->locked = 0;
    lk->owner = 0;
    initlock(&lk->lk, "sleeplock");
}

// 获取睡眠锁：若锁忙，则让当前进程睡眠，避免无谓自旋
void acquiresleep(struct sleeplock *lk)
{
    acquire(&lk->lk);
    while(lk->locked){
        sleep(lk, &lk->lk);   // 释放 lk->lk，并挂起当前进程
        acquire(&lk->lk);     // 被唤醒后重新获取自旋锁检查状态
    }
    lk->locked = 1;
    lk->owner = myproc();
    release(&lk->lk);
}

// 释放睡眠锁：唤醒等待队列中的其他进程
void releasesleep(struct sleeplock *lk)
{
    acquire(&lk->lk);
    lk->locked = 0;
    lk->owner = 0;
    wakeup(lk);
    release(&lk->lk);
}

// 判断当前进程是否持有睡眠锁，仅用于调试断言
int holdingsleep(struct sleeplock *lk)
{
    int r;

    acquire(&lk->lk);
    r = (lk->locked && lk->owner == myproc());
    release(&lk->lk);
    return r;
}
