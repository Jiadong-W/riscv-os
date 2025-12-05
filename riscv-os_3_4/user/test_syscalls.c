#include "user.h"

static void assert_fail(const char *expr)
{
    printf("Assertion failed: %s\n", expr);
    exit(1);
}

#define assert(expr) ((expr) ? (void)0 : assert_fail(#expr))

static int strlen(const char *s)
{
    int n = 0;
    while(s && s[n])
        n++;
    return n;
}

static int strcmp(const char *a, const char *b)
{
    while(*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

void test_filesystem_integrity(void)
{
    printf("Testing filesystem integrity…\n");
    // 创建测试文件
    int fd = open("testfile", O_CREATE | O_RDWR);
    assert(fd >= 0);

    // 写入数据
    char buffer[] = "Hello, filesystem!";
    int bytes = write(fd, buffer, strlen(buffer));
    assert(bytes == strlen(buffer));
    close(fd);

    // 重新打开并验证
    fd = open("testfile", O_RDONLY);
    assert(fd >= 0);
    char read_buffer[64];
    bytes = read(fd, read_buffer, sizeof(read_buffer));
    read_buffer[bytes] = '\0';

    assert(strcmp(buffer, read_buffer) == 0);
    close(fd);

    // 删除文件
    assert(unlink("testfile") == 0);

    printf("Filesystem integrity test passed\n");
}

int main(void)
{
    test_filesystem_integrity();
    return 0;
}
