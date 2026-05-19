#include "rl5/hex5.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAT5_MAX_CELLS 4096
#define SAT5_COPY_BUF 8192

typedef struct {
    int q;
    int r;
} Sat5Cell;

typedef struct {
    const char *model_name;
    const char *input_path;
    const char *cnf_path;
    const char *map_path;
    int record;
    int depth;
    int fixed_center_orientation;
} Options;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s --model basic|super|overlap --record N --boundary-depth D --cnf PATH --map PATH [options]\n"
            "options:\n"
            "  --input PATH                 use explicit hex model data path instead of --model default\n"
            "  --fixed-center-orientation  fix center to the canonical record orientation only\n"
            "  --help                      show this help\n",
            prog);
}

static const char *model_path(const char *name) {
    if (!name || strcmp(name, "basic") == 0) return "data/rl5/hexagons.dat";
    if (strcmp(name, "super") == 0) return "data/rl5/supertile_hexagons.dat";
    if (strcmp(name, "overlap") == 0) return "data/rl5/overlap_supertile_hexagons.dat";
    return NULL;
}

static int parse_args(int argc, char **argv, Options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->model_name = "basic";
    opt->record = 1;
    opt->depth = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) opt->model_name = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) opt->input_path = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) opt->record = atoi(argv[++i]);
        else if (strcmp(argv[i], "--boundary-depth") == 0 && i + 1 < argc) opt->depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) opt->depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cnf") == 0 && i + 1 < argc) opt->cnf_path = argv[++i];
        else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) opt->map_path = argv[++i];
        else if (strcmp(argv[i], "--fixed-center-orientation") == 0) opt->fixed_center_orientation = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]); return 0; }
    }
    if (!opt->input_path) opt->input_path = model_path(opt->model_name);
    if (!opt->input_path) { fprintf(stderr, "unknown model: %s\n", opt->model_name); return 0; }
    if (!opt->cnf_path || !opt->map_path) { usage(argv[0]); return 0; }
    if (opt->record < 1) { fprintf(stderr, "record must be positive\n"); return 0; }
    if (opt->depth < 0) { fprintf(stderr, "boundary depth must be nonnegative\n"); return 0; }
    return 1;
}

static int hex_distance(int q, int r) {
    int s = -q - r;
    int aq = q < 0 ? -q : q;
    int ar = r < 0 ? -r : r;
    int as = s < 0 ? -s : s;
    int m = aq > ar ? aq : ar;
    return m > as ? m : as;
}

static int build_hex_ball(int depth, Sat5Cell *cells, int cap) {
    int n = 0;
    for (int q = -depth; q <= depth; q++) {
        for (int r = -depth; r <= depth; r++) {
            if (hex_distance(q, r) <= depth) {
                if (n >= cap) return -1;
                cells[n++] = (Sat5Cell){q, r};
            }
        }
    }
    return n;
}

static int find_cell(const Sat5Cell *cells, int n, int q, int r) {
    for (int i = 0; i < n; i++) if (cells[i].q == q && cells[i].r == r) return i;
    return -1;
}

static int var_id(int cell, int oriented, int noriented) {
    return 1 + cell * noriented + oriented;
}

static int edge_matches(const Hex5Model *m, const char *a, const char *b) {
    for (int i = 0; i < m->nrules; i++) {
        if (strcmp(m->rule_a[i], a) == 0 && strcmp(m->rule_b[i], b) == 0) return 1;
    }
    return 0;
}

static int emit_clause(FILE *f, long *nclauses, const int *lit, int nlit) {
    for (int i = 0; i < nlit; i++) fprintf(f, "%d ", lit[i]);
    fprintf(f, "0\n");
    (*nclauses)++;
    return ferror(f) ? 0 : 1;
}

static int emit_exactly_one(FILE *f, long *nclauses, int base_var, int count) {
    int *clause = (int *)malloc((size_t)count * sizeof(int));
    if (!clause) return 0;
    for (int i = 0; i < count; i++) clause[i] = base_var + i;
    if (!emit_clause(f, nclauses, clause, count)) { free(clause); return 0; }
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            int lits[2] = {-(base_var + i), -(base_var + j)};
            if (!emit_clause(f, nclauses, lits, 2)) { free(clause); return 0; }
        }
    }
    free(clause);
    return 1;
}

static int center_allowed_orients(const Hex5Model *m,
                                  int record,
                                  int fixed,
                                  int *out,
                                  int cap) {
    int n = 0;
    const Hex5 *base;
    if (record < 1 || record > m->ntiles) return -1;
    base = &m->tiles[record - 1];
    for (int oi = 0; oi < m->noriented; oi++) {
        int ok = 0;
        if (fixed) {
            ok = hex5_equal(&m->oriented[oi], base);
        } else {
            for (int k = 0; k < HEX5_SIDES; k++) {
                Hex5 r;
                hex5_rot_left(base, k, &r);
                if (hex5_equal(&m->oriented[oi], &r)) { ok = 1; break; }
            }
        }
        if (ok) {
            if (n >= cap) return -1;
            out[n++] = oi;
        }
    }
    return n;
}

