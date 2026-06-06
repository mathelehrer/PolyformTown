#include "rl7/inflation7.h"
#include "usage/rl7/rephex_hat.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum { PALETTE_ORDINARY, PALETTE_TREE, PALETTE_SPLIT } Palette;

#define CURRENT_SVG_PATH       "img/rl7/rephex/current.svg"
#define CURRENT_HAT_SVG_PATH   "img/rl7/rephex/current_hat.svg"
#define CURRENT_DATA_DIR       "data/run/rl7/rephex"
#define CURRENT_DATA_PATH      "data/run/rl7/rephex/current.dat"
#define CURRENT_HAT_DATA_PATH  "data/run/rl7/rephex/current_hat.dat"

typedef struct {
    size_t d, b, g, f;
} Counts;

static int parse_level(const char *s, unsigned *level) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !end || *end || v > 6) return 0;
    *level = (unsigned)v;
    return 1;
}

static int parse_rotation_step(const char *s, unsigned *step) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !s[0] || !end || *end || v > 12) return 0;
    *step = (unsigned)v;
    return 1;
}

static const MR7Cell *find_cell(const MR7Cells *cells, int q, int r) {
    for (size_t i = 0; i < cells->n; i++)
        if (cells->cell[i].q == q && cells->cell[i].r == r) return &cells->cell[i];
    return NULL;
}

static void neighbor_delta(int dir, int *dq, int *dr) {
    static const int d[6][2] = {
        {1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}, {1,-1}
    };
    *dq = d[dir % 6][0];
    *dr = d[dir % 6][1];
}

static void sorted_small_ints(int *a, size_t n) {
    for (size_t i = 1; i < n; i++) {
        int v = a[i]; size_t j = i;
        while (j && a[j-1] > v) { a[j] = a[j-1]; j--; }
        a[j] = v;
    }
}

static int component_line_signature(const MR7Cells *cells, const size_t *comp,
                                    size_t cn, int axis, int *counts,
                                    size_t *line_n) {
    int keys[64], line_counts[64];
    size_t n = 0;
    if (cn > 64) return 0;
    for (size_t i = 0; i < cn; i++) {
        const MR7Cell *c = &cells->cell[comp[i]];
        int key = axis == 0 ? c->q : (axis == 1 ? c->r : c->q + c->r);
        size_t j = 0;
        for (; j < n; j++) if (keys[j] == key) break;
        if (j == n) { keys[n] = key; line_counts[n] = 1; n++; }
        else line_counts[j]++;
    }
    for (size_t i = 0; i < n; i++) counts[i] = line_counts[i];
    sorted_small_ints(counts, n);
    *line_n = n;
    return 1;
}

static int is_six_leaf_triangle(const MR7Cells *cells, const size_t *comp, size_t cn) {
    if (cn != 6) return 0;
    for (int axis = 0; axis < 3; axis++) {
        int counts[16]; size_t n = 0;
        if (!component_line_signature(cells, comp, cn, axis, counts, &n)) continue;
        if (n == 3 && counts[0] == 1 && counts[1] == 2 && counts[2] == 3) return 1;
    }
    return 0;
}

static int is_eight_leaf_two_row(const MR7Cells *cells, const size_t *comp, size_t cn) {
    if (cn != 8) return 0;
    for (int axis = 0; axis < 3; axis++) {
        int counts[16]; size_t n = 0;
        if (!component_line_signature(cells, comp, cn, axis, counts, &n)) continue;
        if (n == 2 && counts[0] == 4 && counts[1] == 4) return 1;
    }
    return 0;
}

static void classify_split_leaf_components(MR7Cells *cells) {
    if (!cells || !cells->n) return;
    unsigned char *seen = calloc(cells->n, 1);
    size_t *queue = malloc(cells->n * sizeof(*queue));
    size_t *comp = malloc(cells->n * sizeof(*comp));
    if (!seen || !queue || !comp) { free(seen); free(queue); free(comp); return; }
    for (size_t seed = 0; seed < cells->n; seed++) {
        if (seen[seed] || cells->cell[seed].state != 'G') continue;
        size_t head = 0, tail = 0, cn = 0;
        seen[seed] = 1; queue[tail++] = seed;
        while (head < tail) {
            size_t idx = queue[head++];
            comp[cn++] = idx;
            for (int dir = 0; dir < 6; dir++) {
                int dq = 0, dr = 0;
                neighbor_delta(dir, &dq, &dr);
                const MR7Cell *nb = find_cell(cells, cells->cell[idx].q + dq, cells->cell[idx].r + dr);
                if (!nb || nb->state != 'G') continue;
                size_t nb_idx = (size_t)(nb - cells->cell);
                if (!seen[nb_idx]) { seen[nb_idx] = 1; queue[tail++] = nb_idx; }
            }
        }
        char mark = 'U';                 /* partial / boundary / unknown leaf component */
        if (is_six_leaf_triangle(cells, comp, cn)) mark = 'T';
        else if (is_eight_leaf_two_row(cells, comp, cn)) mark = 'P';
        for (size_t i = 0; i < cn; i++) cells->cell[comp[i]].leaf_tag = mark;
    }
    free(seen); free(queue); free(comp);
}

