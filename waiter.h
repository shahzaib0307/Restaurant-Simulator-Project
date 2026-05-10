#ifndef WAITER_H
#define WAITER_H

typedef struct {
    int id;           /* waiter number 1..NUM_WAITERS */
    int orders_placed;
} WaiterArgs;

void *waiter_thread(void *arg);

#endif
