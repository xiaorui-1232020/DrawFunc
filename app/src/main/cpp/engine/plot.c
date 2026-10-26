#include "plot.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FD_MAX_CURVES 20000
#define FD_MAX_FLOATS_PER_CURVE (1 << 16)

/* 部分 C 库（如 Android bionic 的 strict 模式）不暴露 M_PI。 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* 折线收集器                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    float *buf;
    int n;
    int cap;
    bool open;
    fd_polyline *lines;
    int count;
    int cap_lines;
} poly_builder;

static void pb_init(poly_builder *b)
{
    memset(b, 0, sizeof(*b));
}

static void pb_free(poly_builder *b)
{
    free(b->buf);
    free(b->lines);
    pb_init(b);
}

static void pb_flush(poly_builder *b)
{
    if (b->n >= 4 && b->count < FD_MAX_CURVES) {
        if (b->count >= b->cap_lines) {
            int next = b->cap_lines ? b->cap_lines * 2 : 16;
            if (next > FD_MAX_CURVES) next = FD_MAX_CURVES;
            fd_polyline *p = (fd_polyline *)realloc(b->lines,
                                                    (size_t)next * sizeof(fd_polyline));
            if (!p) {
                b->n = 0;
                return;
            }
            b->lines = p;
            b->cap_lines = next;
        }
        float *copy = (float *)malloc((size_t)b->n * sizeof(float));
        if (!copy) {
            b->n = 0;
            return;
        }
        memcpy(copy, b->buf, (size_t)b->n * sizeof(float));
        b->lines[b->count].xy = copy;
        b->lines[b->count].count = b->n / 2;
        b->lines[b->count].function_id = 0;
        b->lines[b->count].color_index = 0;
        b->count++;
    }
    b->n = 0;
}

static void pb_add(poly_builder *b, float x, float y)
{
    if (b->count >= FD_MAX_CURVES) return;
    if (!b->open) {
        pb_flush(b);
        b->open = true;
    }
    if (b->n + 2 > b->cap) {
        if (b->cap >= FD_MAX_FLOATS_PER_CURVE) {
            pb_flush(b);
            b->open = true;
        } else {
            int next = b->cap ? b->cap * 2 : 256;
            if (next > FD_MAX_FLOATS_PER_CURVE) next = FD_MAX_FLOATS_PER_CURVE;
            float *p = (float *)realloc(b->buf, (size_t)next * sizeof(float));
            if (!p) return;
            b->buf = p;
            b->cap = next;
        }
    }
    if (b->n + 2 <= b->cap) {
        b->buf[b->n++] = x;
        b->buf[b->n++] = y;
    }
}

static void pb_end_line(poly_builder *b)
{
    if (b->open) {
        pb_flush(b);
        b->open = false;
    }
}

/* ------------------------------------------------------------------ */
/* 结果与错误                                                          */
/* ------------------------------------------------------------------ */

void fd_plot_result_free(fd_plot_result *r)
{
    if (!r) return;
    for (int i = 0; i < r->line_count; i++) free(r->lines[i].xy);
    free(r->lines);
    free(r->error_ids);
    free(r->error_msgs);
    memset(r, 0, sizeof(*r));
}

static void add_error(fd_plot_result *out, int capacity, int fn_id,
                      const char *msg, int position, int src_len)
{
    if (out->error_count >= capacity) return;
    out->error_ids[out->error_count] = fn_id;
    char *dst = out->error_msgs[out->error_count];
    if (position >= 0 && position < src_len) {
        snprintf(dst, FD_EXPR_ERR_CAP, "%s（第 %d 个字符附近）", msg, position + 1);
    } else {
        snprintf(dst, FD_EXPR_ERR_CAP, "%s", msg);
    }
    out->error_count++;
}

static void add_compile_error(fd_plot_result *out, int capacity, int fn_id,
                              const fd_expr_error *err, int src_len)
{
    add_error(out, capacity, fn_id,
              err->message[0] ? err->message : "表达式错误",
              err->position, src_len);
}

