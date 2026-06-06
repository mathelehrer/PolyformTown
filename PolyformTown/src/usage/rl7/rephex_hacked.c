#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_HAT 32
#define EPS 1e-7

typedef struct { double x, y; } Pt;
typedef struct { double a,b,c,d,tx,ty; } Tf;

static int g_nh = 0;

static const char *ordinary_fill(const char *name) {
    if (!strcmp(name,"D0")) return "#7da9f7";
    if (!strcmp(name,"D1")) return "#ef8d8d";
    if (!strcmp(name,"H")) return "#79c996";
    if (!strcmp(name,"bridge")) return "#ffffff";
    if (!strcmp(name,"F")) return "#161616";
    if (!strcmp(name,"B")) return "#79c996";
    return "#cccccc";
}

static const char *tree_fill(const char *name) {
    if (!strcmp(name,"D0")) return "#e9d66f";
    if (!strcmp(name,"D1")) return "#f0ad73";
    if (!strcmp(name,"H")) return "#79c996";
    if (!strcmp(name,"bridge")) return "#ffffff";
    if (!strcmp(name,"F")) return "#161616";
    if (!strcmp(name,"B")) return "#b88968";
    return "#cccccc";
}

static const char *fill_for(const char *name, int tree) {
    return tree ? tree_fill(name) : ordinary_fill(name);
}

static int make_dir(const char *path) {
    if (mkdir(path, 0775) && errno != EEXIST) return 0;
    return 1;
}

static int ensure_output_dir(void) {
    return make_dir("img") && make_dir("img/rl7") && make_dir("img/rl7/rephex");
}

static double pdist(Pt a, Pt b) { return hypot(a.x-b.x, a.y-b.y); }

static int read_hat(Pt *hat, int *nh) {
    FILE *fp = fopen("tiles/hat.tile", "r");
    char line[256];
    int in = 0;
    *nh = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strncmp(line, "cycle:", 6)) { in = 1; continue; }
        if (!in) continue;
        int den, a, b;
        if (sscanf(line, " %d %d %d", &den, &a, &b) != 3) continue;
        double ux=0,uy=0,vx=0,vy=0, r=sqrt(3.0);
        if (den == 6) { ux=r/2; uy=-0.5; vx=r/2; vy=0.5; }
        else if (den == 4) { ux=r/4; uy=-0.25; vx=r/4; vy=0.25; }
        else if (den == 3) { ux=1/r; uy=0; vx=1/(2*r); vy=0.5; }
        else continue;
        if (*nh >= MAX_HAT) { fclose(fp); return 0; }
        hat[*nh] = (Pt){a*ux + b*vx, a*uy + b*vy};
        (*nh)++;
    }
    fclose(fp);
    g_nh = *nh;
    return *nh >= 14;
}

static Pt reflect_y(Pt p) { return (Pt){p.x, -p.y}; }

static Pt tx(Tf t, Pt p) {
    return (Pt){t.a*p.x+t.b*p.y+t.tx, t.c*p.x+t.d*p.y+t.ty};
}

static Tf sim_from_edges(Pt s0, Pt s1, Pt d0, Pt d1) {
    double sx=s1.x-s0.x, sy=s1.y-s0.y;
    double dx=d1.x-d0.x, dy=d1.y-d0.y;
    double scale=hypot(dx,dy)/hypot(sx,sy);
    if (scale <= 0) return (Tf){0,0,0,0,0,0};
    double th=atan2(dy,dx)-atan2(sy,sx);
    double co=scale*cos(th), si=scale*sin(th);
    Tf t={co,-si,si,co,0,0};
    Pt q={t.a*s0.x+t.b*s0.y, t.c*s0.x+t.d*s0.y};
    t.tx=d0.x-q.x; t.ty=d0.y-q.y;
    return t;
}

static void transform_poly(const Pt *base, Tf t, Pt *out) {
    for (int i=0;i<g_nh;i++) out[i]=tx(t,base[i]);
    for (int i=g_nh;i<MAX_HAT;i++) out[i]=out[g_nh-1];
}

