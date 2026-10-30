/*
 * main.c - FR8: command-line interface for CampusCompute and the integrated demo.
 *
 * Every feature is a separate sub-command so it can be tested on its own.
 * Exit codes: 0 = success, 1 = runtime/input-file error, 2 = usage error.
 */
#include "banker.h"
#include "common.h"
#include "ipc.h"
#include "jobs.h"
#include "logger.h"
#include "scheduler.h"
#include "workers.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *prog = "campuscompute";

static void usage(FILE *out)
{
    fprintf(out,
        "%s %s - Intelligent Job Scheduling and Resource Management\n\n"
        "Usage: ./%s <command> [arguments]\n\n"
        "Commands:\n"
        "  help                                   show this help\n"
        "  jobs     <jobfile>                     FR1 load + validate jobs, show lifecycle state\n"
        "  fcfs     <jobfile> [-v]                FR2 First-Come First-Served\n"
        "  rr       <jobfile> [quantum] [-v]      FR2 Round Robin (default quantum %d)\n"
        "  priority <jobfile> [-v]                FR2 non-preemptive Priority (lower = higher)\n"
        "  compare  <jobfile> [quantum]           FR2 compare all three on the same job set\n"
        "  logger   [prod] [cons] [items] [cap]   FR3 pthread producer-consumer logger (3 2 5 4)\n"
        "  ipc      [items]                       FR4 shared memory + named semaphores (12)\n"
        "  banker   <statefile>                   FR5 Banker's safety algorithm\n"
        "  workers  [count] [fail_id] [exit|signal]\n"
        "                                         FR6 fork/waitpid workers, optional SIMULATED failure\n"
        "  rlimit   [cpu|nofile|mem|all]          FR7 setrlimit demonstration (all)\n"
        "  cleanup-check [--force]                FR7 verify no leaked threads/processes/IPC\n"
        "  demo                                   integrated run of every feature + summary\n\n"
        "  -v  prints the job lifecycle trace (NEW -> READY -> RUNNING -> COMPLETED)\n\n"
        "Job file   : one job per line 'id arrival burst priority' ('#' = comment)\n"
        "State file : 'n m', Available[m], Allocation[n][m], Maximum[n][m]\n\n"
        "Examples:\n"
        "  ./%s fcfs data/jobs.txt\n"
        "  ./%s rr data/jobs.txt 3 -v\n"
        "  ./%s banker data/banker_safe.txt\n"
        "  ./%s workers 4 3 exit\n"
        "Log file: %s\n",
        CC_NAME, CC_VERSION, prog, RR_DEFAULT_QUANTUM, prog, prog, prog, prog, LOG_FILE);
}

static int usage_error(const char *fmt, const char *arg)
{
    fprintf(stderr, "Error: ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\nRun './%s help' for usage.\n", prog);
    log_event("CLI", "usage error");
    return EXIT_USAGE;
}

/* Strip a trailing "-v" flag; returns 1 if it was present. */
static int take_verbose(int *argc, char **argv)
{
    if (*argc > 0 && strcmp(argv[*argc - 1], "-v") == 0) {
        (*argc)--;
        return 1;
    }
    return 0;
}

