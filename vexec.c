#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ev.h>

#define BUFSIZE 256

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

void child_out_readable_cb(struct ev_loop *loop, ev_io *w, int revents) {
    char *buf[BUFSIZE];
    ssize_t size = read(w->fd, buf, BUFSIZE);
    if (size == 0) {
        ev_io_stop(loop, w);
        return;
    } else if (size < 0) {
        if (EAGAIN == errno || EWOULDBLOCK == errno) {
            return;
        }
        fprintf(stderr, "fread failed\n");
        ev_io_stop(loop, w);
        return;
    }
    fprintf(stdout, ANSI_COLOR_GREEN);
    fwrite(buf, size, 1, stdout);
    fprintf(stdout, ANSI_COLOR_RESET);
}

void child_err_readable_cb(struct ev_loop *loop, ev_io *w, int revents) {
    char *buf[BUFSIZE];
    ssize_t size = read(w->fd, buf, BUFSIZE);
    if (size == 0) {
        ev_io_stop(loop, w);
        return;
    } else if (size < 0) {
        if (EAGAIN == errno || EWOULDBLOCK == errno) {
            return;
        }
        fprintf(stderr, "fread failed\n");
        ev_io_stop(loop, w);
        return;
    }
    fprintf(stdout, ANSI_COLOR_RED);
    fwrite(buf, size, 1, stdout);
    fprintf(stdout, ANSI_COLOR_RESET);
}

void child_status_change_cb(struct ev_loop *loop, ev_child *w, int revents) {
    ev_child_stop(loop, w);
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
    fcntl(child_out, F_SETFL, fcntl(child_out, F_GETFL) | O_NONBLOCK);
    fcntl(child_err, F_SETFL, fcntl(child_err, F_GETFL) | O_NONBLOCK);
    close(fileno(stdin)); // stdin will be read by the child process

    struct ev_loop *loop = ev_default_loop(0);

    ev_io child_out_readable, child_err_readable;
    ev_child child_status_change;

    ev_io_init(&child_out_readable, child_out_readable_cb, child_out, EV_READ);
    ev_io_init(&child_err_readable, child_err_readable_cb, child_err, EV_READ);
    ev_child_init(&child_status_change, child_status_change_cb, pid, 0);

    ev_io_start(loop, &child_out_readable);
    ev_io_start(loop, &child_err_readable);
    ev_child_start(loop, &child_status_change);

    ev_run(loop, 0);

    return 0;
}

