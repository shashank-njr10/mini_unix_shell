# mini_unix_shell

A small Unix shell written in C from scratch, using only POSIX system
calls -- no readline, no libc shell helpers. Built to understand how a
real shell (bash, zsh, etc.) actually works under the hood.

## Features

- **Command execution** via `fork()` + `execvp()` + `waitpid()`
- **Pipelines**: `cmd1 | cmd2 | cmd3` (implemented with `pipe()` and `dup2()`)
- **I/O redirection**: `<`, `>`, `>>`
- **Background jobs**: `cmd &`, plus a `jobs` builtin to list them
- **Job-control signal handling**: `Ctrl-C` / `Ctrl-Z` act on the
  foreground job, not the shell itself, using process groups and
  `tcsetpgrp()`
- **Builtins**: `cd`, `exit`, `jobs`
- Basic error handling: missing commands, invalid paths, failed `fork`/`pipe`

## Build & run

```sh
make
./mini_shell
```

## Layout

| File | Responsibility |
|---|---|
| `src/main.c` | REPL: read a line, parse it, dispatch to a builtin or the executor |
| `src/parser.c` | Turns one input line into a `pipeline_t` (stages, redirection, `&`) |
| `src/executor.c` | Forks/execs a pipeline, wires up pipes and redirection, waits for it |
| `src/jobs.c` | Tracks background/stopped jobs, reaps finished ones |
| `src/builtins.c` | `cd`, `exit`, `jobs` |
| `src/signals.c` | Process-group setup and signal dispositions for job control |

## Try it

```
mini_shell> echo hello | tr a-z A-Z
HELLO
mini_shell> ls -l > listing.txt
mini_shell> sleep 30 &
[1] 12345
mini_shell> jobs
[1]  Running    sleep 30 &
```

Press `Ctrl-C` while a foreground command is running to kill just
that command, or `Ctrl-Z` to stop it -- the shell itself keeps running
either way.
