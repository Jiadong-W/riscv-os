#define MAXARG 32 // 最大命令行参数数目

int kernel_exec(char *path, char **argv);
int flags2perm(int flags);
uint64 walkaddr(pagetable_t pagetable, uint64 va);