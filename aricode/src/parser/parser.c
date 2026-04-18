/*
 * aricode - Ari Code Language
 * Recursive-descent parser with precedence climbing for expressions.
 *
 * Grammar (simplified):
 *
 *   program      = declaration* EOF
 *   declaration  = fn_decl | var_decl | const_decl | statement
 *   fn_decl      = "fn" IDENT "(" params ")" ("->" type)? block
 *   var_decl     = "let" IDENT ":" type "=" expr ";"
 *   const_decl   = "const" IDENT ":" type "=" expr ";"
 *   statement    = if | for | while | match | try_catch
 *                | error_raise | return | block | expr_stmt
 *   block        = "{" declaration* "}"
 *   if           = "if" "(" expr ")" block ("else" (if | block))?
 *   for          = "for" "(" (var_decl | expr_stmt) expr ";" expr ")" block
 *   while        = "while" "(" expr ")" block
 *   match        = "match" "(" expr ")" "{" match_arm* "}"
 *   match_arm    = expr "=>" (block | expr ";")
 *   try_catch    = "try" block "catch" "(" IDENT ":" type ")" block
 *   error_raise  = "error" "." "raise" "(" expr "," expr ")" ";"
 *   return       = "return" expr? ";"
 *   expr_stmt    = expr ";"
 *
 *   expr         = assignment
 *   assignment   = or ("=" or)?
 *   or           = and ("||" and)*
 *   and          = equality ("&&" equality)*
 *   equality     = comparison (("==" | "!=") comparison)*
 *   comparison   = addition (("<" | ">" | "<=" | ">=") addition)*
 *   addition     = multiplication (("+" | "-") multiplication)*
 *   multiplication = unary (("*" | "/" | "%") unary)*
 *   unary        = ("-" | "!") unary | call
 *   call         = primary ("(" args ")" | "." IDENT)*
 *   primary      = INT | FLOAT | STRING | "true" | "false" | "None"
 *                | "Some" "(" expr ")"
 *                | IDENT | "[" expr_list "]" | "(" expr ")"
 *
 *   type         = base_type | "Option" "<" type ">"
 *                | "arr" "<" type ">" | "map" "<" type "," type ">"
 *   base_type    = i8|i16|i32|i64|u8|u16|u32|u64|f32|f64|str|bool
 */

#include "parser.h"
#include "struct_registry.h"
#include "enum_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

/* --- Token access ------------------------------------------------- */

static const ParserToken *peek(Parser *p) {
    if (p->pos < p->token_count)
        return &p->tokens[p->pos];
    /* Should not happen if token stream ends with EOF, but be safe. */
    return &p->tokens[p->token_count - 1];
}

static const ParserToken *previous(Parser *p) {
    if (p->pos > 0)
        return &p->tokens[p->pos - 1];
    return &p->tokens[0];
}

static const ParserToken *advance(Parser *p) {
    if (peek(p)->type != TOKEN_EOF)
        p->pos++;
    return previous(p);
}

static bool check(Parser *p, TokenType type) {
    return peek(p)->type == type;
}

static bool match(Parser *p, TokenType type) {
    if (check(p, type)) {
        advance(p);
        return true;
    }
    return false;
}

/* Match any one of several types.  Variadic, terminated by -1. */
static bool match_any(Parser *p, ...) {
    va_list ap;
    va_start(ap, p);
    int t;
    while ((t = va_arg(ap, int)) != -1) {
        if (check(p, (TokenType)t)) {
            advance(p);
            va_end(ap);
            return true;
        }
    }
    va_end(ap);
    return false;
}

/* --- Error handling ----------------------------------------------- */

__attribute__((unused))
static void parser_error(Parser *p, int line, int col,
                         const char *fmt, ...) {
    if (p->panic_mode) return;
    if (p->error_count >= PARSER_MAX_ERRORS) return;

    ParserError *e = &p->errors[p->error_count++];
    e->line = line;
    e->col  = col;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
    va_end(ap);

    p->panic_mode = true;
}

static void error_at_current(Parser *p, const char *fmt, ...) {
    const ParserToken *t = peek(p);
    if (p->panic_mode) return;
    if (p->error_count >= PARSER_MAX_ERRORS) return;

    ParserError *e = &p->errors[p->error_count++];
    e->line = t->line;
    e->col  = t->col;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
    va_end(ap);

    p->panic_mode = true;
}

/* Expect a specific token or report an error. */
static const ParserToken *expect(Parser *p, TokenType type, const char *what) {
    if (check(p, type))
        return advance(p);

    const ParserToken *t = peek(p);
    error_at_current(p,
        "expected %s, found '%s'",
        what,
        t->lexeme ? t->lexeme : token_type_names[t->type]);
    return NULL;
}

/* Synchronise after an error: skip tokens until we reach something
 * that looks like the start of a new statement/declaration. */
static void synchronize(Parser *p) {
    p->panic_mode = false;

    while (peek(p)->type != TOKEN_EOF) {
        /* If previous token was ';', the next statement starts here. */
        if (previous(p)->type == TOKEN_SEMICOLON) return;

        switch (peek(p)->type) {
        case TOKEN_FN:
        case TOKEN_LET:
        case TOKEN_CONST:
        case TOKEN_IF:
        case TOKEN_FOR:
        case TOKEN_WHILE:
        case TOKEN_RETURN:
        case TOKEN_MATCH:
        case TOKEN_TRY:
        case TOKEN_ERROR:
            return;
        default:
            break;
        }
        advance(p);
    }
}

/* --- String helpers ----------------------------------------------- */

static char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* ================================================================== */
/*  Forward declarations for recursive descent                        */
/* ================================================================== */

