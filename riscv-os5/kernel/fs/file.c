#include "types.h"
#include "spinlock.h"
#include "file.h"
#include "fs.h"
#include "log.h"
#include "printf.h"

// file.c 管理内核态的“打开文件表”。每个 struct file 代表一个打开的对象，
// 可以是普通文件、设备或管道。进程的 ofile[NOFILE] 数组仅保存指针。

// devsw: 设备主编号到读写回调的映射表。设备驱动在启动阶段注册。
struct devsw devsw[NDEV];

static struct {
    struct spinlock lock;    // 保护 file[] 的自旋锁，避免并发分配冲突。
    struct file file[NFILE]; // 全局文件表，容量由 include/file.h 中定义。
} ftable;

// 初始化全局文件表，仅需配置自旋锁。file 数组默认全零。
void fileinit(void)
{
    initlock(&ftable.lock, "ftable");
}

// filealloc: 在全局表中找一个 ref==0 的槽位并返回，引用计数预设为 1。
// 返回的文件表项已清理核心字段，调用者只需进一步填充类型/权限等信息。
struct file *filealloc(void)
{
    acquire(&ftable.lock);
    for(struct file *f = ftable.file; f < ftable.file + NFILE; f++) {
        if(f->ref == 0) {
            f->ref = 1;
            f->type = FD_NONE;
            f->readable = 0;
            f->writable = 0;
            f->pipe = 0;
            f->ip = 0;
            f->off = 0;
            f->major = 0;
            release(&ftable.lock);
            return f;
        }
    }
    release(&ftable.lock);
    return 0;
}

// filedup: 提升引用计数，供 fork/dup 等场景共享同一个 struct file。
struct file *filedup(struct file *f)
{
    acquire(&ftable.lock);
    if(f->ref < 1)
        panic("filedup");
    f->ref++;
    release(&ftable.lock);
    return f;
}

// fileclose: 当引用计数降为 0 时，根据文件类型执行收尾逻辑。
//  - FD_PIPE: 未来可在此实现管道资源回收；
//  - FD_DEVICE: 设备不需要额外操作；
//  - FD_INODE: 通过 iput 归还 inode。
void fileclose(struct file *f)
{
    struct file ff;

    acquire(&ftable.lock);
    if(f->ref < 1)
        panic("fileclose");
    f->ref--;
    if(f->ref > 0) {
        release(&ftable.lock);
        return;
    }

    // 将内容复制一份放到栈上，随后把槽位清零，避免释放路径中再次访问。
    ff = *f;
    f->type = FD_NONE;
    f->pipe = 0;
    f->ip = 0;
    f->readable = 0;
    f->writable = 0;
    f->off = 0;
    release(&ftable.lock);

    switch(ff.type) {
    case FD_PIPE:
        // 管道尚未实现，占位以明确后续扩展位置。
        break;
    case FD_DEVICE:
        // 设备文件无需额外处理，大多数驱动在 devsw 中拥有共享状态。
        break;
    case FD_INODE:
        if(ff.ip)
            iput(ff.ip);   // 递减 inode 引用计数，必要时回收磁盘资源。
        break;
    default:
        break;
    }
}

// filestat 尚未实现，当前返回 -1。未来可在此扩展 stat 系统调用。
int filestat(struct file *f, uint64 addr)
{
    (void)f;
    (void)addr;
    return -1;
}

// fileread 根据文件类型分派读逻辑。
//  - FD_PIPE: 未实现，直接返回错误；
//  - FD_DEVICE: 调用 devsw 中注册的 read 回调；
//  - FD_INODE: 需获得 inode 睡眠锁，调用 readi，并更新文件偏移。
int fileread(struct file *f, uint64 addr, int n)
{
    if(f->readable == 0)
        return -1;

    switch(f->type) {
    case FD_PIPE:
        return -1;
    case FD_DEVICE:
        if(f->major < 0 || f->major >= NDEV || devsw[f->major].read == 0)
            return -1;
        return devsw[f->major].read(1, addr, n);
    case FD_INODE: {
        ilock(f->ip);
        int r = readi(f->ip, 1, addr, f->off, n);
        if(r > 0)
            f->off += r;   // 仅在 read 成功时推进文件偏移。
        iunlock(f->ip);
        return r;
    }
    default:
        return -1;
    }
}

// filewrite 与 fileread 类似，但针对写路径。
//  - 对 inode：writei 负责分配块、复制数据，并在成功后更新 off。
int filewrite(struct file *f, uint64 addr, int n)
{
    if(f->writable == 0)
        return -1;

    switch(f->type) {
    case FD_PIPE:
        return -1;
    case FD_DEVICE:
        if(f->major < 0 || f->major >= NDEV || devsw[f->major].write == 0)
            return -1;
        return devsw[f->major].write(1, addr, n);
    case FD_INODE: {
        // 分批写入以避免单次事务占用过多日志块，公式与 xv6 保持一致。
        int max = ((MAX_OP_BLOCKS - 1 - 1 - 2) / 2) * BLOCK_SIZE;
        int written = 0;
        while(written < n) {
            int chunk = n - written;
            if(chunk > max)
                chunk = max;

            begin_transaction();
            ilock(f->ip);
            int r = writei(f->ip, 1, addr + written, f->off, chunk);
            if(r > 0)
                f->off += r;
            iunlock(f->ip);
            end_transaction();

            if(r < 0)
                return -1;
            if(r != chunk)
                return -1;
            written += r;
        }
        return written;
    }
    default:
        return -1;
    }
}
