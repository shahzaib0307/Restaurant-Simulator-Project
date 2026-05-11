#include "dashboard.h"
#include "queue.h"
#include "chef.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

/* ─── Color pairs ────────────────────────────────────────────── */
#define COL_GREEN    1
#define COL_YELLOW   2
#define COL_RED      3
#define COL_CYAN     4
#define COL_WHITE    5
#define COL_MAGENTA  6
#define COL_HEADER   7
#define COL_BOX      8
#define COL_VIP      9

/* Sub-windows */
static WINDOW *w_header   = NULL;
static WINDOW *w_chefs    = NULL;
static WINDOW *w_queue    = NULL;
static WINDOW *w_log      = NULL;
static WINDOW *w_stats    = NULL;
static WINDOW *w_history  = NULL;
static WINDOW *w_footer   = NULL;

static void make_box(WINDOW *win, const char *title) {
    wattron(win, COLOR_PAIR(COL_BOX));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(COL_BOX));
    if (title && strlen(title) > 0) {
        int w = getmaxx(win);
        wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvwprintw(win, 0, (w - (int)strlen(title) - 2) / 2, " %s ", title);
        wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    }
}

void dashboard_init(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    start_color();

    /* Define color pairs */
    init_pair(COL_GREEN,   COLOR_GREEN,   COLOR_BLACK);
    init_pair(COL_YELLOW,  COLOR_YELLOW,  COLOR_BLACK);
    init_pair(COL_RED,     COLOR_RED,     COLOR_BLACK);
    init_pair(COL_CYAN,    COLOR_CYAN,    COLOR_BLACK);
    init_pair(COL_WHITE,   COLOR_WHITE,   COLOR_BLACK);
    init_pair(COL_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COL_HEADER,  COLOR_BLACK,   COLOR_CYAN);
    init_pair(COL_BOX,     COLOR_CYAN,    COLOR_BLACK);
    init_pair(COL_VIP,     COLOR_BLACK,   COLOR_YELLOW);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    /*
     *  Layout (min 80x24):
     *
     *  ┌─────────────── HEADER (3 rows) ───────────────┐
     *  ├──── CHEFS (8) ────┬──── QUEUE (8) ────────────┤
     *  ├──── STATS (7) ────┴──── LOG (7) ──────────────┤
     *  ├──────────── HISTORY (rows-26) ────────────────┤
     *  └─────────────── FOOTER (3) ────────────────────┘
     */

    int left_w  = cols / 2;
    int right_w = cols - left_w;

    w_header  = newwin(3,    cols,    0,        0);
    w_chefs   = newwin(8,    left_w,  3,        0);
    w_queue   = newwin(8,    right_w, 3,        left_w);
    w_stats   = newwin(7,    left_w,  11,       0);
    w_log     = newwin(7,    right_w, 11,       left_w);
    int hist_h = rows - 21;
    if (hist_h < 4) hist_h = 4;
    w_history = newwin(hist_h, cols,   18,       0);
    w_footer  = newwin(3,    cols,    rows - 3, 0);
}

static void draw_header(void) {
    (void)getmaxx(w_header);
    wbkgd(w_header, COLOR_PAIR(COL_HEADER));
    werase(w_header);

    time_t now = time(NULL);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", localtime(&now));

    wattron(w_header, A_BOLD);
    mvwprintw(w_header, 1, 2,
              "  RESTAURANT ORDER MANAGEMENT SIMULATOR  |  "
              "Threads: %d waiters  %d chefs  |  Stations: %d  |  %s  ",
              NUM_WAITERS, NUM_CHEFS, KITCHEN_STATIONS, tbuf);
    wattroff(w_header, A_BOLD);
    wnoutrefresh(w_header);
}

