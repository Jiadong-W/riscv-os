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

// 定义 VirtIO MMIO 寄存器访问宏：将寄存器偏移映射到 VIRTIO0 基地址
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// 全局磁盘数据结构
struct {
    struct virtq_desc *desc;          // DMA 描述符表：定义数据传输的缓冲区
    struct virtq_avail *avail;        // 可用环：驱动向设备提交请求的队列
    struct virtq_used *used;          // 已用环：设备完成请求后的返回队列
    struct spinlock lock;             // 保护上述结构及 free/used_idx 的自旋锁
    char free[VIRTIO_RING_NUM];       // 描述符空闲标记：1=空闲，0=使用中
    uint16 used_idx;                  // 我们已处理到的 used->idx 下标（驱动视角）
    struct {
        struct buf *b;                // 回写时需要唤醒的缓存块指针
        char status;                  // 设备写入的完成状态：0=成功，其他=错误
    } info[VIRTIO_RING_NUM];          // 每个描述符的附加信息
    struct virtio_blk_req ops[VIRTIO_RING_NUM]; // 请求头数组，每个描述符独占一个
} disk;

// 分配一个空闲描述符
static int alloc_desc(void)
{
    // 线性扫描查找第一个空闲描述符
    for(int i = 0; i < VIRTIO_RING_NUM; i++){
        if(disk.free[i]){
            disk.free[i] = 0;  // 标记为使用中
            return i;          // 返回描述符索引
        }
    }
    return -1;  // 无空闲描述符
}

// 释放单个描述符
static void free_desc(int i)
{
    // 参数检查
    if(i < 0 || i >= VIRTIO_RING_NUM)
        panic("virtio: free_desc index");
    if(disk.free[i])
        panic("virtio: free_desc double");

    // 清空描述符内容
    disk.desc[i].addr = 0;
    disk.desc[i].len = 0;
    disk.desc[i].flags = 0;
    disk.desc[i].next = 0;
    disk.free[i] = 1;  // 标记为空闲
}

// 释放描述符链（处理链式描述符）
static void free_chain(int i)
{
    while(1){
        int flag = disk.desc[i].flags;  // 获取当前描述符标志
        int next = disk.desc[i].next;   // 获取下一个描述符索引
        free_desc(i);                   // 释放当前描述符
        if(flag & VRING_DESC_F_NEXT)    // 检查是否有后续描述符
            i = next;                   // 继续释放下一个
        else
            break;                      // 链结束
    }
}

