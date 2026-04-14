/*
 * aricode - Ari Code Language
 * Semantic Analyzer Implementation
 *
 * THE GUARDIAN: walks the AST and enforces aricode's error philosophy.
 * Silent errors are FORBIDDEN.  Logic errors are INADMISSIBLE.
 * Every suspicious pattern is flagged.
 */

#include "analyzer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Internal helpers                                                  */
/* ================================================================== */

static void emit_error(Analyzer *a, AriErrorLevel level,
                       const char *code, const char *suggestion,
                       int line, int col, const char *fmt, ...) {
    if (a->error_count >= ANALYZER_MAX_ERRORS) return;

    SemanticError *err = &a->errors[a->error_count++];
    err->level      = level;
    err->code       = code;
    err->suggestion = suggestion;
    err->line       = line;
    err->col        = col;

    va_list args;
    va_start(args, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);

    switch (level) {
    case ARI_LEVEL_SILENT:  a->level0_count++; break;
    case ARI_LEVEL_LOGIC:   a->level1_count++; break;
    case ARI_LEVEL_WARNING: a->level2_count++; break;
    default: break;
    }
}

/* ================================================================== */
/*  Forward declarations for AST walking                              */
/* ================================================================== */

static AriType *analyze_expr(Analyzer *a, ASTNode *node);
static void     analyze_stmt(Analyzer *a, ASTNode *node);
static void     analyze_block(Analyzer *a, ASTNode *node, bool new_scope);
static AriType *resolve_type_annotation(Analyzer *a, ASTNode *node);

/* ================================================================== */
/*  Type resolution                                                   */
/* ================================================================== */

/*
 * Resolve a NODE_TYPE_ANNOTATION to an AriType.
 * The type name is in node->string_val (e.g. "i32", "str", "Option").
 */
static AriType *resolve_type_annotation(Analyzer *a, ASTNode *node) {
    if (!node || node->type != NODE_TYPE_ANNOTATION) {
        return type_create(TYPE_UNKNOWN);
    }

    const char *name = node->string_val;
    if (!name) return type_create(TYPE_UNKNOWN);

    /* Check for Option<T> */
    if (strcmp(name, "Option") == 0) {
        AriType *inner = type_create(TYPE_UNKNOWN);
        if (node->child_count > 0) {
            inner = resolve_type_annotation(a, node->children[0]);
        }
        return type_create_option(inner);
    }

    AriType *t = type_from_name(name);
    if (!t) {
        emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                   node->line, node->col,
                   "Unknown type '%s'", name);
        return type_create(TYPE_UNKNOWN);
    }
    return t;
}

/* ================================================================== */
/*  Expression analysis (returns the computed type)                    */
/* ================================================================== */

/*
 * Check if a division operation has a zero-guard.
 * A zero guard means the divisor is checked for zero in a surrounding if.
 * For compile-time constant zero divisors, always flag.
 */
static bool is_zero_literal(ASTNode *node) {
    if (!node) return false;
    if (node->type == NODE_INT_LITERAL && node->int_val == 0) return true;
    if (node->type == NODE_FLOAT_LITERAL && node->float_val == 0.0) return true;
    return false;
}

