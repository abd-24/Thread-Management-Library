// src/tml.c
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include "../include/tml.h"

/* ------------------ Deque Implementation ------------------ */

typedef struct deque {
    tml_task_t **buf;
    int cap;
    int head;   // owner push/pop
    int tail;   // steal pop
    pthread_mutex_t lock;
} deque_t;

static deque_t *deque_create(int cap) {
    deque_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->cap = (cap > 0) ? cap : 256;
    d->buf = calloc(d->cap, sizeof(tml_task_t *));
    if (!d->buf) { free(d); return NULL; }
    d->head = d->tail = 0;
    pthread_mutex_init(&d->lock, NULL);
    return d;
}

static void deque_destroy(deque_t *d) {
    if (!d) return;
    pthread_mutex_destroy(&d->lock);
    free(d->buf);
    free(d);
}

static int deque_size(deque_t *d) {
    int s = d->head - d->tail;
    return (s < 0) ? 0 : s;
}

/* Owner push with lock (safe) */
static void deque_push_front(deque_t *d, tml_task_t *t) {
    if (!d || !t) return;

    pthread_mutex_lock(&d->lock);

    if (deque_size(d) >= d->cap - 1) {
        int size = deque_size(d);
        int newcap = d->cap * 2;
        tml_task_t **newbuf = calloc(newcap, sizeof(tml_task_t *));
        if (newbuf) {
            for (int i = 0; i < size; i++)
                newbuf[i] = d->buf[(d->tail + i) % d->cap];
            free(d->buf);
            d->buf = newbuf;
            d->cap = newcap;
            d->tail = 0;
            d->head = size;
        } else {
            /* allocation failed: unlock and drop the push */
            pthread_mutex_unlock(&d->lock);
            return;
        }
    }

    d->buf[d->head % d->cap] = t;
    d->head++;

    pthread_mutex_unlock(&d->lock);
}

/* Owner pop with lock (safe) */
static tml_task_t *deque_pop_front(deque_t *d) {
    if (!d) return NULL;

    pthread_mutex_lock(&d->lock);
    if (d->head <= d->tail) {
        pthread_mutex_unlock(&d->lock);
        return NULL;
    }

    d->head--;
    tml_task_t *t = d->buf[d->head % d->cap];
    d->buf[d->head % d->cap] = NULL;

    pthread_mutex_unlock(&d->lock);
    return t;
}

/* Steal from the back (locked) */
static tml_task_t *deque_steal_back(deque_t *d) {
    if (!d) return NULL;

    pthread_mutex_lock(&d->lock);

    if (d->head <= d->tail) {
        pthread_mutex_unlock(&d->lock);
        return NULL;
    }

    tml_task_t *t = d->buf[d->tail % d->cap];
    d->buf[d->tail % d->cap] = NULL;
    d->tail++;

    pthread_mutex_unlock(&d->lock);
    return t;
}

/* ---------------- Thread Pool Implementation ---------------- */

typedef struct worker {
    int id;
    pthread_t thread;
    deque_t *dq;
} worker_t;

typedef struct pool {
    worker_t *workers;
    int nworkers;

    pthread_mutex_t gq_lock;          // global queue lock
    tml_task_t **global_q;
    int gq_head, gq_tail, gq_cap;

    atomic_int stop;
} pool_t;

static pool_t g_pool;

/* Worker main loop */
static void *worker_main(void *arg) {
    worker_t *w = (worker_t *)arg;
    srand((unsigned)time(NULL) ^ (unsigned)w->id);

    while (!atomic_load(&g_pool.stop)) {
        tml_task_t *task = NULL;

        /* 1. Try local queue first */
        task = deque_pop_front(w->dq);

        /* 2. Try global queue */
        if (!task) {
            pthread_mutex_lock(&g_pool.gq_lock);
            if (g_pool.gq_head < g_pool.gq_tail) {
                task = g_pool.global_q[g_pool.gq_head % g_pool.gq_cap];
                g_pool.global_q[g_pool.gq_head % g_pool.gq_cap] = NULL;
                g_pool.gq_head++;
            }
            pthread_mutex_unlock(&g_pool.gq_lock);
        }

        /* 3. Try stealing */
        if (!task && g_pool.nworkers > 1) {
            int victim = rand() % g_pool.nworkers;
            if (victim != w->id)
                task = deque_steal_back(g_pool.workers[victim].dq);
        }

        /* 4. Execute task or idle */
        if (task) {
            /* Execute the task -- the task function is responsible for freeing
               any payload pointed to by task->arg (if heap-allocated). We free
               the wrapper here. */
            task->fn(task->arg);
            free(task);
        } else {
            /* small delay to avoid busy-spin */
            sched_yield();
        }
    }

    return NULL;
}

