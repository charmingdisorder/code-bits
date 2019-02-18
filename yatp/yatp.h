#ifndef _YATP_H_
#define _YATP_H_

#include <pthread.h>

enum yatp_prio_t {
        YATP_PRIO_HIGH,
        YATP_PRIO_NORMAL,
        YATP_PRIO_LOW,
        YATP_PRIO_LAST
};

struct yatp_task_t {
        void (*f)(void *);
        void *arg;
        struct yatp_task_t *next;
};

struct yatp_queue_t {
        struct yatp_task_t *first;
        struct yatp_task_t *last;
        enum yatp_prio_t prio;
        unsigned int size;
        unsigned int in_row;
};

struct yatp_t {
        unsigned int n_workers;
        pthread_t *workers;
        pthread_mutex_t q_mutex;
        pthread_cond_t q_event;
        unsigned int is_stopping;
        struct yatp_queue_t *queue[YATP_PRIO_LAST];
};

int yatp_init (struct yatp_t **tpr, unsigned int n_workers);
int yatp_enqueue (struct yatp_t *tp, void (*f) (void *), void *arg,
                  enum yatp_prio_t prio);
int yatp_stop (struct yatp_t *tp);

#endif