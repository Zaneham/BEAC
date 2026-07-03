/* a_lower.c -- ALGOL AST -> JIR.
 *
 * Lowers the program to JIR for the cloned backend. A non-escaping local
 * lives in an alloca and is loaded/stored on use, because mem2reg has not
 * run yet: it is what later promotes the well-behaved ones to SSA, so we
 * leave that to it rather than try to be clever here. Control flow is built
 * the way jir_lower.c builds it (fresh blocks + BR/BR_COND).
 *
 * Storage is in three tiers: a frame-local (fast, the common case), a
 * program-level global (fixed slot in the global area), or, for a variable
 * read from a nested procedure, an activation-record slot reached through
 * the display (see the display section -- this is the faithful B6700 model
 * that makes non-local access work under recursion).
 *
 * Covered: blocks, type decls, assignment (incl. chained), arithmetic /
 * relational / logical expressions, IF, FOR (all four for-list element forms,
 * either step sign, multi-element lists), GO TO / labels, 1-D arrays,
 * procedures and calls, recursion, REAL return, and nested non-local access.
 * Not yet: multi-dimensional arrays, call-by-name / formal procedures. */

#include "a_lower.h"
#include <string.h>
#include <stdlib.h>

#define AL_MAX_LOC    256
#define AL_MAX_LBL    256
#define AL_MAX_PROC   256
#define AL_MAX_GLOBAL 256
#define AL_MAX_EVAR   256
#define AL_IDENT      64

/* The dynamic display (the faithful B6700 model). The global buffer is
 * carved into: [program globals] [G_SP] [display D[1..LEVELS]] [AR stack].
 * Each procedure activation reserves a fixed-size record (word 0 = the
 * saved D[level] the Mark Stack Control Word stands in for; the rest hold
 * escaping variables). A fixed AR size keeps the prologue's stack bump a
 * constant, so we needn't know a procedure's escaping count up front. */
#define AL_DISP_LEVELS  16
#define AL_AR_WORDS     32   /* words per activation record (1 link + 31 vars) */

typedef struct {
    jir_mod_t          *M;
    const a_sema_ctx_t *Sem;
    const a_typemap_t  *tm;
    const a_node_t     *nodes;
    const a_token_t    *toks;
    const char         *src;

    uint32_t cur_blk;
    uint32_t cur_fn;

    /* ALGOL name -> its alloca + jtype, within the current function */
    struct { char nm[AL_IDENT]; uint32_t alloc; uint32_t jty; } loc[AL_MAX_LOC];
    int n_loc;

    /* label name -> its block; reset per function */
    struct { char nm[AL_IDENT]; uint32_t blk; } lbl[AL_MAX_LBL];
    int n_lbl;

    /* procedure name -> jir func index; module-wide, so a call resolves
     * whatever the declaration order (and a procedure can call itself).
     * node/level let us lower procedures nested at any depth and, later,
     * drive the display. */
    struct { char nm[AL_IDENT]; uint32_t idx; uint32_t node; uint16_t level; }
        proc[AL_MAX_PROC];
    int n_proc;

    /* program-level variable -> field index in the global area. Every
     * program-level var is a global (any procedure might reach it), 8 bytes
     * apiece, addressed by JIR_GADDR + a MEMBER GEP rather than off a frame. */
    struct { char nm[AL_IDENT]; uint32_t field; uint32_t jty; } gvar[AL_MAX_GLOBAL];
    int n_gvar;

    /* escaping variable -> (lexical level, word offset in its level's
     * activation record). Reached at run time as D[level] + offset*8, so a
     * recursive procedure's invocations each see their own copy. */
    struct { char nm[AL_IDENT]; uint16_t level; uint16_t word; } evar[AL_MAX_EVAR];
    int n_evar;

    /* global-buffer layout, fixed once program globals are counted */
    uint32_t gsp_field;     /* the AR-stack pointer slot                 */
    uint32_t disp_field0;   /* D[1] slot; D[L] is disp_field0 + (L-1)    */
    uint32_t ar_base_field; /* first slot of the AR-stack region          */
    uint16_t cur_ar_word;   /* next free word in the procedure being lowered */
    uint16_t cur_level;     /* lexical level of the procedure being lowered  */

    /* mutable view of the type table: arrays mint JT_TABLE types here as
     * they are declared (M->S is the same pool, but const for codegen) */
    sema_ctx_t *jS;
} a_lower_t;

static a_lower_t L_;

/* ---- AST / token helpers ---- */

static const a_node_t *ND(const a_lower_t *L, uint32_t i) { return &L->nodes[i]; }

static void ntext(const a_lower_t *L, uint32_t tok, char *buf, int sz)
{
    const a_token_t *t = &L->toks[tok];
    int n = (int)t->len;
    if (n >= sz) n = sz - 1;
    memcpy(buf, L->src + t->offset, (size_t)n);
    buf[n] = '\0';
}

