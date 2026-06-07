/* a_lower.h -- lower an analysed ALGOL program to JIR.
 *
 * This is where the ALGOL front end finally meets the cloned backend. We
 * walk the a_ast tree (with a_sema's types alongside) and emit JIR into a
 * jir_mod_t, which jir_mem2reg and x86_emit then take to machine code,
 * untouched. The module's S is pointed at jS -- a sema_ctx_t carrying only
 * the type table (built by a_types) -- because that is the single thing the
 * codegen reads off types: kind and width.
 *
 * The shape mirrors jir_lower.c, but it speaks ALGOL: the program block
 * becomes an implicit "main", declarations become allocas or display slots,
 * and names resolve through frame-local, global, then display storage. */
#ifndef BEAC_A_LOWER_H
#define BEAC_A_LOWER_H

#include "jir.h"
#include "a_sema.h"
#include "a_types.h"

/* jS is non-const: array declarations grow it with JT_TABLE types and
 * their tbldef entries on the fly, which is the cleanest way to reuse the
 * backend's existing table addressing. */
void a_lower(jir_mod_t *M, sema_ctx_t *jS,
             const a_sema_ctx_t *Sem, a_typemap_t *tm, uint32_t root);

#endif /* BEAC_A_LOWER_H */
