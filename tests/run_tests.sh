#!/usr/bin/env bash
#
# Black-box test suite for mini_shell's non-interactive behavior.
#
# How this works: we never look at the C source at all -- each test
# just feeds a sequence of command lines into mini_shell's stdin
# (exactly the way piping a script into it, e.g. `./mini_shell <
# script`, would) and checks what came out, the same way a human
# typing at the prompt would judge whether it "worked". Ctrl-C/Ctrl-Z
# job-control behavior isn't testable this way -- that needs a real
# terminal device, which is why it lives in test_signals.py instead.

set -u
cd "$(dirname "$0")/.." || exit 1

# Absolute path: some tests deliberately cd elsewhere (e.g. to check
# `cd` itself), and a relative "./mini_shell" would stop resolving the
# moment the test's own working directory changes.
SHELL_BIN="$(pwd)/mini_shell"
PASS=0
FAIL=0

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# --- test harness plumbing ---

# Feeds each argument as its own line of input to mini_shell (as if
# the user had typed it and pressed Enter) and returns everything the
# shell printed, stdout and stderr combined.
run_shell() {
    printf '%s\n' "$@" | "$SHELL_BIN" 2>&1
}

assert_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"
        echo "  expected to find: $needle"
        printf '  actual output:\n%s\n' "$haystack" | sed 's/^/    /'
        FAIL=$((FAIL + 1))
    fi
}

assert_not_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"
        echo "  expected NOT to find: $needle"
        printf '  actual output:\n%s\n' "$haystack" | sed 's/^/    /'
        FAIL=$((FAIL + 1))
    fi
}

