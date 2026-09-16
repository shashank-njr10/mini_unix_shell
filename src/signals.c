#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "signals.h"

pid_t shell_pgid;
int shell_terminal;
int shell_is_interactive;

void init_job_control(void) {
    shell_terminal = STDIN_FILENO;

    /* Concept: isatty() tells us whether stdin is an actual terminal
     * (a human typing at it) versus a pipe or a redirected file, e.g.
     * when the shell is run as `./mini_shell < script.sh` or under a
     * test harness. Job control (process groups, foreground/background
     * distinctions enforced by the terminal driver) only makes sense
     * when there's a real terminal to control. */
    shell_is_interactive = isatty(shell_terminal);
    if (!shell_is_interactive) {
        return;
    }

    /* Concept: a job-control shell must be the foreground process
     * group of its controlling terminal before it can hand that role
     * to the jobs it runs. If mini_shell itself was started in the
     * background from another shell, pause here until whoever is in
     * charge brings it to the foreground. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp())) {
        kill(-shell_pgid, SIGTTIN);
    }

    /* Concept: SIGINT (Ctrl-C) and SIGTSTP (Ctrl-Z) are sent by the
     * terminal driver to whichever process group currently "owns"
     * the terminal -- its foreground process group, tracked with
     * tcsetpgrp(). By ignoring these in the shell process itself, an
     * idle prompt with no job running just shrugs off Ctrl-C/Ctrl-Z
     * instead of the shell dying or suspending. Once a foreground job
     * is started, run_pipeline() hands the terminal to *its* process
     * group with tcsetpgrp(), so the signals go to the job instead.
     * SIGTTIN/SIGTTOU are ignored for a related reason: a job-control
     * shell can itself be sent those if it ever tries to read from or
     * write to the terminal while it isn't the foreground group.
     * Every child undoes this (see child_reset_signals()) right after
     * fork(), so the programs the shell runs still react to Ctrl-C
     * normally -- only the shell process itself ignores them. */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    /* Put the shell in its own process group and claim the terminal
     * for it, so it starts out as the foreground group. */
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        perror("mini_shell: couldn't put the shell in its own process group");
    }
    tcsetpgrp(shell_terminal, shell_pgid);
}

void child_reset_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}
