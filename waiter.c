#include "waiter.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *MENU[] = {
    "Margherita Pizza", "Pasta Carbonara", "Grilled Steak",
    "Caesar Salad",     "Chicken Tikka",   "Lamb Chops",
    "Fish & Chips",     "Mushroom Risotto", "Beef Burger",
    "Sushi Platter",    "Lobster Bisque",   "Tiramisu"
};
#define MENU_SIZE 12

/*
 * waiter_thread — Producer
 *
 * Each waiter continuously:
 *   1. Builds an order (10% chance VIP priority)
 *   2. With 5% probability, cancels a pending order already in queue
 *      (demonstrates order cancellation + reprioritization from proposal)
 *   3. Enqueues the new order (mutex-protected via queue_enqueue)
 *   4. Sleeps 500–2500ms to simulate customer arrival rate
 */
void *waiter_thread(void *arg) {
    WaiterArgs  *w   = (WaiterArgs *)arg;
    int          tid = w->id;
    int          order_counter = 0;
    unsigned int seed = (unsigned int)time(NULL) ^ (tid * 2654435761u);

    while (g_running) {
        /* Build a new order */
        Order o;
        memset(&o, 0, sizeof(o));

        pthread_mutex_lock(&g_stats_mutex);
        g_total_placed++;
        o.id = g_total_placed;
        pthread_mutex_unlock(&g_stats_mutex);

        o.waiter_id   = tid;
        o.chef_id     = -1;
        o.status      = STATUS_PENDING;
        o.time_placed = time(NULL);
        o.priority    = (rand_r(&seed) % 10 == 0) ? PRIORITY_VIP : PRIORITY_NORMAL;
        strncpy(o.item, MENU[rand_r(&seed) % MENU_SIZE], MAX_ITEM_LEN - 1);

        /* ── Order Cancellation (5% chance) ─────────────────────
         * Demonstrates proposal's "order cancellation and
         * reprioritization" additional feature.
         * Mutex protects queue during removal.
         * ─────────────────────────────────────────────────────── */
        if (rand_r(&seed) % 20 == 0) {
            pthread_mutex_lock(&g_queue.mutex);
            int cancelled_id = -1;
            for (int ci = 0; ci < g_queue.count; ci++) {
                int idx = (g_queue.head + ci) % MAX_QUEUE_SIZE;
                if (g_queue.orders[idx].status == STATUS_PENDING) {
                    cancelled_id = g_queue.orders[idx].id;
                    /* Shift remaining entries to fill the gap */
                    for (int cj = ci; cj < g_queue.count - 1; cj++) {
                        int a = (g_queue.head + cj)     % MAX_QUEUE_SIZE;
                        int b = (g_queue.head + cj + 1) % MAX_QUEUE_SIZE;
                        g_queue.orders[a] = g_queue.orders[b];
                    }
                    g_queue.count--;
                    g_queue.tail = (g_queue.tail - 1 + MAX_QUEUE_SIZE)
                                   % MAX_QUEUE_SIZE;
                    break;
                }
            }
            pthread_mutex_unlock(&g_queue.mutex);

            if (cancelled_id != -1) {
                sem_post(&g_queue.spaces_available); /* restore free slot */
                pthread_mutex_lock(&g_stats_mutex);
                g_total_cancelled++;
                pthread_mutex_unlock(&g_stats_mutex);
                log_event("[W%d] X Cancelled order #%d", 3, tid, cancelled_id);
            }
        }

        /* ── Enqueue new order ───────────────────────────────── */
        queue_enqueue(&g_queue, &o);
        order_counter++;

        if (o.priority == PRIORITY_VIP)
            log_event("[W%d] * VIP Order #%d: %s", 2, tid, o.id, o.item);
        else
            log_event("[W%d] Order #%d: %s", 4, tid, o.id, o.item);

        /* Simulate customer arrival rate: 500–2500ms */
        int delay_ms = 500 + rand_r(&seed) % 2000;
        usleep(delay_ms * 1000);
    }

    w->orders_placed = order_counter;
    return NULL;
}
