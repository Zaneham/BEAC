/* a_sema.c -- Burroughs Extended ALGOL semantic analysis.
 *
 * Two passes per block: collect declarations and labels first (so forward
 * GO TO and procedures resolve), then walk the body resolving uses and
 * typing expressions. Procedures push a new frame (level + 1) with their
 * formals; blocks push a name scope at the same level. Standard functions
 * (SQRT, SIN, ...) are recognised by name so they don't read as undeclared. */

#include "a_sema.h"
#include <string.h>
#include <stdio.h>

/* ---- Plumbing ---- */

void a_sema_init(a_sema_ctx_t *S, const a_node_t *nodes, uint32_t n_nodes,
                 const a_token_t *toks, const char *src)
{
    memset(S, 0, sizeof(*S));
    S->nodes = nodes; S->n_nodes = n_nodes;
    S->toks = toks; S->src = src;
}

static const a_node_t *ND(const a_sema_ctx_t *S, uint32_t i)
{
    return &S->nodes[i];
}

static void serr(a_sema_ctx_t *S, uint32_t node, const char *msg)
{
    const a_token_t *t;
    a_sema_err_t *e;
    if (S->n_errs >= A_SEMA_MAX_ERRS) return;
    e = &S->errors[S->n_errs++];
    t = &S->toks[ND(S, node)->tok];
    e->line = t->line; e->col = t->col;
    snprintf(e->msg, sizeof(e->msg), "%s", msg);
}

static void set_type(a_sema_ctx_t *S, uint32_t node, int t)
{
    if (node < A_SEMA_MAX_NODE) S->ntype[node] = (uint8_t)t;
}

/* same identifier spelling? source is uppercase-only, so memcmp suffices */
static int tok_eq(const a_sema_ctx_t *S, uint32_t a, uint32_t b)
{
    const a_token_t *ta = &S->toks[a], *tb = &S->toks[b];
    if (ta->len != tb->len) return 0;
    return memcmp(S->src + ta->offset, S->src + tb->offset, ta->len) == 0;
}

static int tok_is(const a_sema_ctx_t *S, uint32_t tok, const char *s)
{
    const a_token_t *t = &S->toks[tok];
    size_t n = strlen(s);
    if (t->len != n) return 0;
    return memcmp(S->src + t->offset, s, n) == 0;
}

/* ---- Scopes & symbols ---- */

static void push_scope(a_sema_ctx_t *S, uint16_t level)
{
    a_scope_t *sc;
    if (S->n_scopes >= A_SEMA_MAX_SCOPE) return;
    sc = &S->scopes[S->n_scopes++];
    sc->first_sym = S->n_syms;
    sc->n_sym = 0;
    sc->level = level;
}

static void pop_scope(a_sema_ctx_t *S)
{
    if (S->n_scopes > 0) {
        S->n_syms = S->scopes[S->n_scopes - 1].first_sym;  /* reclaim symbols */
        S->n_scopes--;
    }
}

static uint32_t add_sym(a_sema_ctx_t *S, uint32_t name_tok, uint32_t decl_node,
                        int kind, int type, int by_value, int is_param)
{
    a_scope_t *sc;
    a_sym_t *sym;
    uint32_t i;
    if (S->n_scopes == 0 || S->n_syms >= A_SEMA_MAX_SYM) return 0;
    sc = &S->scopes[S->n_scopes - 1];
    for (i = sc->first_sym; i < S->n_syms; i++)        /* redeclaration check */
        if (tok_eq(S, S->syms[i].name_tok, name_tok)) {
            serr(S, decl_node, "identifier declared twice in one block");
            return i;
        }
    sym = &S->syms[S->n_syms];
    sym->name_tok = name_tok; sym->decl_node = decl_node;
    sym->kind = (uint8_t)kind; sym->type = (uint8_t)type;
    sym->by_value = (uint8_t)by_value; sym->is_param = (uint8_t)is_param;
    sym->level = sc->level;
    sc->n_sym++;
    return S->n_syms++;
}

/* resolve a name; returns sym index + 1 (0 = not found). Flags the use as
 * non-local when the symbol lives below the current procedure level. */
static uint32_t resolve(a_sema_ctx_t *S, uint32_t name_tok, uint32_t use_node)
{
    int s;
    for (s = S->n_scopes - 1; s >= 0; s--) {
        const a_scope_t *sc = &S->scopes[s];
        uint32_t i;
        for (i = sc->first_sym; i < sc->first_sym + sc->n_sym; i++)
            if (tok_eq(S, S->syms[i].name_tok, name_tok)) {
                if (S->syms[i].level < S->cur_level) {
                    if (use_node < A_SEMA_MAX_NODE) S->nonlocal[use_node] = 1;
                    /* the declaration outlives a single frame's reach: a
                     * deeper procedure reads it, so flag it for display
                     * storage rather than a plain stack slot. */
                    if (S->syms[i].decl_node < A_SEMA_MAX_NODE)
                        S->escapes[S->syms[i].decl_node] = 1;
                }
                return i + 1;
            }
    }
    return 0;
}

