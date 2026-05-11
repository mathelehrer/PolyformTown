#ifndef RL0_FORGET_MAP_H
#define RL0_FORGET_MAP_H

#include <stddef.h>

#define RL0_FM_MAX_ITEMS 16

/* Indexed vertex figure item.  p is parity/orientation (+1 or -1),
   i is the source/canonical tile vertex index under that parity. */
typedef struct {
    int p;
    int i;
} RL0FMItem;

typedef struct {
    int n;
    RL0FMItem item[RL0_FM_MAX_ITEMS];
} RL0FMCycle;

typedef RL0FMCycle RL0FMArc;

typedef struct {
    int count;
    int capacity;
    int *level;
    RL0FMCycle *cycle;
} RL0FMDeletionSet;

typedef struct {
    RL0FMArc key;
    RL0FMArc *values;
    int value_count;
    int value_capacity;
} RL0FMRow;

typedef struct {
    RL0FMCycle *cycles;
    int cycle_count;
    int cycle_capacity;
    RL0FMRow *rows;
    int row_count;
    int row_capacity;
} RL0ForgetMap;

void rl0_fm_init(RL0ForgetMap *map);
void rl0_fm_clear(RL0ForgetMap *map);
void rl0_fm_deletions_init(RL0FMDeletionSet *set);
void rl0_fm_deletions_clear(RL0FMDeletionSet *set);
int rl0_fm_deletions_add_cycle(RL0FMDeletionSet *set, int level, const RL0FMCycle *cycle);
int rl0_fm_deletions_contains_cycle(const RL0FMDeletionSet *set,
                                    const RL0FMCycle *cycle,
                                    int delete_through_level);
int rl0_fm_load_deletions(RL0FMDeletionSet *set, const char *path);
int rl0_fm_load_completions(RL0ForgetMap *map, const char *path);
int rl0_fm_load_remembrance(RL0ForgetMap *map, const char *path);
int rl0_fm_load_remembrance_filtered(RL0ForgetMap *map,
                                      const char *path,
                                      const RL0FMDeletionSet *deletions,
                                      int delete_through_level);
int rl0_fm_load_completions_with_deletions(RL0ForgetMap *map,
                                           const char *completions_path,
                                           const char *deletions_path,
                                           int delete_through_level);
int rl0_fm_delete_cycle_from_map(RL0ForgetMap *map, const RL0FMCycle *cycle);
int rl0_fm_load_completions_filtered(RL0ForgetMap *map,
                                      const char *path,
                                      const RL0FMDeletionSet *deletions,
                                      int delete_through_level);
int rl0_fm_build_from_cycles(RL0ForgetMap *map);
void rl0_fm_canonicalize_cycle(const RL0FMCycle *in, RL0FMCycle *out);
void rl0_fm_reflect_cycle(const RL0FMCycle *in, RL0FMCycle *out);
int rl0_fm_cycle_equal(const RL0FMCycle *a, const RL0FMCycle *b);
int rl0_fm_arc_equal(const RL0FMArc *a, const RL0FMArc *b);
int rl0_fm_lookup(const RL0ForgetMap *map,
                  const RL0FMArc *key,
                  const RL0FMArc **values,
                  int *value_count);
int rl0_fm_lookup_any_rotation(const RL0ForgetMap *map,
                               const RL0FMArc *key,
                               const RL0FMArc **values,
                               int *value_count,
                               RL0FMArc *matched_key);
int rl0_fm_contains_complete(const RL0ForgetMap *map,
                             const RL0FMCycle *complete);
int rl0_fm_cycle_from_parity_indices(const int *parities,
                                     const int *indices,
                                     int count,
                                     RL0FMCycle *out);

#endif
