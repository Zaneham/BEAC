/* ta_run.c -- end-to-end: ALGOL source actually executing on x86.
 *
 * Lexes -> parses -> sema -> a_lower -> jir_m2r -> x86_emit, JITs a named
 * function into executable memory, and calls it. A function returns the
 * value assigned to its own name, so these are real programs computing real
 * answers: IF, FOR loops, GO TO, procedure calls with arguments, and
 * recursion -- all the way to native machine code. */

#include "tharns.h"
#include "a_lex.h"
#include "a_parse.h"
#include "a_sema.h"
#include "a_types.h"
#include "a_lower.h"
#include "x86.h"

#ifdef _WIN32
#include <windows.h>
#endif

static a_token_t   r_toks[2048];
static a_node_t    r_nodes[16384];
static a_lexer_t   rL;
static a_parser_t  rP;
static a_sema_ctx_t rSem;
static sema_ctx_t  r_js;
static a_typemap_t r_tm;
static jir_mod_t   rM;
static x86_mod_t   rx86;
static int64_t     r_gbuf[4096];  /* global area: globals + display + AR stack */

typedef int64_t (*algol_fn_t)(void);

/* lower `src`, codegen, JIT the function named `fname`, return its result.
 * sentinel <= -100000 means a compile/setup failure, not a program value. */
static int64_t run_fn(const char *src, const char *fname)
{
    uint32_t root, i, fi = 0;
    int found = 0;

    a_lex_init(&rL, src, (uint32_t)strlen(src), r_toks, 2048);
    a_lex_run(&rL);
    a_parse_init(&rP, r_toks, rL.num_toks, src, r_nodes, 16384);
    root = a_parse_program(&rP);
    a_sema_init(&rSem, rP.nodes, rP.n_nodes, r_toks, src);
    if (a_sema_run(&rSem, root) != 0) return -100001;
    a_types_init(&r_tm, &r_js, A_FP_NATIVE);
    a_lower(&rM, &r_js, &rSem, &r_tm, root);
    jir_m2r(&rM);
    memset(r_gbuf, 0, sizeof(r_gbuf));
    /* a whole program runs main first, which seeds the AR-stack pointer; we
     * call a procedure directly, so do main's one job by hand here. */
    r_gbuf[r_tm.gsp_field] = (int64_t)(uintptr_t)&r_gbuf[r_tm.ar_base_field];
    x86_init(&rx86, &rM);
    rx86.globals_base = (uint64_t)(uintptr_t)r_gbuf;   /* where globals live */
    if (x86_emit(&rx86) != SK_OK) return -100002;

    for (i = 0; i < rM.n_funcs; i++)
        if (strcmp(rM.strs + rM.funcs[i].name, fname) == 0) { fi = i; found = 1; break; }
    if (!found) return -100005;
    (void)fi;   /* read only on the Windows JIT path below */

#ifdef _WIN32
    {
        uint32_t len = rx86.codelen, off = rx86.fn_off[fi];
        void *mem = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_EXECUTE_READWRITE);
        algol_fn_t fn;
        void *addr;
        int64_t result;
        if (!mem) return -100003;
        memcpy(mem, rx86.code, len);
        addr = (uint8_t *)mem + off;
        memcpy(&fn, &addr, sizeof(fn));   /* dodge object->function cast warning */
        result = fn();
        VirtualFree(mem, 0, MEM_RELEASE);
        return result;
    }
#else
    return -100004;
#endif
}

/* same pipeline as run_fn, but the JIT'd function is called as returning a
 * double (REAL comes back in XMM0). Returns -1e18 on a setup failure. */
typedef double (*algol_freal_t)(void);
static double run_real(const char *src, const char *fname)
{
    uint32_t root, i, fi = 0;
    int found = 0;
    a_lex_init(&rL, src, (uint32_t)strlen(src), r_toks, 2048);
    a_lex_run(&rL);
    a_parse_init(&rP, r_toks, rL.num_toks, src, r_nodes, 16384);
    root = a_parse_program(&rP);
    a_sema_init(&rSem, rP.nodes, rP.n_nodes, r_toks, src);
    if (a_sema_run(&rSem, root) != 0) return -1e18;
    a_types_init(&r_tm, &r_js, A_FP_NATIVE);
    a_lower(&rM, &r_js, &rSem, &r_tm, root);
    jir_m2r(&rM);
    memset(r_gbuf, 0, sizeof(r_gbuf));
    r_gbuf[r_tm.gsp_field] = (int64_t)(uintptr_t)&r_gbuf[r_tm.ar_base_field];
    x86_init(&rx86, &rM);
    rx86.globals_base = (uint64_t)(uintptr_t)r_gbuf;
    if (x86_emit(&rx86) != SK_OK) return -1e18;
    for (i = 0; i < rM.n_funcs; i++)
        if (strcmp(rM.strs + rM.funcs[i].name, fname) == 0) { fi = i; found = 1; break; }
    if (!found) return -1e18;
    (void)fi;   /* read only on the Windows JIT path below */
#ifdef _WIN32
    {
        uint32_t len = rx86.codelen, off = rx86.fn_off[fi];
        void *mem = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_EXECUTE_READWRITE);
        algol_freal_t fn; void *addr; double result;
        if (!mem) return -1e18;
        memcpy(mem, rx86.code, len);
        addr = (uint8_t *)mem + off;
        memcpy(&fn, &addr, sizeof(fn));
        result = fn();
        VirtualFree(mem, 0, MEM_RELEASE);
        return result;
    }
