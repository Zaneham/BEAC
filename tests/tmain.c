/* tmain.c -- BEAC test runner.
 *
 * Self-registering tests (via TH_REG in tharns.h) drop themselves into
 * th_list at load time; this just walks the list, runs each, and tallies.
 * No shared pipeline state: each BEAC test file owns its own lexer/parser/
 * sema/lower context, since they are cheap enough to keep per-file. */

#include "tharns.h"
#include <stdio.h>
#include <string.h>

tcase_t th_list[TH_MAXTS];
int th_cnt = 0;
int npass  = 0;
int nfail  = 0;
int nskip  = 0;

static void prt_res(const char *tname, const char *tag)
{
    int nlen = (int)strlen(tname);
    int dots = 30 - nlen;
    int i;
    if (dots < 3) dots = 3;
    printf("  %s ", tname);
    for (i = 0; i < dots; i++) putchar('.');
    printf(" %s\n", tag);
}

static void run_one(tcase_t *tc)
{
    int wp = npass, wf = nfail, ws = nskip;
    tc->func();
    if (nfail > wf)       prt_res(tc->tname, "FAIL");
    else if (nskip > ws)  prt_res(tc->tname, "SKIP");
    else if (npass > wp)  prt_res(tc->tname, "PASS");
    else { npass++; prt_res(tc->tname, "PASS"); }
}

int main(int argc, char *argv[])
{
    const char *ftest = NULL;
    int i, total;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--test") && i + 1 < argc) ftest = argv[++i];

    printf("BEAC Test Suite\n");
    printf("===============\n");

    /* group output by category, in first-seen order */
    {
        static const char *seen[TH_MAXTS];
        int n_seen = 0, s, t;
        for (t = 0; t < th_cnt; t++) {
            const char *cat = th_list[t].tcats;
            int known = 0;
            for (s = 0; s < n_seen; s++) if (!strcmp(seen[s], cat)) { known = 1; break; }
            if (known) continue;
            seen[n_seen++] = cat;
            { int hdr = 0;
              for (i = 0; i < th_cnt; i++) {
                  if (strcmp(th_list[i].tcats, cat) != 0) continue;
                  if (ftest && strcmp(ftest, th_list[i].tname) != 0) continue;
                  if (!hdr) { printf("[%s]\n", cat); hdr = 1; }
                  run_one(&th_list[i]);
              } }
        }
    }

    total = npass + nfail + nskip;
    printf("===============\n");
    printf("%d tests: %d passed, %d failed, %d skipped\n",
           total, npass, nfail, nskip);
    return nfail > 0 ? 1 : 0;
}
