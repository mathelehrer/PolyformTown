/* RL1 boundary completion core. Surface tools should use search1/refine1. */

#include "rl1/bcomp1.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/boundary.h"
#include "core/hash.h"
#include "rl0/boundary0.h"

#define BCOMP1_SCORE_N 8

typedef struct {
    const Tile *tile;
    const RL0ForgetMap *map;
    int depth;
    int max_tiles;
    int live_only;
    int print_records;
    size_t max_dfs;
    FILE *debug_exceptions;
    int debug_exceptions_limit;
    int debug_exceptions_count;
    HashTable seen;
    BComp1RecordVec finals;
    BComp1Stats stats;
    size_t progress_interval;
    int progress_depth;
    int progress_tty;
    int progress_printed;
    const char *progress_label;
    int stop_after_output;
    int stop_requested;
    int path_index[128];
    int path_total[128];
    int path_depth;
} B1Search;

static void stats_init(BComp1Stats *s) { memset(s, 0, sizeof(*s)); }

static void progress_render(B1Search *search, int force) {
    if (!search || !search->progress_interval) return;
    if (!force && (search->stats.dfs_calls % search->progress_interval) != 0) return;
    int depth = search->path_depth;
    if (search->progress_depth > 0 && depth > search->progress_depth) depth = search->progress_depth;
    if (search->progress_tty) {
        if (search->progress_printed) fprintf(stderr, "\033[2A");
        fprintf(stderr, "\r\033[Kstats%s%s: outputs=%zu dup=%zu calls=%zu attempts=%zu ok=%zu dead=%zu illegal=%zu no_dict=%zu trunc=%zu cap(tile=%zu attach=%zu hidden=%zu cycle=%zu) max(tile=%zu hidden=%zu bverts=%zu cverts=%zu)\n",
                search->progress_label ? " " : "",
                search->progress_label ? search->progress_label : "",
                search->stats.outputs,
                search->stats.duplicates,
                search->stats.dfs_calls,
                search->stats.attach_attempts,
                search->stats.attach_successes,
                search->stats.filtered_dead,
                search->stats.filtered_illegal,
                search->stats.no_dictionary,
                search->stats.truncated,
                search->stats.max_tiles_stop,
                search->stats.attach_tile_capacity_hit + search->stats.attach_tile_limit_hit,
                search->stats.hidden_rebuild_fail,
                search->stats.cycle_capacity_seen,
                search->stats.max_tile_count_seen,
                search->stats.max_hidden_count_seen,
                search->stats.max_boundary_vertices_seen,
                search->stats.max_cycle_vertices_seen);
        fprintf(stderr, "\r\033[Kpath:");
        for (int i = 0; i < depth; i++) fprintf(stderr, " %d:%d/%d", i + 1, search->path_index[i], search->path_total[i]);
        if (depth < search->path_depth) fprintf(stderr, " ...");
        fprintf(stderr, "\n");
        fflush(stderr);
        search->progress_printed = 1;
    } else {
        fprintf(stderr, "progress%s%s outputs=%zu dup=%zu calls=%zu attempts=%zu ok=%zu dead=%zu illegal=%zu no_dict=%zu trunc=%zu cap(tile=%zu attach=%zu hidden=%zu cycle=%zu) max(tile=%zu hidden=%zu bverts=%zu cverts=%zu) path=",
                search->progress_label ? " " : "",
                search->progress_label ? search->progress_label : "",
                search->stats.outputs,
                search->stats.duplicates,
                search->stats.dfs_calls,
                search->stats.attach_attempts,
                search->stats.attach_successes,
                search->stats.filtered_dead,
                search->stats.filtered_illegal,
                search->stats.no_dictionary,
                search->stats.truncated,
                search->stats.max_tiles_stop,
                search->stats.attach_tile_capacity_hit + search->stats.attach_tile_limit_hit,
                search->stats.hidden_rebuild_fail,
                search->stats.cycle_capacity_seen,
                search->stats.max_tile_count_seen,
                search->stats.max_hidden_count_seen,
                search->stats.max_boundary_vertices_seen,
                search->stats.max_cycle_vertices_seen);
        for (int i = 0; i < depth; i++) fprintf(stderr, " %d:%d/%d", i + 1, search->path_index[i], search->path_total[i]);
        if (depth < search->path_depth) fprintf(stderr, " ...");
        fprintf(stderr, "\n");
    }
}


