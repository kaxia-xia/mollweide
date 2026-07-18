
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstdio>
int main() {
    int pipe_fds[2];
    pipe(pipe_fds);
    pid_t pid = fork();
    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[1]);
        execl("/system/bin/sh", "sh", "-c", "echo hello_from_fork", nullptr);
        _exit(127);
    }
    close(pipe_fds[1]);
    char buf[256];
    int n = read(pipe_fds[0], buf, sizeof(buf));
    buf[n] = 0;
    printf("Got: %s", buf);
    waitpid(pid, nullptr, 0);
    return 0;
}
