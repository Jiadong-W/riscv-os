#pragma once

#ifndef __ASSEMBLER__

#include "types.h"

// MSTATUS 寄存器相关宏定义
#define MSTATUS_MPP_MASK (3L << 11) // 异常发生前的特权级别掩码位
#define MSTATUS_MPP_M (3L << 11)    // 异常发生前处于机器模式
#define MSTATUS_MPP_S (1L << 11)    // 异常发生前处于监管模式  
#define MSTATUS_MPP_U (0L << 11)    // 异常发生前处于用户模式

// mstatus: 机器模式状态寄存器，控制全局中断使能、权限模式等系统级状态
static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x;
}

static inline void 
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// mepc: 机器异常程序计数器，保存发生异常时的指令地址
static inline void 
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x));
}

// SSTATUS 寄存器相关宏定义
#define SSTATUS_SPP (1L << 8)  // 异常发生前的特权级别：1=监管模式，0=用户模式
#define SSTATUS_SPIE (1L << 5) // 异常发生前监管模式下的中断使能状态
#define SSTATUS_UPIE (1L << 4) // 异常发生前用户模式下的中断使能状态
#define SSTATUS_SIE (1L << 1)  // 当前监管模式下的中断使能位
#define SSTATUS_UIE (1L << 0)  // 当前用户模式下的中断使能位

// sstatus: 监管模式状态寄存器，监管模式下的状态和控制信息
static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) );
  return x;
}

static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

// sip: 监管中断等待寄存器，显示当前等待处理的中断
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) );
  return x;
}

static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x));
}

// SIE 寄存器相关宏定义
#define SIE_SEIE (1L << 9) // 外部中断使能位
#define SIE_STIE (1L << 5) // 定时器中断使能位
#define SIE_SSIE (1L << 1) // 软件中断使能位

// sie: 监管中断使能寄存器，控制哪些中断在监管模式下可以触发
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) );
  return x;
}

static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x));
}

// scounteren: 控制用户态是否可以访问 cycle/time/instret 等计数器
#define SCOUNTEREN_CY (1L << 0)
#define SCOUNTEREN_TM (1L << 1)
#define SCOUNTEREN_IR (1L << 2)

static inline uint64
r_scounteren()
{
  uint64 x;
  asm volatile("csrr %0, scounteren" : "=r" (x) );
  return x;
}

static inline void 
w_scounteren(uint64 x)
{
  asm volatile("csrw scounteren, %0" : : "r" (x));
}

// MIE 寄存器相关宏定义
#define MIE_STIE (1L << 5) // 机器模式定时器中断使能位

// mie: 机器中断使能寄存器，控制哪些中断在机器模式下可以触发
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );
  return x;
}

static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}

// sepc: 监管异常程序计数器，保存监管模式下发生异常时的指令地址
static inline void 
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) );
  return x;
}

// medeleg: 机器异常委托寄存器，控制哪些异常从机器模式委托到监管模式处理
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) );
  return x;
}

static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

// mideleg: 机器中断委托寄存器，控制哪些中断从机器模式委托到监管模式处理
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) );
  return x;
}

static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

// stvec: 监管异常向量基址寄存器，设置监管模式下异常处理程序的入口地址
static inline void 
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) );
  return x;
}

// stimecmp: 监管模式定时器比较寄存器，用于设置定时器中断的比较值
static inline uint64
r_stimecmp()
{
  uint64 x;
  // asm volatile("csrr %0, stimecmp" : "=r" (x) );
  asm volatile("csrr %0, 0x14d" : "=r" (x) );
  return x;
}

static inline void 
w_stimecmp(uint64 x)
{
  // asm volatile("csrw stimecmp, %0" : : "r" (x));
  asm volatile("csrw 0x14d, %0" : : "r" (x));
}

