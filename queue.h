#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

/* ─── Constants ─────────────────────────────────────────────── */
#define MAX_QUEUE_SIZE    20
#define NUM_WAITERS        3
#define NUM_CHEFS          3
#define KITCHEN_STATIONS   2
#define MAX_ITEM_LEN      32
#define MAX_LOG_ENTRIES  200

/* ─── Enums ──────────────────────────────────────────────────── */
typedef enum {
    PRIORITY_NORMAL = 0,
    PRIORITY_VIP    = 1
} OrderPriority;

typedef enum {
    STATUS_PENDING   = 0,
    STATUS_PREPARING = 1,
    STATUS_COMPLETED = 2,
    STATUS_CANCELLED = 3
} OrderStatus;

/* ─── Order ──────────────────────────────────────────────────── */
typedef struct {
    int           id;
    int           waiter_id;
    int           chef_id;
    char          item[MAX_ITEM_LEN];
    OrderPriority priority;
    OrderStatus   status;
    time_t        time_placed;
    time_t        time_completed;
    double        wait_seconds;
} Order;

/* ─── Log entry ──────────────────────────────────────────────── */
typedef struct {
    char msg[128];
    int  color_pair;
} LogEntry;

/* ─── Shared queue ───────────────────────────────────────────── */
typedef struct {
    Order           orders[MAX_QUEUE_SIZE];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    sem_t           items_available;
    sem_t           spaces_available;
} OrderQueue;

/* ─── Globals (defined in main.c) ───────────────────────────── */
extern OrderQueue      g_queue;
extern sem_t           g_kitchen_stations;
extern volatile int    g_running;
extern volatile int    g_paused;          /* NEW: pause waiters */
extern volatile int    g_waiter_delay_ms; /* NEW: waiter speed  */
extern volatile int    g_total_placed;
extern volatile int    g_total_completed;
extern volatile int    g_total_cancelled;
extern double          g_total_wait_time;
extern pthread_mutex_t g_stats_mutex;

/* Event log */
extern LogEntry        g_log[MAX_LOG_ENTRIES];
extern int             g_log_head;
extern int             g_log_count;
extern pthread_mutex_t g_log_mutex;

/* Completed history */
#define MAX_HISTORY 50
extern Order           g_history[MAX_HISTORY];
extern int             g_history_count;
extern pthread_mutex_t g_history_mutex;

/* ─── Function prototypes ───────────────────────────────────── */
void queue_init   (OrderQueue *q);
int  queue_enqueue(OrderQueue *q, Order *order);
int  queue_dequeue(OrderQueue *q, Order *out);
void queue_destroy(OrderQueue *q);
void log_event    (const char *fmt, int color_pair, ...);

/* User-input actions (called from main loop) */
void action_inject_vip   (void);
void action_cancel_oldest(void);
void action_reset_stats  (void);

#endif /* QUEUE_H */
