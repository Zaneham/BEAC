/* a_types.h -- bridge ALGOL's type set onto the cloned JIR backend.
 *
 * The codegen consumes a J73 sema_ctx_t and reads two fields off each
 * jtype_t: kind (int vs float, for instruction selection) and width (in
 * bits, for sizes). JOVIAL and Burroughs ALGOL are cousins on the ALGOL-58
 * core, so ALGOL's four scalar types map straight onto the shared JT_ kinds;
 * we deliberately do NOT touch the JOVIAL-only layer (JT_FIXED / JT_STATUS /
 * JT_TABLE / JT_HOLLER).
 *
 * REAL/INTEGER widths are FP-mode parameterised so the M7 faithful path
 * (48-bit unified Burroughs word) drops in later without reshaping anything;
 * only A_FP_NATIVE is wired now. */
#ifndef BEAC_A_TYPES_H
#define BEAC_A_TYPES_H

#include "a_sema.h"   /* a_type_t (AT_*) */
#include "sema.h"     /* J73 sema_ctx_t, jtype_t, jt_kind_t */

typedef enum { A_FP_NATIVE = 0, A_FP_BURROUGHS = 1 } a_fp_mode_t;

/* registered jtype indices for ALGOL's base types */
typedef struct {
    uint32_t t_void;       /* index 0                                  */
    uint32_t t_integer;
    uint32_t t_real;
    uint32_t t_boolean;
    uint32_t t_alpha;
    int      fp_mode;
    /* global-buffer layout, filled by a_lower so a caller (or test) can seed
     * the AR-stack pointer before invoking a procedure as an entry point --
     * in a whole program, main does this, but a directly-called procedure
     * needs G_SP pointed at the AR region first. */
    uint32_t gsp_field;
    uint32_t ar_base_field;
} a_typemap_t;

/* Register ALGOL's base types into S->types[]. Resets S->n_types so index 0
 * is JT_VOID (the lowerer uses type 0 for void results). */
void     a_types_init(a_typemap_t *M, sema_ctx_t *S, int fp_mode);

/* map a sema a_type_t (AT_*) to the jtype index of its value representation */
uint32_t a_jtype_of(const a_typemap_t *M, int algol_type);

/* intern derived types in S->types[] */
uint32_t a_jtype_ptr(sema_ctx_t *S, uint32_t inner);    /* POINTER(inner)   */
uint32_t a_jtype_array(sema_ctx_t *S, uint32_t elem);   /* ARRAY of elem    */

#endif /* BEAC_A_TYPES_H */
