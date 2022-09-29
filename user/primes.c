#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int number()
{
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0)
    {
        for (int i = 2; i < 36; i++)
        {
            write(fd[1], &i ,sizeof(int));
        }
        close(fd[1]);
        exit(0);
    }
    close(fd[1]);
    return fd[0];
}

int prime(int in_fd, int p)
{
    int num;
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0)
    {
        while (read(in_fd, &num, sizeof(int)))
        {
            if (num % p)
            {
                write(fd[1], &num, sizeof(int));
            }
        }
        close(in_fd);
        close(fd[1]);
        exit(0);
        
    }
    close(in_fd);
    close(fd[1]);
    return fd[0];
}

int main(int argc, char *argv[])
{
    int p;
    int num = number();
    while (read(num, &p, sizeof(int)))
    {
        printf("prime %d\n", p);
        num = prime(num,p);
    }
    exit(0);
}