#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "fs.h"
#include "log.h"
#include "string.h"
#include "printf.h"
#include "proc.h"
#include "vm.h"
#include "klog.h"

// fs.c 实现文件系统的核心逻辑：超级块初始化、inode 缓存、块分配、目录遍历
// 以及 read/write 等操作。整体设计与 xv6 类似，通过 bio.c 的缓冲层
// 驱动 virtio_disk.c 提供的 VirtIO 块设备，实现对真实虚拟磁盘的访问。为了便于调试，绝大多数异常情况直接 panic，避免静默数据损坏。

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// 为避免符号链接递归解析导致的无限循环，这里设置最大展开深度。
#define MAX_SYMLINK_DEPTH 8

static struct superblock sb;  // 全局超级块缓存，由 fs_init() 读取并常驻内存。

// itable 维护内存中的 inode 缓存：
//   - lock: 保护 inode 表的自旋锁；
//   - inode[]: 内存inode的缓存表，固定大小的缓存槽，每个槽附带睡眠锁。
struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} itable;

static uint32 balloc(uint32 dev);
static void bfree(uint32 dev, uint32 b);
static uint32 bmap(struct inode *ip, uint32 bn);
static int namecmp(const char *s, const char *t);
static char *skipelem(char *path, char *name);
static int build_symlink_path(char *dst, const char *target, const char *rest);
static struct inode *namex_from(struct inode *start, char *path, int nameiparent, char *name, int depth);
static struct inode *namex(char *path, int nameiparent, char *name);

// 向外暴露超级块只读指针，方便系统调用等模块查询布局信息。
const struct superblock *fs_superblock(void)
{
    return &sb;
}

// fs_init 完成以下工作：
//   1) 构造超级块并写入根设备预设布局；
//   2) 初始化内存 inode 缓存和其睡眠锁；
//   3) 打印布局信息以便调试。
void fs_init(void)
{
    memset(&sb, 0, sizeof(sb));
    sb.magic = FS_MAGIC;
    sb.size = FS_TOTAL_BLOCKS;
    sb.nlog = LOG_SIZE;
    sb.logstart = LOG_START;
    sb.inodestart = INODE_START;
    sb.bmapstart = BMAP_START;
    sb.ninodes = INODE_BLOCKS * IPB;
    sb.nblocks = FS_TOTAL_BLOCKS - DATA_START;

    log_init(ROOTDEV, &sb);

    initlock(&itable.lock, "itable");
    for(int i = 0; i < NINODE; i++) {
        itable.inode[i].ref = 0;
        itable.inode[i].valid = 0;
        initsleeplock(&itable.inode[i].lock, "inode");
    }

    klog_info("fs: superblock total=%u data=%u ninodes=%u", sb.size, sb.nblocks, sb.ninodes);
    klog_info("fs: layout super=%d log[%d~%d) inode[%d~%d) bmap=%d",
              SUPERBLOCK_BLOCKNO,
              LOG_START, LOG_START + LOG_SIZE,
              INODE_START, INODE_START + INODE_BLOCKS,
              BMAP_START);
}

// ialloc 遍历磁盘上的 dinode，寻找 type==0 的槽位，将其清空后分配给调用者。
// 返回值是带引用计数的内存 inode，调用者需要持有睡眠锁。
struct inode *ialloc(uint32 dev, short type)
{
    for(uint32 inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode *)bp->data + (inum % IPB);
        if(dip->type == 0) {
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            log_block_write(bp);          // 通过日志立即持久化，确保崩溃后不会重复分配。
            brelse(bp);

            struct inode *ip = iget(dev, inum);
            ip->type = type;
            ip->nlink = 0;
            ip->size = 0;
            memset(ip->addrs, 0, sizeof(ip->addrs));
            return ip;
        }
        brelse(bp);
    }
    panic("ialloc: no inodes");
    return 0;
}

// iget 在内存 inode 缓存中查找指定 dev/inum。若命中，增加引用计数；
// 若未命中，则选择 ref==0 的空槽并初始化元数据（valid=0 表示懒加载）。
struct inode *iget(uint32 dev, uint32 inum)
{
    struct inode *ip, *empty = 0;

