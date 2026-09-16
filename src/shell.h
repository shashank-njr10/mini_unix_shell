#ifndef SHELL_H
#define SHELL_H

/* ---- Shared data types used across the whole shell ---- */

/* One external program plus its argument list.
 * Concept: execvp() wants argv as a NULL-terminated array of C strings,
 * e.g. {"ls", "-l", NULL} -- that's exactly what we build here. */
typedef struct {
    char **argv;
} command_t;

/* A pipeline is one or more commands. Right now every pipeline has
 * exactly one stage; pipe support ('|') is added in a later step. */
typedef struct {
    command_t *stages;
    int nstages;
} pipeline_t;

#endif
