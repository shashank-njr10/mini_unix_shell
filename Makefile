CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -g

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = mini_shell

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

# Runs both test suites: run_tests.sh drives mini_shell over piped
# stdin to check execution/pipes/redirection/jobs/builtins, and
# test_signals.py drives it over a real pseudo-terminal to check the
# Ctrl-C/Ctrl-Z job-control behavior a piped stdin can't exercise.
test: $(TARGET)
	./tests/run_tests.sh
	python3 tests/test_signals.py

.PHONY: all clean test
