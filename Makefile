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

.PHONY: all clean
