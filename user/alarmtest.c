//
// test program for the alarm lab.
// you can modify this file for testing,
// but please make sure your kernel
// modifications pass the original
// versions of these tests.
//lab alarm（实现用户级中断/故障处理程序） 的测试程序。你可以修改此文件进行测试。
//

/* sigalarm()系统调用
   一个程序在他运行期间调用了sigalarm(n,fn),那么该程序每运行n个cpu时间，内核会暂停当前应用程序并调用函数fn，
  当fn返回时，程序应当从停止的地方恢复。
  如果程序调用 sigalarm(0, 0)，内核会停止周期性的alarm
 */

/*sigreturn()系统调用
  当调用sigalarm(n,fn)后，进程会在每n个cpu时间调用一次用户态的fn函数，
  sigalarm的具体实现是当在trap.c中 时钟中断之后 从内核态返回用户态时，不进行正常的返回，而是返回到用户态的fn函数
  于是sigreturn()系统调用 需要从用户态的当前位置跳转到本身内核态应当正确返回的用户态位置。
*/


#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

void test0();
void test1();
void test2();
void periodic();
void slow_handler();

int main(int argc, char *argv[])
{
  test0();
  test1();
  test2();
  exit(0);
}

//volatile关键字声明count可能被一些未知的因素更改，中断服务程序中修改的供其它程序检测的变量需要加volatile；
volatile static int count; 

void periodic()
{
  count = count + 1;
  printf("alarm!\n");
  sigreturn();
}

// tests whether the kernel calls
// the alarm handler even a single time.
//测试内核是否调用了一次警报程序handler。
void test0()
{
  int i;
  printf("test0 start\n");
  count = 0;
  sigalarm(2, periodic);
  for (i = 0; i < 1000 * 500000; i++)
  {
    if ((i % 1000000) == 0)
      write(2, ".", 1);
    if (count > 0)
      break;
  }
  sigalarm(0, 0);
  if (count > 0)
  {
    printf("test0 passed\n");
  }
  else
  {
    printf("\ntest0 failed: the kernel never called the alarm handler\n");
  }
}

void __attribute__((noinline)) foo(int i, int *j)
{
  if ((i % 2500000) == 0)
  {
    write(2, ".", 1);
  }
  *j += 1;
}

//
// tests that the kernel calls the handler multiple times.
//
// tests that, when the handler returns, it returns to
// the point in the program where the timer interrupt
// occurred, with all registers holding the same values they
// held when the interrupt occurred.
//
//测试内核是否多次调用警报程序handler，
//测试，当警报程序handler返回时，它返回到程序中发生定时器中断的点，所有寄存器都保持与中断发生时相同的值。
void test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  sigalarm(2, periodic);
  for (i = 0; i < 500000000; i++)
  {
    if (count >= 10)
      break;
    foo(i, &j);
  }
  if (count < 10)
  {
    printf("\ntest1 failed: too few calls to the handler\n");
  }
  else if (i != j)
  {
    // the loop should have called foo() i times, and foo() should
    // have incremented j once per call, so j should equal i.
    // once possible source of errors is that the handler may
    // return somewhere other than where the timer interrupt
    // occurred; another is that that registers may not be
    // restored correctly, causing i or j or the address ofj
    // to get an incorrect value.
    printf("\ntest1 failed: foo() executed fewer times than it was called\n");
  }
  else
  {
    printf("test1 passed\n");
  }
}

//
// tests that kernel does not allow reentrant alarm calls.
void test2()
{
  int i;
  int pid;
  int status;

  printf("test2 start\n");
  if ((pid = fork()) < 0)
  {
    printf("test2: fork failed\n");
  }
  if (pid == 0)
  {
    count = 0;
    sigalarm(2, slow_handler);
    for (i = 0; i < 1000 * 500000; i++)
    {
      if ((i % 1000000) == 0)
        write(2, ".", 1);
      if (count > 0)
        break;
    }
    if (count == 0)
    {
      printf("\ntest2 failed: alarm not called\n");
      exit(1);
    }
    exit(0);
  }
  wait(&status);
  if (status == 0)
  {
    printf("test2 passed\n");
  }
}

void slow_handler()
{
  count++;
  printf("alarm!\n");
  if (count > 1)
  {
    printf("test2 failed: alarm handler called more than once\n");
    exit(1);
  }
  for (int i = 0; i < 1000 * 500000; i++)
  {
    asm volatile("nop"); // avoid compiler optimizing away loop
  }
  sigalarm(0, 0);
  sigreturn();
}
