#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <ncurses.h>
#include <signal.h>
#include <time.h>

#include "queue.h"
#include "waiter.h"
#include "chef.h"
#include "dashboard.h"

/* ═══════════════════════════════════════════════════════════════
 *  GLOBAL DEFINITIONS
 *  (declared extern in queue.h; defined exactly once here)
 * ═══════════════════════════════════════════════════════════════ */

OrderQueue      g_queue;
sem_t           g_kitchen_stations;
volatile int    g_running        = 1;
volatile int    g_total_placed   = 0;
volatile int    g_total_completed= 0;
volatile int    g_total_cancelled= 0;
double          g_total_wait_time= 0.0;
pthread_mutex_t g_stats_mutex    = PTHREAD_MUTEX_INITIALIZER;

LogEntry        g_log[MAX_LOG_ENTRIES];
int             g_log_head       = 0;
int             g_log_count      = 0;
pthread_mutex_t g_log_mutex      = PTHREAD_MUTEX_INITIALIZER;

Order           g_history[MAX_HISTORY];
int             g_history_count  = 0;
pthread_mutex_t g_history_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* ─── Signal handler for Ctrl+C ─────────────────────────────── */
static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* ─── Print final report after ncurses exits ─────────────────── */
static void print_final_report(WaiterArgs *waiters, ChefArgs *chefs) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║        RESTAURANT SIMULATION — FINAL REPORT      ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Total orders placed   : %-24d║\n", g_total_placed);
    printf("║  Total orders completed: %-24d║\n", g_total_completed);
    printf("║  Total orders cancelled: %-24d║\n", g_total_cancelled);
    double avg = g_total_completed > 0
                 ? g_total_wait_time / g_total_completed : 0.0;
    printf("║  Average wait time     : %-21.2f sec║\n", avg);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  WAITER STATS                                    ║\n");
    for (int i = 0; i < NUM_WAITERS; i++)
        printf("║    Waiter %d placed  : %-26d║\n",
               waiters[i].id, waiters[i].orders_placed);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  CHEF STATS                                      ║\n");
    for (int i = 0; i < NUM_CHEFS; i++)
        printf("║    Chef %d completed : %-26d║\n",
               chefs[i].id, chefs[i].orders_completed);
    printf("╚══════════════════════════════════════════════════╝\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════════ */
int main(void) {
    srand((unsigned)time(NULL));

    /* ── Init shared structures ─────────────────────────────── */
    queue_init(&g_queue);
    sem_init(&g_kitchen_stations, 0, KITCHEN_STATIONS);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* ── Create waiter threads ──────────────────────────────── */
    pthread_t   waiter_tids[NUM_WAITERS];
    WaiterArgs  waiters[NUM_WAITERS];
    for (int i = 0; i < NUM_WAITERS; i++) {
        waiters[i].id           = i + 1;
        waiters[i].orders_placed= 0;
        pthread_create(&waiter_tids[i], NULL, waiter_thread, &waiters[i]);
    }

    /* ── Create chef threads ────────────────────────────────── */
    pthread_t  chef_tids[NUM_CHEFS];
    for (int i = 0; i < NUM_CHEFS; i++) {
        g_chefs[i].id               = i + 1;
        g_chefs[i].orders_completed = 0;
        g_chefs[i].currently_cooking= 0;
        memset(g_chefs[i].current_item, 0, MAX_ITEM_LEN);
        pthread_create(&chef_tids[i], NULL, chef_thread, &g_chefs[i]);
    }

    /* ── Init ncurses dashboard ─────────────────────────────── */
    dashboard_init();

    /* ── Main loop: refresh dashboard at ~10 FPS ────────────── */
    while (g_running) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q')
            g_running = 0;

        dashboard_draw();
        usleep(100000);   /* 100ms */
    }

    /* ── Shutdown ────────────────────────────────────────────── */
    dashboard_cleanup();

    /* Wake any blocked threads so they can exit */
    for (int i = 0; i < NUM_CHEFS; i++)
        sem_post(&g_queue.items_available);
    for (int i = 0; i < NUM_CHEFS; i++)
        sem_post(&g_kitchen_stations);
    for (int i = 0; i < NUM_WAITERS; i++)
        sem_post(&g_queue.spaces_available);

    /* ── Join all threads ───────────────────────────────────── */
    for (int i = 0; i < NUM_WAITERS; i++)
        pthread_join(waiter_tids[i], NULL);
    for (int i = 0; i < NUM_CHEFS; i++)
        pthread_join(chef_tids[i], NULL);

    /* ── Cleanup ─────────────────────────────────────────────── */
    queue_destroy(&g_queue);
    sem_destroy(&g_kitchen_stations);
    pthread_mutex_destroy(&g_stats_mutex);
    pthread_mutex_destroy(&g_log_mutex);
    pthread_mutex_destroy(&g_history_mutex);

    print_final_report(waiters, g_chefs);
    return 0;
}
