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
extern long __sys_sbrk(int);

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

// sbrk: 调整用户态堆空间，返回旧的堆顶指针；失败返回 (void*)-1。
void *sbrk(int increment)
{
    long ret = __sys_sbrk(increment);
    if(ret < 0) {
        errno = (int)(-ret);
        return (void*)-1;
    }
    errno = 0;
    return (void*)(uint64_t)ret;
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

// -------------------- 用户态时间获取工具 --------------------
// 访问 RISC-V 的 time CSR，供性能测试使用。需要硬件允许用户态读 time。
static inline uint64_t read_time_csr(void)
{
    uint64_t val;
    __asm__ volatile("rdtime %0" : "=r"(val));
    return val;
}

static inline uint64_t get_time(void)
{
    // 与内核 trap.c 保持一致：读取 time CSR，需硬件允许用户态访问。
    return read_time_csr();
}

// -------------------------------------------------------------
// 以下为测试辅助函数，便于在用户态验证系统调用行为。
// -------------------------------------------------------------
void test_basic_syscalls(void)
{
    printf("Testing basic system calls...\n");

    int pid = getpid();
    printf("Current PID: %d\n", pid);

    int child_pid = fork();
    if(child_pid == 0) {
        printf("Child process: PID=%d\n", getpid());
        exit(42);
    } else if(child_pid > 0) {
        int status = 0;
        if(wait(&status) >= 0) {
            printf("Child exited with status: %d\n", status);
        } else {
            printf("wait failed unexpectedly, errno=%d\n", errno);
        }
    } else {
        printf("fork failed, errno=%d\n", errno);
    }

    printf("Basic system call tests finished.\n");
}

 void test_parameter_passing(void) { 
    // 测试不同类型参数的传递
    char buffer[] = "Hello, World!"; 
    int fd = open("/dev/console", O_RDWR); 

    if (fd >= 0) { 
        int bytes_written = write(fd, buffer, strlen(buffer)); 
        printf("Wrote %d bytes\n", bytes_written); 
        close(fd); 
    } 
 
    // 测试边界情况
    int r_invalid_fd = write(-1, buffer, 10);     // 无效文件描述符
    printf("write(-1, buffer, 10) -> %d \n", r_invalid_fd);

    int r_null_buf = write(fd, NULL, 10);         // 空指针
    printf("write(fd, NULL, 10) -> %d\n", r_null_buf);

    int r_neg_len = write(fd, buffer, -1);        // 负数长度
    printf("write(fd, buffer, -1) -> %d\n", r_neg_len);
 }

 void test_security(void) { 
    // 测试无效指针访问
    char *invalid_ptr = (char*)0x1000000;  // 可能无效的地址
    int result = write(1, invalid_ptr, 10); 
    printf("Invalid pointer write result: %d\n", result); 
 
    // 测试缓冲区边界
    char small_buf[4]; 
    result = read(0, small_buf, 1000);  // 尝试读取超过缓冲区大小
    printf("Invalid pointer write result: %d\n", result); 
 
    // 测试权限检查
    // 创建一个仅写文件，后续分别以只读和只写方式打开，
    // 检查写只读和读只写是否被内核拒绝。
    const char *path = "sec";
    int perm_fd = open(path, O_CREATE | O_WRONLY);
    if(perm_fd >= 0) {
        write(perm_fd, "x", 1);   // 初始化文件内容，方便后续读写对比
        close(perm_fd);
    }

    int wr = -1, wr_err = 0;
    int rd = -1, rd_err = 0;

    // 以只读方式打开，再尝试写入，应失败并返回错误码。
    int ro_fd = open(path, O_RDONLY);
    if(ro_fd >= 0) {
        wr = write(ro_fd, "y", 1); // 试图写只读文件
        wr_err = errno;
        close(ro_fd);
    }

    // 以只写方式打开，再尝试读取，同样要求失败。
    int wo_fd = open(path, O_WRONLY);
    if(wo_fd >= 0) {
        char tmp[4];
        rd = read(wo_fd, tmp, sizeof(tmp)); // 试图读只写文件
        rd_err = errno;
        close(wo_fd);
    }

    unlink(path);
    printf("perm %d %d %d %d\n", wr, wr_err, rd, rd_err);
}

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

