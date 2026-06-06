#define _POSIX_C_SOURCE 200809L

/*
 * rephex.c -- Mountain and Range one-screen inspector.
 *
 * The interaction loop follows the existing HCE terminal frontend: fixed
 * one-screen frames, raw one-line command input, and status kept on screen.  The
 * batch printer owns geometry and exports; this UI owns only current state.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define STATUS_MAX 200
#define LINE_MAX   512
#define TERM_COLS  80
#define TERM_ROWS  24
/* Keep the last column unused to avoid terminal auto-wrap. */
#define FRAME_COLS 79
#define BODY_ROWS  16

typedef struct { const char *code; const char *description; } Axiom;
static const Axiom axioms[] = {
    {"D",  "single dimer"},
    {"H",  "single H"},
    {"DH", "fixed-point straight row"},
    {"B3", "brown / branch C3"},
    {"L3", "green / leaf C3"},
    {"F",  "false center generator"},
};

typedef struct {
    int axiom;
    unsigned depth;
    int palette;
    int choosing_axiom;
    int needs_update;
    char status[STATUS_MAX];
} ReplState;

typedef enum { INPUT_SUBMIT, INPUT_SCROLL_UP, INPUT_SCROLL_DOWN, INPUT_EOF } InputKind;
typedef struct { int tty; struct termios oldt; int active; } TermMode;

static void print_blank_line(void) { printf("%-*s\n", FRAME_COLS, ""); }
static void print_fixed(const char *s) { printf("%-*.*s\n", FRAME_COLS, FRAME_COLS, s ? s : ""); }

static void status_line(const char *status) {
    char line[STATUS_MAX + 12];
    snprintf(line, sizeof(line), "STATUS: %.164s", (status && status[0]) ? status : "ready");
    print_fixed(line);
}

static void trim(char *text) {
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1])) text[--n] = '\0';
    size_t start = 0;
    while (text[start] && isspace((unsigned char)text[start])) start++;
    if (start) memmove(text, text + start, strlen(text + start) + 1);
}

static void lower_copy(char *dst, size_t size, const char *src) {
    size_t i = 0;
    while (src[i] && i + 1 < size) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

static int parse_depth(const char *input, unsigned *depth) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(input, &end, 10);
    if (errno || !input[0] || !end || *end || value > 6) return 0;
    *depth = (unsigned)value;
    return 1;
}

static int parse_rotation(const char *input, unsigned *step) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(input, &end, 10);
    if (errno || !input[0] || !end || *end || value > 12) return 0;
    *step = (unsigned)value;
    return 1;
}

static int parse_axiom(const char *input) {
    char lower[32];
    lower_copy(lower, sizeof(lower), input);
    for (int i = 0; i < (int)(sizeof(axioms) / sizeof(axioms[0])); i++) {
        char code[8];
        lower_copy(code, sizeof(code), axioms[i].code);
        if (!strcmp(lower, code)) return i;
    }
    if (strlen(lower) == 1 && lower[0] >= '1' && lower[0] <= '6')
        return lower[0] - '1';
    return -1;
}

static const char *palette_name(int palette) {
    return palette == 1 ? "tree" : (palette == 2 ? "split" : "ordinary");
}

static const char *palette_arg(int palette) {
    return palette == 1 ? " --palette tree" : (palette == 2 ? " --palette split" : "");
}

static int parse_palette_name(const char *input, int *palette) {
    char lower[32];
    lower_copy(lower, sizeof(lower), input);
    if (!strcmp(lower, "o") || !strcmp(lower, "ordinary")) { *palette = 0; return 1; }
    if (!strcmp(lower, "t") || !strcmp(lower, "tree")) { *palette = 1; return 1; }
    if (!strcmp(lower, "s") || !strcmp(lower, "split") || !strcmp(lower, "split-leaf") || !strcmp(lower, "leaf-split")) { *palette = 2; return 1; }
    return 0;
}

/* HCE-compatible raw terminal input and fixed-frame redraw. */
static void term_restore(TermMode *tm) {
    if (tm->active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &tm->oldt);
        tm->active = 0;
    }
}

static int term_setup(TermMode *tm) {
    memset(tm, 0, sizeof(*tm));
    tm->tty = isatty(STDIN_FILENO);
    if (!tm->tty) return 1;
    if (tcgetattr(STDIN_FILENO, &tm->oldt) != 0) return 0;
    struct termios raw = tm->oldt;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return 0;
    tm->active = 1;
    return 1;
}

