#include "types.h"
#include "riscv.h"

extern pagetable_t kernel_pagetable;

void kvminit(void);
void kvminithart(void);

pagetable_t create_pagetable(void);
void destroy_pagetable(pagetable_t pt);
pte_t* walk_lookup(pagetable_t pt, uint64 va);
pte_t* walk_create(pagetable_t pt, uint64 va);
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm);
int map_region(pagetable_t pagetable, uint64 va, uint64 pa,uint64 size, int perm);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
// 将父进程的页表内容拷贝到子进程
int uvmcopy(pagetable_t old, pagetable_t newp, uint64 sz);
// 从用户空间取数据到内核缓冲区
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
// 将内核缓冲区写回用户空间
int copyout(pagetable_t pagetable, uint64 dstva, const char *src, uint64 len);

// 用户地址空间相关辅助函数
void uvmfirst(pagetable_t pagetable, const uint8 *src, uint64 sz);
void uvmfree(pagetable_t pagetable, uint64 sz);

void dump_pagetable(pagetable_t pt, int level);