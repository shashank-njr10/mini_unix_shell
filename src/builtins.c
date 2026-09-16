#include <string.h>

#include "builtins.h"
#include "jobs.h"

int is_builtin(char *const argv[]) {
    return strcmp(argv[0], "jobs") == 0;
}

void run_builtin(char *const argv[]) {
    if (strcmp(argv[0], "jobs") == 0) {
        jobs_print();
    }
}
