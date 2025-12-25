#include "kalloc.h"
#include "memlayout.h"
#include "vm.h"
#include "string.h"
#include "printf.h"
#include "riscv.h"

//内核页表
pagetable_t kernel_pagetable;

extern char etext[]; 
extern char trampoline[];

static int cow_clone_page(pagetable_t pagetable, uint64 va0) {
    // 查找虚拟地址 va0 对应的页表项
    pte_t *pte = walk_lookup(pagetable, va0);
    if (pte == 0)
        return -1;
    // 检查页表项是否有效且属于用户空间
    if ((*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
        return -1;
    // 检查是否为写时拷贝页
    if ((*pte & PTE_COW) == 0)
        return -1;

    // 获取原物理页地址
    uint64 pa = PTE2PA(*pte);

    // 分配一个新的物理页
    void *mem = alloc_page();
    if (mem == 0)
        return -1;

    // 拷贝原页内容到新物理页，实现真正写时复制
    memmove(mem, (void *)pa, PGSIZE);

    // 更新页表项：指向新物理页，去掉 COW 标记，加上写权限
    uint flags = PTE_FLAGS(*pte);
    flags = (flags | PTE_W) & ~PTE_COW;
    *pte = PA2PTE(mem) | flags;

    sfence_vma();

    page_decref((void *)pa);
    return 0;
}

int cow_resolve(pagetable_t pagetable, uint64 faultva) {
    uint64 va0 = PGROUNDDOWN(faultva);
    return cow_clone_page(pagetable, va0);
}

void kvminit(void) {
    // 1. 创建内核页表
    kernel_pagetable = create_pagetable();
    // 2. 映射内核代码段（R+X权限）
    map_region(kernel_pagetable, KERNBASE, KERNBASE, 
        (uint64)etext - KERNBASE, PTE_R | PTE_X);
    // 3. 映射内核数据段（R+W权限）
    map_region(kernel_pagetable, (uint64)etext, (uint64)etext,
        (PHYSTOP - (uint64)etext), PTE_R | PTE_W);
    // 4. 映射设备（UART等）
    map_region(kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
    map_region(kernel_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
    // 5. 映射 trampoline ，方便内核调用抢占代码
    map_region(kernel_pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

void kvminithart(void) { 
    // 将内核页表的根地址写入 satp 寄存器，激活虚拟内存，并刷新 TLB。
    w_satp(MAKE_SATP(kernel_pagetable)); 
    sfence_vma(); 
 }

 // 创建空页表
pagetable_t create_pagetable(void)
{
    pagetable_t pagetable;
    pagetable = (pagetable_t) alloc_page();
    if(pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// 查找页表项（不创建新页表）
pte_t* walk_lookup(pagetable_t pt, uint64 va)
 {
    if (va >= MAXVA) 
        panic("walk");
        
    for (int level = 2; level > 0; level--) {
        pte_t* pte = &pt[PX(level, va)];
        if (!(*pte & PTE_V)) {
            return 0;  // 中间页表不存在
        }
        // 进入下一级页表
        pt = (pagetable_t)PTE2PA(*pte);
    }
    // 返回叶子页表项
    return &pt[PX(0, va)];
}

// 查找或创建页表项（需要时创建中间页表）
pte_t* walk_create(pagetable_t pt, uint64 va) 
{
    if (va >= MAXVA) 
        panic("walk");
    
    for (int level = 2; level > 0; level--) {
        pte_t* pte = &pt[PX(level, va)];
        if (*pte & PTE_V) {
            // 页表已存在，进入下一级
            pt = (pagetable_t)PTE2PA(*pte);
        } else {
            // 分配新页表
            pt = (pagetable_t)alloc_page();
            if (pt == 0) {
                return 0;  // 内存分配失败
            }
            
            // 初始化新页表
            memset(pt, 0, PGSIZE);
            // 设置页表项
            *pte = PA2PTE(pt) | PTE_V;
        }
    }
    return &pt[PX(0, va)];
}

// 建立虚拟地址到物理地址的映射
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm) {
    // 检查地址对齐
    if ((va % PGSIZE) != 0 || (pa % PGSIZE) != 0) {
        return -1;  // 地址未页对齐
    }
    
    // 查找或创建页表项
    pte_t* pte = walk_create(pt, va);
    if (pte == 0) {
        return -1;  // 页表遍历失败
    }
    
    // 检查是否已映射
    if (*pte & PTE_V) {
        panic("mappages: remap");
    }
    
    // 设置页表项：物理地址 + 权限位 + 有效位
    *pte = PA2PTE(pa) | perm | PTE_V;
    return 0;
}

//批量映射一段连续的虚拟地址区间
int map_region(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mapregions: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mapregions: size not aligned");

  if(size == 0)
    panic("mapregions: size");
  
  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk_create(pagetable, a)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mapregions: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

//解除一段虚拟地址的映射，并可选释放对应物理页。
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk_lookup(pagetable, a)) == 0) 
      continue;   // 页表项不存在，跳过
    if((*pte & PTE_V) == 0)  
      continue;  // 页表项无效，跳过
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      page_decref((void*)pa);  // COW 模式下降引用计数
    }
    *pte = 0;
  }
}

// 复制父进程的用户地址空间到子进程
int uvmcopy(pagetable_t old, pagetable_t newp, uint64 sz)
{
    uint64 sz_rounded = PGROUNDUP(sz);
    uint64 pages_mapped = 0;           // 记录已成功映射的页数

    // 遍历每一页
    for(uint64 va = 0; va < sz_rounded; va += PGSIZE){
        pte_t *pte = walk_lookup(old, va); // 查找父进程页表项
        if(pte == 0 || (*pte & PTE_V) == 0)
            goto err; // 页表项不存在或无效，跳转到错误处理

        uint64 pa = PTE2PA(*pte);         // 获取物理页地址
        uint flags = PTE_FLAGS(*pte);     // 获取权限标志
        int cow_candidate = ((*pte & PTE_W) && (*pte & PTE_U)); // 可写且用户页，适合 COW

        page_incref((void*)pa);

        uint new_flags = flags;
        if(cow_candidate){
            // 对可写用户页，去掉写权限，加上 COW 标记
            new_flags = (flags & ~PTE_W) | PTE_COW;
        }

        // 在子进程页表中建立映射，权限包括 COW 标记
        if(map_page(newp, va, pa, new_flags & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_COW)) < 0){
            page_decref((void*)pa); // 映射失败，回退引用计数
            goto err;
        }

        if(cow_candidate){
            // 父进程页表也去掉写权限，加上 COW 标记，实现共享只读
            *pte = PA2PTE(pa) | (new_flags | PTE_V);
        }

        pages_mapped++; // 记录成功映射的页数
    }

    sfence_vma();
    return 0;

err:
    // 错误处理：回退子进程已映射的页面
    if(pages_mapped > 0)
        uvmunmap(newp, 0, pages_mapped, 1);

    // 回退父进程页表的 COW 标记（只对引用计数为1的页恢复写权限）
    uint64 revert_end = pages_mapped * PGSIZE;
    for(uint64 va = 0; va < revert_end; va += PGSIZE){
        pte_t *pte = walk_lookup(old, va);
        if(pte == 0)
            continue;
        if((*pte & PTE_COW) == 0)
            continue;
        uint64 pa = PTE2PA(*pte);
        if(page_refcount((void*)pa) == 1){
            *pte = (*pte | PTE_W) & ~PTE_COW;
        }
    }
    sfence_vma();
    return -1;
}

 // 将用户空间数据拷贝到内核缓冲区
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    while(len > 0){
        uint64 va0 = PGROUNDDOWN(srcva);
        pte_t *pte = walk_lookup(pagetable, va0);
        if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1;

        uint64 pa0 = PTE2PA(*pte);
        uint64 offset = srcva - va0;
        uint64 n = PGSIZE - offset;
        if(n > len)
            n = len;

        memmove(dst, (void*)(pa0 + offset), n);
        len -= n;
        dst += n;
        srcva += n;
    }
    return 0;
}

// 将内核缓冲区的数据写回到用户空间
int copyout(pagetable_t pagetable, uint64 dstva, const char *src, uint64 len)
{
    while(len > 0){
        uint64 va0 = PGROUNDDOWN(dstva);
        pte_t *pte = walk_lookup(pagetable, va0);
        if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1;

        // 命中写时复制页时，先完成物理页克隆
        if((*pte & PTE_COW) && cow_resolve(pagetable, va0) < 0)
            return -1;

        // 再次查找页表项，确保 COW 处理后页表项有效
        pte = walk_lookup(pagetable, va0);
        if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1;
        if((*pte & PTE_W) == 0)
            return -1;

        uint64 pa0 = PTE2PA(*pte);
        uint64 offset = dstva - va0;
        uint64 n = PGSIZE - offset;
        if(n > len)
            n = len;

        memmove((void*)(pa0 + offset), src, n);
        len -= n;
        src += n;
        dstva += n;
    }
    return 0;
}

// 递归释放页表（内部辅助函数）
static void freewalk(pagetable_t pt) {
    // 遍历页表所有条目
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        
       if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
          // 指向下级页表，递归释放
          freewalk((pagetable_t)PTE2PA(pte));
          pt[i] = 0;//清除页表项
       }else if(pte & PTE_V){
          panic("freewalk:leaf");
       }

    }
    // 释放页表本身占用的页面
    free_page((void*)pt);
}

