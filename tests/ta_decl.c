/* ta_decl.c -- Extended ALGOL declaration + block tests (under tharns).
 *
 * Type declarations (incl. OWN), array declarations (multi-dim bounds,
 * shared-bounds segments, SAVE/OWN flags, default REAL), and the block
 * vs compound-statement distinction: BEGIN with leading declarations is a
 * block (new scope), without is a compound statement. Section 5 and 9. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"

static a_token_t  d_toks[512];
static a_node_t   d_nodes[4096];
static a_lexer_t  dL;
static a_parser_t dP;

static void d_setup(const char *src)
{
    a_lex_init(&dL, src, (uint32_t)strlen(src), d_toks, 512);
    a_lex_run(&dL);
    a_parse_init(&dP, d_toks, dL.num_toks, src, d_nodes, 4096);
}
static uint32_t pdecl(const char *src) { d_setup(src); return a_parse_decl(&dP); }
static uint32_t pprog(const char *src) { d_setup(src); return a_parse_program(&dP); }

static int DK(uint32_t n)  { return dP.nodes[n].kind; }
static int DOP(uint32_t n) { return dP.nodes[n].op; }
static unsigned DAUX(uint32_t n) { return dP.nodes[n].aux; }
static uint32_t DCH(uint32_t n, int i)
{
    uint32_t c = dP.nodes[n].first_child;
    while (i-- > 0 && c) c = dP.nodes[c].next_sibling;
    return c;
}
static int DNCH(uint32_t n)
{
    int k = 0; uint32_t c = dP.nodes[n].first_child;
    while (c) { k++; c = dP.nodes[c].next_sibling; }
    return k;
}

static void adc_type(void)
{
    uint32_t r = pdecl("INTEGER A, B, C");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_TYPE_DECL && DOP(r)==A_INTEGER && DAUX(r)==0 &&
          DNCH(r)==3 && DK(DCH(r,0))==AN_VAR);
    PASS();
}
TH_REG("adecl", adc_type)

static void adc_type_own(void)
{
    uint32_t r = pdecl("OWN REAL Q, R");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_TYPE_DECL && DOP(r)==A_REAL && (DAUX(r)&1u) &&
          DNCH(r)==2);
    PASS();
}
TH_REG("adecl", adc_type_own)

static void adc_array_1d(void)
{
    uint32_t r = pdecl("INTEGER ARRAY M [1:10]");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_ARRAY_DECL && DOP(r)==A_INTEGER &&
          DK(DCH(r,0))==AN_VAR && DK(DCH(r,1))==AN_BOUND_PAIR);
    PASS();
}
TH_REG("adecl", adc_array_1d)

static void adc_array_2d(void)
{
    uint32_t r = pdecl("BOOLEAN ARRAY GATE [1:10, 3:9]");
    CHECK(dP.n_errs == 0);
    /* one name + two bound pairs */
    CHECK(DK(r)==AN_ARRAY_DECL && DOP(r)==A_BOOLEAN && DNCH(r)==3 &&
          DK(DCH(r,1))==AN_BOUND_PAIR && DK(DCH(r,2))==AN_BOUND_PAIR);
    PASS();
}
TH_REG("adecl", adc_array_2d)

static void adc_array_shared(void)
{
    uint32_t r = pdecl("REAL ARRAY A, B [1:5]");
    CHECK(dP.n_errs == 0);
    /* two names sharing one bound pair */
    CHECK(DK(r)==AN_ARRAY_DECL && DNCH(r)==3 &&
          DK(DCH(r,0))==AN_VAR && DK(DCH(r,1))==AN_VAR &&
          DK(DCH(r,2))==AN_BOUND_PAIR);
    PASS();
}
TH_REG("adecl", adc_array_shared)

static void adc_array_save_own(void)
{
    uint32_t r = pdecl("SAVE OWN BOOLEAN ARRAY G [1:10]");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_ARRAY_DECL && (DAUX(r)&1u) && (DAUX(r)&2u));
    PASS();
}
TH_REG("adecl", adc_array_save_own)

static void adc_array_default_real(void)
{
    uint32_t r = pdecl("ARRAY X [0:9]");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_ARRAY_DECL && DOP(r)==A_REAL);
    PASS();
}
TH_REG("adecl", adc_array_default_real)

static void adc_array_segments(void)
{
    /* two segments, each its own bounds: chained AN_ARRAY_DECL nodes */
    uint32_t r = pdecl("REAL ARRAY M [1:5], G [0:9]");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_ARRAY_DECL &&
          DK(dP.nodes[r].next_sibling)==AN_ARRAY_DECL);
    PASS();
}
TH_REG("adecl", adc_array_segments)

static void adc_block_with_decls(void)
{
    uint32_t r = pprog("BEGIN INTEGER I; I := 1 END");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_BLOCK && DK(DCH(r,0))==AN_TYPE_DECL &&
          DK(DCH(r,1))==AN_ASSIGN);
    PASS();
}
TH_REG("adecl", adc_block_with_decls)

static void adc_compound_no_decls(void)
{
    /* no declarations -> compound statement, not a block */
    uint32_t r = pprog("BEGIN A := 1; B := 2 END");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_COMPOUND);
    PASS();
}
TH_REG("adecl", adc_compound_no_decls)

static void adc_block_array_stmts(void)
{
    uint32_t r = pprog("BEGIN REAL ARRAY V [1:10]; V[1] := 0 END");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_BLOCK && DK(DCH(r,0))==AN_ARRAY_DECL &&
          DK(DCH(r,1))==AN_ASSIGN);
    PASS();
}
TH_REG("adecl", adc_block_array_stmts)

static void adc_block_multi_decl(void)
{
    uint32_t r = pprog("BEGIN INTEGER I; REAL X; I := 1 END");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_BLOCK && DK(DCH(r,0))==AN_TYPE_DECL &&
          DK(DCH(r,1))==AN_TYPE_DECL && DK(DCH(r,2))==AN_ASSIGN);
    PASS();
}
TH_REG("adecl", adc_block_multi_decl)

static void adc_program(void)
{
    /* a small whole program: block, decl, a FOR loop */
    uint32_t r = pprog(
        "BEGIN INTEGER I; FOR I := 1 STEP 1 UNTIL 10 DO S := S + I END");
    CHECK(dP.n_errs == 0);
    CHECK(DK(r)==AN_BLOCK && DK(DCH(r,0))==AN_TYPE_DECL &&
          DK(DCH(r,1))==AN_FOR);
    PASS();
}
TH_REG("adecl", adc_program)