static AriType *analyze_binary_op(Analyzer *a, ASTNode *node) {
    if (node->child_count < 2) return type_create(TYPE_UNKNOWN);

    AriType *left  = analyze_expr(a, node->children[0]);
    AriType *right = analyze_expr(a, node->children[1]);
    const char *op = node->op;

    if (!op) {
        type_free(left);
        type_free(right);
        return type_create(TYPE_UNKNOWN);
    }

    /* ------- Division checks (ARI-S001) ------- */
    if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
        /* Constant zero divisor */
        if (is_zero_literal(node->children[1])) {
            emit_error(a, ARI_LEVEL_SILENT, ARI_S001_CODE, ARI_S001_FIX,
                       node->line, node->col,
                       "Division by constant zero");
        }
        /* Variable divisor without guard -- warn but don't block.
         * Constant zero is SILENT (blocks), variable is WARNING. */
        else if (node->children[1]->type == NODE_IDENTIFIER) {
            emit_error(a, ARI_LEVEL_WARNING, ARI_W004_CODE, ARI_S001_FIX,
                       node->line, node->col,
                       "Division by '%s' without zero-check guard",
                       node->children[1]->string_val);
        }
    }

    /* ------- Comparison operators return bool ------- */
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0  || strcmp(op, ">") == 0  ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
        /* Operands must be compatible */
        if (!types_compatible(left, right)) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Cannot compare '%s' with '%s'",
                       type_to_string(left), type_to_string(right));
        }
        type_free(left);
        type_free(right);
        return type_create(TYPE_BOOL);
    }

    /* ------- Boolean operators ------- */
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        if (left->kind != TYPE_BOOL) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Left operand of '%s' must be bool, got '%s'",
                       op, type_to_string(left));
        }
        if (right->kind != TYPE_BOOL) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Right operand of '%s' must be bool, got '%s'",
                       op, type_to_string(right));
        }
        type_free(left);
        type_free(right);
        return type_create(TYPE_BOOL);
    }

    /* ------- Arithmetic operators ------- */
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
        strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
        strcmp(op, "%") == 0) {

        /* String concatenation: str + str */
        if (strcmp(op, "+") == 0 &&
            left->kind == TYPE_STR && right->kind == TYPE_STR) {
            type_free(left);
            type_free(right);
            return type_create(TYPE_STR);
        }

        /* Both must be numeric */
        if (!type_is_numeric(left) || !type_is_numeric(right)) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Arithmetic operator '%s' requires numeric operands, "
                       "got '%s' and '%s'",
                       op, type_to_string(left), type_to_string(right));
            type_free(left);
            type_free(right);
            return type_create(TYPE_UNKNOWN);
        }

        /* Result is the wider type */
        AriType *result;
        if (type_bit_width(left) >= type_bit_width(right)) {
            result = type_clone(left);
        } else {
            result = type_clone(right);
        }
        type_free(left);
        type_free(right);
        return result;
    }

    type_free(left);
    type_free(right);
    return type_create(TYPE_UNKNOWN);
}

static AriType *analyze_unary_op(Analyzer *a, ASTNode *node) {
    if (node->child_count < 1) return type_create(TYPE_UNKNOWN);

    AriType *operand = analyze_expr(a, node->children[0]);
    const char *op = node->op;

    if (op && strcmp(op, "!") == 0) {
        if (operand->kind != TYPE_BOOL) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Operator '!' requires bool operand, got '%s'",
                       type_to_string(operand));
        }
        type_free(operand);
        return type_create(TYPE_BOOL);
    }

    if (op && strcmp(op, "-") == 0) {
        if (!type_is_numeric(operand)) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Unary '-' requires numeric operand, got '%s'",
                       type_to_string(operand));
        }
        return operand;  /* same type */
    }

    return operand;
}

/*
 * Builtin return type lookup.
 * Returns the known return type for built-in functions,
 * or NULL if the function is not a builtin.
 */
static AriType *builtin_return_type(const char *name) {
    /* i32 return builtins */
    if (strcmp(name, "arr_new") == 0 ||
        strcmp(name, "arr_get") == 0 ||
        strcmp(name, "arr_set") == 0 ||
        strcmp(name, "arr_len") == 0 ||
        strcmp(name, "str_new") == 0 ||
        strcmp(name, "str_len") == 0 ||
        strcmp(name, "str_eq") == 0 ||
        strcmp(name, "str_char_at") == 0 ||
        strcmp(name, "str_concat") == 0 ||
        strcmp(name, "read_int") == 0 ||
        strcmp(name, "float_to_int") == 0 ||
        strcmp(name, "ip4") == 0 ||
        strcmp(name, "socket_create") == 0 ||
        strcmp(name, "socket_connect") == 0 ||
        strcmp(name, "socket_send") == 0 ||
        strcmp(name, "socket_recv") == 0 ||
        strcmp(name, "socket_close") == 0 ||
        strcmp(name, "socket_bind") == 0 ||
        strcmp(name, "socket_listen") == 0 ||
        strcmp(name, "socket_accept") == 0 ||
        strcmp(name, "mem_free") == 0 ||
        strcmp(name, "file_open") == 0 ||
        strcmp(name, "file_read") == 0 ||
        strcmp(name, "file_write") == 0 ||
        strcmp(name, "file_close") == 0 ||
        strcmp(name, "socket_opt") == 0 ||
        strcmp(name, "buf_stack") == 0 ||
        strcmp(name, "epoll_create") == 0 ||
        strcmp(name, "epoll_add") == 0 ||
        strcmp(name, "epoll_del") == 0 ||
        strcmp(name, "epoll_wait") == 0)
        return type_create(TYPE_I32);
    /* f64 return builtins */
    if (strcmp(name, "read_float") == 0 ||
        strcmp(name, "int_to_float") == 0)
        return type_create(TYPE_F64);
    /* void return builtins (side-effect only) */
    if (strcmp(name, "print_str") == 0 ||
        strcmp(name, "print_int") == 0 ||
        strcmp(name, "print_float") == 0 ||
        strcmp(name, "print_dec") == 0 ||
        strcmp(name, "str_println") == 0)
        return type_create(TYPE_VOID);
    /* dec() returns a decimal compile-time type — treat as i32 for type checking */
    if (strcmp(name, "dec") == 0)
        return type_create(TYPE_I32);
    return NULL;
}

