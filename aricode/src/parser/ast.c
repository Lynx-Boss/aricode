/*
 * aricode - Ari Code Language
 * AST construction and utility functions.
 */

#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Node type names                                                   */
/* ------------------------------------------------------------------ */

static const char *_node_type_names[] = {
    [NODE_PROGRAM]         = "PROGRAM",
    [NODE_VAR_DECL]        = "VAR_DECL",
    [NODE_CONST_DECL]      = "CONST_DECL",
    [NODE_FN_DECL]         = "FN_DECL",
    [NODE_RETURN]          = "RETURN",
    [NODE_IF]              = "IF",
    [NODE_FOR]             = "FOR",
    [NODE_WHILE]           = "WHILE",
    [NODE_MATCH]           = "MATCH",
    [NODE_MATCH_ARM]       = "MATCH_ARM",
    [NODE_TRY_CATCH]       = "TRY_CATCH",
    [NODE_ERROR_RAISE]     = "ERROR_RAISE",
    [NODE_BLOCK]           = "BLOCK",
    [NODE_EXPR_STMT]       = "EXPR_STMT",
    [NODE_BINARY_OP]       = "BINARY_OP",
    [NODE_UNARY_OP]        = "UNARY_OP",
    [NODE_CALL]            = "CALL",
    [NODE_IDENTIFIER]      = "IDENTIFIER",
    [NODE_INT_LITERAL]     = "INT_LITERAL",
    [NODE_FLOAT_LITERAL]   = "FLOAT_LITERAL",
    [NODE_STRING_LITERAL]  = "STRING_LITERAL",
    [NODE_BOOL_LITERAL]    = "BOOL_LITERAL",
    [NODE_ARRAY_LITERAL]   = "ARRAY_LITERAL",
    [NODE_SOME]            = "SOME",
    [NODE_NONE]            = "NONE",
    [NODE_MEMBER_ACCESS]   = "MEMBER_ACCESS",
    [NODE_STRUCT_DECL]     = "STRUCT_DECL",
    [NODE_STRUCT_INIT]     = "STRUCT_INIT",
    [NODE_FIELD_ACCESS]    = "FIELD_ACCESS",
    [NODE_ENUM_DECL]       = "ENUM_DECL",
    [NODE_ENUM_VARIANT]    = "ENUM_VARIANT",
    [NODE_TYPE_ANNOTATION] = "TYPE_ANNOTATION",
};

const char *node_type_name(NodeType type) {
    if (type >= 0 && type < NODE_TYPE_COUNT)
        return _node_type_names[type];
    return "UNKNOWN";
}

/* ------------------------------------------------------------------ */
/*  Construction                                                      */
/* ------------------------------------------------------------------ */

#define INITIAL_CHILD_CAP 4

ASTNode *ast_create_node(NodeType type, int line, int col) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "aricode: out of memory allocating AST node\n");
        exit(1);
    }
    node->type = type;
    node->line = line;
    node->col  = col;
    return node;
}

void ast_add_child(ASTNode *parent, ASTNode *child) {
    if (!parent || !child) return;

    if (parent->child_count == parent->child_cap) {
        size_t new_cap = parent->child_cap == 0
                         ? INITIAL_CHILD_CAP
                         : parent->child_cap * 2;
        ASTNode **tmp = realloc(parent->children, new_cap * sizeof(ASTNode *));
        if (!tmp) {
            fprintf(stderr, "aricode: out of memory growing child array\n");
            exit(1);
        }
        parent->children  = tmp;
        parent->child_cap = new_cap;
    }

    parent->children[parent->child_count++] = child;
}

/* ------------------------------------------------------------------ */
/*  Pretty-printing                                                   */
/* ------------------------------------------------------------------ */

static void print_indent(int level) {
    for (int i = 0; i < level; i++)
        printf("  ");
}

void ast_print(const ASTNode *node, int indent) {
    if (!node) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);
    printf("%s", node_type_name(node->type));
    printf(" [%d:%d]", node->line, node->col);

    /* Print payload when present */
    switch (node->type) {
    case NODE_IDENTIFIER:
    case NODE_STRING_LITERAL:
    case NODE_TYPE_ANNOTATION:
    case NODE_VAR_DECL:
    case NODE_CONST_DECL:
    case NODE_FN_DECL:
        if (node->string_val)
            printf(" name='%s'", node->string_val);
        break;
    case NODE_INT_LITERAL:
        printf(" value=%ld", (long)node->int_val);
        break;
    case NODE_FLOAT_LITERAL:
        printf(" value=%g", node->float_val);
        break;
    case NODE_BOOL_LITERAL:
        printf(" value=%s", node->bool_val ? "true" : "false");
        break;
    case NODE_BINARY_OP:
    case NODE_UNARY_OP:
        if (node->op)
            printf(" op='%s'", node->op);
        break;
    case NODE_MEMBER_ACCESS:
        if (node->string_val)
            printf(" member='%s'", node->string_val);
        break;
    case NODE_STRUCT_DECL:
    case NODE_STRUCT_INIT:
        if (node->string_val)
            printf(" struct='%s'", node->string_val);
        break;
    case NODE_FIELD_ACCESS:
        if (node->string_val)
            printf(" field='%s'", node->string_val);
        break;
    case NODE_ENUM_DECL:
        if (node->string_val)
            printf(" enum='%s'", node->string_val);
        break;
    case NODE_ENUM_VARIANT:
        if (node->string_val)
            printf(" variant='%s' value=%ld", node->string_val, (long)node->int_val);
        break;
    case NODE_ERROR_RAISE:
        if (node->string_val)
            printf(" level='%s'", node->string_val);
        break;
    default:
        if (node->string_val)
            printf(" '%s'", node->string_val);
        break;
    }

    printf("\n");

    for (size_t i = 0; i < node->child_count; i++)
        ast_print(node->children[i], indent + 1);
}

/* ------------------------------------------------------------------ */
/*  Cleanup                                                           */
/* ------------------------------------------------------------------ */

void ast_free(ASTNode *node) {
    if (!node) return;

    for (size_t i = 0; i < node->child_count; i++)
        ast_free(node->children[i]);

    free(node->children);
    free(node->string_val);
    free(node->op);
    free(node);
}
