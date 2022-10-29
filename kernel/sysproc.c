#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0; // not reached
}

uint64 sys_getpid(void)
{
  return myproc()->pid;
}

uint64 sys_fork(void)
{
  return fork();
}

uint64 sys_wait(void)
{
  uint64 p;
  if (argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

//给用户进程分配堆内存
uint64 sys_sbrk(void)
{
  int n;

  if (argint(0, &n) < 0)
    return -1;
  struct proc *p = myproc();
  int oldsz = p->sz;

  //为了实现惰性分配，sbrk()只增加名义上的内存大小，并不实际分配值
  // if (growproc(n) < 0)
  //   return -1;

  if (oldsz + n >= MAXVA || oldsz + n <= 0)
    return oldsz;
  p->sz += n;

  //如果需要增加页面大小的话，我们进行惰性分配，但如果需要减少内存大小的话，那么直接删
  if (n < 0)
    uvmdealloc(p->pagetable, oldsz, oldsz + n);

  // sys_sbrk()返回进程原本的大小
  return oldsz;
}

uint64 sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
