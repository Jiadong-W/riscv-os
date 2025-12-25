#include "user.h"
#include <stdarg.h>

// user 空间的 libc 精简实现。提供进程入口、系统调用封装以及若干测试例程。

// 前向声明：main 由用户程序实现，exit 在稍后定义。
extern int main(void);
int exit(int status) __attribute__((noreturn));

// _start 是所有用户程序的入口。内核加载 ELF 后从该符号开始执行，
// 负责调用 main() 并将返回值作为 exit 的退出码。放入 .text.boot 以便链接脚本控制位置。
void _start(void) __attribute__((noreturn, section(".text.boot")));
void _start(void)
{
    exit(main());
}

int errno = 0;  // 若系统调用返回错误，errno 保存正向错误码，方便测试代码打印。

// syscall_ret: 将内核返回值转换为 libc 语义。
//   - ret >= 0: 直接返回；
//   - ret < 0 : 表示内核返回负错误码，需转为 -1 并写入 errno。
static inline int syscall_ret(int ret)
{
    if(ret < 0) {
        errno = -ret;
        return -1;
    }
    errno = 0;
    return ret;
}

static inline long syscall_ret_long(long ret)
{
    if(ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    errno = 0;
    return ret;
}

// 基础工具函数 ------------------------------------------------------------------

// strlen: 计算以 \0 结尾字符串长度。若传入 NULL，则返回 0 以避免崩溃。
int strlen(const char *s)
{
    int n = 0;
    if(s == 0)
        return 0;
    while(s[n])
        n++;
    return n;
}

// memcpy: 字节复制实现，适用于小型缓冲区。未处理重叠内存场景。
void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while(n--)
        *d++ = *s++;
    return dst;
}

// 汇编桩函数声明，详见 user/usys.S。每个符号对应一次 ecall。
extern int __sys_exit(int);
extern int __sys_getpid(void);
extern int __sys_fork(void);
extern int __sys_wait(int *);
extern int __sys_kill(int);
extern int __sys_write(int, const void *, int);
extern int __sys_read(int, void *, int);
extern int __sys_open(const char *, int);
extern int __sys_close(int);
extern int __sys_unlink(const char *);
extern int __sys_symlink(const char *, const char *);
extern long __sys_sbrk(int);
extern long __sys_time(void);
extern int __sys_set_crash_stage(int);
extern int __sys_recover_log(void);
extern int __sys_clear_cache(void);
extern int __sys_exec(const char *, char **);
extern int __sys_dup(int);
extern int __sys_mknod(const char *, int, int, int);
extern int __sys_chdir(const char *);
extern int __sys_ticks(void);
extern int __sys_getpriority(void);
extern int __sys_klog_dump(void);
extern int __sys_klog_set_threshold(int, int);
extern int __sys_sleep(int);

// 系统调用封装 ------------------------------------------------------------------
// 这些函数位于 ulib.c 中，便于用户程序直接调用，同时统一处理 errno。

int getpid(void)
{
    return syscall_ret(__sys_getpid());
}

int fork(void)
{
    return syscall_ret(__sys_fork());
}

int wait(int *status)
{
    return syscall_ret(__sys_wait(status));
}

int kill(int pid)
{
    return syscall_ret(__sys_kill(pid));
}

int write(int fd, const void *buf, int len)
{
    return syscall_ret(__sys_write(fd, buf, len));
}

int read(int fd, void *buf, int len)
{
    return syscall_ret(__sys_read(fd, buf, len));
}

int open(const char *path, int mode)
{
    return syscall_ret(__sys_open(path, mode));
}

int close(int fd)
{
    return syscall_ret(__sys_close(fd));
}

int unlink(const char *path)
{
    return syscall_ret(__sys_unlink(path));
}

// symlink: 用户态封装，创建指向 target 的符号链接。
int symlink(const char *target, const char *linkpath)
{
    return syscall_ret(__sys_symlink(target, linkpath));
}

