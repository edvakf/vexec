#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "please pass at least one argument");
        return -1;
    }

    char **command = &argv[1];

    pid_t pid;

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return errno;
    }

    if (pid == 0) {
        // child process
        execvp(command[0], command);

        // comes here only when failed
        perror(command[0]);
        return errno;
    }

    // parent process
    int status;
    int w = waitpid(pid, &status, 0);

    if (w == -1) {
        perror("waitpid");
        return errno;
    }

    if (WIFEXITED(status)) {
        printf("finished\n");
        return 0;
    }

    if (WIFSIGNALED(status)) {
        printf("finished by signal %d\n", WTERMSIG(status));
        return 0;
    }

    printf("exited\n");
    return WEXITSTATUS(status);

}

