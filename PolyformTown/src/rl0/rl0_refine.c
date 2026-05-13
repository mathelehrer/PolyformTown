#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/hash.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl0/boundary0.h"
#include "rl0/forget_map.h"

#define SEARCH0_MAX_RECORDS 256
#define SEARCH0_MAX_STATES 200000
#define SEARCH0_MAX_PROV 256
#define SEARCH0_MAX_COORDS (MAX_VERTS * MAX_CYCLES)
#define SEARCH0_MAX_KEY 8192
#define SEARCH0_INF 1000000000
#define SEARCH0_DEFAULT_TILE "preferences/focus.tile"
#define SEARCH0_DEFAULT_COMPLETIONS "data/rl0/completions.dat"
#define SEARCH0_DEFAULT_REMEMBRANCE "data/rl0/remembrance.dat"
#define SEARCH0_DEFAULT_DELETIONS "data/rl0/deletions.dat"
#define SEARCH0_DEFAULT_OPTIMIZE "preferences/optimize.dat"
#define SEARCH0_DEFAULT_HIDDEN_BOUND 256



typedef enum {
    S0C_OK = 0,
    S0C_OK_CARRY,
    S0C_OK_NO_PORT,
    S0C_PRUNE_ATTACH,
    S0C_PRUNE_CLOSURE,
    S0C_ESCAPE_HIDDEN_BOUND,
    S0C_ESCAPE_TILE_BOUND,
    S0C_ESCAPE_BOUNDARY_BOUND,
    S0C_ESCAPE_CYCLE_BOUND,
    S0C_ESCAPE_INTERNAL_BOUND,
    S0C_ESCAPE_CLOSURE_STEPS,
    S0C_ESCAPE_GLOBAL_STEPS,
    S0C_ERROR_CHOOSE_PORT,
    S0C_ERROR_STATE_LIMIT,
    S0C_COUNT
} Search0Code;

typedef enum {
    S0K_OK = 0,
    S0K_PRUNE,
    S0K_ESCAPE,
    S0K_ERROR
} Search0Kind;

typedef struct {
    Search0Code code;
    const char *name;
    Search0Kind kind;
} Search0CodeInfo;

static const Search0CodeInfo search0_code_table[S0C_COUNT] = {
    {S0C_OK, "ok", S0K_OK},
    {S0C_OK_CARRY, "ok:carry", S0K_OK},
    {S0C_OK_NO_PORT, "ok:no-port", S0K_OK},
    {S0C_PRUNE_ATTACH, "prune:attach", S0K_PRUNE},
    {S0C_PRUNE_CLOSURE, "prune:closure", S0K_PRUNE},
    {S0C_ESCAPE_HIDDEN_BOUND, "escape:hidden-bound", S0K_ESCAPE},
    {S0C_ESCAPE_TILE_BOUND, "escape:tile-bound", S0K_ESCAPE},
    {S0C_ESCAPE_BOUNDARY_BOUND, "escape:boundary-bound", S0K_ESCAPE},
    {S0C_ESCAPE_CYCLE_BOUND, "escape:cycle-bound", S0K_ESCAPE},
    {S0C_ESCAPE_INTERNAL_BOUND, "escape:internal-bound", S0K_ESCAPE},
    {S0C_ESCAPE_CLOSURE_STEPS, "escape:closure-steps", S0K_ESCAPE},
    {S0C_ESCAPE_GLOBAL_STEPS, "escape:global-steps", S0K_ESCAPE},
    {S0C_ERROR_CHOOSE_PORT, "error:choose-port", S0K_ERROR},
    {S0C_ERROR_STATE_LIMIT, "error:state-limit", S0K_ERROR}
};

typedef struct {
    int by_code[S0C_COUNT];
} Search0CodeCounts;


static const char *search0_code_name(Search0Code code) {
    if (code < 0 || code >= S0C_COUNT) return "error:bad-code";
    return search0_code_table[code].name;
}

static void search0_counts_init(Search0CodeCounts *counts) {
    if (counts) memset(counts, 0, sizeof(*counts));
}

static void search0_counts_add(Search0CodeCounts *counts, Search0Code code) {
    if (!counts || code < 0 || code >= S0C_COUNT) return;
    counts->by_code[code]++;
}

static int search0_counts_kind(const Search0CodeCounts *counts, Search0Kind kind) {
    int total = 0;
    if (!counts) return 0;
    for (int i = 0; i < S0C_COUNT; i++) {
        if (search0_code_table[i].kind == kind) total += counts->by_code[i];
    }
    return total;
}

static void search0_counts_print_detail(FILE *fp, const Search0CodeCounts *counts) {
    int first = 1;
    if (!fp || !counts) return;
    for (int i = 0; i < S0C_COUNT; i++) {
        if (counts->by_code[i] == 0) continue;
        fprintf(fp, "%s%s=%d", first ? "" : " ", search0_code_table[i].name, counts->by_code[i]);
        first = 0;
    }
    if (first) fprintf(fp, "-");
}

static const char *search0_note_code0(Search0Code code) {
    switch (code) {
    case S0C_ESCAPE_HIDDEN_BOUND: return "hidden";
    case S0C_ESCAPE_TILE_BOUND: return "tile";
    case S0C_ESCAPE_BOUNDARY_BOUND: return "boundary";
    case S0C_ESCAPE_CYCLE_BOUND: return "cycle";
    case S0C_ESCAPE_INTERNAL_BOUND: return "internal";
    case S0C_ESCAPE_CLOSURE_STEPS: return "closure-steps";
    case S0C_ESCAPE_GLOBAL_STEPS: return "global-steps";
    case S0C_ERROR_CHOOSE_PORT: return "err:choose-port";
    case S0C_ERROR_STATE_LIMIT: return "err:state-limit";
    default: return NULL;
    }
}

static void search0_counts_print_note(FILE *fp,
                                      const Search0CodeCounts *branch_counts,
                                      const Search0CodeCounts *checkpoint_counts) {
    int first = 1;
    if (!fp) return;
    for (int i = 0; i < S0C_COUNT; i++) {
        int n = 0;
        const char *code;
        if (branch_counts) n += branch_counts->by_code[i];
        if (checkpoint_counts) n += checkpoint_counts->by_code[i];
        if (n == 0) continue;
        if (search0_code_table[i].kind != S0K_ESCAPE &&
            search0_code_table[i].kind != S0K_ERROR) continue;
        code = search0_note_code0((Search0Code)i);
        if (!code) continue;
        fprintf(fp, "%s%s", first ? "" : " ", code);
        first = 0;
    }
    if (first) fprintf(fp, "-");
}


static Search0Code search0_code_from_attach_status0(AttachStatus status,
                                                    Search0Code geometry_code) {
    switch (status) {
    case ATTACH_STATUS_OK:
        return S0C_OK;
    case ATTACH_STATUS_BOUNDARY_BOUND:
        return S0C_ESCAPE_BOUNDARY_BOUND;
    case ATTACH_STATUS_CYCLE_BOUND:
        return S0C_ESCAPE_CYCLE_BOUND;
    case ATTACH_STATUS_INTERNAL_BOUND:
        return S0C_ESCAPE_INTERNAL_BOUND;
    case ATTACH_STATUS_GEOMETRY:
    default:
        return geometry_code;
    }
}

typedef struct {
    int file_no;
    int have_boundary;
    Poly boundary;
    int have_hidden;
    Coord hidden[SEARCH0_MAX_COORDS];
    int hidden_count;
    int have_tiles;
    Cycle tiles[ATTACH0_MAX_TILES];
    int tile_count;
    int have_pi;
    int parities[RL0_FM_MAX_ITEMS];
    int indices[RL0_FM_MAX_ITEMS];
    int pi_count;
    RL0FMCycle full_cycle;
} Search0Record;

typedef struct {
    Poly poly;
    Cycle tiles[ATTACH0_MAX_TILES];
    int tile_count;
    Coord initial_hidden[SEARCH0_MAX_COORDS];
    int initial_hidden_count;
    Coord hidden[SEARCH0_MAX_COORDS];
    int hidden_count;
    unsigned char prov[SEARCH0_MAX_PROV];
    char key[SEARCH0_MAX_KEY];
} Search0State;

typedef struct {
    Search0State *data;
    int count;
    int cap;
} StateVec;

typedef struct {
    int distance;
    Coord q;
    RL0FMArc key;
    const RL0FMArc *values;
    int value_count;
    int nonempty_count;
} PortChoice;

typedef struct {
    int id;
    int discover_step;
    int division;
    char key[SEARCH0_MAX_KEY];
    char display_key[SEARCH0_MAX_KEY];
} PrintNodeRef;

typedef struct {
    FILE *fp;
    int next_id;
    int limit;
    int sublevel;
    PrintNodeRef *nodes;
    int node_count;
    int node_cap;
    int last_node_for_prov[SEARCH0_MAX_PROV];
} PrintCtx;

static void emit_transition0(PrintCtx *print,
                             const Tile *tile,
                             const RL0ForgetMap *map,
                             const Search0State *src,
                             const Search0State *dst,
                             int step,
                             int graph_level,
                             const PortChoice *choice,
                             const RL0FMArc *arc,
                             Search0Code code);

static void emit_relation0(PrintCtx *print,
                           const Tile *tile,
                           const RL0ForgetMap *map,
                           const Search0State *src,
                           const Search0State *dst,
                           int step,
                           int graph_level,
                           const char *dst_kind,
                           const char *edge_kind);

static void emit_focus0(PrintCtx *print,
                        int node_id,
                        const Search0State *src,
                        int step,
                        Coord port);

static void emit_graph_node_update0(PrintCtx *print,
                                   int id,
                                   int discover_step,
                                   int node_division);

static void emit_graph_node_mark0(PrintCtx *print,
                                  int id,
                                  const char *mark,
                                  Search0Code code);

static int ensure_graph_node0(PrintCtx *print,
                              const Tile *tile,
                              const Search0State *s,
                              const char *kind,
                              int discover_step,
                              int node_division,
                              int *is_new);

static int graph_division_from_distance0(int distance) {
    if (distance <= 0 || distance >= SEARCH0_INF) return 0;
    return distance - 1;
}

static int graph_division_for_state0(const Search0State *s,
                                     const Tile *tile,
                                     const RL0ForgetMap *map,
                                     int fallback);

