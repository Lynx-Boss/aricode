/*
 * aricode - Ari Code Language
 * AST (Abstract Syntax Tree) Node Definitions
 *
 * Every AST node carries source location (line, column) for precise
 * error reporting, which is central to aricode's error philosophy.
 */

#ifndef ARICODE_AST_H
#define ARICODE_AST_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Node types                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    /* Program root */
    NODE_PROGRAM,

    /* Declarations */
    NODE_VAR_DECL,          /* let x: i32 = 10;             */
    NODE_CONST_DECL,        /* const PI: f64 = 3.14;        */
    NODE_FN_DECL,           /* fn name(params) -> type { }  */

    /* Statements */
    NODE_RETURN,
    NODE_IF,
    NODE_FOR,
    NODE_WHILE,
    NODE_MATCH,
    NODE_MATCH_ARM,
    NODE_TRY_CATCH,
    NODE_ERROR_RAISE,
    NODE_BLOCK,
    NODE_EXPR_STMT,
    NODE_BREAK,
    NODE_CONTINUE,

    /* Expressions */
    NODE_BINARY_OP,         /* a + b, a == b, etc.          */
    NODE_UNARY_OP,          /* -a, !a                       */
    NODE_CALL,              /* func(args)                   */
    NODE_IDENTIFIER,
    NODE_INT_LITERAL,
    NODE_FLOAT_LITERAL,
    NODE_DEC_LITERAL,       /* 0.1d, precise decimal literal   */
    NODE_STRING_LITERAL,
    NODE_BOOL_LITERAL,
    NODE_ARRAY_LITERAL,
    NODE_SOME,              /* Some(value)                  */
    NODE_NONE,              /* None                         */
    NODE_MEMBER_ACCESS,     /* error.raise, log.error       */

    /* Structs */
    NODE_STRUCT_DECL,       /* struct Name { field: type, ... }   */
    NODE_STRUCT_INIT,       /* Name { x: val, y: val }            */
    NODE_FIELD_ACCESS,      /* p.x  (reads/writes struct field)   */

    /* Enums */
    NODE_ENUM_DECL,         /* enum Name { Variant, ... }         */
    NODE_ENUM_VARIANT,      /* Name::Variant  (loads int constant) */

    /* Types */
    NODE_TYPE_ANNOTATION,   /* : i32, : str, : Option<i32>  */

    NODE_TYPE_COUNT
} NodeType;

/* Human-readable names for every node type (for debugging / printing). */
const char *node_type_name(NodeType type);

/* ------------------------------------------------------------------ */
/*  AST Node                                                          */
/* ------------------------------------------------------------------ */

typedef struct ASTNode ASTNode;

struct ASTNode {
    NodeType    type;

    /* Source location -- line and column are 1-based. */
    int         line;
    int         col;

    /* ---------- Payload (depends on node type) ---------- */

    /* Generic string payload: identifier name, literal value,
     * operator symbol, type name, etc.  Heap-allocated, owned. */
    char       *string_val;

    /* Second string payload used by some nodes (e.g. operator in
     * binary/unary, or the catch variable name). */
    char       *op;

    /* Numeric payloads */
    int64_t     int_val;
    double      float_val;
    int         bool_val;

    /* ---------- Children ---------- */

    ASTNode   **children;
    size_t      child_count;
    size_t      child_cap;
};

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

/*
 * Create a new AST node.  The node is heap-allocated and zeroed except
 * for the three mandatory fields.
 */
ASTNode *ast_create_node(NodeType type, int line, int col);

/*
 * Append a child node.  The parent takes ownership.
 */
void ast_add_child(ASTNode *parent, ASTNode *child);

/*
 * Pretty-print the AST for debugging.
 * `indent` is the starting indentation level (normally 0).
 */
void ast_print(const ASTNode *node, int indent);

/*
 * Recursively free an AST tree rooted at `node`.
 */
void ast_free(ASTNode *node);

#endif /* ARICODE_AST_H */
