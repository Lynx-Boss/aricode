/*
 * aricode - Ari Code Language
 * Builtin Function Code Generation
 *
 * Handles all builtin function dispatch (print, arrays, strings,
 * file I/O, networking, epoll, etc.) separated from the main codegen.
 */

#ifndef ARICODE_CODEGEN_BUILTINS_H
#define ARICODE_CODEGEN_BUILTINS_H

#include "codegen.h"

/*
 * Try to emit code for a builtin function call.
 * Returns 1 if the function was a recognized builtin (code emitted),
 * 0 if not a builtin (caller should emit a regular function call).
 */
int emit_builtin(CodegenState *cg, const ASTNode *node,
                 const char *name, size_t argc);

#endif /* ARICODE_CODEGEN_BUILTINS_H */