static int singleton_prov0(const Search0State *s) {
    int found = -1;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (!s->prov[p]) continue;
        if (found >= 0) return -1;
        found = p;
    }
    return found;
}

static int parse_optimize_records0(const char *path,
                                   int *out,
                                   int cap,
                                   int *out_count) {
    FILE *fp;
    char line[256];
    int count = 0;
    if (!path || !out || cap <= 0 || !out_count) return 0;
    fp = fopen(path, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        const char *p = line;
        int v = 0;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;
        if (sscanf(p, "%d", &v) != 1) continue;
        if (count >= cap) break;
        out[count++] = v;
    }
    fclose(fp);
    *out_count = count;
    return 1;
}

static int optimized_record0(int file_no,
                             const int *opt_ids,
                             int opt_count) {
    for (int i = 0; i < opt_count; i++) {
        if (opt_ids[i] == file_no) return 1;
    }
    return 0;
}


static int parse_record_selection_token0(const char *text,
                                         unsigned char *selected,
                                         int selected_cap,
                                         int *select_all) {
    const char *p = text;
    if (!text || !selected || !select_all) return 0;
    if (strcmp(text, "All") == 0 || strcmp(text, "all") == 0 || strcmp(text, "ALL") == 0) {
        *select_all = 1;
        return 1;
    }
    while (*p) {
        char *end = NULL;
        long v;
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (!*p) break;
        v = strtol(p, &end, 10);
        if (end == p) return 0;
        if (v >= 0 && v < selected_cap) selected[v] = 1;
        p = end;
        while (*p && *p != ',') {
            if (!isspace((unsigned char)*p)) return 0;
            p++;
        }
    }
    return 1;
}

static int selection_contains0(int file_no,
                               const unsigned char *selected,
                               int select_all) {
    if (select_all) return 1;
    if (file_no < 0 || file_no >= SEARCH0_MAX_PROV) return 0;
    return selected[file_no] != 0;
}

static void skip_ws(const char **pp) {
    while (isspace((unsigned char)**pp)) (*pp)++;
}

static int parse_int0(const char **pp, int *out) {
    char *end = NULL;
    long v;
    skip_ws(pp);
    v = strtol(*pp, &end, 10);
    if (end == *pp) return 0;
    *out = (int)v;
    *pp = end;
    return 1;
}

static int expect_char0(const char **pp, char ch) {
    skip_ws(pp);
    if (**pp != ch) return 0;
    (*pp)++;
    return 1;
}

static int parse_cycle0(const char **pp, Cycle *cycle) {
    cycle->n = 0;
    if (!expect_char0(pp, '[')) return 0;
    skip_ws(pp);
    if (**pp == ']') { (*pp)++; return 1; }
    while (cycle->n < MAX_VERTS) {
        Coord q;
        if (!expect_char0(pp, '(')) return 0;
        if (!parse_int0(pp, &q.v)) return 0;
        if (!expect_char0(pp, ',')) return 0;
        if (!parse_int0(pp, &q.x)) return 0;
        if (!expect_char0(pp, ',')) return 0;
        if (!parse_int0(pp, &q.y)) return 0;
        if (!expect_char0(pp, ')')) return 0;
        cycle->v[cycle->n++] = q;
        skip_ws(pp);
        if (**pp == ',') { (*pp)++; continue; }
        if (**pp == ']') { (*pp)++; return cycle->n > 0; }
        return 0;
    }
    return 0;
}

static int parse_poly0(const char *text, Poly *poly) {
    const char *p = text;
    poly->cycle_count = 0;
    if (!expect_char0(&p, '[')) return 0;
    while (poly->cycle_count < MAX_CYCLES) {
        if (!parse_cycle0(&p, &poly->cycles[poly->cycle_count])) return 0;
        poly->cycle_count++;
        skip_ws(&p);
        if (*p == '|') { p++; continue; }
        if (*p == ']') { p++; skip_ws(&p); return *p == '\0' || *p == '\n' || *p == '\r'; }
        return 0;
    }
    return 0;
}

static int parse_tile_list0(const char *text, Cycle *out, int *out_count) {
    const char *p = text;
    int count = 0;
    if (!expect_char0(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') { p++; *out_count = 0; return 1; }
    while (count < ATTACH0_MAX_TILES) {
        if (!parse_cycle0(&p, &out[count])) return 0;
        count++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; *out_count = count; return 1; }
        return 0;
    }
    return 0;
}

static int parse_coord_list0(const char *text, Coord *out, int *out_count) {
    const char *p = text;
    int count = 0;
    if (!expect_char0(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') { p++; *out_count = 0; return 1; }
    while (count < SEARCH0_MAX_COORDS) {
        if (!expect_char0(&p, '(')) return 0;
        if (!parse_int0(&p, &out[count].v)) return 0;
        if (!expect_char0(&p, ',')) return 0;
        if (!parse_int0(&p, &out[count].x)) return 0;
        if (!expect_char0(&p, ',')) return 0;
        if (!parse_int0(&p, &out[count].y)) return 0;
        if (!expect_char0(&p, ')')) return 0;
        count++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; *out_count = count; return 1; }
        return 0;
    }
    return 0;
}

static int parse_int_list0(const char *text, int *out, int *out_count) {
    const char *p = text;
    int count = 0;
    if (!expect_char0(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') { p++; *out_count = 0; return 1; }
    while (count < RL0_FM_MAX_ITEMS) {
        if (!parse_int0(&p, &out[count])) return 0;
        count++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; *out_count = count; return 1; }
        return 0;
    }
    return 0;
}

static void record_reset0(Search0Record *r) {
    memset(r, 0, sizeof(*r));
}

static int parse_records0(const char *path, Search0Record *records, int *record_count) {
    FILE *fp = fopen(path, "r");
    char line[262144];
    Search0Record rec;
    int count = 0;
    int in_record = 0;
    if (!fp) return 0;
    record_reset0(&rec);
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "---[", 4) == 0) {
            if (in_record) {
                if (count >= SEARCH0_MAX_RECORDS) { fclose(fp); return 0; }
                if (rec.have_pi) {
                    if (!rl0_fm_cycle_from_parity_indices(rec.parities, rec.indices, rec.pi_count, &rec.full_cycle)) {
                        fclose(fp); return 0;
                    }
                    rl0_fm_canonicalize_cycle(&rec.full_cycle, &rec.full_cycle);
                }
                if (rec.have_boundary || rec.have_hidden || rec.have_tiles || rec.have_pi) {
                    records[count++] = rec;
                }
            }
            record_reset0(&rec);
            in_record = 1;
            sscanf(line, "---[%d]---", &rec.file_no);
            continue;
        }
        if (strncmp(line, "boundary:", 9) == 0) {
            rec.have_boundary = parse_poly0(line + 9, &rec.boundary);
        } else if (strncmp(line, "constellation:", 14) == 0) {
            rec.have_hidden = parse_coord_list0(line + 14, rec.hidden, &rec.hidden_count);
        } else if (strncmp(line, "tiles:", 6) == 0) {
            rec.have_tiles = parse_tile_list0(line + 6, rec.tiles, &rec.tile_count);
        } else if (strncmp(line, "parities:", 9) == 0) {
            int pc = 0;
            rec.have_pi = parse_int_list0(line + 9, rec.parities, &pc);
            rec.pi_count = pc;
        } else if (strncmp(line, "indices:", 8) == 0) {
            int ic = 0;
            if (!parse_int_list0(line + 8, rec.indices, &ic)) rec.have_pi = 0;
            if (rec.pi_count != ic) rec.have_pi = 0;
        }
    }
    if (in_record) {
        if (count >= SEARCH0_MAX_RECORDS) { fclose(fp); return 0; }
        if (rec.have_pi) {
            if (!rl0_fm_cycle_from_parity_indices(rec.parities, rec.indices, rec.pi_count, &rec.full_cycle)) {
                fclose(fp); return 0;
            }
            rl0_fm_canonicalize_cycle(&rec.full_cycle, &rec.full_cycle);
        }
        if (rec.have_boundary || rec.have_hidden || rec.have_tiles || rec.have_pi) {
            records[count++] = rec;
        }
    }
    fclose(fp);
    *record_count = count;
    return 1;
}

static int coord_in_list0(const Coord *coords, int count, Coord q) {
    for (int i = 0; i < count; i++) if (coord_eq(coords[i], q)) return 1;
    return 0;
}

static int coord_cmp0(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int edge_in_tiles0(const Cycle *tiles, int tile_count, Coord a, Coord b) {
    for (int t = 0; t < tile_count; t++) {
        const Cycle *c = &tiles[t];
        for (int i = 0; i < c->n; i++) {
            Coord u = c->v[i];
            Coord v = c->v[(i + 1) % c->n];
            if ((coord_eq(u, a) && coord_eq(v, b)) ||
                (coord_eq(u, b) && coord_eq(v, a))) return 1;
        }
    }
    return 0;
}

static int collect_tile_vertices0(const Cycle *tiles, int tile_count, Coord *verts) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (!coord_in_list0(verts, n, tiles[t].v[i])) {
                if (n >= SEARCH0_MAX_COORDS) return -1;
                verts[n++] = tiles[t].v[i];
            }
        }
    }
    return n;
}

static int rebuild_hidden0(const Poly *p, const Cycle *tiles, int tile_count, Coord *hidden) {
    Coord all[SEARCH0_MAX_COORDS];
    Coord boundary[SEARCH0_MAX_COORDS];
    int ac = collect_tile_vertices0(tiles, tile_count, all);
    int bc = build_boundary_vertices(p, boundary);
    int hc = 0;
    if (ac < 0 || bc < 0) return -1;
    for (int i = 0; i < ac; i++) {
        if (!coord_in_list0(boundary, bc, all[i])) {
            if (hc >= SEARCH0_MAX_COORDS) return -1;
            hidden[hc++] = all[i];
        }
    }
    qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp0);
    return hc;
}

