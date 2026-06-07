/* a_token.h -- Burroughs Extended ALGOL token set.
 *
 * Token classes taken verbatim from the B5500 Extended ALGOL Reference
 * Manual (1969), Section 2 "Basic Components". Every reserved word in
 * the manual's delimiter grammar is here so that none can be used as an
 * identifier (manual 2-5, RESTRICTIONS). Symbol and word forms of an
 * operator (e.g. "<" and "LSS") map to the SAME token -- the manual
 * lists them as alternate representations of one delimiter (2-3).
 *
 * Quirks the manual nails down, captured here so we don't guess:
 *   - alphabet is UPPERCASE A-Z only; lowercase is disallowed (2-1)
 *   - "@" is the exponent marker in a number: 8.758@-47 (2-6), NOT "E"
 *   - "*" is exponentiation (ALGOL 60's up-arrow); multiply is "x"/TIMES
 *   - ":=" assignment lives in the separator class (2-2)
 */
#ifndef BEAC_A_TOKEN_H
#define BEAC_A_TOKEN_H

#include <stdint.h>

typedef enum {
    /* ---- Literals (2-5..2-8) ---- */
    A_INTLIT,       /* unsigned integer: 1354          */
    A_REALLIT,      /* decimal/exponent: .546 1354.543 8.758@-47 */
    A_STRLIT,       /* "ALGOL"                          */
    A_IDENT,        /* letter (letter|digit)* , <= 63   */

    /* ---- Logical values (2-2) ---- */
    A_TRUE,
    A_FALSE,

    /* ---- Arithmetic operators (2-2) ---- */
    A_PLUS,         /* +                                */
    A_MINUS,        /* -                                */
    A_TIMES,        /* x  / TIMES   (multiply)          */
    A_SLASH,        /* /            (real divide)       */
    A_DIV,          /* DIV          (integer divide)    */
    A_POW,          /* *            (exponentiation)    */
    A_MOD,          /* MOD                              */

    /* ---- Relational operators (2-2): symbol = word form ---- */
    A_LSS,          /* <   LSS                          */
    A_LEQ,          /* <=  LEQ                          */
    A_EQL,          /* =   EQL                          */
    A_GEQ,          /* >=  GEQ                          */
    A_GTR,          /* >   GTR                          */
    A_NEQ,          /* <>  NEQ                          */

    /* ---- Logical operators (2-2) ---- */
    A_EQV,
    A_IMP,
    A_OR,
    A_AND,
    A_NOT,

    /* ---- Separators / punctuation (2-2) ---- */
    A_ASSIGN,       /* :=                               */
    A_COMMA,        /* ,                                */
    A_DOT,          /* .                                */
    A_AT,           /* @  (separator; exponent handled in number scan) */
    A_COLON,        /* :                                */
    A_SEMI,         /* ;                                */
    A_AMP,          /* &                                */
    A_ARROW,        /* <-                               */

    /* ---- Brackets (2-2) ---- */
    A_LPAREN,       /* (                                */
    A_RPAREN,       /* )                                */
    A_LBRACK,       /* [   LB                           */
    A_RBRACK,       /* ]   RB                           */
    A_HASH,         /* #                                */
    A_BEGIN,
    A_END,

    /* ---- Sequential operators: control (2-2) ---- */
    A_GO, A_TO, A_IF, A_THEN, A_ELSE, A_FOR, A_DO,

    /* ---- Sequential operators: i/o + machine (2-2) ---- */
    A_READ, A_WRITE, A_DOUBLE, A_RELEASE, A_DS, A_TOGGLE, A_JUMP,
    A_SKIP, A_DB, A_DI, A_SET, A_LOCK, A_ZIP, A_CI, A_SC, A_DC,
    A_RESET, A_SB, A_SI, A_TALLY, A_REWIND, A_CLOSE, A_SPACE,
    A_FILL, A_PAGE, A_DBL, A_NO, A_BREAK,

    /* ---- Separator keywords (2-2) ---- */
    A_STEP, A_UNTIL, A_WHILE, A_COMMENT, A_LOC, A_WDS, A_ADD, A_SUB,
    A_LIT, A_CHR, A_NUM, A_ZON, A_DEC, A_OCT, A_WITH,

    /* ---- Declarators (2-2) ---- */
    A_OWN, A_BOOLEAN, A_INTEGER, A_REAL, A_ARRAY, A_SWITCH, A_LABEL,
    A_LOCAL, A_FORWARD, A_SAVE, A_PROCEDURE, A_STREAM, A_LIST,
    A_FORMAT, A_IN, A_OUT, A_MONITOR, A_DUMP, A_FILE, A_ALPHA,
    A_DEFINE, A_REVERSE,

    /* ---- Specificator (2-2) ---- */
    A_VALUE,

    /* ---- Stream / type-transfer / interrogate names used widely ---- */
    A_ENTIER,       /* type-transfer fn (3-10)          */
    A_BOOLEAN_FN,   /* BOOLEAN as type-transfer fn      */

    /* ---- Special ---- */
    A_EOF,
    A_ERROR,

    A_TOK_COUNT
} a_tok_t;

/* ---- Token (same shape as the J73 token_t, for pipeline reuse) ---- */

typedef struct {
    uint16_t type;      /* a_tok_t          */
    uint16_t len;       /* source length    */
    uint32_t offset;    /* offset in source */
    uint32_t line;
    uint16_t col;
} a_token_t;

/* ---- Reserved-word table (sorted by name, binary searched) ---- */

typedef struct {
    const char *name;
    uint16_t    type;   /* a_tok_t */
} a_kw_t;

extern const a_kw_t a_kwtab[];
extern const int    a_nkw;

const char *a_tok_name(int type);

#endif /* BEAC_A_TOKEN_H */