int dup(int fd)
{
    return syscall_ret(__sys_dup(fd));
}

int mknod(const char *path, int major, int minor, int type)
{
    return syscall_ret(__sys_mknod(path, major, minor, type));
}

// sbrk: 调整用户态堆空间，返回旧的堆顶指针；失败返回 (void*)-1。
void *sbrk(int increment)
{
    long ret = syscall_ret_long(__sys_sbrk(increment));
    if(ret < 0)
        return (void*)-1;
    return (void*)(uint64_t)ret;
}

int chdir(const char *path)
{
    return syscall_ret(__sys_chdir(path));
}

int exec(const char *path, char *const argv[])
{
    return syscall_ret(__sys_exec(path, (char **)argv));
}

int set_crash_stage(int stage)
{
    return syscall_ret(__sys_set_crash_stage(stage));
}

int recover_log(void)
{
    return syscall_ret(__sys_recover_log());
}

int clear_cache(void)
{
    return syscall_ret(__sys_clear_cache());
}

int klog_dump(void)
{
    return syscall_ret(__sys_klog_dump());
}

int klog_set_threshold(int record_level, int console_level)
{
    return syscall_ret(__sys_klog_set_threshold(record_level, console_level));
}

// exit 不返回。若内核异常返回（例如尚未实现或错误），选择自旋等待，
// 避免继续执行未定义行为导致内存破坏。
int exit(int status)
{
    __sys_exit(status);
    for(;;) {
        // 正常情况下不会执行到这里。
    }
}

// 通过系统调用获取时间计数器，与其他封装保持一致的错误处理。
uint64_t get_time(void)
{
    long ret = syscall_ret_long(__sys_time());
    if(ret < 0)
        return (uint64_t)-1;
    return (uint64_t)ret;
}

uint64_t get_ticks(void)
{
    long ret = syscall_ret_long(__sys_ticks());
    if(ret < 0)
        return (uint64_t)-1;
    return (uint64_t)ret;
}

int get_priority_level(void)
{
    return syscall_ret(__sys_getpriority());
}

int sleep(int ticks)
{
    return syscall_ret(__sys_sleep(ticks));
}

// -------------------------------------------------------------
// 以下为测试辅助函数，便于在用户态验证系统调用行为。
// -------------------------------------------------------------


void test_basic_syscalls(void)
{
    printf("[syscall] basic functionality...\n");

    int pid = getpid();
    printf("getpid() -> %d\n", pid);

    uint64_t start_ticks = get_ticks();
    sleep(5);
    uint64_t end_ticks = get_ticks();
    unsigned long delta = (unsigned long)(end_ticks - start_ticks);
    printf("sleep(5) advanced ticks by %lu\n", delta);

    int child_pid = fork();
    if(child_pid == 0) {
        printf("child(%d) exiting with status=42\n", getpid());
        exit(42);
    } else if(child_pid > 0) {
        int status = -1;
        int waited = wait(&status);
        printf("wait() -> pid=%d status=%d errno=%d\n", waited, status, errno);
    } else {
        printf("fork failed, errno=%d\n", errno);
    }

    printf("[syscall] basic functionality done.\n");
}

void test_parameter_passing(void)
{
    printf("[syscall] parameter passing...\n");

    char buffer[] = "Hello from syscall tests!\n";
    int fd = open("console", O_RDWR);
    if(fd < 0) {
        printf("open(console) failed, errno=%d\n", errno);
        return;
    }

    int bytes_written = write(fd, buffer, strlen(buffer));
    printf("write(console, buffer, %d) -> %d\n", (int)strlen(buffer), bytes_written);

    int ret = write(-1, buffer, strlen(buffer));
    printf("write(-1, buffer, len) -> %d errno=%d\n", ret, errno);

    
    ret = write(fd, buffer, -1);
    printf("write(fd, buffer, -1) -> %d errno=%d\n", ret, errno);

    void *old_break = sbrk(0);
    void *grown_break = sbrk(4096);
    void *shrunk_break = sbrk(-4096);
    printf("sbrk sequence -> old=%p grown=%p shrink_ret=%p\n", old_break, grown_break, shrunk_break);

    close(fd);
    printf("[syscall] parameter passing done.\n");
}

