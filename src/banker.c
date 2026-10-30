/*
 * banker.c - FR5: Banker's safety algorithm.
 *
 * State file format ('#' starts a comment, whitespace/newlines free-form):
 *     n m                      number of processes, number of resource types
 *     Available[m]
 *     Allocation[n][m]         one row per process
 *     Maximum[n][m]            one row per process
 * Need = Maximum - Allocation is computed by the program.
 *
 * Safety algorithm:
 *     Work = Available, Finish[i] = false
 *     repeat: find i with !Finish[i] and Need[i] <= Work
 *             Work += Allocation[i]; Finish[i] = true; append i to sequence
 *     until no such i exists.  Safe <=> every Finish[i] is true.
 * Scan order is P0..Pn-1 each pass, so the reported sequence is deterministic.
 */
#include "banker.h"
#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int n, m;
    int available[BANKER_MAX_R];
    int alloc[BANKER_MAX_P][BANKER_MAX_R];
    int max[BANKER_MAX_P][BANKER_MAX_R];
    int need[BANKER_MAX_P][BANKER_MAX_R];
} bank_state_t;

#define MAX_TOKENS (2 + BANKER_MAX_R + 2 * BANKER_MAX_P * BANKER_MAX_R)

/* Read every integer token in the file, rejecting anything non-numeric. */
static int read_tokens(const char *path, int *vals, int *count)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Error: cannot open state file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    char line[512];
    int lineno = 0, n = 0, errors = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        char *save = NULL;
        for (char *t = strtok_r(line, " \t\r\n,", &save); t; t = strtok_r(NULL, " \t\r\n,", &save)) {
            int v;
            if (parse_int(t, 0, BANKER_MAX_VAL, &v) != 0) {
                fprintf(stderr, "  %s:%d: '%s' is not an integer in 0..%d\n", path, lineno, t,
                        BANKER_MAX_VAL);
                errors++;
                continue;
            }
            if (n >= MAX_TOKENS) {
                fprintf(stderr, "  %s:%d: too many values\n", path, lineno);
                errors++;
                break;
            }
            vals[n++] = v;
        }
    }
    fclose(f);
    *count = n;
    return errors ? -1 : 0;
}

static int load_state(const char *path, bank_state_t *s)
{
    static int vals[MAX_TOKENS];
    int cnt = 0;
    if (read_tokens(path, vals, &cnt) != 0) {
        fprintf(stderr, "Error: invalid Banker state file '%s'\n", path);
        return -1;
    }
    if (cnt < 2) {
        fprintf(stderr, "Error: '%s' must start with 'n m' (processes, resource types)\n", path);
        return -1;
    }
    s->n = vals[0];
    s->m = vals[1];
    if (s->n < 1 || s->n > BANKER_MAX_P || s->m < 1 || s->m > BANKER_MAX_R) {
        fprintf(stderr, "Error: need 1..%d processes and 1..%d resource types (got n=%d m=%d)\n",
                BANKER_MAX_P, BANKER_MAX_R, s->n, s->m);
        return -1;
    }
    int expected = 2 + s->m + 2 * s->n * s->m;
    if (cnt != expected) {
        fprintf(stderr, "Error: expected %d numbers for n=%d m=%d (Available + Allocation + Maximum), found %d\n",
                expected, s->n, s->m, cnt);
        return -1;
    }
    int k = 2;
    for (int j = 0; j < s->m; j++) s->available[j] = vals[k++];
    for (int i = 0; i < s->n; i++) for (int j = 0; j < s->m; j++) s->alloc[i][j] = vals[k++];
    for (int i = 0; i < s->n; i++) for (int j = 0; j < s->m; j++) s->max[i][j] = vals[k++];

    int bad = 0;
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            if (s->alloc[i][j] > s->max[i][j]) {
                fprintf(stderr, "Error: P%d resource %c: Allocation %d exceeds Maximum %d\n", i,
                        'A' + j, s->alloc[i][j], s->max[i][j]);
                bad = 1;
            }
            s->need[i][j] = s->max[i][j] - s->alloc[i][j];
        }
    return bad ? -1 : 0;
}

static void print_vec(const int *v, int m)
{
    printf("[");
    for (int j = 0; j < m; j++)
        printf("%s%d", j ? " " : "", v[j]);
    printf("]");
}

