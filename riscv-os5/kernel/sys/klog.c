#include <stdarg.h>

#include "types.h"
#include "spinlock.h"
#include "string.h"
#include "printf.h"
#include "klog.h"

// ========================= 最小化内核日志实现 =========================
// 设计说明（单核场景）：
// 1. 使用一把自旋锁串行化日志写入，保证并发安全。
// 2. 采用固定大小的环形缓冲区保存最新的日志文本，超出容量时覆盖最旧条目。
// 3. 每条日志记录时间戳（ticks）、级别和预格式化好的字符串，便于快速打印。
// 4. 为了保持实现轻量，我们自定义了一个简单的 snprintf，仅支持核心占位符。
// 5. 默认保留所有级别的日志，但只将 WARN 及以上级别同步输出到控制台，以免刷屏。

// -------------------------- 配置常量区域 ---------------------------
// 单条日志允许的最大字符数（包含结尾的 '\0'）。
#define KLOG_LINE_MAX    128
// 环形缓冲区中的最大日志条目数量。
#define KLOG_CAPACITY     64

// 日志条目格式：记录时间戳、级别和预格式化文本。
typedef struct {
  uint64 timestamp;                // 记录写入日志时的 ticks 值，用于定位时间
  klog_level_t level;              // 日志级别
  char message[KLOG_LINE_MAX];     // 预先格式化好的日志文本
} klog_entry_t;

// 环形缓冲区状态。
typedef struct {
  struct spinlock lock;            // 串行化访问的自旋锁
  klog_entry_t entries[KLOG_CAPACITY];
  int head;                        // 指向下一条可写入位置的索引
  int count;                       // 当前已存储的日志条目数量（不会超过 KLOG_CAPACITY）
  klog_level_t record_threshold;   // 写入缓存的最低级别（数值越小越严格）
  klog_level_t console_threshold;  // 同步打印到控制台的最低级别
} klog_state_t;

static klog_state_t g_klog;

// ticks 由时钟中断递增，这里仅引用即可。
extern volatile uint64 ticks;

// 日志级别名称表，便于打印时展示人类可读信息。
static const char *const klog_level_name[] = {
  "ERROR",
  "WARN",
  "INFO",
  "DEBUG"
};

// ---------------------- 内部格式化辅助函数 -----------------------
// 为了避免依赖完整的 printf，我们实现一个仅支持核心格式的迷你版。
// 支持：%d、%u、%x、%p、%s、%c、%%，足以覆盖调试场景。

// 简单的写缓冲结构，负责在数组范围内安全写入字符。
typedef struct {
  char *buf;
  int capacity;
  int index;
} klog_buf_t;

static void klog_buf_putc(klog_buf_t *dst, char c)
{
  if(dst->index < dst->capacity - 1) {  // 预留 1 字节用于结尾的 '\0'
    dst->buf[dst->index++] = c;
  }
}

static void klog_write_unsigned(klog_buf_t *dst, unsigned long long x, int base)
{
  static const char digits[] = "0123456789abcdef";
  char tmp[32];
  int p = 0;

  if(x == 0) {
    klog_buf_putc(dst, '0');
    return;
  }

  while(x != 0 && p < (int)sizeof(tmp)) {
    tmp[p++] = digits[x % (unsigned long long)base];
    x /= (unsigned long long)base;
  }

  while(p > 0) {
    klog_buf_putc(dst, tmp[--p]);
  }
}