static int compute_distances0(const Cycle *tiles,
                              int tile_count,
                              const Coord *sources,
                              int source_count,
                              Coord *verts,
                              int *dist,
                              int *vert_count) {
    int n = collect_tile_vertices0(tiles, tile_count, verts);
    int queue[SEARCH0_MAX_COORDS];
    int qh = 0, qt = 0;
    if (n < 0) return 0;
    for (int i = 0; i < n; i++) dist[i] = SEARCH0_INF;
    for (int s = 0; s < source_count; s++) {
        for (int i = 0; i < n; i++) {
            if (coord_eq(verts[i], sources[s]) && dist[i] > 0) {
                dist[i] = 0;
                queue[qt++] = i;
            }
        }
    }
    while (qh < qt) {
        int cur = queue[qh++];
        for (int j = 0; j < n; j++) {
            if (dist[j] != SEARCH0_INF) continue;
            if (!edge_in_tiles0(tiles, tile_count, verts[cur], verts[j])) continue;
            dist[j] = dist[cur] + 1;
            queue[qt++] = j;
        }
    }
    *vert_count = n;
    return 1;
}

static int find_dist0(Coord q, const Coord *verts, const int *dist, int count) {
    for (int i = 0; i < count; i++) if (coord_eq(q, verts[i])) return dist[i];
    return SEARCH0_INF;
}

static int is_port0(Coord q, const Coord *hidden, int hidden_count, const Cycle *tiles, int tile_count) {
    for (int h = 0; h < hidden_count; h++) {
        if (edge_in_tiles0(tiles, tile_count, q, hidden[h])) return 1;
    }
    return 0;
}

static int choose_port0(const Search0State *s,
                        const Tile *tile,
                        const RL0ForgetMap *map,
                        PortChoice *choice,
                        int *port_count,
                        int *min_distance_out) {
    Coord boundary[SEARCH0_MAX_COORDS];
    Coord verts[SEARCH0_MAX_COORDS];
    int dist[SEARCH0_MAX_COORDS];
    int vc, gc;
    int best = 0;
    int pc = 0;
    PortChoice best_choice;
    best_choice.distance = SEARCH0_INF;
    best_choice.value_count = 0;
    best_choice.nonempty_count = 0;
    if (!compute_distances0(s->tiles, s->tile_count, s->initial_hidden, s->initial_hidden_count, verts, dist, &gc)) return 0;
    vc = build_boundary_vertices(&s->poly, boundary);
    if (vc < 0) return 0;
    for (int i = 0; i < vc; i++) {
        RL0FMArc key;
        const RL0FMArc *values = NULL;
        int value_count = 0;
        int nonempty = 0;
        int d;
        if (!is_port0(boundary[i], s->hidden, s->hidden_count, s->tiles, s->tile_count)) continue;
        d = find_dist0(boundary[i], verts, dist, gc);
        if (d == SEARCH0_INF) continue;
        pc++;
        if (!boundary0_build_vertex_arc(tile, s->tiles, s->tile_count, boundary[i], &key) ||
            !rl0_fm_lookup_any_rotation(map, &key, &values, &value_count, NULL) ||
            value_count <= 0) {
            best_choice.distance = d;
            best_choice.q = boundary[i];
            best_choice.key = key;
            best_choice.values = NULL;
            best_choice.value_count = 0;
            best_choice.nonempty_count = 0;
            *choice = best_choice;
            *port_count = pc;
            *min_distance_out = d;
            return -1;
        }
        for (int k = 0; k < value_count; k++) if (values[k].n > 0) nonempty++;
        if (!best || d < best_choice.distance ||
            (d == best_choice.distance && value_count < best_choice.value_count)) {
            best = 1;
            best_choice.distance = d;
            best_choice.q = boundary[i];
            best_choice.key = key;
            best_choice.values = values;
            best_choice.value_count = value_count;
            best_choice.nonempty_count = nonempty;
        }
    }
    if (port_count) *port_count = pc;
    if (min_distance_out) *min_distance_out = best ? best_choice.distance : SEARCH0_INF;
    if (!best) return 0;
    *choice = best_choice;
    return 1;
}

static int graph_division_for_state0(const Search0State *s,
                                     const Tile *tile,
                                     const RL0ForgetMap *map,
                                     int fallback) {
    PortChoice ch;
    int pc = 0;
    int d = SEARCH0_INF;
    int r;
    if (!s || !tile || !map) return fallback;
    r = choose_port0(s, tile, map, &ch, &pc, &d);
    if (r == 0 || d <= 0 || d >= SEARCH0_INF) return fallback;
    return graph_division_from_distance0(d);
}

static void statevec_init0(StateVec *v) { v->data = NULL; v->count = 0; v->cap = 0; }
static void statevec_free0(StateVec *v) { free(v->data); v->data = NULL; v->count = v->cap = 0; }

static void poly_to_key0(const Poly *p, char *buf, size_t cap) {
    size_t off = 0;
    Poly key;
    poly_hash_key_lattice(p, TILE_LATTICE_TETRILLE, &key);
    off += (size_t)snprintf(buf + off, cap - off, "[");
    for (int c = 0; c < key.cycle_count && off < cap; c++) {
        if (c) off += (size_t)snprintf(buf + off, cap - off, "|");
        off += (size_t)snprintf(buf + off, cap - off, "[");
        for (int i = 0; i < key.cycles[c].n && off < cap; i++) {
            if (i) off += (size_t)snprintf(buf + off, cap - off, ",");
            off += (size_t)snprintf(buf + off, cap - off, "(%d,%d,%d)", key.cycles[c].v[i].v, key.cycles[c].v[i].x, key.cycles[c].v[i].y);
        }
        off += (size_t)snprintf(buf + off, cap - off, "]");
    }
    snprintf(buf + off, cap - off, "]");
}

static int statevec_add_merge0(StateVec *v, const Search0State *s) {
    for (int i = 0; i < v->count; i++) {
        if (strcmp(v->data[i].key, s->key) == 0) {
            for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
                if (s->prov[p]) v->data[i].prov[p] = 1;
            }
            return 1;
        }
    }
    if (v->count == v->cap) {
        int nc = v->cap ? 2 * v->cap : 256;
        Search0State *nd = realloc(v->data, (size_t)nc * sizeof(*nd));
        if (!nd) return 0;
        v->data = nd;
        v->cap = nc;
    }
    v->data[v->count++] = *s;
    return 1;
}

static int make_seed0(const Search0Record *r, Search0State *s) {
    memset(s, 0, sizeof(*s));
    s->poly = r->boundary;
    s->tile_count = r->tile_count;
    for (int i = 0; i < r->tile_count; i++) s->tiles[i] = r->tiles[i];
    s->initial_hidden_count = r->hidden_count;
    for (int i = 0; i < r->hidden_count; i++) s->initial_hidden[i] = r->hidden[i];
    s->hidden_count = rebuild_hidden0(&s->poly, s->tiles, s->tile_count, s->hidden);
    if (s->hidden_count < 0) return 0;
    if (r->file_no >= 0 && r->file_no < SEARCH0_MAX_PROV) s->prov[r->file_no] = 1;
    poly_to_key0(&s->poly, s->key, sizeof(s->key));
    return 1;
}

static int force_live_closure_bounded0(Poly *poly,
                                       const Tile *tile,
                                       Cycle *tiles,
                                       int *tile_count,
                                       const RL0ForgetMap *map,
                                       int max_steps,
                                       int hidden_bound,
                                       Coord *hidden,
                                       int *hidden_count_out,
                                       Attach0Stats *astats,
                                       Attach0ClosureStats *cstats,
                                       Search0Code *code_out) {
    Attach0ClosureStats local_cstats;
    Attach0ClosureStats *use_cstats = cstats;
    int hidden_count;
    if (code_out) *code_out = S0C_OK;
    if (!poly || !tile || !tiles || !tile_count || !map || !hidden) {
        if (code_out) *code_out = S0C_ESCAPE_INTERNAL_BOUND;
        return 0;
    }
    if (!use_cstats) {
        attach0_closure_stats_init(&local_cstats);
        use_cstats = &local_cstats;
    }
    if (max_steps <= 0) max_steps = 1024;
    for (int step = 0; step < max_steps; step++) {
        int before_steps = use_cstats->closure_steps;
        AttachStatus closure_status = ATTACH_STATUS_GEOMETRY;
        int ok = attach0_force_live_closure_status(poly,
                                                   tile,
                                                   tiles,
                                                   tile_count,
                                                   map,
                                                   1,
                                                   astats,
                                                   use_cstats,
                                                   &closure_status);
        hidden_count = rebuild_hidden0(poly, tiles, *tile_count, hidden);
        if (hidden_count < 0) {
            if (code_out) *code_out = S0C_ESCAPE_INTERNAL_BOUND;
            return -1;
        }
        if (hidden_count_out) *hidden_count_out = hidden_count;
        if (hidden_count > hidden_bound) {
            if (code_out) *code_out = S0C_ESCAPE_HIDDEN_BOUND;
            return -1;
        }
        if (ok) {
            if (code_out) *code_out = S0C_OK;
            return 1;
        }
        if (use_cstats->closure_steps <= before_steps) {
            if (code_out) {
                *code_out = search0_code_from_attach_status0(closure_status,
                                                             S0C_PRUNE_CLOSURE);
            }
            if (closure_status == ATTACH_STATUS_BOUNDARY_BOUND ||
                closure_status == ATTACH_STATUS_CYCLE_BOUND ||
                closure_status == ATTACH_STATUS_INTERNAL_BOUND) {
                return -1;
            }
            return 0;
        }
    }
    if (code_out) *code_out = S0C_ESCAPE_CLOSURE_STEPS;
    return -1;
}

