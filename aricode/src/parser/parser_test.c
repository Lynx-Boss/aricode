/*
 * aricode - Ari Code Language
 * Parser test suite.
 *
 * Each test builds a token array by hand (simulating lexer output),
 * feeds it to the parser, and verifies the resulting AST structure.
 */

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                               */
/* ------------------------------------------------------------------ */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                          \
    do {                                                    \
        tests_run++;                                        \
        printf("  TEST %-50s ", #name);                     \
        fflush(stdout);                                     \
    } while (0)

#define PASS()                                              \
    do { tests_passed++; printf("[PASS]\n"); } while (0)

#define FAIL(msg)                                           \
    do {                                                    \
        tests_failed++;                                     \
        printf("[FAIL] %s\n", (msg));                       \
    } while (0)

#define ASSERT(cond, msg)                                   \
    do {                                                    \
        if (!(cond)) { FAIL(msg); return; }                 \
    } while (0)

#define ASSERT_NODE(node, expected_type, msg)                \
    ASSERT((node) != NULL && (node)->type == (expected_type), msg)

#define ASSERT_STR(actual, expected, msg)                    \
    ASSERT((actual) != NULL && strcmp((actual), (expected)) == 0, msg)

/* ------------------------------------------------------------------ */
/*  Token builder helpers                                             */
/* ------------------------------------------------------------------ */

#define MAX_TEST_TOKENS 256

typedef struct {
    ParserToken tokens[MAX_TEST_TOKENS];
    size_t count;
} TokenList;

static void tl_init(TokenList *tl) {
    memset(tl, 0, sizeof(*tl));
}

static void tl_add(TokenList *tl, TokenType type, const char *lexeme,
                   int line, int col) {
    assert(tl->count < MAX_TEST_TOKENS);
    ParserToken *t = &tl->tokens[tl->count++];
    t->type   = type;
    t->lexeme = lexeme ? strdup(lexeme) : NULL;
    t->line   = line;
    t->col    = col;
}

/* Add EOF sentinel */
static void tl_end(TokenList *tl) {
    tl_add(tl, TOKEN_EOF, "", 999, 1);
}

static void tl_free(TokenList *tl) {
    for (size_t i = 0; i < tl->count; i++)
        free(tl->tokens[i].lexeme);
}

/* ------------------------------------------------------------------ */
/*  Test 1: let x: i32 = 10;                                         */
/* ------------------------------------------------------------------ */

static void test_var_decl(void) {
    TEST(var_decl__let_x_i32_eq_10);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_LET,        "let",  1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",    1, 5);
    tl_add(&tl, TOKEN_COLON,       ":",    1, 6);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32",  1, 8);
    tl_add(&tl, TOKEN_ASSIGN,      "=",    1, 12);
    tl_add(&tl, TOKEN_INTEGER,     "10",   1, 14);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",    1, 16);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");
    ASSERT_NODE(root, NODE_PROGRAM, "root should be PROGRAM");
    ASSERT(root->child_count == 1, "program should have 1 child");

    ASTNode *decl = root->children[0];
    ASSERT_NODE(decl, NODE_VAR_DECL, "child should be VAR_DECL");
    ASSERT_STR(decl->string_val, "x", "var name should be 'x'");
    ASSERT(decl->line == 1 && decl->col == 1, "location should be 1:1");

    /* child 0: type annotation */
    ASSERT(decl->child_count == 2, "VAR_DECL should have 2 children");
    ASSERT_NODE(decl->children[0], NODE_TYPE_ANNOTATION, "child 0 = type");
    ASSERT_STR(decl->children[0]->string_val, "i32", "type should be i32");

    /* child 1: initializer */
    ASSERT_NODE(decl->children[1], NODE_INT_LITERAL, "child 1 = int literal");
    ASSERT(decl->children[1]->int_val == 10, "value should be 10");

    printf("[PASS]\n");
    tests_passed++;

    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 2: const PI: f64 = 3.14;                                    */
/* ------------------------------------------------------------------ */

static void test_const_decl(void) {
    TEST(const_decl__PI_f64);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_CONST,       "const", 1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "PI",    1, 7);
    tl_add(&tl, TOKEN_COLON,       ":",     1, 9);
    tl_add(&tl, TOKEN_TYPE_F64,    "f64",   1, 11);
    tl_add(&tl, TOKEN_ASSIGN,      "=",     1, 15);
    tl_add(&tl, TOKEN_FLOAT,       "3.14",  1, 17);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",     1, 21);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");
    ASSERT(root->child_count == 1, "one declaration");

    ASTNode *decl = root->children[0];
    ASSERT_NODE(decl, NODE_CONST_DECL, "should be CONST_DECL");
    ASSERT_STR(decl->string_val, "PI", "name should be PI");
    ASSERT_STR(decl->children[0]->string_val, "f64", "type should be f64");
    ASSERT(decl->children[1]->float_val > 3.13 &&
           decl->children[1]->float_val < 3.15, "value ~ 3.14");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 3: fn add(a: i32, b: i32) -> i32 { return a + b; }          */
