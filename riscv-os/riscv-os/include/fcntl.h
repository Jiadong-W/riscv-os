#pragma once

// open 系统调用的标志位，需在内核与用户态之间保持一致
#define O_RDONLY  0x000  // 只读模式
#define O_WRONLY  0x001  // 只写模式  
#define O_RDWR    0x002  // 读写模式
#define O_CREATE  0x200  // 如果文件不存在则创建