static AriType *analyze_call(Analyzer *a, ASTNode *node) {
    if (node->child_count < 1) return type_create(TYPE_UNKNOWN);

    /* First child is the function expression (usually an identifier) */
    ASTNode *fn_expr = node->children[0];
    const char *fn_name = fn_expr->string_val;

    if (fn_expr->type == NODE_IDENTIFIER && fn_name) {
        /* Check builtins first — these are not in the symbol table */
        AriType *bi_ret = builtin_return_type(fn_name);
        if (bi_ret) {
            /* Analyze arguments but don't type-check params for builtins */
            for (size_t i = 1; i < node->child_count; i++)
                type_free(analyze_expr(a, node->children[i]));
            return bi_ret;
        }

        Symbol *sym = symtab_lookup(a->symbols, fn_name);
        if (!sym) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Undefined function '%s'", fn_name);
            /* Still analyze arguments */
            for (size_t i = 1; i < node->child_count; i++)
                type_free(analyze_expr(a, node->children[i]));
            return type_create(TYPE_UNKNOWN);
        }

        sym->is_used = true;

        if (sym->type && sym->type->kind == TYPE_FUNCTION) {
            /* Check argument count */
            size_t expected = sym->type->param_count;
            size_t actual   = node->child_count - 1;
            if (actual != expected) {
                emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                           node->line, node->col,
                           "Function '%s' expects %zu arguments, got %zu",
                           fn_name, expected, actual);
            }

            /* Check argument types */
            for (size_t i = 1; i < node->child_count; i++) {
                AriType *arg_type = analyze_expr(a, node->children[i]);
                if (i - 1 < expected) {
                    AriType *param_type = sym->type->param_types[i - 1];
                    if (!types_can_assign(param_type, arg_type)) {
                        if (types_loses_data(param_type, arg_type)) {
                            emit_error(a, ARI_LEVEL_SILENT, ARI_S006_CODE,
                                       ARI_S006_FIX, node->line, node->col,
                                       "Argument %zu of '%s': implicit conversion "
                                       "from '%s' to '%s' loses data",
                                       i, fn_name,
                                       type_to_string(arg_type),
                                       type_to_string(param_type));
                        } else {
                            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE,
                                       ARI_L006_FIX, node->line, node->col,
                                       "Argument %zu of '%s': expected '%s', "
                                       "got '%s'",
                                       i, fn_name,
                                       type_to_string(param_type),
                                       type_to_string(arg_type));
                        }
                    }
                }
                type_free(arg_type);
            }

            /* Return the function's return type */
            if (sym->type->return_type)
                return type_clone(sym->type->return_type);
        }

        /* Non-function call */
        if (sym->type && sym->type->kind != TYPE_FUNCTION && !sym->is_function) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "'%s' is not a function", fn_name);
        }

        return type_create(TYPE_UNKNOWN);
    }

    /* Member access calls (e.g., log.error) -- analyze but don't type-check deeply */
    for (size_t i = 1; i < node->child_count; i++)
        type_free(analyze_expr(a, node->children[i]));

    return type_create(TYPE_UNKNOWN);
}

