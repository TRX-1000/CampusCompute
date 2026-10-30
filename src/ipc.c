/*
 * ipc.c - FR4: inter-process communication.
 *
 * The parent creates
 *   - a POSIX shared-memory object  /campuscompute_shm_<pid>   (shm_open + mmap)
 *   - three named semaphores        /campuscompute_sem_{empty,full,mutex}_<pid>
 * and forks two cooperating processes:
 *   PRODUCER (job submitter) writes job records into a ring buffer in shared memory
 *   CONSUMER (dispatcher)    reads them back out
 * Both children re-open the objects BY NAME, so they genuinely cooperate through
 * the named IPC objects.
 *
 *   empty : counts free slots   (initial IPC_SLOTS) - producer waits on it
 *   full  : counts filled slots (initial 0)         - consumer waits on it
 *   mutex : binary semaphore guarding in/out indices
 *
 * The parent verifies the checksum of the data exchanged, then closes and
 * unlinks ONLY the objects it created (names include its own pid).
 */
#include "ipc.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    int job_id;
    int burst;
    int priority;
} ipc_record_t;

typedef struct {
    ipc_record_t ring[IPC_SLOTS];
    int  in, out;
    int  total_items;
    int  produced, consumed;
    long producer_sum, consumer_sum;     /* checksum: sum of bursts */
} shm_region_t;

typedef struct {
    char shm[64], empty[64], full[64], mutex[64];
} ipc_names_t;

static void make_names(ipc_names_t *n, pid_t pid)
{
    snprintf(n->shm,   sizeof n->shm,   "/" CC_IPC_PREFIX "shm_%d", (int)pid);
    snprintf(n->empty, sizeof n->empty, "/" CC_IPC_PREFIX "sem_empty_%d", (int)pid);
    snprintf(n->full,  sizeof n->full,  "/" CC_IPC_PREFIX "sem_full_%d", (int)pid);
    snprintf(n->mutex, sizeof n->mutex, "/" CC_IPC_PREFIX "sem_mutex_%d", (int)pid);
}