static void skip_ws(const char **pp) {
    while (isspace((unsigned char)**pp)) (*pp)++;
}
static void stats_observe_state(B1Search *search, const BComp1State *s) {
    size_t boundary_vertices = 0;
    if (!search || !s) return;
    if ((size_t)s->tile_count > search->stats.max_tile_count_seen)
        search->stats.max_tile_count_seen = (size_t)s->tile_count;
    if (s->hidden_count >= 0 && (size_t)s->hidden_count > search->stats.max_hidden_count_seen)
        search->stats.max_hidden_count_seen = (size_t)s->hidden_count;
    for (int c = 0; c < s->poly.cycle_count; c++) {
        int n = s->poly.cycles[c].n;
        if (n < 0) continue;
        boundary_vertices += (size_t)n;
        if ((size_t)n > search->stats.max_cycle_vertices_seen)
            search->stats.max_cycle_vertices_seen = (size_t)n;
        if (n >= MAX_VERTS) search->stats.cycle_capacity_seen++;
    }
    if (boundary_vertices > search->stats.max_boundary_vertices_seen)
        search->stats.max_boundary_vertices_seen = boundary_vertices;
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

static int parse_coord0(const char **pp, Coord *q) {
    return expect_char0(pp, '(') &&
           parse_int0(pp, &q->v) &&
           expect_char0(pp, ',') &&
           parse_int0(pp, &q->x) &&
           expect_char0(pp, ',') &&
           parse_int0(pp, &q->y) &&
           expect_char0(pp, ')');
}

static int parse_cycle0(const char **pp, Cycle *cycle) {
    cycle->n = 0;
    if (!expect_char0(pp, '[')) return 0;
    skip_ws(pp);
    if (**pp == ']') { (*pp)++; return 1; }
    while (cycle->n < MAX_VERTS) {
        if (!parse_coord0(pp, &cycle->v[cycle->n])) return 0;
        cycle->n++;
        skip_ws(pp);
        if (**pp == ',') { (*pp)++; continue; }
        if (**pp == ']') { (*pp)++; return 1; }
        return 0;
    }
    return 0;
}

static int parse_poly0(const char *text, Poly *poly) {
    const char *p = text;
    poly->cycle_count = 0;
    if (!expect_char0(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') { p++; return 1; }
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

static int parse_cycle_list0(const char *text, Cycle *out, int *out_count) {
    const char *p = text;
    *out_count = 0;
    if (!expect_char0(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') { p++; return 1; }
    while (*out_count < ATTACH0_MAX_TILES) {
        if (!parse_cycle0(&p, &out[*out_count])) return 0;
        (*out_count)++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; skip_ws(&p); return *p == '\0' || *p == '\n' || *p == '\r'; }
        return 0;
    }
    return 0;
}

static int parse_coord_list0(const char *text, Coord *out, int *out_count) {
    const char *p = text;
    *out_count = 0;
    if (!expect_char0(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') { p++; return 1; }
    while (*out_count < BCOMP1_MAX_COORDS) {
        if (!parse_coord0(&p, &out[*out_count])) return 0;
        (*out_count)++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; skip_ws(&p); return *p == '\0' || *p == '\n' || *p == '\r'; }
        return 0;
    }
    return 0;
}


static int append_value_text(char *dst, size_t cap, size_t *off, const char *text) {
    size_t n = strlen(text);
    if (*off + n + 1 > cap) return 0;
    memcpy(dst + *off, text, n);
    *off += n;
    dst[*off] = '\0';
    return 1;
}

static int bracket_delta(const char *text, int *seen_bracket) {
    int d = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '[') { d++; *seen_bracket = 1; }
        else if (*p == ']') d--;
    }
    return d;
}

static int read_section_value(FILE *fp, const char *initial, char *dst, size_t cap) {
    enum { LINE_CAP = 262144 };
    char *line = malloc(LINE_CAP);
    size_t off = 0;
    int seen = 0;
    int balance = 0;
    int ok;
    if (!line) return 0;
    dst[0] = '\0';
    if (!append_value_text(dst, cap, &off, initial)) { free(line); return 0; }
    balance += bracket_delta(initial, &seen);
    while ((!seen || balance > 0) && fgets(line, LINE_CAP, fp)) {
        if (!append_value_text(dst, cap, &off, line)) { free(line); return 0; }
        balance += bracket_delta(line, &seen);
    }
    ok = seen && balance == 0;
    free(line);
    return ok;
}

static int parse_int_line0(const char *line, const char *prefix, int *out) {
    size_t n = strlen(prefix);
    const char *p;
    if (strncmp(line, prefix, n) != 0) return 0;
    p = line + n;
    return parse_int0(&p, out);
}

static void record_reset(BComp1Record *r) { memset(r, 0, sizeof(*r)); }

static int record_has_content(const BComp1Record *r) {
    return r->have_boundary || r->have_tiles || r->have_center;
}

static int records_push(BComp1RecordVec *v, const BComp1Record *r) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 64;
        BComp1Record *p = realloc(v->items, next * sizeof(*p));
        if (!p) return 0;
        v->items = p;
        v->cap = next;
    }
    v->items[v->count++] = *r;
    return 1;
}

static int load_records(const char *path, BComp1RecordVec *out) {
    enum { LINE_CAP = 262144, VALUE_CAP = 1048576 };
    FILE *fp = fopen(path, "r");
    char *line = NULL;
    char *value = NULL;
    BComp1Record *r = NULL;
    int ok = 0;
    if (!fp) return 0;
    line = malloc(LINE_CAP);
    value = malloc(VALUE_CAP);
    r = malloc(sizeof(*r));
    if (!line || !value || !r) goto done;
    record_reset(r);
    while (fgets(line, LINE_CAP, fp)) {
        if (strncmp(line, "---[", 4) == 0) {
            if (record_has_content(r)) {
                if (!records_push(out, r)) goto done;
            }
            record_reset(r);
            continue;
        }
        if (parse_int_line0(line, "level:", &r->level)) continue;
        if (parse_int_line0(line, "tile_count:", &r->tile_count)) continue;
        if (parse_int_line0(line, "start_index:", &r->start_index)) continue;
        if (strncmp(line, "direction:", 10) == 0) {
            r->dir = (strstr(line + 10, "cw") && !strstr(line + 10, "ccw")) ? -1 : 1;
            continue;
        }
        if (strncmp(line, "center_tile:", 12) == 0) {
            const char *p;
            if (!read_section_value(fp, line + 12, value, VALUE_CAP)) goto done;
            p = value;
            r->have_center = parse_cycle0(&p, &r->center);
            continue;
        }
        if (strncmp(line, "boundary:", 9) == 0) {
            if (!read_section_value(fp, line + 9, value, VALUE_CAP)) goto done;
            r->have_boundary = parse_poly0(value, &r->boundary);
            continue;
        }
        if (strncmp(line, "constellation:", 14) == 0) {
            if (!read_section_value(fp, line + 14, value, VALUE_CAP)) goto done;
            r->have_hidden = parse_coord_list0(value, r->hidden, &r->hidden_count);
            continue;
        }
        if (strncmp(line, "tiles:", 6) == 0) {
            if (!read_section_value(fp, line + 6, value, VALUE_CAP)) goto done;
            r->have_tiles = parse_cycle_list0(value, r->tiles, &r->tiles_count);
            continue;
        }
    }
    if (record_has_content(r)) {
        if (!records_push(out, r)) goto done;
    }
    ok = 1;

done:
    free(r);
    free(value);
    free(line);
    fclose(fp);
    return ok;
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int coord_in_list(const Coord *items, int count, Coord q) {
    for (int i = 0; i < count; i++) if (coord_eq(items[i], q)) return 1;
    return 0;
}

static int coord_on_boundary(Coord q, const Poly *p) {
    for (int c = 0; c < p->cycle_count; c++) {
        for (int i = 0; i < p->cycles[c].n; i++) if (coord_eq(p->cycles[c].v[i], q)) return 1;
    }
    return 0;
}

static int coord_on_tiles(Coord q, const Cycle *tiles, int tile_count);

static int collect_tile_vertices(const Cycle *tiles, int tile_count, Coord *verts) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (!coord_in_list(verts, n, tiles[t].v[i])) {
                if (n >= BCOMP1_MAX_COORDS) return -1;
                verts[n++] = tiles[t].v[i];
            }
        }
    }
    return n;
}

