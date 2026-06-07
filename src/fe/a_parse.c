/* a_parse.c -- Burroughs Extended ALGOL parser (expressions first).
 *
 * Recursive descent, one function per precedence level, taken straight
 * from the Section 4 grammar. Lowest to highest:
 *   conditional (IF..THEN..ELSE) < EQV < IMP < OR < AND < NOT
 *   < relational < adding(+ -) < multiplying(x / DIV MOD)
 *   < exponentiation(*) < primary
 * Unary +/- prefix a term (the manual's "(adding operator)(term)" form);
 * NOT prefixes a Boolean secondary. "*" is exponentiation, left
 * associative as the grammar writes it (factor ::= factor * primary). */

#include "a_parse.h"
#include <string.h>
#include <stdio.h>

/* ---- AST kind names (for dumps / tests) ---- */

static const struct { int k; const char *n; } a_knames[] = {
    { AN_NONE, "none" }, { AN_PROGRAM, "program" }, { AN_BLOCK, "block" },
    { AN_COMPOUND, "compound" }, { AN_TYPE_DECL, "type_decl" },
    { AN_ARRAY_DECL, "array_decl" }, { AN_BOUND_PAIR, "bound_pair" },
    { AN_PROC_DECL, "proc_decl" }, { AN_SWITCH_DECL, "switch_decl" },
    { AN_PARAM, "param" }, { AN_ASSIGN, "assign" }, { AN_IF_STMT, "if_stmt" },
    { AN_FOR, "for" }, { AN_FOR_STEP, "for_step" }, { AN_FOR_WHILE, "for_while" },
    { AN_FOR_ELEM, "for_elem" }, { AN_GOTO, "goto" }, { AN_PROC_CALL, "proc_call" },
    { AN_LABELED, "labeled" }, { AN_DUMMY, "dummy" }, { AN_IO_STMT, "io_stmt" },
    { AN_NUM, "num" }, { AN_STR, "str" }, { AN_BOOL, "bool" }, { AN_VAR, "var" },
    { AN_INDEX, "index" }, { AN_CALL, "call" }, { AN_BINOP, "binop" },
    { AN_UNOP, "unop" }, { AN_COND_EXPR, "cond_expr" }, { AN_PARTIAL, "partial" },
    { AN_CONCAT, "concat" },
};

const char *a_nk_name(int kind)
{
    int i;
    for (i = 0; i < (int)(sizeof(a_knames) / sizeof(a_knames[0])); i++)
        if (a_knames[i].k == kind) return a_knames[i].n;
    return "?";
}

/* ---- Plumbing ---- */

void a_parse_init(a_parser_t *P, const a_token_t *toks, uint32_t n_toks,
                  const char *src, a_node_t *nodes, uint32_t max_nodes)
{
    memset(P, 0, sizeof(*P));
    P->toks = toks; P->n_toks = n_toks;
    P->src = src; P->nodes = nodes; P->max_nodes = max_nodes;
    P->n_nodes = 1;   /* node 0 is the AN_NONE sentinel */
    P->pos = 0;
}

static const a_token_t *cur(const a_parser_t *P)
{
    uint32_t i = (P->pos < P->n_toks) ? P->pos : (P->n_toks ? P->n_toks - 1 : 0);
    return &P->toks[i];
}

static int curt(const a_parser_t *P) { return cur(P)->type; }

static int peekt(const a_parser_t *P, uint32_t ahead)
{
    uint32_t i = P->pos + ahead;
    return (i < P->n_toks) ? (int)P->toks[i].type : A_EOF;
}

static void adv(a_parser_t *P)
{
    if (P->pos + 1 < P->n_toks) P->pos++;   /* never step past the EOF token */
}

static void perr(a_parser_t *P, const char *msg)
{
    if (P->n_errs >= A_PARSE_MAX_ERRS) return;
    a_parse_err_t *e = &P->errors[P->n_errs++];
    e->line = cur(P)->line; e->col = cur(P)->col;
    snprintf(e->msg, sizeof(e->msg), "%s", msg);
}

static uint32_t alloc(a_parser_t *P, int kind)
{
    uint32_t i;
    if (P->n_nodes >= P->max_nodes) return 0;
    i = P->n_nodes++;
    memset(&P->nodes[i], 0, sizeof(P->nodes[i]));
    P->nodes[i].kind = (uint16_t)kind;
    return i;
}

