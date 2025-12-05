#include "types.h"
#include "printf.h"
#include "string.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "proc.h"
#include "syscall.h"

// sysfile.c 实现与文件系统相关的系统调用：open/read/write/close/unlink 等。
// 这些接口在用户态通过 ulib.c 的封装访问，内核态则依赖 fs.c 提供的原语。

// strequal: 简单的字符串全等比较，主要用于识别特殊设备名（如 console）。
static int strequal(const char *a, const char *b)
{
    while(*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

// argfd: 解析第 n 个系统调用参数并返回文件指针。若调用者需要文件描述符，
// 可通过 pfd 输出。边界检查/空指针检查失败时返回 0。
static struct file *argfd(int n, int *pfd)
{
    int fd;
    if(argint(n, &fd) < 0)
        return 0;
    if(fd < 0 || fd >= NOFILE)
        return 0;
    struct proc *p = myproc();
    struct file *f = p->ofile[fd];
    if(f == 0)
        return 0;
    if(pfd)
        *pfd = fd;
    return f;
}

// fdalloc: 在当前进程的 ofile[] 中找一个空槽，并将 f 绑定到该槽位。
// 与 filealloc 搭配使用，确保每个打开的文件都有一个描述符可供用户态引用。
static int fdalloc(struct file *f)
{
    struct proc *p = myproc();
    for(int fd = 0; fd < NOFILE; fd++) {
        if(p->ofile[fd] == 0) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

// create: open(O_CREATE) 或 mkdir 等操作的公共入口。
// 步骤：
//   1) 获取父目录并加锁；
//   2) 若同名文件已存在，根据类型判断是否可以直接返回；
//   3) 分配新的 inode，设置基本属性并在父目录中插入目录项；
//   4) 对目录类型还需创建 '.' 和 '..'，同时更新父目录链接计数。
static struct inode *create(char *path, short type, short major, short minor)
{
    char name[DIRSIZ];
    struct inode *dp;
    struct inode *ip;

    if((dp = nameiparent(path, name)) == 0)
        return 0;
    ilock(dp);

    if((ip = dirlookup(dp, name, 0)) != 0) {
        iunlockput(dp);
        ilock(ip);
        if(type == T_FILE && ip->type == T_FILE)
            return ip;   // O_CREATE 打开已存在的普通文件时直接复用。
        iunlockput(ip);
        return 0;
    }

    if((ip = ialloc(dp->dev, type)) == 0)
        panic("create: ialloc");

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if(type == T_DIR) {
        dp->nlink++;
        iupdate(dp);
        if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
            panic("create: dirlink");
    }

    if(dirlink(dp, name, ip->inum) < 0)
        panic("create: parent link");

    iunlockput(dp);
    return ip;
}

// sys_open: 解析路径和模式，支持三类场景：
//   1) 打开 console 设备：特殊路径映射到设备表；
//   2) O_CREATE：调用 create 分配新的 inode；
//   3) 普通打开：namei 定位已存在的 inode，并根据类型/权限做检查。
int sys_open(void)
{
    char path[MAXPATH];
    int omode;
    struct inode *ip;
    struct file *f;
    int fd;
    struct proc *p = myproc();

    if(argstr(0, path, sizeof(path)) < 0 || argint(1, &omode) < 0)
        return -1; // 参数 0 为文件路径，1 为打开模式，任一解析失败立刻返回。

    if(strequal(path, "console") || strequal(path, "/dev/console")) {
        // 特殊路径映射到内置控制台设备，跳过 inode 流程。
        if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
            // 分配文件结构或占用文件描述符失败，需要回滚。
            if(f)
                fileclose(f);
            return -1;
        }
        f->type = FD_DEVICE;
        f->readable = !(omode & O_WRONLY);   // 只写模式意味着不可读。
        f->writable = (omode & O_WRONLY) || (omode & O_RDWR); // 需要写权限时标记可写。
        f->off = 0;
        f->ip = 0;
        f->major = CONSOLE;
        if((f->readable && devsw[CONSOLE].read == 0) || (f->writable && devsw[CONSOLE].write == 0)) {
            // 设备驱动不支持所需方向时撤销打开操作。
            myproc()->ofile[fd] = 0; // 释放 ofile 槽位，避免悬挂引用。
            fileclose(f);
            return -1;
        }
        return fd;
    }

    if(omode & O_CREATE) {
        // O_CREATE 走 create() 分支，新建或覆盖普通文件。
        if((ip = create(path, T_FILE, 0, 0)) == 0)
            return -1;
    } else {
        if((ip = namei(path)) == 0)
            return -1;   // 未找到目标路径。
        ilock(ip);
        if(ip->type == T_DIR && (omode & (O_WRONLY | O_RDWR))) {
            // 目录禁止按写模式打开，避免破坏结构。
            iunlockput(ip);
            return -1;   // 禁止以写方式打开目录。
        }
    }

    if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
        // 资源不足时撤销打开，并释放已占资源。
        if(f)
            fileclose(f);
        iunlockput(ip);
        return -1;
    }

    f->type = (ip->type == T_DEV) ? FD_DEVICE : FD_INODE; // 根据 inode 类型决定走设备还是普通路径。
    f->readable = !(omode & O_WRONLY);   // 只写模式意味着不可读。
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR); // 需要写权限时标记可写。
    f->off = 0;
    f->ip = ip;
    f->major = ip->major;

    if(f->type == FD_DEVICE) {
        int major = f->major;
        int need_read = f->readable;
        int need_write = f->writable;
        if(major < 0 || major >= NDEV ||
           (need_read && devsw[major].read == 0) ||
           (need_write && devsw[major].write == 0)) {
            // 检查设备号与驱动能力是否满足需求。
            p->ofile[fd] = 0;
            fileclose(f);
            iunlockput(ip);
            return -1;
        }
    }

    iunlock(ip);
    return fd;
}

// sys_read/sys_write: 解析文件描述符、缓冲区地址和长度，然后交给 fileread / filewrite。
int sys_read(void)
{
    struct file *f;
    uint64 addr;
    int n;

    if((f = argfd(0, 0)) == 0)
        return -1;   // 验证文件描述符有效并取得 struct file。
    if(argaddr(1, &addr) < 0 || argint(2, &n) < 0)
        return -1;   // 第二个参数是用户缓冲区指针，第三个为长度。
    return fileread(f, addr, n);
}

int sys_write(void)
{
    struct file *f;
    uint64 addr;
    int n;

    if((f = argfd(0, 0)) == 0)
        return -1;   // 验证文件描述符有效并取得 struct file。
    if(argaddr(1, &addr) < 0 || argint(2, &n) < 0)
        return -1;   // 第二个参数是用户缓冲区指针，第三个为长度。
    return filewrite(f, addr, n);
}

// sys_close: 将文件描述符从进程表中移除，随后调用 fileclose 回收资源。
int sys_close(void)
{
    int fd;
    struct file *f;

    if((f = argfd(0, &fd)) == 0)
        return -1;   // 解析 fd 并返回文件对象，顺便带回 fd 值。

    myproc()->ofile[fd] = 0; // 释放 ofile 槽位，避免悬挂引用。
    fileclose(f);
    return 0;
}

// isdirempty: 用于 unlink 删除目录时检查目录是否为空。
// 目录的前两个条目固定为 '.' 和 '..'，因此从偏移 2*sizeof(dirent) 开始检查。
static int isdirempty(struct inode *dp)
{
    struct dirent de;
    for(uint32 off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
        if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("isdirempty: read");
        if(de.inum != 0)
            return 0;
    }
    return 1;
}

// 判断是否为 '.' 或 '..'，避免删除特殊目录项。
static int is_special_dirname(const char *name)
{
    return (name[0] == '.' && name[1] == '\0') ||
           (name[0] == '.' && name[1] == '.' && name[2] == '\0');
}

// sys_unlink: 删除目录项并减少目标 inode 的链接计数。
// 关键步骤：
//   1) 定位父目录和目标项，禁止删除 '.'/'..'；
//   2) 若目标为目录，需要确认其为空；
//   3) 将目录项清零，并在必要时递减父目录链接计数；
//   4) 减少目标 inode 的 nlink，若归零则交由 iput 回收。
int sys_unlink(void)
{
    char path[MAXPATH];
    char name[DIRSIZ];
    uint32 off = 0;
    struct inode *dp;
    struct inode *ip;

    if(argstr(0, path, sizeof(path)) < 0)
        return -1;   // 解析待删除路径。
    if((dp = nameiparent(path, name)) == 0)
        return -1;   // 找不到父目录即无法删除。

    ilock(dp);

    if(is_special_dirname(name)) {
        iunlockput(dp);
        return -1;   // 禁止删除 "." 或 ".."。
    }

    if((ip = dirlookup(dp, name, &off)) == 0) {
        iunlockput(dp);
        return -1;   // 目标不存在，直接返回。
    }

    ilock(ip);

    if(ip->nlink < 1)
        panic("sys_unlink: nlink < 1"); // nlink 异常说明文件系统损坏。
    if(ip->type == T_DIR && !isdirempty(ip)) {
        iunlockput(ip);
        iunlockput(dp);
        return -1;   // 非空目录不可删除。
    }

    struct dirent de;
    memset(&de, 0, sizeof(de));
    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("sys_unlink: writei"); // 将目录项清零失败意味着磁盘状态异常。

    if(ip->type == T_DIR) {
        dp->nlink--;          // 父目录丢失一个子目录链接。
        iupdate(dp);
    }

    iunlockput(dp);

    ip->nlink--;           // 目标 inode 链接计数减一。
    iupdate(ip);
    iunlockput(ip);

    return 0;
}
