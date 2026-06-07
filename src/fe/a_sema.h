/* a_sema.h -- Burroughs Extended ALGOL semantic analysis.
 *
 * Walks the a_ast.h tree and: pushes a scope per block and per procedure
 * (Section 5, "every block introduces a new level of nomenclature"),
 * collects declarations, resolves every identifier to its declaration,
 * types every expression, and flags references that reach across a
 * procedure boundary as non-local -- the hook the lowerer's display
 * storage uses to give such variables activation-record slots.
 *
 * Scope model: cur_level is procedure-nesting depth. Blocks share their
 * enclosing procedure's frame (a new name scope but the same level);
 * procedures introduce a new frame (level + 1). A use whose resolved
 * symbol sits at a lower level than the use is therefore non-local. */
#ifndef BEAC_A_SEMA_H
#define BEAC_A_SEMA_H

#include "a_token.h"
#include "a_ast.h"

#define A_SEMA_MAX_SYM   2048
#define A_SEMA_MAX_SCOPE 64
#define A_SEMA_MAX_NODE  16384
#define A_SEMA_MAX_ERRS  64

typedef enum {
    AT_NONE = 0, AT_INTEGER, AT_REAL, AT_BOOLEAN, AT_ALPHA,
    AT_LABEL, AT_PROC
} a_type_t;

typedef enum { SK_VAR, SK_ARRAY, SK_PROC, SK_LABEL } a_symkind_t;

typedef struct {
    uint32_t name_tok;   /* token of the declared name              */
    uint32_t decl_node;  /* declaring AST node                      */
    uint8_t  kind;       /* a_symkind_t                             */
    uint8_t  type;       /* a_type_t: element (array) / return (proc) */
    uint8_t  by_value;   /* formal parameter: by-value else by-name */
    uint8_t  is_param;   /* is a formal parameter                   */
    uint16_t level;      /* procedure-nesting level where declared  */
    uint16_t pad;
} a_sym_t;

typedef struct {
    uint32_t first_sym;
    uint32_t n_sym;
    uint16_t level;
} a_scope_t;

typedef struct {
    uint32_t line;
    uint16_t col;
    char     msg[96];
} a_sema_err_t;

typedef struct {
    const a_node_t  *nodes;
    uint32_t         n_nodes;
    const a_token_t *toks;
    const char      *src;

    a_sym_t   syms[A_SEMA_MAX_SYM];
    uint32_t  n_syms;
    a_scope_t scopes[A_SEMA_MAX_SCOPE];
    int       n_scopes;
    uint16_t  cur_level;

    uint8_t   ntype[A_SEMA_MAX_NODE];     /* a_type_t per node               */
    uint32_t  nsym[A_SEMA_MAX_NODE];      /* resolved sym index + 1 (0 = none) */
    uint8_t   nonlocal[A_SEMA_MAX_NODE];  /* 1 = reference crosses a proc bdry */
    uint8_t   escapes[A_SEMA_MAX_NODE];   /* keyed by DECL node: read from a
                                           * nested proc, so it can't just live
                                           * in a backend frame -- this is what
                                           * tells the lowerer to give it
                                           * display-reachable storage. */

    a_sema_err_t errors[A_SEMA_MAX_ERRS];
    int       n_errs;
} a_sema_ctx_t;

void a_sema_init(a_sema_ctx_t *S, const a_node_t *nodes, uint32_t n_nodes,
                 const a_token_t *toks, const char *src);
int  a_sema_run(a_sema_ctx_t *S, uint32_t root);   /* 0 ok, -1 on errors */

#endif /* BEAC_A_SEMA_H */
