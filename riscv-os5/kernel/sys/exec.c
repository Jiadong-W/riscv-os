#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "elf.h"
#include "fs.h"
#include "file.h"
#include "log.h"
#include "exec.h"
#include "vm.h"
#include "string.h"
#include "printf.h"
#include "klog.h"

// 静态函数声明
static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz);

/*
 * 将ELF段标志转换为页表权限位
 * ELF标志位含义：
 *   0x1: 可执行 (PTE_X)
 *   0x2: 可写 (PTE_W)  
 *   0x4: 可读 (PTE_R)
 * 返回值：对应的页表权限位组合
 */
int flags2perm(int flags)
{
    int perm = PTE_U; // 用户模式可访问是基本权限
    if(flags & 0x1)  // 可执行标志
        perm |= PTE_X;
    if(flags & 0x2)  // 可写标志  
        perm |= PTE_W;
    if(flags & 0x4)  // 可读标志
        perm |= PTE_R;
    return perm;
}

/*
 * exec系统调用的核心实现
 * 功能：加载并执行指定的可执行文件，替换当前进程的地址空间
 * 参数：
 *   path - 可执行文件路径
 *   argv - 命令行参数数组
 * 返回值：成功时不返回（跳转到用户程序），失败返回-1
 */
int kernel_exec(char *path, char **argv)
{
  char *s, *last;
  int i;
  uint64 off;
  uint64 argc, sz = 0, sp;
  uint64 ustack[MAXARG];  // 用户栈参数数组
  struct elfhdr elf;      // ELF文件头
  struct inode *ip;       // 文件inode
  struct proghdr ph;      // 程序头
  pagetable_t pagetable = 0;  // 新页表
  struct proc *p = myproc();  // 当前进程
  const char *fail_reason = "unknown";

  klog_info("exec: pid=%d 请求加载 %s", p->pid, path);
  begin_transaction();

  // 步骤1: 打开并锁定可执行文件
  if((ip = namei(path)) == 0){
    end_transaction();
    klog_warn("exec: pid=%d 找不到文件 %s", p->pid, path);
    return -1;  // 文件不存在
  }
  ilock(ip);

  // 步骤2: 读取并验证ELF文件头
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf)) {
    fail_reason = "读取ELF头失败";
    goto bad;  // 读取ELF头失败
  }

  // 检查ELF魔数，确认是有效的ELF文件
  if(elf.magic != ELF_MAGIC) {
    fail_reason = "ELF 魔数无效";
    goto bad;  // 不是有效的ELF文件
  }

  // 步骤3: 创建新的进程页表
  if((pagetable = proc_pagetable(p)) == 0) {
    fail_reason = "创建用户页表失败";
    goto bad;  // 页表创建失败
  }

  // 步骤4: 加载所有程序段到内存
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 读取程序头
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)) {
      fail_reason = "读取程序段头失败";
      goto bad;
    }
    // 只处理LOAD类型的段（需要加载到内存的段）
    if(ph.type != PT_LOAD)
      continue;
    // 参数检查：内存大小不能小于文件大小
    if(ph.memsz < ph.filesz) {
      fail_reason = "程序段 memsz 小于 filesz";
      goto bad;
    }
    // 地址溢出检查  
    if(ph.vaddr + ph.memsz < ph.vaddr) {
      fail_reason = "程序段虚拟地址溢出";
      goto bad;
    }
    // 虚拟地址必须页面对齐
    if(ph.vaddr % PGSIZE != 0) {
      fail_reason = "程序段虚拟地址未对齐";
      goto bad;
    }
    // 为当前段分配虚拟内存空间
    uint64 newsz;
    if((newsz = uvmalloc_perm(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0) {
      fail_reason = "为程序段分配内存失败";
      goto bad;
    }
    sz = newsz;
    // 将段内容从文件加载到内存
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0) {
      fail_reason = "加载程序段数据失败";
      goto bad;
    }
  }
  // 文件加载完成，释放文件锁和inode
  iunlockput(ip);
  end_transaction();
  ip = 0;

  // 重新获取当前进程指针（防止在加载过程中被切换）
  p = myproc();

  // 步骤5: 设置用户栈
  // 将当前大小向上取整到页面边界
  sz = PGROUNDUP(sz);
  
  // 为用户栈分配页面（包括一个保护页面）
  // 假设用户栈大小为1个页面，保护页面1个，共2个页面
  uint64 stacksize = 2 * PGSIZE;  // 1个栈页面 + 1个保护页面
  uint64 newsz;
  if((newsz = uvmalloc_perm(pagetable, sz, sz + stacksize, PTE_R | PTE_W | PTE_U)) == 0) {
    fail_reason = "分配用户栈失败";
    goto bad;
  }
  sz = newsz;
  
  // 设置保护页面（不可访问）
  // 栈从高地址向低地址增长，保护页面在栈顶之上
  uint64 stacktop = sz;  // 栈顶（保护页面底部）
  uint64 stackbottom = stacktop - PGSIZE;  // 实际栈底部
  
  // 清除保护页面的用户权限，使其不可访问
  pte_t* pte = walk_lookup(pagetable, stacktop - PGSIZE);
  if(pte) {
    *pte &= ~PTE_U;  // 移除用户权限，创建保护页面
  }
  
  sp = stackbottom;  // 初始栈指针指向栈底部

  // 步骤6: 将命令行参数拷贝到用户栈
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG) {
      fail_reason = "命令行参数数量超过上限";
      goto bad;  // 参数过多
    }
    
    // 计算参数字符串在栈中的位置（向低地址增长）
    sp -= strlen(argv[argc]) + 1;  // 为字符串分配空间（包括终止符）
    sp -= sp % 16;  // RISC-V要求栈指针16字节对齐
    
    // 栈溢出检查
    if(sp < (sz - stacksize)) {
      fail_reason = "参数字符串写入导致栈空间不足";
      goto bad;
    }
      
    // 将参数字符串拷贝到用户栈
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0) {
      fail_reason = "复制参数字符串到用户栈失败";
      goto bad;
    }
      
    // 记录参数字符串的地址
    ustack[argc] = sp;
  }
  ustack[argc] = 0;  // 参数列表以NULL结尾

  // 步骤7: 将参数指针数组压栈
  sp -= (argc+1) * sizeof(uint64);  // 为指针数组分配空间
  sp -= sp % 16;  // 16字节对齐
  
  // 栈溢出检查
  if(sp < (sz - stacksize)) {
    fail_reason = "参数指针数组写入导致栈空间不足";
    goto bad;
  }
    
  // 将参数指针数组拷贝到用户栈
  if(copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0) {
    fail_reason = "复制参数指针数组到用户栈失败";
    goto bad;
  }

  // 步骤8: 设置用户程序执行上下文
  // 将参数数组地址保存在a1寄存器（RISC-V中a1对应第二个参数argv）
  p->trapframe->a1 = sp;

  // 保存程序名称用于调试（提取路径中的文件名部分）
  for(last = s = path; *s; s++)
    if(*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // 步骤9: 提交新的用户镜像
  pagetable_t oldpagetable = p->pagetable;  // 保存旧页表
  p->pagetable = pagetable;  // 切换为新页表
  p->sz = sz;  // 更新进程大小
  
  // 设置程序计数器和栈指针
  p->trapframe->epc = elf.entry;  // 程序入口地址（通常是main函数）
  p->trapframe->sp = sp;         // 用户栈指针
  
  // 释放旧页表和地址空间
  proc_freepagetable(oldpagetable);

  // 返回argc（在RISC-V中通过a0寄存器返回）
  klog_info("exec: pid=%d 成功加载 %s, argc=%d", p->pid, last, (int)argc);
  return argc;  

// 错误处理标签
bad:
  if(pagetable)
    proc_freepagetable(pagetable);  // 释放新建的页表
  if(ip){
    iunlockput(ip);  // 释放文件锁和inode
    end_transaction();
  }
  klog_error("exec: pid=%d 加载 %s 失败: %s", p->pid, path, fail_reason);
  return -1;  // 执行失败
}

/*
 * 加载ELF程序段到页表指定的虚拟地址
 * 参数：
 *   pagetable - 目标页表
 *   va - 虚拟地址（必须页面对齐）
 *   ip - 可执行文件inode
 *   offset - 段在文件中的偏移量
 *   sz - 段大小（字节数）
 * 返回值：成功返回0，失败返回-1
 * 注意：调用前必须确保[va, va+sz)虚拟地址范围已经映射
 */
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  // 逐页加载段内容
  for(i = 0; i < sz; i += PGSIZE){
    // 查找虚拟地址对应的物理地址
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");  // 地址应该已经映射
    
    // 计算当前页需要读取的字节数
    if(sz - i < PGSIZE)
      n = sz - i;  // 最后一页可能不满
    else
      n = PGSIZE;  // 完整的一页
      
    // 从文件读取数据到物理内存
    if(readi(ip, 0, (uint64)pa, offset + i, n) != n)
      return -1;  // 读取失败
  }
  
  return 0;
}

/*
 * 通过页表查找虚拟地址对应的物理地址
 * 参数：
 *   pagetable - 页表
 *   va - 虚拟地址
 * 返回值：物理地址，如果未映射返回0
 */
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk_lookup(pagetable, va);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}