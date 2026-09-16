#ifndef SIGNALS_H
#define SIGNALS_H

#include <sys/types.h>

extern pid_t shell_pgid;
extern int shell_terminal;
extern int shell_is_interactive;

/* Called once at startup. Makes the shell the foreground process
 * group of its controlling terminal, and makes the shell itself
 * ignore SIGINT / SIGTSTP / SIGQUIT / SIGTTIN / SIGTTOU -- so those
 * signals affect whichever job currently owns the terminal (via
 * tcsetpgrp() in the executor) instead of killing or suspending the
 * shell process itself. */
void init_job_control(void);

/* Called in each child, right after fork() and before execvp().
 * Undoes the shell's SIG_IGN dispositions for that one process, so
 * the program it's about to become responds to Ctrl-C/Ctrl-Z the
 * normal way instead of silently ignoring them (which it would
 * otherwise inherit across exec()). */
void child_reset_signals(void);

#endif