static int state_after_attach0(const Search0State *s,
                               const Tile *tile,
                               const RL0ForgetMap *map,
                               const PortChoice *choice,
                               int value_index,
                               int hidden_bound,
                               Search0State *out,
                               Attach0Stats *astats,
                               Attach0ClosureStats *cstats,
                               Search0Code *code_out) {
    int hidden_count;
    Cycle grown_tiles[ATTACH0_MAX_TILES];
    int grown_tile_count = 0;

    if (code_out) *code_out = S0C_OK;
    *out = *s;

    if (s->tile_count + choice->values[value_index].n > ATTACH0_MAX_TILES) {
        if (code_out) *code_out = S0C_ESCAPE_TILE_BOUND;
        return -1;
    }

    {
        AttachStatus attach_status = ATTACH_STATUS_GEOMETRY;
        if (!attach0_try_attach_arc_status(&s->poly,
                                           tile,
                                           s->tiles,
                                           s->tile_count,
                                           choice->q,
                                           &choice->values[value_index],
                                           &out->poly,
                                           grown_tiles,
                                           &grown_tile_count,
                                           astats,
                                           &attach_status)) {
            if (code_out) {
                *code_out = search0_code_from_attach_status0(attach_status,
                                                             S0C_PRUNE_ATTACH);
            }
            if (attach_status == ATTACH_STATUS_BOUNDARY_BOUND ||
                attach_status == ATTACH_STATUS_CYCLE_BOUND ||
                attach_status == ATTACH_STATUS_INTERNAL_BOUND) {
                return -1;
            }
            return 0;
        }
    }
    if (grown_tile_count < 0 || grown_tile_count > ATTACH0_MAX_TILES) {
        if (code_out) *code_out = S0C_ESCAPE_TILE_BOUND;
        return -1;
    }
    out->tile_count = grown_tile_count;
    for (int i = 0; i < grown_tile_count; i++) out->tiles[i] = grown_tiles[i];

    hidden_count = rebuild_hidden0(&out->poly, out->tiles, out->tile_count, out->hidden);
    if (hidden_count < 0) {
        if (code_out) *code_out = S0C_ESCAPE_INTERNAL_BOUND;
        return -1;
    }
    out->hidden_count = hidden_count;
    if (out->hidden_count > hidden_bound) {
        if (code_out) *code_out = S0C_ESCAPE_HIDDEN_BOUND;
        return -1;
    }

    /* Force only after the requested arc has completed. */
    {
        int cr = force_live_closure_bounded0(&out->poly,
                                             tile,
                                             out->tiles,
                                             &out->tile_count,
                                             map,
                                             1024,
                                             hidden_bound,
                                             out->hidden,
                                             &hidden_count,
                                             astats,
                                             cstats,
                                             code_out);
        if (cr < 0) return -1;
        if (cr == 0) return 0;
    }
    out->hidden_count = hidden_count;
    poly_to_key0(&out->poly, out->key, sizeof(out->key));
    if (code_out) *code_out = S0C_OK;
    return 1;
}

static int count_prov0(const StateVec *v, unsigned char *present) {
    int count = 0;
    memset(present, 0, SEARCH0_MAX_PROV);
    for (int i = 0; i < v->count; i++) {
        for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
            if (v->data[i].prov[p] && !present[p]) {
                present[p] = 1;
                count++;
            }
        }
    }
    return count;
}

static void add_state_prov0(unsigned char *dst, const Search0State *s) {
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (s->prov[p]) dst[p] = 1;
    }
}

static void union_prov0(unsigned char *dst, const unsigned char *src) {
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (src[p]) dst[p] = 1;
    }
}

static void make_display_dead0(const unsigned char *proven_dead,
                               const unsigned char *terminal_dead,
                               unsigned char *display_dead) {
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        display_dead[p] = (unsigned char)((proven_dead[p] || terminal_dead[p]) ? 1 : 0);
    }
}

static int add_terminal_state_prov0(unsigned char *terminal_dead,
                                    const Search0State *s) {
    int added = 0;
    if (!terminal_dead || !s) return 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (!s->prov[p] || terminal_dead[p]) continue;
        terminal_dead[p] = 1;
        added++;
    }
    return added;
}

static int strip_state_prov0(Search0State *s, const unsigned char *remove) {
    int keep = 0;
    if (!s || !remove) return s ? 1 : 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (remove[p]) s->prov[p] = 0;
        if (s->prov[p]) keep = 1;
    }
    return keep;
}

static int filter_statevec_prov0(StateVec *v, const unsigned char *remove) {
    StateVec next;
    if (!v || !remove) return 1;
    statevec_init0(&next);
    for (int i = 0; i < v->count; i++) {
        Search0State s = v->data[i];
        if (!strip_state_prov0(&s, remove)) continue;
        if (!statevec_add_merge0(&next, &s)) {
            statevec_free0(&next);
            return 0;
        }
    }
    statevec_free0(v);
    *v = next;
    return 1;
}

static int count_marks0(const unsigned char *present) {
    int count = 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (present[p]) count++;
    }
    return count;
}

static int count_prov_with_escaped0(const StateVec *v, const unsigned char *escaped, unsigned char *present) {
    int count;
    count_prov0(v, present);
    union_prov0(present, escaped);
    count = count_marks0(present);
    return count;
}

static int count_unknown0(const unsigned char *initial,
                          const unsigned char *escaped,
                          const unsigned char *dead) {
    int count = 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (initial[p] && !escaped[p] && !dead[p]) count++;
    }
    return count;
}

static int count_initial_present0(const unsigned char *initial,
                                  const unsigned char *present) {
    int count = 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (initial[p] && present[p]) count++;
    }
    return count;
}

static int count_initial_removed0(const unsigned char *initial,
                                  const unsigned char *escaped,
                                  const unsigned char *dead) {
    int count = 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (initial[p] && dead[p] && !escaped[p]) count++;
    }
    return count;
}

static const RL0FMCycle *record_cycle_by_no0(const Search0Record *records,
                                             int record_count,
                                             int file_no) {
    for (int i = 0; i < record_count; i++) {
        if (records[i].file_no == file_no && records[i].have_pi) {
            return &records[i].full_cycle;
        }
    }
    return NULL;
}

static int rebuild_map0(RL0ForgetMap *map,
                        const char *remembrance_path,
                        const RL0FMDeletionSet *deletions,
                        int delete_through_level) {
    rl0_fm_clear(map);
    return rl0_fm_load_remembrance_filtered(map,
                                            remembrance_path,
                                            deletions,
                                            delete_through_level);
}

static int add_new_dead0(const unsigned char *initial,
                         const unsigned char *active,
                         const unsigned char *escaped,
                         unsigned char *dead,
                         int *death_level,
                         const Search0Record *records,
                         int record_count,
                         RL0FMDeletionSet *deletions,
                         int level,
                         int *new_dead,
                         int *new_dead_count) {
    int added = 0;
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (!initial[p] || dead[p] || escaped[p] || active[p]) continue;
        dead[p] = 1;
        if (death_level && death_level[p] < 0) death_level[p] = level;
        if (new_dead && new_dead_count && *new_dead_count < SEARCH0_MAX_PROV) {
            new_dead[(*new_dead_count)++] = p;
        }
        const RL0FMCycle *cycle = record_cycle_by_no0(records, record_count, p);
        if (cycle) {
            if (!rl0_fm_deletions_add_cycle(deletions, level, cycle)) return -1;
        }
        added++;
    }
    return added;
}

static int delete_new_dead_from_map0(RL0ForgetMap *map,
                                      const Search0Record *records,
                                      int record_count,
                                      const int *new_dead,
                                      int start,
                                      int end) {
    if (!map || !new_dead) return 0;
    for (int i = start; i < end; i++) {
        const RL0FMCycle *cycle = record_cycle_by_no0(records, record_count, new_dead[i]);
        if (cycle) {
            rl0_fm_delete_cycle_from_map(map, cycle);
        }
    }
    return 1;
}

static int refilter_states0(StateVec *cur,
                            const Tile *tile,
                            const RL0ForgetMap *map,
                            int hidden_bound,
                            unsigned char *escaped,
                            unsigned char *terminal_dead,
                            Attach0Stats *astats,
                            Attach0ClosureStats *cstats,
                            Search0CodeCounts *counts,
                            PrintCtx *print,
                            int step,
                            int graph_level) {
    StateVec next;
    statevec_init0(&next);
    for (int i = 0; i < cur->count; i++) {
        Search0State src = cur->data[i];
        Search0State s = src;
        {
            int hidden_count = 0;
            Search0Code code = S0C_OK;
            int cr = force_live_closure_bounded0(&s.poly,
                                                 tile,
                                                 s.tiles,
                                                 &s.tile_count,
                                                 map,
                                                 1024,
                                                 hidden_bound,
                                                 s.hidden,
                                                 &hidden_count,
                                                 astats,
                                                 cstats,
                                                 &code);
            if (cr < 0) {
                search0_counts_add(counts, code);
                s.hidden_count = hidden_count;
                add_state_prov0(escaped, &s);
                add_terminal_state_prov0(terminal_dead, &s);
                if (print && print->fp) {
                    int src_id = ensure_graph_node0(print,
                                                    tile,
                                                    &src,
                                                    "carry",
                                                    step,
                                                    graph_level,
                                                    NULL);
                    emit_graph_node_mark0(print, src_id, "escape", code);
                }
                continue;
            }
            if (cr == 0) {
                search0_counts_add(counts, code);
                continue;
            }
            search0_counts_add(counts, S0C_OK);
            s.hidden_count = hidden_count;
        }
        poly_to_key0(&s.poly, s.key, sizeof(s.key));
        if (print && print->fp && strcmp(src.key, s.key) != 0) {
            emit_relation0(print, tile, map, &src, &s, step, graph_level, "closure", "closure");
        }
        if (!statevec_add_merge0(&next, &s)) {
            statevec_free0(&next);
            return 0;
        }
    }
    statevec_free0(cur);
    *cur = next;
    return 1;
}

static int checkpoint_fixed_point0(StateVec *cur,
                                   const Tile *tile,
                                   RL0ForgetMap *map,
                                   const char *remembrance_path,
                                   RL0FMDeletionSet *deletions,
                                   int level,
                                   int hidden_bound,
                                   const unsigned char *initial,
                                   unsigned char *escaped,
                                   unsigned char *terminal_dead,
                                   unsigned char *dead,
                                   int *death_level,
                                   const Search0Record *records,
                                   int record_count,
                                   Attach0Stats *astats,
                                   Attach0ClosureStats *cstats,
                                   Search0CodeCounts *checkpoint_counts,
                                   int *new_dead,
                                   int *new_dead_count,
                                   PrintCtx *print,
                                   int step,
                                   int graph_level) {
    for (;;) {
        unsigned char active[SEARCH0_MAX_PROV];
        int added;
        int new_dead_start = (new_dead_count ? *new_dead_count : 0);
        if (print && print->fp) {
            for (int i = 0; i < cur->count; i++) {
                ensure_graph_node0(print, tile, &cur->data[i], "carry", step, graph_level, NULL);
            }
        }
        count_prov0(cur, active);
        added = add_new_dead0(initial,
                              active,
                              escaped,
                              dead,
                              death_level,
                              records,
                              record_count,
                              deletions,
                              level,
                              new_dead,
                              new_dead_count);
        if (added < 0) return 0;
        /*
           Newly-dead records are provenance-level events, not state-placement
           events.  The deletion/checkpoint logic below is real search state:
           it records the dead provenance, rebuilds the map with the new
           deletion, and refilters current states.  Do not express that by
           moving the "last node for provenance" in the graph.  A provenance
           may have merged into or merely last touched an unrelated visible
           state, so updating that node's division corrupts the layout.
        */
        if (added == 0) return 1;
        if (!new_dead || !new_dead_count ||
            !delete_new_dead_from_map0(map,
                                       records,
                                       record_count,
                                       new_dead,
                                       new_dead_start,
                                       *new_dead_count)) {
            if (!rebuild_map0(map, remembrance_path, deletions, level)) return 0;
        }
        if (print && print->fp) print->sublevel++;
        if (!refilter_states0(cur,
                              tile,
                              map,
                              hidden_bound,
                              escaped,
                              terminal_dead,
                              astats,
                              cstats,
                              checkpoint_counts,
                              print,
                              step,
                              graph_level)) return 0;
    }
}