/* ------------------------------------------------------------------ */

static void test_fn_decl(void) {
    TEST(fn_decl__add_a_b_i32);

    TokenList tl;
    tl_init(&tl);
    /*  fn add(a: i32, b: i32) -> i32 { return a + b; }  */
    tl_add(&tl, TOKEN_FN,          "fn",     1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "add",    1, 4);
    tl_add(&tl, TOKEN_LPAREN,      "(",      1, 7);
    tl_add(&tl, TOKEN_IDENTIFIER,  "a",      1, 8);
    tl_add(&tl, TOKEN_COLON,       ":",      1, 9);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32",    1, 11);
    tl_add(&tl, TOKEN_COMMA,       ",",      1, 14);
    tl_add(&tl, TOKEN_IDENTIFIER,  "b",      1, 16);
    tl_add(&tl, TOKEN_COLON,       ":",      1, 17);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32",    1, 19);
    tl_add(&tl, TOKEN_RPAREN,      ")",      1, 22);
    tl_add(&tl, TOKEN_ARROW,       "->",     1, 24);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32",    1, 27);
    tl_add(&tl, TOKEN_LBRACE,      "{",      1, 31);
    tl_add(&tl, TOKEN_RETURN,      "return", 1, 33);
    tl_add(&tl, TOKEN_IDENTIFIER,  "a",      1, 40);
    tl_add(&tl, TOKEN_PLUS,        "+",      1, 42);
    tl_add(&tl, TOKEN_IDENTIFIER,  "b",      1, 44);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",      1, 45);
    tl_add(&tl, TOKEN_RBRACE,      "}",      1, 47);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *fn = root->children[0];
    ASSERT_NODE(fn, NODE_FN_DECL, "should be FN_DECL");
    ASSERT_STR(fn->string_val, "add", "fn name = 'add'");

    /* child 0: params block */
    ASTNode *params = fn->children[0];
    ASSERT_NODE(params, NODE_BLOCK, "child 0 = params block");
    ASSERT(params->child_count == 2, "two parameters");
    ASSERT_STR(params->children[0]->string_val, "a", "param 0 = a");
    ASSERT_STR(params->children[1]->string_val, "b", "param 1 = b");

    /* child 1: return type */
    ASSERT_NODE(fn->children[1], NODE_TYPE_ANNOTATION, "child 1 = ret type");
    ASSERT_STR(fn->children[1]->string_val, "i32", "ret type = i32");

    /* child 2: body */
    ASTNode *body = fn->children[2];
    ASSERT_NODE(body, NODE_BLOCK, "child 2 = body block");
    ASSERT(body->child_count == 1, "body has 1 statement");

    ASTNode *ret = body->children[0];
    ASSERT_NODE(ret, NODE_RETURN, "statement is RETURN");
    ASSERT(ret->child_count == 1, "return has expression");
    ASSERT_NODE(ret->children[0], NODE_BINARY_OP, "return expr = BINARY_OP");
    ASSERT_STR(ret->children[0]->op, "+", "operator = '+'");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 4: if (x > 0) { return x; } else { return -x; }             */
/* ------------------------------------------------------------------ */

static void test_if_else(void) {
    TEST(if_else__return_abs);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_IF,          "if",     1, 1);
    tl_add(&tl, TOKEN_LPAREN,      "(",      1, 4);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",      1, 5);
    tl_add(&tl, TOKEN_GT,          ">",      1, 7);
    tl_add(&tl, TOKEN_INTEGER,     "0",      1, 9);
    tl_add(&tl, TOKEN_RPAREN,      ")",      1, 10);
    tl_add(&tl, TOKEN_LBRACE,      "{",      1, 12);
    tl_add(&tl, TOKEN_RETURN,      "return", 1, 14);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",      1, 21);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",      1, 22);
    tl_add(&tl, TOKEN_RBRACE,      "}",      1, 24);
    tl_add(&tl, TOKEN_ELSE,        "else",   1, 26);
    tl_add(&tl, TOKEN_LBRACE,      "{",      1, 31);
    tl_add(&tl, TOKEN_RETURN,      "return", 1, 33);
    tl_add(&tl, TOKEN_MINUS,       "-",      1, 40);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",      1, 41);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",      1, 42);
    tl_add(&tl, TOKEN_RBRACE,      "}",      1, 44);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *ifn = root->children[0];
    ASSERT_NODE(ifn, NODE_IF, "should be IF");
    ASSERT(ifn->child_count == 3, "if has condition + then + else");

    /* condition: x > 0 */
    ASSERT_NODE(ifn->children[0], NODE_BINARY_OP, "condition = BINARY_OP");
    ASSERT_STR(ifn->children[0]->op, ">", "op = '>'");

    /* then: { return x; } */
    ASTNode *then_block = ifn->children[1];
    ASSERT_NODE(then_block, NODE_BLOCK, "then = BLOCK");
    ASSERT_NODE(then_block->children[0], NODE_RETURN, "then has return");

    /* else: { return -x; } */
    ASTNode *else_block = ifn->children[2];
    ASSERT_NODE(else_block, NODE_BLOCK, "else = BLOCK");
    ASTNode *else_ret = else_block->children[0];
    ASSERT_NODE(else_ret, NODE_RETURN, "else has return");
    ASSERT_NODE(else_ret->children[0], NODE_UNARY_OP, "return -x = UNARY_OP");
    ASSERT_STR(else_ret->children[0]->op, "-", "unary op = '-'");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 5: for (let i: i32 = 0; i < 10; i + 1) { x; }              */
