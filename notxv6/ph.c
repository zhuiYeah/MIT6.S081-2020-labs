#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define NBUCKET 5
#define NKEYS 100000

// entry结构就是hash表的一个映射
struct entry
{
  int key;
  int value;
  struct entry *next; //映射后面再 链上 一个映射
};
struct entry *table[NBUCKET]; //一共有5个这样的链 （这也太少了吧）

int keys[NKEYS]; //每一链上最多存100000这样的映射
int nthread = 1;

double now()
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

//在判断hashmap中确实不存在此key才能调用该函数
static void insert(int key, int value, struct entry **p, struct entry *n)
{
  //新建一个映射
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  e->next = n;
  *p = e;
}

pthread_mutex_t lock[NBUCKET]; //给每个链都声明一把锁

//多线程出现的问题就在该函数
static void put(int key, int value)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&lock[i]);
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next)
  {
    if (e->key == key)
      break;
  }
  if (e)
  {
    // update the existing key.
    e->value = value;
  }
  else
  {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&lock[i]);
}

static struct entry *get(int key)
{
  int i = key % NBUCKET;
  //pthread_mutex_lock(&lock[i]); 读不需要锁
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next)
  {
    if (e->key == key)
      break;
  }
  //pthread_mutex_unlock(&lock[i]);
  return e;
}

static void *
put_thread(void *xa)
{
  int n = (int)(long)xa; // thread number
  int b = NKEYS / nthread;

  for (int i = 0; i < b; i++)
  {
    put(keys[b * n + i], n);
  }

  return NULL;
}

static void *
get_thread(void *xa)
{
  int n = (int)(long)xa; // thread number
  int missing = 0;

  for (int i = 0; i < NKEYS; i++)
  {
    struct entry *e = get(keys[i]);
    if (e == 0)
      missing++;
  }
  printf("%d: %d keys missing\n", n, missing);
  return NULL;
}

int main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;
  //初始化锁
  for (int i = 0; i < NBUCKET; i++)
    pthread_mutex_init(&lock[i], NULL);

  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++)
  {
    keys[i] = random();
  }

  //
  // first the puts
  //
  t0 = now();
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *)(long)i) == 0);
  }
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  //
  // now the gets
  //
  t0 = now();
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *)(long)i) == 0);
  }
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS * nthread, t1 - t0, (NKEYS * nthread) / (t1 - t0));
}
