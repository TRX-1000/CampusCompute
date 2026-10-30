/*
 * scheduler.c - FR2: FCFS, Round Robin and non-preemptive Priority scheduling.
 *
 * Simulation uses integer time units. Each policy produces a Gantt chart and
 * fills completion time in every job; metrics are then derived:
 *     TAT = CT - arrival,  WT = TAT - burst,  RT = first start - arrival
 *
 * Tie-breaking rules (stated in every report):
 *   FCFS     : earlier arrival first; equal arrival -> lower job id first.
 *   RR       : FIFO ready queue in (arrival, id) order. A job that arrives at
 *              the same instant a quantum expires joins the queue BEFORE the
 *              pre-empted job.
 *   Priority : lower number = higher priority; ties -> earlier arrival ->
 *              lower job id. Non-preemptive: a running job is never interrupted.
 * If no job is ready the CPU idles until the next arrival (shown as IDLE).
 */
#include "scheduler.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_trace = 0;

void sched_set_trace(int on) { g_trace = on; }

static const char *policy_name(policy_t p)
{
    switch (p) {
    case POLICY_FCFS:     return "FCFS (First-Come, First-Served)";
    case POLICY_RR:       return "Round Robin";
    case POLICY_PRIORITY: return "Priority (non-preemptive)";
    }
    return "?";
}

static const char *policy_short(policy_t p)
{
    switch (p) {
    case POLICY_FCFS:     return "FCFS";
    case POLICY_RR:       return "RR";
    case POLICY_PRIORITY: return "PRIORITY";
    }
    return "?";
}

static const char *tie_rule(policy_t p)
{
    switch (p) {
    case POLICY_FCFS:
        return "earlier arrival first; equal arrival -> lower job ID first";
    case POLICY_RR:
        return "FIFO ready queue by (arrival, job ID); a job arriving when a quantum\n"
               "               expires is queued BEFORE the pre-empted job";
    case POLICY_PRIORITY:
        return "lower number = higher priority; ties -> earlier arrival -> lower job ID;\n"
               "               non-preemptive (running job is never interrupted)";
    }
    return "";
}

/* ---------------- Gantt chart storage ---------------- */

void gantt_init(gantt_t *g) { g->segs = NULL; g->count = 0; g->cap = 0; }

void gantt_free(gantt_t *g) { free(g->segs); gantt_init(g); }

static int gantt_add(gantt_t *g, int job_id, int start, int end)
{
    if (end <= start)
        return 0;
    /* merge with previous bar if the same job simply continues */
    if (g->count > 0 && g->segs[g->count - 1].job_id == job_id && g->segs[g->count - 1].end == start) {
        g->segs[g->count - 1].end = end;
        return 0;
    }
    if (g->count == g->cap) {
        int ncap = g->cap ? g->cap * 2 : 32;
        gantt_seg_t *n = realloc(g->segs, (size_t)ncap * sizeof *n);
        if (n == NULL) {
            perror("realloc");
            return -1;
        }
        g->segs = n;
        g->cap = ncap;
    }
    g->segs[g->count++] = (gantt_seg_t){job_id, start, end};
    return 0;
}

/* ---------------- helpers ---------------- */

static void trace(int t, const job_t *j, const char *what)
{
    if (g_trace)
        printf("    t=%-4d J%-4d %s\n", t, j->id, what);
}

