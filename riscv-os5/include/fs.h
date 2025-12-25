#pragma once

#include "types.h"
#include "sleeplock.h"

// ======================== 文件系统核心描述 ========================
// 本文件描述块设备的布局、磁盘索引节点格式以及内存索引节点缓存结构。

// FS_MAGIC: 用于超级块校验的魔数，加载超级块时若不匹配会拒绝挂载。
#define FS_MAGIC        0x20241031u
// BLOCK_SIZE: 单个磁盘块大小（字节）。所有块相关计算均依赖该常量。
#define BLOCK_SIZE      4096
// BLOCK_SIZE_LOG2: 上述块大小的以 2 为底的对数，用于位移运算优化。
#define BLOCK_SIZE_LOG2 12
// FS_TOTAL_BLOCKS: 文件系统总块数，对应 virtio 磁盘映像中的块容量。
#define FS_TOTAL_BLOCKS 2000

// 超级块所在的块号及数量，目前固定为单块超级块。
#define SUPERBLOCK_BLOCKNO 1
#define SUPERBLOCK_NUM     1
// 日志区起始块和大小。对应 kernel/fs/log.c 的物理 redo 日志，用于保证事务一致性。
#define LOG_START          (SUPERBLOCK_BLOCKNO + SUPERBLOCK_NUM)
#define LOG_SIZE           30
// inode 表在磁盘上的起始块号和占用块数。
#define INODE_START        (LOG_START + LOG_SIZE)
#define INODE_BLOCKS       13
// 位图区起始块和大小，用于管理数据块分配情况。
#define BMAP_START         (INODE_START + INODE_BLOCKS)
#define BMAP_BLOCKS        1
// 数据区起始块号，所有文件内容 / 目录数据均存放于此。
#define DATA_START         (BMAP_START + BMAP_BLOCKS)

// 根设备号与根 inode 号，文件系统初始化时会据此创建根目录。
#define ROOTDEV 1
#define ROOTINO 1

// NDIRECT: inode 中直接块指针数量。
// NINDIRECT: 单块可存放的间接块指针数量。
// MAX_FILE_BLOCKS: 单个文件理论上最多引用的块数（直接 + 间接）。
// MAX_FILE_SIZE: 以字节为单位的最大文件长度。
#define NDIRECT    12
#define NINDIRECT  (BLOCK_SIZE / sizeof(uint32))
#define NDOUBLE    (NINDIRECT * NINDIRECT)
#define MAX_FILE_BLOCKS (NDIRECT + NINDIRECT + NDOUBLE)
#define MAX_FILE_SIZE   ((uint64)MAX_FILE_BLOCKS * BLOCK_SIZE)

// BPB: bitmap 中一个磁盘块能描述的数据块数量；每比特对应一个数据块。
// IPB: 单个磁盘块能容纳的 dinode 数量。
#define BPB (BLOCK_SIZE * 8)
#define IPB (BLOCK_SIZE / sizeof(struct dinode))

// IBLOCK/BBLOCK: 将 inode 号或数据块号映射到对应的磁盘块。
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

// 磁盘对象的类型定义，用于 dinode->type。
#define T_DIR   1
#define T_FILE  2
#define T_DEV   3
#define T_SYMLINK 4

// DIRSIZ: 目录项中文件名的最大长度（不含结尾 NULL）。
// NINODE: 内存中可缓存的 inode 数量。
// MAXPATH: 用户态路径缓冲区最大长度。
#define DIRSIZ 14
#define NINODE 50
#define MAXPATH 128

// 磁盘上的索引节点结构。该结构直接写入磁盘，因此需要保持紧凑，
// 并与 kernel/fs/fs.c 中的读写逻辑完全一致。
struct dinode {
    short type;                   // 类型：0 表示空闲；T_DIR/T_FILE/T_DEV/T_SYMLINK 表示有效。
    short major;                  // 设备主编号，仅对 T_DEV 类型有效。
    short minor;                  // 设备次编号，尚未使用但保留接口一致性。
    short nlink;                  // 指向该 inode 的目录项数量（硬链接计数）。
    uint32 size;                  // 文件当前字节长度。
    uint32 addrs[NDIRECT + 2];    // 数据块指针：直接块 + 一级间接块 + 二级间接块。
};

