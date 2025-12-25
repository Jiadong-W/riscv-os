#include "user.h"

#define PAGE_SIZE 4096
#define TEST_PAGES 1024   // 约 4 MB 内存，便于放大差异
#define ITERATIONS 32     // 每种场景循环次数

static char *buffer;

static void touch_all_pages(void) {
    for (int i = 0; i < TEST_PAGES * PAGE_SIZE; i += PAGE_SIZE) {
        buffer[i] = (char)(i & 0x7f);
    }
}

static void run_scenario(const char *label, int child_writes) {
    uint64_t total = 0;
    uint64_t worst = 0;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint64_t start = get_time();
        int pid = fork();
        if (pid < 0) {
            printf("forktest: fork 失败\n");
            exit(-1);
        }

        if (pid == 0) {
            if (child_writes) {
                // 子进程写入每一页，强制触发写时复制
                for (int i = 0; i < TEST_PAGES * PAGE_SIZE; i += PAGE_SIZE) {
                    buffer[i] ^= 0x1;
                }
            } else {
                // 只读一次，验证共享路径
                volatile char guard = buffer[0];
                (void)guard;
            }
            exit(0);
        }

        wait(0);
        uint64_t delta = get_time() - start;
        total += delta;
        if (delta > worst)
            worst = delta;
    }

    printf("forktest: %s 平均耗时 %lu us, 最慢 %lu us\n",
           label, total / ITERATIONS, worst);
}

int main(void) {
    printf("forktest: COW 对比测试开始 (总内存=%d KB)\n",
           (TEST_PAGES * PAGE_SIZE) / 1024);

    buffer = (char *)malloc(TEST_PAGES * PAGE_SIZE);
    if (buffer == 0) {
        printf("forktest: malloc 失败\n");
        exit(-1);
    }

    // 预热所有页面，避免首次缺页噪声
    touch_all_pages();

    run_scenario("子进程仅读取", 0);
    run_scenario("子进程逐页写入", 1);

    free(buffer);
    printf("forktest: 测试完成\n");
    exit(0);
}
