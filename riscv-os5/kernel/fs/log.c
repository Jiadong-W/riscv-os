#include "types.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"
#include "log.h"
#include "string.h"
#include "printf.h"

// ===================== 日志子系统实现 =====================
// 设计遵循 xv6 的写前日志思路：所有对磁盘块的修改都先写入日志区，
// 当事务提交时再一次性复制到目标块，确保崩溃前后数据具有原子性。

struct log_header {
    int n;                     // 日志中有效块的数量
    int block[LOG_SIZE];       // 每个被记录的数据块号
};

struct log_state {
    struct spinlock lock;      // 保护日志状态、支持 begin/end 并发控制
    int start;                 // 日志区起始块号（来自超级块）
    int size;                  // 日志区总块数，用于越界检查
    int outstanding;           // 当前仍在执行的事务数量
    int committing;            // 是否正在提交（提交期间新的 begin 需要等待）
    int dev;                   // 目标设备号
    struct log_header header;  // 内存中的日志头部镜像
};

static struct log_state g_log; // 全局日志状态，仅在本文件内部可见
int crash_stage = 0; // 用于测试崩溃恢复的阶段控制变量

static void read_log_header(void);
static void write_log_header(void);
static void install_transaction(int recovering);
static void commit_transaction(void);
static void write_log_blocks(void);

// ===================== 公共接口 =====================

// log_init: 在文件系统初始化阶段调用，准备日志区并执行一次崩溃恢复。
void log_init(int dev, struct superblock *sb)
{
    if(sizeof(struct log_header) > BLOCK_SIZE)
        panic("log_init: header too large");

    initlock(&g_log.lock, "log");
    g_log.start = sb->logstart;
    g_log.size = sb->nlog;
    g_log.outstanding = 0;
    g_log.committing = 0;
    g_log.dev = dev;
    g_log.header.n = 0;

    recover_log();
}

// begin_transaction: 文件系统系统调用入口调用，标记事务开始。
void begin_transaction(void)
{
    acquire(&g_log.lock);
    for(;;) {
        if(g_log.committing) {
            sleep(&g_log, &g_log.lock); // 正在提交时需要等待
        } else if(g_log.header.n + (g_log.outstanding + 1) * MAX_OP_BLOCKS > g_log.size) {
            // 预估本事务可能写入的块数，若不足则等待提交释放空间
            sleep(&g_log, &g_log.lock);
        } else {
            g_log.outstanding++;
            release(&g_log.lock);
            break;
        }
    }
}

// end_transaction: 系统调用结束时调用，若是最后一个事务则触发提交。
void end_transaction(void)
{
    int do_commit = 0;

    acquire(&g_log.lock);
    if(g_log.outstanding < 1)
        panic("end_transaction: no outstanding");

    g_log.outstanding--;
    if(g_log.committing)
        panic("end_transaction: already committing");

    if(g_log.outstanding == 0) {
        do_commit = 1;
        g_log.committing = 1;
    } else {
        // 释放等待 begin_transaction 的进程
        wakeup(&g_log);
    }
    release(&g_log.lock);

    if(do_commit) {
        commit_transaction();
        acquire(&g_log.lock);
        g_log.committing = 0;
        wakeup(&g_log);
        release(&g_log.lock);
    }
}

// log_block_write: 取代直接的 bwrite 调用，将缓存块加入当前事务。
void log_block_write(struct buf *bp)
{
    int idx;

    acquire(&g_log.lock);
    if(g_log.header.n >= g_log.size)
        panic("log_block_write: log full");
    if(g_log.header.n >= LOG_SIZE)
        panic("log_block_write: header overflow");
    if(g_log.outstanding < 1)
        panic("log_block_write outside transaction");

    for(idx = 0; idx < g_log.header.n; idx++) {
        if(g_log.header.block[idx] == (int)bp->blockno)
            break; // 吸收重复写：同一块只记录一次
    }

    g_log.header.block[idx] = bp->blockno;
    if(idx == g_log.header.n) {
        bpin(bp);                 // 提交结束前不允许缓存驱逐
        g_log.header.n++;
    }
    release(&g_log.lock);
}

// recover_log: 启动时从日志块恢复可能未完成的事务。
void recover_log(void)
{
    read_log_header();
    install_transaction(1); // recovering=1 表示来自启动恢复
    g_log.header.n = 0;
    write_log_header();
}

// ===================== 内部工具函数 =====================

static void write_log_blocks(void)
{
    for(int i = 0; i < g_log.header.n; i++) {
        // 读取日志数据块
        struct buf *to = bread(g_log.dev, g_log.start + i + 1);
        // 读取原始数据块（包含最新修改）
        struct buf *from = bread(g_log.dev, g_log.header.block[i]);
        // 复制数据到日志区域
        memmove(to->data, from->data, BLOCK_SIZE);
        bwrite(to);  // 写入磁盘日志区域
        brelse(from);
        brelse(to);
    }
}

static void install_transaction(int recovering)
{
    for(int i = 0; i < g_log.header.n; i++) {
        // 从日志区域读取数据
        struct buf *log_bp = bread(g_log.dev, g_log.start + i + 1);
        // 读取目标数据块
        struct buf *dst_bp = bread(g_log.dev, g_log.header.block[i]);
        // 将日志数据复制到目标位置
        memmove(dst_bp->data, log_bp->data, BLOCK_SIZE);
        bwrite(dst_bp);  // 写入实际数据块
        
        if(!recovering)
            bunpin(dst_bp);  // 正常提交：释放pin
        
        brelse(log_bp);
        brelse(dst_bp);
    }
}

static void read_log_header(void)
{
    struct buf *bp = bread(g_log.dev, g_log.start);
    struct log_header *disk_header = (struct log_header *)bp->data;
    g_log.header.n = disk_header->n;
    if(g_log.header.n < 0 || g_log.header.n > LOG_SIZE)
        panic("read_log_header: invalid n");
    for(int i = 0; i < g_log.header.n; i++)
        g_log.header.block[i] = disk_header->block[i];
    brelse(bp);
}

static void write_log_header(void)
{
    struct buf *bp = bread(g_log.dev, g_log.start);
    struct log_header *disk_header = (struct log_header *)bp->data;
    // 同步内存日志头到磁盘
    disk_header->n = g_log.header.n;
    for(int i = 0; i < g_log.header.n; i++)
        disk_header->block[i] = g_log.header.block[i];
    bwrite(bp);
    brelse(bp);
}

static void commit_transaction(void)
{
    if(g_log.header.n == 0)
        return; // 没有实际修改需要提交

    write_log_blocks();       // 先把修改写入日志区
    if(crash_stage == 2) {
        // 模拟在事务提交前崩溃：日志尚未写入磁盘，数据丢失
        return;
    }
    write_log_header();       // 将头部写回磁盘，标记为已提交
    
    if(crash_stage == 1) {
        // 模拟在日志写入后、安装前崩溃：日志已写入但未安装到原位置
        return;
    }

    install_transaction(0);   // 将日志内容复制回原数据块
    g_log.header.n = 0;
    write_log_header();       // 清空头部，表示日志可复用
}
