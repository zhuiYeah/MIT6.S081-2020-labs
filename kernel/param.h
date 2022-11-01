#define NPROC        64  // 最大进程数
#define NCPU          8  // 最大cpu数
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk 
#define MAXARG       32  // max exec arguments 函数最多有32个参数，因为只有这么多寄存器存参数
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes 。FS操作写入的最大块
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log  磁盘日志中的最大数据块
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache  
#define FSSIZE       10000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define NBUCKET      13    //来自lab buffers cache ，将原本的buffers分成13组，对每一组各自上锁，降低锁的粒度，减少锁争用
