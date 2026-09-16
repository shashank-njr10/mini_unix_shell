#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "executor.h"

void run_pipeline(pipeline_t *pl) {
    /* Concept: fork() clones the running process into two copies that
     * keep executing from this same point. The parent's fork() call
     * returns the new child's pid; the child's fork() call returns 0.
     * That's how the branch below tells the two copies apart. */
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* --- Child process --- */
        /* Concept: execvp() replaces this process's program code with
         * the requested command, searching the directories in $PATH
         * to find it (the "p" in execvp). If it succeeds it never
         * returns -- the process simply becomes `ls`, `grep`, etc.
         * It only returns here if the command couldn't be started. */
        execvp(pl->stages[0].argv[0], pl->stages[0].argv);

        if (errno == ENOENT) {
            fprintf(stderr, "%s: command not found\n", pl->stages[0].argv[0]);
        } else {
            perror(pl->stages[0].argv[0]);
        }
        _exit(127); /* 127 is the conventional "failed to exec" status */
    }

    /* --- Parent process (the shell itself) --- */
    /* Concept: waitpid() blocks the shell until the given child
     * changes state (here, until it exits), and reports the child's
     * exit status back through `status`. Waiting here is what makes
     * the shell run commands one at a time instead of racing ahead
     * to print the next prompt immediately. */
    int status;
    waitpid(pid, &status, 0);
}