/* ------------------------------------------------------------------ */
/* 输入预处理                                                          */
/* ------------------------------------------------------------------ */

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

/** 剥 `r =` / `ρ =` 前缀。 */
static const char *strip_polar_prefix(const char *src, char *out, size_t cap)
{
    const char *s = skip_ws(src);
    if (*s == 'r' || *s == 'R') {
        const char *t = skip_ws(s + 1);
        if (*t == '=') s = skip_ws(t + 1);
    } else if ((unsigned char)s[0] == 0xCF && (unsigned char)s[1] == 0x81) {
        /* ρ (U+03C1) = CF 81 */
        const char *t = skip_ws(s + 2);
        if (*t == '=') s = skip_ws(t + 1);
    }
    snprintf(out, cap, "%s", s);
    size_t n = strlen(out);
    while (n > 0 && isspace((unsigned char)out[n - 1])) out[--n] = '\0';
    return out;
}

/** 剥 `x =` / `x(t) =` 前缀。 */
static const char *strip_param_prefix(const char *src, char axis, char *out, size_t cap)
{
    const char *s = skip_ws(src);
    if (tolower((unsigned char)*s) == tolower((unsigned char)axis)) {
        const char *t = skip_ws(s + 1);
        if (*t == '(') {
            t = skip_ws(t + 1);
            if (tolower((unsigned char)*t) == 't') {
                t = skip_ws(t + 1);
                if (*t == ')') t = skip_ws(t + 1);
            }
        }
        if (*t == '=') s = skip_ws(t + 1);
    }
    snprintf(out, cap, "%s", s);
    size_t n = strlen(out);
    while (n > 0 && isspace((unsigned char)out[n - 1])) out[--n] = '\0';
    return out;
}

static fd_expr *compile_src(const char *src, const char *const *vars, int nvars,
                            bool degrees, const double params[4],
                            fd_expr_error *err)
{
    static const char *names[4] = {"a", "b", "c", "d"};
    fd_param ps[4];
    for (int i = 0; i < 4; i++) {
        ps[i].name = names[i];
        ps[i].value = params[i];
    }
    return fd_expr_compile(src, vars, nvars, degrees, ps, 4, err);
}

static fd_expr *compile_equation_src(const char *src, const char *const *vars,
                                     int nvars, bool degrees, const double params[4],
                                     fd_expr_error *err)
{
    static const char *names[4] = {"a", "b", "c", "d"};
    fd_param ps[4];
    for (int i = 0; i < 4; i++) {
        ps[i].name = names[i];
        ps[i].value = params[i];
    }
    return fd_expr_compile_equation(src, vars, nvars, degrees, ps, 4, err);
}

/* ------------------------------------------------------------------ */
/* 1) 显函数                                                           */
/* ------------------------------------------------------------------ */

static void sample_explicit(const fd_function *fn, const fd_viewport *vp,
                            bool degrees, const double params[4], double step_px,
                            fd_polyline **out_lines, int *out_count, int *out_cap,
                            fd_plot_result *out, int err_cap)
{
    char body[FD_SRC_CAP];
    fd_expr_strip_explicit_prefix(fn->src1, body, sizeof(body));
    if (body[0] == '\0') {
        add_error(out, err_cap, fn->id, "还没写表达式", 0, (int)strlen(fn->src1));
        return;
    }
    if (fd_expr_top_level_equals(body) >= 0) {
        add_error(out, err_cap, fn->id,
                  "y=f(x) 模式里不能有等号，要画隐函数请切到 F(x,y) 模式",
                  0, (int)strlen(fn->src1));
        return;
    }
    const char *vars[1] = {"x"};
    fd_expr_error err;
    fd_expr *f = compile_src(body, vars, 1, degrees, params, &err);
    if (!f) {
        add_compile_error(out, err_cap, fn->id, &err, (int)strlen(fn->src1));
        return;
    }

    const double h = vp->height_px;
    const double lo = -4.0 * h;
    const double hi = 5.0 * h;
    const double break_jump = 2.0 * h;

    poly_builder b;
    pb_init(&b);
    double arg = 0.0;
    double prev_y = NAN;
    double px = 0.0;
    while (px <= (double)vp->width_px) {
        arg = fd_vp_to_world_x(vp, px);
        const double y = fd_expr_eval(f, &arg);
        if (isfinite(y)) {
            double sy = fd_vp_to_screen_y(vp, y);
            if (sy < lo) sy = lo;
            if (sy > hi) sy = hi;
            if (isfinite(prev_y) && fabs(sy - prev_y) > break_jump) pb_end_line(&b);
            pb_add(&b, (float)px, (float)sy);
            prev_y = sy;
        } else {
            pb_end_line(&b);
            prev_y = NAN;
        }
        px += step_px;
    }
    pb_end_line(&b);
    fd_expr_free(f);
    *out_lines = b.lines;
    *out_count = b.count;
    *out_cap = b.cap_lines;
    free(b.buf);
}