static int rebuild_hidden(const Poly *p, const Cycle *tiles, int tile_count, Coord *hidden) {
    Coord *all = malloc(sizeof(*all) * BCOMP1_MAX_COORDS);
    Coord *boundary = malloc(sizeof(*boundary) * BCOMP1_MAX_COORDS);
    int ac, bc;
    int hc = 0;
    if (!all || !boundary) { free(boundary); free(all); return -1; }
    ac = collect_tile_vertices(tiles, tile_count, all);
    bc = build_boundary_vertices(p, boundary);
    if (ac < 0 || bc < 0) { free(boundary); free(all); return -1; }
    for (int i = 0; i < ac; i++) {
        if (!coord_in_list(boundary, bc, all[i])) {
            if (hc >= BCOMP1_MAX_COORDS) { free(boundary); free(all); return -1; }
            if (hidden) hidden[hc] = all[i];
            hc++;
        }
    }
    if (hidden && hc > 1) qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp_local);
    free(boundary);
    free(all);
    return hc;
}

static int append_text(char **buf, size_t *cap, size_t *off, const char *text) {
    size_t n = strlen(text);
    if (*off + n + 1 > *cap) {
        size_t next = *cap ? *cap : 4096;
        while (*off + n + 1 > next) next *= 2;
        char *p = realloc(*buf, next);
        if (!p) return 0;
        *buf = p;
        *cap = next;
    }
    memcpy(*buf + *off, text, n);
    *off += n;
    (*buf)[*off] = '\0';
    return 1;
}

static int append_coord(char **buf, size_t *cap, size_t *off, Coord q) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "(%d,%d,%d)", q.v, q.x, q.y);
    return append_text(buf, cap, off, tmp);
}

static char *poly_to_string(const Poly *p) {
    char *buf = NULL;
    size_t cap = 0, off = 0;
    if (!append_text(&buf, &cap, &off, "[")) return NULL;
    for (int c = 0; c < p->cycle_count; c++) {
        if (c && !append_text(&buf, &cap, &off, "|")) goto fail;
        if (!append_text(&buf, &cap, &off, "[")) goto fail;
        for (int i = 0; i < p->cycles[c].n; i++) {
            if (i && !append_text(&buf, &cap, &off, ",")) goto fail;
            if (!append_coord(&buf, &cap, &off, p->cycles[c].v[i])) goto fail;
        }
        if (!append_text(&buf, &cap, &off, "]")) goto fail;
    }
    if (!append_text(&buf, &cap, &off, "]")) goto fail;
    return buf;
fail:
    free(buf);
    return NULL;
}

static void print_cycle(FILE *fp, const Cycle *c) {
    fprintf(fp, "[");
    for (int i = 0; i < c->n; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    fprintf(fp, "]");
}

static void print_coord_list(FILE *fp, const Coord *coords, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", coords[i].v, coords[i].x, coords[i].y);
    }
    fprintf(fp, "]");
}

static void print_tile_list(FILE *fp, const Cycle *tiles, int tile_count) {
    fprintf(fp, "[");
    for (int i = 0; i < tile_count; i++) {
        if (i) fprintf(fp, ",");
        print_cycle(fp, &tiles[i]);
    }
    fprintf(fp, "]");
}

static void print_record(FILE *fp, size_t index, const BComp1Record *r) {
    char *boundary = poly_to_string(&r->boundary);
    fprintf(fp, "---[%zu]---\n", index);
    fprintf(fp, "level:%d\n", r->level);
    fprintf(fp, "tile_count:%d\n", r->tile_count);
    fprintf(fp, "start_index:%d\n", r->start_index);
    fprintf(fp, "direction:%s\n", r->dir > 0 ? "ccw" : "cw");
    fprintf(fp, "center_tile:");
    print_cycle(fp, &r->center);
    fprintf(fp, "\n");
    fprintf(fp, "boundary:%s\n", boundary ? boundary : "[]");
    fprintf(fp, "constellation:");
    print_coord_list(fp, r->hidden, r->hidden_count);
    fprintf(fp, "\n");
    fprintf(fp, "tiles:");
    print_tile_list(fp, r->tiles, r->tiles_count);
    fprintf(fp, "\n");
    free(boundary);
}

