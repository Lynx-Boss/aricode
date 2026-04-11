# Makefile for aricode sparring binary analysis tools
# Build: make
# Test:  make test
# Clean: make clean

CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -pedantic -std=c11
TOOLS_DIR = $(shell pwd)

# C tools to build
C_TOOLS  = cycle_estimator

# Shell tools (just need +x)
SH_TOOLS = analyze_binary.sh compare_binaries.sh

# Python tools
PY_TOOLS = instruction_efficiency.py

# Test binary source
TEST_SRC = test_sample.c

.PHONY: all clean test permissions help

all: $(C_TOOLS) permissions

cycle_estimator: cycle_estimator.c
	$(CC) $(CFLAGS) -o $@ $<

permissions:
	@chmod +x $(SH_TOOLS) $(PY_TOOLS) 2>/dev/null || true

# ---- Test infrastructure ----

test_sample.c:
	@echo '/* Auto-generated test sample for binary analysis tools */' > $@
	@echo '#include <stdio.h>' >> $@
	@echo '#include <stdlib.h>' >> $@
	@echo '' >> $@
	@echo 'int compute(int a, int b) {' >> $@
	@echo '    int result = 0;' >> $@
	@echo '    for (int i = 0; i < a; i++) {' >> $@
	@echo '        result += b * i;' >> $@
	@echo '        if (result > 1000) {' >> $@
	@echo '            result = result / 3;' >> $@
	@echo '        }' >> $@
	@echo '    }' >> $@
	@echo '    return result;' >> $@
	@echo '}' >> $@
	@echo '' >> $@
	@echo 'int main(int argc, char *argv[]) {' >> $@
	@echo '    int a = argc > 1 ? atoi(argv[1]) : 10;' >> $@
	@echo '    int b = argc > 2 ? atoi(argv[2]) : 5;' >> $@
	@echo '    int r = compute(a, b);' >> $@
	@echo '    printf("Result: %d\\n", r);' >> $@
	@echo '    return r;' >> $@
	@echo '}' >> $@

test_O0: test_sample.c
	$(CC) -O0 -o $@ $<

test_O2: test_sample.c
	$(CC) -O2 -o $@ $<

test_Os: test_sample.c
	$(CC) -Os -o $@ $<

test: all test_O0 test_O2 test_Os
	@echo ""
	@echo "============================================"
	@echo "  Running binary analysis tool tests"
	@echo "============================================"
	@echo ""
	@echo "---- Test 1: analyze_binary.sh (O0) ----"
	./analyze_binary.sh ./test_O0 compute
	@echo ""
	@echo "---- Test 2: analyze_binary.sh (O2) ----"
	./analyze_binary.sh ./test_O2 compute
	@echo ""
	@echo "---- Test 3: compare_binaries.sh ----"
	./compare_binaries.sh ./test_O0 ./test_O2 ./test_Os --func compute
	@echo ""
	@echo "---- Test 4: cycle_estimator (O0) ----"
	./cycle_estimator ./test_O0 compute
	@echo ""
	@echo "---- Test 5: cycle_estimator (O2) ----"
	./cycle_estimator ./test_O2 compute
	@echo ""
	@echo "---- Test 6: instruction_efficiency.py (O0) ----"
	python3 ./instruction_efficiency.py ./test_O0 compute
	@echo ""
	@echo "---- Test 7: instruction_efficiency.py (O2) ----"
	python3 ./instruction_efficiency.py ./test_O2 compute
	@echo ""
	@echo "============================================"
	@echo "  All tests completed successfully."
	@echo "============================================"

clean:
	rm -f $(C_TOOLS) test_O0 test_O2 test_Os test_sample.c

help:
	@echo "Aricode Sparring Binary Analysis Tools"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build C tools, set permissions (default)"
	@echo "  test         Build everything and run full test suite"
	@echo "  clean        Remove built files and test artifacts"
	@echo "  help         Show this message"
	@echo ""
	@echo "Tools:"
	@echo "  analyze_binary.sh       Deep analysis of a single binary"
	@echo "  compare_binaries.sh     Side-by-side comparison of multiple binaries"
	@echo "  cycle_estimator         CPU cycle estimation (Zen 3)"
	@echo "  instruction_efficiency.py  Instruction efficiency scoring"