/* Place a tile by matching complete indexed paths, not just one point.
   src_idx is the path on the new tile; dst_idx is the path on an already placed tile. */
static int place_by_path(const Pt *src_base, const int *src_idx,
                         const Pt *dst_poly, const int *dst_idx,
                         int n, Pt *out, double *maxerr) {
    Tf t=sim_from_edges(src_base[src_idx[0]], src_base[src_idx[n-1]],
                        dst_poly[dst_idx[0]], dst_poly[dst_idx[n-1]]);
    transform_poly(src_base, t, out);
    *maxerr=0.0;
    for (int i=0;i<n;i++) {
        double e=pdist(out[src_idx[i]], dst_poly[dst_idx[i]]);
        if (e>*maxerr) *maxerr=e;
    }
    return *maxerr < EPS;
}

static Tf affine3(Pt s0, Pt s1, Pt s2, Pt d0, Pt d1, Pt d2) {
    double x0=s0.x,y0=s0.y,x1=s1.x,y1=s1.y,x2=s2.x,y2=s2.y;
    double det = x0*(y1-y2)-y0*(x1-x2)+x1*y2-x2*y1;
    double A00=(d0.x*(y1-y2)-y0*(d1.x-d2.x)+d1.x*y2-d2.x*y1)/det;
    double A01=(x0*(d1.x-d2.x)-d0.x*(x1-x2)+x1*d2.x-x2*d1.x)/det;
    double A02=(x0*(y1*d2.x-d1.x*y2)-y0*(x1*d2.x-d1.x*x2)+d0.x*(x1*y2-x2*y1))/det;
    double A10=(d0.y*(y1-y2)-y0*(d1.y-d2.y)+d1.y*y2-d2.y*y1)/det;
    double A11=(x0*(d1.y-d2.y)-d0.y*(x1-x2)+x1*d2.y-x2*d1.y)/det;
    double A12=(x0*(y1*d2.y-d1.y*y2)-y0*(x1*d2.y-d1.y*x2)+d0.y*(x1*y2-x2*y1))/det;
    return (Tf){A00,A01,A10,A11,A02,A12};
}

static void add_bounds(Pt p, double *xmin,double *xmax,double *ymin,double *ymax,int *first) {
    if (*first) {
        *xmin=*xmax=p.x; *ymin=*ymax=p.y; *first=0;
    } else {
        if (p.x < *xmin) *xmin = p.x;
        if (p.x > *xmax) *xmax = p.x;
        if (p.y < *ymin) *ymin = p.y;
        if (p.y > *ymax) *ymax = p.y;
    }
}

static Pt rotate_pt(Pt p, unsigned step) {
    double a=M_PI*(30.0*(step%12))/180.0, co=cos(a), si=sin(a);
    return (Pt){co*p.x - si*p.y, si*p.x + co*p.y};
}

static void write_poly(FILE *fp, const Pt *p, int n, unsigned step,
                       const char *fill, double sw) {
    fprintf(fp, "<polygon points=\"");
    for (int i=0;i<n;i++) {
        Pt q=rotate_pt(p[i], step);
        fprintf(fp, "%.6f,%.6f%s", q.x, -q.y, i==n-1?"":" ");
    }
    fprintf(fp, "\" fill=\"%s\" stroke=\"#111111\" stroke-width=\"%.4f\"/>\n", fill, sw);
}

