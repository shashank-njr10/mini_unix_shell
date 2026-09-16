#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "executor.h"

/* Points the child's stdin/stdout at the files named by '<', '>' or
 * '>>', if any were given for this command.
 * Concept: open() gives us a fresh file descriptor for the file;
 * dup2(fd, STDIN_FILENO) / dup2(fd, STDOUT_FILENO) then makes that
 * descriptor take over slot 0 (stdin) or slot 1 (stdout), so anything
 * the program reads/writes via stdin/stdout actually goes to the
 * file instead of the terminal. Must run in the child, after fork()
 * but before execvp(), since it permanently rewires this process's
 * file descriptors. */
static void apply_redirection(command_t *cmd) {
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            perror(cmd->infile);
            _exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) {
            perror(cmd->outfile);
            _exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

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
        apply_redirection(&pl->stages[0]);

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
