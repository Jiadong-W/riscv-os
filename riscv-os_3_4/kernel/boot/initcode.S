/*
 * initcode.S
 *   - 将用户态 init 程序的裸二进制嵌入内核镜像。
 *   - 二进制由 Makefile 调用 objcopy 从 user/test_syscalls.elf 生成。
 */

    .section .text
    .align 2
    .globl initcode
    .globl initcode_end
    .globl initcode_size

initcode:
    .incbin "user/test_syscalls.bin"   # 直接包含用户程序镜像
initcode_end:

    .section .rodata
    .align 3
initcode_size:
    .quad initcode_end - initcode      # 提供大小，供 userinit 拷贝使用
