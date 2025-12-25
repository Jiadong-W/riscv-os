#ifndef SPINLOCK_H
#define SPINLOCK_H

struct spinlock {
  uint locked;       // 锁是否被持有
  char *name;        //锁的名称（调试用）
};

void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int holding(struct spinlock *lk);
void push_off(void);
void pop_off(void);

#endif
