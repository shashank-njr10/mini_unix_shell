#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

#define MAX_JOB_PIDS 16

typedef enum { JOB_RUNNING, JOB_STOPPED } job_state_t;

/* One backgrounded pipeline. A pipeline like "cmd1 | cmd2 &" is a
 * single job made up of two member processes (`pids`); the job isn't
 * finished until every member has exited. */
typedef struct job {
    int id;                   /* shell-assigned number shown as "[id]" */
    pid_t pids[MAX_JOB_PIDS]; /* one pid per pipeline stage */
    int done[MAX_JOB_PIDS];   /* done[i] = 1 once pids[i] has exited */
    int npids;
    char *cmdline;            /* original command text, for `jobs` output */
    job_state_t state;
    struct job *next;
} job_t;

/* Registers a job in the given initial state (JOB_RUNNING for one
 * just sent to the background with '&', JOB_STOPPED for a foreground
 * job just suspended with Ctrl-Z). Returns it (still owned by the job
 * list -- the caller must not free it). */
job_t *jobs_add(pid_t *pids, int npids, const char *cmdline, job_state_t state);

/* Prints every job still tracked, for the `jobs` builtin. */
void jobs_print(void);

/* Non-blocking check for background jobs that changed state since we
 * last looked: either one of their member processes exited, or the
 * whole job was stopped (e.g. by a manually sent SIGSTOP). Prints a
 * "Done" or "Stopped" line accordingly, and drops any job whose every
 * member has now finished. Meant to be called right before each
 * prompt is shown, so background jobs get reported without the shell
 * ever blocking to wait for them.
 * Concept: SIGCHLD is the signal the kernel sends a process whenever
 * one of its children changes state. A full job-control shell would
 * install a SIGCHLD handler to react to that immediately; here we
 * take the simpler approach of polling with a non-blocking waitpid()
 * on our own schedule (once per prompt), which avoids doing
 * non-async-signal-safe work like printf() from inside a handler. */
void jobs_reap(void);

#endif
