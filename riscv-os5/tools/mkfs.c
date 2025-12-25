#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdint.h>

// ============================================================================
// 文件系统常量定义 - 必须与内核中的定义完全一致
// ============================================================================

#define BLOCK_SIZE 4096           // 块大小
#define FS_TOTAL_BLOCKS 2000      // 文件系统总块数

// Inode类型
#define T_DIR  1   // 目录
#define T_FILE 2   // 文件
#define T_DEV  3   // 设备
#define T_SYMLINK 4 // 符号链接

// 目录项大小
#define DIRSIZ 14

// 文件系统魔数
#define FS_MAGIC 0x20241031

// 每个块中的inode数
#define IPB (BLOCK_SIZE / sizeof(struct dinode))

// 每个块中的目录项数  
#define DPB (BLOCK_SIZE / sizeof(struct dirent))

// 每个位图块管理的块数
#define BPB (BLOCK_SIZE * 8)

// 日志块数
#define LOG_SIZE 30

// 最大inode数
#define NINODES 50

// 根inode号
#define ROOTINO 1

// 直接块数
#define NDIRECT 12
// 间接块数
#define NINDIRECT (BLOCK_SIZE / sizeof(uint32_t))
#define NDOUBLE (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLE)

// 文件系统布局常量
#define SUPERBLOCK_BLOCKNO 1
#define SUPERBLOCK_NUM 1
#define LOG_START (SUPERBLOCK_BLOCKNO + SUPERBLOCK_NUM)
#define INODE_START (LOG_START + LOG_SIZE)
#define INODE_BLOCKS 13
#define BMAP_START (INODE_START + INODE_BLOCKS)
#define BMAP_BLOCKS 1
#define DATA_START (BMAP_START + BMAP_BLOCKS)

// ============================================================================
// 文件系统数据结构定义 - 必须与内核中的定义完全一致
// ============================================================================

// 目录项结构
struct dirent {
  uint16_t inum;
  char name[DIRSIZ];
};

// 磁盘inode结构 - 与fs.h完全一致
struct dinode {
  short type;                   // 文件类型
  short major;                  // 设备主编号
  short minor;                  // 设备次编号
  short nlink;                  // 链接数
  uint32_t size;                // 文件大小（字节）
  uint32_t addrs[NDIRECT + 2];  // 数据块地址（直接块 + 一级间接 + 二级间接）
};

// 超级块结构 - 与fs.h完全一致
struct superblock {
  uint32_t magic;        // 魔数
  uint32_t size;         // 文件系统映像大小（块数）
  uint32_t nblocks;      // 数据块数
  uint32_t ninodes;      // inode数
  uint32_t nlog;         // 日志块数
  uint32_t logstart;     // 第一个日志块
  uint32_t inodestart;   // 第一个inode块
  uint32_t bmapstart;    // 第一个位图块
};

// ============================================================================
// 全局变量
// ============================================================================

int nbitmap;              // 位图块数量
int ninodeblocks;         // inode块数量  
int nlog;                 // 日志块数量
int nmeta;                // 元数据块总数
int nblocks;              // 数据块总数

int fsfd;                 // 文件系统镜像文件描述符
struct superblock sb;     // 超级块
char zeroes[BLOCK_SIZE];  // 全零块，用于初始化
uint32_t freeinode = 1;   // 下一个空闲inode编号
uint32_t freeblock;       // 下一个空闲数据块编号

// ============================================================================
// 函数声明
// ============================================================================

void balloc(int used);
void wsect(uint32_t sec, void *buf);
void winode(uint32_t inum, struct dinode *ip);
void rinode(uint32_t inum, struct dinode *ip);
void rsect(uint32_t sec, void *buf);
uint32_t ialloc(short type);
void iappend(uint32_t inum, void *p, int n);
void die(const char *s);

// 计算inode所在的块号
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

// 取最小值
#define min(a, b) ((a) < (b) ? (a) : (b))

// ============================================================================
// 工具函数
// ============================================================================

