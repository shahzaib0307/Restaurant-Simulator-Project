#include "queue.h"
#ifndef DASHBOARD_H
#define DASHBOARD_H

void dashboard_init(void);
void dashboard_draw(void);
void dashboard_cleanup(void);

#endif

/* Popup input modes */
typedef enum {
    POPUP_NONE   = 0,
    POPUP_ORDER  = 1,   /* O key: type custom order */
    POPUP_ASSIGN = 2    /* A key: assign order to chef */
} PopupMode;

/* Returns filled dish string and settings via out-params.
 * Returns 1 if user confirmed, 0 if cancelled (ESC) */
int popup_manual_order(char *dish_out, int dish_max,
                       OrderPriority *prio_out, int *waiter_out);
int popup_assign_chef(int *chef_id_out);