static void klog_vsnprintf(char *out, int out_sz, const char *fmt, va_list ap)
{
  klog_buf_t buf = {
    .buf = out,
    .capacity = out_sz,
    .index = 0,
  };

  for(int i = 0; fmt && fmt[i]; i++) {
    char c = fmt[i];
    if(c != '%') {
      klog_buf_putc(&buf, c);
      continue;
    }

    // 处理占位符
    char spec = fmt[++i];
    if(spec == 0) {
      break;  // 字符串提前结束
    }

    switch(spec) {
    case 'd': {
      int val = va_arg(ap, int);
      if(val < 0) {
        klog_buf_putc(&buf, '-');
        klog_write_unsigned(&buf, (unsigned long long)(-(long long)val), 10);
      } else {
        klog_write_unsigned(&buf, (unsigned long long)val, 10);
      }
      break;
    }
    case 'u': {
      unsigned int val = va_arg(ap, unsigned int);
      klog_write_unsigned(&buf, (unsigned long long)val, 10);
      break;
    }
    case 'x': {
      unsigned int val = va_arg(ap, unsigned int);
      klog_write_unsigned(&buf, (unsigned long long)val, 16);
      break;
    }
    case 'p': {
      uint64 val = va_arg(ap, uint64);
      klog_buf_putc(&buf, '0');
      klog_buf_putc(&buf, 'x');
      klog_write_unsigned(&buf, (unsigned long long)val, 16);
      break;
    }
    case 's': {
      const char *str = va_arg(ap, const char*);
      if(str == 0) {
        str = "(null)";
      }
      while(*str) {
        klog_buf_putc(&buf, *str++);
      }
      break;
    }
    case 'c': {
      int ch = va_arg(ap, int);
      klog_buf_putc(&buf, (char)ch);
      break;
    }
    case '%': {
      klog_buf_putc(&buf, '%');
      break;
    }
    default:
      // 未识别的格式化字符，原样输出，方便排查
      klog_buf_putc(&buf, '%');
      klog_buf_putc(&buf, spec);
      break;
    }
  }

  // 确保字符串以 '\0' 结尾
  buf.buf[(buf.index < buf.capacity) ? buf.index : (buf.capacity - 1)] = '\0';
}

// -------------------------- 对外可见函数 --------------------------

void klog_init(void)
{
  initlock(&g_klog.lock, "klog");
  g_klog.head = 0;
  g_klog.count = 0;
  g_klog.record_threshold = KLOG_LEVEL_DEBUG;  // 默认记录所有信息
  g_klog.console_threshold = KLOG_LEVEL_WARN;  // 控制台仅输出警告及以上
}

void klog_set_threshold(klog_level_t record_level, klog_level_t console_level)
{
  acquire(&g_klog.lock);
  g_klog.record_threshold = record_level;
  g_klog.console_threshold = console_level;
  release(&g_klog.lock);
}

void klog_log(klog_level_t level, const char *fmt, ...)
{
  if(level > g_klog.record_threshold) {
    return; // 级别低于记录阈值则直接忽略
  }

  va_list ap;
  va_start(ap, fmt);

  char local_msg[KLOG_LINE_MAX];
  klog_vsnprintf(local_msg, sizeof(local_msg), fmt, ap);
  va_end(ap);

  acquire(&g_klog.lock);

  klog_entry_t *slot = &g_klog.entries[g_klog.head];
  slot->timestamp = ticks;
  slot->level = level;
  safestrcpy(slot->message, local_msg, sizeof(slot->message));

  g_klog.head = (g_klog.head + 1) % KLOG_CAPACITY;
  if(g_klog.count < KLOG_CAPACITY) {
    g_klog.count++;
  }

  klog_level_t console_threshold = g_klog.console_threshold;
  release(&g_klog.lock);

  // 根据阈值决定是否同步打印到控制台
  if(level <= console_threshold) {
    printf("[KLOG][%s][%lu] %s\n",
           klog_level_name[level], (uint64)ticks, local_msg);
  }
}

void klog_dump(void)
{
  acquire(&g_klog.lock);

  int entries_to_show = g_klog.count;
  int start = (g_klog.head - g_klog.count + KLOG_CAPACITY) % KLOG_CAPACITY;

  for(int i = 0; i < entries_to_show; i++) {
    int idx = (start + i) % KLOG_CAPACITY;
    klog_entry_t *e = &g_klog.entries[idx];
    printf("[KLOG][%s][%lu] %s\n",
           klog_level_name[e->level], e->timestamp, e->message);
  }

  release(&g_klog.lock);
}
