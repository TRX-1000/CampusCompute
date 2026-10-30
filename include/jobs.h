/*
 * jobs.h - job representation and loading/validation (FR1).
 */
#ifndef CC_JOBS_H
#define CC_JOBS_H

#define MAX_JOBS 64

/* Limits enforced when reading a job file */
#define JOB_ID_MAX       9999
#define JOB_ARRIVAL_MAX  100000
#define JOB_BURST_MAX    1000
#define JOB_PRIORITY_MAX 99

/* Process-lifecycle states a job moves through */
typedef enum {
    JOB_NEW,        /* loaded, not yet arrived            */
    JOB_READY,      /* arrived, waiting for the CPU       */
    JOB_RUNNING,    /* currently on the CPU               */
    JOB_COMPLETED   /* finished all of its CPU burst      */
} job_status_t;

typedef struct {
    int id;
    int arrival;       /* arrival time                         */
    int burst;         /* total CPU burst                      */
    int priority;      /* lower number = higher priority       */

    /* filled in by a scheduler */
    int remaining;     /* CPU time still needed                */
    int started;       /* has it been dispatched at least once */
    int start_time;    /* first time it got the CPU            */
    int completion;    /* completion time (CT)                 */
    int turnaround;    /* TAT = CT - arrival                   */
    int waiting;       /* WT  = TAT - burst                    */
    int response;      /* RT  = first start - arrival          */
    job_status_t status;
} job_t;

typedef struct {
    job_t jobs[MAX_JOBS];
    int   count;
} job_set_t;

/* Load and validate "id arrival burst priority" lines. Returns 0 or -1. */
int  jobs_load(const char *path, job_set_t *set);
/* Reset runtime fields so the same set can be scheduled again. */
void jobs_reset(job_set_t *set);
void jobs_print(const job_set_t *set);
const char *job_status_name(job_status_t s);

int  cmd_jobs(const char *path);

#endif
