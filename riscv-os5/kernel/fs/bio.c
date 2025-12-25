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
// 哈希桶数量：选取质数以降低冲突概率，简单场景下 37 足够覆盖所有缓冲块。
#define BUF_HASH_SIZE 37

struct {
    struct spinlock lock;      // 保护缓存链表以及引用计数。
    struct buf buf[NBUF];      // 实际的缓存块数组，每个元素维护元数据和数据区。
    struct buf head;           // LRU 双向链表的哨兵节点。
    struct buf *hash[BUF_HASH_SIZE]; // 哈希桶数组，每个桶串联同一哈希值的缓存块
} bcache;

static struct buf *bget(uint dev, uint blockno);
static void disk_rw(struct buf *b, int write);

static inline uint buf_hash(uint dev, uint blockno)
{
    // 对 (dev, blockno) 做简单异或哈希，快速定位缓存桶。
    return (dev ^ blockno) % BUF_HASH_SIZE;
}

static void hash_insert(struct buf *b)
{
    // 将缓存块挂入对应哈希桶，便于后续 O(1) 命中查找。
    uint idx = buf_hash(b->dev, b->blockno);
    b->hash_next = bcache.hash[idx];
    bcache.hash[idx] = b;
}

static void hash_remove(struct buf *b)
{
    // 从哈希桶中摘除缓存块，复用缓存块前需要先清理旧映射。
    uint idx = buf_hash(b->dev, b->blockno);
    struct buf **pp = &bcache.hash[idx];
    while(*pp){
        if(*pp == b){
            *pp = b->hash_next;
            b->hash_next = 0;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

// 初始化缓冲池：
//   - 初始化自旋锁；
//   - 将 head 构建成空链表；
//   - 将所有 buf 节点插入 head 后方并初始化睡眠锁。
// 链表头部代表最近使用，尾部代表最久未使用，便于实现 LRU 淘汰。
void bcache_init(void)
{
    struct buf *b;
    int i;

    initlock(&bcache.lock, "bcache");
    for(i = 0; i < BUF_HASH_SIZE; i++)
        bcache.hash[i] = 0;    // 初始化哈希桶头指针，确保启动时为空
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;

    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;

        b->refcnt = 0;
        b->flags = 0;
        b->hash_next = 0;   // 初始状态下未挂入任何哈希桶
        b->dev = 0;          // 清零设备号，便于后续调试时辨识
        b->blockno = 0;      // 清零块号，防止意外读取旧值
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
//   1) 通过哈希桶快速定位是否命中已有缓存块；
//   2) 若未命中，则从 LRU 链表尾部挑选 refcnt==0 的空闲块并复用；
//   3) 若仍无法命中或复用，说明缓存容量不足，直接 panic（开发期便于发现问题）。
static struct buf *bget(uint dev, uint blockno)
{
    struct buf *b;
    uint idx;

    acquire(&bcache.lock);

    idx = buf_hash(dev, blockno);

    // 先按哈希桶扫描，命中后即可避免遍历整个 LRU 链表。
    for(b = bcache.hash[idx]; b != 0; b = b->hash_next){
        if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 未命中：从链表尾部起寻找 refcnt==0 的空闲块进行复用。
    for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
        if(b->refcnt == 0){
            hash_remove(b);      // 复用前先从旧桶摘除，避免残留映射
            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            hash_insert(b);      // 将新映射挂入对应哈希桶
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

// clear_cache: 清空块缓存，仅供测试使用，会丢失未写磁盘的数据
void clear_cache(void) {
    acquire(&bcache.lock);
    for(int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        b->flags &= ~(B_VALID | B_DIRTY);
        b->refcnt = 0;
    }
    release(&bcache.lock);
}
