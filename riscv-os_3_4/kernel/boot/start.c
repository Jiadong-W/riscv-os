#include "uart.h"
#include "console.h"
#include "printf.h"
#include "kalloc.h"
#include "vm.h"
#include "assert.h"

void main();
void timerinit();

__attribute__ ((aligned (16))) char stack0[4096];

void start()
{
  // 1. 设置特权级和返回地址
  //设置 mstatus 的 MPP 字段为 S-mode，表示 mret 返回后进入 S-mode（超级模式）。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  //设置 mepc 为 main，mret 返回时跳转到 main()。
  w_mepc((uint64)main);

  //关闭虚拟内存分页，保证早期代码能直接访问物理地址。
  w_satp(0);

  //将所有中断和异常委托给 S-mode 处理，内核可以直接响应。
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  //允许 S-mode 访问全部物理内存。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  //设置定时器中断
  timerinit();

  //切换到 S-mode 并跳转 main
  asm volatile("mret");
}

void timerinit()
{
  // 1. 使能 supervisor-mode 的定时器中断
  w_mie(r_mie() | MIE_STIE);

  // 2. 使能 sstc 扩展（允许使用 stimecmp）
  w_menvcfg(r_menvcfg() | (1L << 63));

  // 3. 允许 S-mode 访问 stimecmp 和 time 寄存器
  w_mcounteren(r_mcounteren() | 2);

  // 4. 设置下一个定时器中断的时间点
  w_stimecmp(r_time() + 1000000);
}