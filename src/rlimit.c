/*
 * rlimit.c - FR7: process resource limits (setrlimit) and cleanup verification.
 *
 * Each limit is applied inside a forked child, so the parent (and your shell)
 * keeps its normal limits:
 *   cpu    RLIMIT_CPU soft=1s hard=2s; child spins -> kernel sends SIGXCPU
 *   nofile RLIMIT_NOFILE soft=10; child opens files until open() fails EMFILE
 *   mem    RLIMIT_AS = current size + 64 MiB; a 128 MiB mmap fails (ENOMEM), 16 MiB works
 */
#include "workers.h"
#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static void fmt_lim(rlim_t v, char *buf, size_t n)
{
    if (v == RLIM_INFINITY)
        snprintf(buf, n, "unlimited");
    else
        snprintf(buf, n, "%llu", (unsigned long long)v);
}

static void show_limit(const char *label, int resource, const char *unit)
{
    struct rlimit rl;
    char s[32], h[32];
    if (getrlimit(resource, &rl) == -1) {
        perror("getrlimit");
        return;
    }
    fmt_lim(rl.rlim_cur, s, sizeof s);
    fmt_lim(rl.rlim_max, h, sizeof h);
    printf("  [child pid %d] %s: soft=%s hard=%s %s\n", (int)getpid(), label, s, h, unit);
}

#define CHILD_WATCHDOG_MS 60000   /* safety net only: the kernel normally ends the child in ~1 s */

/* Fork, run fn in the child and wait for it with wait4 (to get its CPU usage).
 * A wall-clock watchdog kills the child with SIGKILL if it is still running
 * after CHILD_WATCHDOG_MS, so a demo can never hang the terminal.
 * *wall_ms receives the elapsed wall time; *timed_out is set if the watchdog fired. */
static int run_child(void (*fn)(void), int *status, struct rusage *ru, double *wall_ms,
                     int *timed_out)
{
    fflush(stdout);
    *timed_out = 0;
    double start = now_ms();
    pid_t p = fork();
    if (p == -1) {
        perror("fork");
        return -1;
    }
    if (p == 0) {
        fn();
        fflush(stdout);
        _exit(0);
    }
    for (;;) {
        pid_t r = wait4(p, status, WNOHANG, ru);
        if (r == p)
            break;
        if (r == -1 && errno != EINTR) {
            perror("wait4");
            return -1;
        }
        if (now_ms() - start > CHILD_WATCHDOG_MS) {
            printf("  [parent] watchdog: child pid %d still running after %d s -> SIGKILL\n",
                   (int)p, CHILD_WATCHDOG_MS / 1000);
            kill(p, SIGKILL);
            while (wait4(p, status, 0, ru) == -1 && errno == EINTR) { }
            *timed_out = 1;
            break;
        }
        usleep(20000);                 /* poll every 20 ms; parent stays almost idle */
    }
    *wall_ms = now_ms() - start;
    return 0;
}

/* ---------------- RLIMIT_CPU ---------------- */

static void cpu_child(void)
{
    disable_core_dumps();                 /* SIGXCPU would otherwise trigger a core dump */
    show_limit("RLIMIT_CPU before", RLIMIT_CPU, "seconds");
    struct rlimit rl = {1, 2};
    if (setrlimit(RLIMIT_CPU, &rl) == -1) {
        perror("setrlimit(RLIMIT_CPU)");
        _exit(2);
    }
    show_limit("RLIMIT_CPU after ", RLIMIT_CPU, "seconds");
    printf("  [child pid %d] simulating a runaway job: infinite CPU-bound loop...\n", (int)getpid());
    fflush(stdout);
    volatile unsigned long spin = 0;
    for (;;)
        spin++;
}

static int demo_cpu(void)
{
    print_section("RLIMIT_CPU: limit CPU time of a runaway job to 1 second");
    int status, timed_out;
    struct rusage ru;
    double wall;
    if (run_child(cpu_child, &status, &ru, &wall, &timed_out) != 0)
        return -1;
    if (timed_out) {
        printf("  Observed effect: limit NOT enforced within the watchdog period.\n");
        log_event("RLIMIT", "RLIMIT_CPU: watchdog fired, limit not enforced");
        return -1;
    }
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec +
                 ru.ru_stime.tv_usec / 1e6;
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("  [parent] child terminated by signal %d (%s) after %.2f s of CPU time "
               "(%.2f s wall time)\n", sig, signal_name(sig), cpu, wall / 1000.0);
        printf("  Observed effect: the kernel enforced the 1 s soft limit by sending SIGXCPU;\n"
               "                   the runaway loop was stopped and the parent was unaffected.\n");
        log_event("RLIMIT", "RLIMIT_CPU=1s: child killed by %s after %.2fs CPU", signal_name(sig), cpu);
        return (sig == SIGXCPU || sig == SIGKILL) ? 0 : -1;
    }
    printf("  [parent] unexpected: child exited with status %d\n", WEXITSTATUS(status));
    return -1;
}

