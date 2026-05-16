#ifndef POLYFORM_TERM_COLOR_H
#define POLYFORM_TERM_COLOR_H

#include <stdio.h>

int term_color_enabled(FILE *fp);
const char *term_color_blue(FILE *fp);
const char *term_color_green(FILE *fp);
const char *term_color_red(FILE *fp);
const char *term_color_reset(FILE *fp);
const char *term_color_word(FILE *fp, const char *word);
const char *term_color_reset_for_word(FILE *fp, const char *word);

#endif