static void print_state(const bank_state_t *s)
{
    /* each matrix block is W characters wide so header and rows line up */
    int W = 3 * s->m + 2;
    if (W < 12)
        W = 12;
    const char *names[3] = {" Allocation", " Maximum", " Need"};
    char cell[64];

    printf("  %-8s", "Process");
    for (int t = 0; t < 3; t++)
        printf("|%-*s", W, names[t]);
    printf("\n  %-8s", "");
    for (int t = 0; t < 3; t++) {
        int len = 0;
        for (int j = 0; j < s->m; j++)
            len += snprintf(cell + len, sizeof cell - len, "%3c", 'A' + j);
        printf("|%-*s", W, cell);
    }
    printf("\n");
    for (int i = 0; i < s->n; i++) {
        printf("  P%-7d", i);
        const int *rows[3] = {s->alloc[i], s->max[i], s->need[i]};
        for (int t = 0; t < 3; t++) {
            int len = 0;
            for (int j = 0; j < s->m; j++)
                len += snprintf(cell + len, sizeof cell - len, "%3d", rows[t][j]);
            printf("|%-*s", W, cell);
        }
        printf("\n");
    }
    printf("\n  Available : ");
    print_vec(s->available, s->m);
    printf("   (resource types ");
    for (int j = 0; j < s->m; j++) printf("%c", 'A' + j);
    printf(")\n");
}

static int need_le_work(const bank_state_t *s, int i, const int *work)
{
    for (int j = 0; j < s->m; j++)
        if (s->need[i][j] > work[j])
            return 0;
    return 1;
}

/* Returns 1 if safe, 0 if unsafe; fills seq/seq_len. */
static int safety_check(const bank_state_t *s, int *seq, int *seq_len)
{
    int work[BANKER_MAX_R], finish[BANKER_MAX_P] = {0};
    memcpy(work, s->available, sizeof work);
    int k = 0, pass = 0, progress = 1;

    printf("\n  Safety algorithm trace (Work starts as Available = ");
    print_vec(work, s->m);
    printf("):\n");

    while (progress && k < s->n) {
        progress = 0;
        pass++;
        for (int i = 0; i < s->n; i++) {
            if (finish[i] || !need_le_work(s, i, work))
                continue;
            printf("    pass %d: P%d Need ", pass, i);
            print_vec(s->need[i], s->m);
            printf(" <= Work ");
            print_vec(work, s->m);
            for (int j = 0; j < s->m; j++)
                work[j] += s->alloc[i][j];
            printf(" -> P%d finishes, releases ", i);
            print_vec(s->alloc[i], s->m);
            printf(", Work = ");
            print_vec(work, s->m);
            printf("\n");
            finish[i] = 1;
            seq[k++] = i;
            progress = 1;
        }
    }
    *seq_len = k;
    if (k == s->n)
        return 1;

    printf("    no remaining process can be satisfied with Work ");
    print_vec(work, s->m);
    printf(":\n");
    for (int i = 0; i < s->n; i++) {
        if (finish[i])
            continue;
        printf("      P%d Need ", i);
        print_vec(s->need[i], s->m);
        printf(" exceeds Work on:");
        for (int j = 0; j < s->m; j++)
            if (s->need[i][j] > work[j])
                printf(" %c (short by %d)", 'A' + j, s->need[i][j] - work[j]);
        printf("\n");
    }
    return 0;
}

int banker_run_file(const char *path, int *is_safe)
{
    bank_state_t s;
    if (load_state(path, &s) != 0)
        return -1;
    printf("State file: %s  (%d processes, %d resource types)\n", path, s.n, s.m);
    printf("Need = Maximum - Allocation\n\n");
    print_state(&s);

    int seq[BANKER_MAX_P], len = 0;
    int safe = safety_check(&s, seq, &len);

    printf("\n  RESULT: ");
    if (safe) {
        printf("SAFE state. Safe sequence: <");
        for (int i = 0; i < len; i++)
            printf("%sP%d", i ? ", " : "", seq[i]);
        printf(">\n");
        log_event("BANKER", "%s: SAFE", path);
    } else {
        printf("UNSAFE state. Only %d of %d processes can be guaranteed to finish", len, s.n);
        if (len > 0) {
            printf(" (");
            for (int i = 0; i < len; i++)
                printf("%sP%d", i ? ", " : "", seq[i]);
            printf(")");
        }
        printf(";\n          the rest could deadlock, so this state must not be entered.\n");
        log_event("BANKER", "%s: UNSAFE", path);
    }
    *is_safe = safe;
    return 0;
}

int cmd_banker(const char *path)
{
    print_banner("FR5 - Banker's algorithm: resource-safety analysis");
    int safe = 0;
    return banker_run_file(path, &safe) == 0 ? EXIT_OK : EXIT_RUNTIME;
}