static int write_map(const Options *opt,
                     const Hex5Model *m,
                     const Sat5Cell *cells,
                     int ncells,
                     int nvars,
                     long nclauses) {
    FILE *f = fopen(opt->map_path, "w");
    if (!f) return 0;
    fprintf(f, "# rl5_sat variable map\n");
    fprintf(f, "model %s\n", opt->model_name ? opt->model_name : "custom");
    fprintf(f, "input %s\n", opt->input_path);
    fprintf(f, "record %d\n", opt->record);
    fprintf(f, "boundary_depth %d\n", opt->depth);
    fprintf(f, "fixed_center_orientation %d\n", opt->fixed_center_orientation);
    fprintf(f, "cells %d\n", ncells);
    fprintf(f, "oriented %d\n", m->noriented);
    fprintf(f, "variables %d\n", nvars);
    fprintf(f, "clauses %ld\n", nclauses);
    fprintf(f, "# var cell q r oriented edges\n");
    for (int ci = 0; ci < ncells; ci++) {
        for (int oi = 0; oi < m->noriented; oi++) {
            int v = var_id(ci, oi, m->noriented);
            fprintf(f, "%d %d %d %d %d", v, ci, cells[ci].q, cells[ci].r, oi);
            for (int d = 0; d < HEX5_SIDES; d++) fprintf(f, " %s", m->oriented[oi].e[d]);
            fprintf(f, "\n");
        }
    }
    return fclose(f) == 0;
}

static int copy_tmp_to_cnf(FILE *body, const char *path, int nvars, long nclauses) {
    FILE *out = fopen(path, "w");
    char buf[SAT5_COPY_BUF];
    size_t n;
    if (!out) return 0;
    fprintf(out, "p cnf %d %ld\n", nvars, nclauses);
    rewind(body);
    while ((n = fread(buf, 1, sizeof(buf), body)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(out); return 0; }
    }
    return fclose(out) == 0;
}

int main(int argc, char **argv) {
    Options opt;
    Hex5Model model;
    Sat5Cell cells[SAT5_MAX_CELLS];
    int ncells;
    int nvars;
    long nclauses = 0;
    FILE *body = NULL;
    int center_cell;
    int center_orients[HEX5_SIDES];
    int center_n;
    const int dq[HEX5_SIDES] = {1, 0, -1, -1, 0, 1};
    const int dr[HEX5_SIDES] = {0, 1, 1, 0, -1, -1};

    if (!parse_args(argc, argv, &opt)) return 1;
    if (!hex5_parse_file(opt.input_path, &model)) {
        fprintf(stderr, "rl5_sat: failed to parse %s\n", opt.input_path);
        return 1;
    }
    if (opt.record > model.ntiles) {
        fprintf(stderr, "rl5_sat: record %d out of range; records=%d\n", opt.record, model.ntiles);
        return 1;
    }
    ncells = build_hex_ball(opt.depth, cells, SAT5_MAX_CELLS);
    if (ncells <= 0) {
        fprintf(stderr, "rl5_sat: failed to build hex ball depth=%d\n", opt.depth);
        return 1;
    }
    center_cell = find_cell(cells, ncells, 0, 0);
    if (center_cell < 0) return 1;
    nvars = ncells * model.noriented;
    body = tmpfile();
    if (!body) {
        fprintf(stderr, "rl5_sat: tmpfile failed: %s\n", strerror(errno));
        return 1;
    }

    for (int ci = 0; ci < ncells; ci++) {
        if (!emit_exactly_one(body, &nclauses, var_id(ci, 0, model.noriented), model.noriented)) return 1;
    }

    center_n = center_allowed_orients(&model,
                                      opt.record,
                                      opt.fixed_center_orientation,
                                      center_orients,
                                      HEX5_SIDES);
    if (center_n <= 0) {
        fprintf(stderr, "rl5_sat: no oriented center choices for record %d\n", opt.record);
        return 1;
    }
    {
        int lits[HEX5_SIDES];
        for (int i = 0; i < center_n; i++) lits[i] = var_id(center_cell, center_orients[i], model.noriented);
        if (!emit_clause(body, &nclauses, lits, center_n)) return 1;
    }

    for (int ci = 0; ci < ncells; ci++) {
        for (int d = 0; d < HEX5_SIDES; d++) {
            int ni = find_cell(cells, ncells, cells[ci].q + dq[d], cells[ci].r + dr[d]);
            int od = (d + 3) % HEX5_SIDES;
            if (ni < 0 || ni <= ci) continue;
            for (int oi = 0; oi < model.noriented; oi++) {
                for (int oj = 0; oj < model.noriented; oj++) {
                    if (!edge_matches(&model, model.oriented[oi].e[d], model.oriented[oj].e[od])) {
                        int lits[2] = {-var_id(ci, oi, model.noriented), -var_id(ni, oj, model.noriented)};
                        if (!emit_clause(body, &nclauses, lits, 2)) return 1;
                    }
                }
            }
        }
    }

    if (!copy_tmp_to_cnf(body, opt.cnf_path, nvars, nclauses)) {
        fprintf(stderr, "rl5_sat: failed to write %s\n", opt.cnf_path);
        return 1;
    }
    fclose(body);
    body = NULL;
    if (!write_map(&opt, &model, cells, ncells, nvars, nclauses)) {
        fprintf(stderr, "rl5_sat: failed to write %s\n", opt.map_path);
        return 1;
    }

    fprintf(stderr,
            "rl5_sat model=%s input=%s record=%d depth=%d cells=%d oriented=%d vars=%d clauses=%ld cnf=%s map=%s\n",
            opt.model_name,
            opt.input_path,
            opt.record,
            opt.depth,
            ncells,
            model.noriented,
            nvars,
            nclauses,
            opt.cnf_path,
            opt.map_path);
    return 0;
}
