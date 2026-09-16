#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

/* Turns one line of raw input into a pipeline_t the executor can run.
 * Returns NULL if the line had nothing useful in it (blank / only
 * whitespace). The returned pipeline_t (and everything inside it) must
 * be released with free_pipeline(). */
pipeline_t *parse_line(char *line);

/* Frees everything parse_line() allocated for one pipeline. */
void free_pipeline(pipeline_t *pl);

#endif
