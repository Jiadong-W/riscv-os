#include "user.h"

#define BLOCK_SIZE 4096
#define SMALL_FILE_COUNT 200
#define SMALL_PAYLOAD "test"
#define LARGE_FILE_ROUNDS 512

struct fstest_case {
    const char *name;
    int (*fn)(void);
};

static int buffer_equals(const char *a, const char *b, int len)
{
    for(int i = 0; i < len; i++){
        if(a[i] != b[i])
            return 0;
    }
    return 1;
}

static int write_full(int fd, const void *buf, int len)
{
    const char *p = (const char *)buf;
    int written = 0;
    while(written < len){
        int r = write(fd, p + written, len - written);
        if(r < 0)
            return -1;
        written += r;
    }
    return 0;
}

static int read_full(int fd, void *buf, int len)
{
    char *p = (char *)buf;
    int read_bytes = 0;
    while(read_bytes < len){
        int r = read(fd, p + read_bytes, len - read_bytes);
        if(r <= 0)
            return r;
        read_bytes += r;
    }
    return read_bytes;
}

static void format_name(char *dst, const char *prefix, int idx)
{
    int pos = 0;
    while(prefix[pos]){
        dst[pos] = prefix[pos];
        pos++;
    }
    char digits[16];
    int len = 0;
    if(idx == 0)
        digits[len++] = '0';
    while(idx > 0){
        digits[len++] = '0' + (idx % 10);
        idx /= 10;
    }
    while(len > 0)
        dst[pos++] = digits[--len];
    dst[pos] = '\0';
}

static int fail(const char *msg)
{
    printf("    [FAIL] %s (errno=%d)\n", msg, errno);
    return -1;
}

// 手册要求：文件系统完整性测试
static int test_filesystem_integrity(void)
{
    const char *path = "fs_integrity";
    const char *payload = "Hello, filesystem!";
    char buf[64];

    unlink(path);
    int fd = open(path, O_CREATE | O_RDWR);
    if(fd < 0)
        return fail("create integrity file");
    if(write_full(fd, payload, strlen(payload)) < 0){
        close(fd);
        return fail("write integrity payload");
    }
    close(fd);

    fd = open(path, O_RDONLY);
    if(fd < 0)
        return fail("reopen integrity file");
    int n = read_full(fd, buf, strlen(payload));
    if(n != (int)strlen(payload)){
        close(fd);
        return fail("integrity read length mismatch");
    }
    buf[n] = '\0';
    close(fd);

    if(!buffer_equals(buf, payload, strlen(payload))){
        unlink(path);
        return fail("integrity payload mismatch");
    }

    if(unlink(path) < 0)
        return fail("integrity unlink");
    return 0;
}

static int concurrent_worker(int idx, int iterations)
{
    char filename[32];
    format_name(filename, "conc_", idx);

    for(int j = 0; j < iterations; j++){
        int fd = open(filename, O_CREATE | O_RDWR);
        if(fd < 0)
            exit(1);
        if(write_full(fd, &j, sizeof(j)) < 0){
            close(fd);
            exit(1);
        }
        close(fd);
        if(unlink(filename) < 0)
            exit(1);
    }
    exit(0);
}

// 手册要求：并发访问测试
static int test_concurrent_access(void)
{
    const int workers = 4;
    const int iterations = 50;

    printf("    spawning %d concurrent writers\n", workers);
    for(int i = 0; i < workers; i++){
        int pid = fork();
        if(pid < 0)
            return fail("fork concurrent worker");
        if(pid == 0)
            concurrent_worker(i, iterations);
    }

    for(int i = 0; i < workers; i++){
        int status = 0;
        if(wait(&status) < 0)
            return fail("wait concurrent worker");
        if(status != 0)
            return fail("child reported failure");
    }
    return 0;
}

static int simulate_restart(void)
{
    if(clear_cache() < 0)
        return fail("clear_cache");
    if(recover_log() < 0)
        return fail("recover_log");
    return 0;
}

static int verify_file_equals(const char *path, const char *payload)
{
    char buf[64];
    int fd = open(path, O_RDONLY);
    if(fd < 0)
        return fail("open verify file");
    int n = read_full(fd, buf, strlen(payload));
    close(fd);
    if(n != (int)strlen(payload))
        return fail("verify read length mismatch");
    if(!buffer_equals(buf, payload, strlen(payload)))
        return fail("verify payload mismatch");
    return 0;
}

