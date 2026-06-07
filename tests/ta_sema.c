/* ta_sema.c -- Extended ALGOL semantic analysis tests (under tharns).
 *
 * Identifier resolution, undeclared detection, redeclaration, expression
 * typing (INTEGER vs REAL vs BOOLEAN), standard-function recognition, and
 * the headline M4 groundwork: a reference that reaches across a procedure
 * boundary is flagged non-local, one that stays inside the frame is not. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"
#include "a_sema.h"

static a_token_t   m_toks[1024];
static a_node_t    m_nodes[8192];
static a_lexer_t   mL;
static a_parser_t  mP;
static a_sema_ctx_t mS;
static const char *g_src;

static void run(const char *src)
{
    uint32_t root;
    g_src = src;
    a_lex_init(&mL, src, (uint32_t)strlen(src), m_toks, 1024);
    a_lex_run(&mL);
    a_parse_init(&mP, m_toks, mL.num_toks, src, m_nodes, 8192);
    root = a_parse_program(&mP);
    a_sema_init(&mS, mP.nodes, mP.n_nodes, m_toks, src);
    a_sema_run(&mS, root);
}

/* first resolved AN_VAR use spelled `name` (declarations have nsym 0) */
static uint32_t find_use(const char *name)
{
    size_t len = strlen(name);
    uint32_t i;
    for (i = 1; i < mP.n_nodes; i++) {
        const a_token_t *t;
        if (mP.nodes[i].kind != AN_VAR || mS.nsym[i] == 0) continue;
        t = &m_toks[mP.nodes[i].tok];
        if (t->len == len && memcmp(g_src + t->offset, name, len) == 0) return i;
    }
    return 0;
}
static uint32_t find_kind(int kind)
{
    uint32_t i;
    for (i = 1; i < mP.n_nodes; i++)
        if (mP.nodes[i].kind == kind) return i;
    return 0;
}

static void asm_resolves(void)
{
    run("BEGIN INTEGER I; I := I + 1 END");
    CHECK(mS.n_errs == 0);
    CHECK(find_use("I") != 0);          /* the use resolved to a symbol */
    PASS();
}
TH_REG("asema", asm_resolves)

static void asm_type_int(void)
{
    run("BEGIN INTEGER I; I := I + 1 END");
    CHECK(mS.n_errs == 0);
    CHECK(mS.ntype[find_kind(AN_BINOP)] == AT_INTEGER);
    PASS();
}
TH_REG("asema", asm_type_int)

static void asm_type_real(void)
{
    run("BEGIN REAL X; X := X + 1 END");
    CHECK(mS.n_errs == 0);
    CHECK(mS.ntype[find_kind(AN_BINOP)] == AT_REAL);
    PASS();
}
TH_REG("asema", asm_type_real)

static void asm_type_bool(void)
{
    run("BEGIN INTEGER I; IF I < 1 THEN I := 0 END");
    CHECK(mS.n_errs == 0);
    CHECK(mS.ntype[find_kind(AN_BINOP)] == AT_BOOLEAN);  /* relational */
    PASS();
}
TH_REG("asema", asm_type_bool)

static void asm_undeclared(void)
{
    run("BEGIN INTEGER I; I := Z END");
    CHECK(mS.n_errs >= 1);              /* Z is undeclared */
    PASS();
}
TH_REG("asema", asm_undeclared)

static void asm_redeclared(void)
{
    run("BEGIN INTEGER I; INTEGER I; I := 1 END");
    CHECK(mS.n_errs >= 1);              /* I declared twice in one block */
    PASS();
}
TH_REG("asema", asm_redeclared)

static void asm_builtin(void)
{
    run("BEGIN REAL X; X := SQRT(X) END");
    CHECK(mS.n_errs == 0);             /* SQRT is a standard function */
    PASS();
}
TH_REG("asema", asm_builtin)

static void asm_nonlocal(void)
{
    /* G is declared in the outer block; the procedure references it,
     * crossing a procedure boundary -> non-local. */
    run("BEGIN INTEGER G; PROCEDURE P; G := G + 1; P END");
    CHECK(mS.n_errs == 0);
    CHECK(mS.nonlocal[find_use("G")] == 1);
    PASS();
}
TH_REG("asema", asm_nonlocal)

static void asm_local(void)
{
    /* L is declared inside the procedure's own block -> local, same frame */
    run("BEGIN PROCEDURE P; BEGIN INTEGER L; L := 1 END; P END");
    CHECK(mS.n_errs == 0);
    CHECK(mS.nonlocal[find_use("L")] == 0);
    PASS();
}
TH_REG("asema", asm_local)