static int write_svg(const char *path, Pt polys[][MAX_HAT], const char **fills, int np,
                     const Pt *hex, int nhex, unsigned step) {
    if (!ensure_output_dir()) return 0;
    double xmin=0,xmax=0,ymin=0,ymax=0; int first=1;
    for (int i=0;i<np;i++) for (int j=0;j<g_nh;j++) add_bounds(rotate_pt(polys[i][j],step),&xmin,&xmax,&ymin,&ymax,&first);
    for (int j=0;j<nhex;j++) add_bounds(rotate_pt(hex[j],step),&xmin,&xmax,&ymin,&ymax,&first);
    double margin=.10, vx=xmin-margin, vy=-(ymax+margin), vw=xmax-xmin+2*margin, vh=ymax-ymin+2*margin;
    double width=1400, height=fmax(260,width*vh/vw);
    FILE *fp=fopen(path,"w");
    if(!fp) return 0;
    fprintf(fp,"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"%.6f %.6f %.6f %.6f\">\n",width,height,vx,vy,vw,vh);
    fprintf(fp,"<rect x=\"%.6f\" y=\"%.6f\" width=\"%.6f\" height=\"%.6f\" fill=\"#ffffff\"/>\n",vx,vy,vw,vh);
    fprintf(fp,"<!-- rephex_hacked edge-rule v4: D/DH/F generated from taught indexed joins -->\n");
    for (int i=0;i<np;i++) write_poly(fp, polys[i], g_nh, step, fills[i], .020);
    if (nhex) write_poly(fp, hex, nhex, step, "#161616", .020);
    fprintf(fp,"</svg>\n");
    fclose(fp);
    return 1;
}

static void hex_points(Pt *hex) {
    for(int k=0;k<6;k++) {
        double a=M_PI*(90.0+60.0*k)/180.0;
        hex[k]=(Pt){cos(a), sin(a)};
    }
}

static int audit_path(const Pt *a, const int *ai, const Pt *b, const int *bi, int n) {
    for (int i=0;i<n;i++) if (pdist(a[ai[i]], b[bi[i]]) > EPS) return 0;
    return 1;
}

static int audit_f_center(Pt polys[][MAX_HAT], int np, const Pt *hex) {
    for (int k=0;k<np;k++) {
        Pt prev=hex[(k+5)%6], cur=hex[k], next=hex[(k+1)%6];
        Pt mid_next={(next.x+cur.x)*.5,(next.y+cur.y)*.5};
        Pt mid_prev={(prev.x+cur.x)*.5,(prev.y+cur.y)*.5};
        if (pdist(polys[k][1], mid_next) > EPS) return 0;
        if (pdist(polys[k][2], cur) > EPS) return 0;
        if (pdist(polys[k][3], mid_prev) > EPS) return 0;
    }
    return 1;
}

static int audit_f_ring(Pt polys[][MAX_HAT], int np) {
    int a[4]={12,13,0,1}, b[4]={6,5,4,3};
    for (int k=0;k<np;k++) if(!audit_path(polys[k],a,polys[(k+1)%np],b,4)) return 0;
    return 1;
}

