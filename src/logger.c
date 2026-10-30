/*
 * logger.c - FR3: concurrent activity logger.
 *
 * Producer threads generate lab events (job submitted, dispatched, ...) and
 * put them into a bounded circular queue. Consumer threads take events out
 * and append them to logs/campuscompute.log.
 *
 * Synchronisation:
 *   - one pthread mutex protects the queue (slots, head/tail/count, stats)
 *   - condition variable not_full : producers wait while the queue is full
 *   - condition variable not_empty: consumers wait while it is empty
 *   Waits are always inside while-loops, so spurious wake-ups are harmless.
 *
 * Clean termination: main joins all producers, then sets producers_done under
 * the mutex and broadcasts not_empty. A consumer exits only when the queue is
 * empty AND producers_done is set, so no event is ever lost.
 */
#include "logger.h"
#include "common.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_MSG_LEN 96

typedef struct {
    int  producer;
    int  seq;
    char text[LOG_MSG_LEN];
} log_entry_t;

typedef struct {
    log_entry_t *slots;
    int capacity, head, tail, count;
    int producers_done;
    pthread_mutex_t lock;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
    /* statistics (protected by lock) */
    long produced, consumed, full_waits, empty_waits;
    int  peak;
} bqueue_t;

typedef struct {
    bqueue_t *q;
    int id;
    int items;
} producer_arg_t;

typedef struct {
    bqueue_t *q;
    int id;
    long written;
} consumer_arg_t;

static void make_event(int producer, int seq, char *buf, size_t n)
{
    int job = producer * 100 + seq;
    switch (seq % 5) {
    case 0: snprintf(buf, n, "job J%d completed on lab node %d", job, producer); break;
    case 1: snprintf(buf, n, "job J%d submitted by user%02d", job, producer); break;
    case 2: snprintf(buf, n, "job J%d moved to READY queue", job); break;
    case 3: snprintf(buf, n, "job J%d dispatched to CPU %d", job, (seq + producer) % 4); break;
    default: snprintf(buf, n, "job J%d resource request granted", job); break;
    }
}

static void *producer_main(void *argp)
{
    producer_arg_t *a = argp;
    bqueue_t *q = a->q;

    for (int seq = 1; seq <= a->items; seq++) {
        log_entry_t e = {.producer = a->id, .seq = seq};
        make_event(a->id, seq, e.text, sizeof e.text);
        usleep(1000 + 500 * a->id);            /* simulate work between events */

        pthread_mutex_lock(&q->lock);
        int announced = 0;
        while (q->count == q->capacity) {
            if (!announced) {
                printf("  [P%d] queue FULL (%d/%d) -> waiting on not_full\n", a->id, q->count,
                       q->capacity);
                q->full_waits++;
                announced = 1;
            }
            pthread_cond_wait(&q->not_full, &q->lock);
        }
        q->slots[q->tail] = e;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        q->produced++;
        if (q->count > q->peak)
            q->peak = q->count;
        printf("  [P%d] produced P%d#%d \"%s\"  (queue %d/%d)\n", a->id, a->id, seq, e.text,
               q->count, q->capacity);
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
    }
    printf("  [P%d] finished producing %d events -> thread exiting\n", a->id, a->items);
    return NULL;
}

