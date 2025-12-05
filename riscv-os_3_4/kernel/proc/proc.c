#include "types.h"
#include "memlayout.h"
#include "fs.h"

#include "kalloc.h"
#include "vm.h"
#include "trap.h"
#include "proc.h"
#include "file.h"
#include "printf.h"
#include "assert.h"
#include "string.h"
#include "spinlock.h"
#include "semaphore.h"

#define NPROC 64

struct cpu cpu;                    // 单核CPU
struct proc proc[NPROC];           // 进程表（数组实现）
struct proc *initproc;             // 初始进程
int nextpid = 1;                   // 下一个可用的PID
static int next_sched_index = 0;   // 下一次从哪个进程槽开始调度

extern volatile uint64 ticks;      // 计时器滴答，由时钟中断更新
struct spinlock wait_lock;         // 等待子进程时的锁

// 蹦床页面引用
extern char trampoline[];
extern unsigned char initcode[];
extern uint64 initcode_size;

// 初始化进程管理系统
void procinit(void)
{
  // 初始化所有进程控制块
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++) {
      p->state = UNUSED;                           // 初始状态为未使用
      p->kstack = 0;
      p->pagetable = 0;
      p->trapframe = 0;
      p->sz = 0;
      p->cwd = 0;
      for(int i = 0; i < NOFILE; i++) {
          p->ofile[i] = 0;
      }
  }

  initlock(&wait_lock, "wait");
}

//获取cpu
struct cpu* mycpu(void)
{
  struct cpu *c = &cpu;
  return c;
}

// 获取当前进程
struct proc* myproc(void)
{
  struct proc *p = mycpu()->proc;
  return p;
}

// 分配新的PID
int allocpid()
{
  int pid;
  for (;;) {
    pid = nextpid;
    nextpid++;
    if (nextpid > NPROC) nextpid = 1;

    struct proc *p = &proc[pid - 1];
    // 如果此槽未被占用，则认为可用
    if (p->state == UNUSED ) {
      return pid;
    }
    // 否则继续尝试下一个 pid
  }
}

// 分配进程结构体
// 成功返回进程指针，失败返回0
struct proc* alloc_process(void)
{
  struct proc *p;

  // 遍历进程表寻找空闲槽位
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->state == UNUSED) {
      goto found;  // 找到空闲进程
    }
  }
  return 0;  // 没有空闲进程

found:
  // 初始化进程基本信息
  p->pid = allocpid();
  p->state = USED;
  p->cwd = 0;
  for(int i = 0; i < NOFILE; i++) {
      p->ofile[i] = 0;
  }
  
  // 分配一页内存用于陷阱帧
  if((p->trapframe = (struct trapframe *)alloc_page()) == 0){
    free_process(p);
    return 0;
  }
  
  // 为进程分配内核栈
  if((p->kstack = (uint64)alloc_page()) == 0) {
    free_page(p->trapframe);
    p->trapframe = 0;
    p->state = UNUSED;
    return 0;
  }

  // 设置执行上下文
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;      // 返回地址
  p->context.sp = p->kstack + PGSIZE;   // 栈指针（栈顶）
  
  return p;
}

// 释放进程资源
void free_process(struct proc *p)
{
  //释放用户页表和用户内存
  if(p->pagetable){
    uvmfree(p->pagetable, p->sz);
    proc_freepagetable(p->pagetable);
    p->pagetable = 0;
  }
  //释放陷阱帧
  if(p->trapframe){
    free_page((void*)p->trapframe);
    p->trapframe = 0;
  }
  //释放内核栈
  if(p->kstack){
    free_page((void*)p->kstack);
    p->kstack = 0;
  }
  
  // 重置进程状态
  for(int i = 0; i < NOFILE; i++) {
    if(p->ofile[i]) {
      fileclose(p->ofile[i]);
      p->ofile[i] = 0;
    }
  }
  if(p->cwd) {
    iput(p->cwd);
    p->cwd = 0;
  }

  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->sz = 0;
  p->state = UNUSED;
}

// 为指定进程创建一个用户页表，初始时没有用户内存，
// 但会映射 trampoline 和 trapframe 两个特殊页面。
pagetable_t proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // 创建一个空的页表。
  pagetable = create_pagetable();
  if(pagetable == 0)
    return 0;

  // 映射 trampoline 代码（用于系统调用返回），
  // 映射到用户虚拟地址空间的最高地址处。
  // 只有内核会用到这个页面，用户态不可访问（不设置 PTE_U）。
  if(map_region(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X) < 0){
    destroy_pagetable(pagetable);
    return 0;
  }

  // 映射 trapframe 页面（用于保存用户寄存器），
  // 映射在 trampoline 页面之下，供 trampoline.S 使用。
  if(map_region(pagetable, TRAPFRAME, (uint64)(p->trapframe), PGSIZE, PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    destroy_pagetable(pagetable);
    return 0;
  }

  return pagetable;
}

