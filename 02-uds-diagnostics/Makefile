CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O2 -g -Iinclude
LDFLAGS ?=

TEST_BIN := build/test_uds_diag
SRC      := src/uds_diag.c
TEST_SRC := tests/test_uds_diag.c

.PHONY: all test clean

all: test

$(TEST_BIN): $(SRC) $(TEST_SRC) include/uds_diag.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build