/* ---------------- RLIMIT_NOFILE ---------------- */

static void nofile_child(void)
{
    show_limit("RLIMIT_NOFILE before", RLIMIT_NOFILE, "descriptors");
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    rl.rlim_cur = 10;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        perror("setrlimit(RLIMIT_NOFILE)");
        _exit(2);
    }
    show_limit("RLIMIT_NOFILE after ", RLIMIT_NOFILE, "descriptors");

    int fds[64], opened = 0;
    for (;;) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd == -1) {
            printf("  [child pid %d] open() #%d failed: %s (errno %d%s)\n", (int)getpid(), opened + 1,
                   strerror(errno), errno, errno == EMFILE ? " = EMFILE" : "");
            break;
        }
        fds[opened++] = fd;
        if (opened == 64)
            break;
    }
    printf("  [child pid %d] successfully opened %d extra descriptors (fds 0-2 already in use)\n",
           (int)getpid(), opened);
    for (int i = 0; i < opened; i++)
        close(fds[i]);
    printf("  [child pid %d] closed all %d descriptors\n", (int)getpid(), opened);
    _exit(opened < 64 ? 0 : 3);
}

static int demo_nofile(void)
{
    print_section("RLIMIT_NOFILE: limit open file descriptors to 10");
    int status, timed_out;
    struct rusage ru;
    double wall;
    if (run_child(nofile_child, &status, &ru, &wall, &timed_out) != 0 || timed_out)
        return -1;
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    printf("  [parent] child exited with status %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    printf("  Observed effect: open() failed with EMFILE once the limit was reached,\n"
           "                   instead of the job exhausting system-wide descriptors.\n");
    log_event("RLIMIT", "RLIMIT_NOFILE=10: open() hit EMFILE (%s)", ok ? "ok" : "unexpected");
    return ok ? 0 : -1;
}

/* ---------------- RLIMIT_AS ---------------- */

static long current_vm_bytes(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    long pages = 0;
    if (f) {
        if (fscanf(f, "%ld", &pages) != 1)
            pages = 0;
        fclose(f);
    }
    return pages * sysconf(_SC_PAGESIZE);
}

/* Ask the kernel directly for anonymous memory. mmap() is a real system call,
 * so an optimising compiler cannot remove it the way it may remove an unused
 * malloc()/free() pair. Returns NULL and leaves errno set on failure. */
static void *map_bytes(size_t n)
{
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static void mem_child(void)
{
    const long MiB = 1024L * 1024L;
    long cur = current_vm_bytes();
    struct rlimit rl;
    getrlimit(RLIMIT_AS, &rl);
    rlim_t target = (rlim_t)(cur + 64 * MiB);
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < target)
        target = rl.rlim_max;                 /* cannot raise above the hard limit */
    rl.rlim_cur = target;                     /* lower only the soft limit */
    printf("  [child pid %d] current address space %ld MiB; setting RLIMIT_AS soft = %ld MiB\n",
           (int)getpid(), cur / MiB, (long)(target / MiB));
    if (setrlimit(RLIMIT_AS, &rl) == -1) {
        perror("setrlimit(RLIMIT_AS)");
        _exit(2);
    }

    errno = 0;
    void *big = map_bytes(128 * MiB);
    int big_err = errno;
    if (big)
        printf("  [child pid %d] mmap(128 MiB) -> succeeded (unexpected)\n", (int)getpid());
    else
        printf("  [child pid %d] mmap(128 MiB) -> failed: %s (%s: over the limit)\n", (int)getpid(),
               strerror(big_err), big_err == ENOMEM ? "ENOMEM" : "errno");

    void *small = map_bytes(16 * MiB);
    if (small)
        memset(small, 1, 16 * MiB);           /* touch every page */
    printf("  [child pid %d] mmap(16 MiB)  -> %s\n", (int)getpid(),
           small ? "succeeded and was written (within the limit)" : "failed (unexpected)");

    int ok = (big == NULL && small != NULL);
    if (big)
        munmap(big, 128 * MiB);
    if (small)
        munmap(small, 16 * MiB);
    _exit(ok ? 0 : 3);
}

static int demo_mem(void)
{
    print_section("RLIMIT_AS: cap a job's virtual memory (+64 MiB headroom)");
    int status, timed_out;
    struct rusage ru;
    double wall;
    if (run_child(mem_child, &status, &ru, &wall, &timed_out) != 0 || timed_out)
        return -1;
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    printf("  [parent] child exited with status %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    printf("  Observed effect: allocations beyond the cap fail cleanly with ENOMEM instead of\n"
           "                   letting one job consume all memory in the lab machine.\n");
    log_event("RLIMIT", "RLIMIT_AS: 128MiB mmap refused, 16MiB allowed (%s)", ok ? "ok" : "unexpected");
    return ok ? 0 : -1;
}

