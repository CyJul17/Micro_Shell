CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS ?=
LDFLAGS ?=

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
endif

TARGET := minishell$(EXEEXT)
SRC := \
	Shell/main.c \
	Shell/parser.c \
	Shell/builtin.c \
	Shell/scheduler.c \
	Shell/deadlock.c \
	Shell/utils.c \
	Shell/shell_getline.c
OBJ := $(SRC:Shell/%.c=build/%.o)

.PHONY: all clean test run compare token-demo

all: $(TARGET)

build:
	mkdir -p build

build/%.o: Shell/%.c Shell/main.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@

test: $(TARGET)
	bash tests/run_tests.sh

run: $(TARGET)
	./$(TARGET)

compare: $(TARGET)
	bash tests/compare.sh tests/processes/mixed.txt

# Optional standalone lexer demo; not part of the shell.
token-demo: Token/tokens.c
	$(CC) $(CFLAGS) $< -o token_demo$(EXEEXT)

clean:
	rm -rf build $(TARGET) minishell minishell.exe token_demo token_demo.exe
