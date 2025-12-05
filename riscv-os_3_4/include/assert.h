#pragma once

#include "printf.h"

// 断言宏：条件不成立时输出错误信息并进入死循环
#define assert(expr) \
    ((expr) ? (void)0 : panic("assertion failed: " #expr " at " __FILE__ ":" _STR(__LINE__)))

#define _STR(x) _VAL(x)
#define _VAL(x) #x