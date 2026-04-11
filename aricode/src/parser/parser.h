/*
 * aricode - Ari Code Language
 * Parser interface (recursive descent with error recovery).
 *
 * The parser consumes an array of tokens produced by the lexer and
 * builds an AST.  On syntax errors it records precise diagnostics
 * (line, column, expected vs. found) and attempts to recover so that
 * multiple errors can be reported in a single pass.
 */

#ifndef ARICODE_PARSER_H
#define ARICODE_PARSER_H

#include "ast.h"
#include "../lexer/tokens.h"

#include <stddef.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  ParserToken (minimal representation needed by the parser)         */
/* ------------------------------------------------------------------ */

typedef struct {
    TokenType   type;
    char       *lexeme;   /* textual content (owned copy or pointer)  */
    int         line;
    int         col;
} ParserToken;

/* ------------------------------------------------------------------ */
/*  Parser error                                                      */
/* ------------------------------------------------------------------ */

#define PARSER_MAX_ERRORS 64

typedef struct {
    int         line;
    int         col;
    char        message[256];
} ParserError;

/* ------------------------------------------------------------------ */
/*  Parser state                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const ParserToken  *tokens;
    size_t              token_count;
    size_t              pos;          /* current index into tokens[]      */

    ParserError         errors[PARSER_MAX_ERRORS];
    size_t              error_count;

    bool                panic_mode;  /* suppresses cascading errors       */
} Parser;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/*
 * Initialise the parser with a token array.
 * The parser does NOT take ownership of the tokens.
 */
void parser_init(Parser *p, const ParserToken *tokens, size_t token_count);

/*
 * Run the parser.  Returns the root NODE_PROGRAM on success (the
 * caller owns it and must ast_free() it).  If there are syntax errors
 * the tree may be partial but is still valid to free.
 */
ASTNode *parser_parse(Parser *p);

/*
 * Return true if the parser recorded at least one error.
 */
bool parser_has_errors(const Parser *p);

/*
 * Print all collected errors to stderr.
 */
void parser_print_errors(const Parser *p);

#endif /* ARICODE_PARSER_H */
