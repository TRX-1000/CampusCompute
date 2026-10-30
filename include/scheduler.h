/*
 * scheduler.h - FR2 CPU scheduling: FCFS, Round Robin, non-preemptive Priority.
 */
#ifndef CC_SCHEDULER_H
#define CC_SCHEDULER_H

#include "jobs.h"

#define RR_DEFAULT_QUANTUM 3
#define RR_MAX_QUANTUM     100

typedef enum { POLICY_FCFS, POLICY_RR, POLICY_PRIORITY } policy_t;

/* One bar of the Gantt chart. job_id == -1 means the CPU was idle. */
typedef struct {
    int job_id;
    int start;
    int end;
} gantt_seg_t;

typedef struct {
    gantt_seg_t *segs;
    int count;
    int cap;
} gantt_t;

typedef struct {
    double avg_tat;
    double avg_wt;
    double avg_rt;
    double throughput;     /* jobs per time unit            */
    double cpu_util;       /* % of makespan the CPU is busy */
    int    makespan;       /* last completion - first arrival */
    int    ctx_switches;   /* dispatches - 1                */
} sched_metrics_t;

void gantt_init(gantt_t *g);
void gantt_free(gantt_t *g);

/* Run a policy on `set` (runtime fields are reset first). 0 on success. */
int  schedule_run(policy_t p, job_set_t *set, int quantum, gantt_t *g);
void sched_compute_metrics(const job_set_t *set, const gantt_t *g, sched_metrics_t *m);
void sched_set_trace(int on);

/* CLI entry points */
int  cmd_schedule(policy_t p, const char *path, int quantum, int trace);
int  cmd_compare(const char *path, int quantum);

#endif