static InputKind read_input(TermMode *tm, char *line, size_t linesz) {
    line[0] = '\0';
    if (!tm->tty) {
        if (!fgets(line, (int)linesz, stdin)) return INPUT_EOF;
        line[strcspn(line, "\r\n")] = '\0';
        trim(line);
        return INPUT_SUBMIT;
    }
    size_t n = 0;
    for (;;) {
        unsigned char ch;
        ssize_t got = read(STDIN_FILENO, &ch, 1);
        if (got == 0) return INPUT_EOF;
        if (got < 0) { if (errno == EINTR) continue; return INPUT_EOF; }
        if (ch == '\r' || ch == '\n') {
            line[n] = '\0'; putchar('\n'); fflush(stdout); trim(line); return INPUT_SUBMIT;
        }
        if (ch == 0x7f || ch == '\b') {
            if (n > 0) { n--; line[n] = '\0'; printf("\b \b"); fflush(stdout); }
            continue;
        }
        if (ch == 0x04) { if (n == 0) return INPUT_EOF; continue; }
        if (ch == 0x1b) {
            unsigned char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[' &&
                read(STDIN_FILENO, &seq[1], 1) == 1) {
                if (seq[1] == 'A') return INPUT_SCROLL_UP;
                if (seq[1] == 'B') return INPUT_SCROLL_DOWN;
            }
            continue;
        }
        if (isprint(ch) && n + 1 < linesz) {
            line[n++] = (char)ch; line[n] = '\0'; putchar((int)ch); fflush(stdout);
        }
    }
}

static void erase_frame(int first, int after_enter) {
    if (first) return;
    /*
     * The interactive frame leaves row 24 unused.  The prompt is on row 23,
     * so return advances to row 24 without scrolling.  Replace only the
     * current frame instead of clearing the terminal/scrollback.
     */
    printf("\r\033[K");
    int rows_up = after_enter ? 23 : 22;
    for (int i = 0; i < rows_up; i++) printf("\033[1A\r\033[K");
}

static int refresh_current(ReplState *state) {
    if (!state->needs_update) return 1;
    char command[LINE_MAX * 4];

    /*
     * Main REPL update flow:
     *   1. Generate current data.
     *   2. Write hex SVG.
     *   3. Write hat SVG.
     *
     * All three calls go through rephex_print.  If a low-lying axiom needs
     * special hat geometry, rephex_print may ask the leaf helper for that
     * one hat-SVG product.  The REPL does not call the helper directly.
     */
    snprintf(command, sizeof(command),
             "mkdir -p data/run/rl7/rephex && "
             "REPHEX_NO_OPEN=1 ./bin/rephex_print %s %u --write-current-only --no-color "
             "> data/run/rl7/rephex/current_print.dat 2>&1 && "
             "REPHEX_NO_OPEN=1 ./bin/rephex_print %s %u --from-current%s --svg --rotation-step 0 --no-color "
             ">> data/run/rl7/rephex/current_print.dat 2>&1 && "
             "REPHEX_NO_OPEN=1 ./bin/rephex_print %s %u --from-current%s --hat-svg --rotation-step 0 --no-color "
             ">> data/run/rl7/rephex/current_print.dat 2>&1",
             axioms[state->axiom].code, state->depth,
             axioms[state->axiom].code, state->depth,
             palette_arg(state->palette),
             axioms[state->axiom].code, state->depth,
             palette_arg(state->palette));
    int rc = system(command);
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) return 0;
    state->needs_update = 0;
    return 1;
}

static int capture_diagram(const ReplState *state, char body[BODY_ROWS][LINE_MAX], int *clipped) {
    char command[LINE_MAX];
    snprintf(command, sizeof(command),
             "./bin/rephex_print %s %u --from-current%s --repl-window --preview-rows %d --preview-cols %d --color 2>/dev/null",
             axioms[state->axiom].code, state->depth,
             palette_arg(state->palette), BODY_ROWS, FRAME_COLS);
    FILE *pipe = popen(command, "r");
    if (!pipe) return 0;
    *clipped = 0;
    int n = 0;
    char line[LINE_MAX];
    while (fgets(line, sizeof(line), pipe)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "@clipped=", 9)) { *clipped = atoi(line + 9) != 0; continue; }
        if (n < BODY_ROWS) snprintf(body[n++], LINE_MAX, "%s", line);
        else *clipped = 1;
    }
    int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 0;
    if (*clipped) snprintf(body[BODY_ROWS - 1], LINE_MAX, "... preview clipped; use PH to inspect SVG");
    while (n < BODY_ROWS) body[n++][0] = '\0';
    return 1;
}

