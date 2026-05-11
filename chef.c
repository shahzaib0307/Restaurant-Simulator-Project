#include "chef.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

ChefArgs g_chefs[NUM_CHEFS];

/* ── Internal: cook a single order ─────────────────────────── */
static void cook_order(ChefArgs *c, Order *o, unsigned int *seed) {
    int tid = c->id;

    o->status  = STATUS_PREPARING;
    o->chef_id = tid;

    c->currently_cooking = 1;
    memcpy(c->current_item, o->item, MAX_ITEM_LEN - 1);
    c->current_item[MAX_ITEM_LEN - 1] = '\0';

    const char *vip_tag = (o->priority == PRIORITY_VIP) ? " VIP" : "";
    log_event("[C%d] Cooking #%d: %s%s", 1, tid, o->id, o->item, vip_tag);

    /* Simulate cooking time */
    int cook_ms = 1000 + rand_r(seed) % 3000;
    if (o->priority == PRIORITY_VIP) cook_ms /= 2;
    usleep(cook_ms * 1000);

    /* Complete */
    o->status         = STATUS_COMPLETED;
    o->time_completed = time(NULL);
    o->wait_seconds   = difftime(o->time_completed, o->time_placed);

    c->currently_cooking = 0;
    memset(c->current_item, 0, MAX_ITEM_LEN);

    pthread_mutex_lock(&g_stats_mutex);
    g_total_completed++;
    g_total_wait_time += o->wait_seconds;
    c->orders_completed++;
    pthread_mutex_unlock(&g_stats_mutex);

    pthread_mutex_lock(&g_history_mutex);
    if (g_history_count < MAX_HISTORY) {
        g_history[g_history_count++] = *o;
    } else {
        memmove(&g_history[0], &g_history[1],
                sizeof(Order) * (MAX_HISTORY - 1));
        g_history[MAX_HISTORY - 1] = *o;
    }
    pthread_mutex_unlock(&g_history_mutex);

    log_event("[C%d] Done  #%d in %.1fs%s", 1, tid, o->id,
              o->wait_seconds, vip_tag);
}

/*
 * chef_thread — Consumer
 *
 * Each iteration:
 *   1. Check if main thread manually assigned an order to THIS chef
 *      (g_has_assignment[idx] flag). If yes, cook it directly —
 *      no semaphore needed since it was pre-removed from the queue.
 *   2. Otherwise, follow normal path:
 *      sem_wait(kitchen_stations) -> dequeue -> cook -> sem_post
 */
void *chef_thread(void *arg) {
    ChefArgs    *c   = (ChefArgs *)arg;
    int          tid = c->id;
    int          idx = tid - 1;   /* 0-based index */
    unsigned int seed = (unsigned int)time(NULL) ^ (tid * 1234567891u);

    while (g_running) {

        /* ── Check for manual assignment first ──────────────── */
        if (g_has_assignment[idx]) {
            Order o = g_assigned_order[idx];
            g_has_assignment[idx] = 0;   /* clear flag */

            log_event("[C%d] Manual assignment: #%d %s", 2,
                      tid, o.id, o.item);
            cook_order(c, &o, &seed);
            continue;   /* skip normal sem_wait path */
        }

        /* ── Normal path: acquire a kitchen station ─────────── */
        sem_wait(&g_kitchen_stations);

        if (!g_running) {
            sem_post(&g_kitchen_stations);
            break;
        }

        /* Check again — a manual assignment might have arrived
         * while we were waiting on the semaphore               */
        if (g_has_assignment[idx]) {
            sem_post(&g_kitchen_stations); /* return station */
            Order o = g_assigned_order[idx];
            g_has_assignment[idx] = 0;
            cook_order(c, &o, &seed);
            continue;
        }

        /* ── Dequeue from shared queue ──────────────────────── */
        Order o;
        if (queue_dequeue(&g_queue, &o) != 0) {
            sem_post(&g_kitchen_stations);
            continue;
        }

        cook_order(c, &o, &seed);

        sem_post(&g_kitchen_stations);
    }

    return NULL;
}
