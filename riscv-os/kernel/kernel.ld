OUTPUT_ARCH("riscv")
ENTRY(_entry)

SECTIONS
{
  . = 0x80000000;
  .text : {
    *(.text .text.*)
    PROVIDE(etext = .);
  }

  . = 0x80010000;
  .data : { 
    *(.data .data.*)
  }

  /* ---- 新增 BSS 符号 ---- */
  . = 0x80014000;
  .bss : {
    . = ALIGN(16);
    PROVIDE(__bss_start = .);   /* entry.S 用 */
    *(.bss .bss.*)
    PROVIDE(end = .);           /* 内核映像末尾 */
  }

  /* ---- 新增栈顶符号 ---- */
  . = ALIGN(16);
  PROVIDE(stack_top = . + 0x4000); /* 16 KiB 向下增长栈 */
}