/*
 * aricode lexer -- tokenizer for the Ari Code language.
 *
 * Handles: keywords, types, error levels, operators, delimiters,
 *          identifiers, integers (dec/hex/bin), floats, strings with
 *          escape sequences, single-line and multi-line comments.
 *
 * On any error the lexer emits a TOKEN_ILLEGAL whose value contains a
 * human-readable diagnostic (line, column, character, suggestion).
 */

#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static inline char current(const Lexer *lex)
{
    return lex->pos < lex->length ? lex->source[lex->pos] : '\0';
}

static inline char peek_char(const Lexer *lex)
{
    return (lex->pos + 1) < lex->length ? lex->source[lex->pos + 1] : '\0';
}

static inline void advance(Lexer *lex)
{
    if (lex->pos < lex->length) {
        if (lex->source[lex->pos] == '\n') {
            lex->line++;
            lex->column = 1;
        } else {
            lex->column++;
        }
        lex->pos++;
    }
}

/* Make a token with the value already filled in by the caller. */
static Token make_token(TokenType type, const char *value,
                        int line, int col, const char *file)
{
    Token t;
    t.type   = type;
    t.line   = line;
    t.column = col;
    t.file   = file;
    strncpy(t.value, value, ARICODE_MAX_TOKEN_LEN - 1);
    t.value[ARICODE_MAX_TOKEN_LEN - 1] = '\0';
    return t;
}

static Token make_error(Lexer *lex, char bad, const char *suggestion)
{
    char buf[ARICODE_MAX_TOKEN_LEN];
    snprintf(buf, sizeof(buf),
             "[%s:%d:%d] Unexpected character '%c' (0x%02X). %s",
             lex->filename, lex->line, lex->column,
             (bad >= 32 && bad < 127) ? bad : '?',
             (unsigned char)bad,
             suggestion ? suggestion : "");
    return make_token(TOKEN_ILLEGAL, buf, lex->line, lex->column,
                      lex->filename);
}

/* ------------------------------------------------------------------ */
/*  Keyword / type / level lookup                                     */
/* ------------------------------------------------------------------ */

typedef struct { const char *word; TokenType type; } KWEntry;

static const KWEntry keywords[] = {
    { "fn",     TOKEN_FN },
    { "let",    TOKEN_LET },
    { "const",  TOKEN_CONST },
    { "if",     TOKEN_IF },
    { "else",   TOKEN_ELSE },
    { "for",    TOKEN_FOR },
    { "while",  TOKEN_WHILE },
    { "return", TOKEN_RETURN },
    { "match",  TOKEN_MATCH },
    { "try",    TOKEN_TRY },
    { "catch",  TOKEN_CATCH },
    { "error",  TOKEN_ERROR },
    { "log",    TOKEN_LOG },
    { "import", TOKEN_IMPORT },
    { "export", TOKEN_EXPORT },
    { "in",     TOKEN_IN },
    { "struct", TOKEN_STRUCT },
    { "break",    TOKEN_BREAK },
    { "continue", TOKEN_CONTINUE },
    { "true",   TOKEN_TRUE },
    { "false",  TOKEN_FALSE },
    { "Some",   TOKEN_SOME },
    { "None",   TOKEN_NONE },
    { "Option", TOKEN_OPTION },
    /* types */
    { "i8",   TOKEN_TYPE_I8 },
    { "i16",  TOKEN_TYPE_I16 },
    { "i32",  TOKEN_TYPE_I32 },
    { "i64",  TOKEN_TYPE_I64 },
    { "u8",   TOKEN_TYPE_U8 },
    { "u16",  TOKEN_TYPE_U16 },
    { "u32",  TOKEN_TYPE_U32 },
    { "u64",  TOKEN_TYPE_U64 },
    { "f32",  TOKEN_TYPE_F32 },
    { "f64",  TOKEN_TYPE_F64 },
    { "str",  TOKEN_TYPE_STR },
    { "bool", TOKEN_TYPE_BOOL },
    { "arr",  TOKEN_TYPE_ARR },
    { "map",  TOKEN_TYPE_MAP },
};
static const size_t kw_count = sizeof(keywords) / sizeof(keywords[0]);

