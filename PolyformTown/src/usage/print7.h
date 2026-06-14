#ifndef POLYFORMTOWN_USAGE_PRINT7_H
#define POLYFORMTOWN_USAGE_PRINT7_H

#include <stddef.h>
#include "rl7/boundary7.h"
#include "rl7/comp7.h"

int print7_write_hex_svg(const State *s, const Model *m, const char *path,
                         char *err, size_t errsz);

#endif
