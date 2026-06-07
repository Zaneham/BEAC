/* ta_types.c -- ALGOL -> JIR type bridge tests (under tharns).
 *
 * Checks ALGOL's scalar types map onto the shared JT_ kinds with the right
 * widths, that the a_type_t -> jtype map is correct (ALPHA folds to REAL),
 * that derived pointer/array types intern, and that the FP-mode parameter
 * swaps REAL/INTEGER widths for the (future) Burroughs faithful path. */

#include "tharns.h"
#include "a_types.h"

static sema_ctx_t  ts;   /* ~3MB; reused across cases (init resets it) */
static a_typemap_t tm;

static void atyp_native_kinds(void)
{
    a_types_init(&tm, &ts, A_FP_NATIVE);
    CHECK(tm.t_void == 0 && ts.types[tm.t_void].kind == JT_VOID);
    CHECK(ts.types[tm.t_integer].kind == JT_SIGNED && ts.types[tm.t_integer].width == 32);
    CHECK(ts.types[tm.t_real].kind == JT_FLOAT && ts.types[tm.t_real].width == 64);
    CHECK(ts.types[tm.t_boolean].kind == JT_BIT);
    CHECK(ts.types[tm.t_alpha].kind == JT_CHAR && ts.types[tm.t_alpha].width == 48);
    PASS();
}
TH_REG("atypes", atyp_native_kinds)

static void atyp_map(void)
{
    a_types_init(&tm, &ts, A_FP_NATIVE);
    CHECK(a_jtype_of(&tm, AT_INTEGER) == tm.t_integer);
    CHECK(a_jtype_of(&tm, AT_REAL)    == tm.t_real);
    CHECK(a_jtype_of(&tm, AT_BOOLEAN) == tm.t_boolean);
    CHECK(a_jtype_of(&tm, AT_ALPHA)   == tm.t_real);   /* treated as REAL */
    CHECK(a_jtype_of(&tm, AT_NONE)    == tm.t_void);
    PASS();
}
TH_REG("atypes", atyp_map)

static void atyp_ptr_array(void)
{
    uint32_t pi, ai;
    a_types_init(&tm, &ts, A_FP_NATIVE);
    pi = a_jtype_ptr(&ts, tm.t_integer);
    ai = a_jtype_array(&ts, tm.t_real);
    CHECK(ts.types[pi].kind == JT_PTR   && ts.types[pi].inner == tm.t_integer);
    CHECK(ts.types[ai].kind == JT_ARRAY && ts.types[ai].inner == tm.t_real);
    PASS();
}
TH_REG("atypes", atyp_ptr_array)

static void atyp_burroughs_widths(void)
{
    a_types_init(&tm, &ts, A_FP_BURROUGHS);
    CHECK(ts.types[tm.t_real].width == 48);      /* 48-bit unified word */
    CHECK(ts.types[tm.t_integer].width == 48);
    PASS();
}
TH_REG("atypes", atyp_burroughs_widths)
