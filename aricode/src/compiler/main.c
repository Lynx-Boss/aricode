/*
 * ARICODE Compiler - CLI Entry Point
 * ===================================
 * Usage: aric <file.ari> [options]
 */

#include "compiler.h"

#include <stdio.h>
#include <string.h>

#define ARICODE_VERSION "0.1.0"

static void print_banner(void) {
    printf("\n");
    printf("  \xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97\n");
    printf("  \xe2\x95\x91  ARICODE COMPILER v%s          \xe2\x95\x91\n",
           ARICODE_VERSION);
    printf("  \xe2\x95\x91  Ari Code - Zero Silent Errors    \xe2\x95\x91\n");
    printf("  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\n");
    printf("\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s <file.ari> [options]\n\n", prog);
    printf("Options:\n");
    printf("  -o <output>    Output binary name\n");
    printf("  --ast          Print the Abstract Syntax Tree\n");
    printf("  --tokens       Print the token list\n");
    printf("  --verbose      Verbose output\n");
    printf("  --help         Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s hello.ari\n", prog);
    printf("  %s hello.ari --ast --tokens\n", prog);
    printf("  %s hello.ari -o hello --verbose\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    CompilerOptions opts = {0};
    opts.output_file = "a.out";

    const char *input = NULL;

    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_banner();
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--ast") == 0) {
            opts.show_ast = true;
        } else if (strcmp(argv[i], "--tokens") == 0) {
            opts.show_tokens = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts.verbose = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (input != NULL) {
                fprintf(stderr, "Error: multiple input files not supported\n");
                return 1;
            }
            input = argv[i];
        }
    }

    print_banner();

    if (!input) {
        fprintf(stderr, "Error: no input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    opts.input_file = input;

    Compiler compiler;
    compiler_init(&compiler, opts);

    return compiler_compile_file(&compiler, input);
}
