#pragma once

#include "types.h"
#include "sleeplock.h"
#include "fs.h"

// 缓存块状态标志
#define B_VALID 0x2   // 缓存内容有效
#define B_DIRTY 0x4   // 缓存已被修改，需要写回

// 缓存块结构：复用 xv6 的双向链表组织形式
struct buf {
    int flags;                 // 状态标志位（B_VALID/B_DIRTY）
    uint dev;                  // 设备号
    uint blockno;              // 块号
    struct sleeplock lock;     // 保护 data[] 内容
    uint refcnt;               // 引用计数
    struct buf *next;          // LRU 双向链表（靠近头部表示最近使用）
    struct buf *prev;
    uchar data[BLOCK_SIZE];    // 实际缓存数据，大小等于磁盘块大小
};

void bcache_init(void);
struct buf *bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);
