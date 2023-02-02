#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 例如在终端中输入 sleep 10  那么 argc为2， argv[0]为sleep ， argv[1]为10
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(2, "Usage: sleep time...\n");
        exit(1);
    }
    int time = atoi(argv[1]);
    printf("进入睡眠 \n");
    sleep(time);
    printf("睡眠结束 \n");
    exit(0);
}