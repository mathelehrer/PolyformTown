/*
 * hce.c -- RL7 HCE local collar explorer REEPL.
 *
 * Frontend for the RL7 boundary/comp/evo mini-stack.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "boundary7.h"
#include "comp7.h"
#include "evo7.h"
#include "usage/print7.h"

#define SVG_PATH_MAX    256

/* Display convention: top = slots 3 2 1, bottom = slots 4 5 0. */
static const int display_top[3] = {3, 2, 1};
static const int display_bottom[3] = {4, 5, 0};

static void centered(char out[16], const char *in){
    size_t w = 11;
    size_t n = strlen(in);
    if(n > w) n = w;
    size_t left = (w - n) / 2;
    size_t pos = 0;
    for(size_t i=0;i<left;i++) out[pos++] = ' ';
    for(size_t i=0;i<n;i++) out[pos++] = in[i];
    while(pos < w) out[pos++] = ' ';
    out[pos] = '\0';
}

static void cell_lines(const Cell *c, int label_q, int label_r, char lines[5][128]){
    const char *t0 = tok_empty(&c->slot[display_top[0]]) ? " " : c->slot[display_top[0]].s;
    const char *t1 = tok_empty(&c->slot[display_top[1]]) ? " " : c->slot[display_top[1]].s;
    const char *t2 = tok_empty(&c->slot[display_top[2]]) ? " " : c->slot[display_top[2]].s;
    const char *b0 = tok_empty(&c->slot[display_bottom[0]]) ? " " : c->slot[display_bottom[0]].s;
    const char *b1 = tok_empty(&c->slot[display_bottom[1]]) ? " " : c->slot[display_bottom[1]].s;
    const char *b2 = tok_empty(&c->slot[display_bottom[2]]) ? " " : c->slot[display_bottom[2]].s;
    char coord[32], mid[16];
    snprintf(coord, sizeof(coord), "(%d,%d)", label_q, label_r);
    centered(mid, coord);
    snprintf(lines[0],128,"┌───┬───┬───┐");
    snprintf(lines[1],128,"│ %-1.1s   %-1.1s   %-1.1s │",t0,t1,t2);
    snprintf(lines[2],128,"│%s│", mid);
    snprintf(lines[3],128,"│ %-1.1s   %-1.1s   %-1.1s │",b0,b1,b2);
    snprintf(lines[4],128,"└───┴───┴───┘");
}

static void ellipsize(char *buf, size_t sz){
    size_t n = strlen(buf);
    if(sz >= 4 && n >= sz){
        buf[sz-4] = '.';
        buf[sz-3] = '.';
        buf[sz-2] = '.';
        buf[sz-1] = '\0';
    }
}

static void print_short_option(int idx, const char *s){
    char buf[77];
    const char *msg = s ? s : "";
    int n = snprintf(buf, sizeof(buf), "%d. %.72s", idx, msg);
    if(n < 0){
        snprintf(buf, sizeof(buf), "%d.", idx);
    }
    ellipsize(buf, sizeof(buf));
    printf("%-76s\n", buf);
}

static void print_status_line(const char *status){
    char buf[77];
    const char *msg = (status && status[0]) ? status : "ready";
    int n = snprintf(buf, sizeof(buf), "STATUS: %.68s", msg);
    if(n < 0){
        snprintf(buf, sizeof(buf), "STATUS: error formatting status");
    }
    ellipsize(buf, sizeof(buf));
    printf("%-76s\n", buf);
}

static void print_blank_line(void){ printf("%-76s\n", ""); }

static void render_help_body(void){
    static const char *help[] = {
        "Commands:",
        "  1..N   apply action",
        "  a      apply all current actions as one parallel step",
        "  p      print hex SVG and open it with xdg-open",
        "  ↑ / ↓  scroll completion list",
        "  u      undo",
        "  r      reset to central edge",
        "  q      quit",
        "  ?      help",
        "",
        "Press return to return to the diagram."
    };
    enum { BODY_ROWS = 21 };
    int nhelp = (int)(sizeof(help) / sizeof(help[0]));
    int top = (BODY_ROWS - nhelp) / 2;
    if(top < 0) top = 0;
    for(int i=0;i<BODY_ROWS;i++){
        if(i >= top && i < top + nhelp) printf("%-76s\n", help[i - top]);
        else print_blank_line();
    }
}

static int render_board_lines(const State *s, char out[][256], int max_lines){
    int nout=0;
    int rr[3]={1,0,-1};
    for(int ri=0;ri<3;ri++){
        int r=rr[ri], idx[16], cnt=0;
        for(int i=0;i<s->ncell;i++) if(s->cells[i].r==r && cnt < 16) idx[cnt++]=i;
        for(int i=0;i<cnt;i++){
            for(int j=i+1;j<cnt;j++){
                if(s->cells[idx[j]].q < s->cells[idx[i]].q){
                    int t=idx[i]; idx[i]=idx[j]; idx[j]=t;
                }
            }
        }
        char blocks[16][5][128];
        for(int i=0;i<cnt;i++){
            const Cell *cell = &s->cells[idx[i]];
            int label_q = cell->q;
            int label_r = cell->r;
            if(label_r == -1) label_q += 1;
            cell_lines(cell, label_q, label_r, blocks[i]);
        }
        const char *indent=(r&1)?"       ":"";
        for(int line=0;line<5 && nout<max_lines;line++){
            int used=snprintf(out[nout], 256, "%s", indent);
            if(used < 0) used = 0;
            if(used >= 256) used = 255;
            for(int i=0;i<cnt;i++){
                snprintf(out[nout] + strlen(out[nout]),
                         256 - strlen(out[nout]), "%s%s",
                         blocks[i][line], (i+1<cnt) ? " " : "");
            }
            nout++;
        }
    }
    return nout;
}