static Counts count_kinds(const MR7Patch *patch) {
    Counts n = {0, 0, 0, 0};
    for (size_t i = 0; i < patch->n; i++) {
        if (patch->tile[i].kind == MR7_D) n.d++;
        else if (patch->tile[i].kind == MR7_B) n.b++;
        else if (patch->tile[i].kind == MR7_G) n.g++;
        else n.f++;
    }
    return n;
}

static const char *palette_name(Palette p) {
    if (p == PALETTE_TREE) return "tree";
    if (p == PALETTE_SPLIT) return "split";
    return "ordinary";
}

static const char *fill_for(char state, Palette p) {
    if (state == 'F') return "#161616";
    if (p == PALETTE_ORDINARY) {
        if (state == '0') return "#7da9f7";
        if (state == '1') return "#ef8d8d";
        return "#79c996"; /* all H cells */
    }
    if (p == PALETTE_SPLIT) {
        if (state == '0') return "#f1d777";
        if (state == '1') return "#f5af74";
        if (state == 'B') return "#b88968"; /* pass / branch H */
        if (state == 'C') return "#d1ae8c"; /* capped H */
        return "#79c996"; /* H leaf default; split shade handled by leaf_tag */
    }
    if (state == '0') return "#f1d777";
    if (state == '1') return "#f5af74";
    if (state == 'B') return "#b88968"; /* pass / branch H */
    if (state == 'C') return "#d1ae8c"; /* capped H */
    return "#79c996"; /* green terminal H */
}

static const char *fill_for_cell(const MR7Cell *cell, Palette p,
                                 MR7Init init, unsigned level) {
    (void)init;
    (void)level;
    if (p == PALETTE_SPLIT && cell->state == 'G') {
        if (cell->leaf_tag == 'T') return "#5fb86f"; /* complete 6-H triangle */
        if (cell->leaf_tag == 'U') return "#bce7b9"; /* partial/boundary/unknown */
        return "#79c996";                            /* P/par8 or unclassified */
    }
    return fill_for(cell->state, p);
}

static const char *ansi_for(char state, Palette p) {
    if (state == 'F') return "[38;5;240m";
    if (p == PALETTE_ORDINARY) {
        if (state == '0') return "[38;5;27m";
        if (state == '1') return "[38;5;160m";
        return "[38;5;35m";
    }
    if (p == PALETTE_SPLIT) {
        if (state == '0') return "[38;5;178m";
        if (state == '1') return "[38;5;166m";
        if (state == 'B') return "[38;5;173m";
        if (state == 'C') return "[38;5;180m";
        return "[38;5;35m";
    }
    if (state == '0') return "[38;5;178m";
    if (state == '1') return "[38;5;166m";
    if (state == 'B') return "[38;5;173m"; /* dark brown pass / branch */
    if (state == 'C') return "[38;5;180m"; /* light brown cap */
    return "[38;5;35m";
}

static const char *ansi_for_cell(const MR7Cell *cell, Palette p) {
    if (p == PALETTE_SPLIT && cell->state == 'G') {
        if (cell->leaf_tag == 'T') return "\033[38;5;34m";
        if (cell->leaf_tag == 'U') return "\033[38;5;120m";
        return "\033[38;5;35m";
    }
    return ansi_for(cell->state, p);
}

static void bounds(const MR7Cells *cells, int *q0, int *q1, int *r0, int *r1) {
    *q0 = *q1 = cells->cell[0].q;
    *r0 = *r1 = cells->cell[0].r;
    for (size_t i = 1; i < cells->n; i++) {
        if (cells->cell[i].q < *q0) *q0 = cells->cell[i].q;
        if (cells->cell[i].q > *q1) *q1 = cells->cell[i].q;
        if (cells->cell[i].r < *r0) *r0 = cells->cell[i].r;
        if (cells->cell[i].r > *r1) *r1 = cells->cell[i].r;
    }
}

static void print_cell_glyph(const MR7Cell *cell, int color, Palette palette, int last) {
    const char *sep = last ? "" : " ";
    if (color) printf("%s⬢\033[0m%s", ansi_for_cell(cell, palette), sep);
    else printf("⬢%s", sep);
}

