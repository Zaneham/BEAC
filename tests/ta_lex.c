/* ta_lex.c -- Extended ALGOL lexer tests (under tharns).
 *
 * Each case checks the exact token-type stream plus the error count, with
 * the manual-specific quirks as the headline asserts: "@" exponents,
 * "*" = power, word/symbol operator equivalence, and the three comment
 * forms (COMMENT...;, BEGIN COMMENT, END-comment). */

#include "tharns.h"
#include "a_lex.h"

/* Returns 1 if `src` lexes to exactly the token types in `exp` (terminated
 * by -1) with `expect_errs` errors (-1 to skip the error check). */
static int lex_match(const char *src, const int *exp, int expect_errs)
{
    static a_token_t toks[256];
    static a_lexer_t L;
    int i = 0;
    a_lex_init(&L, src, (uint32_t)strlen(src), toks, 256);
    a_lex_run(&L);
    for (; exp[i] != -1; i++)
        if ((uint32_t)i >= L.num_toks || L.toks[i].type != exp[i]) return 0;
    if ((uint32_t)i != L.num_toks) return 0;
    if (expect_errs >= 0 && L.num_errs != expect_errs) return 0;
    return 1;
}

static void alx_block_assign(void)
{
    int e[] = { A_BEGIN, A_INTEGER, A_IDENT, A_SEMI, A_IDENT, A_ASSIGN,
                A_INTLIT, A_SEMI, A_END, A_EOF, -1 };
    CHECK(lex_match("BEGIN INTEGER I; I := 1; END", e, 0));
    PASS();
}
TH_REG("alex", alx_block_assign)

static void alx_numbers_at_exp(void)
{
    int e[] = { A_INTLIT, A_REALLIT, A_REALLIT, A_REALLIT, A_EOF, -1 };
    CHECK(lex_match("1354 1354.543 8.758@-47 @68", e, 0));
    PASS();
}
TH_REG("alex", alx_numbers_at_exp)

static void alx_frac_leading_dot(void)
{
    int e[] = { A_REALLIT, A_EOF, -1 };
    CHECK(lex_match(".546", e, 0));
    PASS();
}
TH_REG("alex", alx_frac_leading_dot)

static void alx_dot_separator(void)
{
    int e[] = { A_IDENT, A_DOT, A_IDENT, A_EOF, -1 };
    CHECK(lex_match("REC.FLD", e, 0));
    PASS();
}
TH_REG("alex", alx_dot_separator)

static void alx_rel_symbol(void)
{
    int e[] = { A_IDENT, A_LSS, A_IDENT, A_EOF, -1 };
    CHECK(lex_match("A < B", e, 0));
    PASS();
}
TH_REG("alex", alx_rel_symbol)

static void alx_rel_word(void)
{
    int e[] = { A_IDENT, A_LSS, A_IDENT, A_EOF, -1 };
    CHECK(lex_match("A LSS B", e, 0));
    PASS();
}
TH_REG("alex", alx_rel_word)

static void alx_pow_vs_times(void)
{
    int e[] = { A_IDENT, A_POW, A_IDENT, A_TIMES, A_IDENT, A_EOF, -1 };
    CHECK(lex_match("A * B TIMES C", e, 0));
    PASS();
}
TH_REG("alex", alx_pow_vs_times)

static void alx_multichar_ops(void)
{
    int e[] = { A_LEQ, A_GEQ, A_NEQ, A_ASSIGN, A_ARROW, A_EOF, -1 };
    CHECK(lex_match("<= >= <> := <-", e, 0));
    PASS();
}
TH_REG("alex", alx_multichar_ops)

static void alx_logical_words(void)
{
    int e[] = { A_IDENT, A_AND, A_IDENT, A_OR, A_NOT, A_IDENT, A_EOF, -1 };
    CHECK(lex_match("P AND Q OR NOT R", e, 0));
    PASS();
}
TH_REG("alex", alx_logical_words)

static void alx_string_lit(void)
{
    int e[] = { A_STRLIT, A_EOF, -1 };
    CHECK(lex_match("\"ALGOL\"", e, 0));
    PASS();
}
TH_REG("alex", alx_string_lit)

static void alx_string_dquote(void)
{
    int e[] = { A_STRLIT, A_EOF, -1 };
    CHECK(lex_match("\"SAY \"\"HI\"\"\"", e, 0));
    PASS();
}
TH_REG("alex", alx_string_dquote)

static void alx_comment_skip(void)
{
    int e[] = { A_BEGIN, A_END, A_EOF, -1 };
    CHECK(lex_match("COMMENT hello world ; BEGIN END", e, 0));
    PASS();
}
TH_REG("alex", alx_comment_skip)

static void alx_begin_comment(void)
{
    int e[] = { A_BEGIN, A_INTEGER, A_IDENT, A_SEMI, A_EOF, -1 };
    CHECK(lex_match("BEGIN COMMENT note ; INTEGER X;", e, 0));
    PASS();
}
TH_REG("alex", alx_begin_comment)

static void alx_end_comment(void)
{
    int e[] = { A_BEGIN, A_INTEGER, A_IDENT, A_SEMI, A_END, A_SEMI,
                A_EOF, -1 };
    CHECK(lex_match("BEGIN INTEGER I; END OF BLOCK ;", e, 0));
    PASS();
}
TH_REG("alex", alx_end_comment)

static void alx_end_then_else(void)
{
    int e[] = { A_END, A_ELSE, A_EOF, -1 };
    CHECK(lex_match("END ELSE", e, 0));
    PASS();
}
TH_REG("alex", alx_end_then_else)

static void alx_lowercase_err(void)
{
    int e[] = { A_ERROR, A_ERROR, A_ERROR, A_EOF, -1 };
    CHECK(lex_match("abc", e, 3));
    PASS();
}
TH_REG("alex", alx_lowercase_err)

static void alx_logical_value(void)
{
    int e[] = { A_IDENT, A_ASSIGN, A_TRUE, A_EOF, -1 };
    CHECK(lex_match("FLAG := TRUE", e, 0));
    PASS();
}
TH_REG("alex", alx_logical_value)