assert_eq() {
    local desc="$1" actual="$2" expected="$3"
    if [[ "$actual" == "$expected" ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"
        echo "  expected: $(printf '%q' "$expected")"
        echo "  actual:   $(printf '%q' "$actual")"
        FAIL=$((FAIL + 1))
    fi
}

assert_true() {
    local desc="$1" condition="$2"
    if [[ "$condition" -eq 1 ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"
        FAIL=$((FAIL + 1))
    fi
}

# --- fixtures ---
# This shell has no quote parsing (by design -- it's out of scope for
# the project), so test commands below stick to bare words only. Any
# multi-line input a test needs is written to a file here with plain
# bash first, then read back with `cat`/`<` inside mini_shell.

FRUIT_FILE="$TMPDIR/fruit.txt"
printf 'apple\nbanana\ncherry\n' > "$FRUIT_FILE"

NUMBERS_FILE="$TMPDIR/numbers.txt"
printf '3\n1\n2\n' > "$NUMBERS_FILE"

# --- tests ---

test_simple_command_runs() {
    # What we're testing: the most basic case there is -- typing
    # "echo hello" should fork a child, exec `echo` in it, and have
    # "hello" show up in our output. This exercises the whole
    # fork() -> execvp() -> waitpid() path with no pipes or
    # redirection involved at all.
    local out
    out=$(run_shell "echo hello")
    assert_contains "simple command's output reaches the terminal" "$out" "hello"
}

test_missing_command_reports_error_and_shell_survives() {
    # What we're testing: running a program that isn't on $PATH must
    # not crash the shell or hang it -- it should print a
    # "command not found" message (from the ENOENT branch after a
    # failed execvp()) and, just as importantly, the shell has to
    # still be alive afterward to run the NEXT line we type.
    local out
    out=$(run_shell "nosuchcmd_xyz123" "echo still_alive")
    assert_contains "unknown command reports 'command not found'" "$out" "command not found"
    assert_contains "shell keeps running after a bad command" "$out" "still_alive"
}

test_output_redirection_writes_to_file_not_terminal() {
    # What we're testing: "echo hi > file" should NOT print "hi" back
    # to us -- the '>' operator should open the file and dup2() it
    # onto the child's stdout, so the text goes straight into the
    # file instead.
    local f="$TMPDIR/out.txt"
    run_shell "echo hi > $f" >/dev/null
    assert_eq "'>' sends command output into the target file" "$(cat "$f" 2>/dev/null)" "hi"
}

test_output_redirection_truncates_existing_file() {
    # What we're testing: '>' (unlike '>>') should overwrite whatever
    # was already in the file, not add to it. We seed the file with
    # old content first and check it's gone afterward.
    local f="$TMPDIR/trunc.txt"
    echo "old content that should disappear" > "$f"
    run_shell "echo new > $f" >/dev/null
    assert_eq "'>' truncates the file before writing" "$(cat "$f")" "new"
}

test_append_redirection_adds_without_erasing() {
    # What we're testing: '>>' should add to the end of a file rather
    # than erasing it first. We write two separate commands and expect
    # to find BOTH lines still present afterward, in order.
    local f="$TMPDIR/append.txt"
    run_shell "echo one > $f" "echo two >> $f" >/dev/null
    assert_eq "'>>' appends instead of overwriting" "$(cat "$f")" "$(printf 'one\ntwo')"
}

test_input_redirection_feeds_file_to_command() {
    # What we're testing: '<' should make a command read its input
    # from a file instead of the terminal. We point `cat` at our
    # fruit fixture and expect its exact contents to come back.
    local out
    out=$(run_shell "cat < $FRUIT_FILE")
    assert_contains "'<' feeds the file's contents to the command's stdin" "$out" "banana"
}

test_two_stage_pipeline_connects_processes() {
    # What we're testing: "cmd1 | cmd2" should connect cmd1's stdout
    # straight to cmd2's stdin through a kernel pipe -- no temp file
    # involved. We pipe our 3-line fixture through `grep` and expect
    # only the one matching line back.
    local out
    out=$(run_shell "cat $FRUIT_FILE | grep banana")
    assert_contains "two-stage pipeline: grep filters cat's output" "$out" "banana"
    assert_not_contains "grep in a pipeline drops non-matching lines" "$out" "apple"
}

test_three_stage_pipeline_chains_correctly() {
    # What we're testing: pipelines aren't limited to two commands --
    # the executor should be able to fork and wire up a third stage
    # too, with each stage's output feeding the next one's input in
    # order. sort | head -1 on "3,1,2" should give us "1" first.
    local out
    out=$(run_shell "cat $NUMBERS_FILE | sort | head -1")
    assert_contains "three-stage pipeline (cat | sort | head) sorts correctly" "$out" "1"
}

test_pipeline_output_can_still_be_redirected() {
    # What we're testing: explicit '>' redirection at the end of a
    # pipeline must win over pipe wiring -- "cmd1 | cmd2 > file" has
    # to write cmd2's output to the file, not try to feed a
    # (nonexistent) third pipeline stage.
    local f="$TMPDIR/piped_out.txt"
    run_shell "cat $FRUIT_FILE | grep cherry > $f" >/dev/null
    assert_eq "redirection after a pipe still targets the file" "$(cat "$f")" "cherry"
}

test_background_job_returns_prompt_immediately() {
    # What we're testing: "sleep 2 &" must hand control straight back
    # instead of blocking for 2 seconds -- proven here by the whole
    # test finishing quickly -- and the job should be announced with
    # a "[1] <pid>" line right away.
    # This one can't use run_shell()'s normal $(...) capture: command
    # substitution reads until EVERY process holding its pipe's write
    # end closes it, and a backgrounded child inherits that same pipe
    # as its stdout/stderr. So even though mini_shell itself returns
    # instantly, bash would still sit there blocked for the full 2
    # seconds waiting on the orphaned sleep -- an artifact of how
    # $(...) works, not of the shell under test. Redirecting to a
    # plain file sidesteps it: a file has no such "wait for all
    # writers" behavior, so this truly measures mini_shell's own
    # runtime.
    local capture="$TMPDIR/bg_timing.txt"
    local start=$SECONDS
    printf '%s\n' "sleep 2 &" "jobs" | "$SHELL_BIN" > "$capture" 2>&1
    local elapsed=$((SECONDS - start))
    local out
    out=$(cat "$capture")
    assert_contains "backgrounding announces the job as '[1] <pid>'" "$out" "[1]"
    assert_true "does not block waiting for the backgrounded sleep to finish" "$((elapsed < 2))"
}

test_jobs_builtin_lists_running_background_job() {
    # What we're testing: right after backgrounding a long-running
    # command, `jobs` should list it with state "Running" and show the
    # original command text, so the user can see what's still going.
    local out
    out=$(run_shell "sleep 2 &" "jobs")
    assert_contains "'jobs' shows the backgrounded command as Running" "$out" "Running"
    assert_contains "'jobs' shows the original command text" "$out" "sleep 2"
}

test_background_job_reports_done_on_completion() {
    # What we're testing: once a backgrounded command actually
    # finishes on its own, the shell should notice without being
    # asked -- via the non-blocking reap that runs before every new
    # prompt -- and print a "Done" line for it.
    local out
    out=$(run_shell "sleep 1 &" "sleep 2")
    assert_contains "finished background job is auto-reported as Done" "$out" "Done"
}

test_cd_changes_shell_working_directory() {
    # What we're testing: `cd` has to run inside the shell's OWN
    # process, not a forked child -- otherwise chdir() would only
    # affect that short-lived child and the shell's directory would
    # never actually change. We cd into TMPDIR, then ask `pwd` (a real
    # forked child) to confirm it inherited that new directory.
    local out expected
    expected=$(cd "$TMPDIR" && pwd)
    out=$(run_shell "cd $TMPDIR" "pwd")
    assert_contains "cd changes the directory pwd (and children) see" "$out" "$expected"
}

test_cd_with_no_args_goes_home() {
    # What we're testing: `cd` with no arguments should fall back to
    # $HOME, matching every real shell's behavior.
    local out
    out=$(HOME="$TMPDIR" run_shell "cd" "pwd")
    assert_contains "'cd' with no arguments goes to \$HOME" "$out" "$TMPDIR"
}

test_cd_invalid_path_reports_error_and_does_not_move() {
    # What we're testing: cd-ing into a path that doesn't exist should
    # print a clear error (not crash) and leave the shell's working
    # directory exactly where it was -- it must not silently succeed.
    local out
    out=$(cd "$TMPDIR" && run_shell "cd /no/such/directory_xyz" "pwd")
    assert_contains "invalid cd target reports an error" "$out" "No such file or directory"
    assert_contains "a failed cd leaves the working directory unchanged" "$out" "$TMPDIR"
}

test_exit_terminates_shell_before_later_commands() {
    # What we're testing: `exit` must end the shell PROCESS itself
    # (proving it's a real builtin, not just another forked external
    # command) -- so nothing typed after it should ever run.
    local out
    out=$(run_shell "exit" "echo should_never_print")
    assert_not_contains "no command after exit is executed" "$out" "should_never_print"
}

test_exit_gives_zero_status() {
    # What we're testing: a plain `exit` should leave mini_shell with
    # a successful (0) process exit status, same as a real shell.
    run_shell "exit" >/dev/null
    assert_eq "'exit' leaves a zero exit status" "$?" "0"
}

# --- run everything ---

echo "== mini_unix_shell non-interactive test suite =="
test_simple_command_runs
test_missing_command_reports_error_and_shell_survives
test_output_redirection_writes_to_file_not_terminal
test_output_redirection_truncates_existing_file
test_append_redirection_adds_without_erasing
test_input_redirection_feeds_file_to_command
test_two_stage_pipeline_connects_processes
test_three_stage_pipeline_chains_correctly
test_pipeline_output_can_still_be_redirected
test_background_job_returns_prompt_immediately
test_jobs_builtin_lists_running_background_job
test_background_job_reports_done_on_completion
test_cd_changes_shell_working_directory
test_cd_with_no_args_goes_home
test_cd_invalid_path_reports_error_and_does_not_move
test_exit_terminates_shell_before_later_commands
test_exit_gives_zero_status

echo
echo "== $PASS passed, $FAIL failed =="
[[ $FAIL -eq 0 ]]
