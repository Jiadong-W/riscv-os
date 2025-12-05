#include "uart.h"
#include "kalloc.h"
#include "vm.h"
#include "printf.h"
#include "buf.h"
#include "virtio.h"
#include "fs.h"
#include "file.h"
#include "console.h"

#include "trap.h"
#include "proc.h"

int main() {
    uartinit();
    pmm_init();
    kvminit();
    kvminithart();
    trap_init();
    virtio_disk_init();
    bcache_init();
    fs_init();
    fileinit();
    devsw[CONSOLE].write = consolewrite;
    devsw[CONSOLE].read = consoleread;
    procinit();
    userinit();
    
    printf("Starting scheduler...\n");
    scheduler();

    while (1); // 死循环，防止退出
}