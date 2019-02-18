#include <stdio.h>
#include <unistd.h>

#include "yatp.h"

void dumb_task(void *arg) {
        int t = (size_t) arg;
        printf("Task %d: started, timeout = %d, priority = %d\n",
               t & 0xFF, (t >> 8) & 0xFF, (t >> 16) & 0xFF);
        sleep((t >> 8) & 0xFF);
        printf("Task %d: finished\n", t & 0xFF);
        return;
}

int main (int argc, char **argv)
{
        struct yatp_t *tp;

        yatp_init(&tp, 4);

        yatp_enqueue(tp, dumb_task,
                     (void *)(1 | (5 << 8) | (YATP_PRIO_LOW << 16)),
                     YATP_PRIO_LOW);

        yatp_enqueue(tp, dumb_task,
                     (void *)(2 | (5 << 8) | (YATP_PRIO_NORMAL << 16)),
                     YATP_PRIO_NORMAL);

        yatp_enqueue(tp, dumb_task,
                     (void *)(3 | (5 << 8) | (YATP_PRIO_HIGH << 16)),
                     YATP_PRIO_HIGH);

        yatp_enqueue(tp, dumb_task,
                     (void *)(4 | (5 << 8) | (YATP_PRIO_NORMAL << 16)),
                     YATP_PRIO_NORMAL);

        sleep(5);

        yatp_enqueue(tp, dumb_task,
                     (void *)(11 | (3 << 8) | (YATP_PRIO_HIGH << 16)),
                     YATP_PRIO_HIGH);

        yatp_enqueue(tp, dumb_task,
                     (void *)(12 | (3 << 8) | (YATP_PRIO_HIGH << 16)),
                     YATP_PRIO_HIGH);

        yatp_enqueue(tp, dumb_task,
                     (void *)(13 | (3 << 8) | (YATP_PRIO_HIGH << 16)),
                     YATP_PRIO_HIGH);

        yatp_enqueue(tp, dumb_task,
                     (void *)(14 | (5 << 8) | (YATP_PRIO_HIGH << 16)),
                     YATP_PRIO_HIGH);

        yatp_enqueue(tp, dumb_task,
                     (void *)(19 | (3 << 8) | (YATP_PRIO_NORMAL << 16)),
                     YATP_PRIO_NORMAL);

        sleep(10);

        yatp_enqueue(tp, dumb_task,
                     (void *)(21 | (20 << 8) | (YATP_PRIO_LOW << 16)),
                     YATP_PRIO_LOW);


        sleep (5);

        yatp_stop(tp);

        return 0;
}
