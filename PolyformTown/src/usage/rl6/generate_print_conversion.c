#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_FIGS 2048
#define MAX_VALID 64
#define TOKEN 8

typedef struct { char t[3][TOKEN]; } Triple;
typedef struct { char row[3][6][TOKEN]; } AtlasFigure;

typedef struct { AtlasFigure fig[MAX_FIGS]; int n; } Atlas;

static void rotate3(const Triple *in, int k, Triple *out) {
    for (int i = 0; i < 3; i++) snprintf(out->t[i], TOKEN, "%s", in->t[(i + k) % 3]);
}
static int triple_cmp(const Triple *a, const Triple *b) {
    for (int i = 0; i < 3; i++) { int c = strcmp(a->t[i], b->t[i]); if (c) return c; }
    return 0;
}
static Triple canon3(Triple in) {
    Triple best = in, r;
    for (int k = 1; k < 3; k++) { rotate3(&in, k, &r); if (triple_cmp(&r, &best) < 0) best = r; }
    return best;
}
static char quotient(char c) { return c == 'B' ? 'A' : c == 'D' ? 'C' : c == 'I' ? 'H' : c; }
static int find_valid(const Triple *valid, int n, const Triple *q) {
    for (int i = 0; i < n; i++) if (!triple_cmp(&valid[i], q)) return i;
    return -1;
}
static int parse_refined_figures(const char *path, Triple *out, int *n) {
    FILE *fp = fopen(path, "r"); char line[256]; int in = 0; *n = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strncmp(line, "---[valid vertex triples]---", 28)) { in = 1; continue; }
        if (in && !strncmp(line, "---[", 4)) break;
        if (!in || line[0] == '#' || line[0] == '\n') continue;
        char a[TOKEN], b[TOKEN], c[TOKEN];
        if (sscanf(line, " %7[^,], %7[^,], %7s", a, b, c) == 3) {
            Triple t = {{{0}}}; snprintf(t.t[0], TOKEN, "%s", a); snprintf(t.t[1], TOKEN, "%s", b); snprintf(t.t[2], TOKEN, "%s", c);
            t = canon3(t); if (find_valid(out, *n, &t) < 0 && *n < MAX_VALID) out[(*n)++] = t;
        }
    }
    fclose(fp); return 1;
}
static int parse_basic_map(const char *path, char index_map[16][TOKEN], char letter_map[16]) {
    FILE *fp = fopen(path, "r"); char line[256]; int in = 0, n = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strncmp(line, "---[basic]---", 13)) { in = 1; continue; }
        if (in && !strncmp(line, "---[", 4)) break;
        if (!in || line[0] == '#' || line[0] == '\n') continue;
        char letter = 0, vals[160] = {0};
        if (sscanf(line, " %c: %159[^\n]", &letter, vals) == 2) {
            char *save = NULL; for (char *p = strtok_r(vals, ", ", &save); p; p = strtok_r(NULL, ", ", &save)) {
                if (n >= 16) { fclose(fp); return 0; }
                snprintf(index_map[n], TOKEN, "%s", p); letter_map[n++] = letter;
            }
        }
    }
    fclose(fp); return n;
}
static char letter_for_index(char index_map[16][TOKEN], char letter_map[16], int n, const char *idx) {
    for (int i = 0; i < n; i++) if (!strcmp(index_map[i], idx)) return letter_map[i];
    return 0;
}
static int parse_atlas_line(const char *line, AtlasFigure *out) {
    const char *p = line; int group = 0;
    while (group < 3 && (p = strchr(p, '[')) != NULL) {
        const char *end = strchr(p, ']'); if (!end) return 0;
        char buf[256]; size_t len = (size_t)(end - p - 1); if (len >= sizeof(buf)) return 0;
        memcpy(buf, p + 1, len); buf[len] = 0;
        char *save = NULL; int slot = 0;
        for (char *tok = strtok_r(buf, " ", &save); tok && slot < 6; tok = strtok_r(NULL, " ", &save)) {
            char *comma = strchr(tok, ','); if (!comma) return 0; *comma = 0;
            snprintf(out->row[group][slot++], TOKEN, "%s", tok);
        }
        if (slot != 6) return 0;
        group++; p = end + 1;
    }
    return group == 3;
}
static int read_atlas(const char *command, Atlas *atlas) {
    FILE *fp = popen(command, "r"); char line[1024]; int marker = 0; atlas->n = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "---[cycle vertex figures dump]---")) { marker = 1; continue; }
        if (marker && line[0] == '(') {
            if (atlas->n >= MAX_FIGS || !parse_atlas_line(line, &atlas->fig[atlas->n])) { pclose(fp); return 0; }
            atlas->n++;
        }
    }
    int rc = pclose(fp); return marker && rc == 0;
}
static Triple projected_unified(const AtlasFigure *f, int slot) {
    Triple t = {{{0}}};
    for (int i = 0; i < 3; i++) { t.t[i][0] = quotient(f->row[i][slot][0]); t.t[i][1] = 0; }
    return canon3(t);
}
static Triple projected_basic(const AtlasFigure *f, int slot, char index_map[16][TOKEN], char letter_map[16], int nmap) {
    Triple t = {{{0}}};
    for (int i = 0; i < 3; i++) { char l = letter_for_index(index_map, letter_map, nmap, f->row[i][slot]); t.t[i][0] = quotient(l); t.t[i][1] = 0; }
    return canon3(t);
}
static Triple indexed_basic(const AtlasFigure *f, int slot) {
    Triple t = {{{0}}}; for (int i = 0; i < 3; i++) snprintf(t.t[i], TOKEN, "%s", f->row[i][slot]); return canon3(t);
}
static int make_parent_dirs(const char *path) {
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s", path); char *slash = strrchr(tmp, '/'); if (!slash) return 1; *slash = 0;
    char parent[512]; snprintf(parent, sizeof(parent), "%s", tmp); slash = strrchr(parent, '/');
    if (slash) { *slash = 0; if (mkdir(parent, 0775) && errno != EEXIST) return 0; }
    return !mkdir(tmp, 0775) || errno == EEXIST;
}
int main(int argc, char **argv) {
    const char *outpath = argc > 1 ? argv[1] : "data/rl6/hat/print_conversion.dat";
    Triple valid[MAX_VALID]; int nvalid = 0; char idxmap[16][TOKEN] = {{0}}, lettermap[16] = {0};
    Atlas unified, basic;
    if (!parse_refined_figures("data/rl6/refined_model.dat", valid, &nvalid) || !nvalid) { fprintf(stderr, "cannot read refined figures\n"); return 1; }
    int nmap = parse_basic_map("data/rl6/unified_model.dat", idxmap, lettermap); if (nmap <= 0) { fprintf(stderr, "cannot read basic map\n"); return 1; }
    if (!read_atlas("RL6_DUMP_CYCLE_FIGS=1 ./bin/rl6_reduce --model unified", &unified) ||
        !read_atlas("RL6_DUMP_CYCLE_FIGS=1 ./bin/rl6_reduce --model basic", &basic)) { fprintf(stderr, "cannot compute RL6 atlas dump\n"); return 1; }
    int viable[6] = {0};
    for (int s = 0; s < 6; s++) { viable[s] = 1;
        for (int i = 0; i < unified.n; i++) { Triple t = projected_unified(&unified.fig[i], s); if (find_valid(valid, nvalid, &t) < 0) { viable[s] = 0; break; } }
    }
    int slot = -1, count = 0; for (int s = 0; s < 6; s++) if (viable[s]) { slot = s; count++; }
    if (count != 1 || slot != 1) { fprintf(stderr, "expected unique meeting slot 1\n"); return 1; }
    if (!make_parent_dirs(outpath)) { fprintf(stderr, "cannot create output directory\n"); return 1; }
    FILE *out = fopen(outpath, "w"); if (!out) { fprintf(stderr, "cannot write %s\n", outpath); return 1; }
    fprintf(out, "# Mountain and Range refined vertex -> ordinary/basic hat-index print conversion\n");
    fprintf(out, "# GENERATED DATA: do not edit; regenerate with `make boot` or `make rl6_print_conversion`.\n");
    fprintf(out, "# Inputs: data/rl5/hexagons.dat, data/rl6/unified_model.dat,\n");
    fprintf(out, "#         data/rl6/refined_model.dat, and RL6 closed-cycle atlas computation.\n");
    fprintf(out, "# Cyclic rows are CCW; all cyclic rotations are equivalent.\n");
    fprintf(out, "# Verified atlas meeting vertex slot = %d.\n", slot);
    int unsupported = 0; char unsupported_key[MAX_VALID][4];
    for (int v = 0; v < nvalid; v++) {
        Triple choices[MAX_FIGS]; int nchoice = 0;
        for (int i = 0; i < basic.n; i++) { Triple r = projected_basic(&basic.fig[i], slot, idxmap, lettermap, nmap); if (triple_cmp(&r, &valid[v])) continue; Triple b = indexed_basic(&basic.fig[i], slot); int seen = 0; for (int j = 0; j < nchoice; j++) if (!triple_cmp(&b, &choices[j])) seen = 1; if (!seen) choices[nchoice++] = b; }
        if (!nchoice) { unsupported_key[unsupported][0] = valid[v].t[0][0]; unsupported_key[unsupported][1] = valid[v].t[1][0]; unsupported_key[unsupported][2] = valid[v].t[2][0]; unsupported_key[unsupported][3] = 0; unsupported++; if (strcmp(valid[v].t[0], "G") || strcmp(valid[v].t[1], "G") || strcmp(valid[v].t[2], "G")) { fclose(out); fprintf(stderr, "unexpected unsupported figure\n"); return 1; } continue; }
        if (nchoice != 1) { fclose(out); fprintf(stderr, "ambiguous ordinary lift\n"); return 1; }
        fprintf(out, "%s%s%s = %s %s %s\n", valid[v].t[0], valid[v].t[1], valid[v].t[2], choices[0].t[0], choices[0].t[1], choices[0].t[2]);
    }
    for (int i = 0; i < unsupported; i++) fprintf(out, "# unresolved: %s\n", unsupported_key[i]);
    fclose(out); if (unsupported != 1) { fprintf(stderr, "expected exactly one unsupported figure\n"); return 1; }
    printf("%s\n", outpath); return 0;
}
