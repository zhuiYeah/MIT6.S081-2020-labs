//以下都是open系统调用的第二个参数，描述了以那种方式打开该文件
#define O_RDONLY  0x000  //以只读方式打开
#define O_WRONLY  0x001  //以只写方式打开
#define O_RDWR    0x002  //以读写方式打开
#define O_CREATE  0x200  //该文件需要被创建
#define O_TRUNC   0x400  
#define O_NOFOLLOW 0x800 //来自lab 符号链接，以