// 释放进程的用户页表，并释放其所引用的物理内存。
void proc_freepagetable(pagetable_t pagetable)
{
  // 解除 trampoline 页面映射。
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  // 解除 trapframe 页面映射。
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  // 释放整个页表及其映射的用户空间物理内存。
  destroy_pagetable(pagetable);
}

// 创建新进程
// entry: 进程入口函数
int create_process(void (*entry)(void))
{
  struct proc *p;
  
  // 分配进程结构体
  if((p = alloc_process()) == 0) {
    return -1;
  }
  
  // 设置进程上下文，使其从指定入口开始执行
  p->context.ra = (uint64)entry;
  
  // 初始化进程名
  safestrcpy(p->name, "userprocess", sizeof(p->name));
  
  // 设置父进程为当前进程
  p->parent = myproc() ? myproc() : initproc;

  // 设置进程为可运行状态
  p->state = RUNNABLE;
  
  printf("Created process %d with entry %p\n", p->pid, entry);

  return p->pid;
}

// 复制当前进程，创建一个拥有独立用户地址空间的子进程
int fork_process(void)
{
  struct proc *p = myproc();
  struct proc *np;

  if(p == 0 || p->pagetable == 0)
    return -1;

  // 分配新的进程控制块和内核栈/陷阱帧
  if((np = alloc_process()) == 0)
    return -1;

  // 为子进程创建独立的用户页表，并映射 trampoline/trapframe
  if((np->pagetable = proc_pagetable(np)) == 0){
    free_process(np);
    return -1;
  }

  // 拷贝父进程的用户空间内容到子进程
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    proc_freepagetable(np->pagetable);
    np->pagetable = 0;
    free_process(np);
    return -1;
  }

  np->sz = p->sz;
  // 复制陷阱帧，使得子进程从和父进程相同的位置恢复
  *(np->trapframe) = *(p->trapframe);
  // 子进程在用户态看到的 fork 返回值为 0
  np->trapframe->a0 = 0;  // 子进程返回0

  for(int i = 0; i < NOFILE; i++) {
    if(p->ofile[i]) {
      np->ofile[i] = filedup(p->ofile[i]);
    } else {
      np->ofile[i] = 0;
    }
  }
  if(p->cwd) {
    np->cwd = idup(p->cwd);
  } else {
    np->cwd = 0;
  }

  safestrcpy(np->name, p->name, sizeof(np->name));
  np->parent = p;
  np->state = RUNNABLE;

  return np->pid;
}

// 将被遗弃的子进程重新父级到init进程
void reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++) {
    if(pp->parent == p) {
      pp->parent = initproc;
      // 唤醒init进程来处理这些子进程
      wakeup(initproc);
    }
  }
}

// 退出当前进程
void exit_process(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  for(int fd = 0; fd < NOFILE; fd++) {
    if(p->ofile[fd]) {
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }
  if(p->cwd) {
    iput(p->cwd);
    p->cwd = 0;
  }

  // 将任何子进程交给init进程
  reparent(p);

  // 父进程可能在wait()中睡眠
  acquire(&wait_lock);
  wakeup(p->parent);
  release(&wait_lock);

  p->xstate = status;
  p->state = ZOMBIE;

  // 跳入调度器，永不返回
  intr_off();
  sched();
  panic("zombie exit");
}

// 等待子进程退出
int wait_process(int *status)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;) {
    // 扫描进程表寻找退出的子进程
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++) {
      if(pp->parent == p) {
        havekids = 1;
        if(pp->state == ZOMBIE) {
          // 找到一个僵尸进程
          pid = pp->pid;
          
          // 复制退出状态
          if(status != 0) {
            *status = pp->xstate;
          }

          release(&wait_lock);
          free_process(pp);
          return pid;
        }
      }
    }

    // 如果没有子进程或当前进程已被杀死，则无需等待
    if(!havekids || p->killed) {
      release(&wait_lock);
      return -1;
    }

    // 等待子进程退出
    sleep(p, &wait_lock);
  }
}

// 进程调度器
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  
  for(;;) {
    intr_on();
    intr_off();
    
    int found = 0;
    int start = next_sched_index;

    // 遍历进程表寻找可运行进程（从上次停止的位置开始，保障轮转）
    for(int i = 0; i < NPROC; i++) {
      int idx = (start + i) % NPROC;
      p = &proc[idx];

      if(p->state != RUNNABLE)
        continue;

      found = 1;
      intr_off();
      p->state = RUNNING;
      c->proc = p;
      next_sched_index = (idx + 1) % NPROC;

      swtch(&c->context, &p->context);

      c->proc = 0;
      break; // 进行一次调度后回到外层循环，重新评估队列
    }

    if(!found) {
      intr_on();
      asm volatile("wfi");
    }
  }
}

