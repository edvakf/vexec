#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>

#define BUFSIZE 8

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
    int child_out;
    int child_err;
    vexec_buflist_t buflist;
    pthread_rwlock_t lock;
} vexec_env_t;

void pipe_reader(vexec_env_t *env, int pipe, vexec_mode mode) {
    while (1) {
        pthread_rwlock_wrlock(&env->lock);
        ssize_t size = read(pipe, env->buflist.current->buf, BUFSIZE);
        if (size == 0) {
            pthread_rwlock_unlock(&env->lock);
            break;
        } else if (size < 0) {
            if (EAGAIN == errno || EWOULDBLOCK == errno) {
                pthread_rwlock_unlock(&env->lock);
                continue;
            }
            fprintf(stderr, "fread failed\n");
            pthread_rwlock_unlock(&env->lock);
            break;
        }
        env->buflist.current->size = (size_t)size;
        env->buflist.current->mode = mode;
        if (env->buflist.current == env->buflist.tail) {
            env->buflist.tail = env->buflist.current->next = alloc_vexec_buflist_item();
        }
        env->buflist.current = env->buflist.current->next;
        pthread_rwlock_unlock(&env->lock);
    }
}

void *cb_out_reader(void *arg) {
    vexec_env_t *env = (vexec_env_t *)arg;
    int pipe = env->child_out;
    pipe_reader(env, pipe, VEXEC_MODE_OUT);
    return NULL;
}

void *cb_err_reader(void *arg) {
    vexec_env_t *env = (vexec_env_t *)arg;
    int pipe = env->child_err;
    pipe_reader(env, pipe, VEXEC_MODE_ERR);
    return NULL;
}

int wait_for_child(pid_t pid) {
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
    fcntl(child_out, F_SETFL, O_NONBLOCK);
    fcntl(child_err, F_SETFL, O_NONBLOCK);
    close(fileno(stdin)); // stdin will be read by the child process

    pthread_t th_out_reader, th_err_reader;

    vexec_env_t env;
    env.child_out = child_out;
    env.child_err = child_err;
    env.buflist.head = env.buflist.tail = env.buflist.current = alloc_vexec_buflist_item();
    pthread_rwlock_init(&env.lock, NULL);

    if (-1 == pthread_create(&th_out_reader, NULL, cb_out_reader, &env) ||
        -1 == pthread_create(&th_err_reader, NULL, cb_err_reader, &env)) {
        fprintf(stderr, "pthread_create failed\n");
        return -1;
    }

    if (-1 == wait_for_child(pid)) {
        return -1;
    }
    if (0 != pthread_join(th_out_reader, NULL) || 0 != pthread_join(th_err_reader, NULL)) {
        fprintf(stderr, "could not properly join threads");
        return -1;
    }
    vexec_mode mode = VEXEC_MODE_NEITHER;
    while(1) {
        if (env.buflist.head == env.buflist.current) {
            break;
        }
        if (mode != env.buflist.head->mode) {
            if (VEXEC_MODE_OUT == env.buflist.head->mode) {
                printf("%s", ANSI_COLOR_GREEN);
            } else if (VEXEC_MODE_ERR == env.buflist.head->mode) {
                printf("%s", ANSI_COLOR_RED);
            }
            mode = env.buflist.head->mode;
        }
        fwrite(env.buflist.head->buf, env.buflist.head->size, 1, stdout);
        env.buflist.tail = env.buflist.tail->next = env.buflist.head;
        env.buflist.head = env.buflist.head->next;
        env.buflist.tail->next = NULL;
    }
    printf("%s", ANSI_COLOR_RESET);
    return 0;
}

