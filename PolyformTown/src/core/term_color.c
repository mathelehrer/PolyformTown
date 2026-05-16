#define _POSIX_C_SOURCE 200809L
#include "core/term_color.h"

#include <string.h>
#include <unistd.h>

int term_color_enabled(FILE *fp) {
    if (!fp) return 0;
    return isatty(fileno(fp));
}

const char *term_color_blue(FILE *fp) {
    return term_color_enabled(fp) ? "\033[34m" : "";
}

const char *term_color_green(FILE *fp) {
    return term_color_enabled(fp) ? "\033[33m" : "";
}

const char *term_color_red(FILE *fp) {
    return term_color_enabled(fp) ? "\033[31m" : "";
}

const char *term_color_reset(FILE *fp) {
    return term_color_enabled(fp) ? "\033[0m" : "";
}

const char *term_color_word(FILE *fp, const char *word) {
    if (!term_color_enabled(fp) || !word) return "";
    if (strcmp(word, "live") == 0 || strcmp(word, "living") == 0) return "\033[34m";
    if (strcmp(word, "dead") == 0) return "\033[31m";
    if (strcmp(word, "==>") == 0) return "\033[33m";
    return "";
}

const char *term_color_reset_for_word(FILE *fp, const char *word) {
    if (!term_color_enabled(fp) || !word) return "";
    if (strcmp(word, "live") == 0 ||
        strcmp(word, "living") == 0 ||
        strcmp(word, "dead") == 0 ||
        strcmp(word, "==>") == 0) {
        return "\033[0m";
    }
    return "";
}