/* ------------------------------------------------------------------ */

static void test_for_loop(void) {
    TEST(for_loop__basic);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_FOR,         "for",  1, 1);
    tl_add(&tl, TOKEN_LPAREN,      "(",    1, 5);
    tl_add(&tl, TOKEN_LET,         "let",  1, 6);
    tl_add(&tl, TOKEN_IDENTIFIER,  "i",    1, 10);
    tl_add(&tl, TOKEN_COLON,       ":",    1, 11);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32",  1, 13);
    tl_add(&tl, TOKEN_ASSIGN,      "=",    1, 17);
    tl_add(&tl, TOKEN_INTEGER,     "0",    1, 19);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",    1, 20);
    tl_add(&tl, TOKEN_IDENTIFIER,  "i",    1, 22);
    tl_add(&tl, TOKEN_LT,          "<",    1, 24);
    tl_add(&tl, TOKEN_INTEGER,     "10",   1, 26);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",    1, 28);
    tl_add(&tl, TOKEN_IDENTIFIER,  "i",    1, 30);
    tl_add(&tl, TOKEN_PLUS,        "+",    1, 32);
    tl_add(&tl, TOKEN_INTEGER,     "1",    1, 34);
    tl_add(&tl, TOKEN_RPAREN,      ")",    1, 35);
    tl_add(&tl, TOKEN_LBRACE,      "{",    1, 37);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",    2, 3);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",    2, 4);
    tl_add(&tl, TOKEN_RBRACE,      "}",    3, 1);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *forn = root->children[0];
    ASSERT_NODE(forn, NODE_FOR, "should be FOR");
    /* children: init, condition, update, body */
    ASSERT(forn->child_count == 4, "for has 4 children");
    ASSERT_NODE(forn->children[0], NODE_VAR_DECL, "init = VAR_DECL");
    ASSERT_NODE(forn->children[1], NODE_BINARY_OP, "cond = BINARY_OP");
    ASSERT_NODE(forn->children[2], NODE_BINARY_OP, "update = BINARY_OP");
    ASSERT_NODE(forn->children[3], NODE_BLOCK, "body = BLOCK");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 6: while (x > 0) { x; }                                     */
