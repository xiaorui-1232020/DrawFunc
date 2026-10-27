#include "subst.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "geo.h"
#include "model.h"

static bool is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static const fd_geo *find_point(const char *name)
{
    fd_doc *d = model();
    for (int i = 0; i < d->geo_count; i++) {
        if (d->geos[i].kind == FD_GEO_POINT &&
            strcmp(d->geos[i].name, name) == 0) {
            return &d->geos[i];
        }
    }
    return NULL;
}

static bool is_measure(fd_geo_kind k)
{
    return k == FD_GEO_DISTANCE || k == FD_GEO_ANGLE || k == FD_GEO_POLYGON;
}

static double measure_value(const fd_geo *g)
{
    switch (g->kind) {
        case FD_GEO_DISTANCE:
            return geo_distance(g->p[0], g->p[1]);
        case FD_GEO_ANGLE:
            return geo_angle_deg(g->p[0], g->p[1], g->p[2]);
        case FD_GEO_POLYGON:
            return fabs(geo_polygon_area((const double(*)[2])g->p, g->npts));
        default:
            return 0.0;
    }
}

static bool find_measure(const char *name, double *out)
{
    fd_doc *d = model();
    for (int i = 0; i < d->geo_count; i++) {
        if (!is_measure(d->geos[i].kind)) continue;
        if (strcmp(d->geos[i].name, name) != 0) continue;
        *out = measure_value(&d->geos[i]);
        return true;
    }
    return false;
}

/** 解析 `( A )`；成功返回右括号后的位置，失败返回 NULL。 */
static const char *parse_point_ref(const char *p, char *name, size_t cap)
{
    while (isspace((unsigned char)*p)) p++;
    if (*p != '(') return NULL;
    p++;
    while (isspace((unsigned char)*p)) p++;

    size_t n = 0;
    while (isupper((unsigned char)*p) && n + 1 < cap) name[n++] = *p++;
    name[n] = '\0';
    if (n == 0) return NULL;

    while (isspace((unsigned char)*p)) p++;
    if (*p != ')') return NULL;
    return p + 1;
}

void fd_subst_geo(const char *src, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (!src) {
        out[0] = '\0';
        return;
    }

    size_t o = 0;
    const char *p = src;
    while (*p && o + 1 < cap) {
        if (is_ident_start(*p)) {
            const char *start = p;
            while (is_ident_char(*p)) p++;

            char name[16];
            size_t len = (size_t)(p - start);
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, start, len);
            name[len] = '\0';

            /* x(A) / y(A) */
            if (len == 1 && (name[0] == 'x' || name[0] == 'y')) {
                char pt[16];
                const char *after = parse_point_ref(p, pt, sizeof(pt));
                if (after) {
                    const fd_geo *g = find_point(pt);
                    if (g) {
                        int w = snprintf(out + o, cap - o, "(%.12g)",
                                         g->p[0][name[0] == 'y' ? 1 : 0]);
                        if (w > 0) o += (size_t)w;
                        p = after;
                        continue;
                    }
                }
            }

            /* 距离 / 角 / 面积对象的名字 */
            double v;
            if (find_measure(name, &v)) {
                int w = snprintf(out + o, cap - o, "(%.12g)", v);
                if (w > 0) o += (size_t)w;
                continue;
            }

            if (o + len < cap) {
                memcpy(out + o, start, len);
                o += len;
            }
            continue;
        }

        out[o++] = *p++;
    }
    out[o] = '\0';
}