static ASTNode *parse_declaration(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p);
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_type(Parser *p);
static ASTNode *parse_shift(Parser *p);
static ASTNode *parse_equality(Parser *p);

/* ================================================================== */
/*  Type parsing                                                      */
/* ================================================================== */

static bool is_base_type(TokenType t) {
    return t >= TOKEN_TYPE_I8 && t <= TOKEN_TYPE_MAP;
}

static ASTNode *parse_type(Parser *p) {
    const ParserToken *t = peek(p);

    /* Option<T> */
    if (match(p, TOKEN_OPTION)) {
        ASTNode *node = ast_create_node(NODE_TYPE_ANNOTATION, t->line, t->col);
        node->string_val = str_dup("Option");
        expect(p, TOKEN_LT, "'<' after Option");
        ASTNode *inner = parse_type(p);
        ast_add_child(node, inner);
        expect(p, TOKEN_GT, "'>' to close Option type");
        return node;
    }

    /* arr<T> */
    if (match(p, TOKEN_TYPE_ARR)) {
        ASTNode *node = ast_create_node(NODE_TYPE_ANNOTATION, t->line, t->col);
        node->string_val = str_dup("arr");
        expect(p, TOKEN_LT, "'<' after arr");
        ASTNode *inner = parse_type(p);
        ast_add_child(node, inner);
        expect(p, TOKEN_GT, "'>' to close arr type");
        return node;
    }

    /* map<K,V> */
    if (match(p, TOKEN_TYPE_MAP)) {
        ASTNode *node = ast_create_node(NODE_TYPE_ANNOTATION, t->line, t->col);
        node->string_val = str_dup("map");
        expect(p, TOKEN_LT, "'<' after map");
        ASTNode *key = parse_type(p);
        ast_add_child(node, key);
        expect(p, TOKEN_COMMA, "',' between map key and value types");
        ASTNode *val = parse_type(p);
        ast_add_child(node, val);
        expect(p, TOKEN_GT, "'>' to close map type");
        return node;
    }

    /* Base types: i8..bool */
    if (is_base_type(peek(p)->type)) {
        const ParserToken *bt = advance(p);
        ASTNode *node = ast_create_node(NODE_TYPE_ANNOTATION, bt->line, bt->col);
        node->string_val = str_dup(bt->lexeme ? bt->lexeme
                                              : token_type_names[bt->type]);
        return node;
    }

    /* Identifier used as a type name (user-defined types in the future) */
    if (check(p, TOKEN_IDENTIFIER)) {
        const ParserToken *id = advance(p);
        ASTNode *node = ast_create_node(NODE_TYPE_ANNOTATION, id->line, id->col);
        node->string_val = str_dup(id->lexeme);
        return node;
    }

    error_at_current(p, "expected type annotation");
    return ast_create_node(NODE_TYPE_ANNOTATION, t->line, t->col);
}

/* ================================================================== */
/*  Expression parsing  (precedence climbing)                         */
/* ================================================================== */

/* --- Primary ------------------------------------------------------ */

static ASTNode *parse_primary(Parser *p) {
    const ParserToken *t = peek(p);

    /* Integer literal */
    if (match(p, TOKEN_INTEGER)) {
        ASTNode *n = ast_create_node(NODE_INT_LITERAL, t->line, t->col);
        n->int_val = t->lexeme ? strtol(t->lexeme, NULL, 0) : 0;
        n->string_val = str_dup(t->lexeme);
        return n;
    }

    /* Float literal */
    if (match(p, TOKEN_FLOAT)) {
        ASTNode *n = ast_create_node(NODE_FLOAT_LITERAL, t->line, t->col);
        n->float_val = t->lexeme ? strtod(t->lexeme, NULL) : 0.0;
        n->string_val = str_dup(t->lexeme);
        return n;
    }

    /* String literal */
    if (match(p, TOKEN_STRING)) {
        ASTNode *n = ast_create_node(NODE_STRING_LITERAL, t->line, t->col);
        n->string_val = str_dup(t->lexeme);
        return n;
    }

    /* Boolean literals */
    if (match(p, TOKEN_TRUE)) {
        ASTNode *n = ast_create_node(NODE_BOOL_LITERAL, t->line, t->col);
        n->bool_val = 1;
        n->string_val = str_dup("true");
        return n;
    }
    if (match(p, TOKEN_FALSE)) {
        ASTNode *n = ast_create_node(NODE_BOOL_LITERAL, t->line, t->col);
        n->bool_val = 0;
        n->string_val = str_dup("false");
        return n;
    }

    /* None */
    if (match(p, TOKEN_NONE)) {
        return ast_create_node(NODE_NONE, t->line, t->col);
    }

    /* Some(expr) */
    if (match(p, TOKEN_SOME)) {
        ASTNode *n = ast_create_node(NODE_SOME, t->line, t->col);
        expect(p, TOKEN_LPAREN, "'(' after Some");
        ASTNode *val = parse_expression(p);
        ast_add_child(n, val);
        expect(p, TOKEN_RPAREN, "')' after Some value");
        return n;
    }

    /* Array literal [expr, ...] */
    if (match(p, TOKEN_LBRACKET)) {
        ASTNode *arr = ast_create_node(NODE_ARRAY_LITERAL, t->line, t->col);
        if (!check(p, TOKEN_RBRACKET)) {
            do {
                ast_add_child(arr, parse_expression(p));
            } while (match(p, TOKEN_COMMA));
        }
        expect(p, TOKEN_RBRACKET, "']' to close array literal");
        return arr;
    }

    /* Grouped expression (expr) */
    if (match(p, TOKEN_LPAREN)) {
        ASTNode *expr = parse_expression(p);
        expect(p, TOKEN_RPAREN, "')' to close grouped expression");
        return expr;
    }

    /* Error level literals: Level.SILENT, etc. */
    if (match_any(p, TOKEN_LEVEL_SILENT, TOKEN_LEVEL_LOGIC,
                  TOKEN_LEVEL_WARNING, TOKEN_LEVEL_SYSTEM,
                  TOKEN_LEVEL_CATASTROPHIC, -1)) {
        ASTNode *n = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
        n->string_val = str_dup(t->lexeme ? t->lexeme
                                          : token_type_names[t->type]);
        return n;
    }

    /* Identifier */
    if (match(p, TOKEN_IDENTIFIER)) {
        ASTNode *n = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
        n->string_val = str_dup(t->lexeme);
        return n;
    }

    error_at_current(p, "expected expression, found '%s'",
                     t->lexeme ? t->lexeme : token_type_names[t->type]);
    advance(p); /* skip the unexpected token */
    return ast_create_node(NODE_IDENTIFIER, t->line, t->col);
}

