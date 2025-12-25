#include "types.h"
#include "user.h"

// 命令类型定义
#define EXEC  1  // 执行命令

#define MAXARGS 10

// 基础命令结构体
struct cmd {
  int type;
};

// 执行命令结构体
struct execcmd {
  int type;           // 类型为EXEC
  char *argv[MAXARGS]; // 参数数组
  char *eargv[MAXARGS]; // 参数结束位置（用于字符串终止）
};

struct cmd* parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));
void panic(char *s);
int fork1(void);
struct cmd* parseexec(char **ps, char *es);
struct cmd* nulterminate(struct cmd *cmd);


// 简化版命令执行函数
void runcmd(struct cmd *cmd)
{
  struct execcmd *ecmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    // 检查命令是否有效
    if(ecmd->argv[0] == 0)
      exit(1);  // 无命令名
    exec(ecmd->argv[0], ecmd->argv);
    // 如果exec返回，说明执行失败
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;
  }
  exit(0);
}

// 获取用户输入
int getcmd(char *buf, int nbuf)
{
  // 显示提示符
  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int main(void)
{
  static char buf[100];
  int fd;

  // 确保控制台文件描述符正确设置
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // 主循环：读取并执行命令
  while(getcmd(buf, sizeof(buf)) >= 0){
    char *cmd = buf;
    
    // 跳过前导空白字符
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
      
    // 忽略空行
    if (*cmd == '\n')
      continue;
      
    // 处理cd命令（需要在父进程执行）
    if(cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
      // 移除换行符
      cmd[strlen(cmd)-1] = 0;
      if(chdir(cmd+3) < 0) {
        fprintf(2, "cannot cd %s\n", cmd+3);
      }
    } else {
      // 其他命令：fork子进程执行
      if(fork1() == 0)
        runcmd(parsecmd(cmd));
      wait(0);  // 等待子进程结束
    }
  }
  exit(0);
}

void panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

//安全fork函数
int fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

// 构造函数 
struct cmd* execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}


// 字符串分割符号和空白字符定义
char whitespace[] = " \t\r\n\v";  // 空白字符
char symbols[] = "<|>&;()";       // 特殊符号（保留用于后续扩展）

// 获取token的函数
int gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  // 跳过空白字符
  while(s < es && strchr(whitespace, *s))
    s++;
  // 设置token起始位置
  if(q)
    *q = s;
  ret = *s;
  // 处理不同类型的token
  switch(*s){
  case 0:  // 字符串结束
    break;
  default: // 普通字符（命令或参数）
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  // 设置token结束位置
  if(eq)
    *eq = s;

  // 跳过token后的空白字符
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

// 查看下一个字符
int peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  // 跳过空白字符
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

// 主解析函数
struct cmd* parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseexec(&s, es);
  
  // 检查是否解析完整个输入
  peek(&s, es, "");
  if(s != es) {
    fprintf(2, "leftovers: %s\n", s);
    fprintf(2, "syntax error\n");
    exit(1);
  }
  nulterminate(cmd);
  return cmd;
}

// 解析执行命令
struct cmd* parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  // 创建执行命令结构体
  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  
  // 解析所有参数
  while(!peek(ps, es, "|)&;")) {  // 遇到这些符号停止解析
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
      
    if(tok != 'a') {
      // 简化版不支持特殊符号
      panic("syntax");
    }
    
    // 保存参数
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    
    if(argc >= MAXARGS)
      panic("too many args");

  }
  
  // 参数列表以NULL结束
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// 终止字符串（在适当位置添加\0）
struct cmd* nulterminate(struct cmd *cmd)
{
  int i;
  struct execcmd *ecmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0; // 在每个参数结尾添加null终止
    break;
  }
  return cmd;
}
