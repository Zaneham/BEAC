/* a_lex.h -- Burroughs Extended ALGOL lexer.
 *
 * Flat token array, same shape as the J73 lexer so the rest of the
 * pipeline can consume it unchanged when we wire the frontend in. Source
 * is uppercased on the fly only for keyword matching; token text keeps
 * the original bytes. */
#ifndef BEAC_A_LEX_H
#define BEAC_A_LEX_H

#include "a_token.h"

#define A_MAX_ERRS 64

typedef struct {
    uint32_t line;
    uint16_t col;
    char     msg[96];
} a_lex_err_t;

typedef struct {
    const char *src;
    uint32_t    srclen;

    a_token_t  *toks;
    uint32_t    max_toks;
    uint32_t    num_toks;

    uint32_t    pos;        /* byte cursor   */
    uint32_t    line;
    uint16_t    col;

    a_lex_err_t errors[A_MAX_ERRS];
    int         num_errs;
} a_lexer_t;

/* 0 = ok, -1 = one or more lex errors (see .errors / .num_errs) */
void a_lex_init(a_lexer_t *L, const char *src, uint32_t len,
                a_token_t *toks, uint32_t max_toks);
int  a_lex_run(a_lexer_t *L);

/* copy a token's source text into buf (NUL-terminated, truncated to sz) */
void a_lex_text(const a_lexer_t *L, const a_token_t *t, char *buf, int sz);

#endif /* BEAC_A_LEX_H */
