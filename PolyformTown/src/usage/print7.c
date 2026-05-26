#include "usage/print7.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * print7.c -- RL7 depiction helpers.
 *
 * This renderer is deliberately minimal and literal:
 *   HCE text box  -> one hexagon
 *   HCE slot      -> one corner label
 *
 * It prints the abstract boundary7 patch only.  It does not attempt hat
 * geometry or any recursive supertile placement.
 */

typedef struct { double x, y; } P2;

static void svg_escape(FILE *fp, const char *s){
    for(const unsigned char *p=(const unsigned char *)s; *p; p++){
        if(*p == '&') fputs("&amp;", fp);
        else if(*p == '<') fputs("&lt;", fp);
        else if(*p == '>') fputs("&gt;", fp);
        else if(*p == '"') fputs("&quot;", fp);
        else fputc((int)*p, fp);
    }
}

static int known_count(const Cell *c){
    int n = 0;
    for(int i=0;i<6;i++) if(!tok_empty(&c->slot[i])) n++;
    return n;
}

static int has_tok(Tok *xs, int n, const Tok *x){
    for(int i=0;i<n;i++) if(tok_eq(&xs[i], x)) return 1;
    return 0;
}

static int possible_symbol_count(const State *s, const Model *m, int ci, int slot){
    if(!s || !m || ci < 0 || ci >= s->ncell || slot < 0 || slot >= 6) return 0;
    if(!tok_empty(&s->cells[ci].slot[slot])) return 1;

    Tok seen[128];
    int nseen = 0;
    for(int t=0;t<m->ntile;t++){
        for(int r=0;r<6;r++){
            Tok x = tile_sym(m, t, r, slot);
            if(has_tok(seen, nseen, &x)) continue;
            State tmp = *s;
            char err[240];
            if(!set_cell_slot(&tmp.cells[ci], slot, &x, err, sizeof(err))) continue;
            if(validate_state_local(m, &tmp, err, sizeof(err))){
                if(nseen < (int)(sizeof(seen)/sizeof(seen[0]))) seen[nseen++] = x;
            }
        }
    }
    return nseen;
}

static int resolved_tile_class(const Cell *c, const Model *m){
    if(!c || !m || known_count(c) <= 0) return 0;
    int seen[MAX_TILES];
    int nseen = 0;
    for(int t=0;t<m->ntile;t++){
        int hit = 0;
        for(int r=0;r<6;r++){
            if(tile_matches(m, c, t, r)){ hit = 1; break; }
        }
        if(hit) seen[nseen++] = t;
    }
    if(nseen == 1) return (seen[0] % 3) + 1;
    return 0;
}

static const char *fill_for_cell(const Cell *c, const Model *m){
    switch(resolved_tile_class(c, m)){
    case 1: return "#fee2e2"; /* light red */
    case 2: return "#dcfce7"; /* light green */
    case 3: return "#dbeafe"; /* light blue */
    default:
        if(known_count(c) > 0) return "#f1f5f9";
        return "#f8fafc";
    }
}

static P2 cell_center(const Cell *c, int minq, int maxr, double side,
                      double margin_x, double margin_y){
    const double sqrt3 = 1.7320508075688772;
    const double w = sqrt3 * side;
    const double row_h = 1.5 * side;
    double x = margin_x + (double)(c->q - minq) * w + ((c->r & 1) ? 0.5*w : 0.0);
    double y = margin_y + (double)(maxr - c->r) * row_h;
    return (P2){x, y};
}

static P2 slot_vertex(P2 c, double side, int slot){
    const double sqrt3 = 1.7320508075688772;
    const double hx = 0.5 * sqrt3 * side;
    switch(slot){
    case 0: return (P2){ c.x + hx, c.y + 0.5*side }; /* lower-right */
    case 1: return (P2){ c.x + hx, c.y - 0.5*side }; /* upper-right */
    case 2: return (P2){ c.x,      c.y - side     }; /* top */
    case 3: return (P2){ c.x - hx, c.y - 0.5*side }; /* upper-left */
    case 4: return (P2){ c.x - hx, c.y + 0.5*side }; /* lower-left */
    case 5: return (P2){ c.x,      c.y + side     }; /* bottom */
    default:return c;
    }
}

static P2 label_pos(P2 c, double side, int slot){
    P2 v = slot_vertex(c, side, slot);
    double inward = 0.62;
    /* Keep labels close to their corners while avoiding the too-high drift
     * that made the first hex skin hard to read. */
    return (P2){ c.x + (v.x - c.x) * inward,
                 c.y + (v.y - c.y) * inward + 0.5 };
}

