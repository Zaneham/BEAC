/* a_ast.h -- Burroughs Extended ALGOL abstract syntax tree.
 *
 * Index-based tree: every node is an a_node_t in one flat array, children
 * are a first_child + next_sibling chain (node 0 is the null sentinel).
 * The shape mirrors the lexer's a_token_t style so the whole frontend
 * stays one consistent module.
 *
 * Node kinds follow the manual's own structure:
 *   expressions  -- Section 4 (arithmetic / Boolean / designational)
 *   statements   -- Section 6
 *   blocks       -- Section 5
 *   declarations -- Section 9
 * Operators on AN_BINOP/AN_UNOP are stored as the a_tok_t the lexer
 * produced, so the precedence ladder maps straight onto the token set.
 */
#ifndef BEAC_A_AST_H
#define BEAC_A_AST_H

#include <stdint.h>

typedef enum {
    AN_NONE = 0,        /* sentinel: index 0 is "no node" */

    /* ---- Program / block structure (Section 5) ---- */
    AN_PROGRAM,         /* whole compilation unit                       */
    AN_BLOCK,           /* BEGIN <declarations> <statements> END        */
    AN_COMPOUND,        /* BEGIN <statements> END  (no declarations)    */

    /* ---- Declarations (Section 9) ---- */
    AN_TYPE_DECL,       /* REAL/INTEGER/BOOLEAN id-list; op=type tok,
                         * aux bit0 = OWN. Children: AN_VAR per name.    */
    AN_ARRAY_DECL,      /* ARRAY; op=element type tok. Children: names
                         * then AN_BOUND_PAIR list.                      */
    AN_BOUND_PAIR,      /* lower : upper. child0=lower, child1=upper.    */
    AN_PROC_DECL,       /* PROCEDURE. child0=name, child1=params,
                         * child2=body; op=return type tok (0 = proper). */
    AN_SWITCH_DECL,     /* SWITCH name := designational-expr list.       */
    AN_PARAM,           /* one formal parameter; aux bit0 = by-VALUE.    */

    /* ---- Statements (Section 6) ---- */
    AN_ASSIGN,          /* left-part-list := expr. Children: targets...,
                         * then the source expr (last child).           */
    AN_IF_STMT,         /* IF bool THEN stmt [ELSE stmt].
                         * child0=cond, child1=then, child2=else?        */
    AN_FOR,             /* FOR var := for-list DO stmt. child0=var,
                         * child1..=for-list elems, last child=body.     */
    AN_FOR_STEP,        /* <expr> STEP <expr> UNTIL <expr>              */
    AN_FOR_WHILE,       /* <expr> WHILE <bool>                          */
    AN_FOR_ELEM,        /* a bare list element <expr>                    */
    AN_GOTO,            /* GO TO <designational expr>                    */
    AN_PROC_CALL,       /* procedure statement: child0=name, then args   */
    AN_LABELED,         /* <label> : <statement>. op holds the label tok */
    AN_DUMMY,           /* the empty statement                           */

    /* ---- I/O and machine statements (Section 6, Burroughs) ---- */
    AN_IO_STMT,         /* READ/WRITE/SPACE/... op = the verb tok        */

    /* ---- Expressions (Section 4) ---- */
    AN_NUM,             /* number literal; op = A_INTLIT or A_REALLIT    */
    AN_STR,             /* string literal                               */
    AN_BOOL,            /* TRUE / FALSE; aux = 1 / 0                     */
    AN_VAR,             /* simple variable / identifier (tok = name)     */
    AN_INDEX,           /* subscripted variable: child0=base, then subs  */
    AN_CALL,            /* function designator: child0=name, then args   */
    AN_BINOP,           /* op = operator tok; child0=lhs, child1=rhs     */
    AN_UNOP,            /* op = + / - / NOT; child0=operand              */
    AN_COND_EXPR,       /* IF bool THEN e ELSE e (conditional expr).
                         * child0=cond, child1=then, child2=else.        */
    AN_PARTIAL,         /* partial word designator var.[start:len]
                         * (Burroughs bit field). child0=base,
                         * child1=start, child2=length.                  */
    AN_CONCAT,          /* concatenate expression (Burroughs)           */

    AN_KIND_COUNT
} a_nk_t;

/* ---- Node ---- */

typedef struct {
    uint16_t kind;          /* a_nk_t                                   */
    uint16_t op;            /* operator / type token for binop/decl/etc */
    uint32_t tok;           /* source token index (name, literal, ...)  */
    uint32_t first_child;
    uint32_t next_sibling;
    uint32_t aux;           /* small per-kind payload (flags, counts)   */
} a_node_t;

const char *a_nk_name(int kind);

#endif /* BEAC_A_AST_H */
