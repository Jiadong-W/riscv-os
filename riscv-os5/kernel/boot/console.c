#include "types.h"
#include "uart.h"
#include "printf.h"
#include "console.h"
#include "proc.h"
#include "vm.h"

// console.c 负责将文件系统/printf 的输出汇聚到 UART，对上层表现为标准字符设备。
// 当前仅实现输出路径（write），输入路径返回错误以提醒调用者。未来若接入键盘
// 或串口接收，只需补齐 consoleread 即可。

// 输出单个字符到控制台。当前直接透传到 UART，后续若接入更复杂的输出设备，
// 该函数是首要替换点。
void console_putc(char c) {
    uart_putc(c);
}

// 输出以 \0 结尾的字符串到控制台。通过逐字节调用 console_putc，保持与底层输出
// 解耦，并为之后可能的缓冲或流控提供空间。
void console_puts(const char *s) {
    while(*s) {
        console_putc(*s++);
    }
}

// 将原始缓冲区内容逐字节写入控制台。相比 printf，这里跳过格式化逻辑，直接
// 发送原始字节，供文件系统写入等场景复用。
void console_write(const char *s, int len) {
    for(int i = 0; i < len; i++) {
        console_putc(s[i]);
    }
}

// 以下三个辅助函数提供简单的 ANSI 转义控制，便于在调试时清屏、移动光标。
void clear_screen(void) {
    printf("\033[2J\033[H");
}

void goto_xy(int x, int y) {
    printf("\033[%d;%dH", y, x);
}

void clear_line(void) {
    printf("\033[K");
}

// consolewrite 是 VFS 写入控制台设备时的入口：
//   - user_src=1: 数据来自用户态地址，需要 copyin 校验并拷贝到内核缓冲；
//   - user_src=0: 内核态地址，可直接读取。
// 为减小单次拷贝开销，使用 128 字节的小缓冲循环处理。
int consolewrite(int user_src, uint64 src, int n)
{
    if(n < 0)
        return -1;
    if(n == 0)
        return 0;

    struct proc *p = myproc();     // 当前进程，用于获取页表执行 copyin。
    int tot = 0;                   // 已写入的字节数。
    char buf[128];                 // 内核暂存缓冲，避免直接访问用户空间。

    while(tot < n) {
        int m = n - tot;
        if(m > (int)sizeof(buf))
            m = sizeof(buf);
        if(user_src) {
            if(p == 0 || copyin(p->pagetable, buf, src + tot, m) < 0)
                return -1;
            console_write(buf, m);
        } else {
            const char *kptr = (const char *)(src + tot);
            console_write(kptr, m);
        }
        tot += m;
    }
    return n;
}

// 从控制台设备读取 n 个字符到 dst（用户或内核空间），返回实际读取字节数
int consoleread(int user_dst, uint64 dst, int n)
{
    if(n <= 0)
        return 0;

    struct proc *p = myproc();
    int i = 0; // 已读取字节数

    while(i < n) {
        int ch = uart_getc();      // 从 UART 设备获取一个字符（阻塞等待输入）
        char c = (char)ch;

        if(c == '\033') { // ESC
            // 读取后续字符，丢弃整个序列
            uart_getc(); // 可能是 '['
            uart_getc(); // 可能是 'A'/'B'/'C'/'D'
            continue;
        }
        if(c == '\r')              // 回车符转换为换行符
            c = '\n';

        // 处理退格键（\b 或 DEL）
        if(c == '\b' || c == 0x7f) {
            if(i > 0) {
                i--;
                console_write("\\b \\b", 3);
            }
            continue;
        }

        // 写入到目标缓冲区
        if(user_dst) {
            // 拷贝到用户空间
            if(p == 0 || copyout(p->pagetable, dst + i, &c, 1) < 0)
                break;             // 拷贝失败则退出
        } else {
            // 拷贝到内核空间
            ((char *)dst)[i] = c;
        }
        // 回显输入字符到控制台
        console_putc(c);
        i++;

        if(c == '\n')
            break;
    }

    return i;
}
