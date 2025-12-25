#define UART0   0x10000000L
#define VIRTIO0 0x10001000L

// 内核预期RAM可用于内核和用户页面
// 从物理地址0x80000000到PHYSTOP
#define KERNBASE 0x80000000L // 内核起始物理地址
#define PHYSTOP (KERNBASE + 128*1024*1024) // 内核可用物理内存上限(假设128MB内存)

// 将 trampoline 页面映射到用户和内核空间的最高虚拟地址处。
// trampoline 用于用户态和内核态切换（如系统调用返回）。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 在 trampoline 页面之下为每个进程分配内核栈，
// 每个内核栈之间用无效的保护页隔开，防止栈溢出。
// KSTACK(p) 计算第 p 个进程的内核栈虚拟地址。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 用户内存布局（从地址 0 开始）：
//   - 代码段（text）
//   - 初始数据和 bss 段
//   - 固定大小的用户栈
//   - 可扩展的堆
//   - ...
//   - TRAPFRAME（用于保存用户寄存器，供 trampoline 使用）
//   - TRAMPOLINE（与内核空间共享的 trampoline 页面）
#define TRAPFRAME (TRAMPOLINE - PGSIZE)