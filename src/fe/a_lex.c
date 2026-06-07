/* a_lex.c -- Burroughs Extended ALGOL lexer.
 *
 * Implements Section 2 of the B5500 Extended ALGOL Reference Manual:
 * letters (uppercase only, 2-1), numbers with the "@" exponent marker
 * (2-6), strings (2-7), the full reserved-word set (2-2), and the three
 * comment forms (2-4) -- including the END-comment, where text after an
 * END up to the next END/ELSE/UNTIL (or a non-word character) is ignored.
 *
 * Symbol and word forms of an operator collapse to one token via the
 * reserved table (e.g. "<" and "LSS" -> A_LSS). "*" is exponentiation;
 * multiply is the word TIMES (the manual's "x"), there being no ASCII
 * symbol for it in this dialect. */

#include "a_lex.h"
#include <string.h>
#include <stdio.h>

/* ---- Reserved-word table: sorted by name, binary searched (2-2) ---- */

const a_kw_t a_kwtab[] = {
    { "ADD",       A_ADD       },
    { "ALPHA",     A_ALPHA     },
    { "AND",       A_AND       },
    { "ARRAY",     A_ARRAY     },
    { "BEGIN",     A_BEGIN     },
    { "BOOLEAN",   A_BOOLEAN   },
    { "BREAK",     A_BREAK     },
    { "CHR",       A_CHR       },
    { "CI",        A_CI        },
    { "CLOSE",     A_CLOSE     },
    { "COMMENT",   A_COMMENT   },
    { "DB",        A_DB        },
    { "DBL",       A_DBL       },
    { "DC",        A_DC        },
    { "DEC",       A_DEC       },
    { "DEFINE",    A_DEFINE    },
    { "DI",        A_DI        },
    { "DIV",       A_DIV       },
    { "DO",        A_DO        },
    { "DOUBLE",    A_DOUBLE    },
    { "DS",        A_DS        },
    { "DUMP",      A_DUMP      },
    { "ELSE",      A_ELSE      },
    { "END",       A_END       },
    { "ENTIER",    A_ENTIER    },
    { "EQL",       A_EQL       },
    { "EQV",       A_EQV       },
    { "FALSE",     A_FALSE     },
    { "FILE",      A_FILE      },
    { "FILL",      A_FILL      },
    { "FOR",       A_FOR       },
    { "FORMAT",    A_FORMAT    },
    { "FORWARD",   A_FORWARD   },
    { "GEQ",       A_GEQ       },
    { "GO",        A_GO        },
    { "GTR",       A_GTR       },
    { "IF",        A_IF        },
    { "IMP",       A_IMP       },
    { "IN",        A_IN        },
    { "INTEGER",   A_INTEGER   },
    { "JUMP",      A_JUMP      },
    { "LABEL",     A_LABEL     },
    { "LB",        A_LBRACK    },
    { "LEQ",       A_LEQ       },
    { "LIST",      A_LIST      },
    { "LIT",       A_LIT       },
    { "LOC",       A_LOC       },
    { "LOCAL",     A_LOCAL     },
    { "LOCK",      A_LOCK      },
    { "LSS",       A_LSS       },
    { "MOD",       A_MOD       },
    { "MONITOR",   A_MONITOR   },
    { "NEQ",       A_NEQ       },
    { "NO",        A_NO        },
    { "NOT",       A_NOT       },
    { "NUM",       A_NUM       },
    { "OCT",       A_OCT       },
    { "OR",        A_OR        },
    { "OUT",       A_OUT       },
    { "OWN",       A_OWN       },
    { "PAGE",      A_PAGE      },
    { "PROCEDURE", A_PROCEDURE },
    { "RB",        A_RBRACK    },
    { "READ",      A_READ      },
    { "REAL",      A_REAL      },
    { "RELEASE",   A_RELEASE   },
    { "RESET",     A_RESET     },
    { "REVERSE",   A_REVERSE   },
    { "REWIND",    A_REWIND    },
    { "SAVE",      A_SAVE      },
    { "SB",        A_SB        },
    { "SC",        A_SC        },
    { "SET",       A_SET       },
    { "SI",        A_SI        },
    { "SKIP",      A_SKIP      },
    { "SPACE",     A_SPACE     },
    { "STEP",      A_STEP      },
    { "STREAM",    A_STREAM    },
    { "SUB",       A_SUB       },
    { "SWITCH",    A_SWITCH    },
    { "TALLY",     A_TALLY     },
    { "THEN",      A_THEN      },
    { "TIMES",     A_TIMES     },
    { "TO",        A_TO        },
    { "TOGGLE",    A_TOGGLE    },
    { "TRUE",      A_TRUE      },
    { "UNTIL",     A_UNTIL     },
    { "VALUE",     A_VALUE     },
    { "WDS",       A_WDS       },
    { "WHILE",     A_WHILE     },
    { "WITH",      A_WITH      },
    { "WRITE",     A_WRITE     },
    { "ZIP",       A_ZIP       },
    { "ZON",       A_ZON       },
};
const int a_nkw = (int)(sizeof(a_kwtab) / sizeof(a_kwtab[0]));

