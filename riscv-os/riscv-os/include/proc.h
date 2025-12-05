#include "spinlock.h"
#include "riscv.h"
#include "file.h"

// 内核上下文切换时保存的寄存器
struct context {
  uint64 ra;   // 返回地址寄存器，保存函数返回后继续执行的地址
  uint64 sp;   // 栈指针寄存器，指向当前栈顶位置

  // 被调用者保存的寄存器
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// CPU状态
struct cpu {
  struct proc *proc;      // 当前在该CPU上运行的进程，为空表示无进程运行
  struct context context; // 切换到这里以进入调度器
  int noff;               // push_off()嵌套深度，用于中断禁用控制
  int intena;             // 在push_off()之前中断是否启用
};

// 单核系统：只有一个CPU
extern struct cpu cpu;

// 陷阱帧结构，用于在用户空间和内核空间之间切换时保存和恢复寄存器状态
// 该结构位于用户页表中蹦床页面下方的独立页面中，在内核页表中没有特殊映射
// trampoline.S 中的 uservec 会将用户寄存器保存到陷阱帧中，然后从陷阱帧的
// kernel_sp、kernel_hartid、kernel_satp 初始化寄存器，并跳转到 kernel_trap
// trampoline.S 中的 usertrapret() 和 userret 会设置陷阱帧的 kernel_* 字段，
// 从陷阱帧恢复用户寄存器，切换到用户页表，然后进入用户空间
// 陷阱帧包含被调用者保存的用户寄存器（s0-s11），因为通过 usertrapret() 返回到用户空间的路径
// 不会经过整个内核调用栈
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // 内核页表
  /*   8 */ uint64 kernel_sp;     // 进程内核栈的栈顶
  /*  16 */ uint64 kernel_trap;   // usertrap()函数地址
  /*  24 */ uint64 epc;           // 保存的用户程序计数器
  /*  32 */ uint64 kernel_hartid; // （单核暂未使用）内核线程指针占位
  /*  40 */ uint64 ra;            // 返回地址
  /*  48 */ uint64 sp;            // 栈指针
  /*  56 */ uint64 gp;            // 全局指针
  /*  64 */ uint64 tp;            // 线程指针
  /*  72 */ uint64 t0;            // 临时寄存器
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;            // 被调用者保存寄存器
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;            // 函数参数/返回值寄存器
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;            // 被调用者保存寄存器
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;            // 临时寄存器
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

// 进程状态枚举
enum procstate { 
  UNUSED,    // 未使用
  USED,      // 已使用但未准备运行
  SLEEPING,  // 睡眠中（等待事件）
  RUNNABLE,  // 可运行（就绪状态）
  RUNNING,   // 正在运行
  ZOMBIE     // 僵尸状态（已终止但父进程未回收）
};

// 进程控制块：每个进程的状态信息
struct proc {
  struct spinlock lock;  // 进程锁

  enum procstate state;  // 进程状态
  void *chan;            // 如果非零，表示进程在该通道上睡眠
  int killed;            // 如果非零，表示进程已被杀死
  int xstate;            // 退出状态，返回给父进程的wait
  int pid;               // 进程ID

  // 使用parent时必须持有wait_lock：
  struct proc *parent;   // 父进程指针

  uint64 kstack;        // 内核栈底部虚拟地址
  uint64 sz;            // 用户空间大小（字节）
  pagetable_t pagetable; // 用户页表
  struct trapframe *trapframe; // 陷阱帧页面，用于trampoline.S
  struct context context;      // 上下文切换结构，swtch()切换到这里来运行进程
  struct file *ofile[NOFILE];  // 打开文件表
  struct inode *cwd;           // 当前工作目录
  char name[16];               // 进程名称（调试用）
};

void swtch(struct context *old, struct context *new);

// 进程管理相关函数声明：内核态进程生命周期控制接口
void procinit(void);
struct cpu* mycpu(void);
struct proc* myproc(void);
int allocpid(void);
struct proc* alloc_process(void);
void free_process(struct proc *p);
pagetable_t proc_pagetable(struct proc *p);
void proc_freepagetable(pagetable_t pagetable);
int create_process(void (*entry)(void));
int fork_process(void);
void reparent(struct proc *p);
void exit_process(int status);
int wait_process(int *status);
void scheduler(void);
void sched(void);
void yield(void);
void forkret(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
int kill_process(int pid);
void setkilled(struct proc *p);
int killed(struct proc *p);
void userinit(void);

// 内核自带的测试/演示例程声明
void test_process_creation(void);
void test_scheduler(void);
void test_synchronization(void);
void debug_proc_table(void);
int run_kernel_tests(void);