// 销毁整个页表
void destroy_pagetable(pagetable_t pt) {
    if (pt == 0) return;
    
    // 先解除所有映射并释放物理页
    uvmunmap(pt, 0, (MAXVA / PGSIZE), 1);

    // 递归释放所有页表页面
    freewalk(pt);
}

// 递归打印页表内容（调试用）
void dump_pagetable(pagetable_t pt, int level) {
    if (pt == 0) return;
    
    static const char* level_names[] = {"L2", "L1", "L0"};
    if (level < 0 || level > 2) return;
    
    printf("=== %s Page Table at 0x%p ===\n", level_names[level], pt);
    
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        
        if (pte & PTE_V) {
            printf("  [%d] PTE: 0x%lx -> ", i, pte);
            
            if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
                // 中间页表
                printf("Next level page table at 0x%lx\n", PTE2PA(pte));
                if (level < 2) {
                    dump_pagetable((pagetable_t)PTE2PA(pte), level + 1);
                }
            } else {
                // 叶子映射
                printf("PA: 0x%lx | Perm: ", PTE2PA(pte));
                if (pte & PTE_R) printf("R");
                if (pte & PTE_W) printf("W"); 
                if (pte & PTE_X) printf("X");
                if (pte & PTE_U) printf("U");
                printf("\n");
            }
        }
    }
    
    if (level == 0) {
        printf("=== End of Page Table Dump ===\n");
    }
}

