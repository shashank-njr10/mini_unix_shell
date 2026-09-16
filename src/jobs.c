#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "jobs.h"

/* Concept: a singly linked list is enough here -- we only ever need
 * to add a job, walk every job (`jobs`), or remove one we just
 * finished reaping. No random access is needed. */
static job_t *job_list = NULL;
static int next_job_id = 1;

job_t *jobs_add(pid_t *pids, int npids, const char *cmdline) {
    job_t *job = malloc(sizeof(job_t));
    job->id = next_job_id++;
    job->npids = npids;
    for (int i = 0; i < npids; i++) {
        job->pids[i] = pids[i];
        job->done[i] = 0;
    }
    job->cmdline = strdup(cmdline);
    job->state = JOB_RUNNING;

    job->next = job_list;
    job_list = job;
    return job;
}

static void jobs_remove(job_t *target) {
    job_t **link = &job_list;
    while (*link != NULL) {
        if (*link == target) {
            *link = target->next;
            free(target->cmdline);
            free(target);
            return;
        }
        link = &(*link)->next;
    }
}

void jobs_print(void) {
    for (job_t *j = job_list; j != NULL; j = j->next) {
        printf("[%d]  Running\t\t%s\n", j->id, j->cmdline);
    }
}

/* Finds the tracked job that a just-reaped pid belongs to, and which
 * stage (pipeline position) within that job it was. */
static job_t *find_job_for_pid(pid_t pid, int *slot_out) {
    for (job_t *j = job_list; j != NULL; j = j->next) {
        for (int i = 0; i < j->npids; i++) {
            if (j->pids[i] == pid) {
                *slot_out = i;
                return j;
            }
        }
    }
    return NULL;
}

void jobs_reap(void) {
    int status;
    pid_t pid;

    /* Concept: waitpid(-1, &status, WNOHANG) asks "has any child of
     * mine exited?" and returns immediately with 0 if none has,
     * instead of blocking like a plain wait() would. That's what
     * lets the shell check on background jobs without freezing the
     * prompt while they're still running. Looping until it returns
     * <= 0 drains every child that finished since our last check,
     * not just the first one. */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int slot;
        job_t *job = find_job_for_pid(pid, &slot);
        if (job == NULL) continue; /* not a pid we're tracking */

        job->done[slot] = 1;

        int all_done = 1;
        for (int i = 0; i < job->npids; i++) {
            if (!job->done[i]) { all_done = 0; break; }
        }
        if (all_done) {
            printf("\n[%d]+  Done\t\t%s\n", job->id, job->cmdline);
            jobs_remove(job);
        }
    }
}