static void text_diagram(const MR7Cells *cells, int color, Palette palette,
                         int diagram_only, int repl_window,
                         int preview_rows, int preview_cols) {
    if (!cells->n) return;
    int q0, q1, r0, r1;
    bounds(cells, &q0, &q1, &r0, &r1);
    if (repl_window) {
        const int max_rows = preview_rows > 0 ? preview_rows : 16;
        const int available_cols = preview_cols > 0 ? preview_cols : 79;
        /*
         * In the terminal used for rephex, the flat-top glyph occupies two
         * display columns and each non-final cell includes one separating
         * space.  Reserve the worst-case row indent before choosing a crop.
         */
        const int glyph_cols = 2;
        const int separated_cell_cols = glyph_cols + 1;
        const int max_indent = max_rows > 0 ? max_rows - 1 : 0;
        const int max_cols =
            (available_cols - max_indent + (separated_cell_cols - glyph_cols)) /
            separated_cell_cols;
        const int rspan = r1 - r0 + 1, qspan = q1 - q0 + 1;
        int vr0 = r0, vq0 = q0;
        if (rspan > max_rows) vr0 = r0 + (rspan - max_rows) / 2;
        if (qspan > max_cols) vq0 = q0 + (qspan - max_cols) / 2;
        int vr1 = vr0 + (rspan > max_rows ? max_rows - 1 : rspan - 1);
        int vq1 = vq0 + (qspan > max_cols ? max_cols - 1 : qspan - 1);
        int clipped = vr0 != r0 || vr1 != r1 || vq0 != q0 || vq1 != q1;
        printf("@clipped=%d\n", clipped);
        for (int r = vr0; r <= vr1; r++) {
            int row_q1 = vq1;
            while (row_q1 >= vq0 && !find_cell(cells, row_q1, r)) row_q1--;
            for (int pad = 0; pad < r - vr0; pad++) putchar(' ');
            for (int q = vq0; q <= row_q1; q++) {
                const MR7Cell *cell = find_cell(cells, q, r);
                if (!cell) { printf("  "); continue; }
                print_cell_glyph(cell, color, palette, q == row_q1);
            }
            putchar('\n');
        }
        return;
    }
    if ((q1 - q0 + 1) > 44 || (r1 - r0 + 1) > 44) {
        printf("text diagram omitted: bounding box %d x %d; use SVG depiction\n", q1-q0+1, r1-r0+1);
        return;
    }
    if (!diagram_only) printf("bbox: q=[%d,%d] r=[%d,%d]\n\n", q0, q1, r0, r1);
    /* Corrected display convention: visible rows agree with the reflected SVG
     * convention used by the CCW refined/ordinary tile cycles.  The first
     * displayed row is minimum r; each following row shifts one column right. */
    for (int r = r0; r <= r1; r++) {
        int row_q1 = q1;
        while (row_q1 >= q0 && !find_cell(cells, row_q1, r)) row_q1--;
        for (int pad = 0; pad < r - r0; pad++) putchar(' ');
        for (int q = q0; q <= row_q1; q++) {
            const MR7Cell *cell = find_cell(cells, q, r);
            if (!cell) { printf("  "); continue; }
            print_cell_glyph(cell, color, palette, q == row_q1);
        }
        putchar('\n');
    }
    if (!diagram_only && color && palette == PALETTE_ORDINARY) {
        printf("\nordinary: %s⬢\033[0m = H  %s⬢\033[0m = D0  %s⬢\033[0m = D1  %s⬢\033[0m = F\n",
               ansi_for('G', palette), ansi_for('0', palette), ansi_for('1', palette), ansi_for('F', palette));
    } else if (!diagram_only && color && palette == PALETTE_SPLIT) {
        printf("\nsplit: %s⬢\033[0m = pass  %s⬢\033[0m = cap  %s⬢\033[0m = tri-leaf  %s⬢\033[0m = two-row-leaf  %s⬢\033[0m = unknown-leaf  %s⬢\033[0m = D0  %s⬢\033[0m = D1  %s⬢\033[0m = F\n",
               ansi_for('B', palette), ansi_for('C', palette), ansi_for('T', palette),
               ansi_for('G', palette), ansi_for('t', palette), ansi_for('0', palette),
               ansi_for('1', palette), ansi_for('F', palette));
    } else if (!diagram_only && color) {
        printf("\ntree: %s⬢\033[0m = pass  %s⬢\033[0m = cap  %s⬢\033[0m = leaf  %s⬢\033[0m = D0  %s⬢\033[0m = D1  %s⬢\033[0m = F\n",
               ansi_for('B', palette), ansi_for('C', palette), ansi_for('G', palette),
               ansi_for('0', palette), ansi_for('1', palette), ansi_for('F', palette));
    }
}

static int ensure_dir(const char *path, char *err, size_t errsz) {
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        snprintf(err, errsz, "cannot create %.180s", path); return 0;
    }
    return 1;
}

static int ensure_svg_output_dir(char *err, size_t errsz) {
    return ensure_dir("img", err, errsz) &&
           ensure_dir("img/rl7", err, errsz) &&
           ensure_dir("img/rl7/rephex", err, errsz);
}

static int ensure_runtime_dir(char *err, size_t errsz) {
    return ensure_dir("data", err, errsz) &&
           ensure_dir("data/run", err, errsz) &&
           ensure_dir("data/run/rl7", err, errsz) &&
           ensure_dir(CURRENT_DATA_DIR, err, errsz);
}

