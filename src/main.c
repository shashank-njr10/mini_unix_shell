#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "executor.h"

int main(void) {
    char *line = NULL; /* getline() manages this buffer for us */
    size_t cap = 0;

    while (1) {
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

        run_pipeline(pl);
        free_pipeline(pl);
    }

    free(line);
    return 0;
}