/* a handful of standard functions, recognised by name (Section 3) */
static int is_builtin(const a_sema_ctx_t *S, uint32_t tok)
{
    static const char *fn[] = {
        "SQRT", "SIN", "COS", "ABS", "EXP", "LN", "LOG", "ARCTAN",
        "ENTIER", "SIGN", "TIME"
    };
    int i;
    for (i = 0; i < (int)(sizeof(fn) / sizeof(fn[0])); i++)
        if (tok_is(S, tok, fn[i])) return 1;
    return 0;
}

static int map_type(int op)
{
    switch (op) {
    case A_INTEGER: return AT_INTEGER;
    case A_REAL:    return AT_REAL;
    case A_BOOLEAN: return AT_BOOLEAN;
    case A_ALPHA:   return AT_ALPHA;   /* treated as REAL in arithmetic */
    default:        return AT_NONE;
    }
}

/* ---- Forward decls ---- */

static void w_node(a_sema_ctx_t *S, uint32_t node);
static int  w_expr(a_sema_ctx_t *S, uint32_t node);

/* ---- Declaration collection (pass A) ---- */

static void collect_decl(a_sema_ctx_t *S, uint32_t c)
{
    const a_node_t *n = ND(S, c);
    uint32_t ch;
    int ty;
    switch (n->kind) {
    case AN_TYPE_DECL:
        ty = map_type(n->op);
        for (ch = n->first_child; ch; ch = ND(S, ch)->next_sibling)
            add_sym(S, ND(S, ch)->tok, ch, SK_VAR, ty, 0, 0);
        break;
    case AN_ARRAY_DECL:
        ty = map_type(n->op);
        for (ch = n->first_child; ch; ch = ND(S, ch)->next_sibling)
            if (ND(S, ch)->kind == AN_VAR)        /* names, not bound pairs */
                add_sym(S, ND(S, ch)->tok, ch, SK_ARRAY, ty, 0, 0);
        break;
    case AN_PROC_DECL:
        add_sym(S, n->tok, c, SK_PROC, map_type(n->op), 0, 0);
        break;
    default:
        break;
    }
}

static int is_decl(int kind)
{
    return kind == AN_TYPE_DECL || kind == AN_ARRAY_DECL || kind == AN_PROC_DECL;
}

/* labels in a block/compound's statement list are declared implicitly */
static void collect_labels(a_sema_ctx_t *S, uint32_t parent)
{
    uint32_t c;
    for (c = ND(S, parent)->first_child; c; c = ND(S, c)->next_sibling)
        if (ND(S, c)->kind == AN_LABELED)
            add_sym(S, ND(S, c)->tok, c, SK_LABEL, AT_LABEL, 0, 0);
}

/* ---- Expression typing (pass B) ---- */

static int is_relop(int op)
{
    return op == A_LSS || op == A_LEQ || op == A_EQL ||
           op == A_GEQ || op == A_GTR || op == A_NEQ;
}
static int is_logop(int op)
{
    return op == A_AND || op == A_OR || op == A_IMP || op == A_EQV;
}

static int sym_type(const a_sema_ctx_t *S, uint32_t sref)
{
    return sref ? S->syms[sref - 1].type : AT_NONE;
}

static int w_expr(a_sema_ctx_t *S, uint32_t node)
{
    const a_node_t *n = ND(S, node);
    int t = AT_NONE, lt, rt;
    uint32_t ch, sref;

    switch (n->kind) {
    case AN_NUM:
        t = (n->op == A_INTLIT) ? AT_INTEGER : AT_REAL;
        break;
    case AN_STR:
        t = AT_ALPHA;
        break;
    case AN_BOOL:
        t = AT_BOOLEAN;
        break;
    case AN_VAR:
        sref = resolve(S, n->tok, node);
        if (sref) { S->nsym[node] = sref; t = sym_type(S, sref); }
        else if (is_builtin(S, n->tok)) t = AT_REAL;
        else {
            char nm[64], msg[96];
            const a_token_t *tk = &S->toks[n->tok];
            uint16_t ln = tk->len < 63 ? tk->len : 63;
            memcpy(nm, S->src + tk->offset, ln); nm[ln] = '\0';
            snprintf(msg, sizeof(msg), "undeclared identifier '%s'", nm);
            serr(S, node, msg);
        }
        break;
    case AN_INDEX:
        ch = n->first_child;
        t = w_expr(S, ch);                       /* base (array) */
        for (ch = ND(S, ch)->next_sibling; ch; ch = ND(S, ch)->next_sibling)
            (void)w_expr(S, ch);                 /* subscripts */
        break;
    case AN_CALL:
        ch = n->first_child;
        t = w_expr(S, ch);                       /* callee -> return type */
        for (ch = ND(S, ch)->next_sibling; ch; ch = ND(S, ch)->next_sibling)
            (void)w_expr(S, ch);                 /* actual parameters */
        break;
    case AN_BINOP:
        ch = n->first_child;
        lt = w_expr(S, ch);
        rt = w_expr(S, ND(S, ch)->next_sibling);
        if (is_relop(n->op) || is_logop(n->op))      t = AT_BOOLEAN;
        else if (n->op == A_DIV || n->op == A_MOD)   t = AT_INTEGER;
        else if (n->op == A_SLASH)                   t = AT_REAL;
        else t = (lt == AT_REAL || rt == AT_REAL ||
                  lt == AT_ALPHA || rt == AT_ALPHA) ? AT_REAL : AT_INTEGER;
        break;
    case AN_UNOP:
        lt = w_expr(S, n->first_child);
        t = (n->op == A_NOT) ? AT_BOOLEAN : lt;
        break;
    case AN_COND_EXPR:
        ch = n->first_child;
        (void)w_expr(S, ch);                     /* condition */
        lt = w_expr(S, ND(S, ch)->next_sibling); /* then */
        rt = w_expr(S, ND(S, ND(S, ch)->next_sibling)->next_sibling); /* else */
        t = (lt == AT_REAL || rt == AT_REAL) ? AT_REAL : lt;
        break;
    case AN_PARTIAL:
        for (ch = n->first_child; ch; ch = ND(S, ch)->next_sibling)
            (void)w_expr(S, ch);
        t = AT_INTEGER;
        break;
    default:
        break;
    }
    set_type(S, node, t);
    return t;
}

