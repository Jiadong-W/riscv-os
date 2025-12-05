#include "types.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "printf.h"

// 初始化锁
void initlock(struct spinlock *lk, char *name)
{
  lk->name = name;    // 锁的名称（调试用）
  lk->locked = 0;     // 锁状态：0-未锁定，1-已锁定
}

// 循环自旋直到获得锁
void acquire(struct spinlock *lk)
{
  push_off(); // 禁用中断，避免死锁
  if(holding(lk)){
    printf("panic: acquire, lock name=%s\n", lk->name);
    panic("acquire");
  }

  // RISC-V原子交换操作：将lk->locked设为1，返回原值
  // 如果原值为1（已锁定），则循环等待
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 内存屏障：确保临界区内存访问在获得锁之后执行
  __sync_synchronize();
}

// 释放锁
void release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  // 内存屏障：确保临界区所有存储操作在释放锁前对其他CPU可见
  __sync_synchronize();

  // 原子释放锁，相当于lk->locked = 0
  __sync_lock_release(&lk->locked);

  pop_off();
}

// 检查当前是否持有该锁
// 必须在中断禁用状态下调用
int holding(struct spinlock *lk)
{
  return lk->locked;
}

// push_off/pop_off用于管理中断禁用状态的嵌套
// 多个push_off需要相同数量的pop_off来恢复
// 如果初始时中断已禁用，push_off/pop_off后仍保持禁用

void push_off(void)
{
  int old = intr_get();  // 保存当前中断状态

  intr_off();  // 禁用中断

  if(mycpu()->noff == 0)
    mycpu()->intena = old;  // 记录初始中断使能状态
  mycpu()->noff += 1;       // 增加嵌套深度
}

void pop_off(void)
{
  if(intr_get())
    panic("pop_off - interruptible");
  if(mycpu()->noff < 1)
    panic("pop_off");
  mycpu()->noff -= 1;  // 减少嵌套深度
  if(mycpu()->noff == 0 && mycpu()->intena)
    intr_on();   // 如果回到最外层且原本中断使能，则重新启用中断
}