static void kid(a_parser_t *P, uint32_t parent, uint32_t child)
{
    uint32_t c;
    if (!parent || !child) return;
    c = P->nodes[parent].first_child;
    if (!c) { P->nodes[parent].first_child = child; return; }
    while (P->nodes[c].next_sibling) c = P->nodes[c].next_sibling;
    P->nodes[c].next_sibling = child;
}

static uint32_t mk_bin(a_parser_t *P, int op, uint32_t l, uint32_t r)
{
    uint32_t u = alloc(P, AN_BINOP);
    if (!u) return l;
    P->nodes[u].op = (uint16_t)op;
    kid(P, u, l); kid(P, u, r);
    return u;
}

static uint32_t mk_un(a_parser_t *P, int op, uint32_t operand)
{
    uint32_t u = alloc(P, AN_UNOP);
    if (!u) return operand;
    P->nodes[u].op = (uint16_t)op;
    kid(P, u, operand);
    return u;
}

/* ---- Expression grammar ---- */

static uint32_t p_expr(a_parser_t *P);

static int is_relop(int t)
{
    return t == A_LSS || t == A_LEQ || t == A_EQL ||
           t == A_GEQ || t == A_GTR || t == A_NEQ;
}

/* primary ::= number | string | logical value | ( expr )
 *           | variable [ ( actuals ) | [ subscripts ] | .[ start : len ] ] */
static uint32_t p_primary(a_parser_t *P)
{
    int t = curt(P);
    uint32_t tok = P->pos, u;

    if (t == A_INTLIT || t == A_REALLIT) {
        u = alloc(P, AN_NUM); P->nodes[u].op = (uint16_t)t; P->nodes[u].tok = tok;
        adv(P); return u;
    }
    if (t == A_STRLIT) {
        u = alloc(P, AN_STR); P->nodes[u].tok = tok; adv(P); return u;
    }
    if (t == A_TRUE || t == A_FALSE) {
        u = alloc(P, AN_BOOL); P->nodes[u].aux = (t == A_TRUE); P->nodes[u].tok = tok;
        adv(P); return u;
    }
    if (t == A_LPAREN) {
        adv(P);
        u = p_expr(P);
        if (!(curt(P) == A_RPAREN)) perr(P, "expected ')'"); else adv(P);
        return u;
    }
    if (t == A_IDENT) {
        u = alloc(P, AN_VAR); P->nodes[u].tok = tok; adv(P);
        for (;;) {
            if (curt(P) == A_LPAREN) {            /* function designator */
                uint32_t call = alloc(P, AN_CALL);
                kid(P, call, u); adv(P);
                if (curt(P) != A_RPAREN) {
                    for (;;) {
                        kid(P, call, p_expr(P));
                        if (curt(P) == A_COMMA) { adv(P); continue; }
                        break;
                    }
                }
                if (curt(P) == A_RPAREN) adv(P); else perr(P, "expected ')'");
                u = call;
            } else if (curt(P) == A_LBRACK) {     /* subscripted variable */
                uint32_t idx = alloc(P, AN_INDEX);
                kid(P, idx, u); adv(P);
                for (;;) {
                    kid(P, idx, p_expr(P));
                    if (curt(P) == A_COMMA) { adv(P); continue; }
                    break;
                }
                if (curt(P) == A_RBRACK) adv(P); else perr(P, "expected ']'");
                u = idx;
            } else if (curt(P) == A_DOT && peekt(P, 1) == A_LBRACK) {
                uint32_t part = alloc(P, AN_PARTIAL);   /* var.[start:len] */
                kid(P, part, u); adv(P); adv(P);        /* '.' '[' */
                kid(P, part, p_expr(P));
                if (curt(P) == A_COLON) adv(P); else perr(P, "expected ':'");
                kid(P, part, p_expr(P));
                if (curt(P) == A_RBRACK) adv(P); else perr(P, "expected ']'");
                u = part;
            } else break;
        }
        return u;
    }

    perr(P, "expected a primary");
    u = alloc(P, AN_NUM); P->nodes[u].tok = tok;   /* placeholder, keep shape */
    adv(P);
    return u;
}

/* factor ::= primary | factor * primary   (exponentiation, left assoc) */
static uint32_t p_factor(a_parser_t *P)
{
    uint32_t l = p_primary(P);
    while (curt(P) == A_POW) { adv(P); l = mk_bin(P, A_POW, l, p_primary(P)); }
    return l;
}

