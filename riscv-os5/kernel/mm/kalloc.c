#include "memlayout.h"
#include "printf.h"
#include "types.h"
#include "riscv.h"
#include "string.h"
#include "kalloc.h"

extern char end[];

#define NPAGES ((PHYSTOP - (uint64)KERNBASE) / PGSIZE)

// 位示图相关定义
#define BITS_PER_WORD (sizeof(uint64) * 8)
#define BITMAP_WORDS ((NPAGES + BITS_PER_WORD - 1) / BITS_PER_WORD) //向上取整

static uint64 bitmap[BITMAP_WORDS];  // 0=空闲, 1=已分配
static int free_pages_count = 0;     // 空闲页面计数
static int refcount[NPAGES];         // 每页的引用计数

// 获取物理页对应的下标
static inline int page_index(void *page) {
    return ((char*)page - (char*)KERNBASE) / PGSIZE;
}

// 获取下标对应的物理页地址
static inline void* index_to_page(int index) {
    return (char*)KERNBASE + index * PGSIZE;
}

// 位示图操作函数
static inline void bitmap_set(int index) {
    bitmap[index / BITS_PER_WORD] |= (1UL << (index % BITS_PER_WORD));
}

static inline void bitmap_clear(int index) {
    bitmap[index / BITS_PER_WORD] &= ~(1UL << (index % BITS_PER_WORD));
}

static inline int bitmap_test(int index) {
    return (bitmap[index / BITS_PER_WORD] >> (index % BITS_PER_WORD)) & 1;
}

// 在位示图中查找第一个空闲页
static int find_first_free_page(void) {
    for (int i = 0; i < BITMAP_WORDS; i++) {
        if (bitmap[i] != ~0UL) {  // 如果这个字不是全1
            for (int j = 0; j < BITS_PER_WORD; j++) {
                int index = i * BITS_PER_WORD + j;
                if (index < NPAGES && !bitmap_test(index)) {
                    return index;
                }
            }
        }
    }
    return -1;
}

// 在位示图中查找连续的n个空闲页
static int find_contiguous_free_pages(int n) {
    int start = 0, count = 0;
    
    for (int i = 0; i < NPAGES; i++) {
        if (!bitmap_test(i)) {
            if (count == 0) start = i;
            count++;
            if (count == n) return start;
        } else {
            count = 0;
        }
    }
    return -1;
}

// 初始化物理内存管理器
void pmm_init(void) {
    // 计算位示图占用的内存大小并标记为已分配
    uint64 bitmap_size = BITMAP_WORDS * sizeof(uint64);
    char *bitmap_start = (char*)PGROUNDUP((uint64)end);
    char *bitmap_end = (char*)PGROUNDUP((uint64)bitmap_start + bitmap_size);
    
    // 初始化位示图：全部标记为空闲
    memset(bitmap, 0, sizeof(bitmap));
    
    // 标记位示图自身占用的页面为已分配
    char *p = bitmap_start;
    while (p < bitmap_end) {
        int idx = page_index(p);
        bitmap_set(idx);
        refcount[idx] = 1;
        p += PGSIZE;
    }
    
    // 标记内核代码区域为已分配（从KERNBASE到end）
    p = (char*)KERNBASE;
    while (p < end) {
        int idx = page_index(p);
        bitmap_set(idx);
        refcount[idx] = 1;
        p += PGSIZE;
    }
    
    // 计算总空闲页数
    free_pages_count = 0;
    for (int i = 0; i < NPAGES; i++) {
        if (!bitmap_test(i)) {
            free_pages_count++;
        }
    }
}

// 分配一个物理页
void* alloc_page(void) {
    if (free_pages_count <= 0) {
        return 0;  // 内存不足
    }
    
    int idx = find_first_free_page();
    if (idx == -1) {
        return 0;  // 没有找到空闲页
    }
    
    void *page = index_to_page(idx);
    bitmap_set(idx);
    free_pages_count--;
    refcount[idx] = 1;
    
    memset(page, 0, PGSIZE);  // 清零页面
    return page;
}

// 释放一个物理页
void free_page(void *page) {
    if (((uint64)page % PGSIZE) != 0 ||
        (char*)page < (char*)KERNBASE ||
        (uint64)page >= PHYSTOP) {
        panic("free_page: invalid page address");
    }
    
    int idx = page_index(page);
    if (!bitmap_test(idx)) {
        panic("free_page: double free detected");
    }

    if (refcount[idx] <= 0) {
        panic("free_page: invalid refcount");
    }

    refcount[idx]--;
    if (refcount[idx] > 0) {
        return;
    }

    bitmap_clear(idx);
    free_pages_count++;
    memset(page, 0, PGSIZE);  // 清零页面（可选，安全考虑）
}

// 引用计数辅助函数
void page_incref(void *page) {
    if (((uint64)page % PGSIZE) != 0 ||
        (char*)page < (char*)KERNBASE ||
        (uint64)page >= PHYSTOP) {
        panic("page_incref: invalid page address");
    }

    int idx = page_index(page);
    if (!bitmap_test(idx)) {
        panic("page_incref: page not allocated");
    }
    refcount[idx]++;
}

int page_decref(void *page) {
    free_page(page);
    return refcount[page_index(page)];
}

int page_refcount(void *page) {
    if (((uint64)page % PGSIZE) != 0 ||
        (char*)page < (char*)KERNBASE ||
        (uint64)page >= PHYSTOP) {
        panic("page_refcount: invalid page address");
    }

    int idx = page_index(page);
    if (idx < 0 || idx >= NPAGES) {
        panic("page_refcount: invalid page");
    }
    return refcount[idx];
}

// 分配连续的n个页面
void* alloc_pages(int n) {
    if (n <= 0) return 0;
    if (n == 1) return alloc_page();

    if (free_pages_count < n) {
        return 0;  // 内存不足
    }

    int start_idx = find_contiguous_free_pages(n);
    if (start_idx == -1) {
        return 0;  // 没有足够的连续页面
    }

    // 标记这些页面为已分配
    for (int i = 0; i < n; i++) {
        int idx = start_idx + i;
        if (bitmap_test(idx)) {
            panic("alloc_pages: page already allocated");
        }
        bitmap_set(idx);
        refcount[idx] = 1;
    }

    free_pages_count -= n;
    void *start_page = index_to_page(start_idx);
    memset(start_page, 0, n * PGSIZE);  // 清零所有页面

    return start_page;
}

// 释放连续的n个页面
void free_pages(void *page, int n) {
    if (n <= 0) return;

    for (int i = 0; i < n; i++) {
        void *cur = (char*)page + i * PGSIZE;
        free_page(cur);
    }
}

// 获取内存统计信息
void pmm_stats(void) {
    printf("Memory stats: total=%d, free=%d, allocated=%d\n", 
           NPAGES, free_pages_count, NPAGES - free_pages_count);
}