/* ---- Token names (linear table, avoids -Wswitch-enum churn) ---- */

static const struct { int t; const char *n; } a_names[] = {
    { A_INTLIT, "INTLIT" }, { A_REALLIT, "REALLIT" },
    { A_STRLIT, "STRLIT" }, { A_IDENT, "IDENT" },
    { A_TRUE, "TRUE" }, { A_FALSE, "FALSE" },
    { A_PLUS, "+" }, { A_MINUS, "-" }, { A_TIMES, "TIMES" },
    { A_SLASH, "/" }, { A_DIV, "DIV" }, { A_POW, "*" }, { A_MOD, "MOD" },
    { A_LSS, "LSS" }, { A_LEQ, "LEQ" }, { A_EQL, "EQL" },
    { A_GEQ, "GEQ" }, { A_GTR, "GTR" }, { A_NEQ, "NEQ" },
    { A_EQV, "EQV" }, { A_IMP, "IMP" }, { A_OR, "OR" },
    { A_AND, "AND" }, { A_NOT, "NOT" },
    { A_ASSIGN, ":=" }, { A_COMMA, "," }, { A_DOT, "." },
    { A_AT, "@" }, { A_COLON, ":" }, { A_SEMI, ";" },
    { A_AMP, "&" }, { A_ARROW, "<-" },
    { A_LPAREN, "(" }, { A_RPAREN, ")" }, { A_LBRACK, "[" },
    { A_RBRACK, "]" }, { A_HASH, "#" }, { A_BEGIN, "BEGIN" },
    { A_END, "END" },
    { A_GO, "GO" }, { A_TO, "TO" }, { A_IF, "IF" }, { A_THEN, "THEN" },
    { A_ELSE, "ELSE" }, { A_FOR, "FOR" }, { A_DO, "DO" },
    { A_STEP, "STEP" }, { A_UNTIL, "UNTIL" }, { A_WHILE, "WHILE" },
    { A_OWN, "OWN" }, { A_BOOLEAN, "BOOLEAN" }, { A_INTEGER, "INTEGER" },
    { A_REAL, "REAL" }, { A_ARRAY, "ARRAY" }, { A_SWITCH, "SWITCH" },
    { A_LABEL, "LABEL" }, { A_PROCEDURE, "PROCEDURE" },
    { A_VALUE, "VALUE" }, { A_DEFINE, "DEFINE" }, { A_FILE, "FILE" },
    { A_LIST, "LIST" }, { A_FORMAT, "FORMAT" }, { A_COMMENT, "COMMENT" },
    { A_ENTIER, "ENTIER" },
    { A_EOF, "EOF" }, { A_ERROR, "ERROR" },
};

const char *a_tok_name(int type)
{
    int i;
    for (i = 0; i < (int)(sizeof(a_names) / sizeof(a_names[0])); i++)
        if (a_names[i].t == type) return a_names[i].n;
    return "?";
}

/* keyword lookup: returns token type, or A_IDENT if not reserved */
static int a_kw_lookup(const char *s, int len)
{
    int lo = 0, hi = a_nkw - 1;
    char buf[64];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, s, (size_t)len);
    buf[len] = '\0';
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(buf, a_kwtab[mid].name);
        if (c == 0) return a_kwtab[mid].type;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return A_IDENT;
}

/* ---- Character helpers ---- */

