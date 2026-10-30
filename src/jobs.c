/*
 * jobs.c - FR1: job records, input validation and lifecycle state.
 *
 * Job file format (one job per line, '#' starts a comment):
 *     id  arrival  burst  priority
 * Every line is validated; ALL errors are reported with line numbers and the
 * whole file is rejected if any line is bad, so nothing is half-scheduled.
 */
#include "jobs.h"
#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

const char *job_status_name(job_status_t s)
{
    switch (s) {
    case JOB_NEW:       return "NEW";
    case JOB_READY:     return "READY";
    case JOB_RUNNING:   return "RUNNING";
    case JOB_COMPLETED: return "COMPLETED";
    }
    return "?";
}

void jobs_reset(job_set_t *set)
{
    for (int i = 0; i < set->count; i++) {
        job_t *j = &set->jobs[i];
        j->remaining  = j->burst;
        j->started    = 0;
        j->start_time = -1;
        j->completion = 0;
        j->turnaround = 0;
        j->waiting    = 0;
        j->response   = 0;
        j->status     = JOB_NEW;
    }
}

int jobs_load(const char *path, job_set_t *set)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    static const char *field[4] = {"job id", "arrival time", "CPU burst", "priority"};
    static const long  minv[4]  = {1, 0, 1, 0};
    static const long  maxv[4]  = {JOB_ID_MAX, JOB_ARRIVAL_MAX, JOB_BURST_MAX, JOB_PRIORITY_MAX};

    char line[256];
    int lineno = 0, errors = 0;
    set->count = 0;

    while (fgets(line, sizeof line, f) != NULL) {
        lineno++;
        if (strchr(line, '\n') == NULL && !feof(f)) {
            fprintf(stderr, "  %s:%d: line too long\n", path, lineno);
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') { }
            errors++;
            continue;
        }
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';

        char *tok[4];
        int ntok = 0;
        char *save = NULL;
        for (char *t = strtok_r(line, " \t\r\n", &save); t; t = strtok_r(NULL, " \t\r\n", &save)) {
            if (ntok < 4)
                tok[ntok] = t;
            ntok++;
        }
        if (ntok == 0)
            continue;                         /* blank or comment-only line */
        if (ntok != 4) {
            fprintf(stderr, "  %s:%d: expected 4 fields (id arrival burst priority), found %d\n",
                    path, lineno, ntok);
            errors++;
            continue;
        }

        int v[4], bad = 0;
        for (int k = 0; k < 4; k++) {
            if (parse_int(tok[k], minv[k], maxv[k], &v[k]) != 0) {
                fprintf(stderr, "  %s:%d: invalid %s '%s' (must be an integer %ld..%ld)\n",
                        path, lineno, field[k], tok[k], minv[k], maxv[k]);
                bad = 1;
            }
        }
        if (bad) {
            errors++;
            continue;
        }
        int dup = 0;
        for (int k = 0; k < set->count; k++)
            if (set->jobs[k].id == v[0])
                dup = 1;
        if (dup) {
            fprintf(stderr, "  %s:%d: duplicate job id %d\n", path, lineno, v[0]);
            errors++;
            continue;
        }
        if (set->count >= MAX_JOBS) {
            fprintf(stderr, "  %s:%d: too many jobs (maximum %d)\n", path, lineno, MAX_JOBS);
            errors++;
            continue;
        }
        job_t *j = &set->jobs[set->count++];
        j->id = v[0];
        j->arrival = v[1];
        j->burst = v[2];
        j->priority = v[3];
    }
    fclose(f);

    if (errors > 0) {
        fprintf(stderr, "Error: %d invalid line(s) in '%s' - file rejected, nothing was scheduled.\n",
                errors, path);
        return -1;
    }
    if (set->count == 0) {
        fprintf(stderr, "Error: '%s' contains no jobs.\n", path);
        return -1;
    }
    jobs_reset(set);
    return 0;
}

void jobs_print(const job_set_t *set)
{
    printf("  %-6s %-8s %-6s %-9s %-10s\n", "Job", "Arrival", "Burst", "Priority", "Status");
    printf("  %-6s %-8s %-6s %-9s %-10s\n", "------", "--------", "------", "---------", "----------");
    for (int i = 0; i < set->count; i++) {
        const job_t *j = &set->jobs[i];
        char name[16];
        snprintf(name, sizeof name, "J%d", j->id);
        printf("  %-6s %-8d %-6d %-9d %-10s\n", name, j->arrival, j->burst, j->priority,
               job_status_name(j->status));
    }
}

int cmd_jobs(const char *path)
{
    print_banner("FR1 - Job loading and validation");
    job_set_t set;
    printf("Input file : %s\n", path);
    if (jobs_load(path, &set) != 0) {
        log_event("JOBS", "rejected job file %s", path);
        return EXIT_RUNTIME;
    }
    printf("Result     : %d job(s) loaded, all fields valid\n", set.count);
    printf("Rules      : id 1..%d unique, arrival 0..%d, burst 1..%d, priority 0..%d (lower = higher)\n\n",
           JOB_ID_MAX, JOB_ARRIVAL_MAX, JOB_BURST_MAX, JOB_PRIORITY_MAX);
    jobs_print(&set);
    log_event("JOBS", "loaded %d jobs from %s", set.count, path);
    return EXIT_OK;
}
