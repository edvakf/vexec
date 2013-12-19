#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>

#define BYTES_TO_READ 256

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

typedef struct {
    int child_out;
    int child_err;
    vexec_mode mode;
    pthread_rwlock_t lock;
} vexec_env_t;

void *cb_out_reader(void *arg) {
    vexec_env_t *env = (vexec_env_t *)arg;
    int pipe = env->child_out;
    int size;
    char *buf[BYTES_TO_READ];
    while (1) {
        pthread_rwlock_wrlock(&env->lock);
        size = read(pipe, buf, BYTES_TO_READ);
        if (size == 0) {
            write(fileno(stdout), ANSI_COLOR_RESET, 4);
            pthread_rwlock_unlock(&env->lock);
            break;
        } else if (size < 0) {
            write(fileno(stdout), ANSI_COLOR_RESET, 4);
            fprintf(stderr, "fread failed (child_out)\n");
            pthread_rwlock_unlock(&env->lock);
            break;
        }
        if (env->mode != VEXEC_MODE_OUT) {
            write(fileno(stdout), ANSI_COLOR_GREEN, 5);
            env->mode = VEXEC_MODE_OUT;
        }
        write(fileno(stdout), buf, size);
        if (size != BYTES_TO_READ) {
            write(fileno(stdout), ANSI_COLOR_RESET, 4);
            env->mode = VEXEC_MODE_NEITHER;
        }
        pthread_rwlock_unlock(&env->lock);
    }
    return NULL;
}

void *cb_err_reader(void *arg) {
    vexec_env_t *env = (vexec_env_t *)arg;
    int pipe = env->child_err;
    int size;
    char *buf[BYTES_TO_READ];
    while (1) {
        pthread_rwlock_wrlock(&env->lock);
        size = read(pipe, buf, BYTES_TO_READ);
        if (size == 0) {
            write(fileno(stdout), ANSI_COLOR_RESET, 4);
            pthread_rwlock_unlock(&env->lock);
            break;
        } else if (size < 0) {
            write(fileno(stdout), ANSI_COLOR_RESET, 4);
            fprintf(stderr, "fread failed (child_err)\n");
            pthread_rwlock_unlock(&env->lock);
            break;
        }
        if (env->mode != VEXEC_MODE_ERR) {
            write(fileno(stderr), ANSI_COLOR_RED, 5);
            env->mode = VEXEC_MODE_ERR;
        }
        write(fileno(stderr), buf, size);
        if (size != BYTES_TO_READ) {
            write(fileno(stderr), ANSI_COLOR_RESET, 4);
            env->mode = VEXEC_MODE_NEITHER;
        }
        pthread_rwlock_unlock(&env->lock);
    }
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
    close(fileno(stdin)); // stdin will be read by the child process

    pthread_t th_out_reader, th_err_reader;

    vexec_env_t env;
    env.child_out = child_out;
    env.child_err = child_err;
    env.mode = VEXEC_MODE_NEITHER;
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
    return 0;
}

