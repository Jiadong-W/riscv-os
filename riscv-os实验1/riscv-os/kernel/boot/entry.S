.section .text
.global _entry
_entry:
    /* 1. 设栈指针 */
    la sp, stack_top          /* 提供符号：stack_top（链接脚本里放 end 后 16 KiB） */

    /* 2. 清零 BSS */
    la t0, __bss_start          /* BSS 起始 = __bss_start */
    la t1, end          /* BSS 末尾 = end */
1:
    bgeu t0, t1, 2f     /* t0 >= t1 结束 */
    sd zero, 0(t0)      /* 8 字节清零 */
    addi t0, t0, 8
    j 1b
2:
    /* 3. 跳 C 代码 */
    call start          /* 永不返回 */

/* 4. 死循环保险 */
hang:
    j hang