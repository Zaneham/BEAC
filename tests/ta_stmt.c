/* ta_stmt.c -- Extended ALGOL statement parser tests (under tharns).
 *
 * Covers assignment (incl. chained A := B := e), procedure statements,
 * GO TO, labelled statements, the compound statement, and the IF and FOR
 * statements with their for-list element forms (bare / STEP..UNTIL /
 * STEP..WHILE / WHILE). Nearest-ELSE binding checked too. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"

static a_token_t  s_toks[512];
static a_node_t   s_nodes[4096];
static a_lexer_t  sL;
static a_parser_t sP;

static uint32_t pstmt(const char *src)
{
    a_lex_init(&sL, src, (uint32_t)strlen(src), s_toks, 512);
    a_lex_run(&sL);
    a_parse_init(&sP, s_toks, sL.num_toks, src, s_nodes, 4096);
    return a_parse_stmt(&sP);
}

static int SK_(uint32_t n)  { return sP.nodes[n].kind; }
static int SOP(uint32_t n)  { return sP.nodes[n].op; }
static uint32_t SCH(uint32_t n, int i)
{
    uint32_t c = sP.nodes[n].first_child;
    while (i-- > 0 && c) c = sP.nodes[c].next_sibling;
    return c;
}
static int SNCH(uint32_t n)
{
    int k = 0; uint32_t c = sP.nodes[n].first_child;
    while (c) { k++; c = sP.nodes[c].next_sibling; }
    return k;
}

static void ast_assign(void)
{
    uint32_t r = pstmt("A := 1");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_ASSIGN && SNCH(r)==2 &&
          SK_(SCH(r,0))==AN_VAR && SK_(SCH(r,1))==AN_NUM);
    PASS();
}
TH_REG("astmt", ast_assign)

static void ast_assign_chained(void)
{
    uint32_t r = pstmt("A := B := 1");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_ASSIGN && SNCH(r)==3 &&     /* A, B, then source */
          SK_(SCH(r,0))==AN_VAR && SK_(SCH(r,1))==AN_VAR &&
          SK_(SCH(r,2))==AN_NUM);
    PASS();
}
TH_REG("astmt", ast_assign_chained)

static void ast_assign_subscript(void)
{
    uint32_t r = pstmt("T[I] := X");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_ASSIGN && SK_(SCH(r,0))==AN_INDEX);
    PASS();
}
TH_REG("astmt", ast_assign_subscript)

static void ast_assign_partial(void)
{
    uint32_t r = pstmt("Q.[16:1] := P");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_ASSIGN && SK_(SCH(r,0))==AN_PARTIAL);
    PASS();
}
TH_REG("astmt", ast_assign_partial)

static void ast_proc_call(void)
{
    uint32_t r = pstmt("P(X, Y)");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_PROC_CALL && SK_(SCH(r,0))==AN_CALL);
    PASS();
}
TH_REG("astmt", ast_proc_call)

static void ast_proc_bare(void)
{
    uint32_t r = pstmt("INIT");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_PROC_CALL && SK_(SCH(r,0))==AN_VAR);
    PASS();
}
TH_REG("astmt", ast_proc_bare)

static void ast_goto(void)
{
    uint32_t r = pstmt("GO TO DONE");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_GOTO && SK_(SCH(r,0))==AN_VAR);
    PASS();
}
TH_REG("astmt", ast_goto)

static void ast_labelled(void)
{
    uint32_t r = pstmt("LOOP: A := 1");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_LABELED && SK_(SCH(r,0))==AN_ASSIGN);
    PASS();
}
TH_REG("astmt", ast_labelled)

static void ast_compound(void)
{
    uint32_t r = pstmt("BEGIN A := 1; B := 2 END");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_COMPOUND && SNCH(r)==2 &&
          SK_(SCH(r,0))==AN_ASSIGN && SK_(SCH(r,1))==AN_ASSIGN);
    PASS();
}
TH_REG("astmt", ast_compound)

static void ast_if(void)
{
    uint32_t r = pstmt("IF A THEN B := 1");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_IF_STMT && SNCH(r)==2);   /* cond, then */
    PASS();
}
TH_REG("astmt", ast_if)

static void ast_if_else(void)
{
    uint32_t r = pstmt("IF A THEN B := 1 ELSE C := 2");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_IF_STMT && SNCH(r)==3);   /* cond, then, else */
    PASS();
}
TH_REG("astmt", ast_if_else)

static void ast_if_nested_else(void)
{
    /* nearest-ELSE: the ELSE binds to the inner IF */
    uint32_t r = pstmt("IF A THEN IF B THEN S := 1 ELSE S := 2");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_IF_STMT && SNCH(r)==2 &&   /* outer has no ELSE */
          SK_(SCH(r,1))==AN_IF_STMT && SNCH(SCH(r,1))==3);
    PASS();
}
TH_REG("astmt", ast_if_nested_else)

static void ast_for_simple(void)
{
    uint32_t r = pstmt("FOR I := 1 DO S := 0");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_FOR && SK_(SCH(r,0))==AN_VAR &&
          SK_(SCH(r,1))==AN_FOR_ELEM && SK_(SCH(r,2))==AN_ASSIGN);
    PASS();
}
TH_REG("astmt", ast_for_simple)

static void ast_for_step(void)
{
    uint32_t r = pstmt("FOR I := 1 STEP 1 UNTIL N DO S := 0");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_FOR && SK_(SCH(r,1))==AN_FOR_STEP &&
          SNCH(SCH(r,1))==3);                  /* val, step, until */
    PASS();
}
TH_REG("astmt", ast_for_step)

static void ast_for_while(void)
{
    uint32_t r = pstmt("FOR I := 1 WHILE A < B DO S := 0");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_FOR && SK_(SCH(r,1))==AN_FOR_WHILE);
    PASS();
}
TH_REG("astmt", ast_for_while)

static void ast_for_list(void)
{
    uint32_t r = pstmt("FOR K := A, 1 STEP 1 UNTIL N DO P := 0");
    CHECK(sP.n_errs == 0);
    CHECK(SK_(r)==AN_FOR && SK_(SCH(r,1))==AN_FOR_ELEM &&
          SK_(SCH(r,2))==AN_FOR_STEP);         /* two list elements */
    PASS();
}
TH_REG("astmt", ast_for_list)
