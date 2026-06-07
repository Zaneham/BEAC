# Makefile -- BEAC, the Burroughs Extended ALGOL Compiler

CC       = gcc
WFLAGS   = -std=c99 -Wall -Wextra -Wpedantic -Werror \
           -Wshadow -Wconversion -Wstrict-prototypes \
           -Wmissing-prototypes -Wold-style-definition \
           -Wdouble-promotion -Wundef -Wno-unused-function \
           -Wformat=2 -Wswitch-enum -Wnull-dereference \
           -Wstack-usage=4096 \
           -fno-common -fstack-protector-strong \
           -Isrc -Isrc/fe -Isrc/ir -Isrc/x86
CFLAGS   = $(WFLAGS) -O2
TFLAGS   = $(WFLAGS) -O0
LDFLAGS  =

# ALGOL front end (BEAC's own)
SRC_FE   = src/fe/a_lex.c src/fe/a_parse.c src/fe/a_sema.c \
           src/fe/a_types.c src/fe/a_lower.c
# Shared SSA backend (mem2reg + x86-64 emit), inherited from Skyhawk
SRC_BE   = src/ir/jir_mem2reg.c \
           src/x86/x86_emit.c src/x86/x86_ra.c src/x86/x86_coff.c
SRCS     = src/a_main.c $(SRC_FE) $(SRC_BE)

# Tests
TSRC     = tests/tmain.c tests/ta_lex.c tests/ta_parse.c tests/ta_stmt.c \
           tests/ta_decl.c tests/ta_proc.c tests/ta_sema.c tests/ta_types.c \
           tests/ta_lower.c tests/ta_run.c
TSRCS    = $(TSRC) $(SRC_FE) $(SRC_BE)

BIN      = beac
TBIN     = test_beac

ifeq ($(OS),Windows_NT)
  BIN   := $(BIN).exe
  TBIN  := $(TBIN).exe
endif

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TBIN): $(TSRCS)
	$(CC) $(TFLAGS) -Itests -o $@ $^ $(LDFLAGS)

test: $(TBIN)
	./$(TBIN)

clean:
	rm -f $(BIN) $(TBIN) *.o
