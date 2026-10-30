/*
 * workers.h - FR6 worker processes and failure detection,
 *             FR7 resource limits and cleanup verification.
 */
#ifndef CC_WORKERS_H
#define CC_WORKERS_H

#define WORKERS_MAX 16

typedef enum { FAIL_EXIT, FAIL_SIGNAL } fail_mode_t;

/* fail_worker = 0 means no simulated failure. *failed receives the number of
 * workers the parent detected as failed. */
int workers_run(int count, int fail_worker, fail_mode_t mode, int *failed);
int cmd_workers(int count, int fail_worker, fail_mode_t mode);

/* which: "cpu", "nofile", "mem" or "all" */
int cmd_rlimit(const char *which);

int cmd_cleanup_check(int force);

#endif