static AriType *analyze_expr(Analyzer *a, ASTNode *node) {
    if (!node) return type_create(TYPE_UNKNOWN);

    switch (node->type) {
    case NODE_INT_LITERAL:
        /* Integer literals are polymorphic: they fit any integer type.
         * Default to i32 (the common integer type).  Widening to i64
         * is implicit and safe; narrowing is caught by the type checker
         * when assigning to a typed variable. */
        return type_create(TYPE_I32);

    case NODE_FLOAT_LITERAL:
        return type_create(TYPE_F64);

    case NODE_STRING_LITERAL:
        return type_create(TYPE_STR);

    case NODE_BOOL_LITERAL:
        return type_create(TYPE_BOOL);

    case NODE_NONE:
        return type_create_option(type_create(TYPE_UNKNOWN));

    case NODE_SOME:
        if (node->child_count > 0) {
            AriType *inner = analyze_expr(a, node->children[0]);
            return type_create_option(inner);
        }
        return type_create_option(type_create(TYPE_UNKNOWN));

    case NODE_ARRAY_LITERAL: {
        AriType *elem = type_create(TYPE_UNKNOWN);
        for (size_t i = 0; i < node->child_count; i++) {
            AriType *et = analyze_expr(a, node->children[i]);
            if (i == 0 && elem->kind == TYPE_UNKNOWN) {
                type_free(elem);
                elem = type_clone(et);
            }
            type_free(et);
        }
        return type_create_array(elem);
    }

    case NODE_IDENTIFIER: {
        const char *name = node->string_val;
        if (!name) return type_create(TYPE_UNKNOWN);

        Symbol *sym = symtab_lookup(a->symbols, name);
        if (!sym) {
            emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                       node->line, node->col,
                       "Undefined variable '%s'", name);
            return type_create(TYPE_UNKNOWN);
        }
        sym->is_used = true;

        if (sym->type)
            return type_clone(sym->type);
        return type_create(TYPE_UNKNOWN);
    }

    case NODE_BINARY_OP:
        return analyze_binary_op(a, node);

    case NODE_UNARY_OP:
        return analyze_unary_op(a, node);

    case NODE_CALL:
        return analyze_call(a, node);

    case NODE_MEMBER_ACCESS: {
        /* Analyze the object part */
        if (node->child_count > 0)
            type_free(analyze_expr(a, node->children[0]));
        return type_create(TYPE_UNKNOWN);
    }

    default:
        /* For other node types, analyze children */
        for (size_t i = 0; i < node->child_count; i++)
            type_free(analyze_expr(a, node->children[i]));
        return type_create(TYPE_UNKNOWN);
    }
}

/* ================================================================== */
/*  Statement analysis                                                */
/* ================================================================== */

/*
 * Analyze a variable declaration (let or const).
 * AST structure for NODE_VAR_DECL / NODE_CONST_DECL:
 *   string_val = variable name
 *   children[0] = type annotation (NODE_TYPE_ANNOTATION) - optional
 *   children[1] = initializer expression - optional
 */
static void analyze_var_decl(Analyzer *a, ASTNode *node) {
    bool is_const = (node->type == NODE_CONST_DECL);
    const char *name = node->string_val;
    if (!name) return;

    /* Check for duplicate in current scope */
    Symbol *dup = symtab_lookup_current(a->symbols, name);
    if (dup) {
        emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                   node->line, node->col,
                   "Duplicate variable declaration '%s' "
                   "(first declared at line %d)",
                   name, dup->line);
        return;
    }

    /* Check for shadowing */
    Symbol *outer = symtab_lookup(a->symbols, name);
    if (outer) {
        emit_error(a, ARI_LEVEL_WARNING, ARI_W003_CODE, ARI_W003_FIX,
                   node->line, node->col,
                   "Variable '%s' shadows declaration at line %d",
                   name, outer->line);
    }

    /* Resolve type */
    AriType *decl_type = NULL;
    if (node->child_count > 0 && node->children[0] &&
        node->children[0]->type == NODE_TYPE_ANNOTATION) {
        decl_type = resolve_type_annotation(a, node->children[0]);
    }

    /* Analyze initializer and check type compatibility */
    bool has_init = false;
    if (node->child_count > 1 && node->children[1]) {
        has_init = true;
        AriType *init_type = analyze_expr(a, node->children[1]);

        if (decl_type) {
            /* Check for data-loss conversion (ARI-S006) */
            if (types_loses_data(decl_type, init_type)) {
                emit_error(a, ARI_LEVEL_SILENT, ARI_S006_CODE, ARI_S006_FIX,
                           node->line, node->col,
                           "Implicit conversion from '%s' to '%s' loses data "
                           "in initialization of '%s'",
                           type_to_string(init_type),
                           type_to_string(decl_type), name);
            }
            /* Check type mismatch */
            else if (!types_can_assign(decl_type, init_type)) {
                emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                           node->line, node->col,
                           "Cannot assign '%s' to variable '%s' of type '%s'",
                           type_to_string(init_type), name,
                           type_to_string(decl_type));
            }
        } else {
            /* Infer type from initializer */
            decl_type = type_clone(init_type);
        }
        type_free(init_type);
    }

    if (!decl_type) {
        decl_type = type_create(TYPE_UNKNOWN);
    }

    /* Register in symbol table */
    symtab_define(a->symbols, name, decl_type, is_const, has_init,
                  node->line, node->col);
}

