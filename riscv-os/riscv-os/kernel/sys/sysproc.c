// 进程相关的系统调用实现。

#include "types.h"
#include "proc.h"
#include "syscall.h"
#include "vm.h"

// 获取当前进程的 PID
int sys_getpid(void) {
    return myproc()->pid;
}

// fork: 复制当前进程，子进程返回 0，父进程返回子 PID
int sys_fork(void) {
    return fork_process();
}

// exit: 读取退出码并终止当前进程
int sys_exit(void) {
    int status = 0;
    if(argint(0, &status) < 0)
        return -1;   // 参数解析失败时直接报错，不修改系统状态。
    exit_process(status);   // 完成资源回收并切入调度器。
    return 0; // 不会返回
}

// wait: 等待任意子进程结束，可选写回退出状态
int sys_wait(void) {
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
int sys_kill(void) {
    int pid = 0;
    if(argint(0, &pid) < 0)
        return -1;   // 目标 PID 解析失败。
    return kill_process(pid);
}

// sys_sbrk: 调整当前进程的用户空间大小，返回旧的堆顶地址。
//   - n > 0: 逐页申请并零填充新页面；
//   - n < 0: 释放多余页面，最小收缩到 0。
//   - 失败时返回 -1。
int sys_sbrk(void) {
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
