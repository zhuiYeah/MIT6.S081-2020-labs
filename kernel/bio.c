// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

// buffer cache 是 保存在内存中的 每个节点都是buf结构的双向链表，buf结构保存磁盘块(disk block)内容的缓存副本(内存中)。
//在内存中缓存磁盘块减少了，也为多个进程使用的磁盘块（disk blocks）提供了一个同步点

//接口:
//   要获取特定磁盘块的缓冲区buffer，则调用bread
//   更改缓冲区buffer的数据后，调用bwrite将其写入磁盘
//   缓冲区buffer完成后，调用brelse
//   调用brelse后不要使用该缓冲区buffer
//   在特定时间只有一个进程可以使用特定的缓冲区buffer，所以不要让他用太久哦

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

extern uint ticks;

//来自lab buffer cache，修改bcache结构，将buf分成13份，并获得trap.c中的ticks变量
struct
{
    // struct spinlock lock;
    struct spinlock biglock;       //原本的大锁需要保留，为了特定的情况下避免死锁
    struct spinlock lock[NBUCKET]; //所有的buffer分进13个bucket里面，每个bucket都要有一把锁
    struct buf buf[NBUF];          //内存中 缓存disk block所有缓冲区 都在这个数组里面

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    //所有缓冲区buffers 连接成一个链表，链表的头节点就是bcache.head。head.next是最近使用的buffer，head.prev是到使用最少的buffer
    struct buf head[NBUCKET];
} bcache;

//来自lab buffer cache.将磁盘块号映射到他的buffer可能在的bucket
int hash(int blockno)
{
    return blockno % NBUCKET;
}

void binit(void)
{
    struct buf *b; // b是指向buf结构的指针，即指向内存中 缓存相应diskblock的buffer 的指针

    initlock(&bcache.biglock, "bcache_biglock");

    //将13个bucket锁初始化
    for (int i = 0; i < NBUCKET; i++)
    {
        initlock(&bcache.lock[i], "bcache_onebucket");
    }

    // // Create linked list of buffers
    // bcache.head.prev = &bcache.head;
    // bcache.head.next = &bcache.head;
    //这里就需要13个head全部初始化了
    for (int i = 0; i < NBUCKET; i++)
    {
        bcache.head[i].next = &bcache.head[i];
        bcache.head[i].prev = &bcache.head[i];
    }
    //初始化所有buffer,初始化的时候，还是将所有的buffer链接到一个双向链表上，头节点为bcache.head[0]
    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
        b->lastuse = ticks;
        b->next = bcache.head[0].next;
        b->prev = &bcache.head[0];
        initsleeplock(&b->lock, "buffer");
        bcache.head[0].next->prev = b;
        bcache.head[0].next = b;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
//查找 设备dev上的blockno块  对应的  缓冲区缓存buffer cache ，没找到的话为该dev的blockno分配一个buffer，
//无论如何都返回一个有锁的buf
//这是lab buffer cache的核心和精髓
static struct buf *bget(uint dev, uint blockno)
{
    struct buf *b, *b2 = 0;
    //如果该disk block存在对应buffer，那么这个buffer一定在bucket[i]里面
    int i = hash(blockno), min_ticks = 0;
    acquire(&bcache.lock[i]);

    // Is the block already cached?
    // 1.首先还是判断是否命中,直接命中那是最爽的
    for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            //命中后释放该bucket的锁，得到该buffer的锁，返回该有锁buffer
            release(&bcache.lock[i]);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.lock[i]);

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.

    // 2。未命中
    acquire(&bcache.biglock);
    acquire(&bcache.lock[i]);
    // 2.1 从当前bucket中再找一遍
    for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache.lock[i]);
            release(&bcache.biglock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 2.2 还没命中，只能从当前bucket的LRU中找空闲块了。从当前bucket找到一个LRU buffer（该buffer是最久没使用的，别用了，给新的disk block吧）
    for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next)
    {
        if (b->refcnt == 0 && (b2 == 0 || b->lastuse < min_ticks))
        {
            min_ticks = b->lastuse;
            b2 = b;
        }
    } //b2就是被lru的那一个内存块
    if (b2)
    {
        b2->dev = dev;
        b2->blockno = blockno;
        b2->refcnt++;
        b2->valid = 0;
        release(&bcache.lock[i]);
        release(&bcache.biglock);
        acquiresleep(&b2->lock);
        return b2;
    }

    // 2.3 还没找到，那只能取其他bucket中的内存块了 ,这个j=hash(j+1)debug了半天，写成了j=hash(i+1),妈的
    for (int j = hash(i + 1); j != i; j = hash(j + 1))
    {
        acquire(&bcache.lock[j]);
        for (b = bcache.head[j].next; b != &bcache.head[j]; b = b->next)
        {
            if (b->refcnt == 0 && (b2 == 0 || b->lastuse < min_ticks))
            {
                min_ticks = b->lastuse;
                b2 = b;
            }
        }
        //将buffer从bucket[j]移动到bucket[i]中
        if (b2)
        {
            b2->dev = dev;
            b2->blockno = blockno;
            b2->refcnt++;
            b2->valid = 0;
            // b2是从jbucket里面偷过来的，需要将b2从bucket[j]移除
            b2->next->prev = b2->prev;
            b2->prev->next = b2->next;
            release(&bcache.lock[j]);
            //把b2新增到bucket[i]里面
            b2->next = bcache.head[i].next;
            b2->prev = &bcache.head[i];
            bcache.head[i].next->prev = b2;
            bcache.head[i].next = b2;
            release(&bcache.lock[i]);
            release(&bcache.biglock);
            acquiresleep(&b2->lock);
            return b2;
        }
        release(&bcache.lock[j]);
    }
    release(&bcache.lock[i]);
    release(&bcache.biglock);
    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
//返回一个带有指定块内容的 有锁 buf。
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid)
    {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
//将buffer b 的内容写入对应的磁盘块。b必须有锁
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
//释放一个持有锁的buffer，（旧的LRU策略：移动到head[i].next处 ）（most-recently-used ）
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);
    //那么应该将该buffer放回到哪一个bucket呢?
    int i = hash(b->blockno);

    //取得该bucket的锁
    acquire(&bcache.lock[i]);

    b->refcnt--;
    //这里，该buffer可以被彻底释放了，由于不使用之前的.prev .next LRU实现策略，所以对于空闲块直接设置使用时间即可。
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        // b->next->prev = b->prev;
        // b->prev->next = b->next;
        // b->next = bcache.head.next;
        // b->prev = &bcache.head;
        // bcache.head.next->prev = b;
        // bcache.head.next = b;
        b->lastuse = ticks;
    }

    release(&bcache.lock[i]);
}

// pin
void bpin(struct buf *b)
{
    int i = hash(b->blockno);
    acquire(&bcache.lock[i]);
    b->refcnt++;
    release(&bcache.lock[i]);
}

void bunpin(struct buf *b)
{
    int i = hash(b->blockno);
    acquire(&bcache.lock[i]);
    b->refcnt--;
    release(&bcache.lock[i]);
}