/*
 * Analyze a function declaration.
 * AST structure for NODE_FN_DECL:
 *   string_val = function name
 *   children[0..n-3] = parameter nodes (NODE_VAR_DECL with type annotations)
 *   children[n-2]    = return type annotation (NODE_TYPE_ANNOTATION)
 *   children[n-1]    = function body (NODE_BLOCK)
 *
 * We use a convention: parameters come first, then return type, then body.
 * The parser stores parameters, return type, and body as children.
 */
static void analyze_fn_decl(Analyzer *a, ASTNode *node) {
    const char *name = node->string_val;
    if (!name) return;

    /* We need at least a body */
    if (node->child_count < 1) return;

    /* Parser AST structure for fn_decl:
     *   children[0]   = params wrapper node (contains param VAR_DECLs as its children)
     *   children[1]   = return type (NODE_TYPE_ANNOTATION) — optional
     *   children[last] = body (NODE_BLOCK)
     */
    size_t body_idx = node->child_count - 1;
    ASTNode *body = node->children[body_idx];

    /* Find return type annotation */
    AriType *ret_type = type_create(TYPE_VOID);
    if (body_idx > 0 && node->children[body_idx - 1] &&
        node->children[body_idx - 1]->type == NODE_TYPE_ANNOTATION) {
        type_free(ret_type);
        ret_type = resolve_type_annotation(a, node->children[body_idx - 1]);
    }

    /* Extract parameters from the params wrapper node (children[0]) */
    ASTNode *params_node = (node->child_count > 1) ? node->children[0] : NULL;
    size_t param_count = 0;
    AriType **param_types = NULL;

    if (params_node && params_node->child_count > 0) {
        param_count = params_node->child_count;
        param_types = malloc(param_count * sizeof(AriType *));
        for (size_t i = 0; i < param_count; i++) {
            ASTNode *param = params_node->children[i];
            if (param && param->child_count > 0 &&
                param->children[0]->type == NODE_TYPE_ANNOTATION) {
                param_types[i] = resolve_type_annotation(a, param->children[0]);
            } else {
                param_types[i] = type_create(TYPE_UNKNOWN);
            }
        }
    }

    /* Create function type and register in symbol table */
    AriType *fn_type = type_create_function(param_types, param_count,
                                            type_clone(ret_type));

    /* Check for duplicate at current scope */
    Symbol *dup = symtab_lookup_current(a->symbols, name);
    if (dup) {
        emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                   node->line, node->col,
                   "Duplicate function declaration '%s' "
                   "(first declared at line %d)",
                   name, dup->line);
        type_free(fn_type);
        type_free(ret_type);
        return;
    }

    symtab_define_function(a->symbols, name, fn_type, node->line, node->col);

    /* Analyze function body in a new scope */
    AriType *prev_ret = a->current_fn_return_type;
    bool prev_in_fn = a->in_function;
    bool prev_after_return = a->after_return;

    a->current_fn_return_type = ret_type;
    a->in_function = true;
    a->after_return = false;

    symtab_push_scope(a->symbols);

    /* Define parameters in the function scope */
    if (params_node) {
        for (size_t i = 0; i < param_count; i++) {
            ASTNode *param = params_node->children[i];
            if (param && param->string_val) {
                AriType *pt = type_clone(fn_type->param_types[i]);
                symtab_define(a->symbols, param->string_val, pt,
                              false, true, param->line, param->col);
            }
        }
    }

    /* Analyze body */
    if (body && body->type == NODE_BLOCK) {
        analyze_block(a, body, false);  /* don't push another scope */
    }

    /* Check for unused variables in function scope */
    Scope *fn_scope = symtab_pop_scope(a->symbols);
    if (fn_scope) {
        for (size_t i = 0; i < fn_scope->count; i++) {
            Symbol *sym = &fn_scope->symbols[i];
            if (!sym->is_used && sym->name[0] != '_') {
                emit_error(a, ARI_LEVEL_WARNING, ARI_W001_CODE, ARI_W001_FIX,
                           sym->line, sym->col,
                           "Unused variable '%s'", sym->name);
            }
        }
        scope_free(fn_scope);
    }

    /* Restore state */
    a->current_fn_return_type = prev_ret;
    a->in_function = prev_in_fn;
    a->after_return = prev_after_return;
    type_free(ret_type);
}

/*
 * Analyze a return statement.
 * children[0] = return expression (optional)
 */