#else
    return -1e18;
#endif
}

static int near_d(double a, double b) { double d = a - b; return d < 1e-9 && d > -1e-9; }

static void arun_real_return(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    CHECK(near_d(run_real(
        "BEGIN REAL PROCEDURE HALF; HALF := 1.5 + 2.0; HALF END", "HALF"), 3.5));
    PASS();
#endif
}
TH_REG("arun", arun_real_return)

static void arun_real_divide(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* "/" is real division: 7 / 2 = 3.5 */
    CHECK(near_d(run_real(
        "BEGIN REAL PROCEDURE Q; Q := 7.0 / 2.0; Q END", "Q"), 3.5));
    PASS();
#endif
}
TH_REG("arun", arun_real_divide)

static void arun_real_call(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* a REAL function calling another REAL function -- exercises the XMM0
     * return capture on the call side too. */
    CHECK(near_d(run_real(
        "BEGIN REAL PROCEDURE DUP(X); VALUE X; REAL X; DUP := X TIMES 2.0; "
        "REAL PROCEDURE GO2; GO2 := DUP(1.25); GO2 END", "GO2"), 2.5));
    PASS();
#endif
}
TH_REG("arun", arun_real_call)

static void arun_returns_42(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    CHECK(run_fn("BEGIN INTEGER PROCEDURE ANSWER; ANSWER := 21 + 21; "
                 "ANSWER END", "ANSWER") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_returns_42)

static void arun_multiply(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    CHECK(run_fn("BEGIN INTEGER PROCEDURE F; F := 6 TIMES 7; F END", "F") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_multiply)

static void arun_local_var(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    CHECK(run_fn("BEGIN INTEGER PROCEDURE G; "
                 "BEGIN INTEGER X; X := 40; G := X + 2 END; G END", "G") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_local_var)

static void arun_if_else(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    CHECK(run_fn("BEGIN INTEGER PROCEDURE T; "
                 "BEGIN INTEGER X; X := 5; "
                 "IF X < 10 THEN T := 1 ELSE T := 0 END; T END", "T") == 1);
    PASS();
#endif
}
TH_REG("arun", arun_if_else)

static void arun_for_sum(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* sum 1..10 = 55 */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE S; "
                 "BEGIN INTEGER I; S := 0; "
                 "FOR I := 1 STEP 1 UNTIL 10 DO S := S + I END; S END",
                 "S") == 55);
    PASS();
#endif
}
TH_REG("arun", arun_for_sum)

static void arun_for_negstep(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* negative STEP counts down: sum 10 downto 1 = 55 */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE S; "
                 "BEGIN INTEGER I; S := 0; "
                 "FOR I := 10 STEP -1 UNTIL 1 DO S := S + I END; S END",
                 "S") == 55);
    PASS();
#endif
}
TH_REG("arun", arun_for_negstep)

static void arun_for_while(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* plain WHILE element: E = I + 1 re-evaluated each turn, guard I <= 5.
     * I walks 1,2,3,4,5 (then 6 fails the guard), so S = 1+2+3+4+5 = 15. */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE S; "
                 "BEGIN INTEGER I; S := 0; I := 0; "
                 "FOR I := I + 1 WHILE I <= 5 DO S := S + I END; S END",
                 "S") == 15);
    PASS();
#endif
}
TH_REG("arun", arun_for_while)

static void arun_for_step_while(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* STEP..WHILE: I := 1, step +1, guard I <= 5. Body runs for 1..5,
     * the increment to 6 fails the guard. S = 15. */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE S; "
                 "BEGIN INTEGER I; S := 0; "
                 "FOR I := 1 STEP 1 WHILE I <= 5 DO S := S + I END; S END",
                 "S") == 15);
    PASS();
#endif
}
TH_REG("arun", arun_for_step_while)

static void arun_for_multi(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* multi-element for-list: bare values then a STEP element, one body.
     * 1 + 3 + (10 STEP 5 UNTIL 20 -> 10+15+20) = 1+3+45 = 49 */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE S; "
                 "BEGIN INTEGER I; S := 0; "
                 "FOR I := 1, 3, 10 STEP 5 UNTIL 20 DO S := S + I END; S END",
                 "S") == 49);
    PASS();
#endif
}
TH_REG("arun", arun_for_multi)

