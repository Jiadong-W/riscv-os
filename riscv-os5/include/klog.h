#pragma once

#include "types.h"

// ================= 内核日志接口 =================
// 本文件提供一个极简的日志记录框架：
// 1. 通过枚举定义日志级别（错误、警告、信息、调试）。
// 2. 提供初始化、阈值调整、日志写入与内容转储四个 API。
// 3. 通过便捷宏快速记录不同级别的日志。
//
// 设计目标：
// - 仅服务于单核内核环境，使用一把自旋锁串行化访问即可。
// - 默认记录所有日志，但只将警告及以上级别同步输出到控制台。
// - 保持实现轻量级，易于在后续迭代中扩展。

// 日志级别按照严重程度从高到低排列，数值越小表示问题越严重。
typedef enum {
  KLOG_LEVEL_ERROR = 0,  // 严重错误：立即影响系统运行，需要马上处理
  KLOG_LEVEL_WARN  = 1,  // 警告：潜在问题或异常情况
  KLOG_LEVEL_INFO  = 2,  // 一般信息：用于记录关键状态变更
  KLOG_LEVEL_DEBUG = 3   // 调试信息：高频、详细的辅助日志
} klog_level_t;

// 初始化日志系统。开机时调用一次即可，后续即可安全记录日志。
void klog_init(void);

// 调整日志阈值：只有 level <= record_level 的日志才会写入缓存，
// level <= console_level 的日志会额外同步打印到控制台。
void klog_set_threshold(klog_level_t record_level, klog_level_t console_level);

// 核心日志写入接口，fmt 的格式化语法与 printf 保持一致（支持 %d/%u/%x/%p/%s/%c/%%）。
void klog_log(klog_level_t level, const char *fmt, ...);

// 将当前缓冲区内的日志按时间顺序打印到控制台，便于人工快速查看。
void klog_dump(void);

// 便捷宏：在代码中直接使用，自动填充日志级别参数。
#define klog_error(...) klog_log(KLOG_LEVEL_ERROR, __VA_ARGS__)
#define klog_warn(...)  klog_log(KLOG_LEVEL_WARN,  __VA_ARGS__)
#define klog_info(...)  klog_log(KLOG_LEVEL_INFO,  __VA_ARGS__)
#define klog_debug(...) klog_log(KLOG_LEVEL_DEBUG, __VA_ARGS__)