/* indices of jobs sorted by (arrival, id) */
static void arrival_order(const job_set_t *s, int *order)
{
    for (int i = 0; i < s->count; i++)
        order[i] = i;
    for (int i = 1; i < s->count; i++) {          /* insertion sort, n <= 64 */
        int k = order[i], j = i - 1;
        while (j >= 0 && (s->jobs[order[j]].arrival > s->jobs[k].arrival ||
                          (s->jobs[order[j]].arrival == s->jobs[k].arrival &&
                           s->jobs[order[j]].id > s->jobs[k].id))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = k;
    }
}

typedef struct {
    int items[MAX_JOBS];
    int head;
    int count;
} ready_queue_t;

static void rq_push(ready_queue_t *q, int idx)
{
    q->items[(q->head + q->count) % MAX_JOBS] = idx;
    q->count++;
}

static int rq_pop(ready_queue_t *q)
{
    int idx = q->items[q->head];
    q->head = (q->head + 1) % MAX_JOBS;
    q->count--;
    return idx;
}

/* Move every NEW job with arrival <= time into READY (in arrival order).
 * If q is given the job is also appended to the RR ready queue. */
static void admit_arrivals(job_set_t *s, const int *order, int *next, int time, ready_queue_t *q)
{
    while (*next < s->count && s->jobs[order[*next]].arrival <= time) {
        job_t *j = &s->jobs[order[*next]];
        j->status = JOB_READY;
        if (g_trace) {
            char msg[64];
            snprintf(msg, sizeof msg, "arrived -> READY (burst %d, priority %d)", j->burst, j->priority);
            trace(j->arrival, j, msg);
        }
        if (q)
            rq_push(q, order[*next]);
        (*next)++;
    }
}

static void complete_job(job_t *j, int time)
{
    j->remaining = 0;
    j->completion = time;
    j->status = JOB_COMPLETED;
    trace(time, j, "COMPLETED");
}

static void dispatch(job_t *j, int time)
{
    if (!j->started) {
        j->started = 1;
        j->start_time = time;
    }
    j->status = JOB_RUNNING;
    if (g_trace) {
        char msg[64];
        snprintf(msg, sizeof msg, "dispatched -> RUNNING (remaining %d)", j->remaining);
        trace(time, j, msg);
    }
}

/* ---------------- non-preemptive policies (FCFS, Priority) ---------------- */

/* returns non-zero if job a should run before job b */
static int better(policy_t p, const job_t *a, const job_t *b)
{
    if (p == POLICY_PRIORITY && a->priority != b->priority)
        return a->priority < b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

static int schedule_nonpreemptive(policy_t p, job_set_t *s, gantt_t *g)
{
    int order[MAX_JOBS];
    arrival_order(s, order);
    int next = 0, done = 0, time = 0;

    while (done < s->count) {
        admit_arrivals(s, order, &next, time, NULL);

        job_t *pick = NULL;
        for (int i = 0; i < s->count; i++) {
            job_t *j = &s->jobs[i];
            if (j->status == JOB_READY && (pick == NULL || better(p, j, pick)))
                pick = j;
        }
        if (pick == NULL) {                    /* nothing ready: CPU idles */
            int t_next = s->jobs[order[next]].arrival;
            if (gantt_add(g, -1, time, t_next) != 0)
                return -1;
            time = t_next;
            continue;
        }
        dispatch(pick, time);
        if (gantt_add(g, pick->id, time, time + pick->remaining) != 0)
            return -1;
        time += pick->remaining;
        admit_arrivals(s, order, &next, time, NULL);
        complete_job(pick, time);
        done++;
    }
    return 0;
}

/* ---------------- Round Robin ---------------- */

static int schedule_rr(job_set_t *s, int quantum, gantt_t *g)
{
    int order[MAX_JOBS];
    arrival_order(s, order);
    ready_queue_t q = {.head = 0, .count = 0};
    int next = 0, done = 0, time = 0;

    admit_arrivals(s, order, &next, time, &q);
    while (done < s->count) {
        if (q.count == 0) {                    /* CPU idles until next arrival */
            int t_next = s->jobs[order[next]].arrival;
            if (gantt_add(g, -1, time, t_next) != 0)
                return -1;
            time = t_next;
            admit_arrivals(s, order, &next, time, &q);
            continue;
        }
        job_t *j = &s->jobs[rq_pop(&q)];
        dispatch(j, time);
        int slice = j->remaining < quantum ? j->remaining : quantum;
        if (gantt_add(g, j->id, time, time + slice) != 0)
            return -1;
        time += slice;
        j->remaining -= slice;

        /* jobs that arrived during the slice enter the queue first ... */
        admit_arrivals(s, order, &next, time, &q);
        if (j->remaining == 0) {
            complete_job(j, time);
            done++;
        } else {                               /* ... then the pre-empted job */
            j->status = JOB_READY;
            if (g_trace) {
                char msg[80];
                snprintf(msg, sizeof msg, "quantum expired -> READY (remaining %d, back of queue)",
                         j->remaining);
                trace(time, j, msg);
            }
            rq_push(&q, (int)(j - s->jobs));
        }
    }
    return 0;
}

int schedule_run(policy_t p, job_set_t *set, int quantum, gantt_t *g)
{
    jobs_reset(set);
    if (p == POLICY_RR)
        return schedule_rr(set, quantum, g);
    return schedule_nonpreemptive(p, set, g);
}

/* ---------------- metrics and reporting ---------------- */

void sched_compute_metrics(const job_set_t *s, const gantt_t *g, sched_metrics_t *m)
{
    double sum_tat = 0, sum_wt = 0, sum_rt = 0;
    int first_arrival = s->jobs[0].arrival, last_ct = 0;
    long total_burst = 0;

    for (int i = 0; i < s->count; i++) {
        job_t *j = (job_t *)&s->jobs[i];
        j->turnaround = j->completion - j->arrival;
        j->waiting    = j->turnaround - j->burst;
        j->response   = j->start_time - j->arrival;
        sum_tat += j->turnaround;
        sum_wt  += j->waiting;
        sum_rt  += j->response;
        total_burst += j->burst;
        if (j->arrival < first_arrival)
            first_arrival = j->arrival;
        if (j->completion > last_ct)
            last_ct = j->completion;
    }
    int dispatches = 0;
    for (int i = 0; i < g->count; i++)
        if (g->segs[i].job_id != -1)
            dispatches++;

    m->avg_tat = sum_tat / s->count;
    m->avg_wt  = sum_wt / s->count;
    m->avg_rt  = sum_rt / s->count;
    m->makespan = last_ct - first_arrival;
    m->throughput = m->makespan > 0 ? (double)s->count / m->makespan : 0.0;
    m->cpu_util = m->makespan > 0 ? 100.0 * total_burst / m->makespan : 0.0;
    m->ctx_switches = dispatches > 0 ? dispatches - 1 : 0;
}

static void print_execution_order(const gantt_t *g)
{
    printf("Execution order: ");
    int first = 1;
    for (int i = 0; i < g->count; i++) {
        if (g->segs[i].job_id == -1)
            printf("%s(idle)", first ? "" : " -> ");
        else
            printf("%sJ%d", first ? "" : " -> ", g->segs[i].job_id);
        first = 0;
    }
    printf("\n");
}

static void print_gantt(const gantt_t *g)
{
    const int per_row = 10;
    printf("Gantt chart (timeline):\n");
    for (int r = 0; r < g->count; r += per_row) {
        int end = r + per_row < g->count ? r + per_row : g->count;
        printf("  ");
        for (int i = r; i < end; i++) printf("+------");
        printf("+\n  ");
        for (int i = r; i < end; i++) {
            char label[12];
            if (g->segs[i].job_id == -1)
                snprintf(label, sizeof label, "IDLE");
            else
                snprintf(label, sizeof label, "J%d", g->segs[i].job_id);
            int pad = 6 - (int)strlen(label);
            int left = pad / 2;
            printf("|%*s%s%*s", left, "", label, pad - left, "");
        }
        printf("|\n  ");
        for (int i = r; i < end; i++) printf("+------");
        printf("+\n  ");
        for (int i = r; i < end; i++) printf("%-7d", g->segs[i].start);
        printf("%d\n", g->segs[end - 1].end);
    }
}

static void print_job_table(const job_set_t *s)
{
    printf("\n  %-5s %-7s %-5s %-4s %-6s %-5s %-5s %-5s %-5s %-10s\n",
           "Job", "Arrive", "Burst", "Pri", "Start", "CT", "TAT", "WT", "RT", "Status");
    printf("  %-5s %-7s %-5s %-4s %-6s %-5s %-5s %-5s %-5s %-10s\n",
           "-----", "-------", "-----", "----", "------", "-----", "-----", "-----", "-----", "----------");
    for (int i = 0; i < s->count; i++) {
        const job_t *j = &s->jobs[i];
        char name[12];
        snprintf(name, sizeof name, "J%d", j->id);
        printf("  %-5s %-7d %-5d %-4d %-6d %-5d %-5d %-5d %-5d %-10s\n", name, j->arrival, j->burst,
               j->priority, j->start_time, j->completion, j->turnaround, j->waiting, j->response,
               job_status_name(j->status));
    }
}

static void print_metrics(const sched_metrics_t *m)
{
    printf("\n  Average turnaround time : %.2f\n", m->avg_tat);
    printf("  Average waiting time    : %.2f\n", m->avg_wt);
    printf("  Average response time   : %.2f\n", m->avg_rt);
    printf("  Makespan                : %d time units\n", m->makespan);
    printf("  Throughput              : %.3f jobs/time unit\n", m->throughput);
    printf("  CPU utilisation         : %.1f%%\n", m->cpu_util);
    printf("  Context switches        : %d\n", m->ctx_switches);
}

int cmd_schedule(policy_t p, const char *path, int quantum, int trace_on)
{
    char title[96];
    snprintf(title, sizeof title, "FR2 - CPU scheduling: %s", policy_name(p));
    print_banner(title);

    job_set_t set;
    if (jobs_load(path, &set) != 0)
        return EXIT_RUNTIME;

    printf("Job file     : %s (%d jobs)\n", path, set.count);
    if (p == POLICY_RR)
        printf("Time quantum : %d time units\n", quantum);
    printf("Tie-breaking : %s\n", tie_rule(p));

    gantt_t g;
    gantt_init(&g);
    sched_set_trace(trace_on);
    if (trace_on)
        printf("\nLifecycle trace (NEW -> READY -> RUNNING -> COMPLETED):\n");
    int rc = schedule_run(p, &set, quantum, &g);
    sched_set_trace(0);
    if (rc != 0) {
        gantt_free(&g);
        return EXIT_RUNTIME;
    }

    sched_metrics_t m;
    sched_compute_metrics(&set, &g, &m);
    printf("\n");
    print_execution_order(&g);
    print_gantt(&g);
    print_job_table(&set);
    print_metrics(&m);

    if (p == POLICY_RR)
        log_event("SCHED", "%s q=%d on %s: avgWT=%.2f avgTAT=%.2f avgRT=%.2f", policy_short(p),
                  quantum, path, m.avg_wt, m.avg_tat, m.avg_rt);
    else
        log_event("SCHED", "%s on %s: avgWT=%.2f avgTAT=%.2f avgRT=%.2f", policy_short(p), path,
                  m.avg_wt, m.avg_tat, m.avg_rt);
    gantt_free(&g);
    return EXIT_OK;
}

int cmd_compare(const char *path, int quantum)
{
    print_banner("FR2 - Scheduling policy comparison (same job set)");
    job_set_t base;
    if (jobs_load(path, &base) != 0)
        return EXIT_RUNTIME;

    printf("Job file : %s (%d jobs), RR time quantum = %d\n\n", path, base.count, quantum);
    jobs_print(&base);

    const policy_t pols[3] = {POLICY_FCFS, POLICY_RR, POLICY_PRIORITY};
    job_set_t runs[3];
    sched_metrics_t met[3];
    gantt_t gs[3];

    for (int k = 0; k < 3; k++) {
        runs[k] = base;                        /* identical input for every policy */
        gantt_init(&gs[k]);
        if (schedule_run(pols[k], &runs[k], quantum, &gs[k]) != 0) {
            for (int z = 0; z <= k; z++)
                gantt_free(&gs[z]);
            return EXIT_RUNTIME;
        }
        sched_compute_metrics(&runs[k], &gs[k], &met[k]);
    }

    printf("\n  %-14s %-8s %-8s %-8s %-9s %-10s %-9s\n", "Policy", "Avg WT", "Avg TAT", "Avg RT",
           "Makespan", "Throughput", "Ctx-sw");
    printf("  %-14s %-8s %-8s %-8s %-9s %-10s %-9s\n", "--------------", "--------", "--------",
           "--------", "---------", "----------", "---------");
    for (int k = 0; k < 3; k++) {
        char label[24];
        if (pols[k] == POLICY_RR)
            snprintf(label, sizeof label, "RR (q=%d)", quantum);
        else
            snprintf(label, sizeof label, "%s", pols[k] == POLICY_FCFS ? "FCFS" : "Priority (NP)");
        printf("  %-14s %-8.2f %-8.2f %-8.2f %-9d %-10.3f %-9d\n", label, met[k].avg_wt,
               met[k].avg_tat, met[k].avg_rt, met[k].makespan, met[k].throughput,
               met[k].ctx_switches);
    }

    printf("\n  Per-job waiting time:\n  %-6s %-6s %-8s %-9s\n", "Job", "FCFS", "RR", "Priority");
    for (int i = 0; i < base.count; i++)
        printf("  J%-5d %-6d %-8d %-9d\n", base.jobs[i].id, runs[0].jobs[i].waiting,
               runs[1].jobs[i].waiting, runs[2].jobs[i].waiting);

    printf("\n");
    for (int k = 0; k < 3; k++) {
        printf("  %-9s ", policy_short(pols[k]));
        print_execution_order(&gs[k]);
    }

    int best_wt = 0, best_rt = 0;
    for (int k = 1; k < 3; k++) {
        if (met[k].avg_wt < met[best_wt].avg_wt) best_wt = k;
        if (met[k].avg_rt < met[best_rt].avg_rt) best_rt = k;
    }
    printf("\n  Lowest average waiting time  : %s (%.2f)\n", policy_short(pols[best_wt]), met[best_wt].avg_wt);
    printf("  Lowest average response time : %s (%.2f)\n", policy_short(pols[best_rt]), met[best_rt].avg_rt);

    log_event("SCHED", "compare on %s q=%d: avgWT FCFS=%.2f RR=%.2f PRI=%.2f", path, quantum,
              met[0].avg_wt, met[1].avg_wt, met[2].avg_wt);
    for (int k = 0; k < 3; k++)
        gantt_free(&gs[k]);
    return EXIT_OK;
}