/* ------------------------------------------------------------------ */
/* 自适应曲线（极坐标 / 参数方程）                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    double x, y;
    bool valid;
} sp_point;

typedef struct {
    void *ctx;
    void (*at)(void *ctx, double t, sp_point *p);
} point_fn;

static double deviation(const sp_point *p0, const sp_point *pm, const sp_point *p1)
{
    if (!p0->valid || !p1->valid) return 1e300;
    const double dx = p1->x - p0->x;
    const double dy = p1->y - p0->y;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) return hypot(pm->x - p0->x, pm->y - p0->y);
    double t = ((pm->x - p0->x) * dx + (pm->y - p0->y) * dy) / len2;
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    const double cx = p0->x + t * dx;
    const double cy = p0->y + t * dy;
    return hypot(pm->x - cx, pm->y - cy);
}

static void emit_point(const sp_point *p, poly_builder *b)
{
    if (p->valid) pb_add(b, (float)p->x, (float)p->y);
    else pb_end_line(b);
}

static void refine(const point_fn *f, double t0, const sp_point *p0,
                   double t1, const sp_point *p1,
                   int depth, double eps, int max_depth, poly_builder *b)
{
    const double tm = 0.5 * (t0 + t1);
    if (!(tm > t0 && tm < t1)) {
        emit_point(p1, b);
        return;
    }
    sp_point pm;
    f->at(f->ctx, tm, &pm);
    if (!pm.valid || depth >= max_depth || deviation(p0, &pm, p1) <= eps) {
        if (!pm.valid) pb_end_line(b);
        emit_point(p1, b);
    } else {
        refine(f, t0, p0, tm, &pm, depth + 1, eps, max_depth, b);
        refine(f, tm, &pm, t1, p1, depth + 1, eps, max_depth, b);
    }
}

static void adaptive_curve(const point_fn *f, double t_min, double t_max,
                           int max_depth, double eps, poly_builder *b)
{
    if (!isfinite(t_min) || !isfinite(t_max) || t_max <= t_min) return;
    const double span = t_max - t_min;
    int n0 = (int)(span * 24.0);
    if (n0 < 96) n0 = 96;
    if (n0 > 2048) n0 = 2048;

    sp_point prev, cur;
    f->at(f->ctx, t_min, &prev);
    emit_point(&prev, b);

    double t_prev = t_min;
    for (int i = 1; i <= n0; i++) {
        const double t = t_min + span * (double)i / (double)n0;
        f->at(f->ctx, t, &cur);
        refine(f, t_prev, &prev, t, &cur, 0, eps, max_depth, b);
        t_prev = t;
        prev = cur;
    }
}

typedef struct {
    fd_expr *f;
    fd_viewport vp;
    double limit;
    bool degrees;
} polar_ctx;

static void polar_at(void *ctx_, double t, sp_point *p)
{
    polar_ctx *ctx = (polar_ctx *)ctx_;
    double args[2] = {t, t};
    const double r = fd_expr_eval(ctx->f, args);
    if (!isfinite(r)) {
        p->valid = false;
        return;
    }
    const double theta = ctx->degrees ? t * (M_PI / 180.0) : t;
    const double wx = r * cos(theta);
    const double wy = r * sin(theta);
    const double sx = fd_vp_to_screen_x(&ctx->vp, wx);
    const double sy = fd_vp_to_screen_y(&ctx->vp, wy);
    if (fabs(sx) > ctx->limit || fabs(sy) > ctx->limit) {
        p->valid = false;
    } else {
        p->x = sx;
        p->y = sy;
        p->valid = true;
    }
}

typedef struct {
    fd_expr *fx;
    fd_expr *fy;
    fd_viewport vp;
    double limit;
} param_ctx;

static void param_at(void *ctx_, double t, sp_point *p)
{
    param_ctx *ctx = (param_ctx *)ctx_;
    double arg = t;
    const double wx = fd_expr_eval(ctx->fx, &arg);
    const double wy = fd_expr_eval(ctx->fy, &arg);
    if (!isfinite(wx) || !isfinite(wy)) {
        p->valid = false;
        return;
    }
    const double sx = fd_vp_to_screen_x(&ctx->vp, wx);
    const double sy = fd_vp_to_screen_y(&ctx->vp, wy);
    if (fabs(sx) > ctx->limit || fabs(sy) > ctx->limit) {
        p->valid = false;
    } else {
        p->x = sx;
        p->y = sy;
        p->valid = true;
    }
}

typedef struct {
    fd_polyline **lines;
    int *count;
    int *cap;
} out_sink;

static void sink_transfer(poly_builder *b, out_sink *sink)
{
    /* 已有线段追加到结果数组。 */
    if (b->count > 0) {
        int need = *sink->count + b->count;
        if (need > *sink->cap) {
            int next = *sink->cap ? *sink->cap : 16;
            while (next < need) next *= 2;
            fd_polyline *p = (fd_polyline *)realloc(*sink->lines,
                                                    (size_t)next * sizeof(fd_polyline));
            if (!p) {
                pb_free(b);
                return;
            }
            *sink->lines = p;
            *sink->cap = next;
        }
        memcpy(*sink->lines + *sink->count, b->lines,
               (size_t)b->count * sizeof(fd_polyline));
        *sink->count = need;
    }
    free(b->lines);
    free(b->buf);
    pb_init(b);
}