// menvcfg: 机器环境配置寄存器，配置非特权指令的行为和环境
static inline uint64
r_menvcfg()
{
  uint64 x;
  // asm volatile("csrr %0, menvcfg" : "=r" (x) );
  asm volatile("csrr %0, 0x30a" : "=r" (x) );
  return x;
}

static inline void 
w_menvcfg(uint64 x)
{
  // asm volatile("csrw menvcfg, %0" : : "r" (x));
  asm volatile("csrw 0x30a, %0" : : "r" (x));
}

// pmpcfg0: 物理内存保护配置寄存器 0，设置前16个PMP区域的访问权限
static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

// pmpaddr0: 物理内存保护地址寄存器 0，设置第一个PMP区域的地址范围
static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r" (x));
}

// Sv39 模式的 SATP 配置（RISC-V 三级页表模式）
#define SATP_SV39 (8L << 60)

// 构造 SATP 寄存器的值：高位指定模式，中间是页表物理地址（去掉低12位）
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// 写入页表基址寄存器 (satp)
// satp: 监管地址转换和保护寄存器，控制虚拟内存系统的根页表地址和分页模式
static inline void 
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r" (x));
}

static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x) );
  return x;
}

// scause: 监管异常原因寄存器，记录监管模式下发生异常或中断的具体原因
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) );
  return x;
}

// stval: 监管异常值寄存器，在发生某些异常时存储附加信息（如错误地址）
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) );
  return x;
}

// mcounteren: 机器计数器使能寄存器，控制哪些硬件计数器在低权限模式下可访问
static inline void 
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r" (x));
}

static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) );
  return x;
}

// time: 时间计数器寄存器，提供从系统启动开始的时间周期计数
static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r" (x) );
  return x;
}

// 开启设备中断（设置 SSTATUS_SIE 位）
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// 关闭设备中断（清除 SSTATUS_SIE 位）
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 检查设备中断是否开启
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

// 读取栈指针寄存器 (sp)
static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}

// 读取返回地址寄存器 (ra)
static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) );
  return x;
}

// 刷新 TLB（转换后备缓冲器）
// sfence.vma: 虚地址管理指令，用于同步虚拟内存地址转换的变更
static inline void
sfence_vma()
{
  asm volatile("sfence.vma zero, zero");
}

// 类型定义
typedef uint64 pte_t; // 每个页表项是64位长
typedef uint64* pagetable_t; // 指向一个页表的指针（一个页表包含512个PTE）

#endif

// 页大小和偏移量相关宏
#define PGSIZE 4096 // 页面大小：4096字节
#define PGSHIFT 12  // 页内偏移位数 (2^12 = 4096)

// 地址对齐宏
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1)) // 向上对齐到页面边界
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))           // 向下对齐到页面边界

// 页表项（PTE）标志位
#define PTE_V (1L << 0) // 有效位：指示页表项是否包含有效映射
#define PTE_R (1L << 1) // 可读权限位
#define PTE_W (1L << 2) // 可写权限位
#define PTE_X (1L << 3) // 可执行权限位
#define PTE_U (1L << 4) // 用户模式可访问位
#define PTE_COW (1L << 8) // 写时复制标记位

// 将物理地址转换为页表条目格式：右移12位去掉页内偏移，再左移10位为标志位留出空间
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

// 从页表条目中提取物理地址：右移10位去掉标志位，再左移12位恢复页内偏移
#define PTE2PA(pte) (((pte) >> 10) << 12)

// 提取页表条目的标志位部分（低10位）
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// 页表索引掩码：9位（每个页表有2^9 = 512个条目)
#define PXMASK          0x1FF 

// 计算指定层级页表索引在虚拟地址中的移位量
// 基础偏移12位（页内偏移），每上升一级增加9位
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))

// 从64位虚拟地址中提取指定层级的页表索引
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// Sv39虚拟地址空间的最大有效地址定义
// 12位页内偏移 + 3×9位页表索引 = 39位，减1位避免符号扩展
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))