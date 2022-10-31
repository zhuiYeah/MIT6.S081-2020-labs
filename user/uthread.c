#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
//#include "kernel/proc.h" 这里不能引用内核中的 context 结构体，需要为用户态定义用户态进行线程切换的上下文

/* Possible states of a thread: */
#define FREE 0x0
#define RUNNING 0x1
#define RUNNABLE 0x2

#define STACK_SIZE 8192


#define MAX_THREAD 4


// Saved registers for user context switches. 为了进行用户态的线程切换，定义用户态的上下文
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

//这线程结构体thread ，proc是进程结构体
struct thread
{
  char stack[STACK_SIZE]; /* the thread's stack */
  int state;              /* FREE, RUNNING, RUNNABLE */
  struct context contex;
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
//线程切换，第一个参数是存放当前线程的上下文地址，第二个参数是存放要切换线程的上下文地址
extern void thread_switch(uint64, uint64);

void thread_init(void)
{
  // main() is thread 0, which will make the first invocation to
  // thread_schedule().  it needs a stack so that the first thread_switch() can
  // save thread 0's state.  thread_schedule() won't run the main thread ever
  // again, because its state is set to RUNNING, and thread_schedule() selects
  // a RUNNABLE thread.
  // main() 是线程 0，它将第一次调用 thread_schedule()。 它需要一个线程栈，以便第一个 thread_switch() 可以保存线程 0 的状态。
  // thread_schedule() 不会再运行主线程，因为它的状态设置为 RUNNING，而 thread_schedule() 只会调度选择 RUNNABLE 线程。
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

//一个纯用户态的线程调度函数，线程自己决定yield让出cpu控制权(在uthread.c中)，而不是被始终中断剥夺cpu
void thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* Find another runnable thread. 找到就绪队列中的线程作为被调度的线程*/
  next_thread = 0;
  t = current_thread + 1;
  //一共有MAX_THREAD个线程，遍历所有线程直到找到就绪线程
  for (int i = 0; i < MAX_THREAD; i++)
  {
    if (t >= all_thread + MAX_THREAD)
      t = all_thread;
    if (t->state == RUNNABLE)
    {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  if (next_thread == 0)
  {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread)
  { /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    //从当前线程切换到下一个被调度的线程，把当前线程的寄存器信息存到&t->contex；加载被调度线程的寄存器信息到cpu中
    thread_switch((uint64)&t->contex, (uint64)&current_thread->contex);
  }
  else //如果当前线程就是被调度的线程（遍历了一圈线程池发现还是只有你才能跑），那么不需要切换了
    next_thread = 0;
}

//创建一个线程，该线程执行func函数，该thread_create()执行完成后线程
void thread_create(void (*func)())
{
  struct thread *t;
  
  //找到线程池中状态为FREE的线程
  for (t = all_thread; t < all_thread + MAX_THREAD; t++)
  {
    if (t->state == FREE)
      break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  t->contex.ra = (uint64)func; //ra寄存器存储线程要执行的函数，这样调度该线程的时候swtch.S最后的ret指令就是返回到ra寄存器指向的地址
  t->contex.sp = (uint64)&t->stack + (STACK_SIZE - 1); //sp寄存器指向线程栈顶
  
}

//由线程主动放弃自己对cpu的占有
void thread_yield(void)
{
  current_thread->state = RUNNABLE;
  thread_schedule();
}

volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void thread_a(void)
{
  int i;
  printf("thread_a started\n");
  a_started = 1;
  while (b_started == 0 || c_started == 0)
    thread_yield();

  for (i = 0; i < 100; i++)
  {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);

  current_thread->state = FREE;
  thread_schedule();
}

void thread_b(void)
{
  int i;
  printf("thread_b started\n");
  b_started = 1;
  while (a_started == 0 || c_started == 0)
    thread_yield();

  for (i = 0; i < 100; i++)
  {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  current_thread->state = FREE;
  thread_schedule();
}

void thread_c(void)
{
  int i;
  printf("thread_c started\n");
  c_started = 1;
  while (a_started == 0 || b_started == 0)
    thread_yield();

  for (i = 0; i < 100; i++)
  {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  current_thread->state = FREE;
  thread_schedule();
}

int main(int argc, char *argv[])
{
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  thread_schedule();
  exit(0);
}
