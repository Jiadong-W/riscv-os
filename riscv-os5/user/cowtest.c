#include "user.h"

#define PAGE_SIZE 4096
#define TEST_PAGES 4

static void fill_pattern(char *buf, int value) {
    for (int i = 0; i < TEST_PAGES * PAGE_SIZE; i++) {
        buf[i] = (char)value;
    }
}

int main(void) {
    printf("cowtest: 写时复制功能验证开始\n");

    char *buf = (char *)malloc(TEST_PAGES * PAGE_SIZE);
    if (buf == 0) {
        printf("cowtest: malloc 失败\n");
        exit(-1);
    }

    fill_pattern(buf, 'A');

    uint64_t start = get_time();
    int pid = fork();
    if (pid < 0) {
        printf("cowtest: fork 失败\n");
        exit(-1);
    }

    if (pid == 0) {
        // 子进程：逐页写入不同的标记，触发写时复制
        for (int i = 0; i < TEST_PAGES; i++) {
            buf[i * PAGE_SIZE] = 'a' + i;
        }
        printf("cowtest: 子进程完成写入，触发 COW\n");
        exit(0);
    }

    int status = 0;
    wait(&status);
    uint64_t end = get_time();

    // 父进程：确认数据仍保持在原始值，证明页被独立复制
    int success = 1;
    for (int i = 0; i < TEST_PAGES; i++) {
        if (buf[i * PAGE_SIZE] != 'A') {
            success = 0;
            printf("cowtest: 发现异常写入，页 %d 数据 = %c\n", i, buf[i * PAGE_SIZE]);
        }
    }

    if (!success || status != 0) {
        printf("cowtest: 失败，status=%d\n", status);
        exit(-1);
    }

    // 父进程写入自己的数据，确保不会触发崩溃
    for (int i = 0; i < TEST_PAGES; i++) {
        buf[i * PAGE_SIZE] = 'P';
    }

    printf("cowtest: 测试通过，fork+写操作耗时 %lu us\n", end - start);

    free(buf);
    exit(0);
}
