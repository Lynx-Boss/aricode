# aricode Code Generator - Makefile
# Build the x86_64 code generator library and test suite.

CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -g -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS  += -I.. -I. -Wno-unused-variable -Wno-unused-function

# Source files
AST_SRC      = ../parser/ast.c
ELF_SRC      = elf.c
CODEGEN_SRC  = codegen.c
OPT_SRC      = optimizer.c
TEST_SRC     = codegen_test.c

# Object files
AST_OBJ      = ast.o
ELF_OBJ      = elf.o
CODEGEN_OBJ  = codegen.o
OPT_OBJ      = optimizer.o
TEST_OBJ     = codegen_test.o

# Output
TEST_BIN     = codegen_test

.PHONY: all clean test

all: $(TEST_BIN)

$(AST_OBJ): $(AST_SRC) ../parser/ast.h
	$(CC) $(CFLAGS) -c $(AST_SRC) -o $@

$(ELF_OBJ): $(ELF_SRC) elf.h
	$(CC) $(CFLAGS) -c $(ELF_SRC) -o $@

$(OPT_OBJ): $(OPT_SRC) optimizer.h ../parser/ast.h
	$(CC) $(CFLAGS) -c $(OPT_SRC) -o $@

$(CODEGEN_OBJ): $(CODEGEN_SRC) codegen.h optimizer.h x86_64.h elf.h ../parser/ast.h
	$(CC) $(CFLAGS) -c $(CODEGEN_SRC) -o $@

$(TEST_OBJ): $(TEST_SRC) codegen.h elf.h ../parser/ast.h
	$(CC) $(CFLAGS) -c $(TEST_SRC) -o $@

$(TEST_BIN): $(AST_OBJ) $(ELF_OBJ) $(OPT_OBJ) $(CODEGEN_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(AST_OBJ) $(ELF_OBJ) $(OPT_OBJ) $(CODEGEN_OBJ) $(TEST_OBJ) $(TEST_BIN)