// 为进程创建首批用户页并拷贝入口程序内容
void uvmfirst(pagetable_t pagetable, const uint8 *src, uint64 sz)
{
    if(sz == 0)
        panic("uvmfirst: empty init code");

    uint64 alloc_sz = PGROUNDUP(sz);
    for(uint64 off = 0; off < alloc_sz; off += PGSIZE) {
        uint64 pa = (uint64)alloc_page();
        if(pa == 0)
            panic("uvmfirst: alloc_page");

        memset((void *)pa, 0, PGSIZE);
        if(map_page(pagetable, off, pa, PTE_R | PTE_W | PTE_X | PTE_U) < 0)
            panic("uvmfirst: map_page");

        uint64 chunk = sz > off ? sz - off : 0;
        if(chunk > PGSIZE)
            chunk = PGSIZE;
        if(chunk > 0)
            memmove((void *)pa, src + off, chunk);
    }
}

// uvmalloc_perm: 扩大用户地址空间 [oldsz, newsz)，按权限逐页分配并清零。
uint64 uvmalloc_perm(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int perm)
{
    if(newsz < oldsz)
        return oldsz;

    uint64 start = PGROUNDUP(oldsz);
    uint64 end = PGROUNDUP(newsz);

    if(end < start)
        return oldsz;

    perm |= PTE_U; // always require user access for user mappings

    for(uint64 a = start; a < end; a += PGSIZE) {
        void *mem = alloc_page();
        if(mem == 0) {
            if(a > start)
                uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if(map_page(pagetable, a, (uint64)mem, perm) < 0) {
            free_page(mem);
            if(a > start)
                uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }

    return newsz;
}

uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    return uvmalloc_perm(pagetable, oldsz, newsz, PTE_R | PTE_W | PTE_U);
}

// uvmdealloc: 收缩用户地址空间至 newsz，释放多余页面。
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if(newsz >= oldsz)
        return oldsz;

    uint64 start = PGROUNDUP(newsz);
    uint64 end = PGROUNDUP(oldsz);

    if(end > start)
        uvmunmap(pagetable, start, (end - start) / PGSIZE, 1);

    return newsz;
}

// 释放用户页表中 [0, sz) 范围内的内存映射
void uvmfree(pagetable_t pagetable, uint64 sz)
{
    if(sz == 0)
        return;
    uint64 npages = PGROUNDUP(sz) / PGSIZE;
    uvmunmap(pagetable, 0, npages, 1);
}