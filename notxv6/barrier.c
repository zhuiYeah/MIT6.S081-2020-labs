#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier
{
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread; // Number of threads that have reached this round of the barrier
  int round;   // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

//条件变量是利用线程间共享的全局变量进行同步的一种机制，
//主要包括两个动作：一个线程等待"条件变量的条件成立"而挂起；另一个线程使"条件成立"（给出条件成立信号）。
//为了防止竞争，条件变量的使用总是和一个互斥锁结合在一起。

//这里是生产者消费者模式，如果还有线程没到达，就加入到队列中，等待唤起；如果最后一个线程到达了，就将轮数加一，然后唤醒所有等待这个条件变量的线程。
static void barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  bstate.nthread++;
  if (bstate.nthread == nthread)
  {
    //所有线程都到位了
    bstate.nthread = 0;
    bstate.round++;
    pthread_cond_broadcast(&bstate.barrier_cond);
  }
  else
  {
    //仍然有线程没到位，我自己先sleep了，等待被唤醒哈，卡在这里的线程继续卡着吧
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *thread(void *xa)
{
  long n = (long)xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++)
  {
    int t = bstate.round;
    assert(i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2)
  {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for (i = 0; i < nthread; i++)
  {
    assert(pthread_create(&tha[i], NULL, thread, (void *)i) == 0);
  }
  for (i = 0; i < nthread; i++)
  {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