static int make_seed_state(const Tile *tile, BComp1State *s) {
    memset(s, 0, sizeof(*s));
    s->poly.cycle_count = 1;
    s->poly.cycles[0] = tile->base;
    s->tile_count = 1;
    s->tiles[0] = tile->base;
    s->hidden_count = rebuild_hidden(&s->poly, s->tiles, s->tile_count, NULL);
    return s->hidden_count >= 0;
}

static int state_from_record(const BComp1Record *r, BComp1State *s) {
    if (!r->have_boundary || !r->have_tiles || !r->have_center) return 0;
    memset(s, 0, sizeof(*s));
    s->poly = r->boundary;
    s->tile_count = r->tiles_count;
    if (s->tile_count < 0 || s->tile_count > ATTACH0_MAX_TILES) return 0;
    for (int i = 0; i < s->tile_count; i++) s->tiles[i] = r->tiles[i];
    s->hidden_count = rebuild_hidden(&s->poly, s->tiles, s->tile_count, NULL);
    return s->hidden_count >= 0;
}

static int dictionary_choices_at(const Tile *tile,
                                 const RL0ForgetMap *map,
                                 const BComp1State *s,
                                 Coord target,
                                 RL0FMArc *key,
                                 const RL0FMArc **values,
                                 int *value_count) {
    if (!boundary0_build_vertex_arc(tile, s->tiles, s->tile_count, target, key)) return 0;
    if (!rl0_fm_lookup_any_rotation(map, key, values, value_count, NULL)) return 0;
    return *value_count > 0;
}

static int nonempty_choice_count(const RL0FMArc *values, int value_count) {
    int n = 0;
    for (int i = 0; i < value_count; i++) if (values[i].n > 0) n++;
    return n;
}

static int push_coord_unique(Coord *items, int *count, Coord q) {
    if (coord_in_list(items, *count, q)) return 1;
    if (*count >= BCOMP1_MAX_COORDS) return 0;
    items[(*count)++] = q;
    return 1;
}

static int check_touched_after_attachment(B1Search *search,
                                          const BComp1State *before,
                                          const BComp1State *after,
                                          const Cycle *added_tiles,
                                          int added_count) {
    Coord touched[BCOMP1_MAX_COORDS];
    int touched_count = 0;
    for (int t = 0; t < added_count; t++) {
        for (int i = 0; i < added_tiles[t].n; i++) {
            if (!push_coord_unique(touched, &touched_count, added_tiles[t].v[i])) return 0;
        }
    }
    for (int c = 0; c < before->poly.cycle_count; c++) {
        const Cycle *cycle = &before->poly.cycles[c];
        for (int i = 0; i < cycle->n; i++) {
            Coord q = cycle->v[i];
            if (!coord_on_boundary(q, &after->poly) && coord_on_tiles(q, after->tiles, after->tile_count)) {
                if (!push_coord_unique(touched, &touched_count, q)) return 0;
            }
        }
    }
    for (int i = 0; i < touched_count; i++) {
        RL0FMArc complete;
        Coord q = touched[i];
        if (coord_on_boundary(q, &after->poly)) continue;
        if (!coord_on_tiles(q, after->tiles, after->tile_count)) continue;
        if (!boundary0_build_vertex_arc(search->tile, after->tiles, after->tile_count, q, &complete)) return -1;
        if (!rl0_fm_contains_complete(search->map, &complete)) return -1;
    }
    if (!search->live_only) return 1;
    for (int i = 0; i < touched_count; i++) {
        Coord q = touched[i];
        if (!coord_on_boundary(q, &after->poly)) continue;
        if (!boundary0_vertex_has_dictionary_completion(search->tile, after->tiles, after->tile_count, q, search->map)) return -2;
    }
    return 1;
}

static int state_after_choice(B1Search *search,
                              const BComp1State *s,
                              Coord target,
                              const RL0FMArc *choice,
                              BComp1State *out) {
    Cycle grown_tiles[ATTACH0_MAX_TILES];
    int grown_tile_count = 0;
    Attach0Stats astats;
    const Cycle *added_tiles;
    int added_count;
    attach0_stats_init(&astats);
    out->hidden_count = s->hidden_count;
    if (!attach0_try_attach_arc(&s->poly,
                                search->tile,
                                s->tiles,
                                s->tile_count,
                                target,
                                choice,
                                &out->poly,
                                grown_tiles,
                                &grown_tile_count,
                                &astats)) {
        return 0;
    }
    if (grown_tile_count < 0 || grown_tile_count > ATTACH0_MAX_TILES) {
        search->stats.attach_tile_capacity_hit++;
        return 0;
    }
    if (grown_tile_count > search->max_tiles) {
        search->stats.attach_tile_limit_hit++;
        return 0;
    }
    out->tile_count = grown_tile_count;
    for (int i = 0; i < grown_tile_count; i++) out->tiles[i] = grown_tiles[i];
    stats_observe_state(search, out);
    added_tiles = out->tiles + s->tile_count;
    added_count = out->tile_count - s->tile_count;
    return check_touched_after_attachment(search, s, out, added_tiles, added_count);
}


