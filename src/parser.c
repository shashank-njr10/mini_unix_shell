#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

#define MAX_ARGS 64

/* Splits `text` on whitespace into a NULL-terminated argv array.
 * Concept: strtok_r() walks through a string, handing back one token
 * at a time and remembering its position in `saveptr` between calls.
 * We use the "_r" (reentrant) form instead of plain strtok() so that
 * later, when we tokenize several pipeline stages in nested loops,
 * one call can't clobber another's progress. */
static char **tokenize(char *text) {
    char **argv = malloc(sizeof(char *) * MAX_ARGS);
    int argc = 0;

    char *saveptr;
    char *tok = strtok_r(text, " \t", &saveptr);
    while (tok != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    argv[argc] = NULL; /* execvp() needs this NULL terminator */
    return argv;
}

pipeline_t *parse_line(char *line) {
    /* getline() (used by main.c) leaves the trailing '\n' on the
     * string; strip it so it doesn't end up as part of the last
     * argument. */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    /* A line that's empty or only whitespace means "nothing to run". */
    char *probe = line;
    while (*probe == ' ' || *probe == '\t') probe++;
    if (*probe == '\0') return NULL;

    pipeline_t *pl = malloc(sizeof(pipeline_t));
    pl->nstages = 1;
    pl->stages = malloc(sizeof(command_t));
    pl->stages[0].argv = tokenize(line);

    /* Defensive check: if tokenizing somehow produced no argv[0]
     * (e.g. a line of only separator characters), treat it the same
     * as a blank line instead of handing the executor an empty
     * command. */
    if (pl->stages[0].argv[0] == NULL) {
        free(pl->stages[0].argv);
        free(pl->stages);
        free(pl);
        return NULL;
    }

    return pl;
}

void free_pipeline(pipeline_t *pl) {
    if (!pl) return;
    for (int i = 0; i < pl->nstages; i++) {
        free(pl->stages[i].argv);
    }
    free(pl->stages);
    free(pl);
}