/* ------------------------------------------------------------------ */

static void test_while_loop(void) {
    TEST(while_loop__basic);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_WHILE,       "while", 1, 1);
    tl_add(&tl, TOKEN_LPAREN,      "(",     1, 7);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",     1, 8);
    tl_add(&tl, TOKEN_GT,          ">",     1, 10);
    tl_add(&tl, TOKEN_INTEGER,     "0",     1, 12);
    tl_add(&tl, TOKEN_RPAREN,      ")",     1, 13);
    tl_add(&tl, TOKEN_LBRACE,      "{",     1, 15);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",     2, 3);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",     2, 4);
    tl_add(&tl, TOKEN_RBRACE,      "}",     3, 1);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *wn = root->children[0];
    ASSERT_NODE(wn, NODE_WHILE, "should be WHILE");
    ASSERT(wn->child_count == 2, "while has condition + body");
    ASSERT_NODE(wn->children[0], NODE_BINARY_OP, "cond = BINARY_OP");
    ASSERT_NODE(wn->children[1], NODE_BLOCK, "body = BLOCK");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 7: match (x) { 1 => y, 2 => { z; } }                       */
/* ------------------------------------------------------------------ */

static void test_match(void) {
    TEST(match__two_arms);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_MATCH,       "match", 1, 1);
    tl_add(&tl, TOKEN_LPAREN,      "(",     1, 7);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",     1, 8);
    tl_add(&tl, TOKEN_RPAREN,      ")",     1, 9);
    tl_add(&tl, TOKEN_LBRACE,      "{",     1, 11);
    /* arm 1: 1 => y, */
    tl_add(&tl, TOKEN_INTEGER,     "1",     2, 3);
    tl_add(&tl, TOKEN_FAT_ARROW,   "=>",    2, 5);
    tl_add(&tl, TOKEN_IDENTIFIER,  "y",     2, 8);
    tl_add(&tl, TOKEN_COMMA,       ",",     2, 9);
    /* arm 2: 2 => { z; } */
    tl_add(&tl, TOKEN_INTEGER,     "2",     3, 3);
    tl_add(&tl, TOKEN_FAT_ARROW,   "=>",    3, 5);
    tl_add(&tl, TOKEN_LBRACE,      "{",     3, 8);
    tl_add(&tl, TOKEN_IDENTIFIER,  "z",     3, 10);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",     3, 11);
    tl_add(&tl, TOKEN_RBRACE,      "}",     3, 13);
    tl_add(&tl, TOKEN_RBRACE,      "}",     4, 1);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *mn = root->children[0];
    ASSERT_NODE(mn, NODE_MATCH, "should be MATCH");
    /* children: expr + 2 arms */
    ASSERT(mn->child_count == 3, "match has expr + 2 arms");
    ASSERT_NODE(mn->children[1], NODE_MATCH_ARM, "arm 1");
    ASSERT_NODE(mn->children[2], NODE_MATCH_ARM, "arm 2");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 8: try { } catch (e: str) { }                               */
/* ------------------------------------------------------------------ */

static void test_try_catch(void) {
    TEST(try_catch__basic);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_TRY,         "try",   1, 1);
    tl_add(&tl, TOKEN_LBRACE,      "{",     1, 5);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",     1, 7);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",     1, 8);
    tl_add(&tl, TOKEN_RBRACE,      "}",     1, 10);
    tl_add(&tl, TOKEN_CATCH,       "catch", 1, 12);
    tl_add(&tl, TOKEN_LPAREN,      "(",     1, 18);
    tl_add(&tl, TOKEN_IDENTIFIER,  "e",     1, 19);
    tl_add(&tl, TOKEN_COLON,       ":",     1, 20);
    tl_add(&tl, TOKEN_TYPE_STR,    "str",   1, 22);
    tl_add(&tl, TOKEN_RPAREN,      ")",     1, 25);
    tl_add(&tl, TOKEN_LBRACE,      "{",     1, 27);
    tl_add(&tl, TOKEN_RBRACE,      "}",     1, 29);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *tc = root->children[0];
    ASSERT_NODE(tc, NODE_TRY_CATCH, "should be TRY_CATCH");
    ASSERT_STR(tc->string_val, "e", "catch var = 'e'");
    /* children: try block, catch type, catch block */
    ASSERT(tc->child_count == 3, "try_catch has 3 children");
    ASSERT_NODE(tc->children[0], NODE_BLOCK, "try block");
    ASSERT_NODE(tc->children[1], NODE_TYPE_ANNOTATION, "catch type");
    ASSERT_STR(tc->children[1]->string_val, "str", "catch type = str");
    ASSERT_NODE(tc->children[2], NODE_BLOCK, "catch block");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 9: Some(42) and None                                        */
