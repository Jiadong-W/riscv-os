#pragma once

#include "types.h"

// ======================= VirtIO MMIO 寄存器定义 =======================
// 这些偏移对应 QEMU virtio-mmio 实现的寄存器布局，详见 virtio 规范 5.2 节。
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW     0x090
#define VIRTIO_MMIO_DRIVER_DESC_HIGH    0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW     0x0a0
#define VIRTIO_MMIO_DEVICE_DESC_HIGH    0x0a4

// 状态寄存器标志位
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
#define VIRTIO_CONFIG_S_DRIVER          2
#define VIRTIO_CONFIG_S_DRIVER_OK       4
#define VIRTIO_CONFIG_S_FEATURES_OK     8

// 设备功能位（摘自 virtio-blk）
#define VIRTIO_BLK_F_RO              5
#define VIRTIO_BLK_F_SCSI            7
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX     29

// 简化：一次传输最多占用 8 个描述符（需为 2 的幂）
#define VIRTIO_RING_NUM 8

// 描述符结构体定义
struct virtq_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
};
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

// 可用环：内核写入待处理的描述符编号序列
struct virtq_avail {
    uint16 flags;
    uint16 idx;
    uint16 ring[VIRTIO_RING_NUM];
    uint16 unused;
};

// 已用环：设备写回已完成的描述符编号
struct virtq_used_elem {
    uint32 id;
    uint32 len;
};

struct virtq_used {
    uint16 flags;
    uint16 idx;
    struct virtq_used_elem ring[VIRTIO_RING_NUM];
};

// 块设备请求头（第一个描述符承载）
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

struct virtio_blk_req {
    uint32 type;
    uint32 reserved;
    uint64 sector;
};

struct buf;
void virtio_disk_init(void);
void virtio_disk_rw(struct buf *b, int write);