static void sample_polar(const fd_function *fn, const fd_viewport *vp,
                         bool degrees, const double params[4],
                         int max_depth, double eps,
                         out_sink *sink, fd_plot_result *out, int err_cap)
{
    char body[FD_SRC_CAP];
    strip_polar_prefix(fn->src1, body, sizeof(body));
    if (body[0] == '\0') {
        add_error(out, err_cap, fn->id, "还没写表达式", 0, (int)strlen(fn->src1));
        return;
    }
    const char *vars[2] = {"θ", "t"};
    fd_expr_error err;
    fd_expr *f = compile_src(body, vars, 2, degrees, params, &err);
    if (!f) {
        add_compile_error(out, err_cap, fn->id, &err, (int)strlen(fn->src1));
        return;
    }

    polar_ctx ctx = {f, *vp, 64.0 * fmax(vp->width_px, vp->height_px), degrees};
    point_fn pf = {&ctx, polar_at};
    poly_builder b;
    pb_init(&b);
    adaptive_curve(&pf, fn->t_min, fn->t_max, max_depth, eps, &b);
    pb_end_line(&b);
    sink_transfer(&b, sink);
    fd_expr_free(f);
}

static void sample_parametric(const fd_function *fn, const fd_viewport *vp,
                              bool degrees, const double params[4],
                              int max_depth, double eps,
                              out_sink *sink, fd_plot_result *out, int err_cap)
{
    char xs[FD_SRC_CAP], ys[FD_SRC_CAP];
    strip_param_prefix(fn->src1, 'x', xs, sizeof(xs));
    strip_param_prefix(fn->src2, 'y', ys, sizeof(ys));
    if (xs[0] == '\0') {
        add_error(out, err_cap, fn->id, "缺少 x(t)", 0, (int)strlen(fn->src1));
        return;
    }
    if (ys[0] == '\0') {
        add_error(out, err_cap, fn->id, "缺少 y(t)", 0, (int)strlen(fn->src2));
        return;
    }
    const char *vars[1] = {"t"};
    fd_expr_error err;
    fd_expr *fx = compile_src(xs, vars, 1, degrees, params, &err);
    if (!fx) {
        add_compile_error(out, err_cap, fn->id, &err, (int)strlen(fn->src1));
        return;
    }
    fd_expr *fy = compile_src(ys, vars, 1, degrees, params, &err);
    if (!fy) {
        fd_expr_free(fx);
        add_compile_error(out, err_cap, fn->id, &err, (int)strlen(fn->src2));
        return;
    }

    param_ctx ctx = {fx, fy, *vp, 64.0 * fmax(vp->width_px, vp->height_px)};
    point_fn pf = {&ctx, param_at};
    poly_builder b;
    pb_init(&b);
    adaptive_curve(&pf, fn->t_min, fn->t_max, max_depth, eps, &b);
    pb_end_line(&b);
    sink_transfer(&b, sink);
    fd_expr_free(fx);
    fd_expr_free(fy);
}

