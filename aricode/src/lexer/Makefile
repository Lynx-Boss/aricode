CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -O2
LDFLAGS =

SRC     = lexer.c
TEST    = lexer_test.c
OBJ     = lexer.o
BIN     = lexer_test

.PHONY: all clean test

all: $(BIN)

$(OBJ): $(SRC) lexer.h tokens.h
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

$(BIN): $(TEST) $(OBJ) lexer.h tokens.h
	$(CC) $(CFLAGS) $(TEST) $(OBJ) -o $(BIN) $(LDFLAGS)

test: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)
