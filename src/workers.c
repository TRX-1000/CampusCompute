/*
 * workers.c - FR6: worker processes and failure detection.
 *
 * The parent fork()s one worker per job. Each worker does real CPU work
 * (counts primes) and exits normally with status 0. One worker can be told to
 * fail on purpose - this is a CONTROLLED, SIMULATED failure, not a real OS
 * fault:
 *     exit   mode: worker calls _exit(42)               -> non-zero exit status
 *     signal mode: worker calls abort() -> SIGABRT       -> abnormal termination
 * The parent collects every child with waitpid() and decodes the status with
 * WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG.
 */
#include "workers.h"
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIM_FAIL_EXIT_CODE 42

static long count_primes(long limit)
{
    long count = 0;
    for (long n = 2; n < limit; n++) {
        int prime = 1;
        for (long d = 2; d * d <= n; d++)
            if (n % d == 0) { prime = 0; break; }
        count += prime;
    }
    return count;
}

static void worker_main(int id, int burst, int fail, fail_mode_t mode)
{
    pid_t me = getpid();
    printf("  [W%d pid %d] started job J%d (burst %d) - parent pid %d\n", id, (int)me, id, burst,
           (int)getppid());
    fflush(stdout);

    long primes = count_primes(20000L * burst);  /* real CPU work */
    usleep(120000 * burst);                      /* spread completions out in time */

    if (fail) {
        disable_core_dumps();                    /* no core file, no crash-reporter run */
        if (mode == FAIL_EXIT) {
            printf("  [W%d pid %d] *** SIMULATED FAILURE (controlled test, not a real OS fault): "
                   "calling _exit(%d)\n", id, (int)me, SIM_FAIL_EXIT_CODE);
            fflush(stdout);
            _exit(SIM_FAIL_EXIT_CODE);
        }
        printf("  [W%d pid %d] *** SIMULATED FAILURE (controlled test, not a real OS fault): "
               "calling abort() -> SIGABRT\n", id, (int)me);
        fflush(stdout);
        abort();
    }
    printf("  [W%d pid %d] finished job J%d (%ld primes found) -> exit(0)\n", id, (int)me, id, primes);
    fflush(stdout);
    _exit(0);
}

int workers_run(int count, int fail_worker, fail_mode_t mode, int *failed)
{
    pid_t pids[WORKERS_MAX];
    double t0 = now_ms();
    int spawned = 0;

    printf("Parent pid %d spawning %d worker(s)", (int)getpid(), count);
    if (fail_worker > 0)
        printf("; worker W%d will SIMULATE a failure (%s)", fail_worker,
               mode == FAIL_EXIT ? "exit code 42" : "abort/SIGABRT");
    printf("\n\n");

    for (int i = 0; i < count; i++) {
        int id = i + 1;
        int burst = (id % 3) + 1;
        fflush(stdout);
        pid_t p = fork();
        if (p == -1) {
            perror("fork");
            break;
        }
        if (p == 0)
            worker_main(id, burst, id == fail_worker, mode);
        pids[i] = p;
        spawned++;
        printf("  [parent] fork() -> W%d is pid %d\n", id, (int)p);
    }
    printf("\n");

    typedef struct { int id; pid_t pid; int status; double ms; } result_t;
    result_t res[WORKERS_MAX];
    int nres = 0, nfail = 0;

    /* collect children in the order they actually terminate */
    while (nres < spawned) {
        int status;
        pid_t w = waitpid(-1, &status, 0);
        if (w == -1) {
            if (errno == EINTR)
                continue;
            perror("waitpid");
            break;
        }
        int id = 0;
        for (int i = 0; i < spawned; i++)
            if (pids[i] == w)
                id = i + 1;
        res[nres++] = (result_t){id, w, status, now_ms() - t0};
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("  [parent] waitpid() -> pid %d (W%d): exited normally, status 0\n", (int)w, id);
        } else if (WIFEXITED(status)) {
            printf("  [parent] waitpid() -> pid %d (W%d): FAILURE DETECTED, exit status %d\n", (int)w,
                   id, WEXITSTATUS(status));
            nfail++;
        } else if (WIFSIGNALED(status)) {
            printf("  [parent] waitpid() -> pid %d (W%d): FAILURE DETECTED, killed by signal %d (%s)\n",
                   (int)w, id, WTERMSIG(status), signal_name(WTERMSIG(status)));
            nfail++;
        }
    }

    print_section("Worker status report (decoded from waitpid)");
    printf("  %-6s %-8s %-5s %-30s %-10s\n", "Worker", "PID", "Job", "Outcome", "Ended at");
    for (int i = 0; i < nres; i++) {
        char outcome[64];
        int st = res[i].status;
        int simulated = (res[i].id == fail_worker);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
            snprintf(outcome, sizeof outcome, "COMPLETED (exit 0)");
        else if (WIFEXITED(st))
            snprintf(outcome, sizeof outcome, "FAILED%s exit %d", simulated ? " [SIM]" : "",
                     WEXITSTATUS(st));
        else if (WIFSIGNALED(st))
            snprintf(outcome, sizeof outcome, "FAILED%s signal %d %s", simulated ? " [SIM]" : "",
                     WTERMSIG(st), signal_name(WTERMSIG(st)));
        else
            snprintf(outcome, sizeof outcome, "UNKNOWN status 0x%x", st);
        printf("  W%-5d %-8d J%-4d %-30s %7.0f ms\n", res[i].id, (int)res[i].pid, res[i].id, outcome,
               res[i].ms);
        log_event("WORKER", "W%d pid %d: %s", res[i].id, (int)res[i].pid, outcome);
    }
    printf("\n  Summary: %d worker(s), %d completed normally, %d failure(s) detected by parent\n",
           nres, nres - nfail, nfail);
    if (fail_worker > 0)
        printf("  Note: the failure of W%d was deliberately SIMULATED to test detection.\n", fail_worker);
    if (nfail > 0)
        printf("  Action: failed job(s) marked FAILED; scheduler would re-queue them.\n");

    if (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD)
        printf("  All children reaped - no zombie processes remain.\n");
    *failed = nfail;
    return nres == count ? 0 : -1;
}

int cmd_workers(int count, int fail_worker, fail_mode_t mode)
{
    print_banner("FR6 - Worker processes and failure detection (fork + waitpid)");
    int failed = 0;
    return workers_run(count, fail_worker, mode, &failed) == 0 ? EXIT_OK : EXIT_RUNTIME;
}
