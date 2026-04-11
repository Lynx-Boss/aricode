/*
 * aricode lexer test suite.
 *
 * Each test prints PASS or FAIL.  The process exits with 0 only when
 * every test passes.
 */

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static void expect_token(TokenList *list, size_t idx,
                         TokenType expected_type,
                         const char *expected_value,
                         const char *test_name)
{
    tests_run++;
    if (idx >= list->count) {
        printf("  FAIL  %s -- token index %zu out of range (count=%zu)\n",
               test_name, idx, list->count);
        return;
    }
    Token *t = &list->tokens[idx];
    if (t->type != expected_type) {
        printf("  FAIL  %s -- token[%zu] type: expected %s, got %s (value=\"%s\")\n",
               test_name, idx,
               token_type_names[expected_type],
               token_type_names[t->type],
               t->value);
        return;
    }
    if (expected_value && strcmp(t->value, expected_value) != 0) {
        printf("  FAIL  %s -- token[%zu] value: expected \"%s\", got \"%s\"\n",
               test_name, idx, expected_value, t->value);
        return;
    }
    tests_passed++;
    printf("  PASS  %s\n", test_name);
}

static void expect_illegal(const char *source, const char *test_name)
{
    tests_run++;
    Lexer lex;
    lexer_init(&lex, source, "test");
    TokenList list = lexer_tokenize_all(&lex);

    int found = 0;
    for (size_t i = 0; i < list.count; i++) {
        if (list.tokens[i].type == TOKEN_ILLEGAL) {
            found = 1;
            break;
        }
    }
    if (found) {
        tests_passed++;
        printf("  PASS  %s\n", test_name);
    } else {
        printf("  FAIL  %s -- expected ILLEGAL token but none found\n",
               test_name);
    }
    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 1: let x: i32 = 10;                                         */
/* ------------------------------------------------------------------ */

static void test_let_statement(void)
{
    printf("\n--- Test: let x: i32 = 10; ---\n");
    const char *src = "let x: i32 = 10;";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list, 0, TOKEN_LET,       "let",  "let keyword");
    expect_token(&list, 1, TOKEN_IDENTIFIER, "x",   "identifier x");
    expect_token(&list, 2, TOKEN_COLON,      ":",   "colon");
    expect_token(&list, 3, TOKEN_TYPE_I32,   "i32", "type i32");
    expect_token(&list, 4, TOKEN_ASSIGN,     "=",   "assign");
    expect_token(&list, 5, TOKEN_INTEGER,    "10",  "integer 10");
    expect_token(&list, 6, TOKEN_SEMICOLON,  ";",   "semicolon");
    expect_token(&list, 7, TOKEN_EOF,        "",    "EOF");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 2: fn add(a: i32, b: i32) -> i32 { return a + b; }          */
/* ------------------------------------------------------------------ */

static void test_fn_declaration(void)
{
    printf("\n--- Test: fn add(a: i32, b: i32) -> i32 { return a + b; } ---\n");
    const char *src = "fn add(a: i32, b: i32) -> i32 { return a + b; }";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list,  0, TOKEN_FN,         "fn",     "fn keyword");
    expect_token(&list,  1, TOKEN_IDENTIFIER,  "add",   "identifier add");
    expect_token(&list,  2, TOKEN_LPAREN,      "(",     "lparen");
    expect_token(&list,  3, TOKEN_IDENTIFIER,  "a",     "param a");
    expect_token(&list,  4, TOKEN_COLON,       ":",     "colon after a");
    expect_token(&list,  5, TOKEN_TYPE_I32,    "i32",   "type i32 (a)");
    expect_token(&list,  6, TOKEN_COMMA,       ",",     "comma");
    expect_token(&list,  7, TOKEN_IDENTIFIER,  "b",     "param b");
    expect_token(&list,  8, TOKEN_COLON,       ":",     "colon after b");
    expect_token(&list,  9, TOKEN_TYPE_I32,    "i32",   "type i32 (b)");
    expect_token(&list, 10, TOKEN_RPAREN,      ")",     "rparen");
    expect_token(&list, 11, TOKEN_ARROW,       "->",    "arrow");
    expect_token(&list, 12, TOKEN_TYPE_I32,    "i32",   "return type i32");
    expect_token(&list, 13, TOKEN_LBRACE,      "{",     "lbrace");
    expect_token(&list, 14, TOKEN_RETURN,      "return","return keyword");
    expect_token(&list, 15, TOKEN_IDENTIFIER,  "a",     "return a");
    expect_token(&list, 16, TOKEN_PLUS,        "+",     "plus");
    expect_token(&list, 17, TOKEN_IDENTIFIER,  "b",     "return b");
    expect_token(&list, 18, TOKEN_SEMICOLON,   ";",     "semicolon");
    expect_token(&list, 19, TOKEN_RBRACE,      "}",     "rbrace");
    expect_token(&list, 20, TOKEN_EOF,         "",      "EOF");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 3: error level with string                                   */
/* ------------------------------------------------------------------ */

static void test_error_level(void)
{
    printf("\n--- Test: if (b == 0) { error.raise(Level.LOGIC, \"Division by zero\"); } ---\n");
    const char *src =
        "if (b == 0) { error.raise(Level.LOGIC, \"Division by zero\"); }";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list,  0, TOKEN_IF,              "if",               "if keyword");
    expect_token(&list,  1, TOKEN_LPAREN,          "(",                "lparen");
    expect_token(&list,  2, TOKEN_IDENTIFIER,      "b",                "identifier b");
    expect_token(&list,  3, TOKEN_EQ,              "==",               "equals");
    expect_token(&list,  4, TOKEN_INTEGER,         "0",                "integer 0");
    expect_token(&list,  5, TOKEN_RPAREN,          ")",                "rparen");
    expect_token(&list,  6, TOKEN_LBRACE,          "{",                "lbrace");
    expect_token(&list,  7, TOKEN_ERROR,           "error",            "error keyword");
    expect_token(&list,  8, TOKEN_DOT,             ".",                "dot");
    expect_token(&list,  9, TOKEN_IDENTIFIER,      "raise",            "identifier raise");
    expect_token(&list, 10, TOKEN_LPAREN,          "(",                "lparen 2");
    expect_token(&list, 11, TOKEN_LEVEL_LOGIC,     "Level.LOGIC",      "Level.LOGIC");
    expect_token(&list, 12, TOKEN_COMMA,           ",",                "comma");
    expect_token(&list, 13, TOKEN_STRING,          "Division by zero", "string literal");
    expect_token(&list, 14, TOKEN_RPAREN,          ")",                "rparen 2");
    expect_token(&list, 15, TOKEN_SEMICOLON,       ";",                "semicolon");
    expect_token(&list, 16, TOKEN_RBRACE,          "}",                "rbrace");
    expect_token(&list, 17, TOKEN_EOF,             "",                 "EOF");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 4: comments                                                  */
/* ------------------------------------------------------------------ */

static void test_comments(void)
{
    printf("\n--- Test: comments ---\n");
    const char *src =
        "// this is a comment\n"
        "let a: i32 = 1; /* inline comment */ let b: i32 = 2;\n";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    /* First real token after the line comment */
    expect_token(&list, 0, TOKEN_LET, "let", "let after line comment");
    /* After inline comment: let a : i32 = 1 ; = 7 tokens (indices 0-6), so next let is at 7 */
    expect_token(&list, 7, TOKEN_LET, "let", "let after block comment");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 5: numeric literals                                          */
/* ------------------------------------------------------------------ */

static void test_numeric_literals(void)
{
    printf("\n--- Test: numeric literals ---\n");
    const char *src = "42 3.14 0xFF 0b1010 1_000_000 2.5e10";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list, 0, TOKEN_INTEGER, "42",      "decimal int");
    expect_token(&list, 1, TOKEN_FLOAT,   "3.14",    "float");
    expect_token(&list, 2, TOKEN_INTEGER, "0xFF",    "hex");
    expect_token(&list, 3, TOKEN_INTEGER, "0b1010",  "binary");
    expect_token(&list, 4, TOKEN_INTEGER, "1000000", "underscore int");
    expect_token(&list, 5, TOKEN_FLOAT,   "2.5e10",  "float with exp");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 6: string escape sequences                                   */
/* ------------------------------------------------------------------ */

static void test_string_escapes(void)
{
    printf("\n--- Test: string escape sequences ---\n");
    const char *src = "\"hello\\nworld\" \"tab\\there\" \"quote\\\"inside\"";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list, 0, TOKEN_STRING, "hello\nworld",    "newline escape");
    expect_token(&list, 1, TOKEN_STRING, "tab\there",       "tab escape");
    expect_token(&list, 2, TOKEN_STRING, "quote\"inside",   "quote escape");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 7: operators                                                 */
/* ------------------------------------------------------------------ */

static void test_operators(void)
{
    printf("\n--- Test: operators ---\n");
    const char *src = "+ - * / % == != <= >= && || << >> -> =>";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list,  0, TOKEN_PLUS,      "+",  "plus");
    expect_token(&list,  1, TOKEN_MINUS,     "-",  "minus");
    expect_token(&list,  2, TOKEN_STAR,      "*",  "star");
    expect_token(&list,  3, TOKEN_SLASH,     "/",  "slash");
    expect_token(&list,  4, TOKEN_PERCENT,   "%",  "percent");
    expect_token(&list,  5, TOKEN_EQ,        "==", "eq");
    expect_token(&list,  6, TOKEN_NEQ,       "!=", "neq");
    expect_token(&list,  7, TOKEN_LTE,       "<=", "lte");
    expect_token(&list,  8, TOKEN_GTE,       ">=", "gte");
    expect_token(&list,  9, TOKEN_AND,       "&&", "and");
    expect_token(&list, 10, TOKEN_OR,        "||", "or");
    expect_token(&list, 11, TOKEN_SHL,       "<<", "shl");
    expect_token(&list, 12, TOKEN_SHR,       ">>", "shr");
    expect_token(&list, 13, TOKEN_ARROW,     "->", "arrow");
    expect_token(&list, 14, TOKEN_FAT_ARROW, "=>", "fat arrow");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 8: all keywords                                              */
/* ------------------------------------------------------------------ */

static void test_keywords(void)
{
    printf("\n--- Test: keywords ---\n");
    const char *src =
        "fn let const if else for while return match try catch "
        "error log import export true false Some None Option";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list,  0, TOKEN_FN,      "fn",     "fn");
    expect_token(&list,  1, TOKEN_LET,     "let",    "let");
    expect_token(&list,  2, TOKEN_CONST,   "const",  "const");
    expect_token(&list,  3, TOKEN_IF,      "if",     "if");
    expect_token(&list,  4, TOKEN_ELSE,    "else",   "else");
    expect_token(&list,  5, TOKEN_FOR,     "for",    "for");
    expect_token(&list,  6, TOKEN_WHILE,   "while",  "while");
    expect_token(&list,  7, TOKEN_RETURN,  "return", "return");
    expect_token(&list,  8, TOKEN_MATCH,   "match",  "match");
    expect_token(&list,  9, TOKEN_TRY,     "try",    "try");
    expect_token(&list, 10, TOKEN_CATCH,   "catch",  "catch");
    expect_token(&list, 11, TOKEN_ERROR,   "error",  "error");
    expect_token(&list, 12, TOKEN_LOG,     "log",    "log");
    expect_token(&list, 13, TOKEN_IMPORT,  "import", "import");
    expect_token(&list, 14, TOKEN_EXPORT,  "export", "export");
    expect_token(&list, 15, TOKEN_TRUE,    "true",   "true");
    expect_token(&list, 16, TOKEN_FALSE,   "false",  "false");
    expect_token(&list, 17, TOKEN_SOME,    "Some",   "Some");
    expect_token(&list, 18, TOKEN_NONE,    "None",   "None");
    expect_token(&list, 19, TOKEN_OPTION,  "Option", "Option");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 9: type keywords                                             */
/* ------------------------------------------------------------------ */

static void test_types(void)
{
    printf("\n--- Test: type keywords ---\n");
    const char *src = "i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 str bool arr map";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list,  0, TOKEN_TYPE_I8,   "i8",   "i8");
    expect_token(&list,  1, TOKEN_TYPE_I16,  "i16",  "i16");
    expect_token(&list,  2, TOKEN_TYPE_I32,  "i32",  "i32");
    expect_token(&list,  3, TOKEN_TYPE_I64,  "i64",  "i64");
    expect_token(&list,  4, TOKEN_TYPE_U8,   "u8",   "u8");
    expect_token(&list,  5, TOKEN_TYPE_U16,  "u16",  "u16");
    expect_token(&list,  6, TOKEN_TYPE_U32,  "u32",  "u32");
    expect_token(&list,  7, TOKEN_TYPE_U64,  "u64",  "u64");
    expect_token(&list,  8, TOKEN_TYPE_F32,  "f32",  "f32");
    expect_token(&list,  9, TOKEN_TYPE_F64,  "f64",  "f64");
    expect_token(&list, 10, TOKEN_TYPE_STR,  "str",  "str");
    expect_token(&list, 11, TOKEN_TYPE_BOOL, "bool", "bool");
    expect_token(&list, 12, TOKEN_TYPE_ARR,  "arr",  "arr");
    expect_token(&list, 13, TOKEN_TYPE_MAP,  "map",  "map");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 10: error levels                                             */
/* ------------------------------------------------------------------ */

static void test_error_levels(void)
{
    printf("\n--- Test: error levels ---\n");
    const char *src =
        "Level.SILENT Level.LOGIC Level.WARNING Level.SYSTEM Level.CATASTROPHIC";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");
    TokenList list = lexer_tokenize_all(&lex);

    expect_token(&list, 0, TOKEN_LEVEL_SILENT,       "Level.SILENT",       "Level.SILENT");
    expect_token(&list, 1, TOKEN_LEVEL_LOGIC,        "Level.LOGIC",        "Level.LOGIC");
    expect_token(&list, 2, TOKEN_LEVEL_WARNING,      "Level.WARNING",      "Level.WARNING");
    expect_token(&list, 3, TOKEN_LEVEL_SYSTEM,       "Level.SYSTEM",       "Level.SYSTEM");
    expect_token(&list, 4, TOKEN_LEVEL_CATASTROPHIC, "Level.CATASTROPHIC", "Level.CATASTROPHIC");

    token_list_free(&list);
}

/* ------------------------------------------------------------------ */
/*  Test 11: error cases                                              */
/* ------------------------------------------------------------------ */

static void test_error_cases(void)
{
    printf("\n--- Test: error cases ---\n");

    expect_illegal("\"unterminated string",          "unterminated string (EOF)");
    expect_illegal("\"unterminated\n",               "unterminated string (newline)");
    expect_illegal("let x = @;",                     "invalid character @");
    expect_illegal("let y = #;",                     "invalid character #");
}

/* ------------------------------------------------------------------ */
/*  Test 12: peek does not consume                                    */
/* ------------------------------------------------------------------ */

static void test_peek(void)
{
    printf("\n--- Test: peek ---\n");
    tests_run++;
    Lexer lex;
    lexer_init(&lex, "let x", "test.vt");
    Token p = lexer_peek(&lex);
    Token n = lexer_next_token(&lex);
    if (p.type == n.type && strcmp(p.value, n.value) == 0) {
        tests_passed++;
        printf("  PASS  peek returns same as next\n");
    } else {
        printf("  FAIL  peek returns same as next\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Test 13: line and column tracking                                 */
/* ------------------------------------------------------------------ */

static void test_line_column(void)
{
    printf("\n--- Test: line/column tracking ---\n");
    const char *src = "let x\nlet y";
    Lexer lex;
    lexer_init(&lex, src, "test.vt");

    Token t1 = lexer_next_token(&lex); /* let */
    Token t2 = lexer_next_token(&lex); /* x   */
    Token t3 = lexer_next_token(&lex); /* let (line 2) */
    Token t4 = lexer_next_token(&lex); /* y   */

    tests_run++;
    if (t1.line == 1 && t1.column == 1) { tests_passed++; printf("  PASS  let at 1:1\n"); }
    else printf("  FAIL  let at 1:1 (got %d:%d)\n", t1.line, t1.column);

    tests_run++;
    if (t2.line == 1 && t2.column == 5) { tests_passed++; printf("  PASS  x at 1:5\n"); }
    else printf("  FAIL  x at 1:5 (got %d:%d)\n", t2.line, t2.column);

    tests_run++;
    if (t3.line == 2 && t3.column == 1) { tests_passed++; printf("  PASS  let at 2:1\n"); }
    else printf("  FAIL  let at 2:1 (got %d:%d)\n", t3.line, t3.column);

    tests_run++;
    if (t4.line == 2 && t4.column == 5) { tests_passed++; printf("  PASS  y at 2:5\n"); }
    else printf("  FAIL  y at 2:5 (got %d:%d)\n", t4.line, t4.column);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== aricode lexer test suite ===\n");

    test_let_statement();
    test_fn_declaration();
    test_error_level();
    test_comments();
    test_numeric_literals();
    test_string_escapes();
    test_operators();
    test_keywords();
    test_types();
    test_error_levels();
    test_error_cases();
    test_peek();
    test_line_column();

    printf("\n=== Results: %d / %d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
