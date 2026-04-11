# aricode Semantic Analyzer - Makefile
# Build the semantic analyzer library and test suite.

CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -g -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS  += -I.. -I. -Wno-unused-variable

# Source files
TYPES_SRC      = types.c
SYMTAB_SRC     = symbol_table.c
ANALYZER_SRC   = analyzer.c
TEST_SRC       = analyzer_test.c
AST_SRC        = ../parser/ast.c

# Object files
TYPES_OBJ      = types.o
SYMTAB_OBJ     = symbol_table.o
ANALYZER_OBJ   = analyzer.o
TEST_OBJ       = analyzer_test.o
AST_OBJ        = ast.o

# Output
TEST_BIN       = analyzer_test

.PHONY: all clean test

all: $(TEST_BIN)

$(TYPES_OBJ): $(TYPES_SRC) types.h
	$(CC) $(CFLAGS) -c $(TYPES_SRC) -o $@

$(SYMTAB_OBJ): $(SYMTAB_SRC) symbol_table.h types.h
	$(CC) $(CFLAGS) -c $(SYMTAB_SRC) -o $@

$(ANALYZER_OBJ): $(ANALYZER_SRC) analyzer.h symbol_table.h types.h ../parser/ast.h ../errors/error_levels.h ../errors/error_codes.h
	$(CC) $(CFLAGS) -c $(ANALYZER_SRC) -o $@

$(TEST_OBJ): $(TEST_SRC) analyzer.h symbol_table.h types.h ../parser/ast.h
	$(CC) $(CFLAGS) -c $(TEST_SRC) -o $@

$(AST_OBJ): $(AST_SRC) ../parser/ast.h
	$(CC) $(CFLAGS) -c $(AST_SRC) -o $@

$(TEST_BIN): $(TYPES_OBJ) $(SYMTAB_OBJ) $(ANALYZER_OBJ) $(TEST_OBJ) $(AST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TYPES_OBJ) $(SYMTAB_OBJ) $(ANALYZER_OBJ) $(TEST_OBJ) $(AST_OBJ) $(TEST_BIN)