static FILE *progress_fp0 = NULL;
static int progress_enabled0 = 1;
static int progress_detail0 = 0;

static FILE *progress_out0(void) {
    return progress_fp0 ? progress_fp0 : stdout;
}

static void fprint_fm_cycle0(FILE *fp, const RL0FMCycle *cycle) {
    fprintf(fp, "[");
    for (int i = 0; i < cycle->n; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "[%d,%d]", cycle->item[i].p, cycle->item[i].i);
    }
    fprintf(fp, "]");
}

static int strict_record_live0(const Search0Record *r,
                               const Tile *tile,
                               const RL0ForgetMap *map) {
    Coord boundary[SEARCH0_MAX_COORDS];
    int vc;
    if (!r || !r->have_boundary || !r->have_tiles) return 0;
    vc = build_boundary_vertices(&r->boundary, boundary);
    if (vc < 0) return 0;
    for (int i = 0; i < vc; i++) {
        RL0FMArc key;
        const RL0FMArc *values = NULL;
        int value_count = 0;
        if (!boundary0_build_vertex_arc(tile, r->tiles, r->tile_count, boundary[i], &key)) {
            return 0;
        }
        if (!rl0_fm_lookup_any_rotation(map, &key, &values, &value_count, NULL) ||
            value_count <= 0) {
            return 0;
        }
    }
    return 1;
}

static int cycle_in_list0(const RL0FMCycle *cycles, int count, const RL0FMCycle *cycle) {
    for (int i = 0; i < count; i++) {
        if (rl0_fm_cycle_equal(&cycles[i], cycle)) return 1;
    }
    return 0;
}

static int write_level0_deletions0(const char *path,
                                   const RL0FMCycle *cycles,
                                   int cycle_count) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    fprintf(fp, "# RL0 indexed vertex-figure deletions.\n");
    fprintf(fp, "# Regenerated by rl0_refine from strict Level 0 self-check.\n");
    fprintf(fp, "# Reflections are generated at dictionary load time.\n\n");
    fprintf(fp, "---[0]---\n");
    for (int i = 0; i < cycle_count; i++) {
        fprint_fm_cycle0(fp, &cycles[i]);
        fprintf(fp, "\n");
    }
    fclose(fp);
    return 1;
}

static int write_search_deletions0(const char *path,
                                   const Search0Record *records,
                                   int record_count,
                                   const int *death_level) {
    FILE *fp = fopen(path, "w");
    int max_level = -1;
    if (!fp) return 0;
    fprintf(fp, "# RL0 indexed vertex-figure deletions.\n");
    fprintf(fp, "# Regenerated by rl0_refine.\n");
    fprintf(fp, "# Reflections are generated at dictionary load time.\n\n");
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) {
        if (death_level[p] > max_level) max_level = death_level[p];
    }
    for (int level = 0; level <= max_level; level++) {
        int wrote_header = 0;
        for (int i = 0; i < record_count; i++) {
            int file_no = records[i].file_no;
            if (file_no < 0 || file_no >= SEARCH0_MAX_PROV) continue;
            if (death_level[file_no] != level) continue;
            if (!records[i].have_pi) continue;
            if (!wrote_header) {
                if (level > 0) fprintf(fp, "\n");
                fprintf(fp, "---[%d]---\n", level);
                wrote_header = 1;
            }
            fprint_fm_cycle0(fp, &records[i].full_cycle);
            fprintf(fp, "\n");
        }
    }
    fclose(fp);
    return 1;
}

static int regenerate_level0_deletions0(const char *remembrance_path,
                                        const char *deletions_path,
                                        const Tile *tile,
                                        const Search0Record *records,
                                        int record_count) {
    RL0ForgetMap *raw = calloc(1, sizeof(*raw));
    RL0FMCycle cycles[SEARCH0_MAX_RECORDS];
    int cycle_count = 0;
    if (!raw) return 0;
    rl0_fm_init(raw);
    if (!rl0_fm_load_remembrance(raw, remembrance_path)) {
        free(raw);
        return 0;
    }
    for (int i = 0; i < record_count; i++) {
        if (!records[i].have_pi) continue;
        if (strict_record_live0(&records[i], tile, raw)) continue;
        if (!cycle_in_list0(cycles, cycle_count, &records[i].full_cycle)) {
            if (cycle_count >= SEARCH0_MAX_RECORDS) {
                free(raw);
                return 0;
            }
            cycles[cycle_count++] = records[i].full_cycle;
        }
    }
    free(raw);
    if (!write_level0_deletions0(deletions_path, cycles, cycle_count)) {
        return 0;
    }
    if (progress_enabled0) fprintf(stderr, "regenerated Level 0 deletions: %d\n", cycle_count);
    return 1;
}

static int collect_initial_exclusions0(const Search0Record *records,
                                       int record_count,
                                       const RL0ForgetMap *map,
                                       int *out,
                                       int out_cap) {
    int count = 0;
    for (int i = 0; i < record_count; i++) {
        if (!records[i].have_pi) continue;
        if (records[i].file_no < 0) continue;
        if (rl0_fm_contains_complete(map, &records[i].full_cycle)) continue;
        if (count < out_cap) out[count++] = records[i].file_no;
    }
    return count;
}


static int deletion_set_max_level0(const RL0FMDeletionSet *set) {
    int max_level = -1;
    if (!set) return -1;
    for (int i = 0; i < set->count; i++) {
        if (set->level[i] > max_level) max_level = set->level[i];
    }
    return max_level;
}

static void print_header0(void) {
    if (!progress_enabled0) return;
    fprintf(progress_out0(),
            "%5s %6s %4s %5s | %5s %6s %6s | %-16s\n",
            "lvl", "states", "ok", "prune",
            "roots", "remove", "escape",
            progress_detail0 ? "detail" : "note");
}

static void print_progress0(int distance,
                            int living,
                            const unsigned char *initial,
                            const unsigned char *escaped,
                            const unsigned char *dead,
                            const Search0CodeCounts *branch_counts,
                            const Search0CodeCounts *checkpoint_counts,
                            const int *new_dead,
                            int new_dead_count) {
    int ok = search0_counts_kind(branch_counts, S0K_OK) +
             search0_counts_kind(checkpoint_counts, S0K_OK);
    int prune = search0_counts_kind(branch_counts, S0K_PRUNE) +
                search0_counts_kind(checkpoint_counts, S0K_PRUNE);
    (void)new_dead;
    (void)new_dead_count;
    if (!progress_enabled0) return;
    fprintf(progress_out0(), "%5d %6d %4d %5d | %5d %6d %6d | ",
           distance,
           living,
           ok,
           prune,
           count_unknown0(initial, escaped, dead),
           count_initial_removed0(initial, escaped, dead),
           count_initial_present0(initial, escaped));
    if (progress_detail0) {
        fprintf(progress_out0(), "branch{");
        search0_counts_print_detail(progress_out0(), branch_counts);
        fprintf(progress_out0(), "} check{");
        search0_counts_print_detail(progress_out0(), checkpoint_counts);
        fprintf(progress_out0(), "}");
    } else {
        search0_counts_print_note(progress_out0(), branch_counts, checkpoint_counts);
    }
    fprintf(progress_out0(), "\n");
}


static int min_distance_all0(const StateVec *v, const Tile *tile, const RL0ForgetMap *map) {
    int md = SEARCH0_INF;
    for (int i = 0; i < v->count; i++) {
        PortChoice ch;
        int pc = 0, d = SEARCH0_INF;
        int r = choose_port0(&v->data[i], tile, map, &ch, &pc, &d);
        if (r != 0 && d < md) md = d;
    }
    return md;
}





static void fprint_coord_list_line0(FILE *fp, const Coord *coords, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", coords[i].v, coords[i].x, coords[i].y);
    }
    fprintf(fp, "]");
}

static void fprint_mark_list0(FILE *fp, const unsigned char *marks) {
    int first = 1;
    fprintf(fp, "[");
    for (int i = 0; i < SEARCH0_MAX_PROV; i++) {
        if (!marks[i]) continue;
        if (!first) fprintf(fp, ",");
        fprintf(fp, "%d", i);
        first = 0;
    }
    fprintf(fp, "]");
}

static void fprint_arc0(FILE *fp, const RL0FMArc *arc) {
    fprintf(fp, "[");
    for (int i = 0; i < arc->n; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "[%d,%d]", arc->item[i].p, arc->item[i].i);
    }
    fprintf(fp, "]");
}

