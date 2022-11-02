// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

// 文件系统实现。 五层：
// + Blocks：原始磁盘块的分配器。
// + Log：多步更新的崩溃恢复。
// + Files：inode 分配器、读取、写入、元数据。
// + Directories：具有特殊内容的 inode（其他 inode 的列表！）
// + Names：类似 /usr/rtm/xv6/fs.c 的路径，方便命名。

//此文件包含低级文件系统操作例程。 （更高级别的）系统调用实现在 sysfile.c 中。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
//每个磁盘设备都应该有一个superblock来储存磁盘信息，xv6只挂载了一个磁盘，于是只有一个superblock，整个磁盘的第1块
struct superblock sb;

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Init fs
void fsinit(int dev)
{
  readsb(dev, &sb);
  if (sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block. 分配一个空磁盘块，返回这个磁盘块的块号（即地址）
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for (b = 0; b < sb.size; b += BPB)
  {
    bp = bread(dev, BBLOCK(b, sb));
    for (bi = 0; bi < BPB && b + bi < sb.size; bi++)
    {
      m = 1 << (bi % 8);
      if ((bp->data[bi / 8] & m) == 0)
      {                        // Is block free?
        bp->data[bi / 8] |= m; // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.释放磁盘块
static void bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if ((bp->data[bi / 8] & m) == 0)
    panic("freeing free block");
  bp->data[bi / 8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

// 一个 inode 描述了一个未命名的文件。inode 磁盘结构保存元数据：文件的类型、大小、引用它的链接数以及保存文件内容的块列表。
//  inode 在磁盘上的 sb.startinode 上按顺序排列。 每个 inode 都有一个编号，表示它在磁盘上的位置。
// 内核在内存中保留一个正在使用的 inode 缓存，以提供一个同步访问多个进程使用的 inode 的地方。 缓存的 inode 包括未存储在磁盘上的簿记信息：ip->ref 和 ip->valid。

/* 一个 inode 及其在内存中的表示 在被文件系统代码的其余部分使用之前 会经历一系列状态。
      *Allocation : 如果inode的类型（在磁盘上）非零，则分配一个 inode。 ialloc() 分配，如果引用和链接计数已降至零，则 iput() 释放。
      * Referencing in cache:如果 ip->ref 为零，则 inode 缓存中的条目是空闲的。 否则 ip->ref 跟踪指向条目（打开的文件和当前目录）的内存中指针的数量。 iget() 找到或创建一个缓存条目并增加它的引用； iput() 递减ref。
      * Valid：inode 缓存条目中的信息（类型、大小和 c）只有在 ip->valid 为 1 时才正确。ilock() 从磁盘读取 inode 并设置 ip->valid，而 iput() 如果 ip->ref 已降至零，则清除 ip->valid。
      *  Locked: 文件系统代码只有在首先锁定了 inode 时才能检查和修改 inode 中的信息及其内容。

         Thus a typical sequence is:
     ip = iget(dev, inum)
     ilock(ip)
     ... examine and modify ip->xxx ...
     iunlock(ip)
     iput(ip)
*/

// ilock() 与 iget() 是分开的，因此系统调用可以获得对 inode 的长期引用（对于打开的文件）并且只能在短期内锁定它
//（例如，在 read() 中）。 分离还有助于避免路径名查找期间的死锁和竞争。 iget() 递增 ip->ref 以便 inode 保持缓存状态并且指向它的指针保持有效。

//许多内部文件系统函数期望调用者锁定所涉及的 inode； 这让调用者可以创建多步原子操作。

// icache.lock 自旋锁保护 icache 条目的分配。 由于 ip->ref 指示条目是否空闲，而 ip->dev 和 ip->inum 指示条目持有哪个 i-node，因此在使用这些字段中的任何一个时都必须持有 icache.lock。

//一个 ip->lock sleep-lock 保护除了 ref、dev 和 inum 之外的所有 ip-> 字段。 必须持有 ip->lock 才能读取或写入该 inode 的 ip->valid、ip->size、ip->type 等。
struct
{
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void iinit()
{
  int i = 0;

  initlock(&icache.lock, "icache");
  for (i = 0; i < NINODE; i++)
  {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
}

static struct inode *iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode *ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for (inum = 1; inum < sb.ninodes; inum++)
  {
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode *)bp->data + inum % IPB;
    if (dip->type == 0)
    { // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp); // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
//将内存中一个已修改的inode 复制到disk中
void iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode *)bp->data + ip->inum % IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
//在设备dev上找到编号为 inum的 inode并返回内存中的副本icache.inode[i]。 不锁定 inode，也不从磁盘读取它。
static struct inode *iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++)
  {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum)
    {
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if (empty == 0 && ip->ref == 0) // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if (empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
//增加inode *ip的引用计数
struct inode *idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
//锁定给定的 inode。 如有必要，从磁盘读取 inode。
void ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if (ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if (ip->valid == 0)
  {
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode *)bp->data + ip->inum % IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if (ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.解锁给定的 inode。
void iunlock(struct inode *ip)
{
  if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
//删除对内存中的 inode 的引用。如果那是最后一个引用，inode 缓存条目可以被回收。
//如果这是最后一个引用并且 inode 没有指向它的链接，则释放磁盘上的 inode（及其内容）。
//所有对 iput() 的调用都必须在transaction中，以防它必须释放 inode。
void iput(struct inode *ip)
{
  acquire(&icache.lock);

  if (ip->ref == 1 && ip->valid && ip->nlink == 0)
  {
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&icache.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&icache.lock);
  }

  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
//返回inode ip中第bn个块的磁盘块地址。如果没有这个块，bmap应该分配一个。
static uint bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if (bn < NDIRECT)
  {
    //如果该直接索引为空，则让该直接索引指向磁盘中的一个块（该块可以为文件直接使用）
    //得到数据块的地址
    if ((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  //那么bn就是间接索引了，这里是一级索引
  if (bn < NINDIRECT)
  {
    // Load indirect block, allocating if necessary.
    //一级索引对于inode address区域的第13个地址，这个地址指向一个拥有256个地址的直接索引表
    //得到直接索引块的地址
    if ((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);

    // addr也即磁盘的blockno，
    //得到该disk block（直接索引表）在内存中的buffer.
    bp = bread(ip->dev, addr);
    //得到直接索引表存的数据
    a = (uint *)bp->data;

    // addr现在就是一级索引表指向的直接索引表的第bn个块的地址，如果该地址为0，则分配一个空的磁盘块并让该地址指向它
    //得到数据块的地址
    if ((addr = a[bn]) == 0)
    {
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  //以上如果逻辑块号映射到直接索引或一级索引，那么代码原封不动
  //以下来自 lab large file ，新增了bn映射到二级索引的处理逻辑

  bn -= NINDIRECT;
  if (bn < NDINDIRECT)
  {
    //得到一级索引表的地址
    addr = ip->addrs[12];
    if (addr == 0)
      ip->addrs[12] = addr = balloc(ip->dev);

    //得到一级索引表块 在内存中的buffer
    bp = bread(ip->dev, addr);
    //读取一级索引表的数据
    a = (uint *)bp->data;

    //得到直接索引表的地址
    addr = a[bn / 256];
    if (addr == 0)
    {
      a[bn / 256] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    //得到直接索引表块在内存中的buffer
    bp = bread(bp->dev, addr);
    //读取直接索引表的数据
    a = (uint *)bp->data;

    //得到data块的地址
    addr = a[bn % 256];
    if (addr == 0)
    {
      addr = a[bn % 256] = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
//释放inode 以及 对应文件的全部内容。调用者必须持有 ip->lock。
void itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp, *bp2;
  uint *a, *a2;

  //对直接索引指向的数据块 以及自身 的释放
  for (i = 0; i < NDIRECT; i++)
  {
    if (ip->addrs[i])
    {
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  //对一级索引指向的直接索引块和数据块 以及 自身的释放
  if (ip->addrs[NDIRECT])
  {
    //得到直接索引块在内存中的buffer
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint *)bp->data;
    
    //a[j]就是数据块，逐一释放
    for (j = 0; j < NINDIRECT; j++)
    {
      if (a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    //释放直接索引块
    bfree(ip->dev, ip->addrs[NDIRECT]);
    //inode中指向该直接索引块的地址归零
    ip->addrs[NDIRECT] = 0;
  }

  //以下来自lab large file, 对二级索引及其指向的1个一级索引块、256个直接索引块、256*256个数据块 及其自身的释放
  if (ip->addrs[NDIRECT + 1])
  {
    //得到一级索引块 在内存中的buffer
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint *)bp->data;

    // a[i]是直接索引块的块号
    for (i = 0; i < 256; i++)
    {
      if (a[i])
      {
        //得到直接索引块在内存中的buffer
        bp2 = bread(ip->dev, a[i]);
        a2 = (uint *)bp2->data;
        for (j = 0; j < 256; j++)
        {
          // a2[j]就是数据块的地址（blockno）了,释放数据块
          if (a2[j])
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        //释放直接索引块
        bfree(ip->dev, a[i]);
        //并让一级索引块中指向该直接索引块的地址归零
        a[i]=0;
      }
    }
    brelse(bp);
    //释放一级索引块
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    //让inode中指向一级索引块的地址归零
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
//从 inode  *ip复制 stat 信息到 *st。 调用者必须持有 ip->lock。
void stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
// 从 inode 读取数据。 调用者必须持有 ip->lock。 如果user_dst==1，则dst为用户虚拟地址；否则，dst为内核地址。
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return 0;
  if (off + n > ip->size)
    n = ip->size - off;

  for (tot = 0; tot < n; tot += m, off += m, dst += m)
  {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = min(n - tot, BSIZE - off % BSIZE);
    if (either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1)
    {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
//向 inode 写入数据。Caller 必须持有 ip->lock, 如果user_src==1，则src为用户虚拟地址； 否则，src 是内核地址。返回成功写入的字节数。
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > MAXFILE * BSIZE)
    return -1;

  for (tot = 0; tot < n; tot += m, off += m, src += m)
  {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = min(n - tot, BSIZE - off % BSIZE);
    if (either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1)
    {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if (off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode *
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if (dp->type != T_DIR)
    panic("dirlookup not DIR");

  for (off = 0; off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0)
    {
      // entry matches path element
      if (poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for (off = 0; off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if (de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char *
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else
  {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode *
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while ((path = skipelem(path, name)) != 0)
  {
    ilock(ip);
    if (ip->type != T_DIR)
    {
      iunlockput(ip);
      return 0;
    }
    if (nameiparent && *path == '\0')
    {
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if ((next = dirlookup(ip, name, 0)) == 0)
    {
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if (nameiparent)
  {
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode *
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode *
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
