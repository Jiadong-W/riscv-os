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
	$F/log.o \
	$F/fs.o \
	$F/file.o \
	$S/trampoline.o \
	$S/syscall.o \
	$S/klog.o \
	$S/sysfile.o \
	$S/sysproc.o \
	$S/exec.o \

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

# 用户程序配置 - 在这里添加新的用户程序

# 公共库文件（所有用户程序都会链接这些）
USER_LIB_SRCS = $(U)/ulib.c $(U)/printf.c $(U)/umalloc.c
USER_LIB_OBJS = $(USER_LIB_SRCS:.c=.o)

# 系统调用汇编文件
USER_ASM_SRC = $(U)/usys.S
USER_ASM_OBJ = $(USER_ASM_SRC:.S=.o)

# 用户程序列表 - 只需在这里添加程序名即可
USER_PROGRAMS = init hello sh cowtest mlfqtest klogtest scalltest fstest

# 自动生成用户程序目标文件列表
USER_PROG_SRCS = $(addprefix $(U)/, $(addsuffix .c, $(USER_PROGRAMS)))
USER_PROG_OBJS = $(USER_PROG_SRCS:.c=.o)
USER_PROG_ELFS = $(addprefix $(U)/, $(addsuffix .elf, $(USER_PROGRAMS)))
USER_PROG_BINS = $(addprefix $(U)/, $(addsuffix .bin, $(USER_PROGRAMS)))

# 所有用户程序相关的文件
USER_ALL_OBJS = $(USER_LIB_OBJS) $(USER_ASM_OBJ) $(USER_PROG_OBJS)
USER_ALL_BINS = $(USER_PROG_BINS)

# 文件系统配置
FS_IMG = fs.img

# mkfs工具
MKFS = mkfs
MKFS_SRC = tools/mkfs.c

# 链接器标志：指定内核链接脚本位置
LD = -T $(B)/kernel.ld

.PHONY: all clean qemu qemu-gdb user mkfs user-programs

all: kernel.elf $(FS_IMG)

# 生成用户态系统调用桩（借助脚本自动同步 SYS_ 定义）
$(U)/usys.S: scripts/gen_usys.pl include/syscall.h
	@echo "  GEN  $@"
	perl scripts/gen_usys.pl

# 用户库文件编译规则
$(U)/%.o: $(U)/%.c
	@echo "  CC   $@"
	$(CC) $(UCFLAGS) -c $< -o $@

$(U)/%.o: $(U)/%.S
	@echo "  CC   $@"
	$(CC) $(UCFLAGS) -c $< -o $@

# 用户程序链接规则 - 自动为每个程序生成链接规则
define USER_PROG_TEMPLATE
$(U)/$(1).elf: $(U)/$(1).o $(USER_LIB_OBJS) $(USER_ASM_OBJ) $(U)/user.ld
	$(CC) $(UCFLAGS) -T $(U)/user.ld -o $$@ $(U)/$(1).o $(USER_LIB_OBJS) $(USER_ASM_OBJ)

$(U)/$(1).bin: $(U)/$(1).elf
	$(OBJCOPY) -O binary $$< $$@
endef

# 为每个用户程序应用模板
$(foreach prog,$(USER_PROGRAMS),$(eval $(call USER_PROG_TEMPLATE,$(prog))))

# 构建所有用户程序
user-programs: $(USER_PROG_ELFS)

user: user-programs


# initcode 依赖最新的用户态镜像
$(B)/initcode.o: $(U)/init.bin

# 内核源文件编译规则
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

# 链接内核
kernel.elf: $(OBJS) $(B)/kernel.ld
	$(CC) $(CFLAGS) $(LD) $(OBJS) -o $@


# 构建mkfs工具 - 使用主机gcc
$(MKFS): $(MKFS_SRC)
	gcc -Wall -O2 -o $(MKFS) $(MKFS_SRC)

# 创建文件系统镜像 - 使用ELF文件而不是二进制文件
$(FS_IMG): $(MKFS) $(USER_PROG_ELFS)
	./$(MKFS) $(FS_IMG) $(USER_PROG_ELFS)

clean:
	rm -f kernel.elf $(OBJS) $(FS_IMG) $(USER_ALL_OBJS) $(USER_PROG_ELFS) $(USER_ALL_BINS) $(U)/usys.S $(MKFS)

# 在 QEMU 中运行内核
qemu: kernel.elf $(FS_IMG)
	qemu-system-riscv64 -machine virt -nographic -bios none -kernel kernel.elf -drive file=$(FS_IMG),if=none,format=raw,id=x0 -global virtio-mmio.force-legacy=off \
        -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu-gdb: kernel.elf $(FS_IMG)
	qemu-system-riscv64 -machine virt -nographic -bios none -kernel kernel.elf -drive file=$(FS_IMG),if=none,format=raw,id=x0 -global virtio-mmio.force-legacy=off \
        -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 -s -S