// 字节序转换：主机字节序 -> 小端字节序
uint16_t xshort(uint16_t x) {
  uint16_t y;
  unsigned char *a = (unsigned char*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint32_t xint(uint32_t x) {
  uint32_t y;
  unsigned char *a = (unsigned char*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

// 错误处理函数
void die(const char *s) {
  perror(s);
  exit(1);
}

// ============================================================================
// 磁盘操作函数
// ============================================================================

// 写入指定扇区
void wsect(uint32_t sec, void *buf) {
  if (lseek(fsfd, sec * BLOCK_SIZE, 0) != sec * BLOCK_SIZE)
    die("lseek");
  if (write(fsfd, buf, BLOCK_SIZE) != BLOCK_SIZE)
    die("write");
}

// 读取指定扇区
void rsect(uint32_t sec, void *buf) {
  if (lseek(fsfd, sec * BLOCK_SIZE, 0) != sec * BLOCK_SIZE)
    die("lseek");
  if (read(fsfd, buf, BLOCK_SIZE) != BLOCK_SIZE)
    die("read");
}

// 写入inode
void winode(uint32_t inum, struct dinode *ip) {
  char buf[BLOCK_SIZE];
  uint32_t bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

// 读取inode
void rinode(uint32_t inum, struct dinode *ip) {
  char buf[BLOCK_SIZE];
  uint32_t bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

// ============================================================================
// 分配函数
// ============================================================================

// 分配inode
uint32_t ialloc(short type) {
  uint32_t inum = freeinode++;
  struct dinode din;

  memset(&din, 0, sizeof(din));
  din.type = type;
  din.major = 0;
  din.minor = 0;
  din.nlink = 1;
  din.size = 0;
  winode(inum, &din);
  return inum;
}

// 分配数据块（在位图中标记已使用的块）
void balloc(int used) {
  unsigned char buf[BLOCK_SIZE];
  int i;

  printf("balloc: 前 %d 个块已被分配\n", used);
  assert(used < BPB);  // 确保使用的块数不超过一个位图块能管理的数量
  memset(buf, 0, BLOCK_SIZE);
  
  // 设置位图中已使用块的位
  for (i = 0; i < used; i++) {
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  
  printf("balloc: 在位图块 %d 写入位图\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

// 向inode追加数据（支持二级间接块）
void iappend(uint32_t inum, void *xp, int n) {
  char *p = (char*)xp;
  uint32_t fbn, off, n1;
  struct dinode din;
  char buf[BLOCK_SIZE];
  uint32_t indirect[NINDIRECT];
  uint32_t double_indirect[NINDIRECT];
  uint32_t x;

  rinode(inum, &din);
  off = din.size;
  
  while (n > 0) {
    fbn = off / BLOCK_SIZE;  // 文件内的块号
    assert(fbn < MAXFILE);
    
    // 直接块
    if (fbn < NDIRECT) {
      if (din.addrs[fbn] == 0) {
        din.addrs[fbn] = freeblock++;
      }
      x = din.addrs[fbn];
    } 
    // 一级间接块
    else if (fbn < NDIRECT + NINDIRECT) {
      if (din.addrs[NDIRECT] == 0) {
        din.addrs[NDIRECT] = freeblock++;
        memset(indirect, 0, sizeof(indirect));
        wsect(din.addrs[NDIRECT], (char*)indirect);
      } else {
        rsect(din.addrs[NDIRECT], (char*)indirect);
      }
      if (indirect[fbn - NDIRECT] == 0) {
        indirect[fbn - NDIRECT] = freeblock++;
        wsect(din.addrs[NDIRECT], (char*)indirect);
      }
      x = indirect[fbn - NDIRECT];
    }
    // 二级间接块
    else {
      uint32_t dindex = (fbn - NDIRECT - NINDIRECT) / NINDIRECT;
      uint32_t sindex = (fbn - NDIRECT - NINDIRECT) % NINDIRECT;
      
      if (din.addrs[NDIRECT + 1] == 0) {
        din.addrs[NDIRECT + 1] = freeblock++;
        memset(double_indirect, 0, sizeof(double_indirect));
        wsect(din.addrs[NDIRECT + 1], (char*)double_indirect);
      } else {
        rsect(din.addrs[NDIRECT + 1], (char*)double_indirect);
      }
      
      if (double_indirect[dindex] == 0) {
        double_indirect[dindex] = freeblock++;
        memset(indirect, 0, sizeof(indirect));
        wsect(double_indirect[dindex], (char*)indirect);
        wsect(din.addrs[NDIRECT + 1], (char*)double_indirect);
      } else {
        rsect(double_indirect[dindex], (char*)indirect);
      }
      
      if (indirect[sindex] == 0) {
        indirect[sindex] = freeblock++;
        wsect(double_indirect[dindex], (char*)indirect);
      }
      x = indirect[sindex];
    }
    
    // 写入数据
    n1 = min(n, (fbn + 1) * BLOCK_SIZE - off);
    rsect(x, buf);
    memcpy(buf + off - (fbn * BLOCK_SIZE), p, n1);
    wsect(x, buf);
    
    n -= n1;
    off += n1;
    p += n1;
  }
  
  // 更新inode大小
  din.size = off;
  winode(inum, &din);
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char *argv[]) {
  int i, cc, fd;
  uint32_t rootino, inum, off;
  struct dirent de;
  char buf[BLOCK_SIZE];
  struct dinode din;

  // 确保整数为4字节
  static_assert(sizeof(int) == 4, "整数必须为4字节!");

  if (argc < 2) {
    fprintf(stderr, "用法: mkfs fs.img 文件...\n");
    exit(1);
  }

  // 验证块大小与数据结构对齐
  //assert((BLOCK_SIZE % sizeof(struct dinode)) == 0);
  assert((BLOCK_SIZE % sizeof(struct dirent)) == 0);

  // 打开或创建文件系统镜像
  fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fsfd < 0)
    die(argv[1]);

  // 计算文件系统布局参数 - 使用与内核一致的布局
  nlog = LOG_SIZE;
  ninodeblocks = INODE_BLOCKS;
  nbitmap = BMAP_BLOCKS;
  nmeta = SUPERBLOCK_NUM + nlog + ninodeblocks + nbitmap;
  nblocks = FS_TOTAL_BLOCKS - nmeta;

  // 初始化超级块 - 使用与内核一致的布局
  sb.magic = FS_MAGIC;
  sb.size = FS_TOTAL_BLOCKS;
  sb.nblocks = nblocks;
  sb.ninodes = ninodeblocks * IPB;  // 13 * 64 = 832个inode
  sb.nlog = nlog;
  sb.logstart = LOG_START;
  sb.inodestart = INODE_START;
  sb.bmapstart = BMAP_START;

  printf("创建文件系统:\n");
  printf("  总块数: %d\n", FS_TOTAL_BLOCKS);
  printf("  元数据块: %d (超级块 %d, 日志块 %d, inode块 %d, 位图块 %d)\n", 
         nmeta, SUPERBLOCK_NUM, nlog, ninodeblocks, nbitmap);
  printf("  数据块: %d\n", nblocks);
  printf("  布局: 超级块[%d], 日志[%d-%d], inode[%d-%d], 位图[%d], 数据[%d-%d]\n",
         SUPERBLOCK_BLOCKNO, 
         LOG_START, LOG_START + nlog - 1,
         INODE_START, INODE_START + ninodeblocks - 1,
         BMAP_START,
         DATA_START, FS_TOTAL_BLOCKS - 1);

  freeblock = DATA_START;  // 第一个可分配的数据块

  // 初始化整个文件系统为0
  memset(zeroes, 0, BLOCK_SIZE);
  for (i = 0; i < FS_TOTAL_BLOCKS; i++)
    wsect(i, zeroes);

  // 写入超级块到块1
  memset(buf, 0, sizeof(buf));
  memcpy(buf, &sb, sizeof(sb));
  wsect(SUPERBLOCK_BLOCKNO, buf);

  // 创建根目录
  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  // 在根目录中添加 "." 条目
  memset(&de, 0, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  // 在根目录中添加 ".." 条目
  memset(&de, 0, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  // 添加用户提供的文件到文件系统
  for (i = 2; i < argc; i++) {
    char *original_name = argv[i];  // 保存原始文件名
    char shortname[DIRSIZ + 1];     // 存储短名称的缓冲区
    
    // 提取文件名（去掉路径）
    char *slash = strrchr(original_name, '/');
    if (slash) {
        strncpy(shortname, slash + 1, DIRSIZ);
    } else {
        strncpy(shortname, original_name, DIRSIZ);
    }
    shortname[DIRSIZ] = '\0';  // 确保以null结尾
    
    // 去掉.elf扩展名（如果存在）
    char *dot = strrchr(shortname, '.');
    if (dot && (strcmp(dot, ".elf") == 0 || strcmp(dot, ".bin") == 0)) {
        *dot = '\0'; // 去掉扩展名
    }
    
    // 确保文件名不包含路径分隔符
    assert(strchr(shortname, '/') == 0);

    // 打开原始文件（使用原始文件名）
    if ((fd = open(original_name, 0)) < 0) {
        fprintf(stderr, "无法打开文件: %s\n", original_name);
        die(original_name);
    }

    // 跳过文件名中的前导下划线（如果存在）
    if (shortname[0] == '_') {
        memmove(shortname, shortname + 1, strlen(shortname));
    }

    // 验证文件名长度
    if (strlen(shortname) > DIRSIZ) {
      fprintf(stderr, "文件名太长: %s\n", shortname);
      exit(1);
    }
    
    // 分配inode
    inum = ialloc(T_FILE);

    // 在根目录中创建目录条目
    memset(&de, 0, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    de.name[DIRSIZ - 1] = '\0'; // 保证目录项字符串以null结尾
    iappend(rootino, &de, sizeof(de));

    // 将文件内容写入inode
    printf("添加文件: %s -> /%s (inode %d)\n", original_name, shortname, inum);
    while ((cc = read(fd, buf, sizeof(buf))) > 0) {
      iappend(inum, buf, cc);
    }

    close(fd);
  }

  // 修正根目录inode的大小（对齐到块边界）
  rinode(rootino, &din);
  off = din.size;
  off = ((off / BLOCK_SIZE) + 1) * BLOCK_SIZE;
  din.size = off;
  winode(rootino, &din);

  // 分配已使用的数据块到位图中
  balloc(freeblock - DATA_START);

  close(fsfd);
  printf("文件系统镜像 %s 创建成功\n", argv[1]);
  printf("已使用数据块: %d/%d\n", freeblock - DATA_START, nblocks);
  return 0;
}