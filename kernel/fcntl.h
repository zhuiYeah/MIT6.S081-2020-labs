#define O_RDONLY  0x000 //open系统调用中第二个参数可能包含的flag，表示以只读的方式打开
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

//#ifdef LAB_MMAP
#define PROT_NONE       0x0
#define PROT_READ       0x1  //文件是可读的
#define PROT_WRITE      0x2  //进程虚拟地址映射到的文件地址是可写的，信息包含在掩码prot中
#define PROT_EXEC       0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
//#endif