static int write_current_data(const char *path, MR7Init init, unsigned level,
                              const MR7Cells *cells, const char *record,
                              char *err, size_t errsz) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) { snprintf(err, errsz, "cannot write current runtime data"); return 0; }
    fprintf(fp, "# REPHEX current computed pattern\n");
    fprintf(fp, "# axiom=%s level=%u cells=%zu\n", mr7_init_name(init), level, cells->n);
    fprintf(fp, "# source=%s\n", record ? record : "");
    fprintf(fp, "# q r state orientation colour_index dark_cap leaf_tag\n");
    for (size_t i = 0; i < cells->n; i++)
        fprintf(fp, "%d %d %c %u %u %u %c\n", cells->cell[i].q, cells->cell[i].r,
                cells->cell[i].state, (unsigned)cells->cell[i].ori,
                (unsigned)cells->cell[i].color_index, (unsigned)cells->cell[i].dark_cap,
                cells->cell[i].leaf_tag ? cells->cell[i].leaf_tag : 'N');
    if (fclose(fp) != 0 || rename(tmp, path) != 0) {
        remove(tmp); snprintf(err, errsz, "cannot install current runtime data"); return 0;
    }
    return 1;
}

static int read_current_data(const char *path, MR7Cells *cells, char *err, size_t errsz) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { snprintf(err, errsz, "missing %s; compute a pattern first", path); return 0; }
    MR7Cell *items = NULL;
    size_t n = 0, cap = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;
        MR7Cell c; unsigned ori = 0, colour = 0, dark_cap = 0; char leaf_tag = 'N';
        int fields = sscanf(line, "%d %d %c %u %u %u %c", &c.q, &c.r, &c.state, &ori, &colour, &dark_cap, &leaf_tag);
        if (fields < 5) continue;
        c.ori = (uint8_t)ori; c.color_index = (uint8_t)colour; c.dark_cap = (uint8_t)dark_cap;
        c.leaf_tag = fields >= 7 ? leaf_tag : 'N';
        if (n == cap) {
            size_t next = cap ? cap * 2 : 64;
            MR7Cell *grown = realloc(items, next * sizeof(*items));
            if (!grown) { free(items); fclose(fp); snprintf(err, errsz, "out of memory reading current data"); return 0; }
            items = grown; cap = next;
        }
        items[n++] = c;
    }
    fclose(fp);
    if (!n) { free(items); snprintf(err, errsz, "current runtime data is empty"); return 0; }
    cells->cell = items; cells->n = n;
    return 1;
}

static void rotate_output_point(double *x, double *y, unsigned rotation_step) {
    if (!rotation_step) return;
    const double a = M_PI * (30.0 * (double)(rotation_step % 12)) / 180.0;
    const double c = cos(a), s = sin(a), ox = *x, oy = *y;
    *x = c * ox - s * oy;
    *y = s * ox + c * oy;
}

static void open_svg_viewer(const char *path) {
    const char *no_open = getenv("REPHEX_NO_OPEN");
    if (no_open && no_open[0]) return;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        _exit(127);
    }
}

static void raw_center(int q, int r, double *x, double *y) {
    /* Corrected reflection convention: q advances horizontally and positive
     * r advances downward on screen.  Together with the polygon Y reflection
     * below, stored cyclic tile rows display CCW as required by RL5/RL6. */
    *x = sqrt(3.0) * ((double)q + 0.5 * (double)r);
    *y = 1.5 * (double)r;
}

/* Hex skin geometry follows the oriented RL7 axial directions.  A tile's
 * principal diameter points in its stored orientation.  D0 carries its two
 * upper-side branches; D1 carries the matching lower-side branches. */
static const int skin_dirs[6][2] = {
    {-1, 0}, {0, -1}, {1, -1}, {1, 0}, {0, 1}, {-1, 1}
};

static void unit_for_ori(unsigned ori, double *ux, double *uy) {
    double x, y;
    raw_center(skin_dirs[ori % 6][0], skin_dirs[ori % 6][1], &x, &y);
    double n = sqrt(x * x + y * y);
    *ux = x / n;
    *uy = y / n;
}

static void svg_line(FILE *fp, double x0, double y0, double x1, double y1,
                     unsigned rotation_step, const char *stroke, double width) {
    rotate_output_point(&x0, &y0, rotation_step);
    rotate_output_point(&x1, &y1, rotation_step);
    fprintf(fp, "<line x1=\"%.6f\" y1=\"%.6f\" x2=\"%.6f\" y2=\"%.6f\" stroke=\"%s\" stroke-width=\"%.3f\" stroke-linecap=\"round\"/>\n",
            x0, y0, x1, y1, stroke, width);
}

