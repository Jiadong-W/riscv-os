#pragma once

#include "types.h"
#include "spinlock.h"
#include "fs.h"

struct buf;

// 单次事务最多允许保留的块数量，与 LOG_SIZE 配合可粗略估算事务上限
#define MAX_OP_BLOCKS 10

void log_init(int dev, struct superblock *sb);
void begin_transaction(void);
void end_transaction(void);
void log_block_write(struct buf *bp);
void recover_log(void);

extern int crash_stage;
