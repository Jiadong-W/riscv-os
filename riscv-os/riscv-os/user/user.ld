/*
 * 用户态链接脚本：
 *   - 固定入口为 _start，起始虚拟地址 0。
 *   - 将 .text/.rodata/.data/.bss 依次布置，便于内核直接加载到首个物理页。
 */
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : ALIGN(16)
    {
        *(.text.boot)
        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
        *(.sdata*)
    }

    .bss : ALIGN(16)
    {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    }
}