static void analyze_return(Analyzer *a, ASTNode *node) {
    if (!a->in_function) {
        emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                   node->line, node->col,
                   "'return' outside of function");
        return;
    }

    AriType *ret_type = NULL;
    if (node->child_count > 0 && node->children[0]) {
        ret_type = analyze_expr(a, node->children[0]);
    } else {
        ret_type = type_create(TYPE_VOID);
    }

    /* Check return type matches function signature */
    if (a->current_fn_return_type) {
        if (!types_can_assign(a->current_fn_return_type, ret_type)) {
            if (types_loses_data(a->current_fn_return_type, ret_type)) {
                emit_error(a, ARI_LEVEL_SILENT, ARI_S006_CODE, ARI_S006_FIX,
                           node->line, node->col,
                           "Return type '%s' implicitly converts to '%s' "
                           "with data loss",
                           type_to_string(ret_type),
                           type_to_string(a->current_fn_return_type));
            } else {
                emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                           node->line, node->col,
                           "Return type mismatch: function expects '%s', "
                           "got '%s'",
                           type_to_string(a->current_fn_return_type),
                           type_to_string(ret_type));
            }
        }
    }

    type_free(ret_type);
    a->after_return = true;
}

/*
 * Analyze an if statement.
 * children[0] = condition
 * children[1] = then-block
 * children[2] = else-block (optional)
 */
static void analyze_if(Analyzer *a, ASTNode *node) {
    if (node->child_count < 2) return;

    /* Analyze condition */
    AriType *cond = analyze_expr(a, node->children[0]);

    /* Condition should be bool */
    if (cond->kind != TYPE_BOOL && cond->kind != TYPE_UNKNOWN) {
        emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                   node->children[0]->line, node->children[0]->col,
                   "If condition must be bool, got '%s'",
                   type_to_string(cond));
    }

    /* Check for always-true/false conditions (ARI-W004) */
    if (node->children[0]->type == NODE_BOOL_LITERAL) {
        emit_error(a, ARI_LEVEL_WARNING, ARI_W004_CODE, ARI_W004_FIX,
                   node->children[0]->line, node->children[0]->col,
                   "Condition is always %s",
                   node->children[0]->bool_val ? "true" : "false");
    }

    type_free(cond);

    /* Analyze then-block */
    bool saved_after_return = a->after_return;
    a->after_return = false;
    if (node->children[1]->type == NODE_BLOCK) {
        analyze_block(a, node->children[1], true);
    } else {
        analyze_stmt(a, node->children[1]);
    }

    /* Analyze else-block if present */
    a->after_return = false;
    if (node->child_count > 2 && node->children[2]) {
        if (node->children[2]->type == NODE_BLOCK) {
            analyze_block(a, node->children[2], true);
        } else {
            analyze_stmt(a, node->children[2]);
        }
    }

    a->after_return = saved_after_return;
}

/*
 * Analyze a match statement.
 * children[0] = expression being matched
 * children[1..n] = match arms (NODE_MATCH_ARM)
 */
static void analyze_match(Analyzer *a, ASTNode *node) {
    if (node->child_count < 1) return;

    AriType *match_type = analyze_expr(a, node->children[0]);

    /* Check if matching on Option type - must have Some and None arms */
    if (match_type->kind == TYPE_OPTION) {
        bool has_some = false;
        bool has_none = false;
        for (size_t i = 1; i < node->child_count; i++) {
            ASTNode *arm = node->children[i];
            if (arm && arm->type == NODE_MATCH_ARM && arm->child_count > 0) {
                ASTNode *pattern = arm->children[0];
                if (pattern->type == NODE_SOME) has_some = true;
                if (pattern->type == NODE_NONE) has_none = true;
            }
        }
        if (!has_some || !has_none) {
            emit_error(a, ARI_LEVEL_SILENT, ARI_S002_CODE, ARI_S002_FIX,
                       node->line, node->col,
                       "Non-exhaustive match on Option: must handle both "
                       "Some and None cases");
        }
    }

    /* Analyze each arm */
    for (size_t i = 1; i < node->child_count; i++) {
        ASTNode *arm = node->children[i];
        if (arm && arm->type == NODE_MATCH_ARM) {
            for (size_t j = 0; j < arm->child_count; j++)
                type_free(analyze_expr(a, arm->children[j]));
        }
    }

    type_free(match_type);
}

/*
 * Analyze a try-catch block.
 * children[0] = try block
 * children[1] = catch block
 * op or string_val of catch = catch variable name
 */