static void fprint_imgtable_cycle_raw0(FILE *fp, const Cycle *c) {
    fputc('(', fp);
    for (int i = 0; i < c->n; i++) {
        if (i) fputc(',', fp);
        fprintf(fp, "%d %d %d", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    fputc(')', fp);
}

static void fprint_imgtable_constants0(FILE *fp, const Tile *tile) {
    fputc('(', fp);
    for (int i = 0; i < tile->constant_count; i++) {
        if (i) fputc(',', fp);
        fprintf(fp, "%s=%s", tile->constants[i].name, tile->constants[i].expr);
    }
    fputc(')', fp);
}

static void fprint_imgtable_basis0(FILE *fp, const Tile *tile) {
    fputc('(', fp);
    for (int i = 0; i < tile->basis_count; i++) {
        const TileBasis *b = &tile->bases[i];
        if (i) fputc(',', fp);
        fprintf(fp, "%d:%s,%s;%s,%s", b->valence, b->a11, b->a12, b->a21, b->a22);
    }
    fputc(')', fp);
}

static void fprint_imgtable_shape0(FILE *fp, const Tile *tile, const Poly *poly) {
    int hole_flag = (poly->cycle_count > 1) ? 1 : 0;
    fprintf(fp, "[ %d | ", hole_flag);
    fprint_imgtable_constants0(fp, tile);
    fprintf(fp, " | ");
    fprint_imgtable_basis0(fp, tile);
    for (int i = 0; i < poly->cycle_count; i++) {
        fprintf(fp, " | ");
        fprint_imgtable_cycle_raw0(fp, &poly->cycles[i]);
    }
    fprintf(fp, " ]\n");
}

static void fprint_tile_list0(FILE *fp, const Cycle *tiles, int tile_count) {
    for (int i = 0; i < tile_count; i++) {
        fprintf(fp, "tile: ");
        fprint_imgtable_cycle_raw0(fp, &tiles[i]);
        fprintf(fp, "\n");
    }
}

static void printctx_free0(PrintCtx *print) {
    if (!print) return;
    free(print->nodes);
    print->nodes = NULL;
    print->node_count = 0;
    print->node_cap = 0;
}

static int append_key_text0(char *buf, size_t cap, size_t *used, const char *text) {
    int n;
    if (!buf || !used || !text || cap == 0) return 0;
    if (*used >= cap) return 0;
    n = snprintf(buf + *used, cap - *used, "%s", text);
    if (n < 0 || (size_t)n >= cap - *used) {
        buf[cap - 1] = '\0';
        return 0;
    }
    *used += (size_t)n;
    return 1;
}

static int append_key_vertex0(char *buf, size_t cap, size_t *used, Coord q) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "(%d,%d,%d)", q.v, q.x, q.y);
    return append_key_text0(buf, cap, used, tmp);
}

static void poly_display_key0(const Poly *p, char *buf, size_t cap) {
    size_t used = 0;
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (!p) return;
    append_key_text0(buf, cap, &used, "poly{");
    for (int c = 0; c < p->cycle_count; c++) {
        const Cycle *cy = &p->cycles[c];
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "c%d:", cy->n);
        append_key_text0(buf, cap, &used, tmp);
        for (int i = 0; i < cy->n; i++) {
            append_key_vertex0(buf, cap, &used, cy->v[i]);
        }
        append_key_text0(buf, cap, &used, ";");
    }
    append_key_text0(buf, cap, &used, "}");
}

static int print_find_node0(const PrintCtx *print, const char *display_key) {
    if (!print || !display_key) return -1;
    for (int i = 0; i < print->node_count; i++) {
        if (strcmp(print->nodes[i].display_key, display_key) == 0) return i;
    }
    return -1;
}

static int emit_graph_node0(PrintCtx *print,
                            const Tile *tile,
                            const Search0State *s,
                            const char *kind,
                            int discover_step,
                            int node_division) {
    PrintNodeRef *next_nodes;
    PrintNodeRef *slot;
    int id;
    if (!print || !print->fp) return -1;
    if (print->limit > 0 && print->next_id >= print->limit) return -1;
    if (print->node_count == print->node_cap) {
        int nc = print->node_cap ? 2 * print->node_cap : 256;
        next_nodes = realloc(print->nodes, (size_t)nc * sizeof(*next_nodes));
        if (!next_nodes) return -1;
        print->nodes = next_nodes;
        print->node_cap = nc;
    }
    id = print->next_id++;
    slot = &print->nodes[print->node_count++];
    memset(slot, 0, sizeof(*slot));
    slot->id = id;
    slot->discover_step = discover_step;
    slot->division = node_division;
    snprintf(slot->key, sizeof(slot->key), "%s", s->key);
    poly_display_key0(&s->poly, slot->display_key, sizeof(slot->display_key));

    fprintf(print->fp, "---node %d---\n", id);
    fprintf(print->fp, "kind: %s\n", kind ? kind : "state");
    fprintf(print->fp, "discover_step: %d\n", discover_step);
    fprintf(print->fp, "division: %d\n", node_division);
    fprintf(print->fp, "tile_count: %d\n", s->tile_count);
    fprintf(print->fp, "hidden_count: %d\n", s->hidden_count);
    fprintf(print->fp, "prov: ");
    fprint_mark_list0(print->fp, s->prov);
    fprintf(print->fp, "\n");
    fprintf(print->fp, "hidden: ");
    fprint_coord_list_line0(print->fp, s->hidden, s->hidden_count);
    fprintf(print->fp, "\n");
    fprint_tile_list0(print->fp, s->tiles, s->tile_count);
    fprintf(print->fp, "key: %s\n", s->key);
    fprintf(print->fp, "display_key: %s\n", slot->display_key);
    fprintf(print->fp, "aggregate:\n");
    fprint_imgtable_shape0(print->fp, tile, &s->poly);
    fprintf(print->fp, "---end-node---\n");
    fflush(print->fp);
    return id;
}

static void emit_graph_node_update0(PrintCtx *print,
                                   int id,
                                   int discover_step,
                                   int node_division) {
    if (!print || !print->fp || id < 0) return;
    fprintf(print->fp, "---node-update---\n");
    fprintf(print->fp, "id: %d\n", id);
    fprintf(print->fp, "discover_step: %d\n", discover_step);
    fprintf(print->fp, "division: %d\n", node_division);
    fprintf(print->fp, "---end-node-update---\n");
    fflush(print->fp);
}

static void emit_graph_node_mark0(PrintCtx *print,
                                  int id,
                                  const char *mark,
                                  Search0Code code) {
    if (!print || !print->fp || id < 0) return;
    fprintf(print->fp, "---node-mark---\n");
    fprintf(print->fp, "id: %d\n", id);
    fprintf(print->fp, "mark: %s\n", mark ? mark : "highlight");
    fprintf(print->fp, "status: %s\n", search0_code_name(code));
    fprintf(print->fp, "---end-node-mark---\n");
    fflush(print->fp);
}

static int ensure_graph_node0(PrintCtx *print,
                              const Tile *tile,
                              const Search0State *s,
                              const char *kind,
                              int discover_step,
                              int node_division,
                              int *is_new) {
    int idx;
    char display_key[SEARCH0_MAX_KEY];
    if (!print || !print->fp) {
        if (is_new) *is_new = 0;
        return -1;
    }
    poly_display_key0(&s->poly, display_key, sizeof(display_key));
    idx = print_find_node0(print, display_key);
    if (idx >= 0) {
        int update = 0;
        if (discover_step > print->nodes[idx].discover_step) {
            print->nodes[idx].discover_step = discover_step;
            update = 1;
        }
        if (node_division >= 0 &&
            (print->nodes[idx].division < 0 || node_division > print->nodes[idx].division)) {
            print->nodes[idx].division = node_division;
            update = 1;
        }
        if (update) {
            emit_graph_node_update0(print,
                                    print->nodes[idx].id,
                                    print->nodes[idx].discover_step,
                                    print->nodes[idx].division);
        }
        {
            int prov_id = singleton_prov0(s);
            if (prov_id >= 0 && prov_id < SEARCH0_MAX_PROV) {
                print->last_node_for_prov[prov_id] = print->nodes[idx].id;
            }
        }
        if (is_new) *is_new = 0;
        return print->nodes[idx].id;
    }
    if (is_new) *is_new = 1;
    return emit_graph_node0(print, tile, s, kind, discover_step,
                            node_division);
}

static void emit_graph_edge0(PrintCtx *print,
                             int src_id,
                             int dst_id,
                             int step,
                             int distance,
                             Coord port,
                             const RL0FMArc *arc,
                             const char *kind,
                             Search0Code code) {
    if (!print || !print->fp) return;
    if (src_id < 0 || dst_id < 0) return;
    fprintf(print->fp, "---edge---\n");
    fprintf(print->fp, "kind: %s\n", kind ? kind : "edge");
    fprintf(print->fp, "status: %s\n", search0_code_name(code));
    fprintf(print->fp, "src: %d\n", src_id);
    fprintf(print->fp, "dst: %d\n", dst_id);
    fprintf(print->fp, "step: %d\n", step);
    fprintf(print->fp, "sublevel: %d\n", print->sublevel);
    fprintf(print->fp, "distance: %d\n", distance);
    fprintf(print->fp, "port: (%d,%d,%d)\n", port.v, port.x, port.y);
    fprintf(print->fp, "arc: ");
    if (arc) fprint_arc0(print->fp, arc);
    else fprintf(print->fp, "[]");
    fprintf(print->fp, "\n");
    fprintf(print->fp, "---end-edge---\n");
    fflush(print->fp);
}

static void emit_focus0(PrintCtx *print,
                        int node_id,
                        const Search0State *src,
                        int step,
                        Coord port) {
    if (!print || !print->fp || !src) return;
    if (node_id < 0) return;
    fprintf(print->fp, "---focus---\n");
    fprintf(print->fp, "node: %d\n", node_id);
    fprintf(print->fp, "key: %s\n", src->key);
    fprintf(print->fp, "step: %d\n", step);
    fprintf(print->fp, "port: (%d,%d,%d)\n", port.v, port.x, port.y);
    fprintf(print->fp, "---end-focus---\n");
    fflush(print->fp);
}

