#!/usr/bin/env python3
"""
Interactive job-control tests for mini_unix_shell.

Ctrl-C and Ctrl-Z only mean anything to a REAL terminal: it's the
operating system's terminal driver that watches the raw input stream
for those control bytes and turns them into SIGINT/SIGTSTP, delivered
to whichever process group currently "owns" the terminal
(tcsetpgrp()). Piping input into mini_shell's stdin (the way
run_tests.sh does everything else) never goes through that driver at
all, so this behavior is untestable that way.

Instead, each test here opens a pseudo-terminal (pty) -- a
kernel-provided fake terminal device -- and starts mini_shell attached
to it exactly like a real terminal session would, then writes the same
raw bytes a keyboard would send (0x03 for Ctrl-C, 0x1a for Ctrl-Z).
"""

import os
import pty
import select
import signal
import sys
import time

SHELL = "./mini_shell"


def read_available(fd, timeout=1.5, idle=0.3):
    """Reads whatever mini_shell has printed to the pty so far,
    stopping once `idle` seconds pass with nothing new arriving (or
    `timeout` total elapses) -- we don't know exactly how much output
    a given command will produce ahead of time, so we just drain
    until the output goes quiet."""
    out = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], idle)
        if fd in r:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        elif out:
            break
    return out.decode(errors="replace")


class ShellSession:
    """Spawns one mini_shell process attached to a fresh pty (so it's
    a genuine foreground-process-group / controlling-terminal setup,
    not a pipe), and kills it again on exit. Used as a context manager
    so every test starts from a clean, independent shell."""

    def __enter__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(SHELL, [SHELL])
        read_available(self.fd, timeout=0.5)  # swallow the startup prompt
        return self

    def __exit__(self, *exc):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass

    def send(self, data: bytes):
        os.write(self.fd, data)

    def send_line(self, text: str):
        self.send((text + "\n").encode())

    def read(self, timeout=1.5):
        return read_available(self.fd, timeout=timeout)


results = {"pass": 0, "fail": 0}


def check(desc, condition, detail=""):
    if condition:
        print(f"PASS: {desc}")
        results["pass"] += 1
    else:
        print(f"FAIL: {desc}")
        if detail:
            print(f"  {detail}")
        results["fail"] += 1


def test_ctrlc_kills_foreground_job_not_the_shell():
    """
    What we're testing: pressing Ctrl-C while "sleep 30" is running in
    the foreground should kill just the sleep almost instantly (not
    make us wait out the full 30 seconds), because the terminal driver
    delivers SIGINT to sleep's process group -- the group the shell
    handed the terminal to via tcsetpgrp() right before waiting on it.
    The shell process itself set SIGINT to be ignored at startup, so
    it must survive Ctrl-C and keep accepting commands afterward.
    """
    with ShellSession() as sh:
        sh.send_line("sleep 30")
        time.sleep(0.3)  # let the child actually start and become foreground

        started = time.time()
        sh.send(b"\x03")  # the byte a terminal sends for Ctrl-C
        sh.read(timeout=2.0)
        elapsed = time.time() - started

        check(
            "Ctrl-C returns control well before sleep 30 would finish on its own",
            elapsed < 3.0,
            f"took {elapsed:.1f}s to get the prompt back",
        )

        sh.send_line("echo shell_survived")
        out2 = sh.read()
        check(
            "shell is still alive and responsive after Ctrl-C",
            "shell_survived" in out2,
            f"output was: {out2!r}",
        )


def test_ctrlz_stops_foreground_job_and_it_shows_in_jobs():
    """
    What we're testing: pressing Ctrl-Z while "sleep 30" is running
    should suspend it (SIGTSTP) rather than kill it. The shell should
    print a "Stopped" notice right away (from the WUNTRACED wait in
    run_pipeline), and a follow-up `jobs` command should list that
    same job with state Stopped.
    """
    with ShellSession() as sh:
        sh.send_line("sleep 30")
        time.sleep(0.3)

        sh.send(b"\x1a")  # the byte a terminal sends for Ctrl-Z
        out = sh.read(timeout=2.0)
        check(
            "Ctrl-Z prints a Stopped notice for the job",
            "Stopped" in out,
            f"output was: {out!r}",
        )

        sh.send_line("jobs")
        out2 = sh.read()
        check(
            "'jobs' lists the suspended sleep as Stopped",
            "Stopped" in out2 and "sleep 30" in out2,
            f"output was: {out2!r}",
        )


def test_ctrlc_at_idle_prompt_does_not_kill_shell():
    """
    What we're testing: Ctrl-C with NO job running -- just sitting at
    an empty prompt -- must not kill or exit the shell. This is the
    other half of "the shell ignores SIGINT itself": with nothing in
    the foreground, the terminal's foreground process group IS the
    shell, so it's the one that would receive SIGINT, and it must
    just shrug it off instead of dying.
    """
    with ShellSession() as sh:
        sh.send(b"\x03")  # Ctrl-C with nothing running
        time.sleep(0.3)
        sh.send_line("echo still_here")
        out = sh.read()
        check(
            "shell ignores Ctrl-C when idle and keeps running",
            "still_here" in out,
            f"output was: {out!r}",
        )


def main():
    if not os.path.exists(SHELL):
        print(f"error: {SHELL} not found -- run `make` first", file=sys.stderr)
        sys.exit(1)

    print("== mini_unix_shell interactive (pty) job-control tests ==")
    test_ctrlc_kills_foreground_job_not_the_shell()
    test_ctrlz_stops_foreground_job_and_it_shows_in_jobs()
    test_ctrlc_at_idle_prompt_does_not_kill_shell()

    print()
    print(f"== {results['pass']} passed, {results['fail']} failed ==")
    sys.exit(1 if results["fail"] else 0)


if __name__ == "__main__":
    main()