// 切换到调度器
void sched(void)
{
  int intena;
  struct proc *p = myproc();
  
  // 检查前置条件
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");
  
  // 保存中断状态并切换到调度器
  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 让出CPU
void yield(void)
{
  struct proc *p = myproc();
  intr_off();
  p->state = RUNNABLE;
  sched();
  intr_on();
}

// 子进程第一次调度时执行
void forkret(void)
{
  usertrapret();
}

// 进程睡眠
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep: no proc");
  if(lk == 0)
    panic("sleep without lk");

  // 假设调用者已持有 lk，先标记睡眠状态
  p->chan = chan;
  p->state = SLEEPING;

  // 释放外部锁，并让出 CPU
  release(lk);
  push_off();
  sched();
  pop_off();

  // 返回前重新加锁并清理通道
  acquire(lk);
  p->chan = 0;
}

// 唤醒在指定通道上睡眠的所有进程
void wakeup(void *chan)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()) {
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
    }
  }
}

// 杀死指定PID的进程
int kill_process(int pid)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->pid == pid) {
      p->killed = 1;
      if(p->state == SLEEPING) {
        // 从睡眠中唤醒进程
        p->state = RUNNABLE;
      }
      return 0;
    }
  }
  return -1;
}

// 设置进程为已杀死状态
void setkilled(struct proc *p)
{
  p->killed = 1;
}

// 检查进程是否已被杀死
int killed(struct proc *p)
{
  return p->killed;
}

extern void test(void);
// 设置第一个用户进程
void userinit(void)
{
  struct proc *p;
  
  p = alloc_process();
  initproc = p;
  p->cwd = iget(ROOTDEV, ROOTINO);

  // 创建并初始化用户页表
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0)
    panic("userinit: pagetable");

  // 将初始化程序拷贝到用户空间的第一个页面
  uvmfirst(p->pagetable, initcode, initcode_size);
  p->sz = PGSIZE;

  struct file *f0 = filealloc();
  struct file *f1 = filealloc();
  struct file *f2 = filealloc();
  if(f0 == 0 || f1 == 0 || f2 == 0)
    panic("userinit: filealloc");

  f0->type = FD_DEVICE;
  f0->readable = 1;
  f0->writable = 0;
  f0->major = CONSOLE;

  f1->type = FD_DEVICE;
  f1->readable = 0;
  f1->writable = 1;
  f1->major = CONSOLE;

  f2->type = FD_DEVICE;
  f2->readable = 0;
  f2->writable = 1;
  f2->major = CONSOLE;

  p->ofile[0] = f0;
  p->ofile[1] = f1;
  p->ofile[2] = f2;

  // 设置用户态初始寄存器
  p->trapframe->epc = 0;           // 用户程序入口
  p->trapframe->sp  = PGSIZE;      // 用户栈顶（位于第一页顶部）

  safestrcpy(p->name, "init", sizeof(p->name));

  printf("user_init: first user process pid=%d\n", p->pid);

  p->state = RUNNABLE;
}

// 简单测试任务函数
void simple_task(void) {
    printf("Process %d running\n", myproc()->pid);
    for(volatile int i = 0; i < 1000000; i++); // 简单延时
    exit_process(0);
}

//进程创建测试
void test_process_creation(void) {
  printf("Testing process creation...\n");
  // 测试基本的进程创建
  int pid = create_process(simple_task);
  assert(pid > 0);
  // 测试进程表限制
  int pids[NPROC];
  int count = 0;
  for (int i = 0; i < NPROC + 5; i++) {
    int pid = create_process(simple_task);
    if (pid > 0) {
      pids[count++] = pid;
    } else {
      break;
    }
  }
  printf("Created %d processes\n", count);
  // 清理测试进程
  for (int i = 0; i < count; i++) {
    wait_process(0);
  }
  printf("等待结束\n");
}

// CPU密集型任务
void cpu_intensive_task(void) {
    int pid = myproc()->pid;
    printf("CPU task %d starting\n", pid);
    
    for (int i = 0; i < 5; i++) {
        uint64 sum = 0;
        for (int j = 0; j < 100000; j++) {
            sum += j * j;
        }
        printf("Task %d iteration %d, sum=%lu\n", pid, i, sum);
        yield(); // 主动让出CPU
    }
    
    printf("CPU task %d finished\n", pid);
    exit_process(0);
}

