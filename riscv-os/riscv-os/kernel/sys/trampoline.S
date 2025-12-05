# ---------------------------------------------------------------
# 用户态与内核态切换的低级代码
# 该代码页被内核和用户空间都映射到同一虚拟地址（TRAMPOLINE）
# 主要负责：
#   - 用户态陷入内核时保存用户寄存器到 trapframe，并切换到内核页表
#   - 内核返回用户态时恢复用户寄存器，并切换到用户页表
# ---------------------------------------------------------------
#include "riscv.h"
#include "memlayout.h"

.section trampsec
.align 4

.globl trampoline
.globl uservec
.globl userret

trampoline:

# ---------------------------------------------------------------
# uservec: 用户态陷入内核时执行的入口（位于 trampoline 页面）
#   - 保存用户寄存器到 TRAPFRAME
#   - 切换到内核页表与内核栈
#   - 跳转到 C 语言实现的 usertrap()
# ---------------------------------------------------------------
uservec:
        csrw sscratch, a0               # 暂存用户 a0，腾出 a0 用于访问 trapframe
        li   a0, TRAPFRAME              # 获取当前进程 trapframe 的固定映射地址

        # 保存通用寄存器到 trapframe
        sd ra,   40(a0)
        sd sp,   48(a0)
        sd gp,   56(a0)
        sd tp,   64(a0)
        sd t0,   72(a0)
        sd t1,   80(a0)
        sd t2,   88(a0)
        sd s0,   96(a0)
        sd s1,  104(a0)
        sd a1,  120(a0)
        sd a2,  128(a0)
        sd a3,  136(a0)
        sd a4,  144(a0)
        sd a5,  152(a0)
        sd a6,  160(a0)
        sd a7,  168(a0)
        sd s2,  176(a0)
        sd s3,  184(a0)
        sd s4,  192(a0)
        sd s5,  200(a0)
        sd s6,  208(a0)
        sd s7,  216(a0)
        sd s8,  224(a0)
        sd s9,  232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3,  256(a0)
        sd t4,  264(a0)
        sd t5,  272(a0)
        sd t6,  280(a0)

        # 保存用户 a0 与 sepc
        csrr t0, sscratch
        sd   t0, 112(a0)                # trapframe->a0
        csrr t1, sepc
        sd   t1, 24(a0)                 # trapframe->epc

        # 读取内核上下文信息（仍在用户页表下，仅取出数值）
        ld   sp, 8(a0)                  # trapframe->kernel_sp
        ld   tp, 32(a0)                 # trapframe->kernel_hartid
        ld   t0, 16(a0)                 # trapframe->kernel_trap (usertrap)
        ld   t1, 0(a0)                  # trapframe->kernel_satp (kernel pagetable)

        sfence.vma zero, zero           # 等待前面内存操作完成
        csrw satp, t1                   # 切换到内核页表
        sfence.vma zero, zero           # 清除旧的用户地址转换

        jalr t0                         # 跳转到 usertrap()

# ---------------------------------------------------------------
# userret: 内核返回用户态时调用（由 usertrapret 跳转）
#   a0: 用户页表根 (satp)
# ---------------------------------------------------------------
userret:
        sfence.vma zero, zero           # 确保之前的内存操作完成
        csrw   satp, a0                 # 切换到用户页表
        sfence.vma zero, zero           # 清空 TLB 中旧的映射

        li     a0, TRAPFRAME            # trapframe 的固定映射地址

        # 恢复全部用户寄存器
        ld ra,   40(a0)
        ld sp,   48(a0)
        ld gp,   56(a0)
        ld tp,   64(a0)
        ld t0,   72(a0)
        ld t1,   80(a0)
        ld t2,   88(a0)
        ld s0,   96(a0)
        ld s1,  104(a0)
        ld a1,  120(a0)
        ld a2,  128(a0)
        ld a3,  136(a0)
        ld a4,  144(a0)
        ld a5,  152(a0)
        ld a6,  160(a0)
        ld a7,  168(a0)
        ld s2,  176(a0)
        ld s3,  184(a0)
        ld s4,  192(a0)
        ld s5,  200(a0)
        ld s6,  208(a0)
        ld s7,  216(a0)
        ld s8,  224(a0)
        ld s9,  232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3,  256(a0)
        ld t4,  264(a0)
        ld t5,  272(a0)
        ld t6,  280(a0)

        ld t0,  24(a0)                 # 取出用户态 PC (epc)
        csrw sepc, t0                  # 准备 sret 返回地址

        ld a0, 112(a0)                 # 恢复用户 a0（返回值）

        sret                           # 返回用户态执行

# ---------------------------------------------------------------
# 说明
# - uservec 在用户页表下工作，先保存寄存器，再切换到内核页表并调用 usertrap。
# - userret 在内核决定返回用户态时执行，恢复寄存器并通过 sret 回到用户程序。
# - TRAPFRAME 页面在每个进程的用户页表中映射到相同虚拟地址，实现共享跳板逻辑。
# ---------------------------------------------------------------