/* ------------------------------------------------------------------ */
/* 4) 隐函数：marching squares                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    fd_expr *f;
    fd_viewport vp;
    double args[2];
} implicit_field;

static double field_at(implicit_field *field, double px, double py)
{
    field->args[0] = fd_vp_to_world_x(&field->vp, px);
    field->args[1] = fd_vp_to_world_y(&field->vp, py);
    return fd_expr_eval(field->f, field->args);
}

static double frac(double a, double b)
{
    const double d = a - b;
    if (d == 0.0 || !isfinite(d)) return 0.5;
    const double t = a / d;
    if (!isfinite(t)) return 0.5;
    if (t < 0.0) return 0.0;
    if (t > 1.0) return 1.0;
    return t;
}

static void march(implicit_field *field,
                  double x0, double y0, double x1, double y1,
                  double v00, double v10, double v11, double v01,
                  int depth, int max_depth, poly_builder *b)
{
    if (!isfinite(v00) || !isfinite(v10) || !isfinite(v11) || !isfinite(v01)) return;

    const int cs = (v00 > 0.0 ? 1 : 0) | (v10 > 0.0 ? 2 : 0) |
                   (v11 > 0.0 ? 4 : 0) | (v01 > 0.0 ? 8 : 0);
    if (cs == 0 || cs == 15) return;

    if (depth < max_depth) {
        const double xm = 0.5 * (x0 + x1);
        const double ym = 0.5 * (y0 + y1);
        const double vM = field_at(field, xm, ym);
        const double vB = field_at(field, xm, y0);
        const double vR = field_at(field, x1, ym);
        const double vT = field_at(field, xm, y1);
        const double vL = field_at(field, x0, ym);
        march(field, x0, y0, xm, ym, v00, vB, vM, vL, depth + 1, max_depth, b);
        march(field, xm, y0, x1, ym, vB, v10, vR, vM, depth + 1, max_depth, b);
        march(field, xm, ym, x1, y1, vM, vR, v11, vT, depth + 1, max_depth, b);
        march(field, x0, ym, xm, y1, vL, vM, vT, v01, depth + 1, max_depth, b);
        return;
    }

    const double t0 = frac(v00, v10);
    const double t1 = frac(v10, v11);
    const double t2 = frac(v11, v01);
    const double t3 = frac(v01, v00);
    const double e0x = x0 + (x1 - x0) * t0, e0y = y0;
    const double e1x = x1, e1y = y0 + (y1 - y0) * t1;
    const double e2x = x1 + (x0 - x1) * t2, e2y = y1;
    const double e3x = x0, e3y = y1 + (y0 - y1) * t3;

#define SEG(ax, ay, bx, by)                 \
    do {                                    \
        pb_add(b, (float)(ax), (float)(ay)); \
        pb_add(b, (float)(bx), (float)(by)); \
        pb_end_line(b);                      \
    } while (0)

    switch (cs) {
        case 1: case 14: SEG(e3x, e3y, e0x, e0y); break;
        case 2: case 13: SEG(e0x, e0y, e1x, e1y); break;
        case 3: case 12: SEG(e1x, e1y, e3x, e3y); break;
        case 4: case 11: SEG(e1x, e1y, e2x, e2y); break;
        case 6: case 9:  SEG(e0x, e0y, e2x, e2y); break;
        case 7: case 8:  SEG(e2x, e2y, e3x, e3y); break;
        case 5: {
            const double center = (v00 + v10 + v11 + v01) / 4.0;
            if (center > 0.0) {
                SEG(e0x, e0y, e1x, e1y);
                SEG(e2x, e2y, e3x, e3y);
            } else {
                SEG(e0x, e0y, e3x, e3y);
                SEG(e1x, e1y, e2x, e2y);
            }
            break;
        }
        case 10: {
            const double center = (v00 + v10 + v11 + v01) / 4.0;
            if (center > 0.0) {
                SEG(e0x, e0y, e3x, e3y);
                SEG(e1x, e1y, e2x, e2y);
            } else {
                SEG(e0x, e0y, e1x, e1y);
                SEG(e2x, e2y, e3x, e3y);
            }
            break;
        }
        default: break;
    }
#undef SEG
}

static void sample_implicit(const fd_function *fn, const fd_viewport *vp,
                            bool degrees, const double params[4],
                            double cell_px, int implicit_depth,
                            out_sink *sink, fd_plot_result *out, int err_cap)
{
    if (fn->src1[0] == '\0') {
        add_error(out, err_cap, fn->id, "还没写表达式", 0, 0);
        return;
    }
    const char *vars[2] = {"x", "y"};
    fd_expr_error err;
    /* compile_equation 在没有等号时等价于 compile，顺便支持 x^2+y^2 = 1。 */
    fd_expr *f = compile_equation_src(fn->src1, vars, 2, degrees, params, &err);
    if (!f) {
        add_compile_error(out, err_cap, fn->id, &err, (int)strlen(fn->src1));
        return;
    }

    implicit_field field;
    field.f = f;
    field.vp = *vp;

    int nx = (int)ceil(vp->width_px / cell_px);
    int ny = (int)ceil(vp->height_px / cell_px);
    if (nx < 4) nx = 4;
    if (nx > 400) nx = 400;
    if (ny < 4) ny = 4;
    if (ny > 400) ny = 400;
    const double sx = (double)vp->width_px / nx;
    const double sy = (double)vp->height_px / ny;

    poly_builder b;
    pb_init(&b);
    for (int j = 0; j < ny; j++) {
        const double y0 = j * sy;
        const double y1 = y0 + sy;
        for (int i = 0; i < nx; i++) {
            const double x0 = i * sx;
            const double x1 = x0 + sx;
            march(&field, x0, y0, x1, y1,
                  field_at(&field, x0, y0), field_at(&field, x1, y0),
                  field_at(&field, x1, y1), field_at(&field, x0, y1),
                  0, implicit_depth, &b);
        }
    }
    pb_end_line(&b);
    sink_transfer(&b, sink);
    fd_expr_free(f);
}