// 分配3个连续描述符（用于一个完整的块请求）
static int alloc3_desc(int *idx)
{
    for(int i = 0; i < 3; i++){
        idx[i] = alloc_desc();          // 分配描述符
        if(idx[i] < 0){
            // 分配失败，回滚已分配的描述符
            for(int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
    }
    return 0;  // 成功分配3个描述符
}

// 等待请求完成（轮询模式）
static void wait_for_completion(int head_idx)
{
    // 忙等待：直到设备处理了我们的请求
    while(disk.used_idx == disk.used->idx)
        __sync_synchronize();  // 内存屏障，确保读取最新的 used->idx
    
    __sync_synchronize();      // 再次内存屏障

    // 获取已完成的请求ID
    int id = disk.used->ring[disk.used_idx % VIRTIO_RING_NUM].id;
    if(id != head_idx)
        panic("virtio: unexpected completion");  // 完成顺序错误

    // 检查操作状态
    if(disk.info[id].status != 0)
        panic("virtio: io error");  // I/O操作失败

    // 清理完成信息
    disk.info[id].b = 0;
    disk.used_idx += 1;  // 更新已处理索引
}

// VirtIO 磁盘初始化
void virtio_disk_init(void)
{
    uint32 status = 0;

    initlock(&disk.lock, "virtio_disk");  // 初始化自旋锁

    // 读取设备标识寄存器
    uint32 magic = *R(VIRTIO_MMIO_MAGIC_VALUE);
    uint32 version = *R(VIRTIO_MMIO_VERSION);
    uint32 device_id = *R(VIRTIO_MMIO_DEVICE_ID);
    uint32 vendor = *R(VIRTIO_MMIO_VENDOR_ID);

    // 验证设备身份（VirtIO 块设备）
    if(magic != 0x74726976 ||           // "virt" 魔数
        (version!=1 && version!=2)  ||  // 版本检查
       device_id != 2 ||                // 设备类型：2=块设备
       vendor != 0x554d4551){           // 厂商ID：QEMU
        printf("virtio: probe failed (magic=0x%x version=%u device=%u vendor=0x%x)\n",
               magic, version, device_id, vendor);
        panic("virtio: device not found");
    }

    // 重置设备
    *R(VIRTIO_MMIO_STATUS) = status;

    // 设备发现流程
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;  // 确认设备存在
    *R(VIRTIO_MMIO_STATUS) = status;

    status |= VIRTIO_CONFIG_S_DRIVER;       // 驱动就绪
    *R(VIRTIO_MMIO_STATUS) = status;

    // 协商设备特性
    uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    // 禁用不需要的特性
    features &= ~(1 << VIRTIO_BLK_F_RO);           // 只读
    features &= ~(1 << VIRTIO_BLK_F_SCSI);         // SCSI命令
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);   // 写回缓存配置
    features &= ~(1 << VIRTIO_BLK_F_MQ);           // 多队列
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);       // 任意布局
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);   // 事件索引
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC); // 间接描述符
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;    // 设置驱动支持的特性

    status |= VIRTIO_CONFIG_S_FEATURES_OK;         // 特性协商完成
    *R(VIRTIO_MMIO_STATUS) = status;

    // 验证特性协商结果
    status = *R(VIRTIO_MMIO_STATUS);
    if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio: FEATURES_OK not set");

    // 配置队列0
    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;  // 选择队列0

    if(*R(VIRTIO_MMIO_QUEUE_READY))
        panic("virtio: queue in use");  // 队列已就绪，不应发生

    // 检查队列大小
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if(max == 0)
        panic("virtio: no queue 0");
    if(max < VIRTIO_RING_NUM)
        panic("virtio: queue too short");

    // 分配虚拟队列内存（DMA区域）
    disk.desc = (struct virtq_desc *)alloc_page();   // 描述符表
    disk.avail = (struct virtq_avail *)alloc_page(); // 可用环
    disk.used = (struct virtq_used *)alloc_page();   // 已用环
    if(!disk.desc || !disk.avail || !disk.used)
        panic("virtio: alloc ring");
    
    // 清零分配的内存
    memset(disk.desc, 0, PGSIZE);
    memset(disk.avail, 0, PGSIZE);
    memset(disk.used, 0, PGSIZE);

    // 设置队列大小
    *R(VIRTIO_MMIO_QUEUE_NUM) = VIRTIO_RING_NUM;

    // 配置DMA地址（将虚拟队列地址告知设备）
    uint64 desc_pa = (uint64)disk.desc;
    uint64 avail_pa = (uint64)disk.avail;
    uint64 used_pa = (uint64)disk.used;
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint32)desc_pa;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint32)(desc_pa >> 32);
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint32)avail_pa;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint32)(avail_pa >> 32);
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint32)used_pa;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint32)(used_pa >> 32);

    // 激活队列
    *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

    // 初始化描述符状态
    for(int i = 0; i < VIRTIO_RING_NUM; i++)
        disk.free[i] = 1;  // 所有描述符初始为空闲
    disk.used_idx = 0;     // 已用环处理索引初始为0

    // 驱动完全就绪
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
}

// VirtIO 磁盘读写操作
void virtio_disk_rw(struct buf *b, int write)
{
    // 计算磁盘扇区号（块设备以512字节扇区为单位）
    uint64 sector = ((uint64)b->blockno) * (BLOCK_SIZE / 512);

    acquire(&disk.lock);  // 获取磁盘锁
    
    // 分配3个描述符：请求头 + 数据缓冲区 + 状态区
    int idx[3];
    while(alloc3_desc(idx) < 0)
        ;  // 忙等待直到分配到描述符

    // 设置请求头（描述符0）
    struct virtio_blk_req *req = &disk.ops[idx[0]];
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;  // 操作类型
    req->reserved = 0;
    req->sector = sector;  // 目标扇区

    // 配置描述符0：指向请求头
    disk.desc[idx[0]].addr = (uint64)req;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;  // 有下一个描述符
    disk.desc[idx[0]].next = idx[1];              // 下一个是数据描述符

    // 配置描述符1：指向数据缓冲区
    disk.desc[idx[1]].addr = (uint64)b->data;
    disk.desc[idx[1]].len = BLOCK_SIZE;
    // 设置标志：写操作不需要WRITE（设备→内存），读操作需要WRITE（内存←设备）
    disk.desc[idx[1]].flags = write ? 0 : VRING_DESC_F_WRITE;
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;  // 有下一个描述符
    disk.desc[idx[1]].next = idx[2];               // 下一个是状态描述符

    // 配置描述符2：指向状态字节
    disk.info[idx[0]].status = 0xff;  // 初始状态（非0表示未完成）
    disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;  // 设备写入状态
    disk.desc[idx[2]].next = 0;                    // 链结束

    // 记录缓冲区信息（用于完成时唤醒）
    disk.info[idx[0]].b = b;

    // 将请求提交到可用环
    disk.avail->ring[disk.avail->idx % VIRTIO_RING_NUM] = idx[0];  // 放入可用环
    __sync_synchronize();  // 内存屏障，确保描述符设置对设备可见
    disk.avail->idx += 1;  // 更新可用环索引
    __sync_synchronize();  // 再次内存屏障
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;  // 通知设备有新请求

    // 等待请求完成
    wait_for_completion(idx[0]);

    // 释放描述符链
    free_chain(idx[0]);

    release(&disk.lock);  // 释放磁盘锁
}