/* unsigned integer literal -> value; the lexer validated the shape */
static int64_t parse_int(const char *s)
{
    int64_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

/* REAL literal -> double; the Burroughs "@" exponent is "e" natively */
static double parse_real(const char *s)
{
    char buf[64];
    int i;
    for (i = 0; s[i] && i < 63; i++) buf[i] = (s[i] == '@') ? 'e' : s[i];
    buf[i] = '\0';
    return strtod(buf, NULL);
}

static int tok_atype(int op)
{
    switch (op) {
    case A_INTEGER: return AT_INTEGER;
    case A_REAL:    return AT_REAL;
    case A_BOOLEAN: return AT_BOOLEAN;
    case A_ALPHA:   return AT_ALPHA;
    default:        return AT_NONE;
    }
}

/* ---- JIR emit helpers (mirroring jir_lower.c) ---- */

static uint32_t add_str(a_lower_t *L, const char *s)
{
    jir_mod_t *M = L->M;
    uint32_t off = M->str_len, len = (uint32_t)strlen(s);
    if (off + len + 1 > JIR_MAX_STRS) return 0;
    memcpy(M->strs + off, s, len);
    M->strs[off + len] = '\0';
    M->str_len += len + 1;
    return off;
}

static uint32_t emit(a_lower_t *L, int op, uint32_t type, int nops, int subop)
{
    jir_mod_t *M = L->M;
    uint32_t idx;
    jir_inst_t *I;
    if (M->n_inst >= JIR_MAX_INST) return 0;
    idx = M->n_inst++;
    I = &M->insts[idx];
    memset(I, 0, sizeof(*I));
    I->op = (uint16_t)op; I->n_ops = (uint8_t)nops;
    I->subop = (uint8_t)subop; I->type = type;
    if (L->cur_blk < M->n_blks) M->blks[L->cur_blk].n_inst++;
    return idx;
}

static void setop(a_lower_t *L, uint32_t inst, int slot, uint32_t val)
{
    if (inst < L->M->n_inst && slot >= 0 && slot < 4)
        L->M->insts[inst].ops[slot] = val;
}

static uint32_t mk_ci(a_lower_t *L, int64_t v)
{
    jir_mod_t *M = L->M;
    uint32_t i, idx;
    for (i = 0; i < M->n_consts; i++)
        if (M->consts[i].kind == JC_INT && M->consts[i].iv == v) return JIR_MK_C(i);
    if (M->n_consts >= JIR_MAX_CONST) return JIR_MK_C(0);
    idx = M->n_consts++;
    M->consts[idx].kind = JC_INT; M->consts[idx].iv = v;
    return JIR_MK_C(idx);
}

static uint32_t mk_cf(a_lower_t *L, double v)
{
    jir_mod_t *M = L->M;
    int64_t bits;
    uint32_t i, idx;
    memcpy(&bits, &v, 8);
    for (i = 0; i < M->n_consts; i++)
        if (M->consts[i].kind == JC_FLT && M->consts[i].iv == bits) return JIR_MK_C(i);
    if (M->n_consts >= JIR_MAX_CONST) return JIR_MK_C(0);
    idx = M->n_consts++;
    M->consts[idx].kind = JC_FLT; M->consts[idx].iv = bits;
    return JIR_MK_C(idx);
}

static uint32_t new_blk(a_lower_t *L, const char *name)
{
    jir_mod_t *M = L->M;
    uint32_t idx;
    if (M->n_blks >= JIR_MAX_BLKS) return 0;
    idx = M->n_blks++;
    M->blks[idx].first = M->n_inst;
    M->blks[idx].n_inst = 0;
    M->blks[idx].name = add_str(L, name);
    return idx;
}

static void set_blk(a_lower_t *L, uint32_t idx)
{
    L->cur_blk = idx;
    if (idx < L->M->n_blks) L->M->blks[idx].first = L->M->n_inst;
}

static int blk_term(const a_lower_t *L)
{
    const jir_mod_t *M = L->M;
    const jir_blk_t *b;
    uint16_t last;
    if (L->cur_blk >= M->n_blks) return 0;
    b = &M->blks[L->cur_blk];
    if (b->n_inst == 0) return 0;
    last = M->insts[b->first + b->n_inst - 1].op;
    return last == JIR_BR || last == JIR_BR_COND || last == JIR_RET;
}

static void br_to(a_lower_t *L, uint32_t blk)
{
    uint32_t br = emit(L, JIR_BR, 0, 1, 0);
    setop(L, br, 0, blk);
}

/* ---- Locals / labels / procedures ---- */

static void add_loc(a_lower_t *L, const char *nm, uint32_t alloc, uint32_t jty)
{
    if (L->n_loc >= AL_MAX_LOC) return;
    snprintf(L->loc[L->n_loc].nm, AL_IDENT, "%s", nm);
    L->loc[L->n_loc].alloc = alloc;
    L->loc[L->n_loc].jty = jty;
    L->n_loc++;
}

static int find_loc(const a_lower_t *L, const char *nm)
{
    int i;
    for (i = L->n_loc - 1; i >= 0; i--)         /* newest-first: inner shadows */
        if (strcmp(L->loc[i].nm, nm) == 0) return i;
    return -1;
}

/* label name -> block, created on first mention so forward GO TO works
 * (set_blk fixes the block's instruction offset at the definition site) */
static uint32_t map_lbl(a_lower_t *L, const char *nm)
{
    int i;
    char bn[AL_IDENT + 8];
    uint32_t blk;
    for (i = 0; i < L->n_lbl; i++)
        if (strcmp(L->lbl[i].nm, nm) == 0) return L->lbl[i].blk;
    if (L->n_lbl >= AL_MAX_LBL) return L->cur_blk;
    snprintf(bn, sizeof(bn), "lbl.%.55s", nm);
    blk = new_blk(L, bn);
    snprintf(L->lbl[L->n_lbl].nm, AL_IDENT, "%s", nm);
    L->lbl[L->n_lbl].blk = blk;
    L->n_lbl++;
    return blk;
}

static int find_proc(const a_lower_t *L, const char *nm)
{
    int i;
    for (i = 0; i < L->n_proc; i++)
        if (strcmp(L->proc[i].nm, nm) == 0) return (int)L->proc[i].idx;
    return -1;
}

/* newest-first so an escaping local shadows an earlier same-named global */
static int find_global(const a_lower_t *L, const char *nm)
{
    int i;
    for (i = L->n_gvar - 1; i >= 0; i--)
        if (strcmp(L->gvar[i].nm, nm) == 0) return i;
    return -1;
}

/* does sema say this declaration is read from a nested procedure? */
static int decl_escapes(const a_lower_t *L, uint32_t decl_node)
{
    return decl_node < L->Sem->n_nodes && L->Sem->escapes[decl_node];
}

/* ---- The display: escaping variables live in activation records ---- */

static uint32_t low_global_addr(a_lower_t *L, uint32_t field);   /* fwd */

/* field index of D[level] in the global buffer */
static uint32_t disp_field(const a_lower_t *L, uint16_t level)
{
    return L->disp_field0 + (uint32_t)(level - 1);
}

/* give an escaping variable the next word in its level's record, returning
 * the address-of-that-record-slot recipe (level + word) via the evar table */
static void add_evar(a_lower_t *L, const char *nm, uint16_t level)
{
    if (L->n_evar >= AL_MAX_EVAR || L->cur_ar_word >= AL_AR_WORDS) return;
    snprintf(L->evar[L->n_evar].nm, AL_IDENT, "%s", nm);
    L->evar[L->n_evar].level = level;
    L->evar[L->n_evar].word  = L->cur_ar_word++;
    L->n_evar++;
}

/* newest-first so an inner escaping var shadows an outer same-named one */
static int find_evar(const a_lower_t *L, const char *nm)
{
    int i;
    for (i = L->n_evar - 1; i >= 0; i--)
        if (strcmp(L->evar[i].nm, nm) == 0) return i;
    return -1;
}

/* address of an escaping variable: load D[level] (its record's base) and
 * step to the variable's word. D[level] is kept current by prologues, so
 * this resolves to the right invocation's copy even under recursion. */
static uint32_t low_evar_addr(a_lower_t *L, int ei)
{
    uint16_t level = L->evar[ei].level, word = L->evar[ei].word;
    uint32_t da = low_global_addr(L, disp_field(L, level));  /* &D[level] */
    uint32_t base = emit(L, JIR_LOAD, 0, 1, 0);              /* D[level] value */
    uint32_t add;
    setop(L, base, 0, da);
    add = emit(L, JIR_ADD, 0, 2, 0);                         /* + word*8 */
    setop(L, add, 0, base);
    setop(L, add, 1, mk_ci(L, (int64_t)word * 8));
    return add;
}

/* Procedure entry: push this activation's record and make D[level] point at
 * it, parking the previous occupant in the record so recursion nests. */
static void emit_prologue(a_lower_t *L, uint16_t level)
{
    uint32_t gsp_a, base, disp_a, old, st, disp_a2, newsp, gsp_a2;

    gsp_a = low_global_addr(L, L->gsp_field);
    base  = emit(L, JIR_LOAD, 0, 1, 0);            /* base = G_SP (my record) */
    setop(L, base, 0, gsp_a);

    disp_a = low_global_addr(L, disp_field(L, level));
    old    = emit(L, JIR_LOAD, 0, 1, 0);           /* old = D[level] */
    setop(L, old, 0, disp_a);

    st = emit(L, JIR_STORE, 0, 2, 0);              /* record[0] = old D[level] */
    setop(L, st, 0, old); setop(L, st, 1, base);

    disp_a2 = low_global_addr(L, disp_field(L, level));
    st = emit(L, JIR_STORE, 0, 2, 0);              /* D[level] = my record */
    setop(L, st, 0, base); setop(L, st, 1, disp_a2);

    newsp = emit(L, JIR_ADD, 0, 2, 0);             /* G_SP += record size */
    setop(L, newsp, 0, base);
    setop(L, newsp, 1, mk_ci(L, (int64_t)AL_AR_WORDS * 8));
    gsp_a2 = low_global_addr(L, L->gsp_field);
    st = emit(L, JIR_STORE, 0, 2, 0);
    setop(L, st, 0, newsp); setop(L, st, 1, gsp_a2);
}

/* Procedure exit: restore D[level] from the parked value and pop the record. */
static void emit_epilogue(a_lower_t *L, uint16_t level)
{
    uint32_t disp_a, base, old, disp_a2, st, gsp_a;

    disp_a = low_global_addr(L, disp_field(L, level));
    base   = emit(L, JIR_LOAD, 0, 1, 0);           /* base = D[level] (my record) */
    setop(L, base, 0, disp_a);

    old = emit(L, JIR_LOAD, 0, 1, 0);              /* old = record[0] */
    setop(L, old, 0, base);

    disp_a2 = low_global_addr(L, disp_field(L, level));
    st = emit(L, JIR_STORE, 0, 2, 0);              /* D[level] = old */
    setop(L, st, 0, old); setop(L, st, 1, disp_a2);

    gsp_a = low_global_addr(L, L->gsp_field);
    st = emit(L, JIR_STORE, 0, 2, 0);              /* G_SP = my base (pop) */
    setop(L, st, 0, base); setop(L, st, 1, gsp_a);
}

/* Does this procedure declare any escaping variable (so it needs a record)?
 * Walks the body but stops at nested procedures -- their escapes are theirs. */
static int node_has_escape(const a_lower_t *L, uint32_t node)
{
    const a_node_t *n;
    uint32_t c;
    if (node == 0 || node >= L->Sem->n_nodes) return 0;
    n = ND(L, node);
    if (n->kind == AN_PROC_DECL) return 0;         /* don't descend into nests */
    if (n->kind == AN_TYPE_DECL) {
        for (c = n->first_child; c; c = ND(L, c)->next_sibling)
            if (decl_escapes(L, c)) return 1;
        return 0;
    }
    for (c = n->first_child; c; c = ND(L, c)->next_sibling)
        if (node_has_escape(L, c)) return 1;
    return 0;
}

static int proc_has_escapes(const a_lower_t *L, uint32_t proc_node)
{
    uint32_t c;
    for (c = ND(L, proc_node)->first_child; c; c = ND(L, c)->next_sibling) {
        if (ND(L, c)->kind == AN_PARAM) {
            if (decl_escapes(L, c)) return 1;
        } else if (node_has_escape(L, c)) {        /* the body */
            return 1;
        }
    }
    return 0;
}

/* address of a global field: JIR_GADDR gives the area's base, a MEMBER GEP
 * (field * 8) walks to the slot. Works from any function, no frame needed. */
static uint32_t low_global_addr(a_lower_t *L, uint32_t field)
{
    uint32_t ga = emit(L, JIR_GADDR, 0, 0, 0);
    uint32_t gep = emit(L, JIR_GEP, 0, 1, (int)field);   /* MEMBER form */
    setop(L, gep, 0, ga);
    return gep;
}

/* ---- Forward decls (mutual recursion) ---- */

static uint32_t low_expr(a_lower_t *L, uint32_t node);
static uint32_t low_call(a_lower_t *L, uint32_t node);
static uint32_t low_index_addr(a_lower_t *L, uint32_t node);
static void     low_node(a_lower_t *L, uint32_t node);

/* is this jtype one of our array-as-table types? */
static int is_table(const a_lower_t *L, uint32_t jty)
{
    return jty < (uint32_t)L->jS->n_types &&
           L->jS->types[jty].kind == JT_TABLE;
}

/* ---- Expressions ---- */

static int jpred(int op)
{
    switch (op) {
    case A_LSS: return JP_LT;  case A_LEQ: return JP_LE;
    case A_EQL: return JP_EQ;  case A_NEQ: return JP_NE;
    case A_GTR: return JP_GT;  case A_GEQ: return JP_GE;
    default:    return JP_EQ;
    }
}

static uint32_t low_expr(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    uint32_t jty = a_jtype_of(L->tm, L->Sem->ntype[node]);
    char buf[AL_IDENT];

    switch (n->kind) {
    case AN_NUM:
        ntext(L, n->tok, buf, (int)sizeof(buf));
        return (n->op == A_INTLIT) ? mk_ci(L, parse_int(buf))
                                   : mk_cf(L, parse_real(buf));
    case AN_BOOL:
        return mk_ci(L, n->aux ? 1 : 0);          /* TRUE / FALSE */
    case AN_VAR: {
        int li = find_loc(L, (ntext(L, n->tok, buf, (int)sizeof(buf)), buf));
        uint32_t ld;
        if (li < 0) {                              /* not local */
            int ei = find_evar(L, buf);
            int gi;
            if (ei >= 0) {                          /* escaping var (display) */
                /* address first: the LOAD consumes the address recipe, and
                 * the backend runs instructions in order with no reordering. */
                uint32_t a = low_evar_addr(L, ei);
                ld = emit(L, JIR_LOAD, jty, 1, 0);
                setop(L, ld, 0, a);
                return ld;
            }
            gi = find_global(L, buf);
            if (gi >= 0) {                          /* a program-level global */
                uint32_t a = low_global_addr(L, L->gvar[gi].field);
                ld = emit(L, JIR_LOAD, L->gvar[gi].jty, 1, 0);
                setop(L, ld, 0, a);
                return ld;
            }
            if (find_proc(L, buf) >= 0)            /* a parameterless function call */
                return low_call(L, node);
            return mk_ci(L, 0);                    /* sema already flagged it */
        }
        if (is_table(L, L->loc[li].jty))          /* bare array name = its base */
            return L->loc[li].alloc;
        ld = emit(L, JIR_LOAD, L->loc[li].jty, 1, 0);
        setop(L, ld, 0, L->loc[li].alloc);
        return ld;
    }
    case AN_INDEX: {                              /* subscripted variable, read */
        uint32_t gep = low_index_addr(L, node);
        uint32_t ld = emit(L, JIR_LOAD, jty, 1, 0);
        setop(L, ld, 0, gep);
        return ld;
    }
    case AN_CALL:
        return low_call(L, node);
    case AN_UNOP: {
        uint32_t ov = low_expr(L, n->first_child);
        int isf = (L->Sem->ntype[node] == AT_REAL);
        uint32_t u;
        if (n->op == A_MINUS) {
            u = emit(L, isf ? JIR_FNEG : JIR_NEG, jty, 1, 0);
            setop(L, u, 0, ov); return u;
        }
        if (n->op == A_NOT) {
            u = emit(L, JIR_NOT, jty, 1, 0);
            setop(L, u, 0, ov); return u;
        }
        return ov;                                 /* unary plus = identity */
    }
    case AN_BINOP: {
        uint32_t lhs = n->first_child;
        uint32_t rhs = ND(L, lhs)->next_sibling;
        uint32_t lv = low_expr(L, lhs);
        uint32_t rv = low_expr(L, rhs);
        int isf = (L->Sem->ntype[node] == AT_REAL);
        int op, sub = 0;
        uint32_t inst;
        switch (n->op) {
        case A_PLUS:  op = isf ? JIR_FADD : JIR_ADD; break;
        case A_MINUS: op = isf ? JIR_FSUB : JIR_SUB; break;
        case A_TIMES: op = isf ? JIR_FMUL : JIR_MUL; break;
        case A_SLASH: op = JIR_FDIV; break;        /* "/" is real division */
        case A_DIV:   op = JIR_DIV;  break;
        case A_MOD:   op = JIR_MOD;  break;
        case A_AND:   op = JIR_AND;  break;
        case A_OR:    op = JIR_OR;   break;
        case A_LSS: case A_LEQ: case A_EQL:
        case A_GEQ: case A_GTR: case A_NEQ:
            op = (L->Sem->ntype[lhs] == AT_REAL) ? JIR_FCMP : JIR_ICMP;
            sub = jpred(n->op);
            break;
        default: op = JIR_ADD; break;
        }
        inst = emit(L, op, jty, 2, sub);
        setop(L, inst, 0, lv);
        setop(L, inst, 1, rv);
        return inst;
    }
    default:
        return mk_ci(L, 0);
    }
}

/* address of an assignable variable */
static uint32_t low_lhs(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    char buf[AL_IDENT];
    if (n->kind == AN_VAR) {
        int li = find_loc(L, (ntext(L, n->tok, buf, (int)sizeof(buf)), buf));
        int ei, gi;
        if (li >= 0) return L->loc[li].alloc;
        ei = find_evar(L, buf);                    /* an escaping var (display) */
        if (ei >= 0) return low_evar_addr(L, ei);
        gi = find_global(L, buf);                  /* a program-level global */
        if (gi >= 0) return low_global_addr(L, L->gvar[gi].field);
    }
    if (n->kind == AN_INDEX) return low_index_addr(L, node);  /* A[i] := ... */
    return 0;
}

/* address of an array element: GEP whose type is the array's JT_TABLE, so
 * the backend pulls the lower bound and stride straight from the tbldef. */
static uint32_t low_index_addr(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    uint32_t base = n->first_child;
    uint32_t sub = base ? ND(L, base)->next_sibling : 0;
    char buf[AL_IDENT];
    int li;
    uint32_t gep;
    li = find_loc(L, (ntext(L, ND(L, base)->tok, buf, (int)sizeof(buf)), buf));
    if (li < 0) return 0;
    gep = emit(L, JIR_GEP, L->loc[li].jty, 2, 0);
    setop(L, gep, 0, L->loc[li].alloc);
    setop(L, gep, 1, sub ? low_expr(L, sub) : mk_ci(L, 0));
    return gep;
}

/* procedure / function call, value-returning. Callee resolves through the
 * module-wide proc registry; the func index rides in ops[0] tagged as a
 * const, exactly as jir_lower.c encodes it. */
static uint32_t low_call(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    uint32_t ret_jty = a_jtype_of(L->tm, L->Sem->ntype[node]);
    uint32_t args[16], call;
    int narg = 0, pi, k;
    char buf[AL_IDENT];

    if (n->kind == AN_VAR) {                       /* bare call, no actuals */
        ntext(L, n->tok, buf, (int)sizeof(buf));
        pi = find_proc(L, buf);
        call = emit(L, JIR_CALL, ret_jty, 1, 0);
        setop(L, call, 0, JIR_MK_C(pi < 0 ? 0u : (uint32_t)pi));
        return call;
    }

    /* AN_CALL: callee then actual parameters */
    {
        uint32_t callee = n->first_child, a;
        ntext(L, ND(L, callee)->tok, buf, (int)sizeof(buf));
        pi = find_proc(L, buf);
        for (a = ND(L, callee)->next_sibling; a && narg < 16;
             a = ND(L, a)->next_sibling)
            args[narg++] = low_expr(L, a);
    }
    call = emit(L, JIR_CALL, ret_jty, (narg < 3) ? narg + 1 : 4, 0);
    setop(L, call, 0, JIR_MK_C(pi < 0 ? 0u : (uint32_t)pi));
    for (k = 0; k < narg && k < 3; k++) setop(L, call, k + 1, args[k]);
    if (narg > 3) {                                /* spill to extra pool */
        jir_mod_t *M = L->M;
        M->insts[call].n_ops = 0xFF;
        M->insts[call].ops[0] = M->n_extra;
        M->insts[call].ops[1] = (uint32_t)narg + 1;
        if (M->n_extra < JIR_MAX_EXTRA)
            M->extra[M->n_extra++] = JIR_MK_C(pi < 0 ? 0u : (uint32_t)pi);
        for (k = 0; k < narg && M->n_extra < JIR_MAX_EXTRA; k++)
            M->extra[M->n_extra++] = args[k];
    }
    return call;
}

/* ---- Declarations ---- */

/* a constant array bound. Burroughs lets bounds be arbitrary expressions,
 * but a stack frame whose size isn't known until run time is a fight for
 * another day -- so for now a bound has to fold to a literal. */
static int64_t const_bound(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    char buf[AL_IDENT];
    if (n->kind == AN_NUM && n->op == A_INTLIT) {
        ntext(L, n->tok, buf, (int)sizeof(buf));
        return parse_int(buf);
    }
    if (n->kind == AN_UNOP && n->op == A_MINUS) {  /* a negative lower bound */
        const a_node_t *c = ND(L, n->first_child);
        if (c->kind == AN_NUM && c->op == A_INTLIT) {
            ntext(L, c->tok, buf, (int)sizeof(buf));
            return -parse_int(buf);
        }
    }
    return 0;
}

/* Mint a JT_TABLE type (and its tbldef) for a 1-D array. A Burroughs array
 * is a one-field table that doesn't know it's slumming: n_extra = 1 hands
 * the backend an 8-byte stride, lo/hi feed tbl_lo and the alloca size. One
 * per array variable, no interning -- an array's bounds are its own affair. */
static uint32_t make_array_type(a_lower_t *L, int64_t lo, int64_t hi,
                                uint32_t elem)
{
    sema_ctx_t *S = L->jS;
    uint32_t tdi, ti;
    if (S->n_tbldf >= SM_MAX_TBLDF || S->n_types >= SM_MAX_TYPES) return elem;
    tdi = (uint32_t)S->n_tbldf++;
    memset(&S->tbldef[tdi], 0, sizeof(S->tbldef[tdi]));
    S->tbldef[tdi].n_flds = 1;
    S->tbldef[tdi].flds[0].jtype = elem;
    S->tbldef[tdi].lo_dim = (int32_t)lo;
    S->tbldef[tdi].hi_dim = (int32_t)hi;
    ti = (uint32_t)S->n_types++;
    memset(&S->types[ti], 0, sizeof(S->types[ti]));
    S->types[ti].kind = JT_TABLE;
    S->types[ti].n_extra = 1;                       /* one field -> 8-byte row */
    S->types[ti].inner = elem;
    S->types[ti].extra = tdi;
    return ti;
}

static void low_array_decl(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    uint32_t elem = a_jtype_of(L->tm, tok_atype(n->op));
    int64_t lo = 0, hi = 0;
    uint32_t c;
    char buf[AL_IDENT];
    /* one bound pair for now (1-D); every name in the segment shares it */
    for (c = n->first_child; c; c = ND(L, c)->next_sibling)
        if (ND(L, c)->kind == AN_BOUND_PAIR) {
            uint32_t b0 = ND(L, c)->first_child;
            lo = const_bound(L, b0);
            hi = const_bound(L, ND(L, b0)->next_sibling);
            break;
        }
    for (c = n->first_child; c; c = ND(L, c)->next_sibling)
        if (ND(L, c)->kind == AN_VAR) {
            uint32_t aty = make_array_type(L, lo, hi, elem);
            uint32_t al = emit(L, JIR_ALLOCA, aty, 0, 0);
            ntext(L, ND(L, c)->tok, buf, (int)sizeof(buf));
            add_loc(L, buf, al, aty);
        }
}

static void low_decl(a_lower_t *L, uint32_t node)
{
    const a_node_t *n = ND(L, node);
    uint32_t c;
    char buf[AL_IDENT];
    if (n->kind == AN_ARRAY_DECL) { low_array_decl(L, node); return; }
    if (n->kind != AN_TYPE_DECL) return;           /* procedures: pass 1 */
    {
        uint32_t jty = a_jtype_of(L->tm, tok_atype(n->op));
        for (c = n->first_child; c; c = ND(L, c)->next_sibling) {
            ntext(L, ND(L, c)->tok, buf, (int)sizeof(buf));
            if (decl_escapes(L, c)) {              /* read from a nested proc */
                add_evar(L, buf, L->cur_level);    /* lives in the record */
            } else {
                uint32_t al = emit(L, JIR_ALLOCA, jty, 0, 0);
                add_loc(L, buf, al, jty);          /* fast frame-local */
            }
        }
    }
}

/* ---- Statements ---- */

static void low_assign(a_lower_t *L, uint32_t node)
{
    /* children are target(s) then the source expression (last child) */
    uint32_t c, src = 0, last = 0, val;
    for (c = ND(L, node)->first_child; c; c = ND(L, c)->next_sibling) last = c;
    src = last;
    val = low_expr(L, src);
    for (c = ND(L, node)->first_child; c && c != src; c = ND(L, c)->next_sibling) {
        uint32_t addr = low_lhs(L, c);
        uint32_t st = emit(L, JIR_STORE, 0, 2, 0);
        setop(L, st, 0, val);                      /* value */
        setop(L, st, 1, addr);                     /* address */
    }
}

static void low_if(a_lower_t *L, uint32_t node)
{
    uint32_t cond = ND(L, node)->first_child;
    uint32_t then_n = ND(L, cond)->next_sibling;
    uint32_t else_n = then_n ? ND(L, then_n)->next_sibling : 0;
    uint32_t cv = low_expr(L, cond);
    uint32_t then_b = new_blk(L, "if.then");
    uint32_t else_b = else_n ? new_blk(L, "if.else") : 0;
    uint32_t end_b  = new_blk(L, "if.end");
    uint32_t bc = emit(L, JIR_BR_COND, 0, 3, 0);
    setop(L, bc, 0, cv);
    setop(L, bc, 1, then_b);
    setop(L, bc, 2, else_n ? else_b : end_b);

    set_blk(L, then_b);
    low_node(L, then_n);
    if (!blk_term(L)) br_to(L, end_b);

    if (else_n) {
        set_blk(L, else_b);
        low_node(L, else_n);
        if (!blk_term(L)) br_to(L, end_b);
    }
    set_blk(L, end_b);
}

/* One for-list element (Section 8). Lowers the element in place, running the
 * shared body once per value it produces, and leaves cur_blk on a fresh
 * continuation block so the next element (or the code after the FOR) picks up
 * from there. The controlled variable's type picks the arithmetic: isf selects
 * the float ops, so a REAL loop counter counts in REAL.
 *
 *   bare value  -> v := e; body once
 *   STEP UNTIL  -> counting loop, either step sign. The continuation test is
 *                  (v - limit) * step <= 0, which is the manual's
 *                  (v - limit) * sign(step) <= 0 scaled by |step|, so it works
 *                  for a negative or a runtime-signed step without a branch.
 *   STEP WHILE  -> same increment, but a Boolean guard replaces the limit test.
 *   plain WHILE -> v := e re-evaluated each turn, run while the guard holds.
 * The step and limit are re-evaluated per iteration, per Section 8 (a step with
 * side effects is thus evaluated twice a turn, in the test and the increment;
 * the manual's own definition references it in both places). */
static void low_for_elem(a_lower_t *L, uint32_t elem, uint32_t addr,
                         uint32_t vjty, int isf, uint32_t body)
{
    uint32_t bjty = a_jtype_of(L->tm, AT_BOOLEAN);
    int subop = isf ? JIR_FSUB : JIR_SUB;
    int mulop = isf ? JIR_FMUL : JIR_MUL;
    int addop = isf ? JIR_FADD : JIR_ADD;
    int cmpop = isf ? JIR_FCMP : JIR_ICMP;
    uint32_t st;

    if (ND(L, elem)->kind == AN_FOR_ELEM) {
        st = emit(L, JIR_STORE, 0, 2, 0);
        setop(L, st, 0, low_expr(L, ND(L, elem)->first_child));
        setop(L, st, 1, addr);
        low_node(L, body);
        return;
    }

    if (ND(L, elem)->kind == AN_FOR_WHILE) {
        uint32_t val  = ND(L, elem)->first_child;
        uint32_t cond = ND(L, val)->next_sibling;
        uint32_t hdr = new_blk(L, "for.hdr"), bod = new_blk(L, "for.body");
        uint32_t cont = new_blk(L, "for.cont"), cmp, bc;
        if (!blk_term(L)) br_to(L, hdr);
        set_blk(L, hdr);
        st = emit(L, JIR_STORE, 0, 2, 0);
        setop(L, st, 0, low_expr(L, val)); setop(L, st, 1, addr);
        cmp = low_expr(L, cond);
        bc = emit(L, JIR_BR_COND, 0, 3, 0);
        setop(L, bc, 0, cmp); setop(L, bc, 1, bod); setop(L, bc, 2, cont);
        set_blk(L, bod);
        low_node(L, body);
        if (!blk_term(L)) br_to(L, hdr);
        set_blk(L, cont);
        return;
    }

    /* AN_FOR_STEP: child0 = init, child1 = step, child2 = limit-or-guard.
     * aux 0 = STEP..UNTIL (arithmetic limit), aux 1 = STEP..WHILE (Boolean). */
    {
        uint32_t init  = ND(L, elem)->first_child;
        uint32_t step  = ND(L, init)->next_sibling;
        uint32_t third = ND(L, step)->next_sibling;
        int is_while   = (ND(L, elem)->aux == 1);
        uint32_t hdr = new_blk(L, "for.hdr"), bod = new_blk(L, "for.body");
        uint32_t stp = new_blk(L, "for.step"), cont = new_blk(L, "for.cont");
        uint32_t cmp, bc;

        st = emit(L, JIR_STORE, 0, 2, 0);
        setop(L, st, 0, low_expr(L, init)); setop(L, st, 1, addr);
        if (!blk_term(L)) br_to(L, hdr);

        set_blk(L, hdr);
        if (is_while) {
            cmp = low_expr(L, third);              /* continue while guard */
        } else {
            uint32_t v = emit(L, JIR_LOAD, vjty, 1, 0); setop(L, v, 0, addr);
            uint32_t lv = low_expr(L, third);      /* limit */
            uint32_t sv = low_expr(L, step);
            uint32_t diff = emit(L, subop, vjty, 2, 0);
            uint32_t prod;
            setop(L, diff, 0, v); setop(L, diff, 1, lv);
            prod = emit(L, mulop, vjty, 2, 0);
            setop(L, prod, 0, diff); setop(L, prod, 1, sv);
            cmp = emit(L, cmpop, bjty, 2, JP_LE);  /* (v-limit)*step <= 0 */
            setop(L, cmp, 0, prod);
            setop(L, cmp, 1, isf ? mk_cf(L, 0.0) : mk_ci(L, 0));
        }
        bc = emit(L, JIR_BR_COND, 0, 3, 0);
        setop(L, bc, 0, cmp); setop(L, bc, 1, bod); setop(L, bc, 2, cont);

        set_blk(L, bod);
        low_node(L, body);
        if (!blk_term(L)) br_to(L, stp);

        set_blk(L, stp);
        {
            uint32_t v = emit(L, JIR_LOAD, vjty, 1, 0); setop(L, v, 0, addr);
            uint32_t sv = low_expr(L, step);
            uint32_t add = emit(L, addop, vjty, 2, 0);
            setop(L, add, 0, v); setop(L, add, 1, sv);
            st = emit(L, JIR_STORE, 0, 2, 0);
            setop(L, st, 0, add); setop(L, st, 1, addr);
        }
        br_to(L, hdr);
        set_blk(L, cont);
    }
}

/* FOR v := <for-list> DO body. The for-list is every element between the
 * controlled variable and the body (last child); they run left to right,
 * sharing the one body, per Section 8. */
static void low_for(a_lower_t *L, uint32_t node)
{
    uint32_t var = ND(L, node)->first_child;
    uint32_t body = 0, c, addr, vjty;
    int li, isf;
    char buf[AL_IDENT];

    for (c = ND(L, node)->first_child; c; c = ND(L, c)->next_sibling) body = c;
    addr = low_lhs(L, var);
    li = find_loc(L, (ntext(L, ND(L, var)->tok, buf, (int)sizeof(buf)), buf));
    vjty = (li >= 0) ? L->loc[li].jty : a_jtype_of(L->tm, AT_INTEGER);
    isf = (L->Sem->ntype[var] == AT_REAL);

    for (c = ND(L, var)->next_sibling; c && c != body; c = ND(L, c)->next_sibling)
        low_for_elem(L, c, addr, vjty, isf, body);
}

static void low_goto(a_lower_t *L, uint32_t node)
{
    uint32_t tgt = ND(L, node)->first_child;
    char buf[AL_IDENT];
    if (tgt && ND(L, tgt)->kind == AN_VAR) {
        ntext(L, ND(L, tgt)->tok, buf, (int)sizeof(buf));
        br_to(L, map_lbl(L, buf));
    }
}

static void low_labeled(a_lower_t *L, uint32_t node)
{
    char buf[AL_IDENT];
    uint32_t blk;
    ntext(L, ND(L, node)->tok, buf, (int)sizeof(buf));   /* the label name */
    blk = map_lbl(L, buf);
    if (!blk_term(L)) br_to(L, blk);               /* fall into the label */
    set_blk(L, blk);
    low_node(L, ND(L, node)->first_child);         /* the labelled statement */
}

/* a block or compound: declarations then statements, in source order */
static void low_block(a_lower_t *L, uint32_t node)
{
    uint32_t c;
    for (c = ND(L, node)->first_child; c; c = ND(L, c)->next_sibling) {
        int k = ND(L, c)->kind;
        if (k == AN_TYPE_DECL || k == AN_ARRAY_DECL || k == AN_PROC_DECL)
            low_decl(L, c);                        /* procs handled in pass 1 */
        else
            low_node(L, c);
    }
}

static void low_node(a_lower_t *L, uint32_t node)
{
    const a_node_t *n;
    if (node == 0 || node >= L->Sem->n_nodes) return;
    n = ND(L, node);
    switch (n->kind) {
    case AN_BLOCK:
    case AN_COMPOUND: low_block(L, node); break;
    case AN_ASSIGN:   low_assign(L, node); break;
    case AN_IF_STMT:  low_if(L, node); break;
    case AN_FOR:      low_for(L, node); break;
    case AN_GOTO:     low_goto(L, node); break;
    case AN_LABELED:  low_labeled(L, node); break;
    case AN_PROC_CALL:(void)low_call(L, n->first_child); break;
    case AN_DUMMY:    break;
    default:          break;
    }
}

/* ---- Procedures ---- */

/* lower a procedure declaration into the pre-assigned func slot fi. A typed
 * function returns the value assigned to its own name (ALGOL has no
 * RETURN): we give that name a result slot, and the trailing RET hands it
 * back. Proper procedures RET void. */
static void low_func(a_lower_t *L, uint32_t node, uint32_t fi, uint16_t level)
{
    const a_node_t *n = ND(L, node);
    jir_mod_t *M = L->M;
    jir_func_t *f = &M->funcs[fi];
    uint32_t entry, c, body = 0, result = 0, ret_jty, i;
    uint16_t pcnt = 0;
    int saved_nloc = L->n_loc;
    int has_ar = proc_has_escapes(L, node);        /* needs an activation record? */
    char buf[AL_IDENT];
    /* escaping params: remember (alloca, name, type) to copy into the record
     * once the prologue has established it. */
    struct { uint32_t al; uint32_t pty; char nm[AL_IDENT]; } epar[16];
    int n_epar = 0, ep;

    L->cur_level  = level;
    L->cur_ar_word = 1;                            /* word 0 is the saved link */

    ntext(L, n->tok, buf, (int)sizeof(buf));
    f->name = add_str(L, buf);
    ret_jty = (n->op != 0) ? a_jtype_of(L->tm, tok_atype(n->op)) : 0;
    f->ret_type = ret_jty;
    f->sema_nd = node;
    f->first_blk = M->n_blks;
    L->cur_fn = fi;
    L->n_lbl = 0;                                  /* labels are per-function */

    entry = new_blk(L, "entry");
    set_blk(L, entry);

    for (c = n->first_child; c; c = ND(L, c)->next_sibling) {
        if (ND(L, c)->kind == AN_PARAM) {
            uint32_t pty = a_jtype_of(L->tm, tok_atype(ND(L, c)->op));
            /* the backend always fills the Nth alloca with the Nth arg, so
             * a param needs its slot even when it escapes. */
            uint32_t al = emit(L, JIR_ALLOCA, pty, 0, 0);
            ntext(L, ND(L, c)->tok, buf, (int)sizeof(buf));
            pcnt++;
            if (decl_escapes(L, c) && n_epar < 16) {
                epar[n_epar].al = al; epar[n_epar].pty = pty;
                snprintf(epar[n_epar].nm, AL_IDENT, "%s", buf);
                n_epar++;                          /* copy-in deferred to prologue */
            } else {
                add_loc(L, buf, al, pty);
            }
        } else {
            body = c;                              /* last non-param child */
        }
    }
    f->n_params = pcnt;

    /* push this activation's record before anything reads escaping storage */
    if (has_ar) emit_prologue(L, level);

    /* now the record exists: give each escaping param a word and copy its
     * incoming value in, so body and nested procs share the one cell. */
    for (ep = 0; ep < n_epar; ep++) {
        uint32_t ld, addr, st;
        int ei;
        add_evar(L, epar[ep].nm, level);
        ei = find_evar(L, epar[ep].nm);
        ld = emit(L, JIR_LOAD, epar[ep].pty, 1, 0);
        setop(L, ld, 0, epar[ep].al);
        addr = low_evar_addr(L, ei);
        st = emit(L, JIR_STORE, 0, 2, 0);
        setop(L, st, 0, ld); setop(L, st, 1, addr);
    }

    if (ret_jty != 0) {                            /* result slot = the name */
        result = emit(L, JIR_ALLOCA, ret_jty, 0, 0);
        ntext(L, n->tok, buf, (int)sizeof(buf));
        add_loc(L, buf, result, ret_jty);
    }

    if (body) low_node(L, body);

    if (!blk_term(L)) {
        if (ret_jty != 0) {
            /* load the result BEFORE the epilogue pops the record (the result
             * slot is a frame local, so it survives, but read it first for
             * clarity and to keep the value independent of display state). */
            uint32_t ld = emit(L, JIR_LOAD, ret_jty, 1, 0);
            setop(L, ld, 0, result);
            if (has_ar) emit_epilogue(L, level);
            { uint32_t r = emit(L, JIR_RET, ret_jty, 1, 0); setop(L, r, 0, ld); }
        } else {
            if (has_ar) emit_epilogue(L, level);
            emit(L, JIR_RET, 0, 0, 0);
        }
    }

    f->n_blks = (uint16_t)(M->n_blks - f->first_blk);
    f->n_inst = 0;
    for (i = f->first_blk; i < M->n_blks; i++)
        f->n_inst += M->blks[i].n_inst;

    L->n_loc = saved_nloc;                          /* locals out of scope */
}

/* Walk a scope and register every procedure in it -- and, recursively,
 * every procedure nested inside those -- assigning each a func slot and its
 * lexical level (body_level: top-level procs run at 1, their nested procs at
 * 2, and so on, matching sema). Registering all names up front lets a call
 * resolve regardless of declaration order or nesting. */
static void discover_procs(a_lower_t *L, uint32_t block, uint16_t body_level)
{
    uint32_t c;
    int k = ND(L, block)->kind;
    if (k != AN_BLOCK && k != AN_COMPOUND) return;
    for (c = ND(L, block)->first_child; c; c = ND(L, c)->next_sibling) {
        if (ND(L, c)->kind == AN_PROC_DECL && L->n_proc < AL_MAX_PROC) {
            char nb[AL_IDENT];
            uint32_t body = 0, ch;
            ntext(L, ND(L, c)->tok, nb, (int)sizeof(nb));
            snprintf(L->proc[L->n_proc].nm, AL_IDENT, "%s", nb);
            L->proc[L->n_proc].idx = (uint32_t)L->n_proc;
            L->proc[L->n_proc].node = c;
            L->proc[L->n_proc].level = body_level;
            L->n_proc++;
            for (ch = ND(L, c)->first_child; ch; ch = ND(L, ch)->next_sibling)
                if (ND(L, ch)->kind != AN_PARAM) body = ch;   /* last = body */
            if (body) discover_procs(L, body, (uint16_t)(body_level + 1));
        }
    }
}

/* Register every program-level variable as a global. Any procedure may
 * reach a program-level name, so they all live in the global area instead
 * of main's frame -- one 8-byte field apiece, numbered in order. */
static void register_globals(a_lower_t *L, uint32_t root)
{
    uint32_t c, v;
    int k = ND(L, root)->kind;
    if (k != AN_BLOCK && k != AN_COMPOUND) return;
    for (c = ND(L, root)->first_child; c; c = ND(L, c)->next_sibling) {
        uint32_t jty;
        if (ND(L, c)->kind != AN_TYPE_DECL) continue;
        jty = a_jtype_of(L->tm, tok_atype(ND(L, c)->op));
        for (v = ND(L, c)->first_child; v; v = ND(L, v)->next_sibling) {
            char buf[AL_IDENT];
            if (L->n_gvar >= AL_MAX_GLOBAL) return;
            ntext(L, ND(L, v)->tok, buf, (int)sizeof(buf));
            snprintf(L->gvar[L->n_gvar].nm, AL_IDENT, "%s", buf);
            L->gvar[L->n_gvar].field = (uint32_t)L->n_gvar;
            L->gvar[L->n_gvar].jty = jty;
            L->n_gvar++;
        }
    }
}

/* ---- Entry ---- */

void a_lower(jir_mod_t *M, sema_ctx_t *jS,
             const a_sema_ctx_t *Sem, a_typemap_t *tm, uint32_t root)
{
    a_lower_t *L = &L_;
    jir_func_t *mf;
    uint32_t fi, entry, i;

    memset(M, 0, sizeof(*M));
    M->S = jS;                                     /* const view for codegen */
    M->str_len = 1; M->strs[0] = '\0';             /* offset 0 = empty string */

    memset(L, 0, sizeof(*L));
    L->M = M; L->Sem = Sem; L->tm = tm; L->jS = jS;
    L->nodes = Sem->nodes; L->toks = Sem->toks; L->src = Sem->src;

    register_globals(L, root);     /* program-level vars -> the global area */

    /* carve the rest of the global buffer now that program globals are
     * counted: [globals][G_SP][D[1..LEVELS]][AR stack]. */
    L->gsp_field     = (uint32_t)L->n_gvar;
    L->disp_field0   = L->gsp_field + 1;
    L->ar_base_field = L->disp_field0 + AL_DISP_LEVELS;
    tm->gsp_field     = L->gsp_field;      /* so a caller can seed G_SP */
    tm->ar_base_field = L->ar_base_field;

    /* pass 1: discover every procedure (at any nesting depth), reserve its
     * func slot, then lower each. Discovery first lets calls resolve in any
     * order, across nesting, and self-recursively. */
    discover_procs(L, root, 1);
    M->n_funcs = (uint32_t)L->n_proc;              /* slots 0..n-1 are procs */
    {
        int pi;
        for (pi = 0; pi < L->n_proc; pi++)
            low_func(L, L->proc[pi].node, L->proc[pi].idx, L->proc[pi].level);
    }

    /* pass 2: the program's globals + statements become "main" */
    fi = M->n_funcs++;
    mf = &M->funcs[fi];
    mf->name = add_str(L, "main");
    mf->ret_type = 0; mf->n_params = 0; mf->sema_nd = root;
    mf->first_blk = M->n_blks;
    L->cur_fn = fi;
    L->n_loc = 0;
    L->n_lbl = 0;

    entry = new_blk(L, "entry");
    set_blk(L, entry);

    /* seed the AR-stack pointer to the base of the AR region before any
     * procedure runs -- but only if the program actually uses the display
     * (some escaping variable exists), so display-free programs stay clean.
     * The base address is just the address of the region's first slot. */
    if (L->n_evar > 0) {
        uint32_t arbase = low_global_addr(L, L->ar_base_field);
        uint32_t gsp = low_global_addr(L, L->gsp_field);
        uint32_t st = emit(L, JIR_STORE, 0, 2, 0);
        setop(L, st, 0, arbase); setop(L, st, 1, gsp);
    }

    /* main's body: program-level type decls are globals (no frame slot) and
     * the procedures were lowered in pass 1, so only arrays-as-locals and
     * the actual statements emit here. */
    if (ND(L, root)->kind == AN_BLOCK || ND(L, root)->kind == AN_COMPOUND) {
        uint32_t c;
        for (c = ND(L, root)->first_child; c; c = ND(L, c)->next_sibling) {
            int k = ND(L, c)->kind;
            if (k == AN_TYPE_DECL || k == AN_PROC_DECL) continue;
            if (k == AN_ARRAY_DECL) { low_decl(L, c); continue; }
            low_node(L, c);
        }
    } else {
        low_node(L, root);
    }

    if (!blk_term(L)) {                             /* return 0 -- clean exit */
        uint32_t r = emit(L, JIR_RET, 0, 1, 0);
        setop(L, r, 0, mk_ci(L, 0));
    }

    mf->n_blks = (uint16_t)(M->n_blks - mf->first_blk);
    mf->n_inst = 0;
    for (i = mf->first_blk; i < M->n_blks; i++)
        mf->n_inst += M->blks[i].n_inst;
}
