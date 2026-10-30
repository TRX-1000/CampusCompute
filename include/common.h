/*
 * common.h - shared constants and helper declarations for CampusCompute.
 */
#ifndef CC_COMMON_H
#define CC_COMMON_H

#include <stdio.h>

#define CC_NAME        "CampusCompute"
#define CC_VERSION     "1.0"
#define CC_IPC_PREFIX  "campuscompute_"     /* every POSIX IPC object we create starts with this */
#define LOG_DIR        "logs"
#define LOG_FILE       "logs/campuscompute.log"

/* Exit codes used by the CLI */
#define EXIT_OK        0
#define EXIT_RUNTIME   1
#define EXIT_USAGE     2

/* Strictly parse a base-10 integer in [min, max].
 * Returns 0 on success, -1 if not an integer, -2 if out of range. */
int  parse_int(const char *s, long min, long max, int *out);

/* Same as parse_int but prints a helpful error naming the argument. */
int  parse_arg_int(const char *what, const char *s, long min, long max, int *out);

/* Append one timestamped line to logs/campuscompute.log (thread-safe). */
void log_event(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Count lines currently in the log file (-1 if it does not exist). */
long log_line_count(void);

void print_banner(const char *title);
void print_section(const char *title);

/* Number of threads in this process, read from /proc/self/status. */
int  read_thread_count(void);

/* List /dev/shm objects created by this program (names containing CC_IPC_PREFIX).
 * If filter != NULL only names that also contain filter are counted.
 * If print is non-zero each match is printed. Returns the number found. */
int  list_ipc_objects(const char *filter, int print);

/* Human readable name for common signals ("SIGABRT"). */
const char *signal_name(int sig);

/* Disable core dumps for this process, including Ubuntu's crash handler
 * (apport / systemd-coredump), so a deliberately killed test process
 * does not trigger heavy crash collection. */
void disable_core_dumps(void);

/* Monotonic wall-clock time in milliseconds. */
double now_ms(void);

#endif /* CC_COMMON_H */
