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

typedef enum {
    VEXEC_MODE_OUT,
    VEXEC_MODE_ERR,
    VEXEC_MODE_NEITHER,
} vexec_mode;

typedef struct vexec_buflist_item {
    char buf[BUFSIZE];
    size_t size;
    struct vexec_buflist_item *next;
    vexec_mode mode;
} vexec_buflist_item_t;

typedef struct {
    vexec_buflist_item_t *head;
    vexec_buflist_item_t *current;
    vexec_buflist_item_t *tail;
} vexec_buflist_t;

vexec_buflist_item_t *alloc_vexec_buflist_item() {
    return (vexec_buflist_item_t *)malloc(sizeof(vexec_buflist_item_t));
}

typedef struct {
    int exit_status;
    vexec_buflist_t buflist;
    int number_of_watchers;
} vexec_env_t;

void handle_child_readable(
        struct ev_loop *loop, ev_io *w, int revents, vexec_mode mode) {
    vexec_env_t *env = (vexec_env_t *)ev_userdata(loop);
    ssize_t size = read(w->fd, env->buflist.current->buf, BUFSIZE);
    if (size == 0) {
        ev_io_stop(loop, w);
        env->number_of_watchers--;
        return;
    } else if (size < 0) {
        if (EAGAIN == errno || EWOULDBLOCK == errno) {
            return;
        }
        fprintf(stderr, "fread failed\n");
        ev_io_stop(loop, w);
        env->number_of_watchers--;
        return;
    }
    env->buflist.current->size = (size_t)size;
    env->buflist.current->mode = mode;
    if (env->buflist.current == env->buflist.tail) {
        env->buflist.tail = env->buflist.current->next = alloc_vexec_buflist_item();
    }
    env->buflist.current = env->buflist.current->next;
}

void child_out_readable_cb(struct ev_loop *loop, ev_io *w, int revents) {
    handle_child_readable(loop, w, revents, VEXEC_MODE_OUT);
}

void child_err_readable_cb(struct ev_loop *loop, ev_io *w, int revents) {
    handle_child_readable(loop, w, revents, VEXEC_MODE_ERR);
}

void child_status_change_cb(struct ev_loop *loop, ev_child *w, int revents) {
    vexec_env_t *env = (vexec_env_t *)ev_userdata(loop);
    if (WIFEXITED(w->rstatus)) {
        env->exit_status = WEXITSTATUS(w->rstatus);
    } else if (WIFSIGNALED(w->rstatus)) {
        env->exit_status = 0;
    } else {
        fprintf(stderr, "process terminated abnormally\n");
        env->exit_status = 1;
    }
    ev_child_stop(loop, w);
    env->number_of_watchers--;
}

void clear_buflist_cb(struct ev_loop *loop, ev_periodic *w, int revents) {
    vexec_env_t *env = (vexec_env_t *)ev_userdata(loop);
    vexec_mode mode = VEXEC_MODE_NEITHER;
    while (env->buflist.current != env->buflist.head) {
        if (mode != env->buflist.head->mode) {
            if (VEXEC_MODE_OUT == env->buflist.head->mode) {
                printf("%s", ANSI_COLOR_GREEN);
            } else if (VEXEC_MODE_ERR == env->buflist.head->mode) {
                printf("%s", ANSI_COLOR_RED);
            }
            mode = env->buflist.head->mode;
        }
        fwrite(env->buflist.head->buf, env->buflist.head->size, 1, stdout);
        env->buflist.tail = env->buflist.tail->next = env->buflist.head;
        env->buflist.head = env->buflist.head->next;
        env->buflist.tail->next = NULL;
    }
    if (1 == env->number_of_watchers) {
        // child process died, all out & err buffers cleared
        printf("%s", ANSI_COLOR_RESET);
        ev_periodic_stop(loop, w);
        env->number_of_watchers--;
    }
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

    vexec_env_t env;
    env.exit_status = 0;
    env.buflist.head = env.buflist.tail = env.buflist.current = alloc_vexec_buflist_item();
    env.number_of_watchers = 4;
    ev_set_userdata(loop, &env);

    ev_io child_out_readable, child_err_readable;
    ev_child child_status_change;
    ev_periodic clear_buflist;

    ev_io_init(&child_out_readable, child_out_readable_cb, child_out, EV_READ);
    ev_io_init(&child_err_readable, child_err_readable_cb, child_err, EV_READ);
    ev_child_init(&child_status_change, child_status_change_cb, pid, 0);
    ev_periodic_init(&clear_buflist, clear_buflist_cb, 0.1, 0.1, 0);
    ev_set_priority(&clear_buflist, -1);

    ev_io_start(loop, &child_out_readable);
    ev_io_start(loop, &child_err_readable);
    ev_child_start(loop, &child_status_change);
    ev_periodic_start(loop, &clear_buflist);

    ev_run(loop, 0);

    return env.exit_status;
}

