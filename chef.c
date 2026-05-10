#include "chef.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

ChefArgs g_chefs[NUM_CHEFS];

/*
 * chef_thread — Consumer
 *
 * Workflow per order:
 *   1. sem_wait(kitchen_stations)  → acquire a cooking station
 *   2. queue_dequeue()             → get next order (mutex-protected)
 *   3. prepare the order           → simulate with usleep
 *   4. update stats                → mutex-protected
 *   5. sem_post(kitchen_stations)  → release station
 *
 * The semaphore ensures at most KITCHEN_STATIONS chefs cook
 * simultaneously, demonstrating resource-bounded concurrency.
 */
void *chef_thread(void *arg) {
    ChefArgs *c   = (ChefArgs *)arg;
    int       tid = c->id;

    unsigned int seed = (unsigned int)time(NULL) ^ (tid * 1234567891u);

    while (g_running) {
        /* ── Step 1: acquire a kitchen station ──────────────── */
        sem_wait(&g_kitchen_stations);

        if (!g_running) {
            sem_post(&g_kitchen_stations);
            break;
        }

        /* ── Step 2: dequeue an order ───────────────────────── */
        Order o;
        if (queue_dequeue(&g_queue, &o) != 0) {
            sem_post(&g_kitchen_stations);
            continue;
        }

        /* ── Step 3: mark as preparing ──────────────────────── */
        o.status  = STATUS_PREPARING;
        o.chef_id = tid;

        c->currently_cooking = 1;
        memcpy(c->current_item, o.item, MAX_ITEM_LEN - 1);
        c->current_item[MAX_ITEM_LEN - 1] = '\0';

        const char *vip_tag = (o.priority == PRIORITY_VIP) ? " ⭐VIP" : "";
        log_event("[C%d] Cooking #%d: %s%s", 1, tid, o.id, o.item, vip_tag);

        /* ── Step 4: simulate cooking time (1–4 seconds) ────── */
        int cook_ms = 1000 + rand_r(&seed) % 3000;
        /* VIP orders are expedited */
        if (o.priority == PRIORITY_VIP)
            cook_ms /= 2;
        usleep(cook_ms * 1000);

        /* ── Step 5: complete the order ─────────────────────── */
        o.status         = STATUS_COMPLETED;
        o.time_completed = time(NULL);
        o.wait_seconds   = difftime(o.time_completed, o.time_placed);

        c->currently_cooking = 0;
        memset(c->current_item, 0, MAX_ITEM_LEN);

        /* Update global stats */
        pthread_mutex_lock(&g_stats_mutex);
        g_total_completed++;
        g_total_wait_time += o.wait_seconds;
        c->orders_completed++;
        pthread_mutex_unlock(&g_stats_mutex);

        /* Save to history ring buffer */
        pthread_mutex_lock(&g_history_mutex);
        if (g_history_count < MAX_HISTORY) {
            g_history[g_history_count++] = o;
        } else {
            /* shift left, drop oldest */
            memmove(&g_history[0], &g_history[1],
                    sizeof(Order) * (MAX_HISTORY - 1));
            g_history[MAX_HISTORY - 1] = o;
        }
        pthread_mutex_unlock(&g_history_mutex);

        log_event("[C%d] Done  #%d in %.1fs%s", 1, tid, o.id,
                  o.wait_seconds, vip_tag);

        /* ── Step 6: release kitchen station ────────────────── */
        sem_post(&g_kitchen_stations);
    }

    return NULL;
}
