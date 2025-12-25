// kernel/elf.h
#ifndef _ELF_H
#define _ELF_H

// ELF魔数
#define ELF_MAGIC 0x464C457FU  // "\x7FELF"

// 程序头类型
#define PT_LOAD   1

// ELF文件头
struct elfhdr {
  uint magic;
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint64 entry;     // 程序入口地址
  uint64 phoff;     // 程序头表在文件中的偏移
  uint64 shoff;
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum;   // 程序头数量
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// 程序头 (ELF64 结构)
struct proghdr {
  uint type;      // 段类型
  uint flags;     // 权限标志
  uint64 off;     // 段在文件中的偏移
  uint64 vaddr;   // 段应该加载到的虚拟地址
  uint64 paddr;   // 物理地址（通常与虚拟地址相同）
  uint64 filesz;  // 段在文件中的大小
  uint64 memsz;   // 段在内存中的大小
  uint64 align;
};

#endif