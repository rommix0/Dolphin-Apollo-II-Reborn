#include <stdlib.h>
#include <string.h>
#include "machine/coverage.h"

static const char *const k_space_names[COV_SPACE_COUNT] = { "rom", "low", "ramcode", "ram" };

void coverage_init(coverage *c) { memset(c, 0, sizeof *c); }

void coverage_free(coverage *c)
{
    free(c->edges);
    c->edges = NULL;
    c->edge_count = c->edge_cap = 0;
}

void coverage_mark(coverage *c, cov_space s, uint16_t pc)
{
    c->exec[s][pc >> 3] |= (uint8_t)(1u << (pc & 7));
}

bool coverage_hit(const coverage *c, cov_space s, uint16_t pc)
{
    return ((c->exec[s][pc >> 3] >> (pc & 7)) & 1) != 0;
}

size_t coverage_count(const coverage *c, cov_space s)
{
    size_t n = 0;
    for (unsigned a = 0; a < 0x10000; a++)
        n += coverage_hit(c, s, (uint16_t)a);
    return n;
}

void coverage_add_edge(coverage *c, cov_space from_space, uint16_t from, cov_space to_space, uint16_t to)
{
    for (size_t i = 0; i < c->edge_count; i++) {
        const cov_edge *e = &c->edges[i];
        if (e->from_space == from_space && e->from == from && e->to_space == to_space && e->to == to)
            return;
    }
    if (c->edge_count == c->edge_cap) {
        size_t cap = c->edge_cap ? 2 * c->edge_cap : 64;
        cov_edge *p = realloc(c->edges, cap * sizeof *p);
        if (!p) {
            c->oom = true;
            return;
        }
        c->edges = p;
        c->edge_cap = cap;
    }
    c->edges[c->edge_count++] = (cov_edge){ (uint8_t)from_space, (uint8_t)to_space, from, to };
}

const char *coverage_space_name(cov_space s)
{
    return (unsigned)s < COV_SPACE_COUNT ? k_space_names[s] : "?";
}

static int cmp_edge(const void *pa, const void *pb)
{
    const cov_edge *a = pa, *b = pb;
    if (a->from_space != b->from_space)
        return a->from_space - b->from_space;
    if (a->from != b->from)
        return a->from < b->from ? -1 : 1;
    if (a->to_space != b->to_space)
        return a->to_space - b->to_space;
    return a->to < b->to ? -1 : a->to > b->to;
}

bool coverage_write(const coverage *c, FILE *out)
{
    fprintf(out, "# apollo2 coverage v1: x SPACE ADDR = executed instruction; "
                 "e SPACE FROM SPACE TO = indirect JMP/JSR target\n");
    for (int s = 0; s < COV_SPACE_COUNT; s++)
        for (unsigned a = 0; a < 0x10000; a++)
            if (coverage_hit(c, (cov_space)s, (uint16_t)a))
                fprintf(out, "x %s %04X\n", k_space_names[s], a);
    cov_edge *sorted = NULL;
    if (c->edge_count) {
        sorted = malloc(c->edge_count * sizeof *sorted);
        if (!sorted)
            return false;
        memcpy(sorted, c->edges, c->edge_count * sizeof *sorted);
        qsort(sorted, c->edge_count, sizeof *sorted, cmp_edge);
    }
    for (size_t i = 0; i < c->edge_count; i++)
        fprintf(out, "e %s %04X %s %04X\n", k_space_names[sorted[i].from_space], sorted[i].from,
                k_space_names[sorted[i].to_space], sorted[i].to);
    free(sorted);
    return !ferror(out) && !c->oom;
}
