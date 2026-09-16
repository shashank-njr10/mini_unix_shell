#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "executor.h"
#include "jobs.h"
#include "signals.h"

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
    int nstages = pl->nstages;
    pid_t pids[nstages];

    /* Concept: process groups. Every process in a pipeline is put
     * into the *same* process group (pgid == the first stage's pid),
     * so the whole pipeline can be treated as one unit -- the
     * terminal driver delivers Ctrl-C/Ctrl-Z to every process in
     * whichever group currently owns the terminal, and waitpid()
     * below can wait on all of them without needing a separate signal
     * handler per process. `pgid` starts at 0 and gets set to the
     * first child's pid once that child exists. */
    pid_t pgid = 0;

    /* Concept: pipe() creates one kernel pipe -- a pair of file
     * descriptors where pipefds[2*i] is the read end and
     * pipefds[2*i+1] is the write end. For N pipeline stages we need
     * N-1 pipes to connect each stage's stdout to the next stage's
     * stdin, so we allocate them all up front before forking anyone. */
    int pipefds[2 * (nstages > 1 ? nstages - 1 : 0)];
    for (int i = 0; i < nstages - 1; i++) {
        if (pipe(pipefds + 2 * i) < 0) {
            perror("pipe");
            return;
        }
    }

    for (int i = 0; i < nstages; i++) {
        /* Concept: fork() clones the running process into two copies
         * that keep executing from this same point. The parent's
         * fork() call returns the new child's pid; the child's
         * fork() call returns 0. That's how the branch below tells
         * the two copies apart. */
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return;
        }

        if (pid == 0) {
            /* --- Child process for stage i --- */

            /* Join this pipeline's process group (creating it, if
             * this is the first stage). Both the child and the parent
             * make this same setpgid() call (see below) -- that's a
             * deliberate race-safety pattern: whichever of the two
             * runs first "wins", but either way the group membership
             * is guaranteed to be in place before anyone relies on it
             * (e.g. before tcsetpgrp() hands the terminal to this
             * group). Errors are ignored here since a harmless race
             * (the parent already did it) is the most likely cause. */
            pid_t my_pgid = (i == 0) ? getpid() : pgid;
            setpgid(0, my_pgid);

            /* Give this pipeline's process group the terminal, so the
             * terminal driver routes Ctrl-C/Ctrl-Z to it instead of
             * the shell -- but only for a foreground pipeline; a
             * backgrounded one must NOT take over the terminal. */
            if (shell_is_interactive && !pl->background) {
                tcsetpgrp(shell_terminal, my_pgid);
            }

            /* Undo the shell's SIG_IGN for these signals so the
             * program about to run via execvp() reacts to Ctrl-C /
             * Ctrl-Z the normal way, instead of silently ignoring
             * them like the shell process itself does. */
            child_reset_signals();

            /* Wire this stage's stdin to the previous pipe's read end
             * (every stage except the first reads from a pipe), and
             * its stdout to the next pipe's write end (every stage
             * except the last writes to a pipe). Concept: dup2(fd, N)
             * makes descriptor N a copy of fd, so reads/writes on
             * stdin(0)/stdout(1) transparently go through the pipe. */
            if (i > 0) {
                dup2(pipefds[2 * (i - 1)], STDIN_FILENO);
            }
            if (i < nstages - 1) {
                dup2(pipefds[2 * i + 1], STDOUT_FILENO);
            }

            /* Every pipe fd was inherited from the parent via fork(),
             * including ends this stage doesn't use. They must all be
             * closed here: an unused write end left open would stop
             * readers from ever seeing end-of-file on the pipe. */
            for (int j = 0; j < 2 * (nstages - 1); j++) {
                close(pipefds[j]);
            }

            /* Explicit '<'/'>'/'>>' redirection takes priority over
             * pipe wiring, matching how a real shell behaves, e.g.
             * "cat | grep x > out.txt" still writes to out.txt rather
             * than to the (nonexistent) next pipeline stage. */
            apply_redirection(&pl->stages[i]);

            /* Concept: execvp() replaces this process's program code
             * with the requested command, searching the directories
             * in $PATH to find it (the "p" in execvp). If it succeeds
             * it never returns -- the process simply becomes `ls`,
             * `grep`, etc. It only returns here if the command
             * couldn't be started. */
            execvp(pl->stages[i].argv[0], pl->stages[i].argv);

            if (errno == ENOENT) {
                fprintf(stderr, "%s: command not found\n", pl->stages[i].argv[0]);
            } else {
                perror(pl->stages[i].argv[0]);
            }
            _exit(127); /* 127 is the conventional "failed to exec" status */
        }

        /* --- Parent process (the shell itself) --- */
        pids[i] = pid;
        if (i == 0) pgid = pid;
        /* Mirrors the child's setpgid() call above -- see the comment
         * there for why both sides do this. */
        setpgid(pid, pgid);
    }

    if (shell_is_interactive && !pl->background) {
        tcsetpgrp(shell_terminal, pgid);
    }

    /* The parent never reads or writes these pipes itself -- only the
     * children do -- so it closes every fd right after forking. If
     * the shell kept a pipe's write end open, the reading child on
     * the other side would never see end-of-file and would hang
     * waiting for more input forever. */
    for (int j = 0; j < 2 * (nstages - 1); j++) {
        close(pipefds[j]);
    }

    if (pl->background) {
        /* Don't wait: hand the pipeline's pids to the job list and
         * give the prompt straight back. jobs_reap() (called from the
         * main loop before each prompt) will notice later, from the
         * shell's normal flow, when this job's processes finish. */
        job_t *job = jobs_add(pids, nstages, pl->raw_line, JOB_RUNNING);
        printf("[%d] %d\n", job->id, pids[nstages - 1]);
        return;
    }

    /* Concept: waitpid(pid, &status, WUNTRACED) blocks until the
     * given child either exits or is *stopped* by a signal (like the
     * SIGTSTP that Ctrl-Z sends to the foreground process group).
     * Without WUNTRACED, waitpid() would ignore a stop and keep
     * blocking, leaving the shell hung waiting for a job the user
     * just suspended. Waiting on every stage here is also what makes
     * the shell run one pipeline at a time instead of racing ahead to
     * print the next prompt before it's done. */
    int status;
    int stopped = 0;
    for (int i = 0; i < nstages; i++) {
        waitpid(pids[i], &status, WUNTRACED);
        if (WIFSTOPPED(status)) {
            stopped = 1;
        }
    }

    /* Whether the job finished or was stopped, the shell needs the
     * terminal back so it can read the next line the user types. */
    if (shell_is_interactive) {
        tcsetpgrp(shell_terminal, shell_pgid);
    }

    if (stopped) {
        /* Ctrl-Z: the job is paused, not dead. Track it so `jobs`
         * shows it and its processes aren't left as untracked
         * zombies-in-waiting once they eventually do exit. This
         * shell doesn't implement `fg`/`bg` to resume it (out of
         * scope here) -- it's simply reported as stopped. */
        job_t *job = jobs_add(pids, nstages, pl->raw_line, JOB_STOPPED);
        printf("\n[%d]+  Stopped\t\t%s\n", job->id, pl->raw_line);
    }
}
