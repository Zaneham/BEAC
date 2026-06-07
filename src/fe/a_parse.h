/* a_parse.h -- Burroughs Extended ALGOL parser.
 *
 * Consumes the lexer's flat token array, produces the a_ast.h node tree.
 * Same standalone-module shape as the lexer; a_parse_expr parses a single
 * expression, a_parse_stmt a single statement, and a_parse_program a whole
 * compilation unit (the outer block). */
#ifndef BEAC_A_PARSE_H
#define BEAC_A_PARSE_H

#include "a_token.h"
#include "a_ast.h"

#define A_PARSE_MAX_ERRS 64

typedef struct {
    uint32_t line;
    uint16_t col;
    char     msg[96];
} a_parse_err_t;

typedef struct {
    const a_token_t *toks;
    uint32_t         n_toks;
    uint32_t         pos;
    const char      *src;

    a_node_t        *nodes;
    uint32_t         n_nodes;
    uint32_t         max_nodes;

    a_parse_err_t    errors[A_PARSE_MAX_ERRS];
    int              n_errs;
} a_parser_t;

void     a_parse_init(a_parser_t *P, const a_token_t *toks, uint32_t n_toks,
                      const char *src, a_node_t *nodes, uint32_t max_nodes);
uint32_t a_parse_expr(a_parser_t *P);    /* one expression; 0 on hard fail */
uint32_t a_parse_stmt(a_parser_t *P);    /* one statement                  */
uint32_t a_parse_decl(a_parser_t *P);    /* one declaration                */
uint32_t a_parse_program(a_parser_t *P); /* a whole compilation unit       */

#endif /* BEAC_A_PARSE_H */
