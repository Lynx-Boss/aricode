/*
 * aricode - Import Resolution
 * Handles `import "file.ari";` and `import "file.ari" as ns;`
 * with automatic dependency resolution and namespace rewriting.
 */

#ifndef ARICODE_IMPORTS_H
#define ARICODE_IMPORTS_H

/*
 * Resolve all import statements in source code.
 * Reads imported files, handles nested imports, applies namespace
 * prefixes for `as` imports, and rewrites `ns.func()` calls.
 *
 * Returns a new malloc'd string with all imports resolved and
 * concatenated. Caller must free() the result.
 *
 * base_path: path of the source file (used to resolve relative imports).
 */
char *resolve_imports(const char *source, const char *base_path);

#endif /* ARICODE_IMPORTS_H */
