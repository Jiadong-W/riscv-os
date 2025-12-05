#include "types.h"

// 中断处理相关
void trap_init(void);
void usertrap(void);
void usertrapret(void);
void register_interrupt(int irq, void (*handler)(void));
void enable_interrupt(int irq);
void disable_interrupt(int irq);
void kerneltrap(void);
void timer_interrupt_handler(void);
uint64 get_time(void);
void sbi_set_timer(uint64 time);

//异常相关
void handle_exception(void);
void handle_instruction_page_fault(void);
void test_exception_handling(void);
void handle_illegal_instruction(void);
void handle_breakpoint_exception(void);
void handle_data_page_fault(uint64 cause);
void handle_instruction_page_fault(void);


// 测试函数
void test_timer_interrupt(void);
void test_software_interrupt(void);
void test_nested_interrupts(void);
void test_interrupt_overhead(void);