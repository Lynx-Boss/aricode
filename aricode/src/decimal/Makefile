CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -O2
CFLAGS += -I.. -I. -Wno-unused-variable -Wno-unused-function
LDFLAGS =

# Original decimal module (string-based arbitrary precision)
SRCS = decimal.c decimal_ops.c
OBJS = $(SRCS:.c=.o)
TEST_SRC = test_decimal.c
TEST_BIN = test_decimal

# BCD runtime + codegen integration
RUNTIME_SRC = decimal_runtime.c
CODEGEN_SRC = decimal_codegen.c
CODEGEN_TEST_SRC = test_decimal_codegen.c
AST_SRC = ../parser/ast.c
CG_SRC = ../codegen/codegen.c
ELF_SRC = ../codegen/elf.c

RUNTIME_OBJ = decimal_runtime.o
CODEGEN_OBJ = decimal_codegen.o
CODEGEN_TEST_OBJ = test_decimal_codegen.o
AST_OBJ = ast.o
CG_OBJ = codegen.o
ELF_OBJ = elf.o

CODEGEN_TEST_BIN = test_decimal_codegen

.PHONY: all test test-codegen clean

all: $(TEST_BIN) $(CODEGEN_TEST_BIN)

# --- Original test binary ---

$(TEST_BIN): $(OBJS) $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(OBJS) $(LDFLAGS)

%.o: %.c decimal.h decimal_ops.h
	$(CC) $(CFLAGS) -c $< -o $@

# --- BCD codegen test binary ---

$(RUNTIME_OBJ): $(RUNTIME_SRC) decimal_runtime.h
	$(CC) $(CFLAGS) -c $(RUNTIME_SRC) -o $@

$(CODEGEN_OBJ): $(CODEGEN_SRC) decimal_codegen.h decimal_runtime.h ../codegen/codegen.h ../codegen/x86_64.h
	$(CC) $(CFLAGS) -c $(CODEGEN_SRC) -o $@

$(CODEGEN_TEST_OBJ): $(CODEGEN_TEST_SRC) decimal_runtime.h decimal_codegen.h ../parser/ast.h ../codegen/codegen.h
	$(CC) $(CFLAGS) -c $(CODEGEN_TEST_SRC) -o $@

$(AST_OBJ): $(AST_SRC) ../parser/ast.h
	$(CC) $(CFLAGS) -c $(AST_SRC) -o $@

$(CG_OBJ): $(CG_SRC) ../codegen/codegen.h ../codegen/x86_64.h ../codegen/elf.h ../parser/ast.h
	$(CC) $(CFLAGS) -c $(CG_SRC) -o $@

$(ELF_OBJ): $(ELF_SRC) ../codegen/elf.h
	$(CC) $(CFLAGS) -c $(ELF_SRC) -o $@

$(CODEGEN_TEST_BIN): $(RUNTIME_OBJ) $(CODEGEN_OBJ) $(CODEGEN_TEST_OBJ) $(AST_OBJ) $(CG_OBJ) $(ELF_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

test-codegen: $(CODEGEN_TEST_BIN)
	./$(CODEGEN_TEST_BIN)

clean:
	rm -f $(OBJS) $(TEST_BIN)
	rm -f $(RUNTIME_OBJ) $(CODEGEN_OBJ) $(CODEGEN_TEST_OBJ) $(AST_OBJ) $(CG_OBJ) $(ELF_OBJ) $(CODEGEN_TEST_BIN)