static int run_demo(void)
{
    print_banner("INTEGRATED DEMONSTRATION - CampusCompute (all requirements)");
    printf("Runs every module with repeatable sample inputs from data/, then summarises.\n");
    double t0 = now_ms();

    struct { const char *req, *what, *expected; char observed[96]; int pass; } r[10];
    int n = 0, rc, safe, failed;
    job_set_t set;

    /* FR1 */
    rc = cmd_jobs("data/jobs.txt");
    jobs_load("data/jobs.txt", &set);
    r[n] = (typeof(r[0])){"FR1", "Job loading/validation", "valid jobs loaded", "", rc == 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%d jobs loaded", rc == 0 ? set.count : 0);
    n++;

    /* FR8 (input validation) */
    printf("\n[demo] feeding a deliberately malformed job file:\n");
    rc = cmd_jobs("data/jobs_invalid.txt");
    r[n] = (typeof(r[0])){"FR8", "Malformed input", "file rejected", "", rc != 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc != 0 ? "rejected with line errors" : "accepted!");
    n++;

    /* FR2 */
    rc = cmd_compare("data/jobs.txt", RR_DEFAULT_QUANTUM);
    r[n] = (typeof(r[0])){"FR2", "FCFS / RR / Priority", "3 policies compared", "", rc == 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc == 0 ? "metrics computed (q=3)" : "error");
    n++;

    /* FR3 */
    rc = cmd_logger(3, 2, 5, 4);
    r[n] = (typeof(r[0])){"FR3", "Thread logger", "15 events, none lost", "", rc == 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc == 0 ? "15/15 logged, threads joined" : "mismatch");
    n++;

    /* FR4 */
    rc = cmd_ipc(10);
    r[n] = (typeof(r[0])){"FR4", "Shared memory IPC", "checksum match", "", rc == 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc == 0 ? "10 records, match, unlinked" : "failed");
    n++;

    /* FR5 safe */
    print_banner("FR5 - Banker's algorithm: SAFE test case");
    rc = banker_run_file("data/banker_safe.txt", &safe);
    r[n] = (typeof(r[0])){"FR5", "Banker (safe state)", "SAFE", "", rc == 0 && safe};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc ? "input error" : safe ? "SAFE + sequence" : "UNSAFE");
    n++;

    /* FR5 unsafe */
    print_banner("FR5 - Banker's algorithm: UNSAFE test case");
    rc = banker_run_file("data/banker_unsafe.txt", &safe);
    r[n] = (typeof(r[0])){"FR5", "Banker (unsafe state)", "UNSAFE", "", rc == 0 && !safe};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc ? "input error" : safe ? "SAFE" : "UNSAFE detected");
    n++;

    /* FR6 */
    print_banner("FR6 - Workers with one SIMULATED failure");
    rc = workers_run(4, 3, FAIL_EXIT, &failed);
    r[n] = (typeof(r[0])){"FR6", "Worker failure detect", "1 failure detected", "", rc == 0 && failed == 1};
    snprintf(r[n].observed, sizeof r[n].observed, "3 ok, %d failed (simulated)", failed);
    n++;

    /* FR7 */
    rc = cmd_rlimit("cpu");
    r[n] = (typeof(r[0])){"FR7", "RLIMIT_CPU", "SIGXCPU at 1s", "", rc == 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc == 0 ? "child stopped by SIGXCPU" : "not enforced");
    n++;

    rc = cmd_cleanup_check(0);
    r[n] = (typeof(r[0])){"FR7", "Cleanup verification", "nothing leaked", "", rc == 0};
    snprintf(r[n].observed, sizeof r[n].observed, "%s", rc == 0 ? "all clean" : "leak found");
    n++;

    int passed = 0;
    print_banner("INTEGRATED DEMO SUMMARY");
    printf("  %-4s %-24s %-22s %-30s %-6s\n", "Req", "Check", "Expected", "Observed", "Result");
    printf("  %-4s %-24s %-22s %-30s %-6s\n", "----", "------------------------", "----------------------",
           "------------------------------", "------");
    for (int i = 0; i < n; i++) {
        printf("  %-4s %-24s %-22s %-30s %-6s\n", r[i].req, r[i].what, r[i].expected, r[i].observed,
               r[i].pass ? "PASS" : "FAIL");
        passed += r[i].pass;
    }
    printf("\n  %d/%d checks passed in %.1f s.  Log: %s (%ld lines)\n", passed, n,
           (now_ms() - t0) / 1000.0, LOG_FILE, log_line_count());
    log_event("DEMO", "integrated demo: %d/%d checks passed", passed, n);
    return passed == n ? EXIT_OK : EXIT_RUNTIME;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);    /* line-buffered: no duplicated output across fork() */

    if (argc < 2) {
        usage(stderr);
        return EXIT_USAGE;
    }
    const char *cmd = argv[1];
    int nargs = argc - 2;
    char **a = argv + 2;

    /* record the command line in the event log */
    char cmdline[256] = "";
    for (int i = 1; i < argc && strlen(cmdline) < 200; i++) {
        strncat(cmdline, argv[i], 200 - strlen(cmdline));
        strcat(cmdline, " ");
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(stdout);
        return EXIT_OK;
    }
    log_event("CLI", "command: %s", cmdline);

    if (strcmp(cmd, "jobs") == 0) {
        if (nargs != 1) return usage_error("'jobs' needs exactly one argument: <jobfile>%s", "");
        return cmd_jobs(a[0]);
    }
    if (strcmp(cmd, "fcfs") == 0 || strcmp(cmd, "priority") == 0) {
        int v = take_verbose(&nargs, a);
        if (nargs != 1) return usage_error("'%s' usage: <jobfile> [-v]", cmd);
        return cmd_schedule(cmd[0] == 'f' ? POLICY_FCFS : POLICY_PRIORITY, a[0], 0, v);
    }
    if (strcmp(cmd, "rr") == 0) {
        int v = take_verbose(&nargs, a), q = RR_DEFAULT_QUANTUM;
        if (nargs < 1 || nargs > 2) return usage_error("'rr' usage: <jobfile> [quantum] [-v]%s", "");
        if (nargs == 2 && parse_arg_int("time quantum", a[1], 1, RR_MAX_QUANTUM, &q) != 0)
            return EXIT_USAGE;
        return cmd_schedule(POLICY_RR, a[0], q, v);
    }
    if (strcmp(cmd, "compare") == 0) {
        int q = RR_DEFAULT_QUANTUM;
        if (nargs < 1 || nargs > 2) return usage_error("'compare' usage: <jobfile> [quantum]%s", "");
        if (nargs == 2 && parse_arg_int("time quantum", a[1], 1, RR_MAX_QUANTUM, &q) != 0)
            return EXIT_USAGE;
        return cmd_compare(a[0], q);
    }
    if (strcmp(cmd, "logger") == 0) {
        int v[4] = {3, 2, 5, 4};
        static const char *nm[4] = {"producers", "consumers", "items per producer", "queue capacity"};
        static const long mx[4] = {LOGGER_MAX_PRODUCERS, LOGGER_MAX_CONSUMERS, LOGGER_MAX_ITEMS,
                                   LOGGER_MAX_CAPACITY};
        if (nargs > 4) return usage_error("'logger' takes at most 4 arguments%s", "");
        for (int i = 0; i < nargs; i++)
            if (parse_arg_int(nm[i], a[i], 1, mx[i], &v[i]) != 0)
                return EXIT_USAGE;
        return cmd_logger(v[0], v[1], v[2], v[3]);
    }
    if (strcmp(cmd, "ipc") == 0) {
        int items = 12;
        if (nargs > 1) return usage_error("'ipc' usage: [items]%s", "");
        if (nargs == 1 && parse_arg_int("items", a[0], 1, IPC_MAX_ITEMS, &items) != 0)
            return EXIT_USAGE;
        return cmd_ipc(items);
    }
    if (strcmp(cmd, "banker") == 0) {
        if (nargs != 1) return usage_error("'banker' needs exactly one argument: <statefile>%s", "");
        return cmd_banker(a[0]);
    }
    if (strcmp(cmd, "workers") == 0) {
        int count = 4, fail = 0;
        fail_mode_t mode = FAIL_EXIT;
        if (nargs > 3) return usage_error("'workers' usage: [count] [fail_id] [exit|signal]%s", "");
        if (nargs >= 1 && parse_arg_int("worker count", a[0], 1, WORKERS_MAX, &count) != 0)
            return EXIT_USAGE;
        if (nargs >= 2 && parse_arg_int("fail_id", a[1], 0, count, &fail) != 0)
            return EXIT_USAGE;
        if (nargs == 3) {
            if (strcmp(a[2], "exit") == 0) mode = FAIL_EXIT;
            else if (strcmp(a[2], "signal") == 0) mode = FAIL_SIGNAL;
            else return usage_error("failure mode must be 'exit' or 'signal', got '%s'", a[2]);
        }
        return cmd_workers(count, fail, mode);
    }
    if (strcmp(cmd, "rlimit") == 0) {
        const char *which = nargs == 1 ? a[0] : "all";
        if (nargs > 1 || (strcmp(which, "cpu") && strcmp(which, "nofile") && strcmp(which, "mem") &&
                          strcmp(which, "all")))
            return usage_error("'rlimit' usage: [cpu|nofile|mem|all], got '%s'", nargs ? a[0] : "");
        return cmd_rlimit(which);
    }
    if (strcmp(cmd, "cleanup-check") == 0) {
        if (nargs > 1 || (nargs == 1 && strcmp(a[0], "--force") != 0))
            return usage_error("'cleanup-check' usage: [--force]%s", "");
        return cmd_cleanup_check(nargs == 1);
    }
    if (strcmp(cmd, "demo") == 0) {
        if (nargs != 0) return usage_error("'demo' takes no arguments%s", "");
        return run_demo();
    }
    return usage_error("unknown command '%s'", cmd);
}