static int export_current(ReplState *state, int hats, unsigned rotation_step,
                          char *status, size_t status_size) {
    char command[LINE_MAX * 3];

    /*
     * P/PH are force-refresh + open/export commands.  They never branch the
     * REPL around rephex_print.  The only allowed hack path is inside
     * rephex_print's hat-SVG stage.
     */
    if (!refresh_current(state)) {
        snprintf(status, status_size,
                 "ERR: refresh failed; see data/run/rl7/rephex/current_print.dat");
        return 0;
    }

    snprintf(command, sizeof(command),
             "mkdir -p data/run/rl7/rephex && "
             "./bin/rephex_print %s %u --from-current%s %s --rotation-step %u --no-color "
             "> data/run/rl7/rephex/current_print.dat 2>&1",
             axioms[state->axiom].code, state->depth,
             palette_arg(state->palette),
             hats ? "--hat-svg" : "--svg", rotation_step);
    int rc = system(command);
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
        snprintf(status, status_size, "ERR: print failed; see data/run/rl7/rephex/current_print.dat");
        return 0;
    }
    if (hats) snprintf(status, status_size, "printed hats rot=%u deg: img/rl7/rephex/current_hat.svg", rotation_step * 30);
    else snprintf(status, status_size, "printed hexes rot=%u deg: img/rl7/rephex/current.svg", rotation_step * 30);
    return 1;
}

static void render_axiom_chooser(const ReplState *state) {
    (void)state;
    print_fixed("REPHEX | choose axiom | selection resets depth to 0 | Q quit");
    print_blank_line();
    print_fixed("Choose axiom:");
    print_blank_line();
    for (int i = 0; i < (int)(sizeof(axioms) / sizeof(axioms[0])); i++) {
        char line[80];
        snprintf(line, sizeof(line), "  %d. %-2s  %s", i + 1, axioms[i].code, axioms[i].description);
        print_fixed(line);
    }
    for (int i = 0; i < 11; i++) print_blank_line();
    print_fixed("Enter code or number; return cancels.");
    printf("$: "); fflush(stdout);
}

static const char *legend_ansi(char state, int palette) {
    if (state == 'F') return "\033[38;5;240m";
    if (!palette) {
        if (state == '0') return "\033[38;5;27m";
        if (state == '1') return "\033[38;5;160m";
        return "\033[38;5;35m";
    }
    if (palette == 2) {
        if (state == '0') return "\033[38;5;178m";
        if (state == '1') return "\033[38;5;166m";
        if (state == 'B') return "\033[38;5;173m";
        if (state == 'C') return "\033[38;5;180m";
        if (state == 'T') return "\033[38;5;34m";
        if (state == 't') return "\033[38;5;120m";
        return "\033[38;5;35m";
    }
    if (state == '0') return "\033[38;5;178m";
    if (state == '1') return "\033[38;5;166m";
    if (state == 'B') return "\033[38;5;173m";
    if (state == 'C') return "\033[38;5;180m";
    return "\033[38;5;35m";
}

static void print_legend(const ReplState *state) {
    if (state->palette == 1) {
        printf("tree: %s⬢\033[0m pass  %s⬢\033[0m cap  %s⬢\033[0m leaf  %s⬢\033[0m D0  %s⬢\033[0m D1  %s⬢\033[0m F\n",
               legend_ansi('B', 1), legend_ansi('C', 1), legend_ansi('G', 1),
               legend_ansi('0', 1), legend_ansi('1', 1), legend_ansi('F', 1));
    } else if (state->palette == 2) {
        printf("split: %s⬢\033[0m pass  %s⬢\033[0m cap  %s⬢\033[0m tri-leaf  %s⬢\033[0m two-row-leaf  %s⬢\033[0m unknown-leaf  %s⬢\033[0m D0  %s⬢\033[0m D1  %s⬢\033[0m F\n",
               legend_ansi('B', 2), legend_ansi('C', 2), legend_ansi('T', 2),
               legend_ansi('G', 2), legend_ansi('t', 2), legend_ansi('0', 2),
               legend_ansi('1', 2), legend_ansi('F', 2));
    } else {
        printf("ordinary: %s⬢\033[0m = H  %s⬢\033[0m = D0  %s⬢\033[0m = D1  %s⬢\033[0m = F\n",
               legend_ansi('G', 0), legend_ansi('0', 0), legend_ansi('1', 0),
               legend_ansi('F', 0));
    }
}