static TokenType lookup_keyword(const char *ident)
{
    for (size_t i = 0; i < kw_count; i++) {
        if (strcmp(ident, keywords[i].word) == 0)
            return keywords[i].type;
    }
    return TOKEN_IDENTIFIER;
}

/* ------------------------------------------------------------------ */
/*  Error level lookup: "Level.XXX"                                   */
/* ------------------------------------------------------------------ */

typedef struct { const char *suffix; TokenType type; } LevelEntry;

static const LevelEntry levels[] = {
    { "SILENT",       TOKEN_LEVEL_SILENT },
    { "LOGIC",        TOKEN_LEVEL_LOGIC },
    { "WARNING",      TOKEN_LEVEL_WARNING },
    { "SYSTEM",       TOKEN_LEVEL_SYSTEM },
    { "CATASTROPHIC", TOKEN_LEVEL_CATASTROPHIC },
};
static const size_t level_count = sizeof(levels) / sizeof(levels[0]);

/* Try to consume "Level.XXX" starting from current position.
   Returns 1 and fills `out` on success; returns 0 otherwise.         */
static int try_level(Lexer *lex, Token *out)
{
    /* We already know current ident is "Level" -- check for dot. */
    size_t saved_pos = lex->pos;
    int    saved_line = lex->line;
    int    saved_col  = lex->column;

    /* Skip past "Level" (5 chars) */
    for (int i = 0; i < 5; i++) advance(lex);

    if (current(lex) != '.') {
        /* Not a level -- rewind */
        lex->pos    = saved_pos;
        lex->line   = saved_line;
        lex->column = saved_col;
        return 0;
    }
    advance(lex); /* skip '.' */

    /* Read suffix */
    char suffix[32];
    int  si = 0;
    while (isalpha((unsigned char)current(lex)) && si < (int)sizeof(suffix) - 1) {
        suffix[si++] = current(lex);
        advance(lex);
    }
    suffix[si] = '\0';

    for (size_t i = 0; i < level_count; i++) {
        if (strcmp(suffix, levels[i].suffix) == 0) {
            char full[64];
            snprintf(full, sizeof(full), "Level.%s", suffix);
            *out = make_token(levels[i].type, full, saved_line, saved_col,
                              lex->filename);
            return 1;
        }
    }

    /* Unknown level suffix -- rewind and let it be parsed as ident */
    lex->pos    = saved_pos;
    lex->line   = saved_line;
    lex->column = saved_col;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Skip whitespace and comments                                      */
/* ------------------------------------------------------------------ */

static void skip_whitespace_and_comments(Lexer *lex)
{
    while (lex->pos < lex->length) {
        char c = current(lex);

        /* whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
            continue;
        }

        /* single-line comment */
        if (c == '/' && peek_char(lex) == '/') {
            while (lex->pos < lex->length && current(lex) != '\n')
                advance(lex);
            continue;
        }

        /* multi-line comment */
        if (c == '/' && peek_char(lex) == '*') {
            int start_line = lex->line;
            int start_col  = lex->column;
            advance(lex); /* '/' */
            advance(lex); /* '*' */
            while (lex->pos < lex->length) {
                if (current(lex) == '*' && peek_char(lex) == '/') {
                    advance(lex);
                    advance(lex);
                    break;
                }
                advance(lex);
            }
            (void)start_line;
            (void)start_col;
            continue;
        }

        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Lex a string literal                                              */
/* ------------------------------------------------------------------ */

static Token lex_string(Lexer *lex)
{
    int start_line = lex->line;
    int start_col  = lex->column;
    char quote     = current(lex);
    advance(lex); /* consume opening quote */

    char buf[ARICODE_MAX_TOKEN_LEN];
    int  bi = 0;

    while (lex->pos < lex->length && current(lex) != quote) {
        if (current(lex) == '\n') {
            /* unterminated string at newline */
            char err[ARICODE_MAX_TOKEN_LEN];
            snprintf(err, sizeof(err),
                     "[%s:%d:%d] Unterminated string literal (started at %d:%d). "
                     "Add a closing '%c' before the end of the line.",
                     lex->filename, lex->line, lex->column,
                     start_line, start_col, quote);
            return make_token(TOKEN_ILLEGAL, err, start_line, start_col,
                              lex->filename);
        }
        if (current(lex) == '\\') {
            advance(lex);
            char esc = current(lex);
            switch (esc) {
                case 'n':  buf[bi++] = '\n'; break;
                case 't':  buf[bi++] = '\t'; break;
                case 'r':  buf[bi++] = '\r'; break;
                case '\\': buf[bi++] = '\\'; break;
                case '\'': buf[bi++] = '\''; break;
                case '"':  buf[bi++] = '"';  break;
                case '0':  buf[bi++] = '\0'; break;
                default:
                    buf[bi++] = '\\';
                    buf[bi++] = esc;
                    break;
            }
            advance(lex);
            continue;
        }
        if (bi < ARICODE_MAX_TOKEN_LEN - 1)
            buf[bi++] = current(lex);
        advance(lex);
    }

    if (lex->pos >= lex->length) {
        char err[ARICODE_MAX_TOKEN_LEN];
        snprintf(err, sizeof(err),
                 "[%s:%d:%d] Unterminated string literal (started at %d:%d). "
                 "Add a closing '%c' before end of file.",
                 lex->filename, lex->line, lex->column,
                 start_line, start_col, quote);
        return make_token(TOKEN_ILLEGAL, err, start_line, start_col,
                          lex->filename);
    }

    advance(lex); /* consume closing quote */
    buf[bi] = '\0';
    return make_token(TOKEN_STRING, buf, start_line, start_col,
                      lex->filename);
}

/* ------------------------------------------------------------------ */
/*  Lex a number (int or float, dec/hex/bin)                          */
/* ------------------------------------------------------------------ */

static Token lex_number(Lexer *lex)
{
    int  start_line = lex->line;
    int  start_col  = lex->column;
    char buf[256];
    int  bi = 0;
    int  is_float = 0;

    /* hex */
    if (current(lex) == '0' && (peek_char(lex) == 'x' || peek_char(lex) == 'X')) {
        buf[bi++] = current(lex); advance(lex);
        buf[bi++] = current(lex); advance(lex);
        while (isxdigit((unsigned char)current(lex)) || current(lex) == '_') {
            if (current(lex) != '_') buf[bi++] = current(lex);
            advance(lex);
        }
        buf[bi] = '\0';
        return make_token(TOKEN_INTEGER, buf, start_line, start_col,
                          lex->filename);
    }

    /* binary */
    if (current(lex) == '0' && (peek_char(lex) == 'b' || peek_char(lex) == 'B')) {
        buf[bi++] = current(lex); advance(lex);
        buf[bi++] = current(lex); advance(lex);
        while (current(lex) == '0' || current(lex) == '1' || current(lex) == '_') {
            if (current(lex) != '_') buf[bi++] = current(lex);
            advance(lex);
        }
        buf[bi] = '\0';
        return make_token(TOKEN_INTEGER, buf, start_line, start_col,
                          lex->filename);
    }

    /* decimal / float */
    while (isdigit((unsigned char)current(lex)) || current(lex) == '_') {
        if (current(lex) != '_') buf[bi++] = current(lex);
        advance(lex);
    }
    if (current(lex) == '.' && isdigit((unsigned char)peek_char(lex))) {
        is_float = 1;
        buf[bi++] = current(lex); advance(lex);
        while (isdigit((unsigned char)current(lex)) || current(lex) == '_') {
            if (current(lex) != '_') buf[bi++] = current(lex);
            advance(lex);
        }
    }
    /* exponent */
    if (current(lex) == 'e' || current(lex) == 'E') {
        is_float = 1;
        buf[bi++] = current(lex); advance(lex);
        if (current(lex) == '+' || current(lex) == '-') {
            buf[bi++] = current(lex); advance(lex);
        }
        while (isdigit((unsigned char)current(lex))) {
            buf[bi++] = current(lex); advance(lex);
        }
    }

    buf[bi] = '\0';
    return make_token(is_float ? TOKEN_FLOAT : TOKEN_INTEGER,
                      buf, start_line, start_col, lex->filename);
}

/* ------------------------------------------------------------------ */
/*  Lex an identifier or keyword                                      */
/* ------------------------------------------------------------------ */

static Token lex_identifier(Lexer *lex)
{
    int  start_line = lex->line;
    int  start_col  = lex->column;
    char buf[ARICODE_MAX_TOKEN_LEN];
    int  bi = 0;

    while (isalnum((unsigned char)current(lex)) || current(lex) == '_') {
        if (bi < ARICODE_MAX_TOKEN_LEN - 1)
            buf[bi++] = current(lex);
        advance(lex);
    }
    buf[bi] = '\0';

    /* Check for "Level.XXX" */
    if (strcmp(buf, "Level") == 0 && current(lex) == '.') {
        /* Rewind to start so try_level can consume from "Level" */
        lex->pos    = lex->pos - 5;
        lex->column = start_col;
        lex->line   = start_line;
        Token level_tok;
        if (try_level(lex, &level_tok))
            return level_tok;
        /* If try_level failed, re-advance past "Level" */
        for (int i = 0; i < 5; i++) advance(lex);
    }

    TokenType tt = lookup_keyword(buf);
    return make_token(tt, buf, start_line, start_col, lex->filename);
}

/* ------------------------------------------------------------------ */
/*  Core: produce the next token                                      */
/* ------------------------------------------------------------------ */

static Token lex_token(Lexer *lex)
{
    skip_whitespace_and_comments(lex);

    if (lex->pos >= lex->length)
        return make_token(TOKEN_EOF, "", lex->line, lex->column,
                          lex->filename);

    int  line = lex->line;
    int  col  = lex->column;
    char c    = current(lex);

    /* String literals */
    if (c == '"' || c == '\'')
        return lex_string(lex);

    /* Numbers */
    if (isdigit((unsigned char)c))
        return lex_number(lex);

    /* Identifiers / keywords */
    if (isalpha((unsigned char)c) || c == '_')
        return lex_identifier(lex);

    /* Two-character operators / delimiters */
    char next = peek_char(lex);

    if (c == '-' && next == '>') { advance(lex); advance(lex); return make_token(TOKEN_ARROW,     "->", line, col, lex->filename); }
    if (c == '=' && next == '>') { advance(lex); advance(lex); return make_token(TOKEN_FAT_ARROW, "=>", line, col, lex->filename); }
    if (c == '=' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_EQ,        "==", line, col, lex->filename); }
    if (c == '!' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_NEQ,       "!=", line, col, lex->filename); }
    if (c == '<' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_LTE,       "<=", line, col, lex->filename); }
    if (c == '>' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_GTE,       ">=", line, col, lex->filename); }
    if (c == '<' && next == '<') { advance(lex); advance(lex); return make_token(TOKEN_SHL,       "<<", line, col, lex->filename); }
    if (c == '>' && next == '>') { advance(lex); advance(lex); return make_token(TOKEN_SHR,       ">>", line, col, lex->filename); }
    if (c == '&' && next == '&') { advance(lex); advance(lex); return make_token(TOKEN_AND,       "&&", line, col, lex->filename); }
    if (c == '|' && next == '|') { advance(lex); advance(lex); return make_token(TOKEN_OR,        "||", line, col, lex->filename); }
    if (c == '+' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_PLUS_ASSIGN,  "+=", line, col, lex->filename); }
    if (c == '-' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_MINUS_ASSIGN, "-=", line, col, lex->filename); }
    if (c == '*' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_STAR_ASSIGN,  "*=", line, col, lex->filename); }
    if (c == '/' && next == '=') { advance(lex); advance(lex); return make_token(TOKEN_SLASH_ASSIGN, "/=", line, col, lex->filename); }

    /* Single-character tokens */
    advance(lex);
    switch (c) {
        case '+': return make_token(TOKEN_PLUS,     "+", line, col, lex->filename);
        case '-': return make_token(TOKEN_MINUS,    "-", line, col, lex->filename);
        case '*': return make_token(TOKEN_STAR,     "*", line, col, lex->filename);
        case '/': return make_token(TOKEN_SLASH,    "/", line, col, lex->filename);
        case '%': return make_token(TOKEN_PERCENT,  "%", line, col, lex->filename);
        case '=': return make_token(TOKEN_ASSIGN,   "=", line, col, lex->filename);
        case '!': return make_token(TOKEN_NOT,      "!", line, col, lex->filename);
        case '<': return make_token(TOKEN_LT,       "<", line, col, lex->filename);
        case '>': return make_token(TOKEN_GT,       ">", line, col, lex->filename);
        case '&': return make_token(TOKEN_BIT_AND,  "&", line, col, lex->filename);
        case '|': return make_token(TOKEN_BIT_OR,   "|", line, col, lex->filename);
        case '^': return make_token(TOKEN_BIT_XOR,  "^", line, col, lex->filename);
        case '~': return make_token(TOKEN_BIT_NOT,  "~", line, col, lex->filename);
        case '?': return make_token(TOKEN_QUESTION, "?", line, col, lex->filename);
        case '(': return make_token(TOKEN_LPAREN,   "(", line, col, lex->filename);
        case ')': return make_token(TOKEN_RPAREN,   ")", line, col, lex->filename);
        case '{': return make_token(TOKEN_LBRACE,   "{", line, col, lex->filename);
        case '}': return make_token(TOKEN_RBRACE,   "}", line, col, lex->filename);
        case '[': return make_token(TOKEN_LBRACKET, "[", line, col, lex->filename);
        case ']': return make_token(TOKEN_RBRACKET, "]", line, col, lex->filename);
        case ';': return make_token(TOKEN_SEMICOLON,";", line, col, lex->filename);
        case ':': return make_token(TOKEN_COLON,    ":", line, col, lex->filename);
        case ',': return make_token(TOKEN_COMMA,    ",", line, col, lex->filename);
        case '.': return make_token(TOKEN_DOT,      ".", line, col, lex->filename);
        default:  break;
    }

    return make_error(lex, c,
                      "Remove this character or replace it with a valid aricode token.");
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void lexer_init(Lexer *lex, const char *source, const char *filename)
{
    lex->source     = source;
    lex->length     = strlen(source);
    lex->pos        = 0;
    lex->line       = 1;
    lex->column     = 1;
    lex->filename   = filename;
    lex->has_peeked = 0;
}

Token lexer_next_token(Lexer *lex)
{
    if (lex->has_peeked) {
        lex->has_peeked = 0;
        return lex->peeked;
    }
    return lex_token(lex);
}

Token lexer_peek(Lexer *lex)
{
    if (!lex->has_peeked) {
        lex->peeked     = lex_token(lex);
        lex->has_peeked = 1;
    }
    return lex->peeked;
}

TokenList lexer_tokenize_all(Lexer *lex)
{
    TokenList list;
    list.capacity = 256;
    list.count    = 0;
    list.tokens   = (Token *)malloc(list.capacity * sizeof(Token));

    while (1) {
        Token t = lexer_next_token(lex);

        if (list.count >= list.capacity) {
            list.capacity *= 2;
            list.tokens = (Token *)realloc(list.tokens,
                                           list.capacity * sizeof(Token));
        }
        list.tokens[list.count++] = t;

        if (t.type == TOKEN_EOF || t.type == TOKEN_ILLEGAL)
            break;
    }
    return list;
}

void token_list_free(TokenList *list)
{
    if (list->tokens) {
        free(list->tokens);
        list->tokens   = NULL;
        list->count    = 0;
        list->capacity = 0;
    }
}
