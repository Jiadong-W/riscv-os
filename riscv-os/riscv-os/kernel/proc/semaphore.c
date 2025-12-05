#include "types.h"
#include "semaphore.h"
#include "proc.h"

void sem_init(struct semaphore *sem, int value, char *name)
{
  initlock(&sem->lock, name);
  sem->value = value;
}

void sem_wait(struct semaphore *sem)
{
  acquire(&sem->lock);
  while (sem->value == 0) {
    sleep(sem, &sem->lock);
  }
  sem->value--;
  release(&sem->lock);
}

void sem_signal(struct semaphore *sem)
{
  acquire(&sem->lock);
  sem->value++;
  wakeup(sem);
  release(&sem->lock);
}
