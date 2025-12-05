#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "string.h"
#include "spinlock.h"
#include "buf.h"
#include "virtio.h"
#include "kalloc.h"
#include "riscv.h"

// VirtIO 磁盘驱动：通过 virtio-mmio 接口与 QEMU 提供的块设备通信。
// 实现思路与 xv6 一致，但精简为忙等轮询模式，便于在尚未接入中断控制器时使用。

#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

struct {
    struct virtq_desc *desc;          // DMA 描述符表
    struct virtq_avail *avail;        // 可用环，记录待执行的请求
    struct virtq_used *used;          // 已用环，记录设备完成的请求
    struct spinlock lock;             // 保护上述结构及 free/used_idx
    char free[VIRTIO_RING_NUM];       // 描述符空闲标记
    uint16 used_idx;                  // 我们已处理到的 used->idx 下标
    struct {
        struct buf *b;                // 回写时需要唤醒的缓存块
        char status;                  // 设备写入的完成状态：0=成功
    } info[VIRTIO_RING_NUM];
    struct virtio_blk_req ops[VIRTIO_RING_NUM]; // 请求头，每个描述符独占一个
} disk;

static int alloc_desc(void)
{
    for(int i = 0; i < VIRTIO_RING_NUM; i++){
        if(disk.free[i]){
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void free_desc(int i)
{
    if(i < 0 || i >= VIRTIO_RING_NUM)
        panic("virtio: free_desc index");
    if(disk.free[i])
        panic("virtio: free_desc double");

    disk.desc[i].addr = 0;
    disk.desc[i].len = 0;
    disk.desc[i].flags = 0;
    disk.desc[i].next = 0;
    disk.free[i] = 1;
}

static void free_chain(int i)
{
    while(1){
        int flag = disk.desc[i].flags;
        int next = disk.desc[i].next;
        free_desc(i);
        if(flag & VRING_DESC_F_NEXT)
            i = next;
        else
            break;
    }
}

static int alloc3_desc(int *idx)
{
    for(int i = 0; i < 3; i++){
        idx[i] = alloc_desc();
        if(idx[i] < 0){
            for(int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

static void wait_for_completion(int head_idx)
{
    while(disk.used_idx == disk.used->idx)
        __sync_synchronize();

    __sync_synchronize();

    int id = disk.used->ring[disk.used_idx % VIRTIO_RING_NUM].id;
    if(id != head_idx)
        panic("virtio: unexpected completion");

    if(disk.info[id].status != 0)
        panic("virtio: io error");

    disk.info[id].b = 0;
    disk.used_idx += 1;
}

void virtio_disk_init(void)
{
    uint32 status = 0;

    initlock(&disk.lock, "virtio_disk");

    uint32 magic = *R(VIRTIO_MMIO_MAGIC_VALUE);
    uint32 version = *R(VIRTIO_MMIO_VERSION);
    uint32 device_id = *R(VIRTIO_MMIO_DEVICE_ID);
    uint32 vendor = *R(VIRTIO_MMIO_VENDOR_ID);

    if(magic != 0x74726976 ||
        (version!=1 && version!=2)  ||
       device_id != 2 ||
       vendor != 0x554d4551){
        printf("virtio: probe failed (magic=0x%x version=%u device=%u vendor=0x%x)\n",
               magic, version, device_id, vendor);
        panic("virtio: device not found");
    }

    *R(VIRTIO_MMIO_STATUS) = status;

    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;

    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_RO);
    features &= ~(1 << VIRTIO_BLK_F_SCSI);
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
    features &= ~(1 << VIRTIO_BLK_F_MQ);
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    status = *R(VIRTIO_MMIO_STATUS);
    if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio: FEATURES_OK not set");

    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

    if(*R(VIRTIO_MMIO_QUEUE_READY))
        panic("virtio: queue in use");

    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if(max == 0)
        panic("virtio: no queue 0");
    if(max < VIRTIO_RING_NUM)
        panic("virtio: queue too short");

    disk.desc = (struct virtq_desc *)alloc_page();
    disk.avail = (struct virtq_avail *)alloc_page();
    disk.used = (struct virtq_used *)alloc_page();
    if(!disk.desc || !disk.avail || !disk.used)
        panic("virtio: alloc ring");
    memset(disk.desc, 0, PGSIZE);
    memset(disk.avail, 0, PGSIZE);
    memset(disk.used, 0, PGSIZE);

    *R(VIRTIO_MMIO_QUEUE_NUM) = VIRTIO_RING_NUM;

    uint64 desc_pa = (uint64)disk.desc;
    uint64 avail_pa = (uint64)disk.avail;
    uint64 used_pa = (uint64)disk.used;
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint32)desc_pa;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint32)(desc_pa >> 32);
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint32)avail_pa;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint32)(avail_pa >> 32);
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint32)used_pa;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint32)(used_pa >> 32);

    *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

    for(int i = 0; i < VIRTIO_RING_NUM; i++)
        disk.free[i] = 1;
    disk.used_idx = 0;

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
}

void virtio_disk_rw(struct buf *b, int write)
{
    uint64 sector = ((uint64)b->blockno) * (BLOCK_SIZE / 512);

    acquire(&disk.lock);

    int idx[3];
    while(alloc3_desc(idx) < 0)
        ;

    struct virtio_blk_req *req = &disk.ops[idx[0]];
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;

    disk.desc[idx[0]].addr = (uint64)req;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    disk.desc[idx[1]].addr = (uint64)b->data;
    disk.desc[idx[1]].len = BLOCK_SIZE;
    disk.desc[idx[1]].flags = write ? 0 : VRING_DESC_F_WRITE;
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    disk.info[idx[0]].status = 0xff;
    disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;

    disk.info[idx[0]].b = b;

    disk.avail->ring[disk.avail->idx % VIRTIO_RING_NUM] = idx[0];
    __sync_synchronize();
    disk.avail->idx += 1;
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    wait_for_completion(idx[0]);

    free_chain(idx[0]);

    release(&disk.lock);
}
