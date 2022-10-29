// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

//记录所有用户进程可以分配到的物理页面的共享情况，一共有3w+ 物理页，n数值表示该物理页被n个物理进程共享，共享读取权限，但如果要写入呢？
uint page_ref[(PHYSTOP - KERNBASE) / PGSIZE];

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  //删除物理页的时候，如果该物理页还在被多个进程共享，那么不能删除该物理页，只改变它的共享系数
  if (page_ref[COW_INDEX(pa)] > 1)
  {
    page_ref[COW_INDEX(pa)]--;
    return;
  }

  //只有当共享系数为1的时候，将其置0,并释放物理页
  page_ref[COW_INDEX(pa)] = 0;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
  {
    memset((char *)r, 5, PGSIZE); // fill with junk
    page_ref[COW_INDEX(r)] = 1; //在第一分配物理页的时候 将该页的共享系数设为1
  }
  return (void *)r;
}