static void draw_chefs(void) {
    werase(w_chefs);
    make_box(w_chefs, "KITCHEN STATIONS");

    /* Show semaphore value */
    int sem_val = 0;
    sem_getvalue(&g_kitchen_stations, &sem_val);
    wattron(w_chefs, COLOR_PAIR(COL_CYAN));
    mvwprintw(w_chefs, 1, 2, "Free stations: %d / %d", sem_val, KITCHEN_STATIONS);
    wattroff(w_chefs, COLOR_PAIR(COL_CYAN));

    for (int i = 0; i < NUM_CHEFS; i++) {
        ChefArgs *c = &g_chefs[i];
        int row = 2 + i * 2;

        if (c->currently_cooking) {
            wattron(w_chefs, COLOR_PAIR(COL_GREEN) | A_BOLD);
            mvwprintw(w_chefs, row, 2, "Chef %d [COOKING] %-20s",
                      c->id, c->current_item);
            wattroff(w_chefs, COLOR_PAIR(COL_GREEN) | A_BOLD);
        } else {
            wattron(w_chefs, COLOR_PAIR(COL_WHITE));
            mvwprintw(w_chefs, row, 2, "Chef %d [  IDLE ] %-20s",
                      c->id, "");
            wattroff(w_chefs, COLOR_PAIR(COL_WHITE));
        }
        /* Mini bar */
        wattron(w_chefs, COLOR_PAIR(COL_YELLOW));
        mvwprintw(w_chefs, row, getmaxx(w_chefs) - 10,
                  "done:%3d", c->orders_completed);
        wattroff(w_chefs, COLOR_PAIR(COL_YELLOW));
    }
    wnoutrefresh(w_chefs);
}

static void draw_queue(void) {
    werase(w_queue);
    make_box(w_queue, "ORDER QUEUE");

    pthread_mutex_lock(&g_queue.mutex);
    int count = g_queue.count;
    /* Copy up to 5 orders for display */
    Order snapshot[5];
    int   show = count < 5 ? count : 5;
    for (int i = 0; i < show; i++) {
        snapshot[i] = g_queue.orders[(g_queue.head + i) % MAX_QUEUE_SIZE];
    }
    pthread_mutex_unlock(&g_queue.mutex);

    int w = getmaxx(w_queue);
    /* Queue fill bar */
    int bar_w = w - 4;
    int filled = (count * bar_w) / MAX_QUEUE_SIZE;
    wattron(w_queue, COLOR_PAIR(count > 15 ? COL_RED : COL_GREEN));
    mvwprintw(w_queue, 1, 2, "[");
    for (int i = 0; i < bar_w; i++)
        wprintw(w_queue, i < filled ? "#" : ".");
    wprintw(w_queue, "] %2d/%-2d", count, MAX_QUEUE_SIZE);
    wattroff(w_queue, COLOR_PAIR(count > 15 ? COL_RED : COL_GREEN));

    /* Show next orders */
    for (int i = 0; i < show; i++) {
        Order *o = &snapshot[i];
        int row  = 3 + i;
        if (o->priority == PRIORITY_VIP) {
            wattron(w_queue, COLOR_PAIR(COL_VIP) | A_BOLD);
            mvwprintw(w_queue, row, 2, " #%-4d %-18s [VIP] ",
                      o->id, o->item);
            wattroff(w_queue, COLOR_PAIR(COL_VIP) | A_BOLD);
        } else {
            wattron(w_queue, COLOR_PAIR(COL_WHITE));
            mvwprintw(w_queue, row, 2, " #%-4d %-18s W%d   ",
                      o->id, o->item, o->waiter_id);
            wattroff(w_queue, COLOR_PAIR(COL_WHITE));
        }
    }
    if (count == 0) {
        wattron(w_queue, COLOR_PAIR(COL_CYAN) | A_DIM);
        mvwprintw(w_queue, 3, 2, "  (queue empty — chefs waiting)");
        wattroff(w_queue, COLOR_PAIR(COL_CYAN) | A_DIM);
    }
    wnoutrefresh(w_queue);
}

static void draw_stats(void) {
    werase(w_stats);
    make_box(w_stats, "PERFORMANCE METRICS");

    pthread_mutex_lock(&g_stats_mutex);
    int placed    = g_total_placed;
    int completed = g_total_completed;
    int cancelled = g_total_cancelled;
    double total_wait = g_total_wait_time;
    pthread_mutex_unlock(&g_stats_mutex);

    double avg_wait = completed > 0 ? total_wait / completed : 0.0;

    wattron(w_stats, COLOR_PAIR(COL_CYAN));
    mvwprintw(w_stats, 1, 2, "Orders placed   : %d", placed);
    wattroff(w_stats, COLOR_PAIR(COL_CYAN));

    wattron(w_stats, COLOR_PAIR(COL_GREEN));
    mvwprintw(w_stats, 2, 2, "Orders completed: %d", completed);
    wattroff(w_stats, COLOR_PAIR(COL_GREEN));

    wattron(w_stats, COLOR_PAIR(COL_RED));
    mvwprintw(w_stats, 3, 2, "Orders cancelled: %d", cancelled);
    wattroff(w_stats, COLOR_PAIR(COL_RED));

    wattron(w_stats, COLOR_PAIR(COL_YELLOW));
    mvwprintw(w_stats, 4, 2, "Avg wait time   : %.1f sec", avg_wait);
    mvwprintw(w_stats, 5, 2, "Throughput      : %d orders", completed);
    wattroff(w_stats, COLOR_PAIR(COL_YELLOW));

    /* Show speed and pause state */
    if (g_paused) {
        wattron(w_stats, COLOR_PAIR(COL_RED) | A_BOLD | A_BLINK);
        mvwprintw(w_stats, 5, getmaxx(w_stats) - 12, " PAUSED ");
        wattroff(w_stats, COLOR_PAIR(COL_RED) | A_BOLD | A_BLINK);
    } else {
        wattron(w_stats, COLOR_PAIR(COL_GREEN));
        mvwprintw(w_stats, 5, getmaxx(w_stats) - 14, "spd:%4dms", g_waiter_delay_ms);
        wattroff(w_stats, COLOR_PAIR(COL_GREEN));
    }

    wnoutrefresh(w_stats);
}

