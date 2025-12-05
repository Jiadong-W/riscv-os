#include "memlayout.h"

//寄存器操作的宏
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define THR 0   // 发送寄存器
#define IER 1    //中断启用寄存器
#define FCR 2   //FIFO控制寄存器
#define LCR 3   //线路控制寄存器
#define LSR 5   // 状态寄存器
#define LSR_THRE 0x20         // THR空，准备发送

#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) 
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // 用于设置波特率的模式

void uartinit(void)
{
  //在初始化过程中禁用中断
  WriteReg(IER, 0x00);

  //进入波特率设置模式并设置波特率
  WriteReg(LCR, LCR_BAUD_LATCH);
  WriteReg(0, 0x03); //LSB
  WriteReg(1, 0x00); //MSB

  // 退出波特率设置模式并设置数据格式：8位数据、无校验
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 启用并清空FIFO
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 启用中断
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
}

// 输出一个字符到串口
void uart_putc(char c) {
    // 等待THR空
    while (!(ReadReg(LSR) & LSR_THRE));
    WriteReg(THR,c);
}

// 输出字符串到串口
void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}