/* ------------------------------------------------------------------ */

static void test_some_none(void) {
    TEST(some_none__option_types);

    /* let x: Option<i32> = Some(42); */
    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_LET,         "let",    1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",      1, 5);
    tl_add(&tl, TOKEN_COLON,       ":",      1, 6);
    tl_add(&tl, TOKEN_OPTION,      "Option", 1, 8);
    tl_add(&tl, TOKEN_LT,          "<",      1, 14);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32",    1, 15);
    tl_add(&tl, TOKEN_GT,          ">",      1, 18);
    tl_add(&tl, TOKEN_ASSIGN,      "=",      1, 20);
    tl_add(&tl, TOKEN_SOME,        "Some",   1, 22);
    tl_add(&tl, TOKEN_LPAREN,      "(",      1, 26);
    tl_add(&tl, TOKEN_INTEGER,     "42",     1, 27);
    tl_add(&tl, TOKEN_RPAREN,      ")",      1, 29);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",      1, 30);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *decl = root->children[0];
    ASSERT_NODE(decl, NODE_VAR_DECL, "should be VAR_DECL");

    /* Type: Option<i32> */
    ASTNode *ty = decl->children[0];
    ASSERT_NODE(ty, NODE_TYPE_ANNOTATION, "type = TYPE_ANNOTATION");
    ASSERT_STR(ty->string_val, "Option", "type name = Option");
    ASSERT(ty->child_count == 1, "Option has 1 type param");
    ASSERT_STR(ty->children[0]->string_val, "i32", "inner type = i32");

    /* Value: Some(42) */
    ASTNode *val = decl->children[1];
    ASSERT_NODE(val, NODE_SOME, "value = SOME");
    ASSERT(val->child_count == 1, "Some has 1 child");
    ASSERT_NODE(val->children[0], NODE_INT_LITERAL, "Some child = INT_LITERAL");
    ASSERT(val->children[0]->int_val == 42, "value = 42");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 10: expression precedence  2 + 3 * 4                        */
/* ------------------------------------------------------------------ */

static void test_precedence(void) {
    TEST(precedence__mul_before_add);

    /* 2 + 3 * 4; */
    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_INTEGER,   "2",  1, 1);
    tl_add(&tl, TOKEN_PLUS,      "+",  1, 3);
    tl_add(&tl, TOKEN_INTEGER,   "3",  1, 5);
    tl_add(&tl, TOKEN_STAR,      "*",  1, 7);
    tl_add(&tl, TOKEN_INTEGER,   "4",  1, 9);
    tl_add(&tl, TOKEN_SEMICOLON, ";",  1, 10);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *stmt = root->children[0];
    ASSERT_NODE(stmt, NODE_EXPR_STMT, "expr_stmt");
    ASTNode *expr = stmt->children[0];

    /* Should be: +(2, *(3, 4)) */
    ASSERT_NODE(expr, NODE_BINARY_OP, "top = BINARY_OP");
    ASSERT_STR(expr->op, "+", "top op = '+'");

    ASTNode *left = expr->children[0];
    ASSERT_NODE(left, NODE_INT_LITERAL, "left = 2");
    ASSERT(left->int_val == 2, "left value = 2");

    ASTNode *right = expr->children[1];
    ASSERT_NODE(right, NODE_BINARY_OP, "right = BINARY_OP");
    ASSERT_STR(right->op, "*", "right op = '*'");
    ASSERT(right->children[0]->int_val == 3, "3");
    ASSERT(right->children[1]->int_val == 4, "4");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 11: function call  foo(1, 2)                                 */
/* ------------------------------------------------------------------ */

static void test_call(void) {
    TEST(call__foo_1_2);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_IDENTIFIER, "foo", 1, 1);
    tl_add(&tl, TOKEN_LPAREN,    "(",    1, 4);
    tl_add(&tl, TOKEN_INTEGER,   "1",    1, 5);
    tl_add(&tl, TOKEN_COMMA,     ",",    1, 6);
    tl_add(&tl, TOKEN_INTEGER,   "2",    1, 8);
    tl_add(&tl, TOKEN_RPAREN,    ")",    1, 9);
    tl_add(&tl, TOKEN_SEMICOLON, ";",    1, 10);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *stmt = root->children[0];
    ASTNode *call = stmt->children[0];
    ASSERT_NODE(call, NODE_CALL, "should be CALL");
    /* child 0 = callee, child 1..N = args */
    ASSERT(call->child_count == 3, "callee + 2 args");
    ASSERT_NODE(call->children[0], NODE_IDENTIFIER, "callee = IDENTIFIER");
    ASSERT_STR(call->children[0]->string_val, "foo", "callee = foo");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 12: member access  log.error                                 */
/* ------------------------------------------------------------------ */

static void test_member_access(void) {
    TEST(member_access__log_error);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_IDENTIFIER, "log",   1, 1);
    tl_add(&tl, TOKEN_DOT,        ".",     1, 4);
    tl_add(&tl, TOKEN_IDENTIFIER, "error", 1, 5);
    tl_add(&tl, TOKEN_SEMICOLON,  ";",     1, 10);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *expr = root->children[0]->children[0];
    ASSERT_NODE(expr, NODE_MEMBER_ACCESS, "should be MEMBER_ACCESS");
    ASSERT_STR(expr->string_val, "error", "member = 'error'");
    ASSERT_NODE(expr->children[0], NODE_IDENTIFIER, "object = IDENTIFIER");
    ASSERT_STR(expr->children[0]->string_val, "log", "object = 'log'");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 13: array literal [1, 2, 3]                                  */
/* ------------------------------------------------------------------ */

static void test_array_literal(void) {
    TEST(array_literal__1_2_3);

    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_LBRACKET,  "[", 1, 1);
    tl_add(&tl, TOKEN_INTEGER,   "1", 1, 2);
    tl_add(&tl, TOKEN_COMMA,     ",", 1, 3);
    tl_add(&tl, TOKEN_INTEGER,   "2", 1, 5);
    tl_add(&tl, TOKEN_COMMA,     ",", 1, 6);
    tl_add(&tl, TOKEN_INTEGER,   "3", 1, 8);
    tl_add(&tl, TOKEN_RBRACKET,  "]", 1, 9);
    tl_add(&tl, TOKEN_SEMICOLON, ";", 1, 10);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(!parser_has_errors(&p), "should parse without errors");

    ASTNode *arr = root->children[0]->children[0];
    ASSERT_NODE(arr, NODE_ARRAY_LITERAL, "should be ARRAY_LITERAL");
    ASSERT(arr->child_count == 3, "3 elements");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 14: error cases                                              */
/* ------------------------------------------------------------------ */

static void test_error_missing_semicolon(void) {
    TEST(error__missing_semicolon);

    /* let x: i32 = 10   <-- missing semicolon */
    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_LET,        "let", 1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",   1, 5);
    tl_add(&tl, TOKEN_COLON,       ":",   1, 6);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32", 1, 8);
    tl_add(&tl, TOKEN_ASSIGN,      "=",   1, 12);
    tl_add(&tl, TOKEN_INTEGER,     "10",  1, 14);
    /* no semicolon */
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(parser_has_errors(&p), "should report error");
    ASSERT(p.error_count >= 1, "at least 1 error");

    /* Check that error mentions semicolon */
    ASSERT(strstr(p.errors[0].message, ";") != NULL,
           "error should mention ';'");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

static void test_error_unclosed_brace(void) {
    TEST(error__unclosed_brace);

    /* fn foo() { let x: i32 = 1;   <-- missing } */
    TokenList tl;
    tl_init(&tl);
    tl_add(&tl, TOKEN_FN,          "fn",  1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "foo", 1, 4);
    tl_add(&tl, TOKEN_LPAREN,      "(",   1, 7);
    tl_add(&tl, TOKEN_RPAREN,      ")",   1, 8);
    tl_add(&tl, TOKEN_LBRACE,      "{",   1, 10);
    tl_add(&tl, TOKEN_LET,         "let", 2, 3);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",   2, 7);
    tl_add(&tl, TOKEN_COLON,       ":",   2, 8);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32", 2, 10);
    tl_add(&tl, TOKEN_ASSIGN,      "=",   2, 14);
    tl_add(&tl, TOKEN_INTEGER,     "1",   2, 16);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",   2, 17);
    /* no closing brace */
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(parser_has_errors(&p), "should report error");
    ASSERT(p.error_count >= 1, "at least 1 error");
    ASSERT(strstr(p.errors[0].message, "}") != NULL ||
           strstr(p.errors[0].message, "'}'") != NULL,
           "error should mention missing '}'");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

static void test_error_recovery(void) {
    TEST(error__recovery_continues_parsing);

    /*
     * let x: i32 = 10      <-- missing semicolon
     * let y: i32 = 20;     <-- valid
     *
     * The parser should recover and parse the second declaration.
     */
    TokenList tl;
    tl_init(&tl);
    /* first decl, missing ; */
    tl_add(&tl, TOKEN_LET,        "let", 1, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "x",   1, 5);
    tl_add(&tl, TOKEN_COLON,       ":",   1, 6);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32", 1, 8);
    tl_add(&tl, TOKEN_ASSIGN,      "=",   1, 12);
    tl_add(&tl, TOKEN_INTEGER,     "10",  1, 14);
    /* no semicolon -- next statement */
    tl_add(&tl, TOKEN_LET,        "let", 2, 1);
    tl_add(&tl, TOKEN_IDENTIFIER,  "y",   2, 5);
    tl_add(&tl, TOKEN_COLON,       ":",   2, 6);
    tl_add(&tl, TOKEN_TYPE_I32,    "i32", 2, 8);
    tl_add(&tl, TOKEN_ASSIGN,      "=",   2, 12);
    tl_add(&tl, TOKEN_INTEGER,     "20",  2, 14);
    tl_add(&tl, TOKEN_SEMICOLON,   ";",   2, 16);
    tl_end(&tl);

    Parser p;
    parser_init(&p, tl.tokens, tl.count);
    ASTNode *root = parser_parse(&p);

    ASSERT(parser_has_errors(&p), "should have errors");

    /* The parser should have recovered and produced at least
     * the second declaration. */
    int found_y = 0;
    for (size_t i = 0; i < root->child_count; i++) {
        ASTNode *c = root->children[i];
        if (c->type == NODE_VAR_DECL && c->string_val &&
            strcmp(c->string_val, "y") == 0) {
            found_y = 1;
        }
    }
    ASSERT(found_y, "should have recovered and parsed 'let y'");

    PASS();
    ast_free(root);
    tl_free(&tl);
}

/* ------------------------------------------------------------------ */
/*  Test 15: AST printing (smoke test -- just make sure it doesn't    */
/*  crash)                                                            */
/* ------------------------------------------------------------------ */

static void test_ast_print(void) {
    TEST(ast_print__smoke);

    ASTNode *prog = ast_create_node(NODE_PROGRAM, 1, 1);
    ASTNode *decl = ast_create_node(NODE_VAR_DECL, 1, 1);
    decl->string_val = strdup("x");
    ASTNode *ty = ast_create_node(NODE_TYPE_ANNOTATION, 1, 5);
    ty->string_val = strdup("i32");
    ASTNode *val = ast_create_node(NODE_INT_LITERAL, 1, 11);
    val->int_val = 42;

    ast_add_child(decl, ty);
    ast_add_child(decl, val);
    ast_add_child(prog, decl);

    printf("\n    --- AST dump ---\n    ");
    /* Redirect to suppress in normal output */
    ast_print(prog, 2);
    printf("    --- end dump ---\n");

    ast_free(prog);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("\n=== aricode parser test suite ===\n\n");

    test_var_decl();
    test_const_decl();
    test_fn_decl();
    test_if_else();
    test_for_loop();
    test_while_loop();
    test_match();
    test_try_catch();
    test_some_none();
    test_precedence();
    test_call();
    test_member_access();
    test_array_literal();
    test_error_missing_semicolon();
    test_error_unclosed_brace();
    test_error_recovery();
    test_ast_print();

    printf("\n=== Results: %d passed, %d failed, %d total ===\n\n",
           tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
