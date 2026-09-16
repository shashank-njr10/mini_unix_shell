#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "jobs.h"

int main(void) {
    char *line = NULL; /* getline() manages this buffer for us */
    size_t cap = 0;

    while (1) {
        /* Check whether any background job finished since the last
         * prompt, and report it, before showing a fresh prompt --
         * this is what makes "[1]+  Done   sleep 3" appear on its
         * own once a backgrounded command completes. */
        jobs_reap();

        printf("mini_shell> ");
        fflush(stdout); /* prompt has no newline, so force it to appear now */

        /* Concept: getline() reads one full line of input into a
         * heap buffer that it grows automatically as needed -- safer
         * than fgets() into a fixed-size array, which a long command
         * line could overflow. */
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            /* getline() returns -1 on EOF (e.g. the user pressed
             * Ctrl-D) or on a read error -- either way, time to quit. */
            printf("\n");
            break;
        }

        pipeline_t *pl = parse_line(line);
        if (pl == NULL) {
            continue; /* blank line: nothing to run, just re-prompt */
        }

        /* Builtins (like `jobs`) must run inside the shell's own
         * process rather than a forked child, since they act on the
         * shell's own state. Only a single, non-piped, non-backgrounded
         * command can be a builtin. */
        if (pl->nstages == 1 && !pl->background && is_builtin(pl->stages[0].argv)) {
            run_builtin(pl->stages[0].argv);
        } else {
            run_pipeline(pl);
        }
        free_pipeline(pl);
    }

    free(line);
    return 0;
}