static void analyze_try_catch(Analyzer *a, ASTNode *node) {
    if (node->child_count < 2) return;

    /* Analyze try block */
    if (node->children[0]->type == NODE_BLOCK) {
        analyze_block(a, node->children[0], true);
    }

    /* Check for empty catch block (ARI-S004) */
    ASTNode *catch_block = node->children[1];
    if (catch_block->type == NODE_BLOCK) {
        if (catch_block->child_count == 0) {
            emit_error(a, ARI_LEVEL_SILENT, ARI_S004_CODE, ARI_S004_FIX,
                       catch_block->line, catch_block->col,
                       "Empty catch block swallows errors silently");
        }
        /* Analyze catch block in a new scope with the error variable */
        symtab_push_scope(a->symbols);

        /* Define the catch variable if present */
        const char *catch_var = node->op ? node->op : node->string_val;
        if (catch_var) {
            symtab_define(a->symbols, catch_var,
                          type_create(TYPE_ERROR),
                          false, true, node->line, node->col);
        }

        for (size_t i = 0; i < catch_block->child_count; i++) {
            analyze_stmt(a, catch_block->children[i]);
        }

        Scope *catch_scope = symtab_pop_scope(a->symbols);
        if (catch_scope) {
            /* Check if the error variable was used */
            for (size_t i = 0; i < catch_scope->count; i++) {
                if (!catch_scope->symbols[i].is_used &&
                    catch_scope->symbols[i].name[0] != '_') {
                    /* Catch variable not used is suspicious but not necessarily
                     * an empty catch -- only flag if the catch body exists but
                     * doesn't use the error variable */
                }
            }
            scope_free(catch_scope);
        }
    }
}

/*
 * Analyze an expression statement.
 * An expression whose result is not used.
 * If the expression is a function call returning Result/Option,
 * flag it (ARI-S005).
 */
static void analyze_expr_stmt(Analyzer *a, ASTNode *node) {
    if (node->child_count < 1) return;

    ASTNode *expr = node->children[0];
    AriType *t = analyze_expr(a, expr);

    /* Check for ignored return value of function that can fail (ARI-S005) */
    if (expr->type == NODE_CALL && t->kind == TYPE_OPTION) {
        emit_error(a, ARI_LEVEL_SILENT, ARI_S005_CODE, ARI_S005_FIX,
                   expr->line, expr->col,
                   "Return value of function call is ignored "
                   "(function returns Option/Result)");
    }

    type_free(t);
}

/*
 * Analyze a for loop.
 * children[0] = init (var decl or expr)
 * children[1] = condition
 * children[2] = update
 * children[3] = body
 */
static void analyze_for(Analyzer *a, ASTNode *node) {
    symtab_push_scope(a->symbols);

    for (size_t i = 0; i < node->child_count; i++) {
        if (node->children[i]) {
            if (node->children[i]->type == NODE_BLOCK) {
                analyze_block(a, node->children[i], false);
            } else {
                analyze_stmt(a, node->children[i]);
            }
        }
    }

    Scope *loop_scope = symtab_pop_scope(a->symbols);
    if (loop_scope) scope_free(loop_scope);
}

/*
 * Analyze a while loop.
 * children[0] = condition
 * children[1] = body
 */
static void analyze_while(Analyzer *a, ASTNode *node) {
    if (node->child_count < 2) return;

    AriType *cond = analyze_expr(a, node->children[0]);
    if (cond->kind != TYPE_BOOL && cond->kind != TYPE_UNKNOWN) {
        emit_error(a, ARI_LEVEL_LOGIC, ARI_L006_CODE, ARI_L006_FIX,
                   node->children[0]->line, node->children[0]->col,
                   "While condition must be bool, got '%s'",
                   type_to_string(cond));
    }
    type_free(cond);

    if (node->children[1]->type == NODE_BLOCK) {
        analyze_block(a, node->children[1], true);
    }
}

/* ================================================================== */
/*  Statement dispatcher                                              */
/* ================================================================== */

static void analyze_stmt(Analyzer *a, ASTNode *node) {
    if (!node) return;

    /* Check for unreachable code (ARI-W002) */
    if (a->after_return) {
        emit_error(a, ARI_LEVEL_WARNING, ARI_W002_CODE, ARI_W002_FIX,
                   node->line, node->col,
                   "Unreachable code after return statement");
        a->after_return = false;  /* only warn once */
    }

    switch (node->type) {
    case NODE_VAR_DECL:
    case NODE_CONST_DECL:
        analyze_var_decl(a, node);
        break;

    case NODE_FN_DECL:
        analyze_fn_decl(a, node);
        break;

    case NODE_RETURN:
        analyze_return(a, node);
        break;

    case NODE_IF:
        analyze_if(a, node);
        break;

    case NODE_MATCH:
        analyze_match(a, node);
        break;

    case NODE_TRY_CATCH:
        analyze_try_catch(a, node);
        break;

    case NODE_FOR:
        analyze_for(a, node);
        break;

    case NODE_WHILE:
        analyze_while(a, node);
        break;

    case NODE_BLOCK:
        analyze_block(a, node, true);
        break;

    case NODE_EXPR_STMT:
        analyze_expr_stmt(a, node);
        break;

    case NODE_ERROR_RAISE:
        /* Analyze the error expression */
        for (size_t i = 0; i < node->child_count; i++)
            type_free(analyze_expr(a, node->children[i]));
        break;

    default:
        /* Expression in statement position */
        type_free(analyze_expr(a, node));
        break;
    }
}

