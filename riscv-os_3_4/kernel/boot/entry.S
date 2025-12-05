    .section .text
    .globl _entry
_entry:
    # 设置栈指针
    la sp, stack0
    li a0, 1024*4
    add sp, sp, a0

    # 清零BSS段
    la a0, _bss_start
    la a1, _bss_end
1:
    bge a0, a1, 2f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 1b

2:
    # 跳转到C主函数
    call start

    # 死循环防止退出
spin:  
    j spin