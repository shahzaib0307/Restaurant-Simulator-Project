#ifndef CHEF_H
#define CHEF_H

#include "queue.h"

typedef struct {
    int id;              /* chef number 1..NUM_CHEFS */
    int orders_completed;
    int currently_cooking; /* 1 if busy, 0 if idle */
    char current_item[MAX_ITEM_LEN];
} ChefArgs;

/* shared chef state array (read by dashboard) */
extern ChefArgs g_chefs[NUM_CHEFS];

void *chef_thread(void *arg);

#endif