/* --- Postfix: calls, struct init, field / member access ----------- */

/*
 * If the previous expression is a bare identifier naming a registered
 * struct, and the next token is '{', parse a struct literal:
 *     Name { field: value, field: value, ... }
 *
 * The AST node layout for NODE_STRUCT_INIT:
 *   string_val = struct type name
 *   children[i] (NODE_VAR_DECL) :
 *       string_val = field name
 *       children[0] = value expression
 *
 * Returns the new STRUCT_INIT node, or NULL if `expr` is not a struct
 * name (caller should treat this as a regular identifier reference).
 */
static ASTNode *try_parse_struct_init(Parser *p, ASTNode *expr) {
    if (!expr || expr->type != NODE_IDENTIFIER || !expr->string_val)
        return NULL;
    if (!struct_registry_get(expr->string_val))
        return NULL;
    if (!check(p, TOKEN_LBRACE))
        return NULL;

    advance(p); /* consume '{' */

    ASTNode *init = ast_create_node(NODE_STRUCT_INIT, expr->line, expr->col);
    init->string_val = str_dup(expr->string_val);

    if (!check(p, TOKEN_RBRACE)) {
        do {
            const ParserToken *fname = expect(p, TOKEN_IDENTIFIER, "field name");
            expect(p, TOKEN_COLON, "':' after field name");
            ASTNode *val = parse_expression(p);

            ASTNode *field = ast_create_node(NODE_VAR_DECL,
                                             fname ? fname->line : expr->line,
                                             fname ? fname->col  : expr->col);
            field->string_val = fname ? str_dup(fname->lexeme) : str_dup("");
            ast_add_child(field, val);
            ast_add_child(init, field);
        } while (match(p, TOKEN_COMMA) && !check(p, TOKEN_RBRACE));
    }
    expect(p, TOKEN_RBRACE, "'}' to close struct literal");

    /* We no longer need the plain identifier node — free it. */
    ast_free(expr);
    return init;
}

static ASTNode *parse_call(Parser *p) {
    ASTNode *expr = parse_primary(p);

    /* Enum variant access: Name::Variant.  Lowered eagerly to the variant
     * index so downstream code sees a plain integer.  We still emit a
     * NODE_ENUM_VARIANT with the resolved value so debug printing is
     * informative, but it carries no runtime semantics beyond an int. */
    if (expr && expr->type == NODE_IDENTIFIER && expr->string_val &&
        check(p, TOKEN_COLONCOLON)) {
        advance(p); /* consume :: */
        const ParserToken *vname = expect(p, TOKEN_IDENTIFIER, "variant name after '::'");
        int idx = -1;
        const char *enum_name = expr->string_val;
        if (vname && vname->lexeme) {
            idx = enum_registry_variant_index(enum_name, vname->lexeme);
            if (idx < 0) {
                parser_error(p, vname->line, vname->col,
                             "unknown enum or variant '%s::%s'",
                             enum_name, vname->lexeme);
                idx = 0;
            }
        }
        ASTNode *var = ast_create_node(NODE_ENUM_VARIANT,
                                       expr->line, expr->col);
        var->op         = str_dup(enum_name);
        var->string_val = vname ? str_dup(vname->lexeme) : str_dup("");
        var->int_val    = idx;
        ast_free(expr);
        expr = var;
    }

    /* Struct literal: Name { x: 1, y: 2 } */
    {
        ASTNode *sinit = try_parse_struct_init(p, expr);
        if (sinit) expr = sinit;
    }

    for (;;) {
        if (match(p, TOKEN_LPAREN)) {
            /* Function call */
            ASTNode *call = ast_create_node(NODE_CALL, expr->line, expr->col);
            ast_add_child(call, expr); /* callee */

            if (!check(p, TOKEN_RPAREN)) {
                do {
                    ast_add_child(call, parse_expression(p));
                } while (match(p, TOKEN_COMMA));
            }
            expect(p, TOKEN_RPAREN, "')' after arguments");
            expr = call;

        } else if (match(p, TOKEN_DOT)) {
            /* Dotted access:
             *   - If followed by `identifier (` it is a namespaced call
             *     (log.error, etc.) — represented via NODE_MEMBER_ACCESS.
             *   - Otherwise, it is a struct field access — NODE_FIELD_ACCESS.
             * Both nodes carry the member/field name in string_val and the
             * object in children[0]. */
            const ParserToken *member = expect(p, TOKEN_IDENTIFIER, "member name after '.'");

            NodeType access_type = NODE_FIELD_ACCESS;
            /* Namespaced calls (e.g. log.error(...)) keep the legacy
             * NODE_MEMBER_ACCESS node so existing codegen works. */
            if (check(p, TOKEN_LPAREN))
                access_type = NODE_MEMBER_ACCESS;

            ASTNode *access = ast_create_node(access_type,
                                              expr->line, expr->col);
            ast_add_child(access, expr);
            access->string_val = member ? str_dup(member->lexeme) : str_dup("");
            expr = access;

        } else {
            break;
        }
    }

    return expr;
}

