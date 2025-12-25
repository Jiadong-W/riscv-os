#include "user.h"

int main(void)
{
    printf("[klogtest] 设置日志阈值：记录 DEBUG，控制台 INFO\n");
    if(klog_set_threshold(KLOG_LEVEL_DEBUG, KLOG_LEVEL_INFO) < 0) {
        printf("[klogtest] klog_set_threshold 失败，errno=%d\n", errno);
        return -1;
    }

    printf("[klogtest] 内核日志转储开始\n");
    if(klog_dump() < 0) {
        printf("[klogtest] klog_dump 失败，errno=%d\n", errno);
        return -1;
    }

    printf("[klogtest] 转储完成，可在控制台查看输出\n");
    return 0;
}
