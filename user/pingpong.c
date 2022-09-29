#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc != 1)
    {
        fprintf(2, "Usage: pingpong...\n");
        exit(1);
    }
    int result = -1;
    int fd[2];
    int pid;
    int* write_fd = &fd[1];
    int* read_fd = &fd[0];
    char c = 'a';
    char read_buffer[100];
    result = pipe(fd);
    if (result == -1)
    {
        fprintf(2, "Fail to create pipe\n");
        exit(1);
    }
    pid = fork();
    if (pid == -1)
    {
        fprintf(2, "Fail to fork\n");
        exit(1);
    }
    if (pid == 0)
    {
        close(*write_fd);
        // wait parent process to send the messages
        while(1)
        {
            int bytes = 0;
            bytes = read(*read_fd, read_buffer,sizeof(read_buffer));
            if (bytes > 0)
            {
                printf("%d: received ping\n", getpid());
                break;
            }
        }
    }
    else
    {
        close(*read_fd);
        printf("%d: received pong\n",getpid());
        write(*write_fd, &c, strlen(&c));
        wait(&pid); // wait for the child process to finish
    }
    exit(0);
}