static int original_boundary_gone(const BComp1State *s, const Cycle *original) {
    for (int i = 0; i < original->n; i++) {
        if (coord_on_boundary(original->v[i], &s->poly)) return 0;
    }
    return 1;
}

static int coord_on_tiles(Coord q, const Cycle *tiles, int tile_count) {
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (coord_eq(tiles[t].v[i], q)) return 1;
        }
    }
    return 0;
}


static void debug_print_constants(FILE *fp, const Tile *tile) {
    fprintf(fp, "(");
    for (int i = 0; i < tile->constant_count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "%s=%s", tile->constants[i].name, tile->constants[i].expr);
    }
    fprintf(fp, ")");
}

static void debug_print_basis(FILE *fp, const Tile *tile) {
    fprintf(fp, "(");
    for (int i = 0; i < tile->basis_count; i++) {
        const TileBasis *b = &tile->bases[i];
        if (i) fprintf(fp, ",");
        fprintf(fp, "%d:%s,%s;%s,%s", b->valence, b->a11, b->a12, b->a21, b->a22);
    }
    fprintf(fp, ")");
}

static void debug_print_cycle_raw(FILE *fp, const Cycle *c) {
    fprintf(fp, "(");
    for (int i = 0; i < c->n; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "%d %d %d", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    fprintf(fp, ")");
}

static void debug_print_imgtable_shape(FILE *fp, const Tile *tile, const Poly *poly) {
    fprintf(fp, "[ %d | ", poly->cycle_count > 1 ? 1 : 0);
    debug_print_constants(fp, tile);
    fprintf(fp, " | ");
    debug_print_basis(fp, tile);
    for (int i = 0; i < poly->cycle_count; i++) {
        fprintf(fp, " | ");
        debug_print_cycle_raw(fp, &poly->cycles[i]);
    }
    fprintf(fp, " ]\n");
}

static void debug_print_cycle_shape(FILE *fp, const Tile *tile, const Cycle *cycle) {
    Poly p;
    p.cycle_count = 1;
    p.cycles[0] = *cycle;
    debug_print_imgtable_shape(fp, tile, &p);
}

static void debug_emit_exception(B1Search *search,
                                 const char *kind,
                                 const BComp1State *after,
                                 const Cycle *original,
                                 const Cycle *added_tiles,
                                 int added_count,
                                 const int *candidate_indices,
                                 int candidate_count) {
    FILE *fp = search->debug_exceptions;
    if (!fp) return;
    if (search->debug_exceptions_count >= search->debug_exceptions_limit) return;
    search->debug_exceptions_count++;
    fprintf(fp, "[%d]\n", search->debug_exceptions_count);
    fprintf(fp, "Kind %s\n", kind);
    fprintf(fp, "Aggregate\n");
    debug_print_imgtable_shape(fp, search->tile, &after->poly);
    fprintf(fp, "OriginalBoundary\n");
    debug_print_cycle_shape(fp, search->tile, original);
    fprintf(fp, "AddedTiles\n");
    for (int i = 0; i < added_count; i++) debug_print_cycle_shape(fp, search->tile, &added_tiles[i]);
    fprintf(fp, "Tiles\n");
    for (int i = 0; i < after->tile_count; i++) debug_print_cycle_shape(fp, search->tile, &after->tiles[i]);
    fprintf(fp, "Hidden\n");
    {
        Coord *hidden = malloc(sizeof(*hidden) * BCOMP1_MAX_COORDS);
        int hidden_count = hidden ? rebuild_hidden(&after->poly, after->tiles, after->tile_count, hidden) : -1;
        for (int i = 0; i < hidden_count; i++) {
            fprintf(fp, "(%d,%d,%d)\n", hidden[i].v, hidden[i].x, hidden[i].y);
        }
        free(hidden);
    }
    fprintf(fp, "Candidates\n");
    for (int i = 0; i < candidate_count; i++) {
        Coord q = original->v[candidate_indices[i]];
        fprintf(fp, "(%d,%d,%d)\n", q.v, q.x, q.y);
    }
}

static int boundary_live(B1Search *search, const BComp1State *s) {
    Boundary0Stats bstats;
    boundary0_stats_init(&bstats);
    return boundary0_poly_has_live_boundary(&s->poly,
                                            search->tile,
                                            s->tiles,
                                            s->tile_count,
                                            search->map,
                                            &bstats);
}

static int next_port_from_attachment(B1Search *search,
                                     const BComp1State *after,
                                     const Cycle *original,
                                     const Cycle *added_tiles,
                                     int added_count,
                                     int prior_index,
                                     int dir,
                                     int first_step,
                                     int *next_index_out) {
    int found[8];
    int found_count = 0;
    int n = original->n;
    for (int i = 0; i < n; i++) {
        Coord q = original->v[i];
        if (!coord_on_boundary(q, &after->poly)) continue;
        if (!coord_on_tiles(q, added_tiles, added_count)) continue;
        if (found_count < (int)(sizeof(found) / sizeof(found[0]))) found[found_count] = i;
        found_count++;
    }
    if (found_count == 0) {
        if (original_boundary_gone(after, original)) {
            search->stats.walker_completed++;
            *next_index_out = -1;
            return 2;
        }
        search->stats.walker_zero_not_done++;
        return 0;
    }
    if (found_count == 1) {
        *next_index_out = found[0];
        return 1;
    }

    if (first_step) {
        int best_idx = -1;
        int best_dist = n + 1;
        for (int i = 0; i < found_count && i < (int)(sizeof(found) / sizeof(found[0])); i++) {
            int dist;
            if (dir > 0) dist = (found[i] - prior_index + n) % n;
            else dist = (prior_index - found[i] + n) % n;
            if (dist > 0 && dist < best_dist) {
                best_dist = dist;
                best_idx = found[i];
            }
        }
        if (best_idx >= 0) {
            *next_index_out = best_idx;
            return 1;
        }
    }

    search->stats.walker_multi_port++;
    search->stats.exceptions++;
    if (after->poly.cycle_count > 1) {
        if (!search->live_only && !boundary_live(search, after)) {
            search->stats.filtered_dead++;
            debug_emit_exception(search,
                                 "hole_dead",
                                 after,
                                 original,
                                 added_tiles,
                                 added_count,
                                 found,
                                 found_count < (int)(sizeof(found) / sizeof(found[0])) ? found_count : (int)(sizeof(found) / sizeof(found[0])));
            return 0;
        }
        debug_emit_exception(search,
                             "hole_live_multi",
                             after,
                             original,
                             added_tiles,
                             added_count,
                             found,
                             found_count < (int)(sizeof(found) / sizeof(found[0])) ? found_count : (int)(sizeof(found) / sizeof(found[0])));
    } else {
        debug_emit_exception(search,
                             "multi_no_hole",
                             after,
                             original,
                             added_tiles,
                             added_count,
                             found,
                             found_count < (int)(sizeof(found) / sizeof(found[0])) ? found_count : (int)(sizeof(found) / sizeof(found[0])));
    }
    return 0;
}

static int score_at(const Tile *tile,
                    const RL0ForgetMap *map,
                    const BComp1State *s,
                    Coord q) {
    RL0FMArc key;
    const RL0FMArc *values = NULL;
    int value_count = 0;
    if (!coord_on_boundary(q, &s->poly)) return 1000000;
    if (!dictionary_choices_at(tile, map, s, q, &key, &values, &value_count)) return 1000000;
    return nonempty_choice_count(values, value_count);
}

static void choose_schedule_start(const Tile *tile,
                                  const RL0ForgetMap *map,
                                  const BComp1State *s,
                                  const Cycle *sched,
                                  int *start_out,
                                  int *dir_out) {
    int best_idx = 0;
    int best_dir = 1;
    int best[BCOMP1_SCORE_N];
    int limit = sched->n < BCOMP1_SCORE_N ? sched->n : BCOMP1_SCORE_N;
    for (int k = 0; k < BCOMP1_SCORE_N; k++) best[k] = 1000000;
    for (int base = 0; base < sched->n; base++) {
        for (int dcase = 0; dcase < 2; dcase++) {
            int dir = dcase == 0 ? 1 : -1;
            int cur[BCOMP1_SCORE_N];
            int better = 0;
            for (int k = 0; k < limit; k++) {
                int idx = (base + dir * k) % sched->n;
                if (idx < 0) idx += sched->n;
                cur[k] = score_at(tile, map, s, sched->v[idx]);
            }
            for (int k = 0; k < limit; k++) {
                if (cur[k] < best[k]) { better = 1; break; }
                if (cur[k] > best[k]) break;
            }
            if (better) {
                for (int k = 0; k < limit; k++) best[k] = cur[k];
                best_idx = base;
                best_dir = dir;
            }
        }
    }
    *start_out = best_idx;
    *dir_out = best_dir;
}

static int final_record_from_state(const BComp1State *s,
                                   const Cycle *center,
                                   const Coord *hidden,
                                   int hidden_count,
                                   int start,
                                   int dir,
                                   BComp1Record *r) {
    memset(r, 0, sizeof(*r));
    r->level = hidden_count;
    r->tile_count = s->tile_count;
    r->start_index = start;
    r->dir = dir;
    r->have_center = 1;
    r->have_boundary = 1;
    r->have_hidden = 1;
    r->have_tiles = 1;
    r->center = *center;
    r->boundary = s->poly;
    r->hidden_count = hidden_count;
    r->tiles_count = s->tile_count;
    for (int i = 0; i < hidden_count; i++) r->hidden[i] = hidden[i];
    for (int i = 0; i < s->tile_count; i++) r->tiles[i] = s->tiles[i];
    return 1;
}

static int emit_final(B1Search *search,
                      const BComp1State *s,
                      const Cycle *center,
                      int start,
                      int dir) {
    Coord *hidden = malloc(sizeof(*hidden) * BCOMP1_MAX_COORDS);
    int hidden_count;
    Poly canonical;
    if (!hidden) return 0;
    hidden_count = rebuild_hidden(&s->poly, s->tiles, s->tile_count, hidden);
    if (hidden_count < 0) {
        free(hidden);
        search->stats.hidden_rebuild_fail++;
        return 1;
    }
    if ((size_t)hidden_count > search->stats.max_hidden_count_seen)
        search->stats.max_hidden_count_seen = (size_t)hidden_count;
    poly_hash_key_lattice(&s->poly, search->tile->lattice, &canonical);
    if (!hash_insert(&search->seen, &canonical)) {
        free(hidden);
        search->stats.duplicates++;
        return 1;
    }
    search->stats.outputs++;
    if (search->stop_after_output) search->stop_requested = 1;
    if (search->print_records) {
        BComp1Record *rec = malloc(sizeof(*rec));
        if (!rec) { free(hidden); return 0; }
        if (!final_record_from_state(s, center, hidden, hidden_count, start, dir, rec)) { free(rec); free(hidden); return 0; }
        if (!records_push(&search->finals, rec)) { free(rec); free(hidden); return 0; }
        free(rec);
    }
    free(hidden);
    return 1;
}


static int complete_rings(B1Search *search,
                          const BComp1State *s,
                          const Cycle *center,
                          int rings_left,
                          int depth_seen,
                          int meta_start,
                          int meta_dir);

static int dfs_frontier(B1Search *search,
                        const BComp1State *s,
                        const Cycle *center,
                        const Cycle *original,
                        int target_index,
                        int dir,
                        int rings_left,
                        int depth_seen,
                        int start_index,
                        int first_step,
                        int tree_depth) {
    RL0FMArc key;
    const RL0FMArc *values = NULL;
    int value_count = 0;
    Coord target;
    if (search->stop_requested) return 1;
    if (search->max_dfs && search->stats.dfs_calls >= search->max_dfs) {
        search->stats.truncated++;
        return 1;
    }
    search->stats.dfs_calls++;
    progress_render(search, 0);
    stats_observe_state(search, s);
    if ((size_t)depth_seen > search->stats.max_depth_seen) search->stats.max_depth_seen = (size_t)depth_seen;
    if (target_index < 0 || target_index >= original->n) return 1;
    if (original_boundary_gone(s, original)) {
        search->stats.walker_completed++;
        return complete_rings(search, s, center, rings_left - 1, depth_seen + 1, start_index, dir);
    }
    if (s->tile_count >= search->max_tiles) {
        search->stats.max_tiles_stop++;
        return 1;
    }
    target = original->v[target_index];
    if (!coord_on_boundary(target, &s->poly)) {
        search->stats.walker_zero_not_done++;
        return 1;
    }
    if (!dictionary_choices_at(search->tile, search->map, s, target, &key, &values, &value_count)) {
        search->stats.no_dictionary++;
        return 1;
    }
    for (int i = 0; i < value_count && !search->stop_requested; i++) {
        BComp1State *next;
        const Cycle *added_tiles;
        int added_count;
        int next_index = -1;
        int pr;
        int r;
        if (values[i].n <= 0) continue;
        search->path_depth = tree_depth + 1;
        search->path_index[tree_depth] = i + 1;
        search->path_total[tree_depth] = value_count;
        if (search->progress_tty && tree_depth < search->progress_depth) progress_render(search, 1);
        next = malloc(sizeof(*next));
        if (!next) return 0;
        search->stats.attach_attempts++;
        r = state_after_choice(search, s, target, &values[i], next);
        if (r == -1) {
            search->stats.filtered_illegal++;
            free(next);
            continue;
        }
        if (r == -2) {
            search->stats.filtered_dead++;
            free(next);
            continue;
        }
        if (r == 0) {
            free(next);
            continue;
        }
        search->stats.attach_successes++;
        added_tiles = next->tiles + s->tile_count;
        added_count = next->tile_count - s->tile_count;
        if (added_count <= 0) {
            search->stats.walker_zero_not_done++;
            free(next);
            continue;
        }
        pr = next_port_from_attachment(search,
                                       next,
                                       original,
                                       added_tiles,
                                       added_count,
                                       target_index,
                                       dir,
                                       first_step,
                                       &next_index);
        if (pr == 2) {
            if (!complete_rings(search, next, center, rings_left - 1, depth_seen + 1, start_index, dir)) {
                free(next);
                return 0;
            }
        } else if (pr == 1) {
            search->path_depth = tree_depth + 1;
            search->path_index[tree_depth] = i + 1;
            search->path_total[tree_depth] = value_count;
            if (!dfs_frontier(search,
                              next,
                              center,
                              original,
                              next_index,
                              dir,
                              rings_left,
                              depth_seen,
                              start_index,
                              0,
                              tree_depth + 1)) {
                free(next);
                return 0;
            }
        }
        free(next);
    }
    return 1;
}

static int complete_rings(B1Search *search,
                          const BComp1State *s,
                          const Cycle *center,
                          int rings_left,
                          int depth_seen,
                          int meta_start,
                          int meta_dir) {
    Cycle original;
    int start = 0;
    int dir = 1;
    if (search->stop_requested) return 1;
    if (rings_left <= 0) return emit_final(search, s, center, meta_start, meta_dir);
    if (s->poly.cycle_count <= 0) return emit_final(search, s, center, meta_start, meta_dir);
    original = s->poly.cycles[0];
    if (s->tile_count == 1 && depth_seen == 0) {
        for (int i = 0; i < original.n; i++) {
            if (score_at(search->tile, search->map, s, original.v[i]) >= 1000000) continue;
            for (int dcase = 0; dcase < 2 && !search->stop_requested; dcase++) {
                int d = dcase == 0 ? 1 : -1;
                search->path_depth = 1;
                search->path_index[0] = dcase + 1;
                search->path_total[0] = 2;
                if (!dfs_frontier(search, s, center, &original, i, d, rings_left, depth_seen, i, 1, 1)) return 0;
            }
        }
        return 1;
    }
    choose_schedule_start(search->tile, search->map, s, &original, &start, &dir);
    search->path_depth = 1;
    search->path_index[0] = 1;
    search->path_total[0] = 1;
    return dfs_frontier(search, s, center, &original, start, dir, rings_left, depth_seen, start, 1, 1);
}

static int complete_from_state(B1Search *search, const BComp1State *seed, const Cycle *center) {
    return complete_rings(search, seed, center, search->depth, 0, 0, 1);
}

static int record_boundary_cmp(const void *A, const void *B) {
    const BComp1Record *a = A;
    const BComp1Record *b = B;
    char *as = poly_to_string(&a->boundary);
    char *bs = poly_to_string(&b->boundary);
    int r = 0;
    if (!as || !bs) r = 0;
    else r = strcmp(as, bs);
    free(as);
    free(bs);
    if (r) return r;
    if (a->tile_count != b->tile_count) return a->tile_count - b->tile_count;
    return a->level - b->level;
}

static int loaded_deletion_level(const RL0FMDeletionSet *deletions) {
    int level = -1;
    if (!deletions) return -1;
    for (int i = 0; i < deletions->count; i++) {
        if (deletions->level[i] > level) level = deletions->level[i];
    }
    return level;
}

static int load_rl0_map(RL0ForgetMap *map,
                        const char *remembrance_path,
                        const char *deletions_path) {
    RL0FMDeletionSet deletions;
    int level;
    rl0_fm_init(map);
    if (!deletions_path) {
        return rl0_fm_load_remembrance_filtered(map, remembrance_path, NULL, -1);
    }
    rl0_fm_deletions_init(&deletions);
    if (!rl0_fm_load_deletions(&deletions, deletions_path)) {
        rl0_fm_deletions_clear(&deletions);
        return 0;
    }
    level = loaded_deletion_level(&deletions);
    if (!rl0_fm_load_remembrance_filtered(map, remembrance_path, &deletions, level)) {
        rl0_fm_deletions_clear(&deletions);
        return 0;
    }
    rl0_fm_deletions_clear(&deletions);
    return 1;
}


void bcomp1_options_default(BComp1Options *opts) {
    opts->depth = 1;
    opts->max_tiles = ATTACH0_MAX_TILES;
    opts->live_only = 1;
    opts->collect_records = 0;
    opts->max_dfs = 0;
    opts->debug_exceptions_path = NULL;
    opts->debug_exceptions_limit = 16;
    opts->progress_interval = 0;
    opts->progress_depth = 8;
    opts->progress_tty = 1;
    opts->progress_label = NULL;
    opts->stop_after_output = 0;
}

int bcomp1_context_init(BComp1Context *ctx,
                        const char *tile_path,
                        const char *remembrance_path,
                        const char *deletions_path) {
    memset(ctx, 0, sizeof(*ctx));
    if (!tile_load(tile_path, &ctx->tile)) return 0;
    return load_rl0_map(&ctx->map, remembrance_path, deletions_path);
}

void bcomp1_context_clear(BComp1Context *ctx) {
    rl0_fm_clear(&ctx->map);
}

int bcomp1_load_records(const char *path, BComp1RecordVec *out) {
    memset(out, 0, sizeof(*out));
    return load_records(path, out);
}

void bcomp1_free_records(BComp1RecordVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->count = vec->cap = 0;
}

int bcomp1_make_seed_state(const BComp1Context *ctx, BComp1State *state) {
    return make_seed_state(&ctx->tile, state);
}

int bcomp1_state_from_record(const BComp1Record *record, BComp1State *state) {
    return state_from_record(record, state);
}

int bcomp1_complete_state(const BComp1Context *ctx,
                          const BComp1State *seed,
                          const Cycle *center,
                          const BComp1Options *opts,
                          BComp1Result *result) {
    B1Search search;
    memset(result, 0, sizeof(*result));
    memset(&search, 0, sizeof(search));
    search.tile = &ctx->tile;
    search.map = &ctx->map;
    search.depth = opts->depth < 1 ? 1 : opts->depth;
    search.max_tiles = opts->max_tiles;
    if (search.max_tiles < 1) search.max_tiles = 1;
    if (search.max_tiles > ATTACH0_MAX_TILES) search.max_tiles = ATTACH0_MAX_TILES;
    search.live_only = opts->live_only;
    search.print_records = opts->collect_records;
    search.max_dfs = opts->max_dfs;
    search.debug_exceptions_limit = opts->debug_exceptions_limit < 0 ? 0 : opts->debug_exceptions_limit;
    search.progress_interval = opts->progress_interval;
    search.progress_depth = opts->progress_depth <= 0 ? 8 : opts->progress_depth;
    search.progress_tty = opts->progress_tty && isatty(2);
    search.progress_label = opts->progress_label;
    search.stop_after_output = opts->stop_after_output;
    if (opts->debug_exceptions_path) {
        search.debug_exceptions = fopen(opts->debug_exceptions_path, "w");
        if (!search.debug_exceptions) return 0;
    }
    stats_init(&search.stats);
    hash_init(&search.seen, 65536);
    stats_observe_state(&search, seed);
    if (!complete_from_state(&search, seed, center)) {
        if (search.debug_exceptions) fclose(search.debug_exceptions);
        hash_destroy(&search.seen);
        free(search.finals.items);
        return 0;
    }
    progress_render(&search, 1);
    result->stats = search.stats;
    result->records = search.finals;
    if (search.debug_exceptions) fclose(search.debug_exceptions);
    hash_destroy(&search.seen);
    return 1;
}

void bcomp1_result_clear(BComp1Result *result) {
    bcomp1_free_records(&result->records);
    memset(&result->stats, 0, sizeof(result->stats));
}

void bcomp1_sort_records(BComp1RecordVec *records) {
    qsort(records->items, records->count, sizeof(*records->items), record_boundary_cmp);
}

void bcomp1_print_record(FILE *fp, size_t index, const BComp1Record *record) {
    print_record(fp, index, record);
}
