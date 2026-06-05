#ifndef POLYFORMTOWN_REPHEX_HAT_H
#define POLYFORMTOWN_REPHEX_HAT_H
#include <stddef.h>
#include "rl7/inflation/inflation7.h"

typedef struct {
    size_t total_cells, supported_cells, false_cells, placed_hats, components;
    size_t unresolved_figures, empty_domains, cycle_conflicts, overlaps;
    size_t seed_fallback_used;
    size_t domain_histogram[61];
} RephexHatStats;

int rephex_hat_render(const MR7Cells *cells, const char *output_svg,
                      const char *output_data, const char *axiom,
                      unsigned level, int tree_palette, unsigned rotation_step,
                      RephexHatStats *stats, char *err, size_t errsz);
#endif
