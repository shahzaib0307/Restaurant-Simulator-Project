#include "queue.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

void queue_init(OrderQueue *q) {
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    sem_init(&q->items_available,  0, 0);
    sem_init(&q->spaces_available, 0, MAX_QUEUE_SIZE);
}

/*
 * Enqueue: called by waiter threads (producers).
 * VIP orders are inserted at the head of the queue so
 * they are served before normal orders — demonstrating
 * priority scheduling on top of the producer-consumer model.
 */
int queue_enqueue(OrderQueue *q, Order *order) {
    sem_wait(&q->spaces_available);   /* block when queue is full */
    pthread_mutex_lock(&q->mutex);

    if (order->priority == PRIORITY_VIP && q->count > 0) {
        /* Shift head backward to insert VIP at front */
        q->head = (q->head - 1 + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE;
        q->orders[q->head] = *order;
    } else {
        q->orders[q->tail] = *order;
        q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
    }
    q->count++;

    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->items_available);    /* wake a waiting chef */
    return 0;
}

/*
 * Dequeue: called by chef threads (consumers).
 * Chef must first acquire a kitchen station semaphore
 * (done in chef.c) before calling this.
 */
int queue_dequeue(OrderQueue *q, Order *out) {
    sem_wait(&q->items_available);    /* block when queue is empty */
    pthread_mutex_lock(&q->mutex);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        sem_post(&q->items_available);
        return -1;
    }

    *out     = q->orders[q->head];
    q->head  = (q->head + 1) % MAX_QUEUE_SIZE;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->spaces_available);   /* free a slot for waiters */
    return 0;
}

void queue_destroy(OrderQueue *q) {
    pthread_mutex_destroy(&q->mutex);
    sem_destroy(&q->items_available);
    sem_destroy(&q->spaces_available);
}

/*
 * Thread-safe event logger for the dashboard feed.
 * color_pair: ncurses color pair (1=green,2=yellow,3=red,4=cyan,5=white)
 */
void log_event(const char *fmt, int color_pair, ...) {
    va_list ap;
    va_start(ap, color_pair);

    pthread_mutex_lock(&g_log_mutex);

    int idx = (g_log_head + g_log_count) % MAX_LOG_ENTRIES;
    vsnprintf(g_log[idx].msg, sizeof(g_log[idx].msg), fmt, ap);
    g_log[idx].color_pair = color_pair;

    if (g_log_count < MAX_LOG_ENTRIES)
        g_log_count++;
    else
        g_log_head = (g_log_head + 1) % MAX_LOG_ENTRIES; /* overwrite oldest */

    pthread_mutex_unlock(&g_log_mutex);
    va_end(ap);
}
