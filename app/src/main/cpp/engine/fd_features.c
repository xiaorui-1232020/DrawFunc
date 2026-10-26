#include "fd_features.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 采样区间数（采样点 = FD_FEATURES_SAMPLES + 1）。 */
#define FD_FEATURES_SAMPLES 4000
/* 5 点差商步长 = 可见范围的 1e-6。 */
#define FD_FEATURES_DERIV_STEP_DIV 1e6
/* 双精度机器 epsilon。 */
#define FD_FEATURES_EPS 2.220446049250313e-16
/* Brent 迭代上限。 */
#define FD_FEATURES_BRENT_MAX_ITERS 60
/* 相邻样本 |f| 的直接阈值。 */
#define FD_FEATURES_HUGE 1e12
/* 相邻样本 |f| 的比值阈值。 */
#define FD_FEATURES_RATIO_LIMIT 1e6
/* 求根残差相对系数。 */
#define FD_FEATURES_ROOT_ACCEPT_FACTOR 1e-6
/* 极点判据系数。 */
#define FD_FEATURES_DIVERGE_FACTOR 1e6
/* 差商零判据。 */
#define FD_FEATURES_DERIV_ZERO_FACTOR 1e-9
/* 相切零点判据。 */
#define FD_FEATURES_TOUCH_REL 1e-8
/* 相切判据的参考距离（采样步长的倍数）。 */
#define FD_FEATURES_TOUCH_W_STEPS 4.0
/* 去重相对阈值。 */
#define FD_FEATURES_DEDUPE_REL 1e-9
/* 各类候选区间上限。 */
#define FD_FEATURES_MAX_ROOT_BRACKETS 512
#define FD_FEATURES_MAX_EXTREMUM_BRACKETS 512
#define FD_FEATURES_MAX_TANGENT_BRACKETS 128
#define FD_FEATURES_MAX_DERIV_SPAN 64
/* 一次分析的求值总预算。 */
#define FD_FEATURES_MAX_EVALS 200000

/* 内部候选缓冲：最多 512 个区间，各可能产出一个点。 */
#define FD_FEATURES_HITS_CAP 1024

typedef struct {
    const fd_expr *f;
    double arg[1];
    int evals;
} budget_fn;

static double budget_at(budget_fn *b, double x)
{
    if (b->evals >= FD_FEATURES_MAX_EVALS) return NAN;
    b->evals++;
    b->arg[0] = x;
    return fd_expr_eval(b->f, b->arg);
}

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static int sign_of(double v)
{
    if (v > 0.0) return 1;
    if (v < 0.0) return -1;
    return 0;
}

static int cmp_double(const void *pa, const void *pb)
{
    double a = *(const double *)pa;
    double b = *(const double *)pb;
    /* 返回负值表示 a 排在 b 前面（升序）。 */
    return (a > b) - (a < b);
}

static double dmax(double a, double b) { return a > b ? a : b; }
static double dmin(double a, double b) { return a < b ? a : b; }

/** 有限值的 |y| 中位数；没有可用值或中位数为 0 时退回最大值，再不行给 1。 */
static double representative_scale(const double *values, int n)
{
    double *sorted = (double *)malloc((size_t)n * sizeof(double));
    if (!sorted) return 1.0;
    int count = 0;
    double mx = 0.0;
    for (int i = 0; i < n; i++) {
        double v = values[i];
        if (!isfinite(v)) continue;
        double a = fabs(v);
        if (a > mx) mx = a;
        sorted[count++] = a;
    }
    double out = 1.0;
    if (count > 0) {
        qsort(sorted, (size_t)count, sizeof(double), cmp_double);
        double med = sorted[count / 2];
        if (isfinite(med) && med > 0.0) out = med;
        else if (isfinite(mx) && mx > 0.0) out = mx;
    }
    free(sorted);
    return out;
}

/** 把第 i 段及其左右各一段标记为不可信。 */
static void mark_bad(bool *bad, int i, int n)
{
    for (int k = i - 1; k <= i + 1; k++) {
        if (k >= 0 && k < n) bad[k] = true;
    }
}

