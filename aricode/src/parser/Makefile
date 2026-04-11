# aricode Parser - Makefile
# Build the parser library and test suite.

CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -g -O2
CFLAGS  += -I.. -I. -Wno-unused-variable

# Source files
AST_SRC    = ast.c
PARSER_SRC = parser.c
TEST_SRC   = parser_test.c

# Object files
AST_OBJ    = ast.o
PARSER_OBJ = parser.o
TEST_OBJ   = parser_test.o

# Output
TEST_BIN   = parser_test

.PHONY: all clean test

all: $(TEST_BIN)

$(AST_OBJ): $(AST_SRC) ast.h
	$(CC) $(CFLAGS) -c $(AST_SRC) -o $@

$(PARSER_OBJ): $(PARSER_SRC) parser.h ast.h ../lexer/tokens.h
	$(CC) $(CFLAGS) -c $(PARSER_SRC) -o $@

$(TEST_OBJ): $(TEST_SRC) parser.h ast.h ../lexer/tokens.h
	$(CC) $(CFLAGS) -c $(TEST_SRC) -o $@

$(TEST_BIN): $(AST_OBJ) $(PARSER_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(AST_OBJ) $(PARSER_OBJ) $(TEST_OBJ) $(TEST_BIN)