static int open_svg_path(const char *path){
    const char *no_open = getenv("HCE_NO_OPEN");
    if(no_open && no_open[0] && strcmp(no_open, "0") != 0) return 1;
    char cmd[SVG_PATH_MAX + 64];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", path);
    return system(cmd) == 0;
}

static void render(const Model *m, const State *s, Action *acts, int na,
                   int action_scroll, const char *status, int help_mode){
    (void)m;
    printf("RL7 HCE | q quit | ? help | u undo | a step | p print | r reset | 80x24\n");
    if(help_mode){
        render_help_body();
        print_blank_line();
        printf("$: ");
        fflush(stdout);
        return;
    }

    char board_lines[24][256];
    int nboard = render_board_lines(s, board_lines, 24);
    for(int i=0;i<nboard;i++) printf("%s\n", board_lines[i]);

    int visible = na < 4 ? na : 4;
    int max_scroll = na > visible ? na - visible : 0;
    if(action_scroll < 0) action_scroll = 0;
    if(action_scroll > max_scroll) action_scroll = max_scroll;
    if(na > visible){
        printf("Complete: actions %d-%d of %d  (↑/↓ scroll)\n",
               action_scroll + 1, action_scroll + visible, na);
    } else {
        printf("Complete: showing %d of %d actions\n", visible, na);
    }
    for(int i=0;i<4;i++){
        int ai = action_scroll + i;
        if(i<visible && ai<na) print_short_option(ai+1, acts[ai].title);
        else print_blank_line();
    }
    print_status_line(status);
    print_blank_line();
    printf("$: "); fflush(stdout);
}

static int parse_int_strict(const char *s,int *out){
    while(isspace((unsigned char)*s)) s++;
    if(!*s) return 0;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if(errno || end == s) return 0;
    while(isspace((unsigned char)*end)) end++;
    if(*end || v < 1 || v > 1000000) return 0;
    *out = (int)v;
    return 1;
}

typedef enum { INPUT_SUBMIT, INPUT_SCROLL_UP, INPUT_SCROLL_DOWN, INPUT_EOF } InputKind;

typedef struct {
    int tty;
    struct termios oldt;
    int active;
} TermMode;

static void term_restore(TermMode *tm){
    if(tm->active){
        tcsetattr(STDIN_FILENO, TCSANOW, &tm->oldt);
        tm->active = 0;
    }
}

