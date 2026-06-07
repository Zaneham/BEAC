/* ta_proc.c -- Extended ALGOL procedure declaration tests (under tharns).
 *
 * Section 10: proper procedures vs typed functions, comma-separated
 * formals, the VALUE part (by-value vs by-name), the specification part
 * folded back into the formals (type + array flag), and procedures
 * declared inside a block head. AN_PROC_DECL.op = return type (0 =
 * proper); AN_PARAM.op = specified type, aux bit0 = by-value, bit1 = array. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"

static a_token_t  q_toks[1024];
static a_node_t   q_nodes[8192];
static a_lexer_t  qL;
static a_parser_t qP;

static void q_setup(const char *src)
{
    a_lex_init(&qL, src, (uint32_t)strlen(src), q_toks, 1024);
    a_lex_run(&qL);
    a_parse_init(&qP, q_toks, qL.num_toks, src, q_nodes, 8192);
}
static uint32_t pdecl(const char *src) { q_setup(src); return a_parse_decl(&qP); }
static uint32_t pprog(const char *src) { q_setup(src); return a_parse_program(&qP); }

static int QK(uint32_t n)  { return qP.nodes[n].kind; }
static int QOP(uint32_t n) { return qP.nodes[n].op; }
static unsigned QAUX(uint32_t n) { return qP.nodes[n].aux; }
static uint32_t QCH(uint32_t n, int i)
{
    uint32_t c = qP.nodes[n].first_child;
    while (i-- > 0 && c) c = qP.nodes[c].next_sibling;
    return c;
}
static uint32_t QLAST(uint32_t n)
{
    uint32_t c = qP.nodes[n].first_child, last = 0;
    while (c) { last = c; c = qP.nodes[c].next_sibling; }
    return last;
}

static void aqp_proper(void)
{
    uint32_t r = pdecl("PROCEDURE INIT; BEGIN A := 1 END");
    CHECK(qP.n_errs == 0);
    CHECK(QK(r)==AN_PROC_DECL && QOP(r)==0 &&          /* proper: no type */
          QK(QLAST(r))==AN_COMPOUND);                   /* body */
    PASS();
}
TH_REG("aproc", aqp_proper)

static void aqp_typed_function(void)
{
    uint32_t r = pdecl("REAL PROCEDURE SQ(X); VALUE X; REAL X; "
                       "BEGIN Y := X TIMES X END");
    CHECK(qP.n_errs == 0);
    CHECK(QK(r)==AN_PROC_DECL && QOP(r)==A_REAL);       /* typed function */
    CHECK(QK(QCH(r,0))==AN_PARAM && QOP(QCH(r,0))==A_REAL &&
          (QAUX(QCH(r,0))&1u));                         /* X: REAL, by-value */
    PASS();
}
TH_REG("aproc", aqp_typed_function)

static void aqp_value_and_name(void)
{
    /* N is by-value (INTEGER); A and B are by-name (REAL) */
    uint32_t r = pdecl("PROCEDURE ROOT(A, B, N); VALUE N; "
                       "INTEGER N; REAL A, B; BEGIN A := B END");
    CHECK(qP.n_errs == 0);
    CHECK(QK(QCH(r,0))==AN_PARAM && QOP(QCH(r,0))==A_REAL &&
          (QAUX(QCH(r,0))&1u)==0);                      /* A: REAL, by-name */
    CHECK(QOP(QCH(r,2))==A_INTEGER && (QAUX(QCH(r,2))&1u)); /* N: INT, by-value */
    PASS();
}
TH_REG("aproc", aqp_value_and_name)

static void aqp_array_param(void)
{
    uint32_t r = pdecl("PROCEDURE SUM(V, N); VALUE N; "
                       "INTEGER N; REAL ARRAY V [1]; BEGIN S := 0 END");
    CHECK(qP.n_errs == 0);
    CHECK(QK(QCH(r,0))==AN_PARAM && QOP(QCH(r,0))==A_REAL &&
          (QAUX(QCH(r,0))&2u));                         /* V: REAL array */
    PASS();
}
TH_REG("aproc", aqp_array_param)

static void aqp_body_block(void)
{
    /* body is a block (it has its own declarations) */
    uint32_t r = pdecl("PROCEDURE P(X); REAL X; "
                       "BEGIN INTEGER I; I := X END");
    CHECK(qP.n_errs == 0);
    CHECK(QK(QLAST(r))==AN_BLOCK);
    PASS();
}
TH_REG("aproc", aqp_body_block)

static void aqp_no_params(void)
{
    uint32_t r = pdecl("PROCEDURE TICK; T := T + 1");
    CHECK(qP.n_errs == 0);
    CHECK(QK(r)==AN_PROC_DECL && QK(QLAST(r))==AN_ASSIGN);  /* bare-stmt body */
    PASS();
}
TH_REG("aproc", aqp_no_params)

static void aqp_in_block(void)
{
    /* a procedure declared in a block head, then called */
    uint32_t r = pprog(
        "BEGIN REAL PROCEDURE SQ(X); VALUE X; REAL X; "
        "BEGIN SQ := X TIMES X END; Y := SQ(2) END");
    CHECK(qP.n_errs == 0);
    CHECK(QK(r)==AN_BLOCK && QK(QCH(r,0))==AN_PROC_DECL &&
          QK(QCH(r,1))==AN_ASSIGN);
    PASS();
}
TH_REG("aproc", aqp_in_block)