/* ---------------------- Public API ---------------------- */

int tml_init(int workers) {
    if (workers <= 0) workers = 1;
    memset(&g_pool, 0, sizeof(g_pool));

    g_pool.nworkers = workers;
    g_pool.workers = calloc(workers, sizeof(worker_t));
    if (!g_pool.workers) return -1;

    /* global queue initialization */
    g_pool.gq_cap = 1024;
    g_pool.global_q = calloc(g_pool.gq_cap, sizeof(tml_task_t *));
    if (!g_pool.global_q) { free(g_pool.workers); return -1; }
    g_pool.gq_head = g_pool.gq_tail = 0;

    pthread_mutex_init(&g_pool.gq_lock, NULL);
    atomic_store(&g_pool.stop, 0);

    /* start workers */
    for (int i = 0; i < workers; i++) {
        worker_t *w = &g_pool.workers[i];
        w->id = i;
        /* larger initial deque to reduce resize frequency */
        w->dq = deque_create(1024);
        if (!w->dq) {
            /* cleanup on failure */
            for (int j = 0; j < i; j++)
                deque_destroy(g_pool.workers[j].dq);
            free(g_pool.workers);
            free(g_pool.global_q);
            return -1;
        }
        int rc = pthread_create(&w->thread, NULL, worker_main, w);
        if (rc != 0) {
            /* thread creation failed: clean up and return error */
            atomic_store(&g_pool.stop, 1);
            for (int j = 0; j <= i; j++) {
                if (g_pool.workers[j].thread) pthread_join(g_pool.workers[j].thread, NULL);
                deque_destroy(g_pool.workers[j].dq);
            }
            free(g_pool.workers);
            free(g_pool.global_q);
            return -1;
        }
    }

    return 0;
}

void tml_submit(tml_task_t *task) {
    if (!task) return;

    pthread_mutex_lock(&g_pool.gq_lock);

    if (g_pool.gq_tail - g_pool.gq_head >= g_pool.gq_cap) {
        /* grow global queue */
        int size = g_pool.gq_tail - g_pool.gq_head;
        int newcap = g_pool.gq_cap * 2;
        tml_task_t **newq = calloc(newcap, sizeof(tml_task_t *));
        if (newq) {
            for (int i = 0; i < size; i++)
                newq[i] = g_pool.global_q[(g_pool.gq_head + i) % g_pool.gq_cap];
            free(g_pool.global_q);
            g_pool.global_q = newq;
            g_pool.gq_cap = newcap;
            g_pool.gq_head = 0;
            g_pool.gq_tail = size;
        } else {
            /* allocation failed: unlock and drop submission */
            pthread_mutex_unlock(&g_pool.gq_lock);
            return;
        }
    }

    g_pool.global_q[g_pool.gq_tail % g_pool.gq_cap] = task;
    g_pool.gq_tail++;

    pthread_mutex_unlock(&g_pool.gq_lock);
}

void tml_submit_fn(tml_task_fn fn, void *arg) {
    if (!fn) return;
    tml_task_t *t = malloc(sizeof(tml_task_t));
    if (!t) return;
    t->fn = fn;
    t->arg = arg;
    tml_submit(t);
}

void tml_shutdown(void) {
    atomic_store(&g_pool.stop, 1);

    /* join worker threads */
    for (int i = 0; i < g_pool.nworkers; i++)
        pthread_join(g_pool.workers[i].thread, NULL);

    /* execute and free any tasks remaining in worker deques */
    for (int i = 0; i < g_pool.nworkers; i++) {
        tml_task_t *t;
        while ((t = deque_pop_front(g_pool.workers[i].dq)) != NULL) {
            /* run remaining task to ensure payloads are handled */
            t->fn(t->arg);
            free(t);
        }
    }

    /* execute and free any global queue tasks */
    pthread_mutex_lock(&g_pool.gq_lock);
    while (g_pool.gq_head < g_pool.gq_tail) {
        tml_task_t *t = g_pool.global_q[g_pool.gq_head % g_pool.gq_cap];
        g_pool.global_q[g_pool.gq_head % g_pool.gq_cap] = NULL;
        if (t) {
            t->fn(t->arg);
            free(t);
        }
        g_pool.gq_head++;
    }
    pthread_mutex_unlock(&g_pool.gq_lock);

    /* cleanup deques and arrays */
    for (int i = 0; i < g_pool.nworkers; i++)
        deque_destroy(g_pool.workers[i].dq);

    free(g_pool.workers);
    free(g_pool.global_q);
    pthread_mutex_destroy(&g_pool.gq_lock);
}