static void draw_log(void) {
    werase(w_log);
    make_box(w_log, "EVENT LOG");

    int rows = getmaxy(w_log) - 2;

    pthread_mutex_lock(&g_log_mutex);
    int count = g_log_count;
    /* Show last `rows` entries */
    int start = count > rows ? count - rows : 0;
    for (int i = start; i < count; i++) {
        int idx = (g_log_head + i) % MAX_LOG_ENTRIES;
        int row = 1 + (i - start);
        wattron(w_log, COLOR_PAIR(g_log[idx].color_pair));
        mvwprintw(w_log, row, 1, " %-*.*s",
                  getmaxx(w_log) - 3, getmaxx(w_log) - 3,
                  g_log[idx].msg);
        wattroff(w_log, COLOR_PAIR(g_log[idx].color_pair));
    }
    pthread_mutex_unlock(&g_log_mutex);

    wnoutrefresh(w_log);
}

static void draw_history(void) {
    werase(w_history);
    make_box(w_history, "COMPLETED ORDERS HISTORY");

    int rows = getmaxy(w_history) - 2;
    int w    = getmaxx(w_history);

    /* Column header */
    wattron(w_history, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(w_history, 1, 2,
              "%-6s %-20s %-8s %-8s %-8s",
              "ID", "ITEM", "WAITER", "CHEF", "WAIT(s)");
    wattroff(w_history, COLOR_PAIR(COL_HEADER) | A_BOLD);

    pthread_mutex_lock(&g_history_mutex);
    int cnt   = g_history_count;
    int start = cnt > rows - 1 ? cnt - (rows - 1) : 0;

    for (int i = start; i < cnt; i++) {
        Order *o  = &g_history[i];
        int    row = 2 + (i - start);

        if (o->priority == PRIORITY_VIP) {
            wattron(w_history, COLOR_PAIR(COL_VIP) | A_BOLD);
        } else {
            wattron(w_history, i % 2 == 0 ? COLOR_PAIR(COL_WHITE)
                                           : COLOR_PAIR(COL_CYAN));
        }
        mvwprintw(w_history, row, 2,
                  "%-6d %-20s W%-7d C%-7d %.1f",
                  o->id, o->item, o->waiter_id, o->chef_id, o->wait_seconds);
        if (o->priority == PRIORITY_VIP) {
            mvwprintw(w_history, row, w - 8, " [VIP] ");
            wattroff(w_history, COLOR_PAIR(COL_VIP) | A_BOLD);
        } else {
            wattroff(w_history, i % 2 == 0 ? COLOR_PAIR(COL_WHITE)
                                            : COLOR_PAIR(COL_CYAN));
        }
    }
    pthread_mutex_unlock(&g_history_mutex);

    wnoutrefresh(w_history);
}

static void draw_footer(void) {
    (void)getmaxx(w_footer);
    wbkgd(w_footer, COLOR_PAIR(COL_HEADER));
    werase(w_footer);
    wattron(w_footer, A_BOLD);
    mvwprintw(w_footer, 1, 2,
              " Q:Quit  P:Pause/Resume  F:Faster  S:Slower"
              "  V:VIP  C:Cancel  R:Reset  O:Place Order manually ");
    wattroff(w_footer, A_BOLD);
    wnoutrefresh(w_footer);
}

/* ─── Main draw call ─────────────────────────────────────────── */
void dashboard_draw(void) {
    draw_header();
    draw_chefs();
    draw_queue();
    draw_stats();
    draw_log();
    draw_history();
    draw_footer();
    doupdate();           /* single flush — avoids flicker */
}

void dashboard_cleanup(void) {
    if (w_header)  delwin(w_header);
    if (w_chefs)   delwin(w_chefs);
    if (w_queue)   delwin(w_queue);
    if (w_stats)   delwin(w_stats);
    if (w_log)     delwin(w_log);
    if (w_history) delwin(w_history);
    if (w_footer)  delwin(w_footer);
    endwin();
}

/* ═══════════════════════════════════════════════════════════════
 * POPUP: Manual Order Entry  (O key)
 *
 * Shows a centered dialog where the user can:
 *   - Type a dish name (up to 31 chars)
 *   - Toggle Normal / VIP with TAB
 *   - Choose waiter 1/2/3 with number keys
 *   - Confirm with ENTER, cancel with ESC
 * ═══════════════════════════════════════════════════════════════ */
int popup_manual_order(char *dish_out, int dish_max,
                       OrderPriority *prio_out, int *waiter_out)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int pw = 54, ph = 14;
    int py = (rows - ph) / 2;
    int px = (cols - pw) / 2;

    WINDOW *pop = newwin(ph, pw, py, px);
    keypad(pop, TRUE);
    curs_set(1);   /* show cursor for typing */

    char dish[MAX_ITEM_LEN] = {0};
    int  dlen    = 0;
    OrderPriority prio   = PRIORITY_NORMAL;
    int  waiter  = 1;
    int  confirmed = 0;

    while (1) {
        werase(pop);
        wattron(pop, COLOR_PAIR(COL_BOX));
        box(pop, 0, 0);
        wattroff(pop, COLOR_PAIR(COL_BOX));

        /* Title */
        wattron(pop, COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvwprintw(pop, 0, (pw - 16) / 2, " NEW MANUAL ORDER ");
        wattroff(pop, COLOR_PAIR(COL_HEADER) | A_BOLD);

        /* Dish name field */
        wattron(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        mvwprintw(pop, 2, 2, "Dish Name:");
        wattroff(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        wattron(pop, COLOR_PAIR(COL_WHITE));
        mvwprintw(pop, 3, 2, "[ %-30s ]", dish);
        wattroff(pop, COLOR_PAIR(COL_WHITE));

        /* Priority toggle */
        wattron(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        mvwprintw(pop, 5, 2, "Priority:  (TAB to toggle)");
        wattroff(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        if (prio == PRIORITY_NORMAL) {
            wattron(pop, COLOR_PAIR(COL_WHITE) | A_BOLD);
            mvwprintw(pop, 6, 2, "  [*] NORMAL    [ ] VIP  ");
            wattroff(pop, COLOR_PAIR(COL_WHITE) | A_BOLD);
        } else {
            wattron(pop, COLOR_PAIR(COL_VIP) | A_BOLD);
            mvwprintw(pop, 6, 2, "  [ ] NORMAL    [*] VIP  ");
            wattroff(pop, COLOR_PAIR(COL_VIP) | A_BOLD);
        }

        /* Waiter selection */
        wattron(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        mvwprintw(pop, 8, 2, "Waiter:  (press 1 / 2 / 3)");
        wattroff(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        for (int w = 1; w <= NUM_WAITERS; w++) {
            if (w == waiter) {
                wattron(pop, COLOR_PAIR(COL_GREEN) | A_BOLD);
                mvwprintw(pop, 9, 2 + (w-1)*14, "  [Waiter %d]  ", w);
                wattroff(pop, COLOR_PAIR(COL_GREEN) | A_BOLD);
            } else {
                wattron(pop, COLOR_PAIR(COL_WHITE));
                mvwprintw(pop, 9, 2 + (w-1)*14, "   Waiter %d   ", w);
                wattroff(pop, COLOR_PAIR(COL_WHITE));
            }
        }

        /* Instructions */
        wattron(pop, COLOR_PAIR(COL_MAGENTA));
        mvwprintw(pop, 11, 2, "ENTER = Confirm    ESC = Cancel");
        wattroff(pop, COLOR_PAIR(COL_MAGENTA));

        /* Position cursor at end of dish input */
        wmove(pop, 3, 3 + dlen);
        wrefresh(pop);

        int ch = wgetch(pop);

        if (ch == 27) {          /* ESC — cancel */
            break;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (dlen > 0) {      /* must have typed something */
                confirmed = 1;
                break;
            }
        } else if (ch == '\t') { /* TAB — toggle priority */
            prio = (prio == PRIORITY_NORMAL) ? PRIORITY_VIP : PRIORITY_NORMAL;
        } else if (ch == '1' || ch == '2' || ch == '3') {
            waiter = ch - '0';
        } else if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && dlen > 0) {
            dish[--dlen] = '\0';
        } else if (ch >= 32 && ch < 127 && dlen < dish_max - 1) {
            dish[dlen++] = (char)ch;
            dish[dlen]   = '\0';
        }
    }

    curs_set(0);
    delwin(pop);
    touchwin(stdscr);
    refresh();

    if (confirmed) {
        strncpy(dish_out, dish, dish_max - 1);
        dish_out[dish_max - 1] = '\0';
        *prio_out   = prio;
        *waiter_out = waiter;
    }
    return confirmed;
}

/* ═══════════════════════════════════════════════════════════════
 * POPUP: Manual Chef Assignment  (A key)
 *
 * Shows which chefs are idle and lets the user pick one.
 * The chosen chef gets the oldest pending order from the queue.
 * ═══════════════════════════════════════════════════════════════ */
int popup_assign_chef(int *chef_id_out)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int pw = 50, ph = 12;
    int py = (rows - ph) / 2;
    int px = (cols - pw) / 2;

    WINDOW *pop = newwin(ph, pw, py, px);
    keypad(pop, TRUE);

    /* Check if there are any pending orders first */
    pthread_mutex_lock(&g_queue.mutex);
    int has_pending = 0;
    for (int i = 0; i < g_queue.count; i++) {
        int idx = (g_queue.head + i) % MAX_QUEUE_SIZE;
        if (g_queue.orders[idx].status == STATUS_PENDING) {
            has_pending = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_queue.mutex);

    int confirmed = 0;

    while (1) {
        werase(pop);
        wattron(pop, COLOR_PAIR(COL_BOX));
        box(pop, 0, 0);
        wattroff(pop, COLOR_PAIR(COL_BOX));

        wattron(pop, COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvwprintw(pop, 0, (pw - 22) / 2, " ASSIGN ORDER TO CHEF ");
        wattroff(pop, COLOR_PAIR(COL_HEADER) | A_BOLD);

        if (!has_pending) {
            wattron(pop, COLOR_PAIR(COL_RED) | A_BOLD);
            mvwprintw(pop, 4, 4, "No pending orders in queue!");
            mvwprintw(pop, 6, 4, "Press any key to close.");
            wattroff(pop, COLOR_PAIR(COL_RED) | A_BOLD);
            wrefresh(pop);
            wgetch(pop);
            break;
        }

        wattron(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);
        mvwprintw(pop, 2, 2, "Press chef number to assign oldest order:");
        wattroff(pop, COLOR_PAIR(COL_CYAN) | A_BOLD);

        /* Show each chef and their status */
        for (int i = 0; i < NUM_CHEFS; i++) {
            int busy = g_chefs[i].currently_cooking;
            int row  = 4 + i;
            if (busy) {
                wattron(pop, COLOR_PAIR(COL_RED));
                mvwprintw(pop, row, 4,
                    "[%d] Chef %d — BUSY: %-20s",
                    i+1, i+1, g_chefs[i].current_item);
                wattroff(pop, COLOR_PAIR(COL_RED));
            } else {
                wattron(pop, COLOR_PAIR(COL_GREEN) | A_BOLD);
                mvwprintw(pop, row, 4,
                    "[%d] Chef %d — IDLE (ready to assign)",
                    i+1, i+1);
                wattroff(pop, COLOR_PAIR(COL_GREEN) | A_BOLD);
            }
        }

        wattron(pop, COLOR_PAIR(COL_MAGENTA));
        mvwprintw(pop, 8+0, 2, "Press 1/2/3 to assign   ESC to cancel");
        wattroff(pop, COLOR_PAIR(COL_MAGENTA));

        wrefresh(pop);

        int ch = wgetch(pop);
        if (ch == 27) break;   /* ESC */
        if (ch >= '1' && ch <= '0' + NUM_CHEFS) {
            *chef_id_out = ch - '0';
            confirmed = 1;
            break;
        }
    }

    delwin(pop);
    touchwin(stdscr);
    refresh();
    return confirmed;
}