    acquire(&itable.lock);
    for(ip = itable.inode; ip < itable.inode + NINODE; ip++) {
        if(ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            release(&itable.lock);
            return ip;
        }
        if(empty == 0 && ip->ref == 0)
            empty = ip;
    }

    if(empty == 0)
        panic("iget: no inodes");

    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    release(&itable.lock);

    return ip;
}

// idup 简化接口：对 inode 的引用计数 +1，保持结构复用与 iget 一致。
struct inode *idup(struct inode *ip)
{
    acquire(&itable.lock);
    ip->ref++;
    release(&itable.lock);
    return ip;
}

// ilock: 加睡眠锁并在必要时从磁盘加载 dinode 内容。该函数保障返回后
// inode 数据字段有效，调用者必须在使用完毕后调用 iunlock。
void ilock(struct inode *ip)
{
    if(ip == 0 || ip->ref < 1)
        panic("ilock");

    acquiresleep(&ip->lock);
    if(ip->valid == 0) {
        struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode *)bp->data + (ip->inum % IPB);
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;
        if(ip->type == 0)
            panic("ilock: no type");
    }
}

// iunlock: 在调用者完成 inode 数据访问后释放睡眠锁。
void iunlock(struct inode *ip)
{
    if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("iunlock");
    releasesleep(&ip->lock);
}

// iupdate: 将内存 inode 的数据写回磁盘 dinode，保证持久化。
void iupdate(struct inode *ip)
{
    struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode *)bp->data + (ip->inum % IPB);
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_block_write(bp);
    brelse(bp);
}

// iput: 将引用计数减一。当满足 ref==1、valid==1、nlink==0 时，说明当前
// 已经没有目录项指向该 inode，需回收磁盘块并将 type 置零。
void iput(struct inode *ip)
{
    acquire(&itable.lock);
    if(ip->ref == 1 && ip->valid && ip->nlink == 0) {
        release(&itable.lock);
        ilock(ip);
        itrunc(ip);       // 释放所有数据块并更新 size。
        ip->type = 0;
        iupdate(ip);
        ip->valid = 0;
        releasesleep(&ip->lock);
        acquire(&itable.lock);
    }
    ip->ref--;
    release(&itable.lock);
}

// iunlockput = iunlock + iput 的组合版本，常用于目录遍历中确保不会遗忘释放锁。
void iunlockput(struct inode *ip)
{
    iunlock(ip);
    iput(ip);
}

// bmap: 将逻辑块号 (bn) 映射到磁盘块号，必要时分配新块。
//  - 直接块：直接返回 ip->addrs[bn]；
//  - 一级间接块：ip->addrs[NDIRECT] 指向含有数据块编号的表；
//  - 二级间接块：ip->addrs[NDIRECT + 1] 指向一级间接表，该表再指向真实数据块。
static uint32 bmap(struct inode *ip, uint32 bn)
{
    if(bn < NDIRECT) {
        if(ip->addrs[bn] == 0)
            ip->addrs[bn] = balloc(ip->dev);
        return ip->addrs[bn];
    }

    bn -= NDIRECT;
    if(bn < NINDIRECT) {
        if(ip->addrs[NDIRECT] == 0)
            ip->addrs[NDIRECT] = balloc(ip->dev);   // 延迟分配一级间接块。

        struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint32 *a = (uint32 *)bp->data;
        if(a[bn] == 0) {
            a[bn] = balloc(ip->dev);
            log_block_write(bp);                  // 仅在新增映射时记录日志，避免重复写入。
        }
        uint32 addr = a[bn];
        brelse(bp);
        return addr;
    }

    bn -= NINDIRECT;
    if(bn >= NDOUBLE)
        panic("bmap: out of range");

    if(ip->addrs[NDIRECT + 1] == 0)
        ip->addrs[NDIRECT + 1] = balloc(ip->dev);   // 延迟分配二级间接块。

    struct buf *dbp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    uint32 *d = (uint32 *)dbp->data;
    uint32 first = bn / NINDIRECT;                 // 一级间接索引。
    uint32 second = bn % NINDIRECT;                // 二级间接表内的偏移。

    if(d[first] == 0) {
        d[first] = balloc(ip->dev);               // 分配新的一级间接表。
        log_block_write(dbp);                     // 记录指针更新。
    }
    struct buf *sbp = bread(ip->dev, d[first]);
    uint32 *a = (uint32 *)sbp->data;
    if(a[second] == 0) {
        a[second] = balloc(ip->dev);              // 分配最终数据块。
        log_block_write(sbp);                     // 记录二级表的更新。
    }
    uint32 addr = a[second];
    brelse(sbp);
    brelse(dbp);
    return addr;
}

