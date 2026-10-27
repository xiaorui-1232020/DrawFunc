/*
 * fd_features_analyze 的 host 测试。
 *
 * 用 bash app/src/main/cpp/tests/build_features.sh 编译运行。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fd_features.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failed;
static int g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failed++;                                                        \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

static bool near(double a, double b, double tol)
{
    return isfinite(a) && isfinite(b) && fabs(a - b) <= tol;
}

/** 编译以 x 为唯一变量的表达式（弧度制，和 Kotlin 测试一致）。 */
static fd_expr *compile_x(const char *src)
{
    const char *vars[1] = {"x"};
    fd_expr_error err;
    memset(&err, 0, sizeof(err));
    fd_expr *e = fd_expr_compile(src, vars, 1, false, NULL, 0, &err);
    if (!e) printf("  compile failed: %s -> %s\n", src, err.message);
    return e;
}

static fd_features analyze(const char *src, double lo, double hi)
{
    fd_features feats;
    memset(&feats, 0, sizeof(feats));
    fd_expr *e = compile_x(src);
    if (!e) {
        g_failed++;
        return feats;
    }
    fd_features_analyze(e, lo, hi, FD_FEATURE_MAX, &feats);
    fd_expr_free(e);
    return feats;
}

/* ------------------------------------------------------------------ */

static void test_parabola(void)
{
    fd_features f = analyze("x^2", -10, 10);
    CHECK(f.root_count == 1, "x^2: roots=%d (want 1)", f.root_count);
    if (f.root_count == 1) CHECK(near(f.roots[0], 0.0, 1e-6), "x^2 root=%g", f.roots[0]);
    CHECK(f.extrema_count == 1, "x^2: extrema=%d (want 1)", f.extrema_count);
    if (f.extrema_count == 1) {
        CHECK(near(f.extrema[0].x, 0.0, 1e-6), "x^2 extremum x=%g", f.extrema[0].x);
        CHECK(f.extrema[0].is_max == false, "x^2 extremum should be min");
    }
    CHECK(f.has_y_intercept && near(f.y_intercept, 0.0, 1e-12), "x^2 y-intercept");
    CHECK(!f.has_discontinuity, "x^2 should not report discontinuity");
}

static void test_cubic(void)
{
    fd_features f = analyze("x^3-3*x", -5, 5);
    CHECK(f.root_count == 3, "cubic: roots=%d (want 3)", f.root_count);
    if (f.root_count == 3) {
        CHECK(near(f.roots[0], -sqrt(3.0), 1e-6), "cubic root0=%g", f.roots[0]);
        CHECK(near(f.roots[1], 0.0, 1e-6), "cubic root1=%g", f.roots[1]);
        CHECK(near(f.roots[2], sqrt(3.0), 1e-6), "cubic root2=%g", f.roots[2]);
    }
    CHECK(f.extrema_count == 2, "cubic: extrema=%d (want 2)", f.extrema_count);
    if (f.extrema_count == 2) {
        CHECK(near(f.extrema[0].x, -1.0, 1e-6), "cubic ext0 x=%g", f.extrema[0].x);
        CHECK(near(f.extrema[0].y, 2.0, 1e-6), "cubic ext0 y=%g", f.extrema[0].y);
        CHECK(f.extrema[0].is_max, "cubic ext0 should be max");
        CHECK(near(f.extrema[1].x, 1.0, 1e-6), "cubic ext1 x=%g", f.extrema[1].x);
        CHECK(near(f.extrema[1].y, -2.0, 1e-6), "cubic ext1 y=%g", f.extrema[1].y);
        CHECK(!f.extrema[1].is_max, "cubic ext1 should be min");
    }
}

static void test_sin(void)
{
    /* [-10,10] 里 sin 的零点是 -3π, -2π, -π, 0, π, 2π, 3π，limit=5 只留前 5 个。 */
    fd_features f = analyze("sin(x)", -10, 10);
    CHECK(f.root_count == FD_FEATURE_MAX, "sin: roots=%d (want %d)",
          f.root_count, FD_FEATURE_MAX);
    if (f.root_count >= 4) {
        CHECK(near(f.roots[0], -3 * M_PI, 1e-6), "sin root0=%g", f.roots[0]);
        CHECK(near(f.roots[1], -2 * M_PI, 1e-6), "sin root1=%g", f.roots[1]);
        CHECK(near(f.roots[2], -M_PI, 1e-6), "sin root2=%g", f.roots[2]);
        CHECK(near(f.roots[3], 0.0, 1e-6), "sin root3=%g", f.roots[3]);
    }
    CHECK(f.extrema_count == FD_FEATURE_MAX, "sin: extrema=%d (want %d)",
          f.extrema_count, FD_FEATURE_MAX);
}

static void test_reciprocal(void)
{
    fd_features f = analyze("1/x", -10, 10);
    CHECK(f.root_count == 0, "1/x: roots=%d (want 0)", f.root_count);
    CHECK(f.extrema_count == 0, "1/x: extrema=%d (want 0)", f.extrema_count);
    CHECK(f.has_discontinuity, "1/x should report discontinuity");
}

static void test_no_root_offset(void)
{
    fd_features f = analyze("x^2+1e-9", -10, 10);
    CHECK(f.root_count == 0, "x^2+1e-9: roots=%d (want 0)", f.root_count);
}

static void test_tiny_scale(void)
{
    fd_features f = analyze("1e-12*x^2", -10, 10);
    CHECK(f.root_count == 1, "1e-12*x^2: roots=%d (want 1)", f.root_count);
    if (f.root_count == 1) CHECK(near(f.roots[0], 0.0, 1e-5), "tiny root=%g", f.roots[0]);
}

static void test_tangent_double_root(void)
{
    fd_features f = analyze("(x-2)^2", -10, 10);
    CHECK(f.root_count == 1, "(x-2)^2: roots=%d (want 1)", f.root_count);
    if (f.root_count == 1) CHECK(near(f.roots[0], 2.0, 1e-5), "double root=%g", f.roots[0]);
}

static void test_constant(void)
{
    fd_features f = analyze("3", -10, 10);
    CHECK(f.root_count == 0, "3: roots=%d", f.root_count);
    CHECK(f.extrema_count == 0, "3: extrema=%d", f.extrema_count);
    CHECK(f.has_y_intercept && near(f.y_intercept, 3.0, 1e-12), "3 y-intercept");
}

static void test_y_intercept_out_of_range(void)
{
    fd_features f = analyze("x", 2, 8);
    CHECK(!f.has_y_intercept, "x on [2,8] should have no y-intercept");
    CHECK(f.root_count == 0, "x on [2,8] roots=%d", f.root_count);
}

static void test_bad_ranges(void)
{
    fd_features f = analyze("x", 5, 5);
    CHECK(f.root_count == 0 && f.extrema_count == 0, "collapsed range should be empty");
    f = analyze("x", NAN, 1);
    CHECK(f.root_count == 0, "NaN range should be empty");
}

static void test_constraint_no_hang(void)
{
    /* 病态函数：1/sin(1/x) 在 0 附近无限振荡，必须靠预算安全退出。 */
    fd_features f = analyze("1/sin(1/x)", -1, 1);
    CHECK(true, "pathological function returned");
    (void)f;
}

int main(void)
{
    test_parabola();
    test_cubic();
    test_sin();
    test_reciprocal();
    test_no_root_offset();
    test_tiny_scale();
    test_tangent_double_root();
    test_constant();
    test_y_intercept_out_of_range();
    test_bad_ranges();
    test_constraint_no_hang();

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
