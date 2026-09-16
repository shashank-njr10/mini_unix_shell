#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "shell.h"

/* Runs one pipeline to completion: forks a child, hands it off to the
 * requested program, and waits for it to finish before returning. */
void run_pipeline(pipeline_t *pl);

#endif
