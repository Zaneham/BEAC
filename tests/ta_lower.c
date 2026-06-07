/* ta_lower.c -- ALGOL -> JIR lowering tests (under tharns).
 *
 * Lowers small programs and inspects the emitted JIR: that the program
 * becomes a "main", that declarations produce allocas, assignments produce
 * stores, integer vs real arithmetic pick the right ops, a variable use
 * loads, and chained assignment stores the one value to every target. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"
#include "a_sema.h"
#include "a_types.h"
#include "a_lower.h"

static a_token_t   l_toks[1024];
static a_node_t    l_nodes[8192];
static a_lexer_t   lL;
static a_parser_t  lP;
static a_sema_ctx_t lSem;
static sema_ctx_t  l_js;
static a_typemap_t l_tm;
static jir_mod_t   lM;

static void run(const char *src)
{
    uint32_t root;
    a_lex_init(&lL, src, (uint32_t)strlen(src), l_toks, 1024);
    a_lex_run(&lL);
    a_parse_init(&lP, l_toks, lL.num_toks, src, l_nodes, 8192);
    root = a_parse_program(&lP);
    a_sema_init(&lSem, lP.nodes, lP.n_nodes, l_toks, src);
    a_sema_run(&lSem, root);
    a_types_init(&l_tm, &l_js, A_FP_NATIVE);
    a_lower(&lM, &l_js, &lSem, &l_tm, root);
}

static int count_op(int op)
{
    int c = 0; uint32_t i;
    for (i = 0; i < lM.n_inst; i++) if (lM.insts[i].op == op) c++;
    return c;
}
static int main_named(void)
{
    return lM.n_funcs == 1 &&
           strcmp(lM.strs + lM.funcs[0].name, "main") == 0;
}

static void alw_main_shape(void)
{
    run("BEGIN INTEGER I; I := 21 + 21 END");
    CHECK(main_named());
    CHECK(count_op(JIR_ALLOCA) == 0);   /* I is a global now, not a frame slot */
    CHECK(count_op(JIR_GADDR) >= 1);    /* reached via the global area */
    CHECK(count_op(JIR_ADD) == 1);      /* 21 + 21 */
    CHECK(count_op(JIR_STORE) == 1);    /* I := */
    CHECK(count_op(JIR_RET) >= 1);      /* trailing ret */
    PASS();
}
TH_REG("alower", alw_main_shape)

static void alw_two_vars(void)
{
    run("BEGIN INTEGER A; INTEGER B; A := 2; B := A + 3 END");
    CHECK(main_named());
    CHECK(count_op(JIR_ALLOCA) == 0);   /* A, B are globals now */
    CHECK(count_op(JIR_LOAD) == 1);     /* A in A + 3 */
    CHECK(count_op(JIR_ADD) == 1);
    CHECK(count_op(JIR_STORE) == 2);
    PASS();
}
TH_REG("alower", alw_two_vars)

static void alw_real_arith(void)
{
    run("BEGIN REAL X; X := 1.5 + 2.5 END");
    CHECK(main_named());
    CHECK(count_op(JIR_FADD) == 1);     /* real addition */
    CHECK(count_op(JIR_ADD) == 0);
    PASS();
}
TH_REG("alower", alw_real_arith)

static void alw_chained(void)
{
    /* one value stored to both targets */
    run("BEGIN INTEGER A; INTEGER B; A := B := 5 END");
    CHECK(main_named());
    CHECK(count_op(JIR_STORE) == 2);
    PASS();
}
TH_REG("alower", alw_chained)

static void alw_relational(void)
{
    /* a relational expression lowers to an integer compare */
    run("BEGIN INTEGER I; BOOLEAN P; I := 1; P := I < 2 END");
    CHECK(main_named());
    CHECK(count_op(JIR_ICMP) == 1);
    PASS();
}
TH_REG("alower", alw_relational)
