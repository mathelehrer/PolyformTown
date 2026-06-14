#ifndef POLYFORMTOWN_RL7_EVO7_H
#define POLYFORMTOWN_RL7_EVO7_H

#include <stddef.h>
#include "boundary7.h"
#include "comp7.h"

#define MAX_ACTIONS     512
#define MAX_ADDS        16
#define MAX_HIST        256

typedef struct { int ci, slot; Tok sym; } Add;

typedef struct {
    char title[240];
    char status[240];
    Add add[MAX_ADDS];
    int nadd;
} Action;

typedef struct {
    State s[MAX_HIST];
    int n;
} History;

int enumerate_actions(const Model *m, const State *s, Action *acts);
int apply_action(State *s, const Action *a, char *status, size_t sz);
int apply_parallel_actions(const Model *m, State *s,
                           const Action *acts, int na,
                           char *status, size_t sz);
void push_hist(History *h,const State *s);
int pop_hist(History *h,State *s);

#endif