static int coord_less0(Coord a, Coord b) {
    if (a.v != b.v) return a.v < b.v;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

static Coord default_focus_coord0(const Search0State *s) {
    Coord boundary[SEARCH0_MAX_COORDS];
    int vc = build_boundary_vertices(&s->poly, boundary);
    Coord best = {0,0,0};
    if (vc <= 0) return best;
    best = boundary[0];
    for (int i = 1; i < vc; i++) {
        if (coord_less0(boundary[i], best)) best = boundary[i];
    }
    return best;
}

static Coord choose_focus_coord0(const Search0State *s,
                                 const Tile *tile,
                                 const RL0ForgetMap *map) {
    PortChoice choice;
    int port_count = 0;
    int min_distance = SEARCH0_INF;
    int r = choose_port0(s, tile, map, &choice, &port_count, &min_distance);
    if (r != 0 && choice.q.v > 0) return choice.q;
    return default_focus_coord0(s);
}

static void emit_state_focus0(PrintCtx *print,
                              const Tile *tile,
                              const RL0ForgetMap *map,
                              const Search0State *s,
                              int node_id,
                              int step) {
    emit_focus0(print, node_id, s, step, choose_focus_coord0(s, tile, map));
}

static void emit_seed_statevec0(PrintCtx *print,
                                const Tile *tile,
                                const RL0ForgetMap *map,
                                const StateVec *v) {
    if (!print || !print->fp) return;
    for (int i = 0; i < v->count; i++) {
        int is_new = 0;
        int node_id = ensure_graph_node0(print, tile, &v->data[i], "seed", -1, -1, &is_new);
        if (is_new) emit_state_focus0(print, tile, map, &v->data[i], node_id, -1);
        if (print->limit > 0 && print->next_id >= print->limit) return;
    }
}

static void emit_transition0(PrintCtx *print,
                             const Tile *tile,
                             const RL0ForgetMap *map,
                             const Search0State *src,
                             const Search0State *dst,
                             int step,
                             int graph_level,
                             const PortChoice *choice,
                             const RL0FMArc *arc,
                             Search0Code code) {
    int src_id;
    int dst_id;
    int is_new = 0;
    if (!print || !print->fp || !src || !dst) return;
    src_id = ensure_graph_node0(print, tile, src, "carry", step - 1, graph_level, NULL);
    dst_id = ensure_graph_node0(print,
                                tile,
                                dst,
                                "generated",
                                step,
                                graph_division_for_state0(dst, tile, map, graph_level),
                                &is_new);
    if (is_new) emit_state_focus0(print, tile, map, dst, dst_id, step);
    emit_graph_edge0(print,
                     src_id,
                     dst_id,
                     step,
                     choice ? choice->distance : SEARCH0_INF,
                     choice ? choice->q : (Coord){0,0,0},
                     arc,
                     is_new ? "discover" : "collision",
                     code);
}

static void emit_relation0(PrintCtx *print,
                           const Tile *tile,
                           const RL0ForgetMap *map,
                           const Search0State *src,
                           const Search0State *dst,
                           int step,
                           int graph_level,
                           const char *dst_kind,
                           const char *edge_kind) {
    int src_id;
    int dst_id;
    int is_new = 0;
    if (!print || !print->fp || !src || !dst) return;
    src_id = ensure_graph_node0(print, tile, src, "carry", step - 1, graph_level, NULL);
    dst_id = ensure_graph_node0(print,
                                tile,
                                dst,
                                dst_kind ? dst_kind : "carry",
                                step,
                                graph_division_for_state0(dst, tile, map, graph_level),
                                &is_new);
    if (is_new) emit_state_focus0(print, tile, map, dst, dst_id, step);
    if (src_id == dst_id) return;
    emit_graph_edge0(print,
                     src_id,
                     dst_id,
                     step,
                     SEARCH0_INF,
                     (Coord){0,0,0},
                     NULL,
                     edge_kind ? edge_kind : "carry",
                     S0C_OK);
}


int main(int argc, char **argv) {
    const char *tile_path = SEARCH0_DEFAULT_TILE;
    const char *completions_path = SEARCH0_DEFAULT_COMPLETIONS;
    const char *remembrance_path = SEARCH0_DEFAULT_REMEMBRANCE;
    const char *deletions_path = SEARCH0_DEFAULT_DELETIONS;
    const char *optimize_path = SEARCH0_DEFAULT_OPTIMIZE;
    int delete_level = 0;
    int hidden_bound = SEARCH0_DEFAULT_HIDDEN_BOUND;
    int focus_record = 0;
    int select_all_records = 0;
    int have_record_selection = 0;
    unsigned char selected_records[SEARCH0_MAX_PROV];
    int ignore_pairwise = 0;
    int use_optimized_records = 1;
    int keep_deletions = 0;
    const char *print_path = NULL;
    int print_to_stdout = 0;
    int print_limit = 0;
    Tile tile;
    RL0ForgetMap *map = calloc(1, sizeof(*map));
    Search0Record *records = calloc(SEARCH0_MAX_RECORDS, sizeof(*records));
    int record_count = 0;
    StateVec cur, next;
    unsigned char current_prov[SEARCH0_MAX_PROV];
    unsigned char initial_prov[SEARCH0_MAX_PROV];
    unsigned char escaped_prov[SEARCH0_MAX_PROV];
    unsigned char dead_prov[SEARCH0_MAX_PROV];
    unsigned char terminal_dead_prov[SEARCH0_MAX_PROV];
    unsigned char display_dead_prov[SEARCH0_MAX_PROV];
    int death_level[SEARCH0_MAX_PROV];
    int initial_exclusions[SEARCH0_MAX_PROV];
    int initial_exclusion_count = 0;
    RL0FMDeletionSet deletions;
    int completed_distance = 0;
    int step = 0;
    Attach0Stats astats;
    Attach0ClosureStats cstats;
    PrintCtx print;
    int optimized_ids[SEARCH0_MAX_PROV];
    int optimized_count = 0;
    memset(selected_records, 0, sizeof(selected_records));

    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--tile") == 0 && ai + 1 < argc) {
            tile_path = argv[++ai];
            continue;
        }
        if (strcmp(argv[ai], "--hidden-bound") == 0 && ai + 1 < argc) {
            hidden_bound = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(argv[ai], "--optimize-list") == 0 && ai + 1 < argc) {
            optimize_path = argv[++ai];
            continue;
        }
        if (strcmp(argv[ai], "--remembrance") == 0 && ai + 1 < argc) {
            remembrance_path = argv[++ai];
            continue;
        }
        if (strcmp(argv[ai], "--completions") == 0 && ai + 1 < argc) {
            completions_path = argv[++ai];
            continue;
        }
    }

    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--tile") == 0 ||
            strcmp(argv[ai], "--hidden-bound") == 0 ||
            strcmp(argv[ai], "--optimize-list") == 0 ||
            strcmp(argv[ai], "--remembrance") == 0 ||
            strcmp(argv[ai], "--completions") == 0) {
            ai++;
            continue;
        }
        if (strcmp(argv[ai], "--print") == 0) {
            if (ai + 1 < argc && argv[ai + 1][0] != '-') {
                print_path = argv[++ai];
            } else {
                print_to_stdout = 1;
            }
            continue;
        }
        if (strcmp(argv[ai], "--print-limit") == 0 && ai + 1 < argc) {
            print_limit = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(argv[ai], "--ignore-pairwise") == 0) {
            ignore_pairwise = 1;
            continue;
        }
        if (strcmp(argv[ai], "--optimized") == 0) {
            use_optimized_records = 1;
            continue;
        }
        if (strcmp(argv[ai], "--all") == 0) {
            use_optimized_records = 0;
            have_record_selection = 1;
            select_all_records = 1;
            continue;
        }
        if (strcmp(argv[ai], "--keep-deletions") == 0) {
            keep_deletions = 1;
            continue;
        }
        if (strcmp(argv[ai], "--progress-detail") == 0) {
            progress_detail0 = 1;
            continue;
        }
        if (!parse_record_selection_token0(argv[ai],
                                           selected_records,
                                           SEARCH0_MAX_PROV,
                                           &select_all_records)) {
            fprintf(stderr, "invalid record selection: %s\n", argv[ai]);
            return 1;
        }
        have_record_selection = 1;
        use_optimized_records = 0;
        focus_record = 0;
    }

    if (print_to_stdout || print_path) progress_enabled0 = 0;
    else progress_enabled0 = 1;

    if (use_optimized_records) {
        if (!parse_optimize_records0(optimize_path,
                                     optimized_ids,
                                     SEARCH0_MAX_PROV,
                                     &optimized_count)) {
            fprintf(stderr, "failed to load optimize record list: %s\n",
                    optimize_path);
            return 1;
        }
    }

    memset(&print, 0, sizeof(print));
    for (int i = 0; i < SEARCH0_MAX_PROV; i++) print.last_node_for_prov[i] = -1;
    print.limit = print_limit;

    if (!map || !records) {
        fprintf(stderr, "out of memory allocating records\n");
        return 1;
    }
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }
    if (!parse_records0(completions_path, records, &record_count)) {
        fprintf(stderr, "failed to parse seed completions: %s\n", completions_path);
        return 1;
    }
    if (!keep_deletions) {
        if (!regenerate_level0_deletions0(remembrance_path,
                                          deletions_path,
                                          &tile,
                                          records,
                                          record_count)) {
            fprintf(stderr, "failed to regenerate Level 0 deletions: %s\n", deletions_path);
            return 1;
        }
    }
    if (!rl0_fm_load_deletions(&deletions, deletions_path)) {
        if (keep_deletions) {
            fprintf(stderr,
                    "deletions file missing/unreadable; regenerating Level 0: %s\n",
                    deletions_path);
            if (!regenerate_level0_deletions0(remembrance_path,
                                              deletions_path,
                                              &tile,
                                              records,
                                              record_count)) {
                fprintf(stderr, "failed to regenerate Level 0 deletions: %s\n", deletions_path);
                return 1;
            }
            if (!rl0_fm_load_deletions(&deletions, deletions_path)) {
                fprintf(stderr, "failed to load regenerated deletions: %s\n", deletions_path);
                return 1;
            }
        } else {
            fprintf(stderr, "failed to load regenerated deletions: %s\n", deletions_path);
            return 1;
        }
    }
    if (keep_deletions) {
        delete_level = deletion_set_max_level0(&deletions);
        if (delete_level < 0) delete_level = 0;
        if (progress_enabled0) fprintf(stderr, "loaded deletions through level %d: %s\n", delete_level, deletions_path);
    }
    if (!rebuild_map0(map, remembrance_path, &deletions, delete_level)) {
        fprintf(stderr, "failed to build dictionary\n");
        free(records);
        free(map);
        return 1;
    }

    initial_exclusion_count = collect_initial_exclusions0(records,
                                                          record_count,
                                                          map,
                                                          initial_exclusions,
                                                          SEARCH0_MAX_PROV);

    statevec_init0(&cur);
    statevec_init0(&next);
    attach0_stats_init(&astats);
    attach0_closure_stats_init(&cstats);
    memset(initial_prov, 0, sizeof(initial_prov));
    memset(escaped_prov, 0, sizeof(escaped_prov));
    memset(dead_prov, 0, sizeof(dead_prov));
    memset(terminal_dead_prov, 0, sizeof(terminal_dead_prov));
    memset(display_dead_prov, 0, sizeof(display_dead_prov));
    for (int p = 0; p < SEARCH0_MAX_PROV; p++) death_level[p] = -1;
    for (int i = 0; i < initial_exclusion_count; i++) {
        int p = initial_exclusions[i];
        if (p >= 0 && p < SEARCH0_MAX_PROV) death_level[p] = 0;
    }

    if (print_to_stdout) {
        print.fp = stdout;
        progress_enabled0 = 0;
    } else if (print_path) {
        print.fp = fopen(print_path, "w");
        if (!print.fp) {
            fprintf(stderr, "failed to open print output: %s\n", print_path);
            return 1;
        }
        progress_enabled0 = 0;
    } else {
        progress_enabled0 = 1;
        progress_fp0 = stdout;
    }

    for (int i = 0; i < record_count; i++) {
        Search0State s;
        if (focus_record > 0 && records[i].file_no != focus_record) continue;
        if (have_record_selection &&
            !selection_contains0(records[i].file_no,
                                 selected_records,
                                 select_all_records)) {
            continue;
        }
        if (use_optimized_records &&
            !optimized_record0(records[i].file_no,
                               optimized_ids,
                               optimized_count)) {
            continue;
        }
        if (!records[i].have_boundary || !records[i].have_hidden || !records[i].have_tiles || !records[i].have_pi) continue;
        if (ignore_pairwise && records[i].full_cycle.n == 2) continue;
        if (!rl0_fm_contains_complete(map, &records[i].full_cycle)) continue;
        if (!make_seed0(&records[i], &s)) continue;
        {
            int hidden_count = 0;
            int cr = force_live_closure_bounded0(&s.poly,
                                                 &tile,
                                                 s.tiles,
                                                 &s.tile_count,
                                                 map,
                                                 1024,
                                                 hidden_bound,
                                                 s.hidden,
                                                 &hidden_count,
                                                 &astats,
                                                 &cstats,
                                                 NULL);
            if (cr < 0) {
                s.hidden_count = hidden_count;
                add_state_prov0(escaped_prov, &s);
                continue;
            }
            if (cr == 0) continue;
            s.hidden_count = hidden_count;
        }
        poly_to_key0(&s.poly, s.key, sizeof(s.key));
        if (!statevec_add_merge0(&cur, &s)) {
            fprintf(stderr, "out of memory seeding states\n");
            return 1;
        }
    }

    count_prov_with_escaped0(&cur, escaped_prov, initial_prov);
    print_header0();
    {
        Search0CodeCounts seed_counts;
        search0_counts_init(&seed_counts);
        Search0CodeCounts seed_check_counts;
        search0_counts_init(&seed_check_counts);
        make_display_dead0(dead_prov, terminal_dead_prov, display_dead_prov);
        print_progress0(0,
                        cur.count,
                        initial_prov,
                        escaped_prov,
                        display_dead_prov,
                        &seed_counts,
                        &seed_check_counts,
                        initial_exclusions,
                        initial_exclusion_count);
    }
    emit_seed_statevec0(&print, &tile, map, &cur);

    while (cur.count > 0) {
        int global_min = min_distance_all0(&cur, &tile, map);
        Search0CodeCounts step_counts;
        Search0CodeCounts checkpoint_counts;
        int old_count = cur.count;
        search0_counts_init(&step_counts);
        search0_counts_init(&checkpoint_counts);
        if (global_min == SEARCH0_INF) {
            if (progress_enabled0) fprintf(progress_out0(), "checkpoint complete: no remaining ports states=%d\n", cur.count);
            break;
        }
        statevec_free0(&next);
        statevec_init0(&next);
        for (int i = 0; i < cur.count; i++) {
            Search0State src_state = cur.data[i];
            PortChoice ch;
            int pc = 0, d = SEARCH0_INF;
            int r;
            if (!strip_state_prov0(&src_state, escaped_prov)) continue;
            r = choose_port0(&src_state, &tile, map, &ch, &pc, &d);
            if (r < 0) {
                if (print.fp) {
                    int node_id = ensure_graph_node0(&print, &tile, &src_state, "carry", step, graph_division_from_distance0(global_min), NULL);
                    emit_focus0(&print, node_id, &src_state, step, ch.q);
                }
                search0_counts_add(&step_counts, S0C_ERROR_CHOOSE_PORT); continue;
            }
            if (r == 0) {
                search0_counts_add(&step_counts, S0C_OK_NO_PORT);
                if (!statevec_add_merge0(&next, &src_state)) {
                    fprintf(stderr, "state limit while carrying no-port state\n");
                    return 1;
                }
                continue;
            }
            if (d != global_min) {
                search0_counts_add(&step_counts, S0C_OK_CARRY);
                if (!statevec_add_merge0(&next, &src_state)) {
                    fprintf(stderr, "state limit while carrying future-distance state\n");
                    return 1;
                }
                continue;
            }
            if (print.fp) {
                int node_id = ensure_graph_node0(&print, &tile, &src_state, "carry", step, graph_division_from_distance0(global_min), NULL);
                emit_focus0(&print, node_id, &src_state, step, ch.q);
            }
            for (int k = 0; k < ch.value_count; k++) {
                Search0State out;
                if (ch.values[k].n <= 0) continue;
                {
                    Search0Code code = S0C_OK;
                    int ar = state_after_attach0(&src_state, &tile, map, &ch, k, hidden_bound, &out, &astats, &cstats, &code);
                    search0_counts_add(&step_counts, code);
                    if (ar > 0) {
                        if (print.fp) emit_transition0(&print, &tile, map, &src_state, &out, step, graph_division_from_distance0(global_min), &ch, &ch.values[k], code);
                        if (!statevec_add_merge0(&next, &out)) {
                            fprintf(stderr, "state limit while adding generated state\n");
                            return 1;
                        }
                    } else if (ar < 0) {
                        add_state_prov0(escaped_prov, &out);
                        add_terminal_state_prov0(terminal_dead_prov, &out);
                        if (print.fp) {
                            int src_id = ensure_graph_node0(&print,
                                                            &tile,
                                                            &src_state,
                                                            "carry",
                                                            step,
                                                            graph_division_from_distance0(global_min),
                                                            NULL);
                            emit_graph_node_mark0(&print, src_id, "escape", code);
                        }
                        if (!filter_statevec_prov0(&next, out.prov)) {
                            fprintf(stderr, "state limit while pruning escaped provenance\n");
                            return 1;
                        }
                        break;
                    }
                }
            }
        }
        if (!filter_statevec_prov0(&next, escaped_prov)) {
            fprintf(stderr, "state limit while filtering escaped provenance\n");
            return 1;
        }
        count_prov_with_escaped0(&next, escaped_prov, current_prov);
        (void)old_count;
        if (next.count == 0) {
            int new_dead[SEARCH0_MAX_PROV];
            int new_dead_count = 0;
            {
                int cf = checkpoint_fixed_point0(&next,
                                                 &tile,
                                                 map,
                                                 remembrance_path,
                                                 &deletions,
                                                 global_min,
                                                 hidden_bound,
                                                 initial_prov,
                                                 escaped_prov,
                                                 terminal_dead_prov,
                                                 dead_prov,
                                                 death_level,
                                                 records,
                                                 record_count,
                                                 &astats,
                                                 &cstats,
                                                 &checkpoint_counts,
                                                 new_dead,
                                                 &new_dead_count,
                                                 &print,
                                                 step,
                                                 graph_division_from_distance0(global_min));
                if (cf == 0) {
                    fprintf(stderr, "checkpoint fixed point failed\n");
                    return 1;
                }
            }
            if (new_dead_count > 0 && !write_search_deletions0(deletions_path, records, record_count, death_level)) {
                fprintf(stderr, "failed to write deletions: %s\n", deletions_path);
                return 1;
            }
            make_display_dead0(dead_prov, terminal_dead_prov, display_dead_prov);
            print_progress0(global_min, next.count, initial_prov, escaped_prov, display_dead_prov, &step_counts, &checkpoint_counts, new_dead, new_dead_count);
            break;
        }
        {
            int next_min;
            statevec_free0(&cur);
            cur = next;
            statevec_init0(&next);
            next_min = min_distance_all0(&cur, &tile, map);
            if (next_min > global_min) {
                int new_dead[SEARCH0_MAX_PROV];
                int new_dead_count = 0;
                completed_distance = global_min;
                {
                    int cf = checkpoint_fixed_point0(&cur,
                                                     &tile,
                                                     map,
                                                     remembrance_path,
                                                     &deletions,
                                                     completed_distance,
                                                     hidden_bound,
                                                     initial_prov,
                                                     escaped_prov,
                                                     terminal_dead_prov,
                                                     dead_prov,
                                                     death_level,
                                                     records,
                                                     record_count,
                                                     &astats,
                                                     &cstats,
                                                     &checkpoint_counts,
                                                     new_dead,
                                                     &new_dead_count,
                                                     &print,
                                                     step,
                                                     graph_division_from_distance0(completed_distance));
                    if (cf == 0) {
                        fprintf(stderr, "checkpoint fixed point failed\n");
                        return 1;
                    }
                }
                if (new_dead_count > 0 && !write_search_deletions0(deletions_path, records, record_count, death_level)) {
                    fprintf(stderr, "failed to write deletions: %s\n", deletions_path);
                    return 1;
                }
                make_display_dead0(dead_prov, terminal_dead_prov, display_dead_prov);
                print_progress0(completed_distance, cur.count, initial_prov, escaped_prov, display_dead_prov, &step_counts, &checkpoint_counts, new_dead, new_dead_count);
                count_prov_with_escaped0(&cur, escaped_prov, current_prov);
                if (count_unknown0(initial_prov, escaped_prov, display_dead_prov) == 0) break;
            }
        }
        step++;
    }

    (void)completed_distance;
    if (print.fp && print.fp != stdout) fclose(print.fp);
    printctx_free0(&print);
    statevec_free0(&cur);
    statevec_free0(&next);
    free(records);
    free(map);
    return 0;
}
