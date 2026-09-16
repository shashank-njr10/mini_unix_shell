#ifndef BUILTINS_H
#define BUILTINS_H

/* Some commands (like `jobs`) have to run inside the shell's own
 * process instead of a forked child, because they need to act on or
 * report the shell's own state. These two functions let main.c
 * recognize and dispatch those commands before falling back to
 * run_pipeline() for everything else. */

/* Returns 1 if argv[0] names a builtin this shell handles itself. */
int is_builtin(char *const argv[]);

/* Runs the builtin named by argv[0], in the shell's own process. */
void run_builtin(char *const argv[]);

#endif