/* term ::= factor | term (x | / | DIV | MOD) factor */
static uint32_t p_term(a_parser_t *P)
{
    uint32_t l = p_factor(P);
    while (curt(P) == A_TIMES || curt(P) == A_SLASH ||
           curt(P) == A_DIV   || curt(P) == A_MOD) {
        int op = curt(P); adv(P); l = mk_bin(P, op, l, p_factor(P));
    }
    return l;
}

/* simple arithmetic expr: optional leading +/- , then (+|-) terms */
static uint32_t p_add(a_parser_t *P)
{
    uint32_t l;
    if (curt(P) == A_PLUS || curt(P) == A_MINUS) {
        int op = curt(P); adv(P);
        l = (op == A_MINUS) ? mk_un(P, A_MINUS, p_term(P)) : p_term(P);
    } else {
        l = p_term(P);
    }
    while (curt(P) == A_PLUS || curt(P) == A_MINUS) {
        int op = curt(P); adv(P); l = mk_bin(P, op, l, p_term(P));
    }
    return l;
}

/* relation ::= simple arith (relop) simple arith   (non-associative) */
static uint32_t p_rel(a_parser_t *P)
{
    uint32_t l = p_add(P);
    if (is_relop(curt(P))) { int op = curt(P); adv(P); l = mk_bin(P, op, l, p_add(P)); }
    return l;
}

static uint32_t p_not(a_parser_t *P)
{
    if (curt(P) == A_NOT) { adv(P); return mk_un(P, A_NOT, p_not(P)); }
    return p_rel(P);
}

static uint32_t p_and(a_parser_t *P)
{
    uint32_t l = p_not(P);
    while (curt(P) == A_AND) { adv(P); l = mk_bin(P, A_AND, l, p_not(P)); }
    return l;
}

static uint32_t p_or(a_parser_t *P)
{
    uint32_t l = p_and(P);
    while (curt(P) == A_OR) { adv(P); l = mk_bin(P, A_OR, l, p_and(P)); }
    return l;
}

static uint32_t p_imp(a_parser_t *P)
{
    uint32_t l = p_or(P);
    while (curt(P) == A_IMP) { adv(P); l = mk_bin(P, A_IMP, l, p_or(P)); }
    return l;
}

static uint32_t p_eqv(a_parser_t *P)
{
    uint32_t l = p_imp(P);
    while (curt(P) == A_EQV) { adv(P); l = mk_bin(P, A_EQV, l, p_imp(P)); }
    return l;
}

/* expr ::= IF (bool) THEN expr ELSE expr | simple/Boolean expr.
 * The condition is a Boolean expression (p_eqv, no bare conditional);
 * the THEN/ELSE arms are full expressions so they chain right. */
static uint32_t p_expr(a_parser_t *P)
{
    if (curt(P) == A_IF) {
        uint32_t u = alloc(P, AN_COND_EXPR);
        adv(P);
        kid(P, u, p_eqv(P));
        if (curt(P) == A_THEN) adv(P); else perr(P, "expected THEN");
        kid(P, u, p_expr(P));
        if (curt(P) == A_ELSE) adv(P); else perr(P, "expected ELSE");
        kid(P, u, p_expr(P));
        return u;
    }
    return p_eqv(P);
}

/* ---- Statement grammar (Sections 5-8) ---- */

static int eat(a_parser_t *P, int type, const char *msg)
{
    if (curt(P) == type) { adv(P); return 1; }
    perr(P, msg);
    return 0;
}

static uint32_t p_stmt(a_parser_t *P);

/* one for-list element (Section 8):
 *   arith                              -> AN_FOR_ELEM
 *   arith STEP arith UNTIL arith       -> AN_FOR_STEP (aux 0)
 *   arith STEP arith WHILE bool        -> AN_FOR_STEP (aux 1, limit is cond)
 *   arith WHILE bool                   -> AN_FOR_WHILE */
static uint32_t p_for_elem(a_parser_t *P)
{
    uint32_t val = p_expr(P), e;
    if (curt(P) == A_STEP) {
        adv(P);
        e = alloc(P, AN_FOR_STEP);
        kid(P, e, val); kid(P, e, p_expr(P));
        if (curt(P) == A_UNTIL)      { adv(P); P->nodes[e].aux = 0; kid(P, e, p_expr(P)); }
        else if (curt(P) == A_WHILE) { adv(P); P->nodes[e].aux = 1; kid(P, e, p_expr(P)); }
        else perr(P, "expected UNTIL or WHILE after STEP");
        return e;
    }
    if (curt(P) == A_WHILE) {
        adv(P);
        e = alloc(P, AN_FOR_WHILE);
        kid(P, e, val); kid(P, e, p_expr(P));
        return e;
    }
    e = alloc(P, AN_FOR_ELEM);
    kid(P, e, val);
    return e;
}

