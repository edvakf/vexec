#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

int wait_and_exit(pid_t pid) {
    int status;

    if (-1 == waitpid(pid, &status, 0)) {
        perror("waitpid");
        return -1;
    }
    if (WIFEXITED(status)) {
        return 0;
    }
    if (WIFSIGNALED(status)) {
        return 0;
    }

    fprintf(stderr, "unexpectedly exited\n");
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "please pass at least one argument\n");
        return -1;
    }

    char **command = &argv[1];

    int pipes_out[2], pipes_err[2];

    if (pipe(pipes_out) == -1 || pipe(pipes_err) == -1) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // child process
        close(pipes_out[0]);
        close(pipes_err[0]);
        close(fileno(stdout));
        close(fileno(stderr));
        dup2(pipes_out[1], fileno(stdout));
        dup2(pipes_err[1], fileno(stderr));

        execvp(command[0], command);

        // comes here only when failed
        perror(command[0]);
        return -1;
    }

    // parent process
    close(pipes_out[1]);
    close(pipes_err[1]);
    int child_out = pipes_out[0];
    int child_err = pipes_err[0];
    close(fileno(stdin)); // stdin will be read by the child process

    char buf[256];
    int read_size;
    read_size = read(child_out, buf, sizeof(buf));
    printf("%s", buf);

    return wait_and_exit(pid);
}