/* sem_wait that retries if interrupted by a signal */
static int sem_wait_retry(sem_t *s)
{
    while (sem_wait(s) == -1) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

/* Wait on `s`; if it would block, say so first (makes the coordination visible). */
static int sem_wait_verbose(sem_t *s, const char *who, const char *semname, const char *why)
{
    if (sem_trywait(s) == 0)
        return 0;
    if (errno != EAGAIN)
        return -1;
    printf("  [%s pid %d] %s -> blocking on sem_wait(%s)\n", who, (int)getpid(), why, semname);
    return sem_wait_retry(s);
}

typedef struct {
    shm_region_t *r;
    sem_t *empty, *full, *mutex;
} attach_t;

/* Child side: open the existing objects by name. */
static int child_attach(const ipc_names_t *n, attach_t *a)
{
    int fd = shm_open(n->shm, O_RDWR, 0);
    if (fd == -1) {
        perror("child shm_open");
        return -1;
    }
    a->r = mmap(NULL, sizeof(shm_region_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (a->r == MAP_FAILED) {
        perror("child mmap");
        return -1;
    }
    a->empty = sem_open(n->empty, 0);
    a->full  = sem_open(n->full, 0);
    a->mutex = sem_open(n->mutex, 0);
    if (a->empty == SEM_FAILED || a->full == SEM_FAILED || a->mutex == SEM_FAILED) {
        perror("child sem_open");
        return -1;
    }
    return 0;
}

static void child_detach(attach_t *a)
{
    sem_close(a->empty);
    sem_close(a->full);
    sem_close(a->mutex);
    munmap(a->r, sizeof(shm_region_t));
}

static void run_producer(const ipc_names_t *n, int items)
{
    attach_t a;
    if (child_attach(n, &a) != 0)
        _exit(10);
    printf("  [PRODUCER pid %d] attached to %s and 3 named semaphores\n", (int)getpid(), n->shm);

    long sum = 0;
    for (int i = 0; i < items; i++) {
        ipc_record_t rec = {.job_id = 101 + i, .burst = (i * 7) % 9 + 1, .priority = i % 4};
        if (sem_wait_verbose(a.empty, "PRODUCER", "empty", "ring buffer FULL") != 0 ||
            sem_wait_retry(a.mutex) != 0)
            _exit(11);
        int slot = a.r->in;
        a.r->ring[slot] = rec;
        a.r->in = (slot + 1) % IPC_SLOTS;
        a.r->produced++;
        sem_post(a.mutex);
        sem_post(a.full);

        int ev = 0, fv = 0;
        sem_getvalue(a.empty, &ev);
        sem_getvalue(a.full, &fv);
        printf("  [PRODUCER pid %d] wrote J%d (burst %d, pri %d) -> slot %d   [empty=%d full=%d]\n",
               (int)getpid(), rec.job_id, rec.burst, rec.priority, slot, ev, fv);
        sum += rec.burst;
        usleep(5000);                    /* submitter is fast -> ring fills up */
    }
    sem_wait_retry(a.mutex);
    a.r->producer_sum = sum;
    sem_post(a.mutex);
    printf("  [PRODUCER pid %d] done: %d records, burst checksum %ld -> exit(0)\n", (int)getpid(),
           items, sum);
    log_event("IPC", "producer wrote %d records via %s (checksum %ld)", items, n->shm, sum);
    child_detach(&a);
    fflush(stdout);
    _exit(0);
}

static void run_consumer(const ipc_names_t *n, int items)
{
    attach_t a;
    if (child_attach(n, &a) != 0)
        _exit(20);
    printf("  [CONSUMER pid %d] attached to %s and 3 named semaphores\n", (int)getpid(), n->shm);

    long sum = 0;
    usleep(60000);                       /* start late so the producer fills the ring first */
    for (int i = 0; i < items; i++) {
        if (sem_wait_verbose(a.full, "CONSUMER", "full", "ring buffer EMPTY") != 0 ||
            sem_wait_retry(a.mutex) != 0)
            _exit(21);
        int slot = a.r->out;
        ipc_record_t rec = a.r->ring[slot];
        a.r->out = (slot + 1) % IPC_SLOTS;
        a.r->consumed++;
        sem_post(a.mutex);
        sem_post(a.empty);

        sum += rec.burst;
        printf("  [CONSUMER pid %d] read  J%d (burst %d, pri %d) <- slot %d   running sum=%ld\n",
               (int)getpid(), rec.job_id, rec.burst, rec.priority, slot, sum);
        usleep(30000);                   /* dispatcher is slower than the submitter */
    }
    sem_wait_retry(a.mutex);
    a.r->consumer_sum = sum;
    sem_post(a.mutex);
    printf("  [CONSUMER pid %d] done: %d records, burst checksum %ld -> exit(0)\n", (int)getpid(),
           items, sum);
    log_event("IPC", "consumer read %d records via %s (checksum %ld)", items, n->shm, sum);
    child_detach(&a);
    fflush(stdout);
    _exit(0);
}

int cmd_ipc(int items)
{
    print_banner("FR4 - Shared-memory IPC with named semaphores");
    pid_t me = getpid();
    ipc_names_t n;
    make_names(&n, me);
    char filter[32];                      /* matches only this run's objects */
    snprintf(filter, sizeof filter, "_%d", (int)me);

    int rc = EXIT_RUNTIME;
    int shm_created = 0, e_created = 0, f_created = 0, m_created = 0;
    shm_region_t *r = MAP_FAILED;
    sem_t *se = SEM_FAILED, *sf = SEM_FAILED, *sm = SEM_FAILED;
    pid_t prod = -1, cons = -1;

    printf("Parent pid %d, %d job records, ring buffer of %d slots\n\n", (int)me, items, IPC_SLOTS);

    /* ---- create shared memory ---- */
    int fd = shm_open(n.shm, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        fprintf(stderr, "Error: shm_open(%s): %s\n", n.shm, strerror(errno));
        goto cleanup;
    }
    shm_created = 1;
    if (ftruncate(fd, sizeof(shm_region_t)) == -1) {
        fprintf(stderr, "Error: ftruncate: %s\n", strerror(errno));
        close(fd);
        goto cleanup;
    }
    r = mmap(NULL, sizeof(shm_region_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (r == MAP_FAILED) {
        fprintf(stderr, "Error: mmap: %s\n", strerror(errno));
        goto cleanup;
    }
    memset(r, 0, sizeof *r);
    r->total_items = items;
    printf("  created shared memory  %-36s (%zu bytes)\n", n.shm, sizeof(shm_region_t));

    /* ---- create named semaphores ---- */
    se = sem_open(n.empty, O_CREAT | O_EXCL, 0600, IPC_SLOTS);
    if (se == SEM_FAILED) { fprintf(stderr, "Error: sem_open(%s): %s\n", n.empty, strerror(errno)); goto cleanup; }
    e_created = 1;
    sf = sem_open(n.full, O_CREAT | O_EXCL, 0600, 0);
    if (sf == SEM_FAILED) { fprintf(stderr, "Error: sem_open(%s): %s\n", n.full, strerror(errno)); goto cleanup; }
    f_created = 1;
    sm = sem_open(n.mutex, O_CREAT | O_EXCL, 0600, 1);
    if (sm == SEM_FAILED) { fprintf(stderr, "Error: sem_open(%s): %s\n", n.mutex, strerror(errno)); goto cleanup; }
    m_created = 1;
    printf("  created semaphore      %-36s (initial %d = free slots)\n", n.empty, IPC_SLOTS);
    printf("  created semaphore      %-36s (initial 0 = filled slots)\n", n.full);
    printf("  created semaphore      %-36s (initial 1 = binary lock)\n", n.mutex);

    printf("\n  IPC objects visible in /dev/shm now:\n");
    list_ipc_objects(filter, 1);
    log_event("IPC", "created %s + 3 named semaphores", n.shm);

    /* ---- fork the cooperating processes ---- */
    printf("\n");
    fflush(stdout);
    prod = fork();
    if (prod == -1) { perror("fork producer"); goto cleanup; }
    if (prod == 0)
        run_producer(&n, items);

    cons = fork();
    if (cons == -1) { perror("fork consumer"); goto cleanup; }
    if (cons == 0)
        run_consumer(&n, items);

    int ok = 1;
    for (int k = 0; k < 2; k++) {
        int status;
        pid_t w = waitpid(-1, &status, 0);
        if (w == -1) {
            if (errno == EINTR) { k--; continue; }
            perror("waitpid");
            ok = 0;
            break;
        }
        const char *who = (w == prod) ? "producer" : "consumer";
        if (WIFEXITED(status))
            printf("  [parent] %s pid %d exited with status %d\n", who, (int)w, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("  [parent] %s pid %d killed by signal %d\n", who, (int)w, WTERMSIG(status));
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;
    }
    prod = cons = -1;

    print_section("IPC result");
    printf("  Records produced : %d  (checksum %ld)\n", r->produced, r->producer_sum);
    printf("  Records consumed : %d  (checksum %ld)\n", r->consumed, r->consumer_sum);
    if (ok && r->produced == items && r->consumed == items && r->producer_sum == r->consumer_sum) {
        printf("  Data integrity   : MATCH - all records crossed process boundary intact\n");
        rc = EXIT_OK;
    } else {
        printf("  Data integrity   : MISMATCH or child failure\n");
    }

cleanup:
    if (prod > 0) { kill(prod, SIGTERM); waitpid(prod, NULL, 0); }
    if (cons > 0) { kill(cons, SIGTERM); waitpid(cons, NULL, 0); }

    print_section("Cleanup (only objects created by this run)");
    if (r != MAP_FAILED)
        printf("  munmap(shared region)                         : %s\n",
               munmap(r, sizeof(shm_region_t)) == 0 ? "ok" : strerror(errno));
    if (se != SEM_FAILED) sem_close(se);
    if (sf != SEM_FAILED) sem_close(sf);
    if (sm != SEM_FAILED) sem_close(sm);
    if (e_created) printf("  sem_unlink(%-34s) : %s\n", n.empty, sem_unlink(n.empty) == 0 ? "ok" : strerror(errno));
    if (f_created) printf("  sem_unlink(%-34s) : %s\n", n.full,  sem_unlink(n.full)  == 0 ? "ok" : strerror(errno));
    if (m_created) printf("  sem_unlink(%-34s) : %s\n", n.mutex, sem_unlink(n.mutex) == 0 ? "ok" : strerror(errno));
    if (shm_created) printf("  shm_unlink(%-34s) : %s\n", n.shm, shm_unlink(n.shm) == 0 ? "ok" : strerror(errno));

    int left = list_ipc_objects(filter, 1);
    printf("  Remaining objects from this run in /dev/shm   : %d\n", left);
    log_event("IPC", "cleanup done, %d objects remaining", left);
    if (left != 0)
        rc = EXIT_RUNTIME;
    return rc;
}