static uint32_t p_for(a_parser_t *P)
{
    uint32_t f = alloc(P, AN_FOR);
    adv(P);                                 /* FOR */
    kid(P, f, p_primary(P));                /* controlled variable */
    eat(P, A_ASSIGN, "expected ':=' in FOR clause");
    for (;;) {
        kid(P, f, p_for_elem(P));
        if (curt(P) == A_COMMA) { adv(P); continue; }
        break;
    }
    eat(P, A_DO, "expected DO");
    kid(P, f, p_stmt(P));                   /* body */
    return f;
}

static uint32_t p_if(a_parser_t *P)
{
    uint32_t n = alloc(P, AN_IF_STMT);
    adv(P);                                 /* IF */
    kid(P, n, p_eqv(P));                    /* Boolean condition */
    eat(P, A_THEN, "expected THEN");
    kid(P, n, p_stmt(P));                   /* then-branch */
    if (curt(P) == A_ELSE) { adv(P); kid(P, n, p_stmt(P)); }  /* nearest-ELSE */
    return n;
}

static uint32_t p_goto(a_parser_t *P)
{
    uint32_t g = alloc(P, AN_GOTO);
    adv(P);                                 /* GO */
    eat(P, A_TO, "expected TO after GO");
    kid(P, g, p_expr(P));                   /* designational expression */
    return g;
}

/* ---- Declaration grammar (Section 9) ---- */

static int is_type_tok(int t)
{
    return t == A_REAL || t == A_INTEGER || t == A_BOOLEAN || t == A_ALPHA;
}

/* tokens that can begin a declaration in a block head */
static int starts_decl(a_parser_t *P)
{
    int t = curt(P);
    return t == A_OWN || t == A_SAVE || is_type_tok(t) ||
           t == A_ARRAY || t == A_PROCEDURE || t == A_SWITCH;
}

/* array-list: comma-separated segments; each segment is one or more
 * identifiers sharing a bracketed bound-pair list (multi-dimensional via
 * comma-separated pairs). One AN_ARRAY_DECL per segment, chained by
 * next_sibling; the head is returned. aux bit0 = OWN, bit1 = SAVE. */
static uint32_t p_array_decl(a_parser_t *P, int elem, unsigned flags)
{
    uint32_t head = 0, tail = 0;
    for (;;) {
        uint32_t seg = alloc(P, AN_ARRAY_DECL);
        P->nodes[seg].op  = (uint16_t)elem;
        P->nodes[seg].aux = flags;
        for (;;) {                          /* identifiers up to the '[' */
            if (curt(P) == A_IDENT) {
                uint32_t v = alloc(P, AN_VAR);
                P->nodes[v].tok = P->pos; adv(P);
                kid(P, seg, v);
            } else perr(P, "expected array identifier");
            if (curt(P) == A_COMMA) { adv(P); continue; }
            break;
        }
        eat(P, A_LBRACK, "expected '[' for array bounds");
        for (;;) {                          /* bound-pair-list */
            uint32_t bp = alloc(P, AN_BOUND_PAIR);
            kid(P, bp, p_expr(P));           /* lower bound */
            eat(P, A_COLON, "expected ':' in bound pair");
            kid(P, bp, p_expr(P));           /* upper bound */
            kid(P, seg, bp);
            if (curt(P) == A_COMMA) { adv(P); continue; }
            break;
        }
        eat(P, A_RBRACK, "expected ']' after array bounds");
        if (!head) head = seg; else P->nodes[tail].next_sibling = seg;
        tail = seg;
        if (curt(P) == A_COMMA) { adv(P); continue; }  /* next segment */
        break;
    }
    return head;
}

/* Do two identifier tokens spell the same name? Local resolution within a
 * procedure heading (matching VALUE / specification ids to formals). */
static int same_tok_text(const a_parser_t *P, uint32_t a, uint32_t b)
{
    const a_token_t *ta = &P->toks[a], *tb = &P->toks[b];
    if (ta->len != tb->len) return 0;
    return memcmp(P->src + ta->offset, P->src + tb->offset, ta->len) == 0;
}

