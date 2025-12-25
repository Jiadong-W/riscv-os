//
// 系统调用核心调度与参数处理逻辑。
// 负责：
//   1. 管理系统调用表（编号 -> 内核实现函数）。
//   2. 提供参数解析、用户指针验证等安全辅助函数。
//   3. 在陷入后根据 a7 中的编号分发到具体 sys_xxx()。
//
// 约定：
//   - 所有系统调用实现返回值写入 trapframe->a0。
//   - 参数通过 trapframe->a0~a5 传入（共 6 个整型/指针参数）。
//   - 需要使用 argint/argaddr/argstr 等辅助函数完成基础校验。
//

#include "types.h"
#include "proc.h"
#include "syscall.h"
#include "printf.h"
#include "vm.h"
#include "string.h"
#include "memlayout.h"
#include "riscv.h"

uint64 sys_getpid(void);
uint64 sys_fork(void);
uint64 sys_exit(void);
uint64 sys_wait(void);
uint64 sys_kill(void);
uint64 sys_write(void);
uint64 sys_read(void);
uint64 sys_open(void);
uint64 sys_close(void);
uint64 sys_unlink(void);
uint64 sys_sbrk(void);
uint64 sys_time(void);
uint64 sys_symlink(void);
uint64 sys_set_crash_stage(void);
uint64 sys_recover_log(void);
uint64 sys_clear_cache(void);
uint64 sys_exec(void);
uint64 sys_dup(void);
uint64 sys_mknod(void);
uint64 sys_chdir(void);
uint64 sys_ticks(void);
uint64 sys_getpriority(void);
uint64 sys_klog_dump(void);
uint64 sys_klog_set_threshold(void);
uint64 sys_sleep(void);

// 系统调用描述符
struct syscall_desc {
    uint64 (*func)(void);      // 对应的内核实现入口
    char *name;             // 调试使用的名称
    int arg_count;          // 需要读取的参数数量（位于 a0~a5）
    // 可扩展：参数类型描述、权限控制等元数据
};

// 系统调用表
struct syscall_desc syscall_table[] = {
    [SYS_exit]   = { sys_exit,   "exit",   1 },
    [SYS_getpid] = { sys_getpid, "getpid", 0 },
    [SYS_fork]   = { sys_fork,   "fork",   0 },
    [SYS_wait]   = { sys_wait,   "wait",   1 },
    [SYS_kill]   = { sys_kill,   "kill",   1 },
    [SYS_write]  = { sys_write,  "write",  3 },
    [SYS_read]   = { sys_read,   "read",   3 },
    [SYS_open]   = { sys_open,   "open",   2 },
    [SYS_close]  = { sys_close,  "close",  1 },
    [SYS_unlink] = { sys_unlink, "unlink", 1 },
    [SYS_sbrk]   = { sys_sbrk,   "sbrk",   1 },
    [SYS_time]   = { sys_time,   "time",   0 },
    [SYS_symlink] = { sys_symlink, "symlink", 2 },
    [SYS_set_crash_stage] = { sys_set_crash_stage, "set_crash_stage", 1 },
    [SYS_recover_log] = { sys_recover_log, "recover_log", 0 },
    [SYS_clear_cache] = { sys_clear_cache, "clear_cache", 0 },
    [SYS_exec]   = { sys_exec,   "exec",   2 }, 
    [SYS_dup]   = { sys_dup,   "dup",   1 },
    [SYS_mknod] = { sys_mknod, "mknod", 4 }, 
    [SYS_chdir] = { sys_chdir, "chdir", 1 }, 
    [SYS_ticks] = { sys_ticks, "ticks", 0 },
    [SYS_getpriority] = { sys_getpriority, "getpriority", 0 },
    [SYS_klog_dump] = { sys_klog_dump, "klog_dump", 0 },
    [SYS_klog_set_threshold] = { sys_klog_set_threshold, "klog_set_threshold", 2 },
    [SYS_sleep] = { sys_sleep, "sleep", 1 },
};