static void render_state(ReplState *state) {
    char line[STATUS_MAX + 64];
    char body[BODY_ROWS][LINE_MAX];
    int clipped = 0;
    int have_diagram = refresh_current(state) && capture_diagram(state, body, &clipped);
    print_fixed("REPHEX | A axiom | +/- depth | P print | PH hex | T color | ? help | Q quit");
    snprintf(line, sizeof(line), "axiom: %-2s %-25s  depth: %-2u  palette: %s",
             axioms[state->axiom].code, axioms[state->axiom].description, state->depth,
             palette_name(state->palette));
    print_fixed(line);
    print_blank_line();
    for (int i = 0; i < BODY_ROWS; i++) printf("%s\n", have_diagram ? body[i] : "");
    print_blank_line();
    if (!have_diagram) status_line("ERR: rephex_print failed");
    else if (clipped) {
        snprintf(line, sizeof(line), "%s%spreview clipped",
                 (state->status[0] && strcmp(state->status, "ready")) ? state->status : "",
                 (state->status[0] && strcmp(state->status, "ready")) ? " | " : "");
        status_line(line);
    } else status_line(state->status);
    print_legend(state);
    printf("$: "); fflush(stdout);
}

static void render_help(void) {
    print_fixed("REPHEX | help | Q quit");
    print_blank_line();
    print_fixed("Commands:");
    print_fixed("  A           choose axiom from menu");
    print_fixed("  A H         choose axiom immediately; resets depth to 0");
    print_fixed("  A H 2       choose axiom H immediately at depth 2");
    print_fixed("  0 .. 6      set inflation depth immediately");
    print_fixed("  + / -       increase or decrease inflation depth");
    print_fixed("  P [n]       print hats; DH phase + n x 30 degrees (default 0)");
    print_fixed("  PH [n]      print/open hexes; rotate n x 30 degrees (default 0)");
    print_fixed("  T           toggle palette O -> T/tree -> S/split");
    print_fixed("  T O|T|S     direct palette: ordinary, tree, split");
    print_fixed("  Q           quit");
    for (int i = 0; i < 8; i++) print_blank_line();
    print_fixed("Press return to return.");
    printf("$: "); fflush(stdout);
}

