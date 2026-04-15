# aricode - Ari Code Programming Language

A compiled programming language with JavaScript-like syntax that generates optimized x86_64 machine code.

## Key Features

- **Zero silent errors** - compilation blocks if errors are not handled. No bug passes unnoticed.
- **5-level error system** - errors are classified as SILENT, LOGIC, WARNING, SYSTEM, or CATASTROPHIC, each with its own behavior.
- **Performance podium** - every function gets rated GOLD, SILVER, BRONZE, or IRON based on runtime performance metrics.
- **Compiles to native x86_64 binary** - no interpreter, no VM, just fast machine code.

## Quick Start

```bash
# Build the project
make all

# Compile an aricode source file
./build/aric examples/hello.ari -o hello

# Run the resulting binary
./hello
```

## Project Structure

```
aricode/
├── Makefile              # Master build system
├── README.md             # This file
├── aricode.toml          # Project configuration
├── spec/                 # Language specification
│   ├── LANGUAGE_SPEC.md  # Full language spec
│   ├── ERROR_LEVELS.md   # Error level definitions
│   └── PODIUM.md         # Podium system spec
└── src/
    ├── lexer/            # Tokenizer
    │   ├── lexer.c
    │   ├── lexer.h
    │   ├── tokens.h
    │   ├── lexer_test.c
    │   └── Makefile
    ├── parser/           # AST builder
    │   ├── parser.c
    │   ├── parser.h
    │   ├── ast.c
    │   ├── ast.h
    │   ├── parser_test.c
    │   └── Makefile
    ├── errors/           # Error & podium system
    │   ├── error_system.c / .h
    │   ├── error_registry.c / .h
    │   ├── error_levels.h
    │   ├── error_codes.h
    │   ├── podium.c / .h
    │   ├── test_errors.c
    │   ├── test_podium.c
    │   └── Makefile
    └── semantic/         # Type system
        └── types.h
```

## Error Levels

| Level | Name         | Action   | Description                                  |
|-------|--------------|----------|----------------------------------------------|
| 0     | SILENT       | Block    | Hidden bugs that would go unnoticed silently |
| 1     | LOGIC        | Block    | Logic errors in program flow                 |
| 2     | WARNING      | Warn     | Potential issues, shown but do not block      |
| 3     | SYSTEM       | Block    | OS/runtime-level failures                    |
| 4     | CATASTROPHIC | Abort    | Unrecoverable errors, immediate termination  |

## Performance Podium

| Medal   | Criteria                          | Meaning                        |
|---------|-----------------------------------|--------------------------------|
| GOLD    | Optimal time and space complexity | Best possible implementation   |
| SILVER  | Near-optimal performance          | Good, minor room to improve    |
| BRONZE  | Acceptable but suboptimal         | Works, but could be better     |
| IRON    | Poor performance detected         | Needs refactoring              |

## Building

```bash
make all        # Build everything (lexer, parser, errors)
make test       # Run all test suites
make clean      # Clean all build artifacts
make compiler   # Build the aric compiler
make install    # Install aric to /usr/local/bin (requires sudo)
```

## License

Copyright (c) 2026 Edwin F. Veliz Jaramillo. All rights reserved.
See [LICENSE](../LICENSE) for details.
