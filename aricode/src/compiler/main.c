/*
 * ARICODE Compiler - CLI Entry Point
 * ===================================
 * Usage: aric <file.ari> [options]
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
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

    const char *inputs[64];
    int input_count = 0;

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
        } else if (strcmp(argv[i], "--avx2") == 0) {
            opts.use_avx2 = true;
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
            if (input_count < 64)
                inputs[input_count++] = argv[i];
        }
    }

    print_banner();

    if (input_count == 0) {
        fprintf(stderr, "Error: no input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Multi-file: concatenate all input files into one source string */
    if (input_count == 1) {
        opts.input_file = inputs[0];
        Compiler compiler;
        compiler_init(&compiler, opts);
        return compiler_compile_file(&compiler, inputs[0]);
    }

    /* Multiple files: read and concatenate */
    size_t total_size = 0;
    for (int i = 0; i < input_count; i++) {
        FILE *f = fopen(inputs[i], "r");
        if (!f) {
            fprintf(stderr, "Error: could not open '%s'\n", inputs[i]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        total_size += (size_t)ftell(f) + 2; /* +2 for newline separator */
        fclose(f);
    }

    char *combined = malloc(total_size + 1);
    if (!combined) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    size_t pos = 0;

    for (int i = 0; i < input_count; i++) {
        FILE *f = fopen(inputs[i], "r");
        fseek(f, 0, SEEK_END);
        size_t sz = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        fread(combined + pos, 1, sz, f);
        pos += sz;
        combined[pos++] = '\n';
        fclose(f);
    }
    combined[pos] = '\0';

    opts.input_file = inputs[0];
    Compiler compiler;
    compiler_init(&compiler, opts);
    return compiler_compile_string(&compiler, combined, inputs[0]);
}
