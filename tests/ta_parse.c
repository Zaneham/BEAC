/* ta_parse.c -- Extended ALGOL expression parser tests (under tharns).
 *
 * Lexes then parses a single expression and checks the AST shape, with
 * the precedence ladder as the headline: "*" is exponentiation, TIMES is
 * multiply, multiplying binds tighter than adding, relational sits under
 * the logicals, EQV is the lowest logical operator. Plus calls,
 * subscripts, the Burroughs partial-word designator, and the conditional
 * expression. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"

static a_token_t  a_toks[512];
static a_node_t   a_nodes[4096];
static a_lexer_t  aL;
static a_parser_t aP;

static uint32_t aparse(const char *src)
{
    a_lex_init(&aL, src, (uint32_t)strlen(src), a_toks, 512);
    a_lex_run(&aL);
    a_parse_init(&aP, a_toks, aL.num_toks, src, a_nodes, 4096);
    return a_parse_expr(&aP);
}

static int AK(uint32_t n)  { return aP.nodes[n].kind; }
static int AOP(uint32_t n) { return aP.nodes[n].op; }
static uint32_t ACH(uint32_t n, int i)
{
    uint32_t c = aP.nodes[n].first_child;
    while (i-- > 0 && c) c = aP.nodes[c].next_sibling;
    return c;
}
static int ANCH(uint32_t n)
{
    int k = 0; uint32_t c = aP.nodes[n].first_child;
    while (c) { k++; c = aP.nodes[c].next_sibling; }
    return k;
}

static void apr_add(void)
{
    uint32_t r = aparse("A + B");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_BINOP && AOP(r)==A_PLUS &&
          AK(ACH(r,0))==AN_VAR && AK(ACH(r,1))==AN_VAR);
    PASS();
}
TH_REG("aparse", apr_add)

static void apr_mul_over_add(void)
{
    uint32_t r = aparse("A + B TIMES C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_PLUS && AK(ACH(r,1))==AN_BINOP && AOP(ACH(r,1))==A_TIMES);
    PASS();
}
TH_REG("aparse", apr_mul_over_add)

static void apr_mul_then_add(void)
{
    uint32_t r = aparse("A TIMES B + C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_PLUS && AOP(ACH(r,0))==A_TIMES);
    PASS();
}
TH_REG("aparse", apr_mul_then_add)

static void apr_pow_is_star(void)
{
    uint32_t r = aparse("A * B");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_BINOP && AOP(r)==A_POW);
    PASS();
}
TH_REG("aparse", apr_pow_is_star)

static void apr_add_left_assoc(void)
{
    uint32_t r = aparse("A + B + C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_PLUS && AOP(ACH(r,0))==A_PLUS);
    PASS();
}
TH_REG("aparse", apr_add_left_assoc)

static void apr_unary_minus(void)
{
    uint32_t r = aparse("-A + B");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_PLUS && AK(ACH(r,0))==AN_UNOP && AOP(ACH(r,0))==A_MINUS);
    PASS();
}
TH_REG("aparse", apr_unary_minus)

static void apr_rel_under_and(void)
{
    uint32_t r = aparse("A < B AND C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_AND && AOP(ACH(r,0))==A_LSS);
    PASS();
}
TH_REG("aparse", apr_rel_under_and)

static void apr_rel_word_form(void)
{
    uint32_t r = aparse("A LSS B AND C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_AND && AOP(ACH(r,0))==A_LSS);
    PASS();
}
TH_REG("aparse", apr_rel_word_form)

static void apr_not(void)
{
    uint32_t r = aparse("NOT A");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_UNOP && AOP(r)==A_NOT);
    PASS();
}
TH_REG("aparse", apr_not)

static void apr_or_under_and(void)
{
    uint32_t r = aparse("A AND B OR C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_OR && AOP(ACH(r,0))==A_AND);
    PASS();
}
TH_REG("aparse", apr_or_under_and)

static void apr_eqv_lowest(void)
{
    uint32_t r = aparse("A EQV B IMP C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_EQV && AOP(ACH(r,1))==A_IMP);
    PASS();
}
TH_REG("aparse", apr_eqv_lowest)

static void apr_cond_expr(void)
{
    uint32_t r = aparse("IF A THEN B ELSE C");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_COND_EXPR && ANCH(r)==3);
    PASS();
}
TH_REG("aparse", apr_cond_expr)

static void apr_cond_chain(void)
{
    uint32_t r = aparse("IF A THEN B ELSE IF C THEN D ELSE E");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_COND_EXPR && AK(ACH(r,2))==AN_COND_EXPR);
    PASS();
}
TH_REG("aparse", apr_cond_chain)

static void apr_call(void)
{
    uint32_t r = aparse("F(X, Y)");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_CALL && ANCH(r)==3 && AK(ACH(r,0))==AN_VAR);
    PASS();
}
TH_REG("aparse", apr_call)

static void apr_index(void)
{
    uint32_t r = aparse("T[I, J]");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_INDEX && ANCH(r)==3);
    PASS();
}
TH_REG("aparse", apr_index)

static void apr_partial_word(void)
{
    uint32_t r = aparse("Q.[16:1]");
    CHECK(aP.n_errs == 0);
    CHECK(AK(r)==AN_PARTIAL && ANCH(r)==3);
    PASS();
}
TH_REG("aparse", apr_partial_word)

static void apr_paren(void)
{
    uint32_t r = aparse("(A + B) TIMES C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_TIMES && AOP(ACH(r,0))==A_PLUS);
    PASS();
}
TH_REG("aparse", apr_paren)

static void apr_pow_over_mul(void)
{
    uint32_t r = aparse("A TIMES B * C");
    CHECK(aP.n_errs == 0);
    CHECK(AOP(r)==A_TIMES && AOP(ACH(r,1))==A_POW);
    PASS();
}
TH_REG("aparse", apr_pow_over_mul)
