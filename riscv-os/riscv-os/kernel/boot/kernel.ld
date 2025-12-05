OUTPUT_ARCH("riscv")
ENTRY(_entry)

PHDRS {
    text PT_LOAD FLAGS(5); /* RX */
    rodata PT_LOAD FLAGS(4); /* R*/
    data PT_LOAD FLAGS(6); /* RW*/
}

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }:text

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) 
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }:rodata

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) 
    . = ALIGN(16);
    *(.data .data.*)
  }:data

  .bss : {
    . = ALIGN(16);
    PROVIDE(_bss_start = .);
    *(.sbss .sbss.*)
    . = ALIGN(16);
    *(.bss .bss.*)
    PROVIDE(_bss_end = .);
  }:data
  
  PROVIDE(end = .);
}