static void write_hex_points(FILE *fp, P2 c, double side){
    int order[6] = {2, 1, 0, 5, 4, 3};
    for(int i=0;i<6;i++){
        P2 p = slot_vertex(c, side, order[i]);
        fprintf(fp, "%.2f,%.2f%s", p.x, p.y, i == 5 ? "" : " ");
    }
}

static void draw_slot_label(FILE *fp, const State *s, const Model *m,
                            int ci, int slot, P2 p){
    const Tok *tok = &s->cells[ci].slot[slot];
    if(tok_empty(tok)){
        int n = possible_symbol_count(s, m, ci, slot);
        if(n > 0 && n < 8){
            fprintf(fp,
                    "<text class=\"opt\" x=\"%.2f\" y=\"%.2f\">%d</text>\n",
                    p.x, p.y + 0.0, n);
        }
        return;
    }
    fprintf(fp,
            "<text class=\"tok\" x=\"%.2f\" y=\"%.2f\">",
            p.x, p.y + 1.0);
    svg_escape(fp, tok->s);
    fputs("</text>\n", fp);
}

static void draw_cell(FILE *fp, const State *s, const Model *m,
                      int ci, P2 center, double side){
    const Cell *c = &s->cells[ci];
    fprintf(fp,
            "<g class=\"cell\" data-q=\"%d\" data-r=\"%d\">\n",
            c->q, c->r);
    fprintf(fp, "<polygon class=\"hex\" fill=\"%s\" points=\"", fill_for_cell(c, m));
    write_hex_points(fp, center, side);
    fputs("\"/>\n", fp);
    for(int sl=0; sl<6; sl++){
        P2 p = label_pos(center, side, sl);
        draw_slot_label(fp, s, m, ci, sl, p);
    }
    fputs("</g>\n", fp);
}

int print7_write_hex_svg(const State *s, const Model *m, const char *path,
                         char *err, size_t errsz){
    if(!s || !m || !path || !path[0]){
        snprintf(err, errsz, "print7: missing state, model, or output path");
        return 0;
    }

    int minq=0, maxq=0, minr=0, maxr=0;
    if(s->ncell > 0){
        minq=maxq=s->cells[0].q;
        minr=maxr=s->cells[0].r;
    }
    for(int i=1;i<s->ncell;i++){
        if(s->cells[i].q < minq) minq=s->cells[i].q;
        if(s->cells[i].q > maxq) maxq=s->cells[i].q;
        if(s->cells[i].r < minr) minr=s->cells[i].r;
        if(s->cells[i].r > maxr) maxr=s->cells[i].r;
    }

    const double side = 46.0;
    const double sqrt3 = 1.7320508075688772;
    const double w = sqrt3 * side;
    const double row_h = 1.5 * side;
    const double pad = 34.0;
    double width = pad * 2.0 + (double)(maxq - minq + 1) * w + 0.5*w;
    double height = pad * 2.0 + (double)(maxr - minr + 1) * row_h + side;
    if(width < 260.0) width = 260.0;
    if(height < 220.0) height = 220.0;

    FILE *fp = fopen(path, "w");
    if(!fp){
        snprintf(err, errsz, "print7: could not write %.200s", path);
        return 0;
    }

    fprintf(fp,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n",
            width, height, width, height);
    fputs("<style><![CDATA[\n", fp);
    fputs("svg { background: #ffffff; }\n", fp);
    fputs(".hex { stroke: #111827; stroke-width: 1.4; }\n", fp);
    fputs(".tok { font: 700 15px ui-monospace, SFMono-Regular, Menlo, monospace; fill: #020617; text-anchor: middle; dominant-baseline: middle; }\n", fp);
    fputs(".opt { font: 600 12px ui-monospace, SFMono-Regular, Menlo, monospace; fill: #94a3b8; text-anchor: middle; dominant-baseline: middle; }\n", fp);
    fputs("]]></style>\n", fp);
    fputs("<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n", fp);
    fputs("<g class=\"patch\">\n", fp);

    /* Draw sparse cells in row-major display order so overlaps are stable. */
    for(int r=maxr; r>=minr; r--){
        for(int q=minq; q<=maxq; q++){
            int ci = find_cell(s, q, r);
            if(ci < 0) continue;
            P2 center = cell_center(&s->cells[ci], minq, maxr, side,
                                    pad + 0.5*w, pad + side);
            draw_cell(fp, s, m, ci, center, side);
        }
    }

    fputs("</g>\n</svg>\n", fp);
    if(fclose(fp) != 0){
        snprintf(err, errsz, "print7: could not close %.200s", path);
        return 0;
    }
    return 1;
}