static void svg_arrow_head(FILE *fp, double px, double py, double ux, double uy,
                           double size, unsigned rotation_step,
                           const char *stroke, double width) {
    double vx = -uy, vy = ux;
    double tipx = px + size * ux, tipy = py + size * uy;
    double backx = px - 0.75 * size * ux, backy = py - 0.75 * size * uy;
    svg_line(fp, backx + 0.60 * size * vx, backy + 0.60 * size * vy,
             tipx, tipy, rotation_step, stroke, width);
    svg_line(fp, backx - 0.60 * size * vx, backy - 0.60 * size * vy,
             tipx, tipy, rotation_step, stroke, width);
}

static void write_hex_skin(FILE *fp, const MR7Cell *cell, unsigned rotation_step) {
    const char *ink = "#252525";
    double cx, cy, ux, uy;
    raw_center(cell->q, cell->r, &cx, &cy);
    unit_for_ori(cell->ori, &ux, &uy);

    if (cell->state == 'F') {
        const char *gray = "#777777";
        for (unsigned d = 0; d < 6; d++) {
            double vx, vy;
            unit_for_ori(d, &vx, &vy);
            svg_line(fp, cx, cy, cx + 0.62 * vx, cy + 0.62 * vy,
                     rotation_step, gray, 0.032);
            svg_arrow_head(fp, cx + 0.52 * vx, cy + 0.52 * vy, vx, vy,
                           0.085, rotation_step, gray, 0.028);
        }
        return;
    }

    if (cell->state == 'G' || cell->state == 'C') {
        /* Terminal/cap H cells use the half-arrow convention:
         * the principal arrow runs into the cell and terminates at centroid. */
        svg_line(fp, cx - 0.78 * ux, cy - 0.78 * uy,
                 cx, cy, rotation_step, ink, 0.036);
        svg_arrow_head(fp, cx - 0.105 * ux, cy - 0.105 * uy, ux, uy,
                       0.105, rotation_step, ink, 0.032);
    } else {
        /* Pass cells and D cells use a continuing diameter with two
         * same-facing direction arrows.  This is deliberately used for B:
         * low-lying F1 has six B pass states around the false center, not caps. */
        svg_line(fp, cx - 0.78 * ux, cy - 0.78 * uy,
                 cx + 0.78 * ux, cy + 0.78 * uy, rotation_step, ink, 0.036);
        svg_arrow_head(fp, cx - 0.39 * ux, cy - 0.39 * uy, ux, uy,
                       0.105, rotation_step, ink, 0.032);
        svg_arrow_head(fp, cx + 0.39 * ux, cy + 0.39 * uy, ux, uy,
                       0.105, rotation_step, ink, 0.032);
    }

    if (cell->state == '0' || cell->state == '1') {
        /* For a horizontal D0->D1 axis: D0 branches above, D1 below.
         * This realizes the a,b / g,h branch convention as a first visual
         * test; it remains data-derived from the stored D orientation. */
        int sign = cell->state == '0' ? -1 : 1;
        int b0 = ((int)cell->ori + sign + 6) % 6;
        int b1 = ((int)cell->ori + 2 * sign + 12) % 6;
        for (int i = 0; i < 2; i++) {
            double bx, by;
            unit_for_ori((unsigned)(i ? b1 : b0), &bx, &by);
            svg_line(fp, cx, cy, cx + 0.62 * bx, cy + 0.62 * by,
                     rotation_step, ink, 0.032);
            svg_arrow_head(fp, cx + 0.52 * bx, cy + 0.52 * by, bx, by,
                           0.085, rotation_step, ink, 0.028);
        }
    }
}

static int write_svg(const char *path, MR7Init init, unsigned level,
                     Palette palette, const MR7Patch *patch, const MR7Cells *cells,
                     const char *record, unsigned rotation_step,
                     char *err, size_t errsz) {
    (void)patch; (void)record;
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.new", path);
    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) { snprintf(err, errsz, "cannot write temporary SVG output"); return 0; }
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    for (size_t i = 0; i < cells->n; i++) {
        double cx, cy; raw_center(cells->cell[i].q, cells->cell[i].r, &cx, &cy);
        for (int k = 0; k < 6; k++) {
            double a = M_PI * (30.0 + 60.0 * k) / 180.0;
            double x = cx + cos(a), y = cy - sin(a);
            rotate_output_point(&x, &y, rotation_step);
            if (i == 0 && k == 0) xmin = xmax = x, ymin = ymax = y;
            if (x < xmin) xmin = x;
            if (x > xmax) xmax = x;
            if (y < ymin) ymin = y;
            if (y > ymax) ymax = y;
        }
    }
    const double margin = 0.16;
    const double view_x = xmin - margin;
    const double view_y = ymin - margin;
    const double view_w = xmax - xmin + 2.0 * margin;
    const double view_h = ymax - ymin + 2.0 * margin;
    const double width = 1200.0;
    const double height = fmax(240.0, width * view_h / view_w);
    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"%.6f %.6f %.6f %.6f\">\n",
            width, height, view_x, view_y, view_w, view_h);
    fprintf(fp, "<rect x=\"%.6f\" y=\"%.6f\" width=\"%.6f\" height=\"%.6f\" fill=\"#ffffff\"/>\n",
            view_x, view_y, view_w, view_h);
    for (size_t i = 0; i < cells->n; i++) {
        double cx, cy; raw_center(cells->cell[i].q, cells->cell[i].r, &cx, &cy);
        fprintf(fp, "<polygon points=\"");
        for (int k = 0; k < 6; k++) {
            double a = M_PI * (30.0 + 60.0 * k) / 180.0;
            double x = cx + cos(a), y = cy - sin(a);
            rotate_output_point(&x, &y, rotation_step);
            fprintf(fp, "%.6f,%.6f%s", x, y, k == 5 ? "" : " ");
        }
        fprintf(fp, "\" fill=\"%s\" stroke=\"#282828\" stroke-width=\"0.020\"/>\n",
                fill_for_cell(&cells->cell[i], palette, init, level));
    }
    for (size_t i = 0; i < cells->n; i++) {
        /* Draw arrows in both ordinary and tree palettes.  Ordinary is not a
         * blank-fill palette; it still needs F spokes and pass/D/H direction
         * marks for debugging the low axioms. */
        write_hex_skin(fp, &cells->cell[i], rotation_step);
    }
    fputs("</svg>\n", fp);
    if (fclose(fp) != 0 || rename(tmp_path, path) != 0) {
        remove(tmp_path);
        snprintf(err, errsz, "cannot install SVG output");
        return 0;
    }
    return 1;
}

