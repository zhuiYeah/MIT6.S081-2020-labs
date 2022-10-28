#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if (argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
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
  backtrace(); //来自 lab backtrace()
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//来自 lab alarm定时警告（用户级中断/故障处理程序）
uint64 sys_sigreturn(void)
{
  struct proc *p = myproc();
  *p->trapframe = *p->handler_trapframe;
  p->ticks = 0;
  return 0;
}

//来自lab alarm定时警告 （用户级中断/故障处理程序）,得到用户态调用sigalarm(n,fn)的参数并写入proc结构中
uint64 sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  struct proc *p;
  p = myproc();
  if (argint(0, &interval) != 0 || argaddr(1, &handler) != 0 || interval < 0)
    return -1;
  p->interval = interval;
  p->handler = handler;
  p->ticks = 0;
  return 0;
}