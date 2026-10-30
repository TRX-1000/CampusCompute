/*
 * util.c - small helpers shared by every module: strict integer parsing,
 * the thread-safe event log, /proc and /dev/shm inspection.
 */
#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

int parse_int(const char *s, long min, long max, int *out)
{
    if (s == NULL || *s == '\0')
        return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0')
        return -1;
    if (v < min || v > max)
        return -2;
    *out = (int)v;
    return 0;
}

int parse_arg_int(const char *what, const char *s, long min, long max, int *out)
{
    int rc = parse_int(s, min, max, out);
    if (rc == -1)
        fprintf(stderr, "Error: %s must be an integer, got '%s'\n", what, s ? s : "");
    else if (rc == -2)
        fprintf(stderr, "Error: %s must be between %ld and %ld, got %s\n", what, min, max, s);
    return rc;
}

static int ensure_log_dir(void)
{
    if (mkdir(LOG_DIR, 0755) == -1 && errno != EEXIST)
        return -1;
    return 0;
}

void log_event(const char *module, const char *fmt, ...)
{
    /* One mutex serialises writers inside this process; O_APPEND (fopen "a")
     * keeps lines from different processes from overwriting each other. */
    pthread_mutex_lock(&g_log_mutex);
    if (ensure_log_dir() == 0) {
        FILE *f = fopen(LOG_FILE, "a");
        if (f != NULL) {
            struct timespec ts;
            struct tm tm;
            char tbuf[32];
            clock_gettime(CLOCK_REALTIME, &ts);
            localtime_r(&ts.tv_sec, &tm);
            strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", &tm);
            fprintf(f, "%s.%03ld [pid %d] [%-7s] ", tbuf, ts.tv_nsec / 1000000L,
                    (int)getpid(), module);
            va_list ap;
            va_start(ap, fmt);
            vfprintf(f, fmt, ap);
            va_end(ap);
            fputc('\n', f);
            fclose(f);
        }
    }
    pthread_mutex_unlock(&g_log_mutex);
}

long log_line_count(void)
{
    FILE *f = fopen(LOG_FILE, "r");
    if (f == NULL)
        return -1;
    long lines = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n')
            lines++;
    fclose(f);
    return lines;
}

void print_banner(const char *title)
{
    printf("\n====================================================================\n");
    printf("  %s\n", title);
    printf("====================================================================\n");
}

void print_section(const char *title)
{
    printf("\n--- %s ---\n", title);
}

int read_thread_count(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (f == NULL)
        return -1;
    char line[256];
    int threads = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "Threads:", 8) == 0) {
            threads = atoi(line + 8);
            break;
        }
    }
    fclose(f);
    return threads;
}

int list_ipc_objects(const char *filter, int print)
{
    DIR *d = opendir("/dev/shm");
    if (d == NULL) {
        if (print)
            printf("    (cannot open /dev/shm: %s)\n", strerror(errno));
        return 0;
    }
    int found = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, CC_IPC_PREFIX) == NULL)
            continue;
        if (filter != NULL && strstr(e->d_name, filter) == NULL)
            continue;
        found++;
        if (print) {
            const char *kind = strncmp(e->d_name, "sem.", 4) == 0 ? "named semaphore"
                                                                 : "shared memory  ";
            printf("    /dev/shm/%-45s (%s)\n", e->d_name, kind);
        }
    }
    closedir(d);
    return found;
}

const char *signal_name(int sig)
{
    switch (sig) {
    case SIGABRT: return "SIGABRT";
    case SIGTERM: return "SIGTERM";
    case SIGKILL: return "SIGKILL";
    case SIGSEGV: return "SIGSEGV";
    case SIGXCPU: return "SIGXCPU";
    case SIGINT:  return "SIGINT";
    case SIGUSR1: return "SIGUSR1";
    case SIGPIPE: return "SIGPIPE";
    default:      return "SIG?";
    }
}

void disable_core_dumps(void)
{
    struct rlimit no_core = {0, 0};
    setrlimit(RLIMIT_CORE, &no_core);
    /* non-dumpable: the kernel skips the core dump entirely, so the
     * system crash reporter is never started for our test processes */
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
}

double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
