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
 * ═══════════════════════════════════════════════════════════════ */
OrderQueue      g_queue;
sem_t           g_kitchen_stations;
volatile int    g_running         = 1;
volatile int    g_paused          = 0;       /* p key toggles */
volatile int    g_waiter_delay_ms = 1500;    /* +/- keys adjust */
volatile int    g_total_placed    = 0;
volatile int    g_total_completed = 0;
volatile int    g_total_cancelled = 0;
double          g_total_wait_time = 0.0;
pthread_mutex_t g_stats_mutex     = PTHREAD_MUTEX_INITIALIZER;

LogEntry        g_log[MAX_LOG_ENTRIES];
int             g_log_head        = 0;
int             g_log_count       = 0;
pthread_mutex_t g_log_mutex       = PTHREAD_MUTEX_INITIALIZER;

Order           g_history[MAX_HISTORY];
int             g_history_count   = 0;
pthread_mutex_t g_history_mutex   = PTHREAD_MUTEX_INITIALIZER;

/* Manual assignment slots */
Order        g_assigned_order[NUM_CHEFS];
volatile int g_has_assignment[NUM_CHEFS] = {0, 0, 0};

/* ─── Signal handler ─────────────────────────────────────────── */
static void handle_signal(int sig) { (void)sig; g_running = 0; }

/* ─── Final report ───────────────────────────────────────────── */
static void print_final_report(WaiterArgs *waiters) {
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
               g_chefs[i].id, g_chefs[i].orders_completed);
    printf("╚══════════════════════════════════════════════════╝\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════════ */
int main(void) {
    srand((unsigned)time(NULL));

    queue_init(&g_queue);
    sem_init(&g_kitchen_stations, 0, KITCHEN_STATIONS);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* ── Waiter threads ─────────────────────────────────────── */
    pthread_t  waiter_tids[NUM_WAITERS];
    WaiterArgs waiters[NUM_WAITERS];
    for (int i = 0; i < NUM_WAITERS; i++) {
        waiters[i].id            = i + 1;
        waiters[i].orders_placed = 0;
        pthread_create(&waiter_tids[i], NULL, waiter_thread, &waiters[i]);
    }

    /* ── Chef threads ───────────────────────────────────────── */
    pthread_t chef_tids[NUM_CHEFS];
    for (int i = 0; i < NUM_CHEFS; i++) {
        g_chefs[i].id                = i + 1;
        g_chefs[i].orders_completed  = 0;
        g_chefs[i].currently_cooking = 0;
        memset(g_chefs[i].current_item, 0, MAX_ITEM_LEN);
        pthread_create(&chef_tids[i], NULL, chef_thread, &g_chefs[i]);
    }

    /* ── Dashboard ──────────────────────────────────────────── */
    dashboard_init();

    /* ── Main loop ──────────────────────────────────────────── */
    while (g_running) {
        int ch = getch();

        switch (ch) {

        /* ── Quit ──────────────────────────────────────────── */
        case 'q': case 'Q':
            g_running = 0;
            break;

        /* ── Pause / Resume all waiters ────────────────────── */
        case 'p': case 'P':
            g_paused = !g_paused;
            log_event("[USER] %s", 5, g_paused ? "PAUSED" : "RESUMED");
            break;

        /* ── Speed up waiters: F key (shorter delay = faster orders) */
        case 'f': case 'F': case '+': case '=':
            if (g_waiter_delay_ms > 200)
                g_waiter_delay_ms -= 200;
            log_event("[USER] FASTER — order delay now %dms", 4, g_waiter_delay_ms);
            break;

        /* ── Slow down waiters: S key (longer delay = slower orders) */
        case 's': case 'S': case '-': case '_':
            if (g_waiter_delay_ms < 5000)
                g_waiter_delay_ms += 200;
            log_event("[USER] SLOWER — order delay now %dms", 4, g_waiter_delay_ms);
            break;

        /* ── Inject a VIP order manually ───────────────────── */
        case 'v': case 'V':
            action_inject_vip();
            break;

        /* ── Cancel the oldest pending order ───────────────── */
        case 'c': case 'C':
            action_cancel_oldest();
            break;

        /* ── Reset stats counters ──────────────────────────── */
        case 'r': case 'R':
            action_reset_stats();
            break;

        /* ── Manual order entry popup ──────────────────────── */
        /* Auto-pauses waiters, opens input dialog, auto-resumes */
        case 'o': case 'O': {
            char dish[MAX_ITEM_LEN] = {0};
            OrderPriority prio;
            int waiter_id;
            int was_paused = g_paused;
            g_paused = 1;                      /* pause waiters  */
            nodelay(stdscr, FALSE);            /* blocking input */
            if (popup_manual_order(dish, MAX_ITEM_LEN, &prio, &waiter_id))
                action_manual_order(dish, prio, waiter_id);
            nodelay(stdscr, TRUE);
            if (!was_paused) g_paused = 0;    /* resume if wasn't already paused */
            break;
        }

        default:
            break;
        }

        dashboard_draw();
        usleep(100000);   /* 10 FPS */
    }

    /* ── Shutdown: wake all blocked threads ─────────────────── */
    dashboard_cleanup();

    for (int i = 0; i < NUM_CHEFS; i++)   sem_post(&g_queue.items_available);
    for (int i = 0; i < NUM_CHEFS; i++)   sem_post(&g_kitchen_stations);
    for (int i = 0; i < NUM_WAITERS; i++) sem_post(&g_queue.spaces_available);

    for (int i = 0; i < NUM_WAITERS; i++) pthread_join(waiter_tids[i], NULL);
    for (int i = 0; i < NUM_CHEFS; i++)   pthread_join(chef_tids[i],   NULL);

    queue_destroy(&g_queue);
    sem_destroy(&g_kitchen_stations);
    pthread_mutex_destroy(&g_stats_mutex);
    pthread_mutex_destroy(&g_log_mutex);
    pthread_mutex_destroy(&g_history_mutex);

    print_final_report(waiters);
    return 0;
}
