/*
 * yatp.c: Yet Another Thread Pool
 *
 * yatp is very simple thread pool
 *
 * Copyright (c) 2019 Alexey Mikhailov. All rights reserved.
 *
 * This work is licensed under the terms of the MIT license.
 * For a copy, see <https://opensource.org/licenses/MIT>.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>

#include "yatp.h"

#define PROG "yatp"

#define dprintf(...) \
        do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)

#define YATP_PRIO_HIGH_THRESHOLD 3

static struct yatp_task_t *yatp_get_task (struct yatp_queue_t *q)
{
        struct yatp_task_t *t = q->first;

        if (q->first->next == NULL) {
                q->first = NULL;
                q->last = NULL;
                q->size = 0;
        } else {
                q->first=q->first->next;
                q->size--;
        }

        return t;
}

static struct yatp_task_t *yatp_dequeue (struct yatp_t *tp)
{
        struct yatp_queue_t *hq = tp->queue[YATP_PRIO_HIGH];
        struct yatp_queue_t *nq = tp->queue[YATP_PRIO_NORMAL];
        struct yatp_queue_t *lq = tp->queue[YATP_PRIO_LOW];

        if (hq->size) {
                if ((hq->in_row >= YATP_PRIO_HIGH_THRESHOLD) && nq->size) {
                        /* going to run normal prio'd task because of policy */
                        hq->in_row = 0;
                } else {
                        hq->in_row++;
                        return yatp_get_task(hq);
                }

        }

        if (nq->size) {
                return yatp_get_task(nq);
        }

        if (lq->size) {
                return yatp_get_task(lq);
        }

        /* no tasks */
        return NULL;
}

static void *yatp_worker (void *t)
{
        struct yatp_t *tp = (struct yatp_t *)t;
        struct yatp_task_t *task = NULL;

        for (;;) {
                pthread_mutex_lock(&tp->q_mutex);

                if (tp->is_stopping) {
                        pthread_mutex_unlock(&tp->q_mutex);
                        break;
                }

                task = yatp_dequeue(tp);

                if (task == NULL) {
                        pthread_cond_wait(&(tp->q_event), &tp->q_mutex);
                        task = yatp_dequeue(tp);
                }

                pthread_mutex_unlock(&tp->q_mutex);

                if (task)
                        (task->f)(task->arg);

                /* XXX: task->arg? */
                free(task);
        }

        return NULL;
}

int yatp_enqueue (struct yatp_t *tp, void (*f) (void *), void *arg,
                  enum yatp_prio_t prio)
{
        int ret = 0;
        (void) ret;

        if (tp->is_stopping)
                return -1;

        if (pthread_mutex_lock(&tp->q_mutex) != 0) {
                fprintf(stderr, "yatp_enqueue: pthread_mutex_lock()\n");
                return -1;
        }

        do {
                struct yatp_queue_t *q = tp->queue[prio];
                struct yatp_task_t *t;

                t = malloc(sizeof(struct yatp_task_t));

                if (t == NULL) {
                        fprintf(stderr, "yatp_enqueue: malloc()\n");
                        ret = -1;
                        break;
                }

                t->f = f;
                t->arg = arg;
                t->next = NULL;

                if (q->size == 0) {
                        q->first = t;
                        q->last = t;
                } else {
                        q->last->next = t;
                        q->last = t;
                }

                q->size++;

                if (pthread_cond_signal(&(tp->q_event)) != 0) {
                        fprintf(stderr, "yatp_enqueue: pthread_cond_signal()");
                }

        } while (0);

        if (pthread_mutex_unlock(&tp->q_mutex) != 0) {
                fprintf(stderr, "yatp_enqueue: pthread_mutex_unlock()\n");
        }

        return ret;
}

int yatp_init (struct yatp_t **tpr, unsigned int n_workers)
{
        /* XXX: cleanup on errors */
        int ret, i;
        struct yatp_t *tp = malloc(sizeof(struct yatp_t));

        if (tp == NULL)
                return -1;

        tp->is_stopping = 0;
        tp->n_workers = n_workers;
        tp->workers = malloc(sizeof(pthread_t)*n_workers);

        if (tp->workers == NULL) {
                fprintf(stderr, "%s: malloc() failed\n", PROG);
                goto err1;
        }

        if ((ret = pthread_mutex_init(&(tp->q_mutex), NULL)) != 0) {
                fprintf(stderr, "%s: pthread_mutex_init() failed with %d\n",
                        PROG, ret);
                goto err2;
        }

        if ((ret = pthread_cond_init(&(tp->q_event), NULL)) != 0) {
                fprintf(stderr, "%s: pthread_cond_init() failed with %d\n",
                        PROG, ret);
                goto err3;
        }

        for (i = 0; i < YATP_PRIO_LAST; i++) {
                struct yatp_queue_t *q;

                q = malloc(sizeof(struct yatp_queue_t));
                tp->queue[i] = q;

                if (q == NULL) {
                        fprintf(stderr, "%s: malloc() failed\n", PROG);
                        goto err4;
                }

                q->prio = i;
                q->first = NULL;
                q->last = NULL;
                q->size = 0;
        }

        for (i = 0; i < tp->n_workers; i++) {
                if ((ret = pthread_create(&(tp->workers[i]), NULL, yatp_worker,
                                          (void *)tp)) != 0) {
                        tp->workers[i] = (pthread_t)NULL;
                        fprintf(stderr, "%s: pthread_create() failed with %d\n",
                                PROG, ret);
                        goto err4;
                }
        }

        *tpr = tp;

        return 0;

err4:
        for (i = 0; i < YATP_PRIO_LAST; i++) {
                if (tp->queue[i] != NULL) {
                        free(tp->queue[i]);
                } else {
                        break;
                }
        }
        pthread_cond_destroy(&(tp->q_event));
err3:
        pthread_mutex_destroy(&(tp->q_mutex));
err2:
        free(tp->workers);
err1:
        free(tp);

        return -1;
}

int yatp_stop (struct yatp_t *tp)
{
        int i, err = 0;
        struct yatp_task_t *task = NULL;

        dprintf("%s: shutting down...\n", __func__);

        if (pthread_mutex_lock(&(tp->q_mutex)) != 0) {
                fprintf(stderr, "yatp_enqueue: pthread_mutex_lock()\n");
                return -1;
        }

        tp->is_stopping = 1;

        if (pthread_cond_broadcast(&(tp->q_event)) != 0) {
                fprintf(stderr, "yatp_stop: thread_cond_broadcast\n");
                err = 1;
        }

        if (pthread_mutex_unlock(&(tp->q_mutex)) != 0) {
                fprintf(stderr, "yatp_stop: thread_mutex_unlock\n");
                err = 1;
        }

        for (i = 0; i < tp->n_workers; i++) {
                if (pthread_join(tp->workers[i], NULL) != 0) {
                        fprintf(stderr, "yatp_stop: pthread_join\n");
                        err = 1;
                }
        }

        if (!err) {
                if (tp->workers)
                        free(tp->workers);

                while ((task = yatp_dequeue(tp)) != NULL) {
                        free(task);

                }

                for (i = 0; i < YATP_PRIO_LAST; i++) {
                        free(tp->queue[i]);
                }

                pthread_mutex_destroy(&tp->q_mutex);
                pthread_cond_destroy(&tp->q_event);

                free(tp);
        }

        return err;
}
