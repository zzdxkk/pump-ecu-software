CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O2 -g -Iinclude
LDFLAGS ?= -lm

TEST_BIN := build/test_can_codec
SRC      := src/can_codec.c
TEST_SRC := tests/test_can_codec.c

.PHONY: all test clean

all: test

$(TEST_BIN): $(SRC) $(TEST_SRC) include/can_codec.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build