/** 第 [from, to) 段里有没有不可信的。 */
static bool spans_bad(const bool *bad, int n, int from, int to)
{
    for (int k = from; k < to; k++) {
        if (k < 0 || k >= n) return true;
        if (bad[k]) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Brent 求解器                                                        */
/* ------------------------------------------------------------------ */

typedef double (*fd_fn1)(void *ctx, double x);

/**
 * Brent 求根，要求 g(a) 与 g(b) 严格异号。
 *
 * @param out 成功时写入根
 * @return true 找到根
 */
static bool brent_root(fd_fn1 g, void *ctx, double a_in, double b_in,
                       double fa_in, double fb_in, double *out)
{
    double a = a_in, b = b_in, fa = fa_in, fb = fb_in;
    if (fa == 0.0) {
        *out = a;
        return true;
    }
    if (fb == 0.0) {
        *out = b;
        return true;
    }
    if ((fa < 0.0) == (fb < 0.0)) return false;
    if (fabs(fa) < fabs(fb)) {
        double t = a;
        a = b;
        b = t;
        t = fa;
        fa = fb;
        fb = t;
    }
    double c = a, fc = fa;
    double d = b - a;
    bool mflag = true;

    for (int k = 0; k < FD_FEATURES_BRENT_MAX_ITERS; k++) {
        double tol = 4.0 * FD_FEATURES_EPS * fabs(b) + DBL_MIN;
        if (fabs(b - a) <= tol || fb == 0.0) {
            *out = b;
            return true;
        }

        double s;
        if (fa != fc && fb != fc) {
            s = a * fb * fc / ((fa - fb) * (fa - fc)) +
                b * fa * fc / ((fb - fa) * (fb - fc)) +
                c * fa * fb / ((fc - fa) * (fc - fb));
        } else {
            s = b - fb * (b - a) / (fb - fa);
        }
        if (!isfinite(s)) s = 0.5 * (a + b);

        double quarter = 0.25 * (3.0 * a + b);
        bool inside = (a < b) ? (s > quarter && s < b) : (s < quarter && s > b);
        bool slow = mflag ? (fabs(s - b) >= 0.5 * fabs(b - c))
                          : (fabs(s - b) >= 0.5 * fabs(c - d));
        if (!inside || slow) {
            s = 0.5 * (a + b);
            mflag = true;
        } else {
            mflag = false;
        }
        if (s == a || s == b) {
            *out = b;
            return true;
        }

        double fs = g(ctx, s);
        if (!isfinite(fs)) return false;
        d = c;
        c = b;
        fc = fb;
        if ((fa < 0.0) != (fs < 0.0)) {
            b = s;
            fb = fs;
        } else {
            a = s;
            fa = fs;
        }
        if (fabs(fa) < fabs(fb)) {
            double t = a;
            a = b;
            b = t;
            t = fa;
            fa = fb;
            fb = t;
        }
    }
    *out = b;
    return true;
}

/**
 * Brent 极小化，在 ax/bx/cx 三点给出的区间里找 g 的极小。
 */
static bool brent_min(fd_fn1 g, void *ctx, double ax, double bx, double cx,
                      double *out_x, double *out_f)
{
    const double cgold = 0.3819660112501051;
    double a = dmin(ax, cx);
    double b = dmax(ax, cx);
    double x = bx, w = bx, v = bx;
    double fx = g(ctx, x);
    if (!isfinite(fx)) return false;
    double fw = fx, fv = fx;
    double best_x = x, best_f = fx;
    double d = 0.0, e = 0.0;

    for (int k = 0; k < FD_FEATURES_BRENT_MAX_ITERS; k++) {
        double xm = 0.5 * (a + b);
        double tol1 = 4.0 * FD_FEATURES_EPS * fabs(x) + DBL_MIN;
        double tol2 = 2.0 * tol1;
        if (fabs(x - xm) <= tol2 - 0.5 * (b - a)) break;

        if (fabs(e) > tol1) {
            double r = (x - w) * (fx - fv);
            double q = (x - v) * (fx - fw);
            double p = (x - v) * q - (x - w) * r;
            double qq = 2.0 * (q - r);
            if (qq > 0.0) p = -p;
            qq = fabs(qq);
            double etemp = e;
            e = d;
            if (fabs(p) >= fabs(0.5 * qq * etemp) || p <= qq * (a - x) || p >= qq * (b - x)) {
                e = (x >= xm) ? a - x : b - x;
                d = cgold * e;
            } else {
                d = p / qq;
                double u = x + d;
                if (u - a < tol2 || b - u < tol2) d = (xm > x) ? tol1 : -tol1;
            }
        } else {
            e = (x >= xm) ? a - x : b - x;
            d = cgold * e;
        }

        double u = (fabs(d) >= tol1) ? x + d : x + (d > 0.0 ? tol1 : -tol1);
        if (u == x || u == a || u == b) break;
        double fu = g(ctx, u);
        if (!isfinite(fu)) break;

        if (fu <= fx) {
            if (u >= x) a = x;
            else b = x;
            v = w;
            fv = fw;
            w = x;
            fw = fx;
            x = u;
            fx = fu;
        } else {
            if (u < x) a = u;
            else b = u;
            if (fu <= fw || w == x) {
                v = w;
                fv = fw;
                w = u;
                fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = u;
                fv = fu;
            }
        }
        if (fx < best_f) {
            best_f = fx;
            best_x = x;
        }
    }
    *out_x = best_x;
    *out_f = best_f;
    return true;
}

/* ------------------------------------------------------------------ */
/* 求导与判据                                                          */
/* ------------------------------------------------------------------ */

/** 求值回调上下文，用来把 budget_fn 塞进 Brent 的通用函数指针里。 */
typedef struct {
    budget_fn *fn;
    double h; /* 仅 deriv5 用 */
} wrap_ctx;

static double wrap_f(void *ctx, double x)
{
    return budget_at(((wrap_ctx *)ctx)->fn, x);
}

static double wrap_abs_f(void *ctx, double x)
{
    double v = budget_at(((wrap_ctx *)ctx)->fn, x);
    return isfinite(v) ? fabs(v) : NAN;
}

static double wrap_deriv5(void *ctx, double x)
{
    wrap_ctx *c = (wrap_ctx *)ctx;
    budget_fn *b = c->fn;
    double h = c->h;
    double hp = (x + h) - x;
    double hm = x - (x - h);
    if (hp <= 0.0 || hm <= 0.0 || !isfinite(hp) || !isfinite(hm)) return NAN;
    double f2 = budget_at(b, x + 2.0 * h);
    double f1 = budget_at(b, x + h);
    double fm1 = budget_at(b, x - h);
    double fm2 = budget_at(b, x - 2.0 * h);
    if (!isfinite(f2) || !isfinite(f1) || !isfinite(fm1) || !isfinite(fm2)) return NAN;
    double h_eff = 0.5 * (hp + hm);
    double v = (fm2 - 8.0 * fm1 + 8.0 * f1 - f2) / (12.0 * h_eff);
    return isfinite(v) ? v : NAN;
}

/** 求根收敛后的验收。 */
static bool is_root_residual(double y, double ya, double yb, double root_tol)
{
    if (!isfinite(y)) return false;
    double r = fabs(y);
    if (r <= root_tol) return true;
    double local = dmin(fabs(ya), fabs(yb));
    return local > 0.0 && r <= FD_FEATURES_ROOT_ACCEPT_FACTOR * local;
}

/** 相切零点判据。 */
static bool is_tangent_zero(budget_fn *fn, double x_star, double g_star, double w)
{
    if (g_star == 0.0) return true;
    if (w <= 0.0 || !isfinite(w)) return false;
    double yl = fabs(budget_at(fn, x_star - w));
    double yr = fabs(budget_at(fn, x_star + w));
    if (!isfinite(yl) || !isfinite(yr)) return false;
    double ref = dmin(yl, yr);
    if (ref <= 0.0) return false;
    return g_star <= FD_FEATURES_TOUCH_REL * ref;
}

/** 驻点处"确实比邻居大/小"的校验。 */
static bool is_local_extremum(budget_fn *fn, double x0, double y0, double h, bool is_max)
{
    double yl = budget_at(fn, x0 - h);
    double yr = budget_at(fn, x0 + h);
    if (!isfinite(yl) || !isfinite(yr)) return false;
    double eps = 1e-6 * dmax(fabs(y0), dmax(fabs(yl), fabs(yr)));
    if (is_max) return y0 >= yl - eps && y0 >= yr - eps;
    return y0 <= yl + eps && y0 <= yr + eps;
}

/* ------------------------------------------------------------------ */
/* 排序去重                                                            */
/* ------------------------------------------------------------------ */

static int dedupe_sorted(double *values, int n, int limit)
{
    if (limit <= 0 || n <= 0) return 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (isfinite(values[i])) values[count++] = values[i];
    }
    qsort(values, (size_t)count, sizeof(double), cmp_double);
    int out = 0;
    for (int i = 0; i < count; i++) {
        double v = values[i];
        if (out == 0) {
            values[out++] = v;
        } else {
            double last = values[out - 1];
            double tol = FD_FEATURES_DEDUPE_REL * dmax(1.0, dmax(fabs(v), fabs(last)));
            if (v - last > tol) values[out++] = v;
        }
        if (out >= limit) break;
    }
    return out;
}

static int cmp_extremum(const void *pa, const void *pb)
{
    const fd_extremum *a = (const fd_extremum *)pa;
    const fd_extremum *b = (const fd_extremum *)pb;
    return (a->x > b->x) - (a->x < b->x);
}

static int dedupe_extrema(fd_extremum *values, int n, int limit)
{
    if (limit <= 0 || n <= 0) return 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (isfinite(values[i].x) && isfinite(values[i].y)) values[count++] = values[i];
    }
    qsort(values, (size_t)count, sizeof(fd_extremum), cmp_extremum);
    int out = 0;
    for (int i = 0; i < count; i++) {
        if (out == 0) {
            values[out++] = values[i];
        } else {
            const fd_extremum *last = &values[out - 1];
            double tol = FD_FEATURES_DEDUPE_REL *
                         dmax(1.0, dmax(fabs(values[i].x), fabs(last->x)));
            if (values[i].x - last->x > tol) values[out++] = values[i];
        }
        if (out >= limit) break;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* 主入口                                                              */
/* ------------------------------------------------------------------ */

void fd_features_analyze(const fd_expr *f, double x_min, double x_max,
                         int limit, fd_features *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (!f) return;
    /* 多变量（隐函数上下文）没法按单个 x 描述特征。 */
    if (fd_expr_var_count(f) > 1) return;
    if (!isfinite(x_min) || !isfinite(x_max) || x_min >= x_max) return;

    double range = x_max - x_min;
    double step = range / FD_FEATURES_SAMPLES;
    if (!isfinite(range) || !isfinite(step) || step <= 0.0) return;

    budget_fn fn;
    fn.f = f;
    fn.arg[0] = 0.0;
    fn.evals = 0;
    int max_keep = limit < 0 ? 0 : limit;

    /* y 截距 */
    if (!(x_min > 0.0 || x_max < 0.0)) {
        fn.arg[0] = 0.0;
        double v = fd_expr_eval(f, fn.arg);
        if (isfinite(v)) {
            out->y_intercept = v;
            out->has_y_intercept = true;
        }
    }

    const int n = FD_FEATURES_SAMPLES;
    double *xs = (double *)malloc((size_t)(n + 1) * sizeof(double));
    double *ys = (double *)malloc((size_t)(n + 1) * sizeof(double));
    bool *bad = (bool *)calloc((size_t)n, sizeof(bool));
    int *dir = (int *)malloc((size_t)n * sizeof(int));
    double *d = (double *)malloc((size_t)(n + 1) * sizeof(double));
    int *valleys = (int *)malloc((size_t)n * sizeof(int));
    double *root_hits = (double *)malloc((size_t)FD_FEATURES_HITS_CAP * sizeof(double));
    fd_extremum *ext_hits = (fd_extremum *)malloc((size_t)FD_FEATURES_HITS_CAP *
                                                   sizeof(fd_extremum));
    if (!xs || !ys || !bad || !dir || !d || !valleys || !root_hits || !ext_hits) {
        free(xs);
        free(ys);
        free(bad);
        free(dir);
        free(d);
        free(valleys);
        free(root_hits);
        free(ext_hits);
        return;
    }

    for (int i = 0; i <= n; i++) {
        double x = (i == n) ? x_max : x_min + step * i;
        xs[i] = x;
        ys[i] = budget_at(&fn, x);
    }

    bool has_disc = false;

    /* ---------- 第 0 步：标记不可信区间 ---------- */
    for (int i = 0; i < n; i++) {
        double a = ys[i];
        double b = ys[i + 1];
        if (!isfinite(a) || !isfinite(b)) {
            bad[i] = true;
            has_disc = true;
            continue;
        }
        double aa = fabs(a);
        double bb = fabs(b);
        if (aa > FD_FEATURES_HUGE || bb > FD_FEATURES_HUGE) {
            bad[i] = true;
            has_disc = true;
            continue;
        }
        double lo = dmin(aa, bb);
        double hi = dmax(aa, bb);
        if (lo > 0.0 && hi / lo > FD_FEATURES_RATIO_LIMIT) {
            bad[i] = true;
            has_disc = true;
        }
    }

    double y_scale = representative_scale(ys, n + 1);
    double root_tol = FD_FEATURES_ROOT_ACCEPT_FACTOR * y_scale;

    int root_hit_count = 0;
#define ADD_ROOT(v)                                                          \
    do {                                                                     \
        if (root_hit_count < FD_FEATURES_HITS_CAP) root_hits[root_hit_count++] = (v); \
    } while (0)

    /* ---------- 路径 1：采样点恰好是 0 ---------- */
    for (int i = 0; i <= n; i++) {
        if (ys[i] != 0.0) continue;
        bool left_bad = i > 0 && bad[i - 1];
        bool right_bad = i < n && bad[i];
        if (left_bad && right_bad) continue;
        ADD_ROOT(xs[i]);
    }

    /* ---------- 路径 3 的候选：|f| 在网格上的局部极小 ---------- */
    int valley_count = 0;
    for (int j = 1; j < n; j++) {
        if (bad[j - 1] || bad[j]) continue;
        double y0 = fabs(ys[j]);
        double yl = fabs(ys[j - 1]);
        double yr = fabs(ys[j + 1]);
        if (!isfinite(y0) || !isfinite(yl) || !isfinite(yr)) continue;
        if (y0 < yl && y0 < yr) valleys[valley_count++] = j;
    }

    /* ---------- 路径 2：单调段 + 变号 → Brent 求根 ---------- */
    for (int i = 0; i < n; i++) {
        dir[i] = bad[i] ? 0 : sign_of(ys[i + 1] - ys[i]);
    }

    wrap_ctx ctx;
    ctx.fn = &fn;
    ctx.h = 0.0;

    int root_brackets = 0;
    int seg_start = 0;
    for (int i = 1; i <= n; i++) {
        bool boundary = (i == n) || dir[i - 1] == 0 || dir[i] == 0 || dir[i - 1] != dir[i];
        if (boundary) {
            int s = seg_start;
            int e = i;
            double ys_s = ys[s];
            double ys_e = ys[e];
            if (e > s && isfinite(ys_s) && isfinite(ys_e) &&
                (ys_s < 0.0) != (ys_e < 0.0) &&
                root_brackets < FD_FEATURES_MAX_ROOT_BRACKETS) {
                root_brackets++;
                double xr;
                if (brent_root(wrap_f, &ctx, xs[s], xs[e], ys_s, ys_e, &xr)) {
                    double yr = budget_at(&fn, xr);
                    if (is_root_residual(yr, ys_s, ys_e, root_tol)) {
                        ADD_ROOT(xr);
                    } else {
                        has_disc = true;
                        int at;
                        if (isfinite(xr)) {
                            double t = (xr - x_min) / step;
                            at = (int)t;
                            if (at < 0) at = 0;
                            if (at > n - 1) at = n - 1;
                        } else {
                            at = (e - 1 < s) ? s : e - 1;
                        }
                        mark_bad(bad, s, n);
                        mark_bad(bad, at, n);
                    }
                }
            }
            seg_start = i;
        }
    }

    /* ---------- 路径 3：|f| 谷底 → 相切零点 ---------- */
    double w = FD_FEATURES_TOUCH_W_STEPS * step;
    int tangent_tried = 0;
    for (int k = 0; k < valley_count; k++) {
        if (tangent_tried >= FD_FEATURES_MAX_TANGENT_BRACKETS) break;
        int j = valleys[k];
        if (bad[j - 1] || bad[j]) continue;
        tangent_tried++;
        /* 对 |f| 求极小，谷底值即 |f(x*)|。 */
        double rx, rf;
        if (!brent_min(wrap_abs_f, &ctx, xs[j - 1], xs[j], xs[j + 1], &rx, &rf)) continue;
        double g_star = rf;
        if (!isfinite(rx) || !isfinite(g_star)) continue;
        if (is_tangent_zero(&fn, rx, g_star, w)) ADD_ROOT(rx);
    }

    out->root_count = dedupe_sorted(root_hits, root_hit_count, max_keep);
    for (int i = 0; i < out->root_count; i++) out->roots[i] = root_hits[i];

    /* ---------- 路径 4：极值点 ---------- */
    int ext_hit_count = 0;
    double h = range / FD_FEATURES_DERIV_STEP_DIV;
    if (h > 0.0 && isfinite(h)) {
        /* 4a. 扫描用的差商：直接复用已采到的 ys[]，零额外求值。 */
        for (int i = 0; i <= n; i++) {
            double dl = (i > 0) ? (ys[i] - ys[i - 1]) / (xs[i] - xs[i - 1]) : NAN;
            double dr = (i < n) ? (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) : NAN;
            if (isfinite(dl) && isfinite(dr)) d[i] = 0.5 * (dl + dr);
            else if (isfinite(dr)) d[i] = dr;
            else if (isfinite(dl)) d[i] = dl;
            else d[i] = NAN;
        }
        double d_scale = representative_scale(d, n + 1);
        double d_zero = FD_FEATURES_DERIV_ZERO_FACTOR * d_scale;

        int last_idx = -1;
        int last_sign = 0;
        int extrema_tried = 0;
        for (int j = 0; j <= n; j++) {
            double dj = d[j];
            if (!isfinite(dj)) {
                last_idx = -1;
                last_sign = 0;
                continue;
            }
            int s = (dj > d_zero) ? 1 : (dj < -d_zero) ? -1 : 0;
            if (s == 0) continue;

            if (last_idx >= 0 && s != last_sign &&
                extrema_tried < FD_FEATURES_MAX_EXTREMUM_BRACKETS) {
                int span = j - last_idx;
                if (span >= 1 && span <= FD_FEATURES_MAX_DERIV_SPAN &&
                    !spans_bad(bad, n, last_idx, j)) {
                    extrema_tried++;
                    bool is_max = last_sign > 0;
                    wrap_ctx dctx;
                    dctx.fn = &fn;
                    dctx.h = h;
                    double xd;
                    if (brent_root(wrap_deriv5, &dctx, xs[last_idx], xs[j],
                                   d[last_idx], d[j], &xd)) {
                        double y0 = budget_at(&fn, xd);
                        double ref = dmax(y_scale, dmax(fabs(ys[last_idx]), fabs(ys[j])));
                        bool divergent = !isfinite(y0) || fabs(y0) > FD_FEATURES_HUGE ||
                                         (ref > 0.0 &&
                                          fabs(y0) > FD_FEATURES_DIVERGE_FACTOR * ref);
                        if (divergent) {
                            has_disc = true;
                            mark_bad(bad, last_idx, n);
                            mark_bad(bad, j - 1, n);
                        } else if (is_local_extremum(&fn, xd, y0, h, is_max)) {
                            if (ext_hit_count < FD_FEATURES_HITS_CAP) {
                                ext_hits[ext_hit_count].x = xd;
                                ext_hits[ext_hit_count].y = y0;
                                ext_hits[ext_hit_count].is_max = is_max;
                                ext_hit_count++;
                            }
                        }
                    }
                }
            }
            last_idx = j;
            last_sign = s;
        }
    }

    out->extrema_count = dedupe_extrema(ext_hits, ext_hit_count, max_keep);
    for (int i = 0; i < out->extrema_count; i++) out->extrema[i] = ext_hits[i];

    out->has_discontinuity = has_disc;

#undef ADD_ROOT
    free(xs);
    free(ys);
    free(bad);
    free(dir);
    free(d);
    free(valleys);
    free(root_hits);
    free(ext_hits);
}