static int term_setup(TermMode *tm){
    memset(tm, 0, sizeof(*tm));
    tm->tty = isatty(STDIN_FILENO);
    if(!tm->tty) return 1;
    if(tcgetattr(STDIN_FILENO, &tm->oldt) != 0) return 0;
    struct termios raw = tm->oldt;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if(tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return 0;
    tm->active = 1;
    return 1;
}

static InputKind read_input(TermMode *tm, char *line, size_t linesz){
    line[0] = '\0';
    if(!tm->tty){
        if(!fgets(line, linesz, stdin)) return INPUT_EOF;
        line[strcspn(line, "\r\n")] = '\0';
        if(strcmp(line, "\033[A") == 0) return INPUT_SCROLL_UP;
        if(strcmp(line, "\033[B") == 0) return INPUT_SCROLL_DOWN;
        return INPUT_SUBMIT;
    }

    size_t n = 0;
    for(;;){
        unsigned char ch;
        ssize_t got = read(STDIN_FILENO, &ch, 1);
        if(got == 0) return INPUT_EOF;
        if(got < 0){
            if(errno == EINTR) continue;
            return INPUT_EOF;
        }
        if(ch == '\r' || ch == '\n'){
            line[n] = '\0';
            putchar('\n');
            fflush(stdout);
            return INPUT_SUBMIT;
        }
        if(ch == 0x7f || ch == '\b'){
            if(n > 0){
                n--;
                line[n] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if(ch == 0x04){
            if(n == 0) return INPUT_EOF;
            continue;
        }
        if(ch == 0x1b){
            unsigned char seq[2];
            ssize_t g1 = read(STDIN_FILENO, &seq[0], 1);
            if(g1 == 1 && seq[0] == '['){
                ssize_t g2 = read(STDIN_FILENO, &seq[1], 1);
                if(g2 == 1){
                    if(seq[1] == 'A') return INPUT_SCROLL_UP;
                    if(seq[1] == 'B') return INPUT_SCROLL_DOWN;
                }
            }
            continue;
        }
        if(isprint(ch)){
            if(n + 1 < linesz){
                line[n++] = (char)ch;
                line[n] = '\0';
                putchar((int)ch);
                fflush(stdout);
            }
        }
    }
}

static void erase_frame(int first, int after_enter){
    if(first) return;
    printf("\r\033[K");
    int rows = after_enter ? 24 : 23;
    for(int i=0;i<rows;i++) printf("\033[1A\r\033[K");
}

static int max_action_scroll(int na){ return na > 4 ? na - 4 : 0; }

int main(int argc, char **argv){
    const char *model_path = DEFAULT_MODEL;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--model")==0 && i+1<argc) model_path=argv[++i];
        else if(strcmp(argv[i],"--help")==0){ printf("usage: %s [--model data/rl6/refined_model.dat]\n", argv[0]); return 0; }
        else { fprintf(stderr,"ERR: unknown argument '%s'\n",argv[i]); return 2; }
    }
    Model m; char status[256], err[256];
    if(!load_model(model_path,&m,err,sizeof(err))){ fprintf(stderr,"ERR: %s\n",err); return 1; }

    TermMode tm;
    if(!term_setup(&tm)){
        fprintf(stderr, "ERR: could not configure terminal input\n");
        return 1;
    }

    State s; History h; Action acts[MAX_ACTIONS]; char line[256];
    int first_frame=1, action_scroll=0, help_mode=0, after_enter=0;
    h.n=0;
    init_state(&s);
    snprintf(status,sizeof(status),"loaded %s: %d tiles, %d edge rules, %d vertex words",model_path,m.ntile,m.nrule,m.nvword);

    for(;;){
        int na=enumerate_actions(&m,&s,acts);
        int max_scroll = max_action_scroll(na);
        if(action_scroll > max_scroll) action_scroll = max_scroll;
        if(action_scroll < 0) action_scroll = 0;
        erase_frame(first_frame, after_enter);
        first_frame=0;
        after_enter=0;
        render(&m,&s,acts,na,action_scroll,status,help_mode);

        InputKind input = read_input(&tm, line, sizeof(line));
        if(input == INPUT_EOF){ printf("\nEOF\n"); break; }
        if(input == INPUT_SCROLL_UP || input == INPUT_SCROLL_DOWN){
            if(!help_mode){
                if(input == INPUT_SCROLL_UP && action_scroll > 0) action_scroll--;
                if(input == INPUT_SCROLL_DOWN && action_scroll < max_action_scroll(na)) action_scroll++;
            }
            after_enter = 0;
            continue;
        }

        after_enter = 1;
        char *p=line;
        while(isspace((unsigned char)*p)) p++;

        if(help_mode){
            help_mode = 0;
            continue;
        }
        if(!*p){ snprintf(status,sizeof(status),"ready"); continue; }
        if(strcmp(p,"r")==0){ init_state(&s); h.n=0; action_scroll=0; snprintf(status,sizeof(status),"reset to central edge"); continue; }
        if(strcmp(p,"q")==0||strcmp(p,"quit")==0){ break; }
        if(strcmp(p,"?")==0||strcmp(p,"help")==0){ help_mode=1; continue; }
        if(strcmp(p,"u")==0){ if(pop_hist(&h,&s)) snprintf(status,sizeof(status),"undo"); else snprintf(status,sizeof(status),"ERR: nothing to undo"); action_scroll=0; continue; }
        if(strcmp(p,"p")==0 || strcmp(p,"ph")==0){
            char svg_path[SVG_PATH_MAX], svg_err[240];
            snprintf(svg_path, sizeof(svg_path), "img/hce_out.svg");
            if(system("mkdir -p img") != 0){
                snprintf(status,sizeof(status),"ERR: could not create img directory");
                continue;
            }
            if(print7_write_hex_svg(&s, &m, svg_path, svg_err, sizeof(svg_err))){
                if(open_svg_path(svg_path)) snprintf(status,sizeof(status),"wrote hex SVG %.196s",svg_path);
                else snprintf(status,sizeof(status),"wrote hex SVG %.176s; open failed",svg_path);
            } else {
                snprintf(status,sizeof(status),"ERR: %.220s",svg_err);
            }
            continue;
        }
        if(strcmp(p,"a")==0){
            State before=s;
            if(apply_parallel_actions(&m, &s, acts, na, status, sizeof(status))){
                push_hist(&h,&before);
            }
            action_scroll=0;
            continue;
        }
        int choice=0;
        if(!parse_int_strict(p,&choice)){
            snprintf(status,sizeof(status),"ERR: parse failed; type number, q, r, u, a, p, ph, or ?");
            continue;
        }
        if(choice<1 || choice>na){ snprintf(status,sizeof(status),"ERR: action %d out of range 1..%d",choice,na); continue; }
        push_hist(&h,&s);
        if(!apply_action(&s,&acts[choice-1],status,sizeof(status))) pop_hist(&h,&s);
        action_scroll=0;
    }
    term_restore(&tm);
    printf("bye\n");
    return 0;
}