int cmd_rlimit(const char *which)
{
    print_banner("FR7 - Process resource limits (setrlimit)");
    printf("Limits are applied inside forked children; parent pid %d keeps its own limits.\n",
           (int)getpid());
    int all = strcmp(which, "all") == 0, rc = 0;
    if (all || strcmp(which, "cpu") == 0)    rc |= demo_cpu();
    if (all || strcmp(which, "nofile") == 0) rc |= demo_nofile();
    if (all || strcmp(which, "mem") == 0)    rc |= demo_mem();
    printf("\n  RESULT: %s\n", rc == 0 ? "PASS - limits enforced as expected" : "FAIL");
    return rc == 0 ? EXIT_OK : EXIT_RUNTIME;
}

/* ---------------- cleanup verification ---------------- */

/* Look for other running campuscompute processes (stray workers / zombies). */
static int scan_processes(void)
{
    DIR *d = opendir("/proc");
    if (d == NULL)
        return 0;
    int found = 0;
    struct dirent *e;
    pid_t me = getpid();
    while ((e = readdir(d)) != NULL) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        if (*end != '\0' || pid <= 0 || pid == me)
            continue;
        char path[64], buf[256];
        snprintf(path, sizeof path, "/proc/%ld/stat", pid);
        FILE *f = fopen(path, "r");
        if (f == NULL)
            continue;
        if (fgets(buf, sizeof buf, f)) {
            char *l = strchr(buf, '('), *r = strrchr(buf, ')');
            if (l && r && r > l) {
                *r = '\0';
                if (strcmp(l + 1, "campuscompute") == 0) {
                    printf("    pid %ld state %c\n", pid, r[2]);
                    found++;
                }
            }
        }
        fclose(f);
    }
    closedir(d);
    return found;
}

static int force_remove_ipc(void)
{
    DIR *d = opendir("/dev/shm");
    if (d == NULL)
        return 0;
    int removed = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char path[300], name[300];
        struct stat st;
        snprintf(path, sizeof path, "/dev/shm/%s", e->d_name);
        if (stat(path, &st) != 0 || st.st_uid != getuid())
            continue;                                     /* only our own user's objects */
        if (strncmp(e->d_name, "sem." CC_IPC_PREFIX, 4 + strlen(CC_IPC_PREFIX)) == 0) {
            snprintf(name, sizeof name, "/%s", e->d_name + 4);
            if (sem_unlink(name) == 0) { printf("    removed semaphore %s\n", name); removed++; }
        } else if (strncmp(e->d_name, CC_IPC_PREFIX, strlen(CC_IPC_PREFIX)) == 0) {
            snprintf(name, sizeof name, "/%s", e->d_name);
            if (shm_unlink(name) == 0) { printf("    removed shared memory %s\n", name); removed++; }
        }
    }
    closedir(d);
    return removed;
}

int cmd_cleanup_check(int force)
{
    print_banner("FR7 - Cleanup verification");
    int issues = 0;

    int threads = read_thread_count();
    printf("  [1] Threads in this process         : %d %s\n", threads,
           threads == 1 ? "-> PASS (no leftover logger threads)" : "-> CHECK");
    if (threads != 1) issues++;

    errno = 0;
    pid_t w = waitpid(-1, NULL, WNOHANG);
    int no_children = (w == -1 && errno == ECHILD);
    printf("  [2] Un-reaped child processes       : %s\n",
           no_children ? "none (waitpid -> ECHILD) -> PASS" : "present -> FAIL");
    if (!no_children) issues++;

    printf("  [3] Other campuscompute processes   :\n");
    int procs = scan_processes();
    printf("      %d found %s\n", procs, procs == 0 ? "-> PASS (no stray workers/zombies)" : "-> FAIL");
    if (procs) issues++;

    printf("  [4] Program IPC objects in /dev/shm :\n");
    int ipc = list_ipc_objects(NULL, 1);
    if (ipc && force) {
        printf("      --force given: removing this program's leftover objects\n");
        force_remove_ipc();
        ipc = list_ipc_objects(NULL, 0);
    }
    printf("      %d found (prefix '%s') %s\n", ipc, CC_IPC_PREFIX,
           ipc == 0 ? "-> PASS" : "-> FAIL (run: ./campuscompute cleanup-check --force)");
    if (ipc) issues++;

    long lines = log_line_count();
    printf("  [5] Log file                        : %s (%ld lines) - kept on purpose as evidence\n",
           LOG_FILE, lines < 0 ? 0 : lines);

    printf("\n  RESULT: %s\n", issues == 0 ? "ALL CLEAN - no leaked threads, processes or IPC objects"
                                           : "issues found (see above)");
    log_event("CLEANUP", "check: %s", issues == 0 ? "all clean" : "issues found");
    return issues == 0 ? EXIT_OK : EXIT_RUNTIME;
}
