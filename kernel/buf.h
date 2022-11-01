struct buf {
  int valid;   // has data been read from disk? 是否已从磁盘读取数据
  int disk;    // does disk "own" buf?
  uint dev;   //dev和blockno唯一的标识了该buf缓存的是哪一个磁盘区域
  uint blockno;
  struct sleeplock lock;
  uint refcnt;      //该buffer的引用计数，表示有几个进程要用这个buffer
  struct buf *prev; // LRU cache list
  struct buf *next; //所有缓冲区buffers 连接成一个链表。head.next是最近刚使用的buffer，head.prev是到最长时间未使用的buffer
  uchar data[BSIZE];

  //为 kernel/buf.h 中添加 lastuse 字段，即最后使用的时间，便于使用新的利用时间戳的LRU 机制
  uint lastuse;
};