static void *consumer_main(void *argp)
{
    consumer_arg_t *a = argp;
    bqueue_t *q = a->q;

    for (;;) {
        pthread_mutex_lock(&q->lock);
        int announced = 0;
        while (q->count == 0 && !q->producers_done) {
            if (!announced) {
                printf("  [C%d] queue EMPTY -> waiting on not_empty\n", a->id);
                q->empty_waits++;
                announced = 1;
            }
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
        if (q->count == 0 && q->producers_done) {   /* nothing left and nobody will add */
            pthread_mutex_unlock(&q->lock);
            break;
        }
        log_entry_t e = q->slots[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        q->consumed++;
        printf("  [C%d] consumed P%d#%d -> written to log  (queue %d/%d)\n", a->id, e.producer,
               e.seq, q->count, q->capacity);
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->lock);

        /* file I/O happens outside the queue lock so producers are not blocked by disk */
        log_event("LOGGER", "C%d <- P%d#%d: %s", a->id, e.producer, e.seq, e.text);
        a->written++;
        usleep(4000);                                 /* consumers are slower -> queue fills */
    }
    printf("  [C%d] queue empty and producers done -> thread exiting (wrote %ld entries)\n", a->id,
           a->written);
    return NULL;
}

int cmd_logger(int producers, int consumers, int items, int capacity)
{
    print_banner("FR3 - Concurrent activity logger (bounded producer-consumer)");
    printf("Producers=%d  Consumers=%d  Events/producer=%d  Queue capacity=%d\n", producers,
           consumers, items, capacity);
    printf("Sync: pthread mutex + condition variables (not_full / not_empty)\n");
    printf("Log file: %s\n\n", LOG_FILE);

    bqueue_t q;
    memset(&q, 0, sizeof q);
    q.capacity = capacity;
    q.slots = calloc((size_t)capacity, sizeof *q.slots);
    if (q.slots == NULL) {
        perror("calloc");
        return EXIT_RUNTIME;
    }
    if (pthread_mutex_init(&q.lock, NULL) != 0 || pthread_cond_init(&q.not_full, NULL) != 0 ||
        pthread_cond_init(&q.not_empty, NULL) != 0) {
        fprintf(stderr, "Error: failed to initialise mutex/condition variables\n");
        free(q.slots);
        return EXIT_RUNTIME;
    }

    long lines_before = log_line_count();
    if (lines_before < 0)
        lines_before = 0;
    log_event("LOGGER", "session start: %d producers, %d consumers, %d events each, capacity %d",
              producers, consumers, items, capacity);

    pthread_t pt[LOGGER_MAX_PRODUCERS], ct[LOGGER_MAX_CONSUMERS];
    producer_arg_t pa[LOGGER_MAX_PRODUCERS];
    consumer_arg_t ca[LOGGER_MAX_CONSUMERS];
    int np = 0, nc = 0, rc = EXIT_OK;

    for (int i = 0; i < consumers; i++) {
        ca[i] = (consumer_arg_t){.q = &q, .id = i + 1, .written = 0};
        if (pthread_create(&ct[i], NULL, consumer_main, &ca[i]) != 0) {
            fprintf(stderr, "Error: pthread_create (consumer %d) failed\n", i + 1);
            rc = EXIT_RUNTIME;
            break;
        }
        nc++;
    }
    for (int i = 0; rc == EXIT_OK && i < producers; i++) {
        pa[i] = (producer_arg_t){.q = &q, .id = i + 1, .items = items};
        if (pthread_create(&pt[i], NULL, producer_main, &pa[i]) != 0) {
            fprintf(stderr, "Error: pthread_create (producer %d) failed\n", i + 1);
            rc = EXIT_RUNTIME;
            break;
        }
        np++;
    }

    for (int i = 0; i < np; i++)
        pthread_join(pt[i], NULL);
    printf("\n  [main] all %d producer thread(s) joined\n", np);

    pthread_mutex_lock(&q.lock);
    q.producers_done = 1;
    pthread_cond_broadcast(&q.not_empty);        /* wake every waiting consumer */
    pthread_mutex_unlock(&q.lock);
    printf("  [main] producers_done set, not_empty broadcast to consumers\n");

    for (int i = 0; i < nc; i++)
        pthread_join(ct[i], NULL);
    printf("  [main] all %d consumer thread(s) joined\n", nc);

    long expected = (long)np * items;
    log_event("LOGGER", "session end: produced=%ld consumed=%ld", q.produced, q.consumed);

    print_section("Logger summary");
    printf("  Events produced          : %ld (expected %ld)\n", q.produced, expected);
    printf("  Events consumed/logged   : %ld\n", q.consumed);
    printf("  Lost or duplicated       : %ld\n", q.produced - q.consumed);
    printf("  Peak queue occupancy     : %d/%d\n", q.peak, q.capacity);
    printf("  Producer waits (full)    : %ld\n", q.full_waits);
    printf("  Consumer waits (empty)   : %ld\n", q.empty_waits);
    long lines_after = log_line_count();
    printf("  Lines appended to log    : %ld (%s)\n", lines_after - lines_before, LOG_FILE);

    pthread_cond_destroy(&q.not_full);
    pthread_cond_destroy(&q.not_empty);
    pthread_mutex_destroy(&q.lock);
    free(q.slots);

    int threads = read_thread_count();
    printf("  Threads alive now        : %d (main thread only => all workers terminated)\n", threads);
    printf("  Mutex/condvars destroyed, queue memory freed\n");

    if (rc == EXIT_OK && (q.produced != expected || q.consumed != q.produced)) {
        printf("  RESULT: FAIL - event count mismatch\n");
        rc = EXIT_RUNTIME;
    } else if (rc == EXIT_OK) {
        printf("  RESULT: PASS - every event logged exactly once, clean shutdown\n");
    }
    return rc;
}
