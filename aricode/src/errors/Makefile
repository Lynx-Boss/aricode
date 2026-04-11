# ═══════════════════════════════════════════════════════════
# ARICODE Error System & Podium System - Build
# ═══════════════════════════════════════════════════════════
#
# Targets:
#   make all          - build everything
#   make test         - build and run all tests
#   make run_errors   - build and run error system tests
#   make run_podium   - build and run podium system tests
#   make clean        - remove build artifacts

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS =

# Source files
ERROR_SRCS = error_system.c error_registry.c
PODIUM_SRCS = podium.c

# Test binaries
TEST_ERRORS_BIN = test_errors_bin
TEST_PODIUM_BIN = test_podium_bin

# ── Phony targets ─────────────────────────────────────────

.PHONY: all test run_errors run_podium clean

# ── Default: build everything ─────────────────────────────

all: $(TEST_ERRORS_BIN) $(TEST_PODIUM_BIN)

# ── Build test binaries ──────────────────────────────────

$(TEST_ERRORS_BIN): test_errors.c $(ERROR_SRCS) error_levels.h error_codes.h error_system.h error_registry.h
	$(CC) $(CFLAGS) -o $@ test_errors.c $(ERROR_SRCS) $(LDFLAGS)

$(TEST_PODIUM_BIN): test_podium.c $(PODIUM_SRCS) $(ERROR_SRCS) podium.h error_system.h error_levels.h error_registry.h
	$(CC) $(CFLAGS) -o $@ test_podium.c $(PODIUM_SRCS) $(ERROR_SRCS) $(LDFLAGS)

# ── Run tests ─────────────────────────────────────────────

test: $(TEST_ERRORS_BIN) $(TEST_PODIUM_BIN)
	@echo ""
	@echo "=== Running Error System Tests ==="
	@echo ""
	./$(TEST_ERRORS_BIN)
	@echo ""
	@echo "=== Running Podium System Tests ==="
	@echo ""
	./$(TEST_PODIUM_BIN)
	@echo ""
	@echo "=== ALL TESTS COMPLETE ==="

run_errors: $(TEST_ERRORS_BIN)
	./$(TEST_ERRORS_BIN)

run_podium: $(TEST_PODIUM_BIN)
	./$(TEST_PODIUM_BIN)

# ── Clean ─────────────────────────────────────────────────

clean:
	rm -f $(TEST_ERRORS_BIN) $(TEST_PODIUM_BIN) *.o
	rm -rf .aricode