/* tokens that begin a specification inside a procedure heading */
static int is_specifier(a_parser_t *P)
{
    int t = curt(P);
    return is_type_tok(t) || t == A_ARRAY || t == A_LABEL ||
           t == A_SWITCH || t == A_PROCEDURE || t == A_FILE ||
           t == A_LIST || t == A_FORMAT;
}

/* procedure declaration (Section 10).
 *   [type] PROCEDURE name (formals) ; [VALUE ids ;] [specifications] body
 * The formal list is comma-separated (the Burroughs simplification). VALUE
 * marks by-value formals; the rest are by-name. The specification part is
 * resolved into the AN_PARAM nodes. Caller has parsed any leading type.
 * AN_PROC_DECL: op = return type (0 = proper), tok = name, children =
 * AN_PARAM... then the body. AN_PARAM: tok = name, op = specified type,
 * aux bit0 = by-value, bit1 = array. */
#define A_PROC_MAXP 64
static uint32_t p_proc_decl(a_parser_t *P, int return_type)
{
    uint32_t pd = alloc(P, AN_PROC_DECL);
    uint32_t pnode[A_PROC_MAXP], pname[A_PROC_MAXP];
    int np = 0, i;

    P->nodes[pd].op = (uint16_t)return_type;     /* 0 = proper procedure */
    adv(P);                                       /* PROCEDURE */

    if (curt(P) == A_IDENT) { P->nodes[pd].tok = P->pos; adv(P); }
    else perr(P, "expected procedure name");

    if (curt(P) == A_LPAREN) {                    /* formal parameter part */
        adv(P);
        if (curt(P) != A_RPAREN) for (;;) {
            if (curt(P) == A_IDENT) {
                if (np < A_PROC_MAXP) {
                    uint32_t pm = alloc(P, AN_PARAM);
                    P->nodes[pm].tok = P->pos;
                    pnode[np] = pm; pname[np] = P->pos; np++;
                }
                adv(P);
            } else perr(P, "expected formal parameter");
            if (curt(P) == A_COMMA) { adv(P); continue; }
            break;
        }
        eat(P, A_RPAREN, "expected ')' after parameters");
    }
    eat(P, A_SEMI, "expected ';' after procedure heading");

    if (curt(P) == A_VALUE) {                     /* VALUE part */
        adv(P);
        for (;;) {
            if (curt(P) == A_IDENT) {
                for (i = 0; i < np; i++)
                    if (same_tok_text(P, pname[i], P->pos))
                        P->nodes[pnode[i]].aux |= 1u;        /* by-value */
                adv(P);
            } else perr(P, "expected identifier in VALUE part");
            if (curt(P) == A_COMMA) { adv(P); continue; }
            break;
        }
        eat(P, A_SEMI, "expected ';' after VALUE part");
    }

    while (is_specifier(P)) {                      /* specification part */
        int spec_ty = 0, is_array = 0;
        if (is_type_tok(curt(P))) { spec_ty = curt(P); adv(P); }
        if (curt(P) == A_ARRAY) { is_array = 1; adv(P); }
        else if (!spec_ty) adv(P);                 /* LABEL/SWITCH/PROCEDURE/... */
        for (;;) {
            if (curt(P) == A_IDENT) {
                for (i = 0; i < np; i++)
                    if (same_tok_text(P, pname[i], P->pos)) {
                        if (spec_ty)  P->nodes[pnode[i]].op = (uint16_t)spec_ty;
                        if (is_array) P->nodes[pnode[i]].aux |= 2u;   /* array */
                    }
                adv(P);
            } else perr(P, "expected identifier in specification");
            if (curt(P) == A_COMMA) { adv(P); continue; }
            break;
        }
        if (is_array && curt(P) == A_LBRACK) {     /* lower-bound list */
            adv(P);
            while (curt(P) != A_RBRACK && curt(P) != A_SEMI && curt(P) != A_EOF)
                adv(P);
            eat(P, A_RBRACK, "expected ']' in array specification");
        }
        eat(P, A_SEMI, "expected ';' after specification");
    }

    for (i = 0; i < np; i++) kid(P, pd, pnode[i]);
    kid(P, pd, p_stmt(P));                         /* procedure body */
    return pd;
}

/* declaration ::= [SAVE] [OWN] [type] ( type-list | ARRAY array-list
 *                 | PROCEDURE ... ). Switch declarations still fail loud. */