static int low_axiom_hat_case(MR7Init init, unsigned level) {
    if (level == 0 && (init == MR7_INIT_D || init == MR7_INIT_DH || init == MR7_INIT_F)) return 1;
    if (level == 1 && init == MR7_INIT_F) return 1;
    return 0;
}

static int run_low_axiom_hat_helper(MR7Init init, unsigned level, Palette palette,
                                    unsigned rotation_step) {
    char command[1024];
    snprintf(command, sizeof(command),
             "REPHEX_NO_OPEN=1 ./bin/rephex_hacked %s %u%s --from-current --hat-svg "
             "--rotation-step %u --no-color",
             mr7_init_name(init), level,
             (palette == PALETTE_TREE || palette == PALETTE_SPLIT) ? " --palette tree" : "",
             rotation_step);
    int rc = system(command);
    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

static int run_hat_renderer(const MR7Cells *cells, MR7Init init, unsigned level,
                            Palette palette, unsigned rotation_step,
                            char *err, size_t errsz) {
    RephexHatStats stats;

    /*
     * Low-lying axiom hat depictions are a one-off service leaf.  Try that
     * helper only for the final hat-SVG product.  If it fails, fall through to
     * the published normal renderer so generation, terminal, hex SVG, and most
     * hat SVG output remain available.
     */
    if (low_axiom_hat_case(init, level) &&
        run_low_axiom_hat_helper(init, level, palette, rotation_step)) {
        printf("hat-status: complete placed=low-axiom-helper axiom=%s level=%u\n",
               mr7_init_name(init), level);
        return 1;
    }

    if (!rephex_hat_render(cells, CURRENT_HAT_SVG_PATH, CURRENT_HAT_DATA_PATH,
                           mr7_init_name(init), level, (palette == PALETTE_SPLIT ? 2 : (palette == PALETTE_TREE ? 1 : 0)),
                           rotation_step, &stats, err, errsz)) return 0;
    printf("hat-status: %s placed=%zu/%zu unresolved_figures=%zu conflicts=%zu overlaps=%zu\n",
           stats.placed_hats == stats.total_cells ? "complete" : "partial",
           stats.placed_hats, stats.total_cells, stats.unresolved_figures,
           stats.cycle_conflicts, stats.overlaps);
    return 1;
}

static int write_axioms(const char *path, const MR7ColorMap *map) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "cannot write %s\n", path); return 0; }
    fprintf(fp, "# Mountain and Range RL7 inflation axiom catalogue\n");
    fprintf(fp, "# Generated from inflation7.c; C3 short records use verified indexed-shift +2 witnesses.\n");
    fprintf(fp, "# Scale-dependent syzygy reductions are explicitly unchecked.\n\n");
    for (int i = 0; i < MR7_INIT_COUNT; i++) {
        MR7Init init = (MR7Init)i;
        char record[1024] = {0}, err[256];
        MR7Patch p; MR7Cells c;
        if (!mr7_make_init(init, &p, map, record, sizeof(record), err, sizeof(err)) ||
            !mr7_tiles_to_cells(&p, &c, err, sizeof(err))) {
            fprintf(fp, "%s ERROR %s\n", mr7_init_name(init), err); fclose(fp); return 0;
        }
        Counts n = count_kinds(&p);
        fprintf(fp, "[%s]\naxiom = %s\nlevel0 = tiles:%zu D:%zu B:%zu G:%zu F:%zu cells:%zu\n",
                mr7_init_name(init), record, p.n, n.d, n.b, n.g, n.f, c.n);
        mr7_cells_free(&c); mr7_patch_free(&p);
        for (unsigned level = 1; level <= 2; level++) {
            if (!mr7_expand_init(init, level, &p, map, record, sizeof(record), err, sizeof(err))) {
                fprintf(fp, "level%u = UNRESOLVED %s\n", level, err);
                continue;
            }
            if (!mr7_tiles_to_cells(&p, &c, err, sizeof(err))) {
                fprintf(fp, "level%u = UNRESOLVED %s\n", level, err);
                mr7_patch_free(&p);
                continue;
            }
            n = count_kinds(&p);
            fprintf(fp, "level%u = tiles:%zu D:%zu B:%zu G:%zu F:%zu cells:%zu\n",
                    level, p.n, n.d, n.b, n.g, n.f, c.n);
            mr7_cells_free(&c); mr7_patch_free(&p);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    return 1;
}

static const char *init_description(MR7Init init) {
    switch (init) {
        case MR7_INIT_D: return "single dimer";
        case MR7_INIT_H: return "single H";
        case MR7_INIT_DH: return "fixed-point straight row";
        case MR7_INIT_B3: return "brown / branch C3";
        case MR7_INIT_L3: return "green / leaf C3";
        case MR7_INIT_F: return "false center; sixfold generator";
        case MR7_INIT_COUNT: break;
    }
    return "?";
}

static void usage(FILE *fp, const char *argv0) {
    fprintf(fp,
        "usage: %s AXIOM [LEVEL] [--palette ordinary|tree|split|O|T|S] [--svg] [--hat-svg] [--rotation-step N]\n"
        "       %s AXIOM LEVEL --write-current-only\n"
        "       %s AXIOM LEVEL --from-current [--svg|--hat-svg|--repl-window]\n"
        "       %s --list-inits\n"
        "       %s --check\n"
        "\n"
        "AXIOM: D H DH B3 L3 F\n"
        "Default output is a colored terminal depiction in the ordinary palette.\n"
        "--svg writes and opens img/rl7/rephex/current.svg.\n"
        "--hat-svg writes and opens img/rl7/rephex/current_hat.svg.\n",
        argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    unsigned level = 0;
    int have_level = 0, have_init = 0;
    int check = 0, list = 0, color = isatty(STDOUT_FILENO);
    Palette palette = PALETTE_ORDINARY;
    MR7Init init = MR7_INIT_D;
    int svg_requested = 0, hat_svg_requested = 0, diagram_only = 0, repl_window = 0;
    int from_current = 0, write_current_only = 0;
    int preview_rows = 16, preview_cols = 79;
    unsigned rotation_step = 0;
    const char *svg_path = CURRENT_SVG_PATH;
    const char *axiom_path = NULL, *audit_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(stdout, argv[0]); return 0;
        } else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--list-inits")) list = 1;
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc) {
            if (!strcmp(argv[i+1], "ordinary") || !strcmp(argv[i+1], "O") || !strcmp(argv[i+1], "o")) palette = PALETTE_ORDINARY;
            else if (!strcmp(argv[i+1], "tree") || !strcmp(argv[i+1], "T") || !strcmp(argv[i+1], "t")) palette = PALETTE_TREE;
            else if (!strcmp(argv[i+1], "split") || !strcmp(argv[i+1], "S") || !strcmp(argv[i+1], "s") || !strcmp(argv[i+1], "split-leaf") || !strcmp(argv[i+1], "leaf-split")) palette = PALETTE_SPLIT;
            else { usage(stderr, argv[0]); return 2; }
            i++;
        }
        else if (!strcmp(argv[i], "--svg")) svg_requested = 1;
        else if (!strcmp(argv[i], "--hat-svg")) hat_svg_requested = 1;
        else if (!strcmp(argv[i], "--rotation-step") && i + 1 < argc && parse_rotation_step(argv[i+1], &rotation_step)) { i++; }
        else if (!strcmp(argv[i], "--from-current")) from_current = 1;
        else if (!strcmp(argv[i], "--write-current-only")) write_current_only = 1;
        else if (!strcmp(argv[i], "--diagram-only")) diagram_only = 1;
        else if (!strcmp(argv[i], "--repl-window")) { diagram_only = 1; repl_window = 1; }
        else if (!strcmp(argv[i], "--preview-rows") && i + 1 < argc) preview_rows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--preview-cols") && i + 1 < argc) preview_cols = atoi(argv[++i]);
        /* Internal/test flags: not part of the ordinary inspector workflow. */
        else if (!strcmp(argv[i], "--no-color")) color = 0;
        else if (!strcmp(argv[i], "--color")) color = 1;
        else if (!strcmp(argv[i], "--write-axioms") && i + 1 < argc) axiom_path = argv[++i];
        else if (!strcmp(argv[i], "--audit-c3") && i + 1 < argc) audit_path = argv[++i];
        else if (argv[i][0] != '-' && !have_init && mr7_parse_init(argv[i], &init)) have_init = 1;
        else if (argv[i][0] != '-' && have_init && !have_level && parse_level(argv[i], &level)) have_level = 1;
        else { usage(stderr, argv[0]); return 2; }
    }

    MR7ColorMap map; mr7_color_map_default(&map);
    if (list) {
        for (int i = 0; i < MR7_INIT_COUNT; i++)
            printf("%-3s  %s\n", mr7_init_name((MR7Init)i), init_description((MR7Init)i));
        if (!have_init && !axiom_path && !audit_path && !svg_requested && !hat_svg_requested && !check) return 0;
    }
    if (check && !mr7_check_reference_counts(stdout, &map)) return 1;
    if (axiom_path && !write_axioms(axiom_path, &map)) return 1;
    if (audit_path) {
        FILE *fp = fopen(audit_path, "wb");
        char audit_err[256];
        if (!fp) { fprintf(stderr, "cannot write %s\n", audit_path); return 1; }
        if (!mr7_write_c3_reduction_audit(fp, 4, audit_err, sizeof(audit_err))) {
            fclose(fp); fprintf(stderr, "C3 audit failure: %s\n", audit_err); return 1;
        }
        fclose(fp);
    }
    if (!have_init) {
        if (check || axiom_path || audit_path) return 0;
        usage(stderr, argv[0]); return 2;
    }

    MR7Patch patch = {0}; MR7Cells cells = {0}; char record[1024] = {0}, err[256];
    Counts n = {0, 0, 0, 0};
    if (from_current) {
        if (!read_current_data(CURRENT_DATA_PATH, &cells, err, sizeof(err))) {
            fprintf(stderr, "runtime-data failure: %s\n", err); return 1;
        }
    } else {
        if (!mr7_expand_init(init, level, &patch, &map, record, sizeof(record), err, sizeof(err))) {
            fprintf(stderr, "inflation failure: %s\n", err); return 1;
        }
        if (!mr7_tiles_to_cells(&patch, &cells, err, sizeof(err))) {
            fprintf(stderr, "cell-map failure: %s\n", err); mr7_patch_free(&patch); return 1;
        }
        n = count_kinds(&patch);
        if (!ensure_runtime_dir(err, sizeof(err)) ||
            !write_current_data(CURRENT_DATA_PATH, init, level, &cells, record, err, sizeof(err))) {
            fprintf(stderr, "runtime-data failure: %s\n", err); mr7_cells_free(&cells); mr7_patch_free(&patch); return 1;
        }
    }
    if (palette == PALETTE_SPLIT) {
        classify_split_leaf_components(&cells);
        if (!ensure_runtime_dir(err, sizeof(err)) ||
            !write_current_data(CURRENT_DATA_PATH, init, level, &cells, record, err, sizeof(err))) {
            fprintf(stderr, "runtime-data failure: %s\n", err); mr7_cells_free(&cells); if (!from_current) mr7_patch_free(&patch); return 1;
        }
    }
    if (write_current_only) {
        mr7_cells_free(&cells); if (!from_current) mr7_patch_free(&patch); return 0;
    }
    if (!diagram_only && !from_current) {
        printf("%s  inflation=%u  palette=%s\n",
               mr7_init_name(init), level, palette_name(palette));
        printf("tiles=%zu  cells=%zu  D=%zu B=%zu G=%zu F=%zu\n",
               patch.n, cells.n, n.d, n.b, n.g, n.f);
        printf("axiom: %s\n", record);
    }
    text_diagram(&cells, color, palette, diagram_only, repl_window, preview_rows, preview_cols);
    if (svg_requested) {
        if (!ensure_svg_output_dir(err, sizeof(err)) ||
            !write_svg(svg_path, init, level, palette, &patch, &cells, record, rotation_step, err, sizeof(err))) {
            fprintf(stderr, "svg failure: %s\n", err); mr7_cells_free(&cells); mr7_patch_free(&patch); return 1;
        }
        printf("svg: %s\n", svg_path);
        open_svg_viewer(svg_path);
    }
    if (hat_svg_requested) {
        fflush(stdout);
        if (!ensure_svg_output_dir(err, sizeof(err)) ||
            !ensure_runtime_dir(err, sizeof(err)) ||
            !run_hat_renderer(&cells, init, level, palette, rotation_step, err, sizeof(err))) {
            fprintf(stderr, "hat-svg failure: %s\n", err); mr7_cells_free(&cells); mr7_patch_free(&patch); return 1;
        }
        printf("hat-svg: %s\n", CURRENT_HAT_SVG_PATH);
        printf("hat-data: %s\n", CURRENT_HAT_DATA_PATH);
        open_svg_viewer(CURRENT_HAT_SVG_PATH);
    }
    mr7_cells_free(&cells); if (!from_current) mr7_patch_free(&patch);
    return 0;
}