/*
   Source-of-truth edge rules taught in chat:

   D0:
     blue tile is the anchor orientation.
     From blue vertex 9 concavity attach reflected white vertex 3:
       blue edge(8,9,10) = reflected-white edge(2,3,4)
     From reflected-white vertex 9 attach red vertex 3:
       reflected-white edge(8,9,10) = red edge(2,3,4)

   DH0:
     Start with the same D arrangement.
     Attach pass H to an unoccupied blue/yellow edge:
       existing edge(0,1,2) = H edge(10,9,8)

   F1:
     false-center hex, with six pass/branch hats.
     for each branch hat:
       hat vertex 2 -> hex vertex
       hat vertices 1 and 3 -> adjacent hex edge midpoints
     ring:
       edge(12,13,0,1) = reverse edge(3,4,5,6)
*/
static int hacked_render(const char *axiom, unsigned level, int tree, unsigned rot) {
    Pt hat[MAX_HAT], refl[MAX_HAT];
    int nh=0;
    if(!read_hat(hat,&nh)) return 0;
    for(int i=0;i<nh;i++) refl[i]=reflect_y(hat[i]);
    for(int i=nh;i<MAX_HAT;i++){ hat[i]=hat[nh-1]; refl[i]=refl[nh-1]; }

    Pt polys[8][MAX_HAT]; const char *fills[8]; int np=0; Pt hex[6]={{0,0}};
    double err=0.0;

    if((!strcmp(axiom,"D") && level==0) || (!strcmp(axiom,"DH") && level==0)) {
        /* Anchor is the taught blue/yellow orientation.  Output order is bridge/anchor/red
           so the visible contact follows the taught blue -> white -> red chain while
           preserving painter order for shared edges. */
        memcpy(polys[np], hat, sizeof(hat)); fills[np++]=fill_for("D0",tree);

        int blue_e8910[3]={8,9,10};
        int white_e234[3]={2,3,4};
        if(!place_by_path(refl, white_e234, polys[0], blue_e8910, 3, polys[np], &err)) return 0;
        fills[np++]=fill_for("bridge",tree);

        int white_e8910[3]={8,9,10};
        int red_e234[3]={2,3,4};
        if(!place_by_path(hat, red_e234, polys[1], white_e8910, 3, polys[np], &err)) return 0;
        fills[np++]=fill_for("D1",tree);

        if(!audit_path(polys[0], blue_e8910, polys[1], white_e234, 3)) return 0;
        if(!audit_path(polys[1], white_e8910, polys[2], red_e234, 3)) return 0;

        if(!strcmp(axiom,"DH")) {
            int host_e012[3]={0,1,2};
            int green_e1098[3]={10,9,8};
            if(!place_by_path(hat, green_e1098, polys[0], host_e012, 3, polys[np], &err)) return 0;
            fills[np++]=fill_for("B",tree);
            if(!audit_path(polys[0], host_e012, polys[3], green_e1098, 3)) return 0;
        }

        return write_svg("img/rl7/rephex/current_hat.svg",polys,fills,np,hex,0,(rot+8)%12);
    }

    if(!strcmp(axiom,"F") && level==0) {
        hex_points(hex);
        return write_svg("img/rl7/rephex/current_hat.svg",polys,fills,0,hex,6,rot%12);
    }

    if(!strcmp(axiom,"F") && level==1) {
        hex_points(hex);
        for(int k=0;k<6;k++) {
            Pt prev=hex[(k+5)%6], cur=hex[k], next=hex[(k+1)%6];
            Pt mid_next={(next.x+cur.x)*.5,(next.y+cur.y)*.5};
            Pt mid_prev={(prev.x+cur.x)*.5,(prev.y+cur.y)*.5};
            Tf t=affine3(hat[1],hat[2],hat[3], mid_next,cur,mid_prev);
            transform_poly(hat,t,polys[np]);
            fills[np++]=fill_for("B",tree);
        }
        if(!audit_f_center(polys,np,hex)) return 0;
        if(!audit_f_ring(polys,np)) return 0;
        return write_svg("img/rl7/rephex/current_hat.svg",polys,fills,np,hex,6,rot%12);
    }

    return 2;
}

int main(int argc, char **argv) {
    const char *axiom=NULL; unsigned level=0, rot=0; int have_level=0, tree=0, hats=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--palette") && i+1<argc && !strcmp(argv[i+1],"tree")) { tree=1; i++; }
        else if(!strcmp(argv[i],"--hat-svg")) hats=1;
        else if(!strcmp(argv[i],"--rotation-step") && i+1<argc) { rot=(unsigned)atoi(argv[++i]); }
        else if(argv[i][0]!='-' && !axiom) axiom=argv[i];
        else if(argv[i][0]!='-' && axiom && !have_level) { level=(unsigned)atoi(argv[i]); have_level=1; }
    }
    if(hats && axiom) {
        int r=hacked_render(axiom,level,tree,rot);
        if(r==1) {
            printf("hat-status: complete placed=hacked-edge-rule-v4 axiom=%s level=%u\n",axiom,level);
            printf("hat-svg: img/rl7/rephex/current_hat.svg\n");
            return 0;
        }
        fprintf(stderr,"rephex_hacked: no hacked edge-rule render for %s %u\n",axiom,level);
        return 1;
    }
    fprintf(stderr,"usage: rephex_hacked AXIOM LEVEL [--palette tree] --hat-svg [--rotation-step n]\n");
    return 2;
}
