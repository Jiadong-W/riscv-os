#include "user.h"

// 本测试程序通过同时启动多类典型进程，观察多级反馈队列（MLFQ）的调度行为。
// 每类进程会打印自身的阶段完成时刻（以 ticks 为单位），便于对比谁获得了优先的 CPU 时间。
// 由于用户态暂未提供真正的 sleep/IO 系统调用，这里通过忙等待模拟计算与思考间歇。

typedef void (*task_entry_t)(void);

// 利用大量算术运算模拟 CPU 计算负载，避免被编译器优化掉。
static void compute_burn(unsigned long loops)
{
  volatile unsigned long acc = 0x12345678u;
  for(unsigned long i = 0; i < loops; i++) {
    acc ^= (acc << 1) + i;
    acc += 0x9e3779b97f4a7c15ULL; // 与黄金比例相关的常数，制造更多数据依赖
  }
  if(acc == 0) {
    printf("[warn] acc=0?\n");
  }
}

// 使用 sleep(ticks) 模拟“思考”或等待 I/O 的阶段，主动让出 CPU。
static void pseudo_wait(uint64_t delta_ticks)
{
  while(delta_ticks > 0) {
    int chunk = (delta_ticks > 0x7fffffffULL) ? 0x7fffffff : (int)delta_ticks;
    if(chunk <= 0)
      break;
    if(sleep(chunk) < 0) {
      printf("[等待] sleep(%d) 失败，errno=%d\n", chunk, errno);
      break;
    }
    delta_ticks -= (uint64_t)chunk;
  }
}

static void log_progress(const char *tag, const char *phase, int round)
{
  uint64_t t = get_ticks();
  int level = get_priority_level();
  printf("[%s] %s #%d 在周期 %lu (优先级=%d)\n", tag, phase, round, (unsigned long)t, level);
}

// 1. 纯 CPU 密集型任务：持续计算且很少主动停顿，容易被 MLFQ 降级。
static void cpu_bound_worker(void)
{
  for(int round = 1; round <= 6; round++) {
    compute_burn(24UL * 1000 * 1000);
    log_progress("CPU密集", "完成重计算", round);
  }
  printf("[CPU密集] 全部阶段完成，准备退出 (ticks=%lu, 优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());
  exit(0);
}

// 2. 互动式任务：执行短计算后快速打印，模拟需要快速响应的前台应用。
static void interactive_worker(void)
{
  for(int round = 1; round <= 12; round++) {
    compute_burn(2UL * 1000 * 1000);
    log_progress("互动任务", "快速响应", round);
    pseudo_wait(5); // 模拟用户思考时间
  }
  printf("[互动任务] 已完成所有响应 (ticks=%lu, 优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());
  exit(0);
}

// 3. 批处理任务：每个阶段计算量大，但阶段之间做短暂整理，模拟后端批量作业。
static void batch_worker(void)
{
  for(int round = 1; round <= 4; round++) {
    compute_burn(12UL * 1000 * 1000);
    log_progress("批处理", "批次结束", round);
    pseudo_wait(3); // 简短的整理/写盘时间
  }
  printf("[批处理] 所有批次执行完毕 (ticks=%lu, 优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());
  exit(0);
}

// 4. 短任务：工作量极小，验证 MLFQ 是否优先快速完成短作业。
static void short_job_worker(void)
{
  compute_burn(1UL * 1000 * 1000);
  log_progress("短作业", "瞬时完成", 1);
  compute_burn(1UL * 1000 * 1000);
  log_progress("短作业", "收尾", 2);
  printf("[短作业] 很快结束 (ticks=%lu, 优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());
  exit(0);
}

// 5. 老化考察任务：先长时间计算被降级，再模拟等待以触发提升。
static void aging_probe_worker(void)
{
  // 第一阶段：连续计算，触发时间片用尽
  for(int i = 0; i < 3; i++) {
    compute_burn(10UL * 1000 * 1000);
  }
  log_progress("老化观察", "初始重计算完成", 1);

  // 第二阶段：长时间等待，等待老化机制将其拉回高优先级
  printf("[老化观察] 进入长等待窗口，观察是否被提升... (当前优先级=%d)\n", get_priority_level());
  pseudo_wait(120);
  log_progress("老化观察", "等待结束", 2);

  // 第三阶段：再次计算，若老化生效应更快获得 CPU
  compute_burn(4UL * 1000 * 1000);
  log_progress("老化观察", "验证阶段", 3);

  printf("[老化观察] 测试完成 (ticks=%lu, 优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());
  exit(0);
}

struct scenario {
  const char *name;
  task_entry_t entry;
  const char *description;
};

static struct scenario scenarios[] = {
  {"CPU密集",      cpu_bound_worker,     "持续占用 CPU，验证降级与较长时间片"},
  {"互动任务",      interactive_worker,   "短计算+频繁打印，验证高优先级保持"},
  {"批处理",        batch_worker,         "长批次+短整理，观察队列切换"},
  {"短作业",        short_job_worker,     "瞬间完成的小任务，应快速结束"},
  {"老化观察",      aging_probe_worker,   "经历降级后等待老化提升"},
};

int main(void)
{
  int total = sizeof(scenarios) / sizeof(scenarios[0]);
  printf("[MLFQ测试] 启动 %d 个子进程，观察多级反馈队列行为\n", total);
  printf("[MLFQ测试] 全局时间起点 ticks=%lu (父进程初始优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());

  int pids[sizeof(scenarios) / sizeof(scenarios[0])];

  for(int i = 0; i < total; i++) {
    int pid = fork();
    if(pid == 0) {
      // 子进程执行自身任务
      printf("[%s] 子进程 PID=%d 启动：%s (初始优先级=%d)\n", scenarios[i].name, getpid(), scenarios[i].description, get_priority_level());
      scenarios[i].entry();
      // entry 内部都会 exit，这里不应返回
      printf("[%s] 意外返回 main()，强制退出\n", scenarios[i].name);
      exit(1);
    } else if(pid > 0) {
      pids[i] = pid;
      printf("[父进程] 已创建 %s | PID=%d (父进程优先级=%d)\n", scenarios[i].name, pid, get_priority_level());
    } else {
      printf("[父进程] 创建 %s 失败，跳过\n", scenarios[i].name);
      pids[i] = -1;
    }
  }

  // 等待所有子进程结束并打印退出顺序
  for(int remain = 0; remain < total; remain++) {
    int status = 0;
    int pid = wait(&status);
    if(pid < 0) {
      printf("[父进程] wait 出错，剩余子进程可能已经退出\n");
      break;
    }
    printf("[父进程] 子进程 PID=%d 结束，退出码=%d，完成时间 ticks=%lu (父进程优先级=%d)\n",
           pid, status, (unsigned long)get_ticks(), get_priority_level());
  }

  printf("[MLFQ测试] 全部任务结束，父进程退出 (ticks=%lu, 优先级=%d)\n", (unsigned long)get_ticks(), get_priority_level());
  exit(0);
}