//
// check_user_range
//   确认用户态指针 [addr, addr+size) 对应的每个页面：
//     1. 落在合法用户地址空间（MAXVA 以下）。
//     2. 页表项存在且带有 PTE_U 标记。
//     3. 若 write=1，则要求页面具备写权限。
//   若全部满足返回 0，否则返回 -1。
//
static int check_user_range(uint64 addr, int size, int write)
{
    struct proc *p = myproc();
    pagetable_t pagetable = p->pagetable;
    uint64 start = addr;
    uint64 end;

    if(size < 0)
        return -1;
    if(size == 0)
        return 0;

    if(addr >= MAXVA)
        return -1;

    if(addr + (uint64)size < addr)
        return -1;

    end = addr + (uint64)size;
    if(end > MAXVA)
        return -1;

    while(start < end) {
        uint64 va0 = PGROUNDDOWN(start);
        pte_t *pte = walk_lookup(pagetable, va0);
        if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1;
        if(write && ((*pte & PTE_W) == 0))
            return -1;

        uint64 next = va0 + PGSIZE;
        if(next <= start)
            return -1;
        if(next > end)
            next = end;
        start = next;
    }

    return 0;
}

// 对外接口：校验任意读写权限的用户指针
int check_user_ptr_rw(const void *ptr, int size, int write)
{
    return check_user_range((uint64)ptr, size, write);
}

// 常见的只读指针校验封装
int check_user_ptr(const void *ptr, int size)
{
    return check_user_ptr_rw(ptr, size, 0);
}

// 参数提取辅助函数
int get_syscall_arg(int n, long *arg) {
    struct proc *p = myproc();
    switch(n) {
        case 0: *arg = p->trapframe->a0; break;
        case 1: *arg = p->trapframe->a1; break;
        case 2: *arg = p->trapframe->a2; break;
        case 3: *arg = p->trapframe->a3; break;
        case 4: *arg = p->trapframe->a4; break;
        case 5: *arg = p->trapframe->a5; break;
        default: return -1;
    }
    return 0;
}

int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

//
// fetchstr
//   将用户态字符串拷贝到内核缓冲区，保证以 '\0' 结尾。
//   成功返回字符串长度，失败返回 -1。
//
int fetchstr(uint64 addr, char *buf, int max)
{
    struct proc *p = myproc();
    int i;
    char c;

    if(max <= 0)
        return -1;

    for(i = 0; i < max; i++) {
        if(copyin(p->pagetable, &c, addr + i, 1) < 0)
            return -1;
        buf[i] = c;
        if(c == '\0')
            return i;
    }

    buf[max - 1] = '\0';
    return -1;
}

// 读取第 n 个系统调用参数，并解析为 int
int argint(int n, int *ip)
{
    long val;
    if(get_syscall_arg(n, &val) < 0)
        return -1;
    *ip = (int)val;
    return 0;
}

// 读取第 n 个参数并验证它是合法的用户指针
int argaddr(int n, uint64 *ip)
{
    long val;
    if(get_syscall_arg(n, &val) < 0)
        return -1;
    if(val != 0) {
        if(check_user_ptr((const void*)val, 1) < 0)
            return -1;
    }
    *ip = (uint64)val;
    return 0;
}

// 读取第 n 个参数并当作用户态字符串拷贝到 buf
int argstr(int n, char *buf, int max)
{
    uint64 addr;
    if(argaddr(n, &addr) < 0)
        return -1;
    return fetchstr(addr, buf, max);
}

// 系统调用分发器
void syscall_dispatch(void) {
    struct proc *p = myproc();
    int num = p->trapframe->a7;

    if(num < 0 || num >= sizeof(syscall_table)/sizeof(syscall_table[0]) || syscall_table[num].func == 0) {
        printf("%d %s: unknown sys call %d\n",p->pid, p->name, num);
        p->trapframe->a0 = -1; // 未知系统调用
        return;
    }

    // 调用实现函数
    uint64 ret = syscall_table[num].func();
    p->trapframe->a0 = ret;
}

// 对外提供的系统调用入口，由内核陷入路径调用
void syscall(void)
{
    syscall_dispatch();
}