// itrunc: 释放 inode 关联的所有数据块，包括直接块、一级间接块与二级间接块。
// 调用方必须已经持有 inode 锁。
void itrunc(struct inode *ip)
{
    // 释放直接块。
    for(int i = 0; i < NDIRECT; i++) {
        if(ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    // 释放一级间接块引用的所有数据块，并回收该间接块。
    if(ip->addrs[NDIRECT]) {
        struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint32 *a = (uint32 *)bp->data;
        for(uint32 j = 0; j < NINDIRECT; j++) {
            if(a[j]) {
                bfree(ip->dev, a[j]);
                a[j] = 0;
            }
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    // 释放二级间接块：先释放所有一级间接表及其下的实际数据块，再回收顶层指针块。
    if(ip->addrs[NDIRECT + 1]) {
        struct buf *dbp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
        uint32 *d = (uint32 *)dbp->data;
        for(uint32 i = 0; i < NINDIRECT; i++) {
            if(d[i]) {
                struct buf *sbp = bread(ip->dev, d[i]);
                uint32 *a = (uint32 *)sbp->data;
                for(uint32 j = 0; j < NINDIRECT; j++) {
                    if(a[j]) {
                        bfree(ip->dev, a[j]);
                        a[j] = 0;
                    }
                }
                brelse(sbp);
                bfree(ip->dev, d[i]);
                d[i] = 0;
            }
        }
        brelse(dbp);
        bfree(ip->dev, ip->addrs[NDIRECT + 1]);
        ip->addrs[NDIRECT + 1] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

// readi: 从 inode 中读取数据到 dst。支持用户态/内核态缓冲区，通过 user_dst 参数区分。
int readi(struct inode *ip, int user_dst, uint64 dst, uint32 off, uint32 n)
{
    if(off > ip->size || off + n < off)
        return -1;
    if(off + n > ip->size)
        n = ip->size - off;   // 读取越界时截断到文件末尾。

    uint32 tot = 0;
    char *kdst = (char *)dst;

    while(tot < n) {
        uint32 bn = (off + tot) / BLOCK_SIZE;
        struct buf *bp = bread(ip->dev, bmap(ip, bn));
        uint32 block_off = (off + tot) % BLOCK_SIZE;
        uint32 m = MIN(n - tot, BLOCK_SIZE - block_off);

        if(user_dst) {
            if(copyout(myproc()->pagetable, dst + tot, (const char *)(bp->data + block_off), m) < 0) {
                brelse(bp);
                return -1;
            }
        } else {
            memmove(kdst + tot, bp->data + block_off, m);
        }
        brelse(bp);
        tot += m;
    }
    return n;
}

// writei: 将 src 缓冲区的数据写入 inode。必要时分配新块并更新文件大小。
int writei(struct inode *ip, int user_src, uint64 src, uint32 off, uint32 n)
{
    if(off > ip->size || off + n < off)
        return -1;
    if(off + n > MAX_FILE_SIZE)
        return -1;

    uint32 tot = 0;
    char *ksrc = (char *)src;

    while(tot < n) {
        uint32 bn = (off + tot) / BLOCK_SIZE;
        struct buf *bp = bread(ip->dev, bmap(ip, bn));
        uint32 block_off = (off + tot) % BLOCK_SIZE;
        uint32 m = MIN(n - tot, BLOCK_SIZE - block_off);

        if(user_src) {
            if(copyin(myproc()->pagetable, (char *)(bp->data + block_off), src + tot, m) < 0) {
                brelse(bp);
                return -1;
            }
        } else {
            memmove(bp->data + block_off, ksrc + tot, m);
        }
        log_block_write(bp);
        brelse(bp);
        tot += m;
    }

    if(off + n > ip->size)
        ip->size = off + n;
    iupdate(ip);
    return n;
}

// dirlookup: 在目录 dp 中查找 name，对应目录项存在则返回 inode。poff 若非空
// 会记录目录项偏移，便于删除/覆盖。
struct inode *dirlookup(struct inode *dp, char *name, uint32 *poff)
{
    if(dp->type != T_DIR)
        panic("dirlookup not DIR");

    struct dirent de;
    // 线性扫描整个目录文件
    for(uint32 off = 0; off < dp->size; off += sizeof(de)) {
        // 读取目录项
        if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlookup read");
        if(de.inum == 0)  // 跳过空闲槽位
            continue;
        // 名称比较
        if(namecmp(name, de.name) == 0) {
            if(poff)
                *poff = off;  // 记录目录项偏移
            return iget(dp->dev, de.inum);  // 返回目标inode
        }
    }
    return 0;  // 未找到
}

// dirlink: 在目录 dp 中插入 name -> inum 的条目。若 name 已存在直接返回 -1。
int dirlink(struct inode *dp, char *name, uint32 inum)
{
    // 检查名称是否已存在
    if(dirlookup(dp, name, 0) != 0)
        return -1;

    struct dirent de;
    uint32 off;
    // 寻找空闲槽位或文件末尾
    for(off = 0; off < dp->size; off += sizeof(de)) {
        if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlink read");
        if(de.inum == 0)  // 找到空闲槽位
            break;
    }

    // 构建新目录项
    memset(&de, 0, sizeof(de));
    de.inum = inum;
    // 复制文件名（最多13个字符 + 终止符）
    for(int i = 0; i < DIRSIZ - 1 && name[i]; i++) {
        de.name[i] = name[i];
    }

    // 写入目录项
    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink write");
    return 0;
}

struct inode *namei(char *path)
{
    return namex(path, 0, 0);
}

struct inode *nameiparent(char *path, char *name)
{
    return namex(path, 1, name);
}

// namecmp: 对目录项名称进行比较，考虑 DIRSIZ 固定长度的填充情况。
static int namecmp(const char *s, const char *t)
{
    for(int i = 0; i < DIRSIZ; i++) {
        char cs = s[i];
        char ct = t[i];
        if(cs != ct)
            return (unsigned char)cs - (unsigned char)ct;
        if(cs == '\0')
            return 0;
    }
    return 0;
}

// skipelem: 解析路径中的下一个元素，返回下一个待处理的位置。
// 同时将当前元素复制到 name (长度限制为 DIRSIZ)。
static char *skipelem(char *path, char *name)
{
    char *s;
    int len;

    // 跳过前导斜杠
    while(*path == '/')
        path++;
    if(*path == '\0')
        return 0;
        
    // 提取当前路径组件
    s = path;
    while(*path != '/' && *path != '\0')
        path++;
    len = path - s;
    
    // 复制到name缓冲区（固定长度）
    memset(name, 0, DIRSIZ);
    if(len > 0) {
        if(len >= DIRSIZ)
            memmove(name, s, DIRSIZ - 1);  // 截断处理
        else
            memmove(name, s, len);
    }
    
    // 跳过后续斜杠，返回剩余路径
    while(*path == '/')
        path++;
    return path;
}

// build_symlink_path: 将符号链接目标与剩余路径拼接成新的查找串。返回 0 表示成功，-1 表示路径过长。
static int build_symlink_path(char *dst, const char *target, const char *rest)
{
    safestrcpy(dst, target, MAXPATH);
    if(rest && *rest) {
        int len = strlen(dst);
        if(len + 1 >= MAXPATH)
            return -1;
        dst[len++] = '/';
        dst[len] = '\0';
        if(strlen(rest) >= MAXPATH - len)
            return -1;
        safestrcpy(dst + len, rest, MAXPATH - len);
    }
    return 0;
}

// namex_from: 基于指定起点解析路径，支持符号链接展开并遵循最大深度限制。
static struct inode *namex_from(struct inode *ip, char *path, int nameiparent, char *name, int depth)
{
    struct inode *next;
    char elem[DIRSIZ];
    // 逐级解析路径组件
    while((path = skipelem(path, elem)) != 0) {
        ilock(ip);
        
        // 路径中间组件必须是目录
        if(ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }
        
        // nameiparent模式：返回父目录
        if(nameiparent && *path == '\0') {
            if(name) {
                memset(name, 0, DIRSIZ);
                memmove(name, elem, DIRSIZ);
            }
            iunlock(ip);
            return ip;  // 返回父目录（已加锁）
        }

        struct inode *parent = ip;
        if((next = dirlookup(ip, elem, 0)) == 0) {
            iunlockput(ip);
            return 0;  // 路径组件不存在
        }
        
        iunlock(ip);
        ip = next;

        // 如果当前 inode 是符号链接，并且不是在 nameiparent 模式下处理最后一个分量，则需要解析符号链接。
        if(ip->type == T_SYMLINK && !(nameiparent && *path == '\0')) {
            // 检查递归深度，防止符号链接循环导致无限递归。
            if(depth >= MAX_SYMLINK_DEPTH) {
                iunlockput(ip);
                iput(parent);
                return 0;
            }

            char target[MAXPATH];
            ilock(ip); // 加锁以安全读取符号链接内容
            uint32 stored = ip->size;
            // 检查符号链接内容长度是否合法
            if(stored == 0 || stored > MAXPATH) {
                iunlockput(ip);
                iput(parent);
                return 0;
            }
            // 读取符号链接目标路径字符串
            if(readi(ip, 0, (uint64)target, 0, stored) != stored) {
                iunlockput(ip);
                iput(parent);
                return 0;
            }
            target[stored - 1] = '\0'; // 确保字符串结尾
            iunlock(ip);
            iput(ip);

            char combined[MAXPATH];
            // 绝对路径符号链接：从根目录重新解析
            if(target[0] == '/') {
                struct inode *root = iget(ROOTDEV, ROOTINO);
                iput(parent);
                if(root == 0)
                    return 0;
                // 拼接符号链接目标和剩余路径，combined = target + path
                if(build_symlink_path(combined, target, path) < 0) {
                    iput(root);
                    return 0;
                }
                // 递归解析新路径，递归深度+1
                return namex_from(root, combined, nameiparent, name, depth + 1);
            } else {
                // 相对路径符号链接：从父目录继续解析
                if(build_symlink_path(combined, target, path) < 0) {
                    iput(parent);
                    return 0;
                }
                // 递归解析新路径，递归深度+1
                return namex_from(parent, combined, nameiparent, name, depth + 1);
            }
        }

        iput(parent);
    }

    if(nameiparent) {
        iput(ip);  // nameiparent模式不返回目标inode
        return 0;
    }
    return ip;  // 返回目标inode
}

// namex: 路径解析核心函数。
//   - nameiparent=0: 返回最终目标 inode；
//   - nameiparent=1: 返回父目录并将最后一个路径名写入 name。
//   解析过程中会自动展开符号链接，确保与 POSIX 行为保持一致。
static struct inode *namex(char *path, int nameiparent, char *name)
{
    struct proc *p = myproc();
    struct inode *start;

    if(path == 0 || *path == '\0')
        return 0;

    if(*path == '/' || p == 0 || p->cwd == 0)
        start = iget(ROOTDEV, ROOTINO);
    else
        start = idup(p->cwd);

    return namex_from(start, path, nameiparent, name, 0);
}


// balloc: 在 bitmap 中找到第一个空闲数据块，标记为已用并清零内容。
static uint32 balloc(uint32 dev)
{
    for(uint32 b = 0; b < sb.nblocks; b++) {
        uint32 bno = b + DATA_START;
        struct buf *bp = bread(dev, BBLOCK(bno, sb));
        uint32 bi = bno % BPB;
        uint8 mask = 1 << (bi % 8);
        if((bp->data[bi / 8] & mask) == 0) {
            bp->data[bi / 8] |= mask;
            log_block_write(bp);
            brelse(bp);

            bp = bread(dev, bno);
            memset(bp->data, 0, BLOCK_SIZE);  // 新分配块必须清零，防止泄露旧数据。
            log_block_write(bp);
            brelse(bp);
            return bno;
        }
        brelse(bp);
    }
    panic("balloc: out of blocks");
    return 0;
}

// bfree: 清除 bitmap 中的位，表示数据块重新可用。调用者需确保该块确实闲置。
static void bfree(uint32 dev, uint32 b)
{
    struct buf *bp = bread(dev, BBLOCK(b, sb));
    uint32 bi = b % BPB;
    bp->data[bi / 8] &= ~(1 << (bi % 8));
    log_block_write(bp);
    brelse(bp);
}
