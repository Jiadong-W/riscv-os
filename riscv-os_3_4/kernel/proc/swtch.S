# 上下文切换
#
#   void swtch(struct context *old, struct context *new);
#
# 保存当前寄存器到 old 指向的内存，恢复 new 指向的寄存器，实现进程/线程切换。

.globl swtch
swtch:
        # 保存当前函数返回地址（ra）到 old->ra
        sd ra, 0(a0)
        # 保存当前栈指针（sp）到 old->sp
        sd sp, 8(a0)
        # 保存所有被调用者保存寄存器（s0~s11）到 old
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        # 从 new 恢复所有寄存器
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        # 跳转到新上下文的返回地址，完成切换
        ret