/* a_types.c -- ALGOL type set -> JIR jtype table. See a_types.h. */

#include "a_types.h"

/* append a jtype to the pool, return its index */
static uint32_t reg(sema_ctx_t *S, int kind, int width, uint32_t inner)
{
    uint32_t i;
    if (S->n_types >= SM_MAX_TYPES) return 0;
    i = (uint32_t)S->n_types++;
    S->types[i].kind    = (uint8_t)kind;
    S->types[i].pad     = 0;
    S->types[i].width   = (uint16_t)width;
    S->types[i].scale   = 0;
    S->types[i].n_extra = 0;
    S->types[i].inner   = inner;
    S->types[i].extra   = 0;
    return i;
}

void a_types_init(a_typemap_t *M, sema_ctx_t *S, int fp_mode)
{
    /* native: REAL is IEEE double, INTEGER a 32-bit word (Skyhawk's
     * best-trodden codegen path). Burroughs faithful (M7): the 48-bit
     * unified word -- integer is real with a zero exponent. */
    int rw = (fp_mode == A_FP_BURROUGHS) ? 48 : 64;
    int iw = (fp_mode == A_FP_BURROUGHS) ? 48 : 32;

    S->n_types = 0;
    M->fp_mode   = fp_mode;
    M->t_void    = reg(S, JT_VOID,   0,  0);   /* index 0 = void result */
    M->t_integer = reg(S, JT_SIGNED, iw, 0);
    M->t_real    = reg(S, JT_FLOAT,  rw, 0);
    M->t_boolean = reg(S, JT_BIT,     1, 0);
    M->t_alpha   = reg(S, JT_CHAR,   48, 0);   /* a 6-character word */
}

uint32_t a_jtype_of(const a_typemap_t *M, int algol_type)
{
    switch (algol_type) {
    case AT_INTEGER: return M->t_integer;
    case AT_REAL:    return M->t_real;
    case AT_BOOLEAN: return M->t_boolean;
    case AT_ALPHA:   return M->t_real;   /* "ALPHA is treated as REAL" (manual 9-3) */
    default:         return M->t_void;
    }
}

uint32_t a_jtype_ptr(sema_ctx_t *S, uint32_t inner)
{
    return reg(S, JT_PTR, 64, inner);    /* machine pointer */
}

uint32_t a_jtype_array(sema_ctx_t *S, uint32_t elem)
{
    return reg(S, JT_ARRAY, 0, elem);    /* size from bounds at lowering */
}
