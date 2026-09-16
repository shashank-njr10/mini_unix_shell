#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "jobs.h"

int is_builtin(char *const argv[]) {
    return strcmp(argv[0], "jobs") == 0
        || strcmp(argv[0], "cd") == 0
        || strcmp(argv[0], "exit") == 0;
}

/* Concept: cd has to be a builtin, not an external program. Every
 * process has its own current working directory, inherited by its
 * children at fork() but never propagated back to the parent. If `cd`
 * ran as a forked child (like `ls` does), it would chdir() *that
 * child's* directory and then exit, leaving the shell's own working
 * directory completely unchanged. Running it directly in the shell's
 * process is the only way `cd` can actually affect the shell. */
static void builtin_cd(char *const argv[]) {
    const char *target = argv[1];
    if (target == NULL) {
        target = getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "cd: HOME not set\n");
            return;
        }
    }

    if (chdir(target) != 0) {
        /* Covers the common error cases: no such directory, path
         * points at a file instead of a directory, or missing
         * permission to enter it. */
        perror(target);
    }
}

void run_builtin(char *const argv[]) {
    if (strcmp(argv[0], "jobs") == 0) {
        jobs_print();
    } else if (strcmp(argv[0], "cd") == 0) {
        builtin_cd(argv);
    } else if (strcmp(argv[0], "exit") == 0) {
        /* Same reasoning as cd: exit() has to run in the shell's own
         * process. If a forked child called exit(), only that child
         * would terminate -- the shell would just wait on it and then
         * print the next prompt as usual. */
        exit(0);
    }
}