/* --- Unary -------------------------------------------------------- */

static ASTNode *parse_unary(Parser *p) {
    if (check(p, TOKEN_MINUS) || check(p, TOKEN_NOT)) {
        const ParserToken *op = advance(p);
        ASTNode *operand = parse_unary(p);
        ASTNode *node = ast_create_node(NODE_UNARY_OP, op->line, op->col);
        node->op = str_dup(op->lexeme ? op->lexeme : token_type_names[op->type]);
        ast_add_child(node, operand);
        return node;
    }
    return parse_call(p);
}

/* --- Binary (precedence climbing) --------------------------------- */

static ASTNode *parse_multiplication(Parser *p) {
    ASTNode *left = parse_unary(p);

    while (check(p, TOKEN_STAR) || check(p, TOKEN_SLASH) ||
           check(p, TOKEN_PERCENT)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_unary(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup(op->lexeme ? op->lexeme : token_type_names[op->type]);
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_addition(Parser *p) {
    ASTNode *left = parse_multiplication(p);

    while (check(p, TOKEN_PLUS) || check(p, TOKEN_MINUS)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_multiplication(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup(op->lexeme ? op->lexeme : token_type_names[op->type]);
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_shift(p);

    while (check(p, TOKEN_LT) || check(p, TOKEN_GT) ||
           check(p, TOKEN_LTE) || check(p, TOKEN_GTE)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_shift(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup(op->lexeme ? op->lexeme : token_type_names[op->type]);
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_comparison(p);

    while (check(p, TOKEN_EQ) || check(p, TOKEN_NEQ)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_comparison(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup(op->lexeme ? op->lexeme : token_type_names[op->type]);
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

/* --- Bitwise operators (between equality and logical AND) --- */

static ASTNode *parse_shift(Parser *p) {
    ASTNode *left = parse_addition(p);

    while (check(p, TOKEN_SHL) || check(p, TOKEN_SHR)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_addition(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup(op->lexeme ? op->lexeme : token_type_names[op->type]);
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_bitwise_and(Parser *p) {
    ASTNode *left = parse_equality(p);

    while (check(p, TOKEN_BIT_AND)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_equality(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup("&");
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_bitwise_xor(Parser *p) {
    ASTNode *left = parse_bitwise_and(p);

    while (check(p, TOKEN_BIT_XOR)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_bitwise_and(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup("^");
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_bitwise_or(Parser *p) {
    ASTNode *left = parse_bitwise_xor(p);

    while (check(p, TOKEN_BIT_OR)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_bitwise_xor(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup("|");
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_and(Parser *p) {
    ASTNode *left = parse_bitwise_or(p);

    while (check(p, TOKEN_AND)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_bitwise_or(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup("&&");
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_or(Parser *p) {
    ASTNode *left = parse_and(p);

    while (check(p, TOKEN_OR)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_and(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup("||");
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }
    return left;
}

static ASTNode *parse_assignment(Parser *p) {
    ASTNode *left = parse_or(p);

    /* Ternary: cond ? then_expr : else_expr */
    if (check(p, TOKEN_QUESTION)) {
        advance(p);
        ASTNode *then_expr = parse_expression(p);
        expect(p, TOKEN_COLON, "':' in ternary operator");
        ASTNode *else_expr = parse_expression(p);
        ASTNode *node = ast_create_node(NODE_IF, left->line, left->col);
        ast_add_child(node, left);
        ASTNode *tb = ast_create_node(NODE_BLOCK, then_expr->line, then_expr->col);
        ASTNode *ts = ast_create_node(NODE_EXPR_STMT, then_expr->line, then_expr->col);
        ast_add_child(ts, then_expr); ast_add_child(tb, ts);
        ast_add_child(node, tb);
        ASTNode *eb = ast_create_node(NODE_BLOCK, else_expr->line, else_expr->col);
        ASTNode *es = ast_create_node(NODE_EXPR_STMT, else_expr->line, else_expr->col);
        ast_add_child(es, else_expr); ast_add_child(eb, es);
        ast_add_child(node, eb);
        return node;
    }

    if (check(p, TOKEN_ASSIGN)) {
        const ParserToken *op = advance(p);
        ASTNode *right = parse_assignment(p);
        ASTNode *bin = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        bin->op = str_dup("=");
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        return bin;
    }

    /* Compound assignments: x += e → x = x + e */
    if (check(p, TOKEN_PLUS_ASSIGN) || check(p, TOKEN_MINUS_ASSIGN) ||
        check(p, TOKEN_STAR_ASSIGN) || check(p, TOKEN_SLASH_ASSIGN)) {
        const ParserToken *op = advance(p);
        const char *arith_op = "+";
        if (op->type == TOKEN_MINUS_ASSIGN) arith_op = "-";
        else if (op->type == TOKEN_STAR_ASSIGN) arith_op = "*";
        else if (op->type == TOKEN_SLASH_ASSIGN) arith_op = "/";

        ASTNode *rhs_expr = parse_assignment(p);

        /* Build: x = x <op> rhs */
        ASTNode *arith = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        arith->op = str_dup(arith_op);
        /* Clone left as the read side of x */
        ASTNode *left_copy = ast_create_node(left->type, left->line, left->col);
        if (left->string_val) left_copy->string_val = str_dup(left->string_val);
        left_copy->int_val = left->int_val;
        ast_add_child(arith, left_copy);
        ast_add_child(arith, rhs_expr);

        ASTNode *assign = ast_create_node(NODE_BINARY_OP, op->line, op->col);
        assign->op = str_dup("=");
        ast_add_child(assign, left);
        ast_add_child(assign, arith);
        return assign;
    }

    return left;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_assignment(p);
}

/* ================================================================== */
/*  Statement parsing                                                 */
/* ================================================================== */

/* --- Block -------------------------------------------------------- */

static ASTNode *parse_block(Parser *p) {
    const ParserToken *t = peek(p);
    expect(p, TOKEN_LBRACE, "'{'");

    ASTNode *block = ast_create_node(NODE_BLOCK, t->line, t->col);

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        ASTNode *decl = parse_declaration(p);
        if (decl)
            ast_add_child(block, decl);
    }

    expect(p, TOKEN_RBRACE, "'}'");
    return block;
}

/* --- Return ------------------------------------------------------- */

static ASTNode *parse_return(Parser *p) {
    const ParserToken *t = previous(p); /* TOKEN_RETURN already consumed */
    ASTNode *node = ast_create_node(NODE_RETURN, t->line, t->col);

    if (!check(p, TOKEN_SEMICOLON)) {
        ast_add_child(node, parse_expression(p));
    }
    expect(p, TOKEN_SEMICOLON, "';' after return statement");
    return node;
}

/* --- If ----------------------------------------------------------- */

static ASTNode *parse_if(Parser *p) {
    const ParserToken *t = previous(p); /* TOKEN_IF already consumed */
    ASTNode *node = ast_create_node(NODE_IF, t->line, t->col);

    expect(p, TOKEN_LPAREN, "'(' after if");
    ast_add_child(node, parse_expression(p)); /* condition */
    expect(p, TOKEN_RPAREN, "')' after if condition");

    ast_add_child(node, parse_block(p));      /* then branch */

    if (match(p, TOKEN_ELSE)) {
        if (check(p, TOKEN_IF)) {
            advance(p);
            ast_add_child(node, parse_if(p)); /* else if */
        } else {
            ast_add_child(node, parse_block(p)); /* else */
        }
    }

    return node;
}

/* --- For ---------------------------------------------------------- */

static ASTNode *parse_for(Parser *p) {
    const ParserToken *t = previous(p); /* TOKEN_FOR already consumed */
    ASTNode *node = ast_create_node(NODE_FOR, t->line, t->col);

    expect(p, TOKEN_LPAREN, "'(' after for");

    /* Check for for-each: for (let x in arr) { ... }
     * Desugars to: for (let __i=0; __i < arr_len(arr); __i = __i+1)
     * with `let x = arr_get(arr, __i)` prepended to body. */
    if (check(p, TOKEN_LET)) {
        advance(p);
        const ParserToken *name_tok = expect(p, TOKEN_IDENTIFIER, "variable name");

        if (check(p, TOKEN_IN)) {
            /* FOR-EACH syntax: for (let x in collection) { ... } */
            advance(p); /* consume 'in' */
            ASTNode *collection = parse_expression(p);
            expect(p, TOKEN_RPAREN, "')' after for-in");

            /* Create: let __i: i32 = 0 */
            ASTNode *init = ast_create_node(NODE_VAR_DECL, t->line, t->col);
            init->string_val = str_dup("__fi");
            ASTNode *i_type = ast_create_node(NODE_TYPE_ANNOTATION, t->line, t->col);
            i_type->string_val = str_dup("i32");
            ast_add_child(init, i_type);
            ASTNode *zero = ast_create_node(NODE_INT_LITERAL, t->line, t->col);
            zero->int_val = 0;
            ast_add_child(init, zero);
            ast_add_child(node, init);

            /* Condition: __i < arr_len(collection) */
            ASTNode *cond = ast_create_node(NODE_BINARY_OP, t->line, t->col);
            cond->op = str_dup("<");
            ASTNode *i_ref = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            i_ref->string_val = str_dup("__fi");
            ast_add_child(cond, i_ref);
            /* arr_len(collection) */
            ASTNode *len_call = ast_create_node(NODE_CALL, t->line, t->col);
            ASTNode *len_id = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            len_id->string_val = str_dup("arr_len");
            ast_add_child(len_call, len_id);
            ast_add_child(len_call, ast_create_node(NODE_IDENTIFIER, t->line, t->col));
            len_call->children[1]->string_val = str_dup(collection->string_val ? collection->string_val : "__arr");
            ast_add_child(cond, len_call);
            ast_add_child(node, cond);

            /* Update: __i = __i + 1 */
            ASTNode *update = ast_create_node(NODE_BINARY_OP, t->line, t->col);
            update->op = str_dup("=");
            ASTNode *i_lhs = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            i_lhs->string_val = str_dup("__fi");
            ast_add_child(update, i_lhs);
            ASTNode *inc = ast_create_node(NODE_BINARY_OP, t->line, t->col);
            inc->op = str_dup("+");
            ASTNode *i_val = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            i_val->string_val = str_dup("__fi");
            ast_add_child(inc, i_val);
            ASTNode *one = ast_create_node(NODE_INT_LITERAL, t->line, t->col);
            one->int_val = 1;
            ast_add_child(inc, one);
            ast_add_child(update, inc);
            ast_add_child(node, update);

            /* Parse body block */
            ASTNode *body = parse_block(p);

            /* Prepend: let x = arr_get(collection, __i) */
            ASTNode *elem_decl = ast_create_node(NODE_VAR_DECL, t->line, t->col);
            elem_decl->string_val = name_tok ? str_dup(name_tok->lexeme) : str_dup("_");
            ASTNode *get_call = ast_create_node(NODE_CALL, t->line, t->col);
            ASTNode *get_id = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            get_id->string_val = str_dup("arr_get");
            ast_add_child(get_call, get_id);
            ASTNode *coll_ref = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            coll_ref->string_val = str_dup(collection->string_val ? collection->string_val : "__arr");
            ast_add_child(get_call, coll_ref);
            ASTNode *i_idx = ast_create_node(NODE_IDENTIFIER, t->line, t->col);
            i_idx->string_val = str_dup("__fi");
            ast_add_child(get_call, i_idx);
            ast_add_child(elem_decl, get_call);

            /* Insert elem_decl at the beginning of body */
            /* Shift children right and insert at 0 */
            ast_add_child(body, NULL); /* make room */
            for (size_t bi = body->child_count - 1; bi > 0; bi--)
                body->children[bi] = body->children[bi - 1];
            body->children[0] = elem_decl;

            ast_add_child(node, body);
            ast_free(collection);
            return node;
        }

        /* Regular C-style for: continue parsing let init */
        ASTNode *init = ast_create_node(NODE_VAR_DECL, t->line, t->col);
        init->string_val = name_tok ? str_dup(name_tok->lexeme) : str_dup("");

        if (match(p, TOKEN_COLON)) {
            ast_add_child(init, parse_type(p));
        }
        if (match(p, TOKEN_ASSIGN)) {
            ast_add_child(init, parse_expression(p));
        }
        expect(p, TOKEN_SEMICOLON, "';' after for initializer");
        ast_add_child(node, init);
    } else if (check(p, TOKEN_SEMICOLON)) {
        advance(p);
        /* empty initializer -- add a null placeholder */
        ast_add_child(node, ast_create_node(NODE_EXPR_STMT, t->line, t->col));
    } else {
        ASTNode *init_expr = parse_expression(p);
        ASTNode *init_stmt = ast_create_node(NODE_EXPR_STMT,
                                             init_expr->line, init_expr->col);
        ast_add_child(init_stmt, init_expr);
        expect(p, TOKEN_SEMICOLON, "';' after for initializer");
        ast_add_child(node, init_stmt);
    }

    /* Condition */
    if (!check(p, TOKEN_SEMICOLON))
        ast_add_child(node, parse_expression(p));
    else
        ast_add_child(node, ast_create_node(NODE_BOOL_LITERAL, t->line, t->col));
    expect(p, TOKEN_SEMICOLON, "';' after for condition");

    /* Update */
    if (!check(p, TOKEN_RPAREN))
        ast_add_child(node, parse_expression(p));
    else
        ast_add_child(node, ast_create_node(NODE_EXPR_STMT, t->line, t->col));

    expect(p, TOKEN_RPAREN, "')' after for clauses");
    ast_add_child(node, parse_block(p));

    return node;
}

/* --- While -------------------------------------------------------- */

static ASTNode *parse_while(Parser *p) {
    const ParserToken *t = previous(p);
    ASTNode *node = ast_create_node(NODE_WHILE, t->line, t->col);

    expect(p, TOKEN_LPAREN, "'(' after while");
    ast_add_child(node, parse_expression(p));
    expect(p, TOKEN_RPAREN, "')' after while condition");
    ast_add_child(node, parse_block(p));

    return node;
}

/* --- Match -------------------------------------------------------- */

static ASTNode *parse_match(Parser *p) {
    const ParserToken *t = previous(p);
    ASTNode *node = ast_create_node(NODE_MATCH, t->line, t->col);

    expect(p, TOKEN_LPAREN, "'(' after match");
    ast_add_child(node, parse_expression(p));
    expect(p, TOKEN_RPAREN, "')' after match expression");

    expect(p, TOKEN_LBRACE, "'{' to open match body");

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        const ParserToken *arm_tok = peek(p);
        ASTNode *arm = ast_create_node(NODE_MATCH_ARM,
                                       arm_tok->line, arm_tok->col);

        /* Pattern (for now just an expression) */
        ast_add_child(arm, parse_expression(p));
        expect(p, TOKEN_FAT_ARROW, "'=>' in match arm");

        /* Body: block or single expression followed by comma/semicolon */
        if (check(p, TOKEN_LBRACE)) {
            ast_add_child(arm, parse_block(p));
            match(p, TOKEN_COMMA); /* optional trailing comma */
        } else {
            ASTNode *body_expr = parse_expression(p);
            ASTNode *body_stmt = ast_create_node(NODE_EXPR_STMT,
                                                 body_expr->line, body_expr->col);
            ast_add_child(body_stmt, body_expr);
            ast_add_child(arm, body_stmt);
            /* expect comma or allow the closing brace */
            if (!check(p, TOKEN_RBRACE))
                expect(p, TOKEN_COMMA, "',' after match arm expression");
        }

        ast_add_child(node, arm);
    }

    expect(p, TOKEN_RBRACE, "'}' to close match");
    return node;
}

/* --- Try / Catch -------------------------------------------------- */

static ASTNode *parse_try_catch(Parser *p) {
    const ParserToken *t = previous(p);
    ASTNode *node = ast_create_node(NODE_TRY_CATCH, t->line, t->col);

    ast_add_child(node, parse_block(p)); /* try block */

    expect(p, TOKEN_CATCH, "'catch' after try block");
    expect(p, TOKEN_LPAREN, "'(' after catch");

    const ParserToken *name = expect(p, TOKEN_IDENTIFIER, "catch variable name");
    node->string_val = name ? str_dup(name->lexeme) : str_dup("");

    expect(p, TOKEN_COLON, "':' after catch variable");
    ast_add_child(node, parse_type(p));  /* catch type */
    expect(p, TOKEN_RPAREN, "')' after catch clause");

    ast_add_child(node, parse_block(p)); /* catch block */

    return node;
}

/* --- error.raise(...) --------------------------------------------- */

static ASTNode *parse_error_raise(Parser *p) {
    const ParserToken *t = previous(p); /* TOKEN_ERROR already consumed */
    ASTNode *node = ast_create_node(NODE_ERROR_RAISE, t->line, t->col);

    expect(p, TOKEN_DOT, "'.' after error");
    const ParserToken *member = expect(p, TOKEN_IDENTIFIER, "'raise' after error.");
    /* We could validate member->lexeme == "raise" here */
    if (member && member->lexeme)
        node->string_val = str_dup(member->lexeme);

    expect(p, TOKEN_LPAREN, "'(' after error.raise");

    /* Arguments: level, message */
    ast_add_child(node, parse_expression(p));
    if (match(p, TOKEN_COMMA))
        ast_add_child(node, parse_expression(p));

    expect(p, TOKEN_RPAREN, "')' after error.raise arguments");
    expect(p, TOKEN_SEMICOLON, "';' after error.raise");

    return node;
}

/* --- Statement dispatcher ----------------------------------------- */

static ASTNode *parse_statement(Parser *p) {
    if (match(p, TOKEN_RETURN))  return parse_return(p);
    if (match(p, TOKEN_IF))      return parse_if(p);
    if (match(p, TOKEN_FOR))     return parse_for(p);
    if (match(p, TOKEN_WHILE))   return parse_while(p);
    if (match(p, TOKEN_MATCH))   return parse_match(p);
    if (match(p, TOKEN_TRY))     return parse_try_catch(p);
    if (match(p, TOKEN_ERROR))   return parse_error_raise(p);
    if (match(p, TOKEN_BREAK)) {
        ASTNode *n = ast_create_node(NODE_BREAK, previous(p)->line, previous(p)->col);
        expect(p, TOKEN_SEMICOLON, "';' after break");
        return n;
    }
    if (match(p, TOKEN_CONTINUE)) {
        ASTNode *n = ast_create_node(NODE_CONTINUE, previous(p)->line, previous(p)->col);
        expect(p, TOKEN_SEMICOLON, "';' after continue");
        return n;
    }

    if (check(p, TOKEN_LBRACE))  return parse_block(p);

    /* Expression statement */
    ASTNode *expr = parse_expression(p);
    ASTNode *stmt = ast_create_node(NODE_EXPR_STMT, expr->line, expr->col);
    ast_add_child(stmt, expr);
    expect(p, TOKEN_SEMICOLON, "';' after expression");
    return stmt;
}

/* ================================================================== */
/*  Declaration parsing                                               */
/* ================================================================== */

/* --- Variable / constant declaration ------------------------------ */

static ASTNode *parse_var_declaration(Parser *p, bool is_const) {
    const ParserToken *kw = previous(p);
    NodeType ntype = is_const ? NODE_CONST_DECL : NODE_VAR_DECL;

    const ParserToken *name = expect(p, TOKEN_IDENTIFIER, "variable name");

    ASTNode *node = ast_create_node(ntype, kw->line, kw->col);
    node->string_val = name ? str_dup(name->lexeme) : str_dup("");

    /* Type annotation (required) */
    expect(p, TOKEN_COLON, "':' for type annotation");
    ast_add_child(node, parse_type(p));

    /* Initializer */
    expect(p, TOKEN_ASSIGN, "'=' for initializer");
    ast_add_child(node, parse_expression(p));

    expect(p, TOKEN_SEMICOLON, "';' after declaration");
    return node;
}

/* --- Function declaration ----------------------------------------- */

static ASTNode *parse_fn_declaration(Parser *p) {
    const ParserToken *kw = previous(p); /* TOKEN_FN already consumed */

    const ParserToken *name = expect(p, TOKEN_IDENTIFIER, "function name");

    ASTNode *fn = ast_create_node(NODE_FN_DECL, kw->line, kw->col);
    fn->string_val = name ? str_dup(name->lexeme) : str_dup("");

    /* Parameter list */
    expect(p, TOKEN_LPAREN, "'(' after function name");

    /* We create a temporary BLOCK node to hold parameter declarations. */
    ASTNode *params = ast_create_node(NODE_BLOCK, kw->line, kw->col);

    if (!check(p, TOKEN_RPAREN)) {
        do {
            const ParserToken *pname = expect(p, TOKEN_IDENTIFIER, "parameter name");
            expect(p, TOKEN_COLON, "':' after parameter name");
            ASTNode *ptype = parse_type(p);

            ASTNode *param = ast_create_node(NODE_VAR_DECL,
                                             pname ? pname->line : kw->line,
                                             pname ? pname->col  : kw->col);
            param->string_val = pname ? str_dup(pname->lexeme) : str_dup("");
            ast_add_child(param, ptype);
            ast_add_child(params, param);
        } while (match(p, TOKEN_COMMA));
    }

    expect(p, TOKEN_RPAREN, "')' after parameters");
    ast_add_child(fn, params); /* child 0: params */

    /* Return type (optional, indicated by ->) */
    if (match(p, TOKEN_ARROW)) {
        ast_add_child(fn, parse_type(p)); /* child 1: return type */
    }

    /* Body */
    ast_add_child(fn, parse_block(p)); /* child 1 or 2: body */

    return fn;
}

/* --- Struct declaration ------------------------------------------- */

/*
 * struct Name {
 *     field1: type1,
 *     field2: type2,
 * }
 *
 * AST layout for NODE_STRUCT_DECL:
 *   string_val = struct name
 *   children[i] = NODE_VAR_DECL  (field declaration)
 *       string_val = field name
 *       children[0] = NODE_TYPE_ANNOTATION
 *
 * The declaration is also registered in the global struct registry
 * so that later expressions can recognise `Name { ... }` literals and
 * `p.x` field accesses.
 */
static ASTNode *parse_struct_declaration(Parser *p) {
    const ParserToken *kw = previous(p);           /* `struct` already eaten */
    const ParserToken *name = expect(p, TOKEN_IDENTIFIER, "struct name");

    ASTNode *node = ast_create_node(NODE_STRUCT_DECL, kw->line, kw->col);
    node->string_val = name ? str_dup(name->lexeme) : str_dup("");

    /* Register in the global registry BEFORE parsing the body so that
     * the body can theoretically reference other structs (and so that
     * duplicate names are caught early). */
    if (name && name->lexeme) {
        if (!struct_registry_add(name->lexeme)) {
            parser_error(p, name->line, name->col,
                         "duplicate struct declaration '%s'", name->lexeme);
        }
    }

    expect(p, TOKEN_LBRACE, "'{' to open struct body");

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        const ParserToken *fname = expect(p, TOKEN_IDENTIFIER, "field name");
        expect(p, TOKEN_COLON, "':' after field name");
        ASTNode *ftype = parse_type(p);

        ASTNode *field = ast_create_node(NODE_VAR_DECL,
                                         fname ? fname->line : kw->line,
                                         fname ? fname->col  : kw->col);
        field->string_val = fname ? str_dup(fname->lexeme) : str_dup("");
        ast_add_child(field, ftype);
        ast_add_child(node, field);

        /* Register field */
        if (name && name->lexeme && fname && fname->lexeme) {
            const char *tname = ftype->string_val ? ftype->string_val : "i32";
            struct_registry_add_field(name->lexeme, fname->lexeme, tname);
        }

        /* Trailing comma optional */
        if (!match(p, TOKEN_COMMA) && !check(p, TOKEN_RBRACE)) {
            /* Accept semicolons too, defensively */
            match(p, TOKEN_SEMICOLON);
        }
    }

    expect(p, TOKEN_RBRACE, "'}' to close struct body");
    return node;
}

/* ================================================================== */
/*  Enum declarations                                                  */
/* ================================================================== */
/*
 * enum Name { Variant1, Variant2, ... }
 *
 * AST layout for NODE_ENUM_DECL:
 *   string_val = enum name
 *   children[i] = NODE_IDENTIFIER (variant name)
 *
 * Each variant is registered in the enum registry with its zero-based
 * index so that later expressions can lower `Name::Variant` to an int.
 */
static ASTNode *parse_enum_declaration(Parser *p) {
    const ParserToken *kw = previous(p);
    const ParserToken *name = expect(p, TOKEN_IDENTIFIER, "enum name");

    ASTNode *node = ast_create_node(NODE_ENUM_DECL, kw->line, kw->col);
    node->string_val = name ? str_dup(name->lexeme) : str_dup("");

    if (name && name->lexeme) {
        if (!enum_registry_add(name->lexeme)) {
            parser_error(p, name->line, name->col,
                         "duplicate enum declaration '%s'", name->lexeme);
        }
    }

    expect(p, TOKEN_LBRACE, "'{' to open enum body");

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        const ParserToken *vname = expect(p, TOKEN_IDENTIFIER, "variant name");
        if (vname) {
            ASTNode *vnode = ast_create_node(NODE_IDENTIFIER, vname->line, vname->col);
            vnode->string_val = str_dup(vname->lexeme);
            ast_add_child(node, vnode);

            if (name && name->lexeme && vname->lexeme) {
                if (!enum_registry_add_variant(name->lexeme, vname->lexeme)) {
                    parser_error(p, vname->line, vname->col,
                                 "duplicate variant '%s' in enum '%s'",
                                 vname->lexeme, name->lexeme);
                }
            }
        }

        if (!match(p, TOKEN_COMMA) && !check(p, TOKEN_RBRACE)) {
            match(p, TOKEN_SEMICOLON);
        }
    }

    expect(p, TOKEN_RBRACE, "'}' to close enum body");
    return node;
}

/* --- Top-level declaration dispatcher ----------------------------- */

static ASTNode *parse_declaration(Parser *p) {
    ASTNode *node = NULL;

    if (match(p, TOKEN_FN)) {
        node = parse_fn_declaration(p);
    } else if (match(p, TOKEN_LET)) {
        node = parse_var_declaration(p, false);
    } else if (match(p, TOKEN_CONST)) {
        node = parse_var_declaration(p, true);
    } else if (match(p, TOKEN_STRUCT)) {
        node = parse_struct_declaration(p);
    } else if (match(p, TOKEN_ENUM)) {
        node = parse_enum_declaration(p);
    } else {
        node = parse_statement(p);
    }

    if (p->panic_mode) synchronize(p);
    return node;
}

/* ================================================================== */
/*  Program (entry point)                                             */
/* ================================================================== */

static ASTNode *parse_program(Parser *p) {
    ASTNode *program = ast_create_node(NODE_PROGRAM, 1, 1);

    while (!check(p, TOKEN_EOF)) {
        ASTNode *decl = parse_declaration(p);
        if (decl)
            ast_add_child(program, decl);
    }

    return program;
}

/* ================================================================== */
/*  Public API                                                        */
/* ================================================================== */

void parser_init(Parser *p, const ParserToken *tokens, size_t token_count) {
    memset(p, 0, sizeof(*p));
    p->tokens      = tokens;
    p->token_count = token_count;
    p->pos         = 0;
}

ASTNode *parser_parse(Parser *p) {
    return parse_program(p);
}

bool parser_has_errors(const Parser *p) {
    return p->error_count > 0;
}

void parser_print_errors(const Parser *p) {
    for (size_t i = 0; i < p->error_count; i++) {
        const ParserError *e = &p->errors[i];
        fprintf(stderr, "aricode:parser: [%d:%d] error: %s\n",
                e->line, e->col, e->message);
    }
}