/* ------------------------------------------------------------------ */
/* 主入口                                                              */
/* ------------------------------------------------------------------ */

bool fd_plot(const fd_function *fns, int fn_count,
             const fd_viewport *vp, fd_quality q,
             bool default_degrees, const double params[4],
             fd_plot_result *out)
{
    memset(out, 0, sizeof(*out));
    out->viewport = *vp;
    if (!fns || fn_count <= 0 || vp->width_px <= 1.0f || vp->height_px <= 1.0f) {
        return true;
    }

    int err_cap = fn_count > 0 ? fn_count : 1;
    out->error_ids = (int *)malloc((size_t)err_cap * sizeof(int));
    out->error_msgs = (char(*)[FD_EXPR_ERR_CAP])malloc((size_t)err_cap * FD_EXPR_ERR_CAP);
    if (!out->error_ids || !out->error_msgs) {
        free(out->error_ids);
        free(out->error_msgs);
        out->error_ids = NULL;
        out->error_msgs = NULL;
        return false;
    }

    double cell_px = 5.0;
    int implicit_depth = 1;
    double explicit_step = 1.0;
    double flatness = 0.25;
    int adaptive_depth = 8;
    switch (q) {
        case FD_QUALITY_FAST:
            cell_px = 9.0; implicit_depth = 0; explicit_step = 2.0;
            flatness = 0.7; adaptive_depth = 5; break;
        case FD_QUALITY_HIGH:
            cell_px = 3.0; implicit_depth = 2; explicit_step = 0.5;
            flatness = 0.12; adaptive_depth = 10; break;
        case FD_QUALITY_NORMAL:
        default:
            break;
    }

    fd_polyline *lines = NULL;
    int line_count = 0;
    int line_cap = 0;
    out_sink sink = {&lines, &line_count, &line_cap};

    for (int k = 0; k < fn_count; k++) {
        const fd_function *fn = &fns[k];
        if (!fn->visible) continue;
        const bool deg = fn->degrees < 0 ? default_degrees : fn->degrees != 0;

        switch (fn->mode) {
            case FD_PLOT_EXPLICIT: {
                fd_polyline *tmp = NULL;
                int n = 0, cap = 0;
                sample_explicit(fn, vp, deg, params, explicit_step, &tmp, &n, &cap,
                                out, err_cap);
                for (int i = 0; i < n; i++) {
                    tmp[i].function_id = fn->id;
                    tmp[i].color_index = (uint8_t)fn->color_index;
                }
                if (n > 0) {
                    if (line_count + n > line_cap) {
                        int next = line_cap ? line_cap : 16;
                        while (next < line_count + n) next *= 2;
                        fd_polyline *p = (fd_polyline *)realloc(lines,
                                                                (size_t)next * sizeof(fd_polyline));
                        if (p) {
                            lines = p;
                            line_cap = next;
                        } else {
                            n = 0;
                        }
                    }
                    if (n > 0) {
                        memcpy(lines + line_count, tmp, (size_t)n * sizeof(fd_polyline));
                        line_count += n;
                    }
                }
                free(tmp);
                break;
            }
            case FD_PLOT_POLAR: {
                const int start = line_count;
                sample_polar(fn, vp, deg, params, adaptive_depth, flatness,
                             &sink, out, err_cap);
                for (int i = start; i < line_count; i++) {
                    lines[i].function_id = fn->id;
                    lines[i].color_index = (uint8_t)fn->color_index;
                }
                break;
            }
            case FD_PLOT_PARAMETRIC: {
                const int start = line_count;
                sample_parametric(fn, vp, deg, params, adaptive_depth, flatness,
                                  &sink, out, err_cap);
                for (int i = start; i < line_count; i++) {
                    lines[i].function_id = fn->id;
                    lines[i].color_index = (uint8_t)fn->color_index;
                }
                break;
            }
            case FD_PLOT_IMPLICIT: {
                const int start = line_count;
                sample_implicit(fn, vp, deg, params, cell_px, implicit_depth,
                                &sink, out, err_cap);
                for (int i = start; i < line_count; i++) {
                    lines[i].function_id = fn->id;
                    lines[i].color_index = (uint8_t)fn->color_index;
                }
                break;
            }
        }
    }

    out->lines = lines;
    out->line_count = line_count;
    return true;
}

void fd_function_summary(const fd_function *fn, char *buf, size_t cap)
{
    switch (fn->mode) {
        case FD_PLOT_EXPLICIT:
            snprintf(buf, cap, "y = %s", fn->src1);
            break;
        case FD_PLOT_POLAR:
            snprintf(buf, cap, "r = %s", fn->src1);
            break;
        case FD_PLOT_PARAMETRIC:
            snprintf(buf, cap, "(%s, %s)", fn->src1, fn->src2);
            break;
        case FD_PLOT_IMPLICIT:
            snprintf(buf, cap, "%s", fn->src1);
            break;
        default:
            snprintf(buf, cap, "%s", fn->src1);
            break;
    }
}
