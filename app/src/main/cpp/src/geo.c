#include "geo.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double geo_distance(const double *a, const double *b)
{
    double dx = b[0] - a[0];
    double dy = b[1] - a[1];
    return sqrt(dx * dx + dy * dy);
}

double geo_polygon_area(const double (*pts)[2], int n)
{
    if (!pts || n < 3) return 0.0;
    double twice = 0.0;
    for (int i = 0; i < n; i++) {
        const double *p = pts[i];
        const double *q = pts[(i + 1) % n];
        twice += p[0] * q[1] - q[0] * p[1];
    }
    return fabs(twice) * 0.5;
}

double geo_angle_deg(const double *v, const double *a, const double *b)
{
    double ax = a[0] - v[0];
    double ay = a[1] - v[1];
    double bx = b[0] - v[0];
    double by = b[1] - v[1];
    double la = sqrt(ax * ax + ay * ay);
    double lb = sqrt(bx * bx + by * by);
    if (la < 1e-12 || lb < 1e-12) return 0.0;
    double c = (ax * bx + ay * by) / (la * lb);
    if (c < -1.0) c = -1.0;
    if (c > 1.0) c = 1.0;
    return acos(c) * 180.0 / M_PI;
}

void geo_reflect_point(const double *p, const double *l0, const double *l1,
                       double out[2])
{
    double dx = l1[0] - l0[0];
    double dy = l1[1] - l0[1];
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-18) {
        out[0] = p[0];
        out[1] = p[1];
        return;
    }
    /* p 在直线上的投影参数 t */
    double t = ((p[0] - l0[0]) * dx + (p[1] - l0[1]) * dy) / len2;
    double fx = l0[0] + t * dx;
    double fy = l0[1] + t * dy;
    out[0] = 2.0 * fx - p[0];
    out[1] = 2.0 * fy - p[1];
}

void geo_unit_dir(const double *a, const double *b, double out[2])
{
    double dx = b[0] - a[0];
    double dy = b[1] - a[1];
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-18) {
        out[0] = 1.0;
        out[1] = 0.0;
        return;
    }
    out[0] = dx / len;
    out[1] = dy / len;
}

void geo_perp(const double *d, double out[2])
{
    out[0] = -d[1];
    out[1] = d[0];
}
