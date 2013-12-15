#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "please give at least one arg");
        return -1;
    }

    char **command = &argv[1];

    execvp(command[0], command);

    perror(command[0]);
    return -1;
}
