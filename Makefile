CROSS = riscv64-unknown-elf-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJDUMP = $(CROSS)objdump
OBJCOPY = $(CROSS)objcopy

CFLAGS = -march=rv64imafdc -mabi=lp64d -mcmodel=medany -ffreestanding -nostdlib -Wall -Iinclude
LDFLAGS = -T kernel/kernel.ld

SRCS = kernel/boot/entry.S kernel/uart.c kernel/start.c kernel/printf.c
OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

qemu: kernel.elf
	qemu-system-riscv64 -machine virt -bios none -kernel kernel.elf -nographic

clean:
	rm -f $(OBJS) kernel.elf

.PHONY: qemu clean