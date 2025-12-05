B = kernel/boot
M = kernel/mm
T = kernel/trap
P = kernel/proc
S = kernel/sys
F = kernel/fs
U = user

#告诉 make 工具，内核需要哪些源文件编译生成的目标文件
OBJS = \
	$B/entry.o \
	$B/initcode.o \
	$B/start.o \
	$B/uart.o \
	$B/console.o \
	$B/printf.o \
	$B/main.o \
	$M/kalloc.o \
	$M/vm.o \
	$M/string.o \
	$T/trap.o \
	$T/kernelvec.o \
	$P/proc.o \
	$P/spinlock.o \
	$P/swtch.o \
	$P/semaphore.o \
	$P/sleeplock.o \
	$F/bio.o \
	$F/virtio_disk.o \
	$F/fs.o \
	$F/file.o \
	$S/trampoline.o \
	$S/syscall.o \
	$S/sysfile.o \
	$S/sysproc.o \

# 自动检测工具链前缀
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-gcc -v >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-gcc -v >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-linux-gnu-gcc -v >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	else echo "*** Error: Couldn't find a riscv64 GCC toolchain." 1>&2; exit 1; fi)
endif

# 设置编译器
CC = $(TOOLPREFIX)gcc
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -march=rv64g -mabi=lp64 -mcmodel=medany -Wall -O2 -nostdlib -nostartfiles -fno-builtin -Iinclude -Iuser -g

# 用户态编译参数：沿用内核 ABI/优化设置，附带用户头文件搜索路径
UCFLAGS = $(filter-out -O2,$(CFLAGS)) -Os

# 用户态程序构建配置：生成 init 程序 test_syscalls，并转换为内核可直接拷贝的二进制
USER_SRCS = $(U)/test_syscalls.c $(U)/ulib.c $(U)/printf.c
USER_ASM  = $(U)/usys.S
USER_OBJS = $(USER_SRCS:.c=.o) $(USER_ASM:.S=.o)
USER_ELF  = $(U)/test_syscalls.elf
USER_BIN  = $(U)/test_syscalls.bin

FS_IMG = fs.img
FS_BLKS = 1024
FS_BLKSIZE = 4096

# 链接器标志：指定内核链接脚本位置
LD = -T $(B)/kernel.ld

.PHONY: all clean qemu qemu-gdb user

all: kernel.elf

# 生成用户态系统调用桩（借助脚本自动同步 SYS_ 定义）
$(U)/usys.S: scripts/gen_usys.pl include/syscall.h
	@echo "  GEN  $@"
	perl scripts/gen_usys.pl

# 用户态 .c/.S -> .o
$(U)/%.o: $(U)/%.c
	$(CC) $(UCFLAGS) -c $< -o $@

$(U)/%.o: $(U)/%.S
	$(CC) $(UCFLAGS) -c $< -o $@

# 链接用户程序并导出纯二进制，供 initcode 嵌入
$(USER_ELF): $(USER_OBJS) $(U)/user.ld
	$(CC) $(UCFLAGS) -T $(U)/user.ld -o $@ $(USER_OBJS)

$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

# initcode 依赖最新的用户态镜像
$(B)/initcode.o: $(USER_BIN)

user: $(USER_BIN)

$(FS_IMG):
	@echo "  GEN  $@"
	@python3 -c "import os; path='$(FS_IMG)'; size=$(FS_BLKS)*$(FS_BLKSIZE); f=open(path, 'wb'); f.truncate(size); f.close()"


# 分别编译每个源文件为 .o 文件
$(B)/%.o: $(B)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.o: $(B)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

$(M)/%.o: $(M)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(T)/%.o: $(T)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(T)/%.o: $(T)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

$(P)/%.o: $(P)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(P)/%.o: $(P)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

$(S)/%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(S)/%.o: $(S)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

$(F)/%.o: $(F)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 链接所有 .o 文件生成最终的内核镜像
kernel.elf: $(OBJS) $(B)/kernel.ld
	$(CC) $(CFLAGS) $(LD) $(OBJS) -o $@

clean:
	rm -f kernel.elf $(OBJS) $(FS_IMG) $(USER_OBJS) $(USER_ELF) $(USER_BIN) $(U)/usys.S

# 在 QEMU 中运行内核（默认会加载最新生成的 init 用户程序）
qemu: kernel.elf $(FS_IMG)
	qemu-system-riscv64 -machine virt -nographic -bios none -kernel kernel.elf -drive file=$(FS_IMG),if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu-gdb: kernel.elf $(FS_IMG)
	qemu-system-riscv64 -machine virt -nographic -bios none -kernel kernel.elf -drive file=$(FS_IMG),if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 -s -S