/* ================================================================== */
/*  Block analysis                                                    */
/* ================================================================== */

static void analyze_block(Analyzer *a, ASTNode *node, bool new_scope) {
    if (!node) return;

    if (new_scope) symtab_push_scope(a->symbols);

    bool saved_after_return = a->after_return;
    a->after_return = false;

    for (size_t i = 0; i < node->child_count; i++) {
        analyze_stmt(a, node->children[i]);
    }

    if (new_scope) {
        Scope *scope = symtab_pop_scope(a->symbols);
        if (scope) {
            for (size_t i = 0; i < scope->count; i++) {
                Symbol *sym = &scope->symbols[i];
                if (!sym->is_used && !sym->is_function && sym->name[0] != '_') {
                    emit_error(a, ARI_LEVEL_WARNING, ARI_W001_CODE,
                               ARI_W001_FIX, sym->line, sym->col,
                               "Unused variable '%s'", sym->name);
                }
            }
            scope_free(scope);
        }
    }

    if (!a->after_return)
        a->after_return = saved_after_return;
}

/* ================================================================== */
/*  Public API                                                        */
/* ================================================================== */

void analyzer_init(Analyzer *a, ASTNode *root) {
    memset(a, 0, sizeof(Analyzer));
    a->root    = root;
    a->symbols = symtab_create();
}

bool analyzer_analyze(Analyzer *a) {
    if (!a->root) return true;

    /* For a program node, analyze all top-level statements */
    if (a->root->type == NODE_PROGRAM) {
        for (size_t i = 0; i < a->root->child_count; i++) {
            analyze_stmt(a, a->root->children[i]);
        }
    } else {
        analyze_stmt(a, a->root);
    }

    /* Check for unused variables in global scope */
    /* (We don't pop the global scope, so check directly) */
    Scope *global = a->symbols->current;
    if (global) {
        for (size_t i = 0; i < global->count; i++) {
            Symbol *sym = &global->symbols[i];
            if (!sym->is_used && !sym->is_function && sym->name[0] != '_') {
                emit_error(a, ARI_LEVEL_WARNING, ARI_W001_CODE,
                           ARI_W001_FIX, sym->line, sym->col,
                           "Unused variable '%s'", sym->name);
            }
        }
    }

    return !analyzer_has_blocking_errors(a);
}

bool analyzer_has_blocking_errors(const Analyzer *a) {
    return a->level0_count > 0 || a->level1_count > 0;
}

bool analyzer_has_errors(const Analyzer *a) {
    return a->error_count > 0;
}

void analyzer_print_errors(const Analyzer *a) {
    for (size_t i = 0; i < a->error_count; i++) {
        const SemanticError *e = &a->errors[i];
        const char *label = ari_level_label(e->level);
        const char *level_name = ari_level_name(e->level);

        fprintf(stderr, "\n");
        fprintf(stderr, "  [%s] %s (%s)\n", e->code, label, level_name);
        fprintf(stderr, "  --> line %d, col %d\n", e->line, e->col);
        fprintf(stderr, "  %s\n", e->message);
        if (e->suggestion) {
            fprintf(stderr, "  fix: %s\n", e->suggestion);
        }
    }

    if (a->error_count > 0) {
        fprintf(stderr, "\n  Summary: %zu error(s)\n", a->error_count);
        if (a->level0_count > 0)
            fprintf(stderr, "    Level 0 (FORBIDDEN):     %zu -- compilation BLOCKED\n",
                    a->level0_count);
        if (a->level1_count > 0)
            fprintf(stderr, "    Level 1 (INADMISSIBLE):  %zu -- compilation BLOCKED\n",
                    a->level1_count);
        if (a->level2_count > 0)
            fprintf(stderr, "    Level 2 (SUSPICIOUS):    %zu -- warnings\n",
                    a->level2_count);
        fprintf(stderr, "\n");
    }
}

void analyzer_destroy(Analyzer *a) {
    if (!a) return;
    if (a->symbols) {
        symtab_destroy(a->symbols);
        a->symbols = NULL;
    }
}
