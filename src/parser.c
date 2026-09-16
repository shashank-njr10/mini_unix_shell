#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

#define MAX_ARGS 64

/* Splits `text` on whitespace into tokens and sorts them into `cmd`:
 * plain words go into argv, while '<', '>' and '>>' are recognized as
 * redirection operators whose *next* token is a filename rather than
 * an argument to the program.
 * Concept: strtok_r() walks through a string, handing back one token
 * at a time and remembering its position in `saveptr` between calls.
 * We use the "_r" (reentrant) form instead of plain strtok() so that
 * later, when we tokenize several pipeline stages in nested loops,
 * one call can't clobber another's progress. */
static void tokenize(char *text, command_t *cmd) {
    char **argv = malloc(sizeof(char *) * MAX_ARGS);
    int argc = 0;

    cmd->infile = NULL;
    cmd->outfile = NULL;
    cmd->append = 0;

    char *saveptr;
    char *tok = strtok_r(text, " \t", &saveptr);
    while (tok != NULL && argc < MAX_ARGS - 1) {
        if (strcmp(tok, "<") == 0) {
            tok = strtok_r(NULL, " \t", &saveptr);
            if (tok != NULL) cmd->infile = tok;
        } else if (strcmp(tok, ">>") == 0) {
            tok = strtok_r(NULL, " \t", &saveptr);
            if (tok != NULL) { cmd->outfile = tok; cmd->append = 1; }
        } else if (strcmp(tok, ">") == 0) {
            tok = strtok_r(NULL, " \t", &saveptr);
            if (tok != NULL) { cmd->outfile = tok; cmd->append = 0; }
        } else {
            argv[argc++] = tok;
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    argv[argc] = NULL; /* execvp() needs this NULL terminator */
    cmd->argv = argv;
}

#define MAX_STAGES 16

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

    /* Keep an untouched copy of the line (post newline-strip) before
     * we start cutting it up below -- this is what shows up in
     * `jobs` output for background jobs, since by the time we're done
     * parsing, the original buffer has been split into pieces with
     * embedded NUL bytes and can't be printed as-is. */
    char *raw_copy = strdup(line);

    /* A trailing '&' means "run this pipeline in the background and
     * give me the prompt back immediately instead of waiting for it
     * to finish." Detect and strip it (plus any whitespace around it)
     * before tokenizing, so it never ends up mistaken for a program
     * argument. */
    int background = 0;
    size_t end = strlen(line);
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
    if (end > 0 && line[end - 1] == '&') {
        background = 1;
        end--;
        while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
        line[end] = '\0';
    }

    /* Split the line on '|' first, into up to MAX_STAGES raw stage
     * strings, before tokenizing each stage individually. A pipeline
     * like "ls -l | grep foo | wc -l" becomes three stages here.
     * Concept: strtok_r() is used again, with '|' as the delimiter
     * this time, kept separate from the whitespace-tokenizing pass
     * inside tokenize() via its own saveptr. */
    char *stage_text[MAX_STAGES];
    int nstages = 0;
    char *saveptr;
    char *stage = strtok_r(line, "|", &saveptr);
    while (stage != NULL && nstages < MAX_STAGES) {
        stage_text[nstages++] = stage;
        stage = strtok_r(NULL, "|", &saveptr);
    }

    if (nstages == 0) {
        /* e.g. the user typed just "&" -- nothing left to run. */
        free(raw_copy);
        return NULL;
    }

    pipeline_t *pl = malloc(sizeof(pipeline_t));
    pl->nstages = nstages;
    pl->background = background;
    pl->raw_line = raw_copy;
    pl->stages = malloc(sizeof(command_t) * nstages);

    for (int i = 0; i < nstages; i++) {
        tokenize(stage_text[i], &pl->stages[i]);

        /* Defensive check: an empty stage (e.g. "ls || wc", or a
         * trailing '|' with nothing after it) tokenizes to no argv[0]
         * at all. That's a malformed pipeline, so bail out cleanly
         * instead of handing the executor a command with no program
         * to run. */
        if (pl->stages[i].argv[0] == NULL) {
            fprintf(stderr, "mini_shell: syntax error: empty command near '|'\n");
            /* Only stages [0..i] have been tokenized (and so only
             * they have a real argv to free) -- tell free_pipeline()
             * to stop there instead of walking into uninitialized
             * memory for the stages we never got to. */
            pl->nstages = i + 1;
            free_pipeline(pl);
            return NULL;
        }
    }

    return pl;
}

void free_pipeline(pipeline_t *pl) {
    if (!pl) return;
    for (int i = 0; i < pl->nstages; i++) {
        free(pl->stages[i].argv);
    }
    free(pl->stages);
    free(pl->raw_line);
    free(pl);
}