// 超级块记录整体文件系统元数据，fs_init 会将其读入 sb 全局变量。
struct superblock {
    uint32 magic;                 // 魔数：校验该磁盘块是否为预期的文件系统。
    uint32 size;                  // 文件系统包含的磁盘块总数。
    uint32 nblocks;               // 数据区块数量（不含元数据区）。
    uint32 ninodes;               // inode 总数，用于越界检查。
    uint32 nlog;                  // 日志块数量，目前始终等于 LOG_SIZE。
    uint32 logstart;              // 日志区起始块号。
    uint32 inodestart;            // inode 表起始块号。
    uint32 bmapstart;             // 位图区起始块号。
};

// 目录项：将文件名映射到 inode 号。未使用的目录项 inum 为 0。
struct dirent {
    uint16 inum;                  // 目标 inode 号，为 0 表示目录槽位空闲。
    char name[DIRSIZ];            // 固定长度文件名，若不足 DIRSIZ 以 \0 填充。
};

// 内存中的索引节点。包含磁盘 dinode 的镜像字段与缓存控制信息。
struct inode {
    uint32 dev;                   // 所属设备号（便于支持多设备）。
    uint32 inum;                  // inode 号，用于在磁盘上定位 dinode。
    int ref;                      // 引用计数：>0 表示被进程或其他数据结构持有。
    struct sleeplock lock;        // 保护 inode 元数据/数据的睡眠锁。
    int valid;                    // 标记缓存内容是否已从磁盘加载。

    short type;                   // 文件类型，语义与 dinode->type 对齐。
    short major;                  // 设备主编号。
    short minor;                  // 设备次编号。
    short nlink;                  // 硬链接计数。
    uint32 size;                  // 文件当前字节长度。
    uint32 addrs[NDIRECT + 2];    // 数据块索引缓存，写回时同步到 dinode。
};

// ========== fs.c 中实现的核心接口 ==========
// 初始化超级块、inode 缓存并创建根目录。
void fs_init(void);
// 返回超级块的只读指针，便于查询布局信息。
const struct superblock *fs_superblock(void);

// 获取/管理 inode 缓存条目的工具函数。
struct inode *iget(uint32 dev, uint32 inum);   // 根据设备和 inode 号获取缓存项，引用计数 +1。
struct inode *idup(struct inode *ip);          // 简单增加引用计数。
void ilock(struct inode *ip);                  // 加锁并懒加载磁盘数据到内存 inode。
void iunlock(struct inode *ip);                // 释放 inode 的睡眠锁。
void iput(struct inode *ip);                   // 减少引用，必要时回收 inode。
void iunlockput(struct inode *ip);             // 将 iunlock 与 iput 组合。
void itrunc(struct inode *ip);                 // 回收 inode 关联的数据块并将长度清零。
struct inode *ialloc(uint32 dev, short type);  // 在磁盘上分配新 inode，并返回内存镜像。
void iupdate(struct inode *ip);                // 将内存 inode 的修改写回磁盘。

// readi/writei: 以 inode 为中心的数据传输接口，可处理用户态和内核态缓冲区。
int readi(struct inode *ip, int user_dst, uint64 dst, uint32 off, uint32 n);
int writei(struct inode *ip, int user_src, uint64 src, uint32 off, uint32 n);

// 目录遍历与路径解析相关接口。
struct inode *dirlookup(struct inode *dp, char *name, uint32 *poff); // 在目录中查找子项。
int dirlink(struct inode *dp, char *name, uint32 inum);               // 在目录中插入新目录项。
struct inode *namei(char *path);                                     // 解析完整路径。
struct inode *nameiparent(char *path, char *name);                   // 定位父目录并返回最后一段名称。

// 便捷宏：将块号转换为字节偏移，供 readi/writei 等处使用。
#define BLOCK_OFFSET(b) ((uint64)(b) << BLOCK_SIZE_LOG2)
