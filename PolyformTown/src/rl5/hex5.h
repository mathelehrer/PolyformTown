#ifndef HEX5_H
#define HEX5_H

#include <stddef.h>

#define HEX5_SIDES 6
#define HEX5_TOK 64
#define HEX5_MAX_TILES 512
#define HEX5_MAX_ORIENTED 4096
#define HEX5_MAX_RULES 4096
#define HEX5_MAX_ATTACH 4096
#define HEX5_MAX_VFIGS 65536
#define HEX5_MAX_BCOMP 262144
#define HEX5_MAX_INNER 262144

typedef struct {
    char e[HEX5_SIDES][HEX5_TOK];
} Hex5;

typedef struct {
    Hex5 tiles[HEX5_MAX_TILES];
    int ntiles;
    Hex5 oriented[HEX5_MAX_ORIENTED];
    int noriented;
    char rule_a[HEX5_MAX_RULES][HEX5_TOK];
    char rule_b[HEX5_MAX_RULES][HEX5_TOK];
    int nrules;
    int nsource_rules;
} Hex5Model;

typedef struct {
    int from;
    int to;
} Attach5Edge;

typedef struct {
    Attach5Edge edges[HEX5_MAX_ATTACH];
    int nedges;
    int noriented;
} Attach5Dict;

typedef struct {
    int h[3];
} VFig5;

typedef struct {
    VFig5 figs[HEX5_MAX_VFIGS];
    int nfigs;
    long one_entries;
    long pair_entries;
} VComp5Dict;

typedef struct {
    int center;
    int r[HEX5_SIDES];
} Ring5;

typedef struct {
    Ring5 rings[HEX5_MAX_BCOMP];
    int nrings;
    long starts;
    long closure_tests;
    long closure_hits;
} BComp5Result;

typedef struct {
    char a[HEX5_SIDES][HEX5_TOK];
    char b[HEX5_SIDES][HEX5_TOK];
} Inner5;

typedef struct {
    Inner5 rows[HEX5_MAX_INNER];
    int nrows;
} Inner5Set;

void hex5_rot_left(const Hex5 *in, int k, Hex5 *out);
void hex5_rot_right(const Hex5 *in, int k, Hex5 *out);
void hex5_canonical(const Hex5 *in, Hex5 *out);
int hex5_cmp(const Hex5 *a, const Hex5 *b);
int hex5_equal(const Hex5 *a, const Hex5 *b);
int hex5_parse_file(const char *path, Hex5Model *m);
int hex5_model_finish(Hex5Model *m);

int attach5_build(const Hex5Model *m, Attach5Dict *a);
int attach5_has(const Attach5Dict *a, int from, int to);
int attach5_lookup(const Attach5Dict *a, int from, int *out, int cap);

int vcomp5_build(const Attach5Dict *a, VComp5Dict *v);
int vcomp5_lookup_one(const VComp5Dict *v, int h, int (*out)[2], int cap);
int vcomp5_lookup_pair(const VComp5Dict *v, int h0, int h1, int *out, int cap);

int bcomp5_build(const Hex5Model *m, const VComp5Dict *v, BComp5Result *b);
int bcomp5_inner_generated(const Hex5Model *m, const BComp5Result *b, Inner5Set *s);
int hex5_inner_extracted(const Hex5Model *m, Inner5Set *s);

#endif
