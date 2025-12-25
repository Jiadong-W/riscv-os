// init.c: 初始用户级程序 - 系统的第一个用户进程
// 负责初始化系统环境并启动shell

#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "user.h"

// Shell程序的启动参数
char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  // 初始化控制台设备
  if(open("console", O_RDWR) < 0){
    mknod("console", CONSOLE, 0,T_DEV);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf("init: starting shell\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      // 子进程：执行shell程序
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    // 父进程（init）：监控子进程状态
    for(;;){
      // wait()调用会返回当shell退出或者孤儿进程退出时
      wpid = wait((int *) 0);
      if(wpid == pid){
        // shell进程退出，需要重新启动
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // 其他孤儿进程退出，不需要特殊处理
      }
    }
  }
}
