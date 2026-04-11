#ifndef ARICODE_LEXER_H
#define ARICODE_LEXER_H

#include "tokens.h"
#include <stddef.h>

#define ARICODE_MAX_TOKEN_LEN 4096

/* ------------------------------------------------------------------ */
/*  Token                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    TokenType   type;
    char        value[ARICODE_MAX_TOKEN_LEN];
    int         line;
    int         column;
    const char *file;
} Token;

/* ------------------------------------------------------------------ */
/*  Lexer                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *source;      /* full source text (not owned)          */
    size_t      length;      /* strlen(source)                        */
    size_t      pos;         /* current byte position                 */
    int         line;        /* 1-based line number                   */
    int         column;      /* 1-based column number                 */
    const char *filename;    /* source file name (not owned)          */

    /* peek-ahead cache */
    int         has_peeked;
    Token       peeked;
} Lexer;

/* ------------------------------------------------------------------ */
/*  Token list returned by lexer_tokenize_all                         */
/* ------------------------------------------------------------------ */
typedef struct {
    Token  *tokens;
    size_t  count;
    size_t  capacity;
} TokenList;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/* Initialise a lexer over `source` (must remain valid for the lexer's
   lifetime).  `filename` is used only in error messages.              */
void  lexer_init(Lexer *lex, const char *source, const char *filename);

/* Return the next token, advancing the cursor.                        */
Token lexer_next_token(Lexer *lex);

/* Return the next token WITHOUT advancing the cursor.                 */
Token lexer_peek(Lexer *lex);

/* Tokenize the entire source and return a TokenList.
   The caller must call token_list_free() when done.                   */
TokenList lexer_tokenize_all(Lexer *lex);

/* Free a TokenList returned by lexer_tokenize_all.                    */
void  token_list_free(TokenList *list);

#endif /* ARICODE_LEXER_H */
