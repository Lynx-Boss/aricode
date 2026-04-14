# ARICODE Compiler - Makefile
# ===========================
# Builds the `aric` compiler binary from all components.

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -g -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS  += -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable

# Source directories (relative to this Makefile)
LEXER_DIR   = ../lexer
PARSER_DIR  = ../parser
ERRORS_DIR  = ../errors
CODEGEN_DIR = ../codegen
DECIMAL_DIR = ../decimal
SEMANTIC_DIR = ../semantic
COMPILER_DIR = .

# Source files
SRCS = \
	$(COMPILER_DIR)/main.c \
	$(COMPILER_DIR)/compiler.c \
	$(COMPILER_DIR)/imports.c \
	$(LEXER_DIR)/lexer.c \
	$(PARSER_DIR)/parser.c \
	$(PARSER_DIR)/ast.c \
	$(ERRORS_DIR)/error_system.c \
	$(ERRORS_DIR)/error_registry.c \
	$(ERRORS_DIR)/podium.c \
	$(CODEGEN_DIR)/codegen.c \
	$(CODEGEN_DIR)/optimizer.c \
	$(CODEGEN_DIR)/elf.c \
	$(DECIMAL_DIR)/decimal.c \
	$(DECIMAL_DIR)/decimal_ops.c \
	$(SEMANTIC_DIR)/analyzer.c \
	$(SEMANTIC_DIR)/types.c \
	$(SEMANTIC_DIR)/symbol_table.c

# Include paths
INCLUDES = -I$(COMPILER_DIR) -I$(LEXER_DIR) -I$(PARSER_DIR) -I$(ERRORS_DIR) -I$(CODEGEN_DIR) -I$(DECIMAL_DIR) -I$(SEMANTIC_DIR)

# Output binary
TARGET = aric

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

clean:
	rm -f $(TARGET)
	rm -rf .aricode

test: $(TARGET)
	@echo ""
	@echo "=== Testing with hello.ari ==="
	./$(TARGET) ../../examples/hello.ari --tokens --ast
	@echo ""
	@echo "=== Testing with math.ari ==="
	./$(TARGET) ../../examples/math.ari --ast
	@echo ""
	@echo "=== Testing with errors.ari ==="
	./$(TARGET) ../../examples/errors.ari --ast
	@echo ""
	@echo "=== All tests completed ==="
