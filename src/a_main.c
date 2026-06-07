/* a_main.c -- BEAC command-line driver.
 *
 * Drives the Burroughs Extended ALGOL pipeline end to end:
 *   source -> a_lex -> a_parse -> a_sema -> a_types/a_lower -> JIR
 *          -> mem2reg -> x86 emit -> object file, or JIT and run.
 *
 * Stages can be dumped for inspection (--lex, --parse, --ir). The default
 * action is to compile; --run JITs a named procedure and prints the integer
 * it returns, which is how you watch a program actually execute. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "fe/a_lex.h"
#include "fe/a_parse.h"
#include "fe/a_sema.h"
#include "fe/a_types.h"
#include "fe/a_lower.h"
#include "ir/jir.h"
#include "x86/x86.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* These are large; keep them out of the stack. */
static char        g_src[1 << 20];
static a_token_t   g_toks[1 << 16];
static a_node_t    g_nodes[1 << 17];
static a_lexer_t   g_lex;
static a_parser_t  g_par;
static a_sema_ctx_t g_sem;
static sema_ctx_t  g_js;
static a_typemap_t g_tm;
static jir_mod_t   g_jir;
static x86_mod_t   g_x86;
static int64_t     g_globals[1 << 16];   /* program globals + display + AR stack */

static int read_file(const char *path, char *buf, size_t max)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp) { fprintf(stderr, "beac: cannot open '%s'\n", path); return -1; }
    n = fread(buf, 1, max - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return (int)n;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: beac [options] <file.alg>\n"
        "\n"
        "  --lex            dump the token stream\n"
        "  --parse          report the parse (node count, errors)\n"
        "  --ir             dump the JIR\n"
        "  --run [PROC]     JIT and run PROC (default: the program), print result\n"
        "  -o <file.obj>    write a COFF object file\n");
}

#ifdef _WIN32
/* JIT the named function into executable memory and return what it gives back
 * in RAX. The AR-stack pointer is seeded first, since calling a procedure
 * directly skips the program entry that would normally do it. */
static int64_t jit_run(const char *fname)
{
    uint32_t i, fi = 0, len, off;
    int found = 0;
    void *mem, *addr;
    int64_t (*fn)(void);
    int64_t result;

    for (i = 0; i < g_jir.n_funcs; i++)
        if (strcmp(g_jir.strs + g_jir.funcs[i].name, fname) == 0) { fi = i; found = 1; break; }
    if (!found) { fprintf(stderr, "beac: no procedure '%s'\n", fname); return -1; }

    memset(g_globals, 0, sizeof(g_globals));
    g_globals[g_tm.gsp_field] = (int64_t)(uintptr_t)&g_globals[g_tm.ar_base_field];

    g_x86.globals_base = (uint64_t)(uintptr_t)g_globals;
    if (x86_emit(&g_x86) != SK_OK) { fprintf(stderr, "beac: codegen failed\n"); return -1; }

    len = g_x86.codelen; off = g_x86.fn_off[fi];
    mem = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return -1;
    memcpy(mem, g_x86.code, len);
    addr = (uint8_t *)mem + off;
    memcpy(&fn, &addr, sizeof(fn));
    result = fn();
    VirtualFree(mem, 0, MEM_RELEASE);
    return result;
}
#endif

int main(int argc, char **argv)
{
    const char *infile = NULL, *outfile = NULL, *runproc = NULL;
    int mode_lex = 0, mode_parse = 0, mode_ir = 0, do_run = 0;
    uint32_t root, i;

    for (i = 1; i < (uint32_t)argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "--lex"))   mode_lex = 1;
        else if (!strcmp(a, "--parse")) mode_parse = 1;
        else if (!strcmp(a, "--ir"))    mode_ir = 1;
        else if (!strcmp(a, "--run")) {
            do_run = 1;
            if (i + 1 < (uint32_t)argc && argv[i + 1][0] != '-') runproc = argv[++i];
        }
        else if (!strcmp(a, "-o") && i + 1 < (uint32_t)argc) outfile = argv[++i];
        else if (a[0] == '-') { fprintf(stderr, "beac: unknown flag '%s'\n", a); usage(); return 1; }
        else infile = a;
    }
    if (!infile) { usage(); return 1; }

    if (read_file(infile, g_src, sizeof(g_src)) < 0) return 1;

    /* ---- Lex ---- */
    a_lex_init(&g_lex, g_src, (uint32_t)strlen(g_src), g_toks, 1 << 16);
    a_lex_run(&g_lex);
    if (g_lex.num_errs) {
        for (i = 0; i < (uint32_t)g_lex.num_errs; i++)
            fprintf(stderr, "%s:%u:%u: %s\n", infile, g_lex.errors[i].line,
                    g_lex.errors[i].col, g_lex.errors[i].msg);
        return 1;
    }
    if (mode_lex) { printf("%u tokens\n", g_lex.num_toks); return 0; }

    /* ---- Parse ---- */
    a_parse_init(&g_par, g_toks, g_lex.num_toks, g_src, g_nodes, 1 << 17);
    root = a_parse_program(&g_par);
    if (g_par.n_errs) {
        for (i = 0; i < (uint32_t)g_par.n_errs; i++)
            fprintf(stderr, "%s:%u:%u: %s\n", infile, g_par.errors[i].line,
                    g_par.errors[i].col, g_par.errors[i].msg);
        return 1;
    }
    if (mode_parse) { printf("parsed: %u nodes, root %u\n", g_par.n_nodes, root); return 0; }

    /* ---- Sema ---- */
    a_sema_init(&g_sem, g_par.nodes, g_par.n_nodes, g_toks, g_src);
    if (a_sema_run(&g_sem, root) != 0) {
        for (i = 0; i < (uint32_t)g_sem.n_errs; i++)
            fprintf(stderr, "%s:%u:%u: %s\n", infile, g_sem.errors[i].line,
                    g_sem.errors[i].col, g_sem.errors[i].msg);
        return 1;
    }

    /* ---- Lower ---- */
    a_types_init(&g_tm, &g_js, A_FP_NATIVE);
    a_lower(&g_jir, &g_js, &g_sem, &g_tm, root);
    jir_m2r(&g_jir);

    if (mode_ir) {
        uint32_t k;
        printf("functions: %u, instructions: %u, blocks: %u\n",
               g_jir.n_funcs, g_jir.n_inst, g_jir.n_blks);
        for (k = 0; k < g_jir.n_funcs; k++)
            printf("  %s: %u blocks, %u insts\n", g_jir.strs + g_jir.funcs[k].name,
                   g_jir.funcs[k].n_blks, g_jir.funcs[k].n_inst);
        return 0;
    }

    /* ---- Codegen ---- */
    x86_init(&g_x86, &g_jir);

    if (do_run) {
#ifdef _WIN32
        int64_t r = jit_run(runproc ? runproc : "main");
        printf("%lld\n", (long long)r);
        return 0;
#else
        fprintf(stderr, "beac: --run needs Windows (JIT via VirtualAlloc)\n");
        return 1;
#endif
    }

    if (x86_emit(&g_x86) != SK_OK) { fprintf(stderr, "beac: codegen failed\n"); return 1; }

    if (outfile) {
        if (x86_coff(&g_x86, outfile) != SK_OK) {
            fprintf(stderr, "beac: cannot write '%s'\n", outfile);
            return 1;
        }
        printf("beac: wrote %s (%u bytes)\n", outfile, g_x86.codelen);
        return 0;
    }

    printf("beac: %u bytes of code (use -o to write, --run to execute)\n", g_x86.codelen);
    return 0;
}
