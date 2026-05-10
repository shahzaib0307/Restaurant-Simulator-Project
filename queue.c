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

/* ─── User-triggered actions ─────────────────────────────────── */

static const char *VIP_ITEMS[] = {
    "Lobster Thermidor", "Wagyu Steak", "Truffle Pasta", "Champagne Risotto"
};
#define VIP_ITEM_COUNT 4

/*
 * action_inject_vip — manually force a VIP order into the queue.
 * Called from the main thread on keypress 'v'.
 * Uses queue_enqueue() so the same mutex/semaphore path is exercised.
 */
void action_inject_vip(void) {
    Order o;
    memset(&o, 0, sizeof(o));

    pthread_mutex_lock(&g_stats_mutex);
    g_total_placed++;
    o.id = g_total_placed;
    pthread_mutex_unlock(&g_stats_mutex);

    o.waiter_id   = 0;   /* 0 = injected by user */
    o.chef_id     = -1;
    o.status      = STATUS_PENDING;
    o.time_placed = time(NULL);
    o.priority    = PRIORITY_VIP;
    snprintf(o.item, MAX_ITEM_LEN, "%s", VIP_ITEMS[o.id % VIP_ITEM_COUNT]);

    queue_enqueue(&g_queue, &o);
    log_event("[USER] Injected VIP #%d: %s", 2, o.id, o.item);
}

/*
 * action_cancel_oldest — remove the oldest PENDING order from the queue.
 * Called from the main thread on keypress 'c'.
 */
void action_cancel_oldest(void) {
    pthread_mutex_lock(&g_queue.mutex);
    int cancelled_id = -1;
    for (int i = 0; i < g_queue.count; i++) {
        int idx = (g_queue.head + i) % MAX_QUEUE_SIZE;
        if (g_queue.orders[idx].status == STATUS_PENDING) {
            cancelled_id = g_queue.orders[idx].id;
            /* Shift subsequent entries forward */
            for (int j = i; j < g_queue.count - 1; j++) {
                int a = (g_queue.head + j)     % MAX_QUEUE_SIZE;
                int b = (g_queue.head + j + 1) % MAX_QUEUE_SIZE;
                g_queue.orders[a] = g_queue.orders[b];
            }
            g_queue.count--;
            g_queue.tail = (g_queue.tail - 1 + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE;
            break;
        }
    }
    pthread_mutex_unlock(&g_queue.mutex);

    if (cancelled_id != -1) {
        sem_post(&g_queue.spaces_available);
        pthread_mutex_lock(&g_stats_mutex);
        g_total_cancelled++;
        pthread_mutex_unlock(&g_stats_mutex);
        log_event("[USER] Cancelled oldest order #%d", 3, cancelled_id);
    } else {
        log_event("[USER] No pending orders to cancel", 5);
    }
}

/*
 * action_reset_stats — zero out all performance counters.
 * Called from the main thread on keypress 'r'.
 */
void action_reset_stats(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_total_placed    = 0;
    g_total_completed = 0;
    g_total_cancelled = 0;
    g_total_wait_time = 0.0;
    pthread_mutex_unlock(&g_stats_mutex);

    pthread_mutex_lock(&g_history_mutex);
    g_history_count = 0;
    pthread_mutex_unlock(&g_history_mutex);

    log_event("[USER] Stats reset", 4);
}
