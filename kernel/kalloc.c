// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

//定义页表的ref结构体，用于记录页表的引用计数，包含一个锁和一个引用计数
struct {
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE];
} ref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");//初始化
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    ref.cnt[(uint64)p / PGSIZE] = 1;//free时会减一
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  //引用计数减一，只有当引用数为0时才会真正释放
  acquire(&ref.lock);
  if(--ref.cnt[(uint64)pa / PGSIZE] == 0) {
    release(&ref.lock);
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  } else {
    release(&ref.lock);
  }
}
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    acquire(&ref.lock);
    ref.cnt[(uint64)r / PGSIZE] = 1;//初始化引用计数为1
    release(&ref.lock);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

//判断一个page是不是cow
int cowpage(pagetable_t pagetable, uint64 va) {
  if(va >= MAXVA)
    return 0;
  pte_t* pte = walk(pagetable, va, 0);
  if(!pte || !(*pte & PTE_V))
    return 0;
  return (*pte & PTE_F ? 1 : 0);
}

//将一个cow page复制
void* cowalloc(pagetable_t pagetable, uint64 va) {
  if(va % PGSIZE != 0)
    return 0;
  uint64 pa = walkaddr(pagetable, va);
  if(pa == 0)
    return 0;
  pte_t* pte = walk(pagetable, va, 0);
  if(krefcnt((char*)pa) == 1) {//引用计数为1，直接取消cow标志
    *pte |= PTE_W;
    *pte &= ~PTE_F;
    return (void*)pa;
  } else {
    char* mem = kalloc();//否则分配一个新的页面
    if(mem == 0)
      return 0;
    memmove(mem, (char*)pa, PGSIZE);//复制内容
    *pte &= ~PTE_V;//取消原来的映射，并建立新的映射
    if (mappages(pagetable, va, PGSIZE, (uint64)mem, (PTE_FLAGS(*pte) | PTE_W) & ~PTE_F) != 0) {
      kfree(mem);
      *pte |= PTE_V;
      return 0;
    }
    kfree((char*)PGROUNDDOWN(pa));
    return mem;
  } 
}

//返回引用计数
int krefcnt(void *pa) {
  return ref.cnt[(uint64)pa / PGSIZE];
}

//增加引用计数
int kaddrefcnt(void* pa) {
  if (((uint64)pa % PGSIZE != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP))
    return -1;
  acquire(&ref.lock);
  ref.cnt[(uint64)pa / PGSIZE]++;
  release(&ref.lock);
  return 0;
}