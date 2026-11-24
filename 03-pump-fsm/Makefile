CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O2 -g -Iinclude -I../01-can-codec/include
LDFLAGS ?= -lm

TEST_BIN := build/test_pump_fsm
SRC      := src/pump_fsm.c ../01-can-codec/src/can_codec.c
TEST_SRC := tests/test_pump_fsm.c

.PHONY: all test clean

all: test

$(TEST_BIN): $(SRC) $(TEST_SRC) include/pump_fsm.h ../01-can-codec/include/can_codec.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build