// 手册要求：崩溃恢复测试
static int test_crash_recovery(void)
{
    const char *path = "fs_crash";
    const char *payload = "journal-data";

    unlink(path);
    if(set_crash_stage(1) < 0)
        return fail("set crash stage 1");
    int fd = open(path, O_CREATE | O_RDWR);
    if(fd < 0)
        return fail("create crash file stage1");
    if(write_full(fd, payload, strlen(payload)) < 0){
        close(fd);
        return fail("write crash payload stage1");
    }
    close(fd);
    if(set_crash_stage(0) < 0)
        return fail("restore crash stage from 1");
    if(simulate_restart() < 0)
        return -1;
    if(verify_file_equals(path, payload) < 0){
        unlink(path);
        return -1;
    }
    unlink(path);

    if(set_crash_stage(2) < 0)
        return fail("set crash stage 2");
    fd = open(path, O_CREATE | O_RDWR);
    if(fd < 0)
        return fail("create crash file stage2");
    if(write_full(fd, payload, strlen(payload)) < 0){
        close(fd);
        return fail("write crash payload stage2");
    }
    close(fd);
    if(set_crash_stage(0) < 0)
        return fail("restore crash stage from 2");
    if(simulate_restart() < 0)
        return -1;

    fd = open(path, O_RDONLY);
    if(fd >= 0){
        close(fd);
        unlink(path);
        return fail("stage2 data unexpectedly persisted");
    }
    unlink(path);
    return 0;
}

// 手册要求：性能测试（统计小文件与大文件写入耗时）
static int test_filesystem_performance(void)
{
    uint64_t start = 0;
    uint64_t elapsed_small = 0;
    uint64_t elapsed_large = 0;
    char filename[32];

    // 小文件批量创建/删除
    start = get_time();
    for(int i = 0; i < SMALL_FILE_COUNT; i++){
        format_name(filename, "small_", i);
        int fd = open(filename, O_CREATE | O_RDWR);
        if(fd < 0)
            return fail("open small file");
        if(write_full(fd, SMALL_PAYLOAD, sizeof(SMALL_PAYLOAD) - 1) < 0){
            close(fd);
            return fail("write small file");
        }
        close(fd);
    }
    elapsed_small = get_time() - start;

    start = get_time();
    int fd = open("large_file", O_CREATE | O_RDWR);
    if(fd < 0)
        return fail("open large file");
    char block[BLOCK_SIZE];
    for(int i = 0; i < BLOCK_SIZE; i++)
        block[i] = (char)i;
    for(int i = 0; i < LARGE_FILE_ROUNDS; i++){
        if(write_full(fd, block, sizeof(block)) < 0){
            close(fd);
            return fail("write large block");
        }
    }
    close(fd);
    elapsed_large = get_time() - start;

    printf("    small files (%d x %dB): %lu cycles\n",
           SMALL_FILE_COUNT, (int)sizeof(SMALL_PAYLOAD) - 1, elapsed_small);
    printf("    large file (%dKB): %lu cycles\n",
           (LARGE_FILE_ROUNDS * BLOCK_SIZE) / 1024, elapsed_large);

    for(int i = 0; i < SMALL_FILE_COUNT; i++){
        format_name(filename, "small_", i);
        unlink(filename);
    }
    unlink("large_file");
    return 0;
}

static struct fstest_case cases[] = {
    { "filesystem integrity", test_filesystem_integrity },
    { "concurrent access", test_concurrent_access },
    { "crash recovery", test_crash_recovery },
    { "filesystem performance", test_filesystem_performance },
};

int main(void)
{
    int total = sizeof(cases) / sizeof(cases[0]);
    int failures = 0;

    printf("[fstest] running %d filesystem tests\n", total);
    for(int i = 0; i < total; i++){
        printf("[fstest] CASE %s\n", cases[i].name);
        if(cases[i].fn() == 0)
            printf("[fstest] PASS %s\n", cases[i].name);
        else{
            printf("[fstest] FAIL %s\n", cases[i].name);
            failures++;
        }
    }

    if(failures == 0)
        printf("[fstest] all tests passed\n");
    else
        printf("[fstest] %d/%d tests failed\n", failures, total);
    exit(failures == 0 ? 0 : 1);
}
