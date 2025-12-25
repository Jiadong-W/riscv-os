// 进程相关的系统调用实现。

#include "types.h"
#include "proc.h"
#include "syscall.h"
#include "vm.h"
#include "trap.h"
#include "log.h"
#include "klog.h"

extern volatile uint64 ticks;
extern struct spinlock tickslock;

// 获取当前进程的 PID
uint64 sys_getpid(void) {
    return myproc()->pid;
}

// fork: 复制当前进程，子进程返回 0，父进程返回子 PID
uint64 sys_fork(void) {
    return fork_process();
}

// exit: 读取退出码并终止当前进程
uint64 sys_exit(void) {
    int status = 0;
    if(argint(0, &status) < 0)
        return -1;   // 参数解析失败时直接报错，不修改系统状态。
    exit_process(status);   // 完成资源回收并切入调度器。
    return 0; // 不会返回
}

// wait: 等待任意子进程结束，可选写回退出状态
uint64 sys_wait(void) {
    uint64 addr = 0;
    if(argaddr(0, &addr) < 0)
        return -1;   // 解析用户态指针，非法输入立即返回。

    int status = 0;
    int *status_ptr = addr ? &status : 0;

    if(addr && check_user_ptr_rw((void*)addr, sizeof(status), 1) < 0)
        return -1;   // 确认用户缓冲区可写，阻止越界/非法页访问。

    int pid = wait_process(status_ptr);

    if(pid >= 0 && addr) {
        if(copyout(myproc()->pagetable, (uint64)addr, (const char*)&status, sizeof(status)) < 0)
            return -1;   // 将退出码写回用户缓冲区可能失败，需返回错误。
    }
    return pid;
}

// kill: 向目标 PID 设置 killed 标记
uint64 sys_kill(void) {
    int pid = 0;
    if(argint(0, &pid) < 0)
        return -1;   // 目标 PID 解析失败。
    return kill_process(pid);
}

uint64 sys_time(void) {
    return get_time();
}

uint64 sys_ticks(void) {
    acquire(&tickslock);
    uint64 value = ticks;
    release(&tickslock);
    return value;
}

uint64 sys_sleep(void) {
    int ticks_to_sleep = 0;
    if(argint(0, &ticks_to_sleep) < 0)
        return -1;
    if(ticks_to_sleep <= 0)
        return 0;

    acquire(&tickslock);
    uint64 start = ticks;
    while(ticks - start < (uint64)ticks_to_sleep) {
        if(killed(myproc())) {
            release(&tickslock);
            return -1;
        }
        sleep((void*)&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64 sys_getpriority(void) {
    struct proc *p = myproc();
    if(p == 0)
        return -1;
    return p->priority_level;
}

// sys_sbrk: 调整当前进程的用户空间大小，返回旧的堆顶地址。
//   - n > 0: 逐页申请并零填充新页面；
//   - n < 0: 释放多余页面，最小收缩到 0。
//   - 失败时返回 -1。
uint64 sys_sbrk(void) {
    int n = 0;
    if(argint(0, &n) < 0)
        return -1;   // 获取堆增量失败。

    struct proc *p = myproc();
    uint64 oldsz = p->sz;

    if(n > 0) {
        uint64 newsz = oldsz + (uint64)n;
        if(newsz < oldsz)
            return -1; // 检测溢出，防止地址空间回绕。
        uint64 res = uvmalloc(p->pagetable, oldsz, newsz); // 按页分配物理内存并建立映射。
        if(res == 0)
            return -1;   // 分配失败，保持旧的 break。
        p->sz = res;  // 记录新的地址空间上限。
    } else if(n < 0) {
        long target = (long)oldsz + (long)n;
        if(target < 0)
            target = 0; // 不允许收缩到负地址。
        p->sz = uvmdealloc(p->pagetable, oldsz, (uint64)target); // 释放多余页面并更新记录。
    }

    return (int)oldsz;
}

uint64 sys_set_crash_stage(void) {
    int stage;
    if(argint(0, &stage) < 0)
        return -1;
    extern int crash_stage;
    crash_stage = stage;
    return 0;
}

uint64 sys_recover_log(void) {
    recover_log();
    return 0;
}

extern void clear_cache(void);

uint64 sys_clear_cache(void) {
    clear_cache();
    return 0;
}

uint64 sys_klog_dump(void) {
    klog_dump();
    return 0;
}

uint64 sys_klog_set_threshold(void) {
    int record_level = 0;
    int console_level = 0;
    if(argint(0, &record_level) < 0 || argint(1, &console_level) < 0)
        return -1;
    if(record_level < KLOG_LEVEL_ERROR || record_level > KLOG_LEVEL_DEBUG)
        return -1;
    if(console_level < KLOG_LEVEL_ERROR || console_level > KLOG_LEVEL_DEBUG)
        return -1;
    klog_set_threshold((klog_level_t)record_level, (klog_level_t)console_level);
    return 0;
}
