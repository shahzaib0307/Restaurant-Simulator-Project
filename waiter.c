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
 * Respects two user-controlled globals:
 *   g_paused         — if 1, waiter sleeps and generates no orders
 *   g_waiter_delay_ms — controls how fast orders are generated
 *
 * Also has 5% chance to cancel a pending order each cycle.
 */
void *waiter_thread(void *arg) {
    WaiterArgs  *w   = (WaiterArgs *)arg;
    int          tid = w->id;
    int          order_counter = 0;
    unsigned int seed = (unsigned int)time(NULL) ^ (tid * 2654435761u);

    while (g_running) {

        /* ── Pause check ─────────────────────────────────────── */
        while (g_paused && g_running)
            usleep(100000);  /* sleep 100ms while paused */
        if (!g_running) break;

        /* ── Build order ─────────────────────────────────────── */
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

        /* ── Order Cancellation (5% chance) ──────────────────── */
        if (rand_r(&seed) % 20 == 0) {
            pthread_mutex_lock(&g_queue.mutex);
            int cancelled_id = -1;
            for (int ci = 0; ci < g_queue.count; ci++) {
                int idx = (g_queue.head + ci) % MAX_QUEUE_SIZE;
                if (g_queue.orders[idx].status == STATUS_PENDING) {
                    cancelled_id = g_queue.orders[idx].id;
                    for (int cj = ci; cj < g_queue.count - 1; cj++) {
                        int a = (g_queue.head + cj)     % MAX_QUEUE_SIZE;
                        int b = (g_queue.head + cj + 1) % MAX_QUEUE_SIZE;
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
                log_event("[W%d] X Cancelled order #%d", 3, tid, cancelled_id);
            }
        }

        /* ── Enqueue ─────────────────────────────────────────── */
        queue_enqueue(&g_queue, &o);
        order_counter++;

        if (o.priority == PRIORITY_VIP)
            log_event("[W%d] * VIP Order #%d: %s", 2, tid, o.id, o.item);
        else
            log_event("[W%d] Order #%d: %s", 4, tid, o.id, o.item);

        /* ── Delay controlled by user (+/-) ──────────────────── */
        int delay = g_waiter_delay_ms + (int)(rand_r(&seed) % 500) - 250;
        if (delay < 100) delay = 100;
        usleep(delay * 1000);
    }

    w->orders_placed = order_counter;
    return NULL;
}
