#define T_DIR 1     // Directory
#define T_FILE 2    // File
#define T_DEVICE 3  // Device
#define T_SYMLINK 4 //来自lab 符号链接， 符号链接文件,理解成windows里面的快捷方式即可

struct stat
{
  int dev;     // File system's disk device   文件属于哪个磁盘设备
  uint ino;    // Inode number                文件的inode号
  short type;  // Type of file                文件类型 目前就4种
  short nlink; // Number of links to file     链接到该文件的链接数量
  uint64 size; // Size of file in bytes       文件的字节数
};