static int is_upper(int c) { return c >= 'A' && c <= 'Z'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_lower(int c) { return c >= 'a' && c <= 'z'; }

/* ---- Lexer plumbing ---- */

void a_lex_init(a_lexer_t *L, const char *src, uint32_t len,
                a_token_t *toks, uint32_t max_toks)
{
    memset(L, 0, sizeof(*L));
    L->src = src; L->srclen = len;
    L->toks = toks; L->max_toks = max_toks;
    L->pos = 0; L->line = 1; L->col = 1;
}

void a_lex_text(const a_lexer_t *L, const a_token_t *t, char *buf, int sz)
{
    int n = (int)t->len;
    if (n >= sz) n = sz - 1;
    if (n < 0) n = 0;
    memcpy(buf, L->src + t->offset, (size_t)n);
    buf[n] = '\0';
}

static char peek(const a_lexer_t *L, uint32_t ahead)
{
    uint32_t p = L->pos + ahead;
    return (p < L->srclen) ? L->src[p] : '\0';
}

static char cur(const a_lexer_t *L)
{
    return (L->pos < L->srclen) ? L->src[L->pos] : '\0';
}

static void adv(a_lexer_t *L)
{
    if (L->pos >= L->srclen) return;
    if (L->src[L->pos] == '\n') { L->line++; L->col = 1; }
    else { L->col = (uint16_t)(L->col + 1); }
    L->pos++;
}

static void push(a_lexer_t *L, int type, uint32_t off, uint32_t len,
                 uint32_t line, uint16_t col)
{
    a_token_t *t;
    if (L->num_toks >= L->max_toks) return;
    t = &L->toks[L->num_toks++];
    t->type = (uint16_t)type;
    t->len  = (uint16_t)len;
    t->offset = off;
    t->line = line;
    t->col  = col;
}

static void err(a_lexer_t *L, const char *msg)
{
    if (L->num_errs >= A_MAX_ERRS) return;
    a_lex_err_t *e = &L->errors[L->num_errs++];
    e->line = L->line; e->col = L->col;
    snprintf(e->msg, sizeof(e->msg), "%s", msg);
}

/* length of an uppercase word [A-Z][A-Z0-9]* starting at offset p */
static uint32_t word_len(const a_lexer_t *L, uint32_t p)
{
    uint32_t n = 0;
    if (p >= L->srclen || !is_upper(L->src[p])) return 0;
    while (p + n < L->srclen &&
           (is_upper(L->src[p + n]) || is_digit(L->src[p + n]))) n++;
    return n;
}

/* COMMENT ... ; -- skip through the terminating semicolon (2-4) */
static void skip_comment(a_lexer_t *L)
{
    while (L->pos < L->srclen && cur(L) != ';') adv(L);
    if (L->pos < L->srclen) adv(L);   /* eat the ';' */
}

/* END <letters/digits/blanks, excluding END/ELSE/UNTIL> -- comment (2-4) */
static void skip_end_comment(a_lexer_t *L)
{
    for (;;) {
        char c = cur(L);
        if (c == ' ' || c == '\t') { adv(L); continue; }
        if (is_digit((unsigned char)c)) { adv(L); continue; }
        if (is_upper((unsigned char)c)) {
            uint32_t wl = word_len(L, L->pos);
            const char *w = L->src + L->pos;
            if ((wl == 3 && memcmp(w, "END", 3) == 0) ||
                (wl == 4 && memcmp(w, "ELSE", 4) == 0) ||
                (wl == 5 && memcmp(w, "UNTIL", 5) == 0))
                return;                       /* stop: leave the word */
            { uint32_t i; for (i = 0; i < wl; i++) adv(L); }
            continue;
        }
        return;   /* newline, ';', ')', lowercase, EOF, ... */
    }
}

/* scan a number: [digits] ['.' digits] ['@' [+-] digits], REAL if it has
 * a fraction or exponent, else INT (2-6). Caller guarantees a valid start
 * (a digit, or '.' followed by a digit, or '@' followed by a sign/digit). */
static void scan_number(a_lexer_t *L)
{
    uint32_t off = L->pos, line = L->line; uint16_t col = L->col;
    int is_real = 0, ndig = 0;

    while (is_digit((unsigned char)cur(L))) { adv(L); ndig++; }

    if (cur(L) == '.' && is_digit((unsigned char)peek(L, 1))) {
        is_real = 1; adv(L);                  /* '.' */
        while (is_digit((unsigned char)cur(L))) { adv(L); ndig++; }
    }

    if (cur(L) == '@') {
        char s = peek(L, 1);
        if (is_digit((unsigned char)s) ||
            ((s == '+' || s == '-') && is_digit((unsigned char)peek(L, 2)))) {
            is_real = 1; adv(L);              /* '@' */
            if (cur(L) == '+' || cur(L) == '-') adv(L);
            while (is_digit((unsigned char)cur(L))) adv(L);
        }
    }

    if (ndig > 12) err(L, "number exceeds 12 significant digits (2-6)");
    push(L, is_real ? A_REALLIT : A_INTLIT, off, L->pos - off, line, col);
}

/* scan a "..." string; "" is an embedded quote. Max 63 chars (2-7). */
static void scan_string(a_lexer_t *L)
{
    uint32_t off = L->pos, line = L->line; uint16_t col = L->col;
    int n = 0;
    adv(L);                                   /* opening '"' */
    for (;;) {
        char c = cur(L);
        if (c == '\0') { err(L, "unterminated string"); break; }
        if (c == '"') {
            if (peek(L, 1) == '"') { adv(L); adv(L); n++; continue; }
            adv(L);                            /* closing '"' */
            break;
        }
        adv(L); n++;
    }
    if (n > 63) err(L, "string exceeds 63 characters (2-7)");
    push(L, A_STRLIT, off, L->pos - off, line, col);
}

/* scan identifier or reserved word */
static void scan_word(a_lexer_t *L)
{
    uint32_t off = L->pos, line = L->line; uint16_t col = L->col;
    uint32_t len;
    int type;
    while (is_upper((unsigned char)cur(L)) || is_digit((unsigned char)cur(L)))
        adv(L);
    len = L->pos - off;
    type = a_kw_lookup(L->src + off, (int)len);

    if (type == A_COMMENT) { skip_comment(L); return; }   /* no token */

    if (type == A_IDENT && len > 63)
        err(L, "identifier exceeds 63 characters (2-5)");

    push(L, type, off, len, line, col);

    if (type == A_END) skip_end_comment(L);
}

/* ---- Main scan ---- */

int a_lex_run(a_lexer_t *L)
{
    while (L->pos < L->srclen) {
        char c = cur(L);
        uint32_t off = L->pos, line = L->line; uint16_t col = L->col;

        /* whitespace (space is a separator but carries no token) */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { adv(L); continue; }

        if (is_upper((unsigned char)c)) { scan_word(L); continue; }
        if (is_digit((unsigned char)c)) { scan_number(L); continue; }
        if (c == '.' && is_digit((unsigned char)peek(L, 1))) { scan_number(L); continue; }
        if (c == '@') {
            char s = peek(L, 1);
            if (is_digit((unsigned char)s) ||
                ((s == '+' || s == '-') && is_digit((unsigned char)peek(L, 2)))) {
                scan_number(L); continue;
            }
        }
        if (c == '"') { scan_string(L); continue; }

        if (is_lower((unsigned char)c)) {
            err(L, "lowercase letters are not permitted (2-1)");
            adv(L);
            push(L, A_ERROR, off, 1, line, col);
            continue;
        }

        /* operators and punctuation */
        switch (c) {
        case ':':
            if (peek(L, 1) == '=') { adv(L); adv(L); push(L, A_ASSIGN, off, 2, line, col); }
            else { adv(L); push(L, A_COLON, off, 1, line, col); }
            break;
        case '<':
            if (peek(L, 1) == '=') { adv(L); adv(L); push(L, A_LEQ, off, 2, line, col); }
            else if (peek(L, 1) == '>') { adv(L); adv(L); push(L, A_NEQ, off, 2, line, col); }
            else if (peek(L, 1) == '-') { adv(L); adv(L); push(L, A_ARROW, off, 2, line, col); }
            else { adv(L); push(L, A_LSS, off, 1, line, col); }
            break;
        case '>':
            if (peek(L, 1) == '=') { adv(L); adv(L); push(L, A_GEQ, off, 2, line, col); }
            else { adv(L); push(L, A_GTR, off, 1, line, col); }
            break;
        case '=': adv(L); push(L, A_EQL, off, 1, line, col); break;
        case '+': adv(L); push(L, A_PLUS, off, 1, line, col); break;
        case '-': adv(L); push(L, A_MINUS, off, 1, line, col); break;
        case '*': adv(L); push(L, A_POW, off, 1, line, col); break;
        case '/': adv(L); push(L, A_SLASH, off, 1, line, col); break;
        case ',': adv(L); push(L, A_COMMA, off, 1, line, col); break;
        case '.': adv(L); push(L, A_DOT, off, 1, line, col); break;
        case '@': adv(L); push(L, A_AT, off, 1, line, col); break;
        case ';': adv(L); push(L, A_SEMI, off, 1, line, col); break;
        case '&': adv(L); push(L, A_AMP, off, 1, line, col); break;
        case '(': adv(L); push(L, A_LPAREN, off, 1, line, col); break;
        case ')': adv(L); push(L, A_RPAREN, off, 1, line, col); break;
        case '[': adv(L); push(L, A_LBRACK, off, 1, line, col); break;
        case ']': adv(L); push(L, A_RBRACK, off, 1, line, col); break;
        case '#': adv(L); push(L, A_HASH, off, 1, line, col); break;
        case '"': scan_string(L); break;   /* unreachable: handled above */
        default:
            err(L, "unexpected character");
            adv(L);
            push(L, A_ERROR, off, 1, line, col);
            break;
        }
    }

    push(L, A_EOF, L->pos, 0, L->line, L->col);
    return L->num_errs > 0 ? -1 : 0;
}