//调度器测试
void test_scheduler(void) {
    printf("Testing scheduler...\n");
    // 创建多个计算密集型进程
    for (int i = 0; i < 3; i++) {
        create_process(cpu_intensive_task);
    }
    // 观察调度行为
    uint64 start_time = get_time();
    uint64 start = get_time();
    while (get_time() - start < 1000000) {
        // 空转等待
    }
    for (int i = 0; i < 3; i++) {
    wait_process(0);
    }
    uint64 end_time = get_time();
    printf("Scheduler test completed in %lu cycles\n", 
           end_time - start_time);
 }

// 共享缓冲区
#define BUFFER_SIZE 3
struct shared_buffer {
  int data[BUFFER_SIZE];
  int in, out, count;
} buffer;

static struct semaphore buffer_mutex;
static struct semaphore buffer_empty;
static struct semaphore buffer_full;

void shared_buffer_init(void) {
  buffer.in = buffer.out = buffer.count = 0;
  sem_init(&buffer_mutex, 1, "buf-mtx");
  sem_init(&buffer_empty, BUFFER_SIZE, "buf-empty");
  sem_init(&buffer_full, 0, "buf-full");
}

// 不主动yield的CPU密集型任务，依靠时钟中断抢占
void stubborn_task(void) {
  int pid = myproc()->pid;
  printf("Stubborn task %d starting without yield\n", pid);

  intr_on(); // 开启中断以允许定时器驱动抢占

  uint64 start = ticks;
  while (ticks - start < 5) {
    // 空转等待时钟中断推动ticks前进
  }

  printf("Stubborn task %d observed %lu ticks and exits\n", pid, ticks - start);
  exit_process(0);
}

// 用于观察调度器是否能在卡死任务存在时获得运行机会
void observer_task(void) {
  int pid = myproc()->pid;
  intr_on();
  for (int i = 0; i < 3; i++) {
    printf("Observer %d iteration %d (ticks=%lu)\n", pid, i, ticks);
    yield();
  }

  printf("Observer %d completed\n", pid);
  exit_process(0);
}

// 验证时钟中断驱动的抢占式调度可在任务不yield时仍推进系统
void test_preemptive_scheduler(void) {
  printf("Testing preemptive scheduler with timer interrupts...\n");

  int stubborn_pid = create_process(stubborn_task);
  int observer1 = create_process(observer_task);
  int observer2 = create_process(observer_task);

  assert(stubborn_pid > 0 && observer1 > 0 && observer2 > 0);

  for (int i = 0; i < 3; i++) {
    wait_process(0);
  }

  printf("Preemptive scheduler test completed\n");
}

void producer_task(void) {
  intr_on();

  for (int i = 0; i < 5; i++) {
    sem_wait(&buffer_empty);
    sem_wait(&buffer_mutex);

    buffer.data[buffer.in] = i;
    buffer.in = (buffer.in + 1) % BUFFER_SIZE;
    buffer.count++;
    printf("Produced: %d\n", i);

    sem_signal(&buffer_mutex);
    sem_signal(&buffer_full);

    //yield();
  }
  exit_process(0);
}

void consumer_task(void) {
  intr_on();

  for (int i = 0; i < 5; i++) {
    sem_wait(&buffer_full);
    sem_wait(&buffer_mutex);

    int item = buffer.data[buffer.out];
    buffer.out = (buffer.out + 1) % BUFFER_SIZE;
    buffer.count--;
    printf("Consumed: %d\n", item);

    sem_signal(&buffer_mutex);
    sem_signal(&buffer_empty);

    //yield();
  }
  exit_process(0);
}

 //同步机制测试
 void test_synchronization(void) {
    // 测试生产者-消费者场景
    shared_buffer_init();
    create_process(producer_task);
    create_process(consumer_task);
    // 等待完成
    wait_process(0);
    wait_process(0);
    printf("Synchronization test completed\n");
 }

 //进程状态调试
void debug_proc_table(void) {
  printf("=== Process Table ===\n");
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc[i];
    if (p->state != UNUSED) {
      printf("PID:%d State:%d Name:%s\n", 
        p->pid, p->state, p->name);
    }
  }
}

int run_kernel_tests(void)
{
  static int tests_executed = 0;

  if(tests_executed){
    printf("[kernel-test] tests already executed\n");
    return 0;
  }

  tests_executed = 1;

  printf("[kernel-test] begin\n");
  test_process_creation();

  uint64 start = get_time();
  while(get_time() - start < 500000); 

  printf("\n");
  test_scheduler();

  start = get_time();
  while(get_time() - start < 500000);
  printf("\n");
  test_synchronization();
  //printf("\n");
  //test_preemptive_scheduler();
  printf("[kernel-test] end\n");
  return 0;
}