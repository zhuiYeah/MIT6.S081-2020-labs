#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 使用 pipe() 和 fork() 实现父进程发送一个字符，子进程成功接收该字符后打印
// received ping，再向父进程发送一个字符，父进程成功接收后打印 received pong。

int main(int argc, char *argv[])
{
    int p1[2], p2[2];
    char buffer[] = {'l'};
    int len = sizeof(buffer);
    pipe(p1);
    pipe(p2);
    // 子进程
    if (fork() == 0)
    {
        close(p1[1]);
        close(p2[0]);
        // 从buffer中读取长度为len的字节 读入p1[0]中
        if (read(p1[0], buffer, len) != len)
        {
            printf("child read error!\n");
            exit(1);
        }
        printf("子进程%d: received ping\n", getpid());
        // 将p2[1]中的前len B数据写入buffer中，
        if (write(p2[1], buffer, len) != len)
        {
            printf("child write error\n");
            exit(1);
        }
        exit(0);
    }
    // 父进程
    else
    {
        close(p1[0]);
        close(p2[1]);
        if (write(p1[1], buffer, len) != len)
        {
            printf("parent write error!\n");
            exit(1);
        }
        if (read(p2[0], buffer, len) != len)
        {
            printf("parent read error!\n");
            exit(1);
        }
        printf("父进程%d: received pong\n");
        exit(0);
    }
    exit(0);
}