static void usage(const char *argv0) {
    printf("usage: %s\n", argv0);
    printf("Interactive Mountain and Range inspector. For batch output use ./bin/rephex_print.\n");
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) { usage(argv[0]); return 0; }
        fprintf(stderr, "rephex is interactive; use ./bin/rephex_print for command-line depiction\n");
        return 2;
    }
    TermMode term;
    if (!term_setup(&term)) { fprintf(stderr, "ERR: could not configure terminal input\n"); return 1; }
    ReplState state = {1, 0, 0, 0, 1, "ready"}; /* H at depth zero. */
    char input[64], command[64];
    int first_frame = 1, after_enter = 0, help = 0;
    for (;;) {
        erase_frame(first_frame, after_enter); first_frame = 0; after_enter = 0;
        if (help) render_help();
        else if (state.choosing_axiom) render_axiom_chooser(&state);
        else render_state(&state);
        InputKind ik = read_input(&term, input, sizeof(input));
        if (ik == INPUT_EOF) break;
        if (ik != INPUT_SUBMIT) continue;
        after_enter = 1;
        if (help) { help = 0; continue; }
        if (state.choosing_axiom) {
            if (!input[0]) { state.choosing_axiom = 0; snprintf(state.status, sizeof(state.status), "ready"); continue; }
            lower_copy(command, sizeof(command), input);
            if (!strcmp(command, "q") || !strcmp(command, "quit")) break;
            int next = parse_axiom(input);
            if (next < 0) { snprintf(state.status, sizeof(state.status), "ERR: unknown axiom '%s'", input); continue; }
            state.axiom = next; state.depth = 0; state.choosing_axiom = 0; state.needs_update = 1;
            snprintf(state.status, sizeof(state.status), "selected %s; depth reset to 0", axioms[next].code);
            continue;
        }
        lower_copy(command, sizeof(command), input);
        if (!command[0]) { snprintf(state.status, sizeof(state.status), "ready"); continue; }
        if (!strcmp(command, "q") || !strcmp(command, "quit")) break;
        if (!strcmp(command, "?") || !strcmp(command, "help")) { help = 1; continue; }
        if (!strcmp(command, "a")) { state.choosing_axiom = 1; continue; }
        if (command[0] == 'a' && isspace((unsigned char)command[1])) {
            char axiom_text[16] = {0}, depth_text[16] = {0}, extra[16] = {0};
            int fields = sscanf(command + 1, " %15s %15s %15s", axiom_text, depth_text, extra);
            int next = fields >= 1 ? parse_axiom(axiom_text) : -1;
            unsigned requested_depth = 0;
            if (next < 0 || fields > 2 || (fields == 2 && !parse_depth(depth_text, &requested_depth))) {
                snprintf(state.status, sizeof(state.status), "ERR: use A H or A H 2");
                continue;
            }
            state.axiom = next;
            state.depth = fields == 2 ? requested_depth : 0;
            state.needs_update = 1;
            snprintf(state.status, sizeof(state.status), "selected %s; depth %u",
                     axioms[next].code, state.depth);
            continue;
        }
        {
            unsigned requested_depth = 0;
            if (parse_depth(command, &requested_depth)) {
                state.depth = requested_depth;
                state.needs_update = 1;
                snprintf(state.status, sizeof(state.status), "depth set to %u", state.depth);
                continue;
            }
        }
        if (!strcmp(command, "+")) {
            if (state.depth >= 6) snprintf(state.status, sizeof(state.status), "depth already at maximum 6");
            else { state.depth++; state.needs_update = 1; snprintf(state.status, sizeof(state.status), "depth increased to %u", state.depth); }
            continue;
        }
        if (!strcmp(command, "-")) {
            if (!state.depth) snprintf(state.status, sizeof(state.status), "depth already at 0");
            else { state.depth--; state.needs_update = 1; snprintf(state.status, sizeof(state.status), "depth decreased to %u", state.depth); }
            continue;
        }
        if (!strcmp(command, "t")) {
            state.palette = (state.palette + 1) % 3;
            state.needs_update = 1;
            snprintf(state.status, sizeof(state.status), "palette: %s", palette_name(state.palette));
            continue;
        }
        if (command[0] == 't' && isspace((unsigned char)command[1])) {
            char pal[32] = {0}, extra[32] = {0};
            int fields = sscanf(command + 1, " %31s %31s", pal, extra);
            int parsed = 0;
            if (fields == 1 && parse_palette_name(pal, &parsed)) {
                state.palette = parsed;
                state.needs_update = 1;
                snprintf(state.status, sizeof(state.status), "palette: %s", palette_name(state.palette));
            } else {
                snprintf(state.status, sizeof(state.status), "ERR: use T or T O|T|S");
            }
            continue;
        }
        if (!strncmp(command, "ph", 2) && (command[2] == '\0' || isspace((unsigned char)command[2]))) {
            unsigned step = 0; char extra[16] = {0};
            int fields = sscanf(command + 2, " %15s %15s", input, extra);
            if (fields > 1 || (fields == 1 && !parse_rotation(input, &step))) {
                snprintf(state.status, sizeof(state.status), "ERR: use PH or PH n, n=0..12"); continue;
            }
            export_current(&state, 0, step, state.status, sizeof(state.status)); continue;
        }
        if (command[0] == 'p' && (command[1] == '\0' || isspace((unsigned char)command[1]))) {
            unsigned step = 0; char extra[16] = {0};
            int fields = sscanf(command + 1, " %15s %15s", input, extra);
            if (fields > 1 || (fields == 1 && !parse_rotation(input, &step))) {
                snprintf(state.status, sizeof(state.status), "ERR: use P or P n, n=0..12"); continue;
            }
            export_current(&state, 1, step, state.status, sizeof(state.status)); continue;
        }
        snprintf(state.status, sizeof(state.status), "ERR: use A, A H 2, 0..6, +, -, P, PH, T, T O|T|S, ?, or Q");
    }
    term_restore(&term);
    printf("\nbye\n");
    return 0;
}
