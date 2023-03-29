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

extern char end[]; // first address after kernel.也即用户进程可以使用的物理内存的开始地址
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

// kmem结构包含一个 空闲内存链表 ，以及一个对链表进行操作的 互斥自旋锁
//需要实现每个CPU持有一个freelist,于是这里改成kmem数组
struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  //将所有空闲内存分配给当前cpu
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  push_off(); //关中断才能调用cpuid
  int id = cpuid();
  pop_off();

  //将这一个物理页放入当前cpu的空闲链表中
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
//如果当前 cpu 有空闲内存块，就直接分配；没有的话，从其他 cpu 对应的 freelist 中“偷”一块。
void *kalloc(void)
{
  struct run *r;

  push_off(); //关中断才能调用cpuid
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r)
    kmem[id].freelist = r->next;
  //我没有空闲内存了，要从其他cpu的空闲链表偷一块物理内存了哦
  else
  { 
    for (int i =0 ;i <NCPU;i++){
      if (i == id) continue;
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if (r)
         kmem[i].freelist = r->next;
      release(&kmem[i].lock);
      if(r) break;
    }
  }
  release(&kmem[id].lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