static uint32_t p_decl(a_parser_t *P)
{
    int own = 0, save = 0, ty = 0;
    unsigned flags;
    while (curt(P) == A_OWN || curt(P) == A_SAVE) {
        if (curt(P) == A_OWN) own = 1; else save = 1;
        adv(P);
    }
    if (is_type_tok(curt(P))) { ty = curt(P); adv(P); }

    if (curt(P) == A_ARRAY) {
        adv(P);
        flags = (own ? 1u : 0u) | (save ? 2u : 0u);
        return p_array_decl(P, ty ? ty : A_REAL, flags);
    }
    if (curt(P) == A_PROCEDURE) return p_proc_decl(P, ty);
    if (curt(P) == A_SWITCH) {
        perr(P, "switch declarations not yet implemented");
        while (curt(P) != A_SEMI && curt(P) != A_END && curt(P) != A_EOF)
            adv(P);
        return 0;
    }
    if (ty) {                               /* type declaration */
        uint32_t d = alloc(P, AN_TYPE_DECL);
        P->nodes[d].op  = (uint16_t)ty;
        P->nodes[d].aux = own ? 1u : 0u;
        for (;;) {
            if (curt(P) == A_IDENT) {
                uint32_t v = alloc(P, AN_VAR);
                P->nodes[v].tok = P->pos; adv(P);
                kid(P, d, v);
            } else perr(P, "expected identifier in type declaration");
            if (curt(P) == A_COMMA) { adv(P); continue; }
            break;
        }
        return d;
    }
    perr(P, "expected a declaration");
    return 0;
}

/* BEGIN [<declaration> ;]... <statement> [; <statement>]... END.
 * Any leading declarations make it a block (a new scope, Section 5);
 * otherwise it is a compound statement. */
static uint32_t p_block(a_parser_t *P)
{
    uint32_t node = alloc(P, AN_BLOCK);
    int has_decl = 0;
    adv(P);                                 /* BEGIN */
    while (starts_decl(P)) {
        has_decl = 1;
        kid(P, node, p_decl(P));            /* kid splices array-segment chains */
        if (curt(P) == A_SEMI) { adv(P); continue; }
        break;
    }
    while (curt(P) != A_END && curt(P) != A_EOF) {
        kid(P, node, p_stmt(P));
        if (curt(P) == A_SEMI) { adv(P); continue; }
        break;
    }
    eat(P, A_END, "expected END");
    if (!has_decl) P->nodes[node].kind = AN_COMPOUND;
    return node;
}

/* assignment (chained: A := B := expr) or a procedure statement */
static uint32_t p_assign_or_call(a_parser_t *P)
{
    uint32_t e = p_expr(P), asn, pc;
    if (curt(P) != A_ASSIGN) {              /* no ':=' -> procedure statement */
        pc = alloc(P, AN_PROC_CALL);
        kid(P, pc, e);
        return pc;
    }
    asn = alloc(P, AN_ASSIGN);
    kid(P, asn, e);                         /* first left part */
    while (curt(P) == A_ASSIGN) {
        adv(P);
        kid(P, asn, p_expr(P));
        if (curt(P) != A_ASSIGN) break;     /* the last parsed child is the source */
    }
    return asn;
}

static uint32_t p_stmt(a_parser_t *P)
{
    /* label : statement  ('label :' is A_COLON, distinct from ':=') */
    if (curt(P) == A_IDENT && peekt(P, 1) == A_COLON) {
        uint32_t lb = alloc(P, AN_LABELED);
        P->nodes[lb].tok = P->pos;          /* the label identifier */
        adv(P); adv(P);                     /* ident ':' */
        kid(P, lb, p_stmt(P));
        return lb;
    }
    switch (curt(P)) {
    case A_BEGIN: return p_block(P);
    case A_IF:    return p_if(P);
    case A_FOR:   return p_for(P);
    case A_GO:    return p_goto(P);
    case A_SEMI:                            /* empty statement */
    case A_END:   return alloc(P, AN_DUMMY);
    default:      return p_assign_or_call(P);
    }
}

uint32_t a_parse_expr(a_parser_t *P)
{
    return p_expr(P);
}

uint32_t a_parse_stmt(a_parser_t *P)
{
    return p_stmt(P);
}

uint32_t a_parse_decl(a_parser_t *P)
{
    return p_decl(P);
}

/* A whole compilation unit -- in practice the outer block. */
uint32_t a_parse_program(a_parser_t *P)
{
    return p_stmt(P);
}
