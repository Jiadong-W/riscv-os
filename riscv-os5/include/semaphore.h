#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "spinlock.h"

struct semaphore {
  struct spinlock lock;
  int value;
};

void sem_init(struct semaphore *sem, int value, char *name);
void sem_wait(struct semaphore *sem);
void sem_signal(struct semaphore *sem);

#endif