static void arun_factorial(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* 5! = 120, iterative */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE F; "
                 "BEGIN INTEGER I; F := 1; "
                 "FOR I := 1 STEP 1 UNTIL 5 DO F := F TIMES I END; F END",
                 "F") == 120);
    PASS();
#endif
}
TH_REG("arun", arun_factorial)

static void arun_goto_loop(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* sum 1..5 = 15 via a backward GO TO */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE G; "
                 "BEGIN INTEGER I; INTEGER S; I := 1; S := 0; "
                 "LOOP: S := S + I; I := I + 1; "
                 "IF I <= 5 THEN GO TO LOOP; G := S END; G END", "G") == 15);
    PASS();
#endif
}
TH_REG("arun", arun_goto_loop)

static void arun_call_arg(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* a function called with an argument */
    /* TWICE/RUNIT, not DBL/GO -- those are Burroughs reserved words */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE TWICE(X); VALUE X; INTEGER X; "
                 "TWICE := X TIMES 2; "
                 "INTEGER PROCEDURE RUNIT; RUNIT := TWICE(21); RUNIT END",
                 "RUNIT") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_call_arg)

static void arun_array_simple(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* store, then read back, three array elements */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE T; "
                 "BEGIN INTEGER ARRAY A[1:3]; "
                 "A[1] := 10; A[2] := 20; A[3] := 12; "
                 "T := A[1] + A[2] + A[3] END; T END", "T") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_array_simple)

static void arun_array_loop(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* V[i] := i*i for i in 1..5, then sum: 1+4+9+16+25 = 55 */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE TOT; "
                 "BEGIN INTEGER ARRAY V[1:5]; INTEGER I; INTEGER S; "
                 "FOR I := 1 STEP 1 UNTIL 5 DO V[I] := I TIMES I; "
                 "S := 0; "
                 "FOR I := 1 STEP 1 UNTIL 5 DO S := S + V[I]; "
                 "TOT := S END; TOT END", "TOT") == 55);
    PASS();
#endif
}
TH_REG("arun", arun_array_loop)

static void arun_global(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* G is a program-level global: one procedure writes it, another reads
     * it, across a procedure boundary -- the M4 non-local case. */
    CHECK(run_fn("BEGIN INTEGER G; "
                 "INTEGER PROCEDURE GETG; GETG := G; "
                 "INTEGER PROCEDURE RUN; BEGIN G := 42; RUN := GETG END; "
                 "RUN END", "RUN") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_global)

static void arun_nested_uplevel(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* OUTER has local N; INNER (nested in OUTER) reads N across the lexical
     * boundary -- the display case. OUTER sets N, calls INNER, returns it. */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE OUTER; "
                 "BEGIN INTEGER N; "
                 "INTEGER PROCEDURE INNER; INNER := N + 2; "
                 "N := 40; OUTER := INNER END; "
                 "OUTER END", "OUTER") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_nested_uplevel)

static void arun_nested_param(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* INNER reads OUTER's parameter K (an escaping formal) */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE OUTER(K); VALUE K; INTEGER K; "
                 "BEGIN INTEGER PROCEDURE INNER; INNER := K TIMES 2; "
                 "OUTER := INNER END; "
                 "INTEGER PROCEDURE RUNIT; RUNIT := OUTER(21); RUNIT END",
                 "RUNIT") == 42);
    PASS();
#endif
}
TH_REG("arun", arun_nested_param)

static void arun_recursive_uplevel(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* The display's reason to exist: SUM is recursive AND a nested INNER
     * reads SUM's parameter N. INNER is called AFTER the recursive call
     * returns, so a single static cell for N would be clobbered -- only
     * per-invocation activation records give the right answer.
     * SUM(N) = N + SUM(N-1), SUM(0)=0, so SUM(3) = 3+2+1 = 6. */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE SUM(N); VALUE N; INTEGER N; "
                 "BEGIN INTEGER PROCEDURE INNER; INNER := N; "
                 "IF N <= 0 THEN SUM := 0 "
                 "ELSE BEGIN INTEGER T; T := SUM(N - 1); "
                 "SUM := INNER + T END END; "
                 "INTEGER PROCEDURE RUNIT; RUNIT := SUM(3); RUNIT END",
                 "RUNIT") == 6);
    PASS();
#endif
}
TH_REG("arun", arun_recursive_uplevel)

static void arun_recursion(void)
{
#ifndef _WIN32
    SKIP("JIT requires Windows");
#else
    /* recursive factorial: FACT(5) = 120 */
    CHECK(run_fn("BEGIN INTEGER PROCEDURE FACT(N); VALUE N; INTEGER N; "
                 "BEGIN IF N < 2 THEN FACT := 1 "
                 "ELSE FACT := N TIMES FACT(N - 1) END; "
                 "INTEGER PROCEDURE RUNIT; RUNIT := FACT(5); RUNIT END",
                 "RUNIT") == 120);
    PASS();
#endif
}
TH_REG("arun", arun_recursion)
