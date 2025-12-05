#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "printf.h"
#include "string.h"
#include "virtio.h"

// buffer cache (bio.c) 为文件系统提供按块缓存，负责低层块设备读写调度。
// 通过 virtio_disk.c 驱动与 QEMU 虚拟磁盘交互，实现真实的块设备读写。

// 缓存块数量：单核环境下 32 足以支撑简单文件操作。
#define NBUF 32

struct {
    struct spinlock lock;      // 保护缓存链表以及引用计数。
    struct buf buf[NBUF];      // 实际的缓存块数组，每个元素维护元数据和数据区。
    struct buf head;           // LRU 双向链表的哨兵节点。
} bcache;

static struct buf *bget(uint dev, uint blockno);
static void disk_rw(struct buf *b, int write);

// 初始化缓冲池：
//   - 初始化自旋锁；
//   - 将 head 构建成空链表；
//   - 将所有 buf 节点插入 head 后方并初始化睡眠锁。
// 链表头部代表最近使用，尾部代表最久未使用，便于实现 LRU 淘汰。
void bcache_init(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;

    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;

        b->refcnt = 0;
        b->flags = 0;
        initsleeplock(&b->lock, "buffer");
    }
}

// bread 返回 dev:blockno 对应的缓存块。
// 若缓存中已有，会直接命中；否则通过 bget 选取 LRU 空闲块并从磁盘读入。
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b = bget(dev, blockno);
    if(!(b->flags & B_VALID)){
        disk_rw(b, 0);           // 触发一次实际磁盘读取并填充 buf->data。
        b->flags |= B_VALID;
    }
    return b;
}

// bwrite 将缓存块写回磁盘，并保留 B_DIRTY 标记清空的副作用。
// 调用者必须已经持有睡眠锁，否则写回期间可能出现数据竞争。
void bwrite(struct buf *b)
{
    if(!holdingsleep(&b->lock))
        panic("bwrite: not holding lock");

    b->flags |= B_DIRTY;
    disk_rw(b, 1);
}

// brelse 在调用者完成对缓存块的访问后释放睡眠锁，并尝试将其移回 LRU 头部。
// refcnt 递减为 0 时，该块代表“空闲可复用”，会被移动到链表头方便下次命中。
void brelse(struct buf *b)
{
    if(!holdingsleep(&b->lock))
        panic("brelse: not holding lock");

    releasesleep(&b->lock);

    acquire(&bcache.lock);
    b->refcnt--;
    if(b->refcnt == 0){
        // 先从当前位置摘除，再插入到头部实现最近最少使用策略。
        b->next->prev = b->prev;
        b->prev->next = b->next;

        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    release(&bcache.lock);
}

// bpin/bunpin 在日志或其他场景需要阻止缓存被 LRU 淘汰时使用。
void bpin(struct buf *b)
{
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}

void bunpin(struct buf *b)
{
    acquire(&bcache.lock);
    b->refcnt--;
    release(&bcache.lock);
}

// bget 是缓存查找与分配的核心：
//   1) 按 LRU 链表从头到尾遍历寻找命中，命中后直接加引用并上睡眠锁；
//   2) 若未命中，则从尾部寻找 refcnt==0 的空闲块，将其复用；
//   3) 若链表无可用块，说明缓存容量不足，直接 panic（开发期便于发现问题）。
static struct buf *bget(uint dev, uint blockno)
{
    struct buf *b;

    acquire(&bcache.lock);

    // 查找命中的缓存块。
    for(b = bcache.head.next; b != &bcache.head; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 未命中：从链表尾部起寻找空闲块进行复用。
    for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
        if(b->refcnt == 0){
            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    panic("bget: no buffers");
    return 0; // 不会执行到此，避免编译器警告。
}

// disk_rw 使用 virtio 磁盘驱动与 QEMU 模拟块设备交互。
//  - read: 将磁盘块拷贝进 buf->data；
//  - write: 将 buf->data 写回磁盘，并清除脏页标记。
static void disk_rw(struct buf *b, int write)
{
    virtio_disk_rw(b, write);
    if(write)
        b->flags &= ~B_DIRTY;
}