void test_security(void)
{
    printf("[syscall] safety checks...\n");

    char *invalid_ptr = (char*)0xffffffffffffffffULL;
    int result = write(1, invalid_ptr, 16);
    printf("write(invalid_ptr) -> %d errno=%d\n", result, errno);

    char *invalid_buf = (char*)0xffffffff00000000ULL;
    result = read(0, invalid_buf, 16);
    printf("read(invalid_ptr) -> %d errno=%d\n", result, errno);

    const char *path = "sec";
    unlink(path);
    int perm_fd = open(path, O_CREATE | O_WRONLY);
    if(perm_fd >= 0) {
        write(perm_fd, "x", 1);
        close(perm_fd);
    }

    int wr = -1, wr_err = 0;
    int rd = -1, rd_err = 0;

    int ro_fd = open(path, O_RDONLY);
    if(ro_fd >= 0) {
        wr = write(ro_fd, "y", 1);
        wr_err = errno;
        close(ro_fd);
    }

    int wo_fd = open(path, O_WRONLY);
    if(wo_fd >= 0) {
        char tmp;
        rd = read(wo_fd, &tmp, 1);
        rd_err = errno;
        close(wo_fd);
    }

    unlink(path);
    printf("write on read-only fd -> %d errno=%d\n", wr, wr_err);
    printf("read on write-only fd -> %d errno=%d\n", rd, rd_err);
    printf("[syscall] safety checks done.\n");
}
/*
void test_filesystem_integrity(void)
{
    printf("[syscall] filesystem integrity...\n");

    const char *file = "syscall-test-file";
    const char *link = "syscall-test-link";
    unlink(file);
    unlink(link);

    int fd = open(file, O_CREATE | O_RDWR);
    if(fd < 0) {
        printf("open(%s) failed, errno=%d\n", file, errno);
        return;
    }

    const char *payload = "SYS-CALL-CHECK";
    const char *extra = "-OK";
    write(fd, payload, strlen(payload));

    int dupfd = dup(fd);
    if(dupfd >= 0) {
        write(dupfd, extra, strlen(extra));
        close(dupfd);
    } else {
        printf("dup failed, errno=%d\n", errno);
    }
    close(fd);

    fd = open(file, O_RDONLY);
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int read_bytes = -1;
    if(fd >= 0) {
        read_bytes = read(fd, buf, sizeof(buf) - 1);
        close(fd);
    }
    printf("read(%s) -> %d bytes, content='%s'\n", file, read_bytes, buf);

    if(symlink(file, link) == 0) {
        int lfd = open(link, O_RDONLY);
        char lbuf[32];
        memset(lbuf, 0, sizeof(lbuf));
        int lbytes = -1;
        if(lfd >= 0) {
            lbytes = read(lfd, lbuf, sizeof(lbuf) - 1);
            close(lfd);
        }
        printf("symlink(%s->%s) read %d bytes: '%s'\n", link, file, lbytes, lbuf);
    } else {
        printf("symlink creation failed, errno=%d\n", errno);
    }

    unlink(link);
    unlink(file);
    printf("[syscall] filesystem integrity done.\n");
}
*/

void test_syscall_performance(void)
{
    uint64_t start_time = get_time();

    // 大量系统调用测试
    for(int i = 0; i < 10000; i++) {
        getpid();
    }

    uint64_t end_time = get_time();
    unsigned int delta = (unsigned int)(end_time - start_time);
    printf("10000 getpid() calls took %u cycles\n", delta);
}



char* strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char* gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

void* memset(void *dst, int c, unsigned int n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}
