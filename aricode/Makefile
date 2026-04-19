# ===============================================================
# ARICODE - Ari Code Programming Language
# Master Build System
# ===============================================================
#
# Targets:
#   make all        - build everything (lexer, parser, errors, compiler)
#   make test       - run MODULE unit tests (lexer, parser, errors)
#   make test-e2e   - run END-TO-END aricode suites (run_all.sh + run_edge.sh)
#   make test-all   - run BOTH: module unit tests + end-to-end suites
#   make clean      - clean all build artifacts
#   make compiler   - build just the aric compiler
#   make install    - copy aric to /usr/local/bin (requires sudo)
#   make lexer      - build the lexer module
#   make parser     - build the parser module
#   make errors     - build the error/podium system

# ── Subdirectories ────────────────────────────────────────────

LEXER_DIR   = src/lexer
PARSER_DIR  = src/parser
ERRORS_DIR  = src/errors

# ── Phony targets ─────────────────────────────────────────────

.PHONY: all test test-e2e test-all clean compiler install lexer parser errors

# ── Default: build everything ─────────────────────────────────

all: lexer parser errors

# ── Module targets ────────────────────────────────────────────

lexer:
	@echo "=== Building Lexer ==="
	$(MAKE) -C $(LEXER_DIR) all

parser:
	@echo "=== Building Parser ==="
	$(MAKE) -C $(PARSER_DIR) all

errors:
	@echo "=== Building Error System ==="
	$(MAKE) -C $(ERRORS_DIR) all

# ── Compiler target (placeholder until main.c exists) ─────────

compiler: lexer parser errors
	@echo "=== Building aric compiler ==="
	@echo "[TODO] Compiler entry point (src/main.c) not yet created."

# ── Install ───────────────────────────────────────────────────

install: compiler
	@echo "=== Installing aric to /usr/local/bin ==="
	sudo cp -f build/aric /usr/local/bin/aric
	@echo "Installed successfully."

# ── Tests ─────────────────────────────────────────────────────

test:
	@echo ""
	@echo "========================================"
	@echo "  ARICODE - Running All Test Suites"
	@echo "========================================"
	@echo ""
	@echo "--- Lexer Tests ---"
	$(MAKE) -C $(LEXER_DIR) test
	@echo ""
	@echo "--- Parser Tests ---"
	$(MAKE) -C $(PARSER_DIR) test
	@echo ""
	@echo "--- Error & Podium Tests ---"
	$(MAKE) -C $(ERRORS_DIR) test
	@echo ""
	@echo "========================================"
	@echo "  ALL TEST SUITES PASSED"
	@echo "========================================"

# ── End-to-end suites (compile .ari programs, run binaries) ─

test-e2e:
	@echo ""
	@echo "========================================"
	@echo "  ARICODE - End-to-End Test Suites"
	@echo "========================================"
	@if [ ! -x src/compiler/aric ]; then \
	    echo "ERROR: aric compiler not built at src/compiler/aric"; \
	    echo "Run: cd src/compiler && make"; \
	    exit 1; \
	fi
	@echo ""
	@echo "--- Functional tests (run_all.sh, 39 cases) ---"
	@bash tests/run_all.sh
	@echo ""
	@echo "--- Edge-case regressions (run_edge.sh, 21 cases) ---"
	@bash tests/run_edge.sh

# ── The whole kitchen sink ──────────────────────────────────

test-all: test test-e2e
	@echo ""
	@echo "========================================"
	@echo "  MODULE UNIT TESTS + E2E SUITES PASSED"
	@echo "========================================"

# ── Clean ─────────────────────────────────────────────────────

clean:
	@echo "=== Cleaning all build artifacts ==="
	$(MAKE) -C $(LEXER_DIR) clean
	$(MAKE) -C $(PARSER_DIR) clean
	$(MAKE) -C $(ERRORS_DIR) clean
	rm -rf build/
	rm -rf .aricode/
	@echo "Clean complete."
