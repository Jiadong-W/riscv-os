#include "types.h"
#include "user.h"

typedef long Align; // 强制内存块按 long 对齐

union header {
  struct {
    union header *ptr; // 下一个空闲块
    unsigned int size;         // 当前块大小（单位：Header）
  } s;
  Align x;
};

typedef union header Header;

static Header base;     // 空闲链表起点
static Header *freep;   // 当前空闲链表指针

// 释放用户空间内存
void free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1; // 获取头部
  // 合并相邻空闲块
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

// 扩展堆空间
static Header* morecore(unsigned int nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096; // 最小分配单位
  p = sbrk(nu * sizeof(Header));
  if(p == SBRK_ERROR)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

// 分配用户空间内存
void* malloc(unsigned int nbytes)
{
  Header *p, *prevp;
  unsigned int nunits;

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}