/* ---- Statement / block walking (pass B) ---- */

static void w_children(a_sema_ctx_t *S, uint32_t node)
{
    uint32_t c;
    for (c = ND(S, node)->first_child; c; c = ND(S, c)->next_sibling)
        w_node(S, c);
}

static void w_block(a_sema_ctx_t *S, uint32_t node, int is_compound)
{
    uint32_t c;
    push_scope(S, S->cur_level);
    if (!is_compound)
        for (c = ND(S, node)->first_child; c; c = ND(S, c)->next_sibling)
            if (is_decl(ND(S, c)->kind)) collect_decl(S, c);
    collect_labels(S, node);
    w_children(S, node);
    pop_scope(S);
}

static void w_proc(a_sema_ctx_t *S, uint32_t node)
{
    const a_node_t *n = ND(S, node);
    uint32_t c, body = 0;
    S->cur_level++;
    push_scope(S, S->cur_level);
    for (c = n->first_child; c; c = ND(S, c)->next_sibling) {
        if (ND(S, c)->kind == AN_PARAM)
            add_sym(S, ND(S, c)->tok, c, SK_VAR,
                    map_type(ND(S, c)->op), ND(S, c)->aux & 1u, 1);
        else
            body = c;                            /* last non-param child */
    }
    if (body) w_node(S, body);
    pop_scope(S);
    S->cur_level--;
}

static void w_node(a_sema_ctx_t *S, uint32_t node)
{
    const a_node_t *n;
    uint32_t c;
    if (node == 0 || node >= S->n_nodes) return;
    n = ND(S, node);

    switch (n->kind) {
    case AN_BLOCK:      w_block(S, node, 0); break;
    case AN_COMPOUND:   w_block(S, node, 1); break;
    case AN_PROC_DECL:  w_proc(S, node); break;

    case AN_TYPE_DECL:  break;                   /* collected already */
    case AN_ARRAY_DECL:                          /* but bounds are exprs */
        for (c = n->first_child; c; c = ND(S, c)->next_sibling)
            if (ND(S, c)->kind == AN_BOUND_PAIR) w_children(S, c);
        break;

    case AN_ASSIGN:
    case AN_PROC_CALL:
    case AN_GOTO:
        for (c = n->first_child; c; c = ND(S, c)->next_sibling)
            (void)w_expr(S, c);
        break;

    case AN_IF_STMT:
        c = n->first_child;
        (void)w_expr(S, c);                      /* condition */
        for (c = ND(S, c)->next_sibling; c; c = ND(S, c)->next_sibling)
            w_node(S, c);                        /* then / else statements */
        break;

    case AN_FOR:
        c = n->first_child;
        (void)w_expr(S, c);                      /* controlled variable */
        for (c = ND(S, c)->next_sibling; c; c = ND(S, c)->next_sibling)
            w_node(S, c);                        /* for-list elems + body */
        break;

    case AN_FOR_STEP:
    case AN_FOR_WHILE:
    case AN_FOR_ELEM:
        for (c = n->first_child; c; c = ND(S, c)->next_sibling)
            (void)w_expr(S, c);
        break;

    case AN_LABELED:
        w_node(S, n->first_child);               /* the labelled statement */
        break;

    case AN_DUMMY:
        break;

    default:                                     /* a bare expression node */
        (void)w_expr(S, node);
        break;
    }
}

int a_sema_run(a_sema_ctx_t *S, uint32_t root)
{
    push_scope(S, 0);                            /* outermost (program) scope */
    w_node(S, root);
    pop_scope(S);
    return S->n_errs > 0 ? -1 : 0;
}
