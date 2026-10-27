/*
 * expr.c / mathfns.c 的 assert 风格测试。
 *
 * 用 bash tests/build_expr.sh 编译运行；全部通过时退出码为 0。
 */

#include "expr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846
#define TEST_E 2.71828182845904523536
#define TEST_PHI 1.6180339887498949

static int g_checks = 0;
static int g_failures = 0;

static const char *const VARS_X[] = {"x"};
static const char *const VARS_XY[] = {"x", "y"};
static const char *const VARS_THETA[] = {"θ"};
static const char *const VARS_THETA_T[] = {"θ", "t"};
static const char *const VARS_A[] = {"a"};

static fd_expr *compile_at(const char *src, const char *const *vars, int nvars,
                           bool degrees, const fd_param *params, int nparams,
                           int line)
{
    fd_expr_error err;
    fd_expr *e = fd_expr_compile(src, vars, nvars, degrees, params, nparams, &err);

    g_checks++;
    if (!e) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: compile \"%s\" failed: %s @%d\n",
                line, src ? src : "(null)", err.message, err.position);
    }
    return e;
}

static double eval1_at(const char *src, double x, bool degrees, int line)
{
    fd_expr *e = compile_at(src, VARS_X, 1, degrees, NULL, 0, line);
    double v;

    if (!e) return NAN;
    v = fd_expr_eval(e, &x);
    fd_expr_free(e);
    return v;
}

static double eval2_at(const char *src, double x, double y, bool degrees, int line)
{
    fd_expr *e = compile_at(src, VARS_XY, 2, degrees, NULL, 0, line);
    double args[2];
    double v;

    if (!e) return NAN;
    args[0] = x;
    args[1] = y;
    v = fd_expr_eval(e, args);
    fd_expr_free(e);
    return v;
}

static void check_near(double got, double want, double eps,
                       const char *label, int line)
{
    bool ok;

    g_checks++;
    if (isnan(want)) {
        ok = isnan(got);
    } else if (isinf(want)) {
        ok = (got == want);
    } else {
        ok = (got == want) || (isfinite(got) && fabs(got - want) <= eps);
    }

    if (!ok) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: %s = %.17g, want %.17g\n",
                line, label, got, want);
    }
}

#define NEAR(got, want, eps, label) check_near((got), (want), (eps), (label), __LINE__)
#define E1(src, x) eval1_at((src), (x), false, __LINE__)
#define E1D(src, x) eval1_at((src), (x), true, __LINE__)
#define E2(src, x, y) eval2_at((src), (x), (y), false, __LINE__)

static void expect_error_at(const char *src, const char *const *vars, int nvars,
                            bool degrees, int want_pos, const char *want_sub,
                            int line)
{
    fd_expr_error err;

    memset(&err, 0, sizeof(err));
    err.position = -2;

    g_checks++;
    {
        fd_expr *e = fd_expr_compile(src, vars, nvars, degrees, NULL, 0, &err);
        if (e) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: \"%s\" 应该报错，却编译成功了\n",
                    line, src ? src : "(null)");
            fd_expr_free(e);
            return;
        }
    }

    if (want_pos >= 0 && err.position != want_pos) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: \"%s\" 的位置是 %d，期望 %d（%s）\n",
                line, src, err.position, want_pos, err.message);
    }
    g_checks++;

    if (want_sub) {
        g_checks++;
        if (!strstr(err.message, want_sub)) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: \"%s\" 的报错里没有「%s」：%s\n",
                    line, src, want_sub, err.message);
        }
    }
}

#define EXPECT_ERR(src, vars, n, pos, sub) \
    expect_error_at((src), (vars), (n), false, (pos), (sub), __LINE__)

/** 与 expect_error_at 相同，但走 fd_expr_compile_equation。 */
static void expect_eq_error_at(const char *src, const char *const *vars, int nvars,
                               int want_pos, const char *want_sub, int line)
{
    fd_expr_error err;
    fd_expr *e;

    memset(&err, 0, sizeof(err));
    err.position = -2;

    g_checks++;
    e = fd_expr_compile_equation(src, vars, nvars, false, NULL, 0, &err);
    if (e) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: 等式 \"%s\" 应该报错，却编译成功了\n",
                line, src ? src : "(null)");
        fd_expr_free(e);
        return;
    }

    g_checks++;
    if (want_pos >= 0 && err.position != want_pos) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: 等式 \"%s\" 的位置是 %d，期望 %d（%s）\n",
                line, src, err.position, want_pos, err.message);
    }

    if (want_sub) {
        g_checks++;
        if (!strstr(err.message, want_sub)) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: 等式 \"%s\" 的报错里没有「%s」：%s\n",
                    line, src, want_sub, err.message);
        }
    }
}

#define EXPECT_EQ_ERR(src, vars, n, pos, sub) \
    expect_eq_error_at((src), (vars), (n), (pos), (sub), __LINE__)

/* ------------------------------------------------------------------ */

static void test_precedence(void)
{
    NEAR(E1("2+3*4", 0.0), 14.0, 1e-12, "2+3*4");
    NEAR(E1("(1+2)*3", 0.0), 9.0, 1e-12, "(1+2)*3");
    NEAR(E1("10/4", 0.0), 2.5, 1e-12, "10/4");
    NEAR(E1("2^3^2", 0.0), 512.0, 1e-12, "2^3^2");
    NEAR(E1("-2^2", 0.0), -4.0, 1e-12, "-2^2");
    NEAR(E1("(-2)^2", 0.0), 4.0, 1e-12, "(-2)^2");
    NEAR(E1("2^-1", 0.0), 0.5, 1e-12, "2^-1");
    NEAR(E1("7%3", 0.0), 1.0, 1e-12, "7%3");
    NEAR(E1("-1%3", 0.0), 2.0, 1e-12, "-1%3");
    NEAR(E1("1+2*3^2", 0.0), 19.0, 1e-12, "1+2*3^2");
    NEAR(E1("2*3/4", 0.0), 1.5, 1e-12, "2*3/4");
    NEAR(E1("2-3-4", 0.0), -5.0, 1e-12, "2-3-4");
    NEAR(E1("8/4/2", 0.0), 1.0, 1e-12, "8/4/2");
    NEAR(E1("--3", 0.0), 3.0, 1e-12, "--3");
    NEAR(E1("+3", 0.0), 3.0, 1e-12, "+3");
    /* 中括号、分号与连续阶乘 */
    NEAR(E1("[1+2]*3", 0.0), 9.0, 1e-12, "[1+2]*3");
    NEAR(E1("max(1;7;3)", 0.0), 7.0, 1e-12, "max(1;7;3)");
    NEAR(E1("3!!", 0.0), 720.0, 1e-12, "3!!");
}

static void test_implicit_and_numbers(void)
{
    NEAR(E1("2x", 3.0), 6.0, 1e-12, "2x");
    NEAR(E1("3sin(x)", 0.0), 0.0, 1e-12, "3sin(x)");
    NEAR(E1("(x+1)(x-1)", 3.0), 8.0, 1e-12, "(x+1)(x-1)");
    NEAR(E1("2π", 0.0), 2.0 * TEST_PI, 0.0, "2π");
    NEAR(E2("xy", 2.0, 5.0), 10.0, 1e-12, "xy");
    NEAR(E1("2πx", 3.0), 2.0 * TEST_PI * 3.0, 1e-12, "2πx");
    NEAR(E1("2e", 0.0), 2.0 * TEST_E, 0.0, "2e");
    NEAR(E1("2πe", 0.0), 2.0 * TEST_PI * TEST_E, 1e-12, "2πe");

    NEAR(E1(".5", 0.0), 0.5, 1e-12, ".5");
    NEAR(E1("1.", 0.0), 1.0, 1e-12, "1.");
    NEAR(E1("2E-3", 0.0), 0.002, 1e-15, "2E-3");
    NEAR(E1("2e3", 0.0), 2000.0, 1e-12, "2e3");
    NEAR(E1("1e-2", 0.0), 0.01, 1e-15, "1e-2");
    NEAR(E1("x x", 4.0), 16.0, 1e-12, "x x");
    NEAR(E1("2(x+1)", 2.0), 6.0, 1e-12, "2(x+1)");

    /* 对应 Kotlin 里“不传 degrees 时与旧行为逐位一致”的那组表达式 */
    NEAR(E1("2sin(x)cos(x)", 0.5), 2.0 * sin(0.5) * cos(0.5), 0.0, "2sin(x)cos(x)");
    NEAR(E1("sqrt(x^2)+|x|", -4.0), 8.0, 0.0, "sqrt(x^2)+|x| @-4");
    NEAR(E1("sqrt(x^2)+|x|", 9.0), 18.0, 0.0, "sqrt(x^2)+|x| @9");
    NEAR(E1("x!", 5.0), 120.0, 0.0, "x! @5");
    NEAR(E1("2^3^2+x%3", 7.0), 513.0, 0.0, "2^3^2+x%3 @7");
}

static void test_functions_without_parens(void)
{
    NEAR(E1("sin x", TEST_PI / 2.0), 1.0, 1e-12, "sin x");
    NEAR(E1("√9", 0.0), 3.0, 1e-12, "√9");
    NEAR(E1("√(4)", 0.0), 2.0, 1e-12, "√(4)");
    NEAR(E1("2√9", 0.0), 6.0, 1e-12, "2√9");
    NEAR(E1("ln e", 0.0), 1.0, 1e-12, "ln e");
    NEAR(E1("cos 0", 0.0), 1.0, 1e-12, "cos 0");
}

static void test_builtin_functions(void)
{
    NEAR(E1("sin(π/2)", 0.0), 1.0, 1e-12, "sin(π/2)");
    NEAR(E1("cos(0)", 0.0), 1.0, 1e-12, "cos(0)");
    NEAR(E1("ln(e)", 0.0), 1.0, 1e-12, "ln(e)");
    NEAR(E1("log(100)", 0.0), 2.0, 1e-12, "log(100)");
    NEAR(E1("log(2,8)", 0.0), 3.0, 1e-12, "log(2,8)");
    NEAR(E1("sqrt(16)", 0.0), 4.0, 1e-12, "sqrt(16)");
    NEAR(E1("cbrt(-8)", 0.0), -2.0, 1e-12, "cbrt(-8)");
    NEAR(E1("|(-3)|", 0.0), 3.0, 1e-12, "|(-3)|");
    NEAR(E1("|x|", -2.0), 2.0, 1e-12, "|x|");
    NEAR(E1("5!", 0.0), 120.0, 1e-12, "5!");
    NEAR(E1("0!", 0.0), 1.0, 1e-12, "0!");
    NEAR(E1("max(1,7,3)", 0.0), 7.0, 1e-12, "max");
    NEAR(E1("min(1,7,3)", 0.0), 1.0, 1e-12, "min");
    NEAR(E1("clamp(5,0,3)", 0.0), 3.0, 1e-12, "clamp");
    NEAR(E1("mod(-1,3)", 0.0), 2.0, 1e-12, "mod");
    NEAR(E1("root(-8,3)", 0.0), -2.0, 1e-9, "root");
    NEAR(E1("sinc(0)", 0.0), 1.0, 1e-12, "sinc(0)");
    NEAR(E1("erf(0)", 0.0), 0.0, 1e-12, "erf(0)");
    NEAR(E1("gamma(5)", 0.0), 24.0, 1e-9, "gamma(5)");
    NEAR(E1("gamma(0.5)", 0.0), sqrt(TEST_PI), 1e-9, "gamma(0.5)");
    NEAR(E1("atan2(1,1)", 0.0), TEST_PI / 4.0, 1e-12, "atan2(1,1)");
    NEAR(E1("hypot(3,4)", 0.0), 5.0, 1e-12, "hypot");
    NEAR(E1("hypot(3,4,12)", 0.0), 13.0, 1e-12, "hypot 三个参数");
    NEAR(E1("deg(π)", 0.0), 180.0, 1e-12, "deg(π)");
    NEAR(E1("rad(180)", 0.0), TEST_PI, 1e-12, "rad(180)");

    NEAR(E1("cot(π/4)", 0.0), 1.0, 1e-12, "cot");
    NEAR(E1("sec(0)", 0.0), 1.0, 1e-12, "sec");
    NEAR(E1("csc(π/2)", 0.0), 1.0, 1e-12, "csc");
    NEAR(E1("asin(1)", 0.0), TEST_PI / 2.0, 1e-12, "asin");
    NEAR(E1("acos(0.5)", 0.0), TEST_PI / 3.0, 1e-12, "acos");
    NEAR(E1("atan(1)", 0.0), TEST_PI / 4.0, 1e-12, "atan");
    NEAR(E1("sinh(0)", 0.0), 0.0, 0.0, "sinh(0)");
    NEAR(E1("cosh(0)", 0.0), 1.0, 0.0, "cosh(0)");
    NEAR(E1("tanh(0)", 0.0), 0.0, 0.0, "tanh(0)");
    NEAR(E1("coth(1)", 0.0), 1.0 / tanh(1.0), 0.0, "coth(1)");
    NEAR(E1("asinh(0)", 0.0), 0.0, 0.0, "asinh(0)");
    NEAR(E1("acosh(1)", 0.0), 0.0, 0.0, "acosh(1)");
    NEAR(E1("atanh(0)", 0.0), 0.0, 0.0, "atanh(0)");
    NEAR(E1("exp(0)", 0.0), 1.0, 0.0, "exp(0)");
    NEAR(E1("lg(1000)", 0.0), 3.0, 1e-12, "lg(1000)");
    NEAR(E1("lb(8)", 0.0), 3.0, 1e-12, "lb(8)");
    NEAR(E1("sign(-3)", 0.0), -1.0, 0.0, "sign(-3)");
    NEAR(E1("floor(-1.5)", 0.0), -2.0, 0.0, "floor");
    NEAR(E1("ceil(-1.5)", 0.0), -1.0, 0.0, "ceil");
    NEAR(E1("round(1.5)", 0.0), 2.0, 0.0, "round(1.5)");
    /* kotlin.math.round 是 rint：.5 取偶（2.5 -> 2、0.5 -> 0、-2.5 -> -2） */
    NEAR(E1("round(2.5)", 0.0), 2.0, 0.0, "round(2.5) ties to even");
    NEAR(E1("round(0.5)", 0.0), 0.0, 0.0, "round(0.5) ties to even");
    NEAR(E1("round(-2.5)", 0.0), -2.0, 0.0, "round(-2.5) ties to even");
    NEAR(E1("round(1.4)", 0.0), 1.0, 0.0, "round(1.4)");
    NEAR(E1("trunc(-1.5)", 0.0), -1.0, 0.0, "trunc");
    NEAR(E1("frac(-1.5)", 0.0), 0.5, 0.0, "frac");
    NEAR(E1("pow(2,10)", 0.0), 1024.0, 0.0, "pow");
    NEAR(E1("abs(-7)", 0.0), 7.0, 0.0, "abs");

    /* 大小写不敏感的函数名 */
    NEAR(E1("SIN(0)", 0.0), 0.0, 0.0, "SIN(0)");
    NEAR(E1("SQRT(9)", 0.0), 3.0, 0.0, "SQRT(9)");
    /* min/max 一个参数也合法（arity 1..8） */
    NEAR(E1("min(5)", 0.0), 5.0, 0.0, "min(5)");
}

static void test_constants(void)
{
    NEAR(E1("π", 0.0), TEST_PI, 0.0, "π");
    NEAR(E1("pi", 0.0), TEST_PI, 0.0, "pi");
    NEAR(E1("PI", 0.0), TEST_PI, 0.0, "PI");
    NEAR(E1("tau", 0.0), 2.0 * TEST_PI, 0.0, "tau");
    NEAR(E1("τ", 0.0), 2.0 * TEST_PI, 0.0, "τ");
    NEAR(E1("e", 0.0), TEST_E, 0.0, "e");
    NEAR(E1("phi", 0.0), TEST_PHI, 0.0, "phi");
    NEAR(E1("φ", 0.0), TEST_PHI, 0.0, "φ");
    NEAR(E1("inf", 0.0), INFINITY, 0.0, "inf");
    NEAR(E1("infinity", 0.0), INFINITY, 0.0, "infinity");
    NEAR(E1("nan", 0.0), NAN, 0.0, "nan");
    NEAR(E1("∞", 0.0), INFINITY, 0.0, "∞");

    /* 0.5 附近的组合常量 */
    NEAR(E1("2tau", 0.0), 4.0 * TEST_PI, 1e-12, "2tau");
    NEAR(E1("2φ", 0.0), 2.0 * TEST_PHI, 1e-12, "2φ");
    NEAR(E1("2πφ", 0.0), 2.0 * TEST_PI * TEST_PHI, 1e-12, "2πφ");
}

static void test_unicode_symbols(void)
{
    NEAR(E1("2×3", 0.0), 6.0, 0.0, "2×3");
    NEAR(E1("2·3", 0.0), 6.0, 0.0, "2·3");
    NEAR(E1("2⋅3", 0.0), 6.0, 0.0, "2⋅3");
    NEAR(E1("6÷2", 0.0), 3.0, 0.0, "6÷2");
    NEAR(E1("2−1", 0.0), 1.0, 0.0, "2−1");
    NEAR(E1("∛(-8)", 0.0), -2.0, 1e-12, "∛(-8)");
    NEAR(E1("2**2", 0.0), 4.0, 0.0, "2**2");
    NEAR(E1("2**3**2", 0.0), 512.0, 0.0, "2**3**2");
    NEAR(E1("2 × π", 0.0), 2.0 * TEST_PI, 1e-12, "2 × π");
    NEAR(E1("3!", 0.0), 6.0, 0.0, "3!");
    NEAR(E1("2^3!", 0.0), 64.0, 0.0, "2^3!");
    NEAR(E1("-3!", 0.0), -6.0, 0.0, "-3!");
    NEAR(E1("2|x|", -3.0), 6.0, 0.0, "2|x|");
}

static void test_special_values(void)
{
    NEAR(E1("1/0", 0.0), INFINITY, 0.0, "1/0");
    NEAR(E1("-1/0", 0.0), -INFINITY, 0.0, "-1/0");
    NEAR(E1("0/0", 0.0), NAN, 0.0, "0/0");
    NEAR(E1("sqrt(-1)", 0.0), NAN, 0.0, "sqrt(-1)");
    NEAR(E1("ln(-1)", 0.0), NAN, 0.0, "ln(-1)");
    NEAR(E1("asin(2)", 0.0), NAN, 0.0, "asin(2)");
    NEAR(E1("(-1)!", 0.0), NAN, 0.0, "(-1)!");
    NEAR(E1("0.5!", 0.0), sqrt(TEST_PI) / 2.0, 1e-9, "0.5!");
    NEAR(E1("2^2000", 0.0), INFINITY, 0.0, "2^2000");
    NEAR(E1("1e400", 0.0), INFINITY, 0.0, "1e400");
}

static void test_equations(void)
{
    fd_expr_error err;
    fd_expr *e;

    e = fd_expr_compile_equation("x^2+y^2=25", VARS_XY, 2, false, NULL, 0, &err);
    g_checks++;
    if (!e) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: equation compile: %s\n", __LINE__, err.message);
    } else {
        double a1[2] = {3.0, 4.0};
        double a2[2] = {4.0, 0.0};
        NEAR(fd_expr_eval(e, a1), 0.0, 1e-12, "x^2+y^2=25 @(3,4)");
        NEAR(fd_expr_eval(e, a2), -9.0, 1e-12, "x^2+y^2=25 @(4,0)");
        fd_expr_free(e);
    }

    e = fd_expr_compile_equation("x^2+y^2", VARS_XY, 2, false, NULL, 0, &err);
    g_checks++;
    if (!e) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: equation no-eq compile: %s\n", __LINE__, err.message);
    } else {
        double a[2] = {3.0, 4.0};
        NEAR(fd_expr_eval(e, a), 25.0, 1e-12, "x^2+y^2 @(3,4)");
        fd_expr_free(e);
    }

    e = fd_expr_compile_equation("y=sin(x)", VARS_XY, 2, true, NULL, 0, &err);
    g_checks++;
    if (!e) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: y=sin(x) compile: %s\n", __LINE__, err.message);
    } else {
        double a[2] = {90.0, 1.0};
        NEAR(fd_expr_eval(e, a), 0.0, 1e-9, "y=sin(x) DEG");
        g_checks++;
        if (!fd_expr_is_degrees(e)) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: equation 的 degrees 丢失\n", __LINE__);
        }
        fd_expr_free(e);
    }

    EXPECT_EQ_ERR("y=", VARS_X, 1, 1, "等号右边是空的");
    EXPECT_EQ_ERR("y=   ", VARS_X, 1, 1, "等号右边是空的");

    /* 与角度无关的等式在 DEG / RAD 下逐位一致 */
    {
        fd_expr *rd = fd_expr_compile_equation("x^2+y^2=25", VARS_XY, 2, true,
                                               NULL, 0, &err);
        fd_expr *rr = fd_expr_compile_equation("x^2+y^2=25", VARS_XY, 2, false,
                                               NULL, 0, &err);
        g_checks++;
        if (!rd || !rr) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: 无关角度的等式编译失败\n", __LINE__);
        } else {
            double a[2] = {3.0, 4.0};
            g_checks++;
            if (fd_expr_eval(rd, a) != fd_expr_eval(rr, a)) {
                g_failures++;
                fprintf(stderr, "FAIL line %d: DEG/RAD 下等式结果不一致\n", __LINE__);
            }
        }
        fd_expr_free(rd);
        fd_expr_free(rr);
    }

    /* 顶层等号定位 */
    g_checks++;
    if (fd_expr_top_level_equals("x^2+y^2=25") != 7) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: top_level_equals 应为 7\n", __LINE__);
    }
    g_checks++;
    if (fd_expr_top_level_equals("(x)=1") != 3) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: top_level_equals 应为 3\n", __LINE__);
    }
    g_checks++;
    if (fd_expr_top_level_equals("f(x=1)") != -1) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: 括号内等号不应被返回\n", __LINE__);
    }
    g_checks++;
    if (fd_expr_top_level_equals("π=1") != 1) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: 多字节字符后的等号下标应为 1\n", __LINE__);
    }
}

static void test_errors(void)
{
    EXPECT_ERR("", VARS_X, 1, 0, "表达式是空的");
    EXPECT_ERR("   ", VARS_X, 1, 0, "表达式是空的");
    EXPECT_ERR("x+", VARS_X, 1, 2, "表达式不完整");
    EXPECT_ERR("(x", VARS_X, 1, 2, "括号没有闭合");
    EXPECT_ERR("sin(x", VARS_X, 1, 5, "右括号");
    EXPECT_ERR("|x", VARS_X, 1, 2, "绝对值");
    EXPECT_ERR("foo(x)", VARS_X, 1, 0, "不认识的名字");
    EXPECT_ERR("1.2.3", VARS_X, 1, 3, "前面缺少运算符");
    EXPECT_ERR("sin(1,2)", VARS_X, 1, 0, "参数");
    EXPECT_ERR("sin()", VARS_X, 1, 0, "缺少参数");
    EXPECT_ERR("atan2(1)", VARS_X, 1, 0, "需要");
    EXPECT_ERR("atan2 x", VARS_X, 1, 0, "要写括号");
    EXPECT_ERR("x,y", VARS_X, 1, 1, "逗号");
    EXPECT_ERR("x=1", VARS_X, 1, 1, "只能有一个等号");
    EXPECT_ERR("x)", VARS_X, 1, 1, "多余的右括号");
    EXPECT_ERR("x*", VARS_X, 1, 2, "表达式不完整");
    EXPECT_ERR("2 3", VARS_X, 1, 2, "前面缺少运算符");
    EXPECT_ERR("$", VARS_X, 1, 0, "看不懂这个字符");
    EXPECT_ERR("*x", VARS_X, 1, 0, "运算符");
    EXPECT_ERR("x+*2", VARS_X, 1, 2, "运算符");
    EXPECT_ERR("(", VARS_X, 1, 1, "表达式不完整");
    EXPECT_ERR("||", VARS_X, 1, 2, "表达式不完整");
    EXPECT_ERR("y", VARS_X, 1, 0, "可用的变量");
    EXPECT_ERR("y", VARS_X, 1, 0, "可绑定参数");
    EXPECT_ERR("sinx", VARS_X, 1, 0, "括号");
    EXPECT_ERR("1+2*cookie", VARS_X, 1, 4, "cookie");
    EXPECT_ERR("z(x)", VARS_X, 1, 0, "z");
    EXPECT_ERR("zw", VARS_X, 1, 0, "zw");
    EXPECT_ERR("π+z", VARS_X, 1, 2, "z"); /* position 按码点，不是字节 */

    /* 参数名写错要报错（对应 ParameterSet 的 require） */
    {
        fd_param bad[1];
        fd_expr_error err;
        fd_expr *e;

        bad[0].name = "alpha";
        bad[0].value = 2.0;
        g_checks++;
        e = fd_expr_compile("x", VARS_X, 1, false, bad, 1, &err);
        if (e) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: 非法参数名竟然编译成功\n", __LINE__);
            fd_expr_free(e);
        } else if (!strstr(err.message, "alpha")) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: 非法参数名的报错没点名：%s\n", __LINE__, err.message);
        }
        g_checks++;
    }
}

static void test_degrees(void)
{
    NEAR(E1D("sin(x)", 90.0), 1.0, 1e-9, "DEG sin(90)");
    NEAR(E1D("sin(x)", 0.0), 0.0, 1e-9, "DEG sin(0)");
    NEAR(E1D("sin(x)", 30.0), 0.5, 1e-9, "DEG sin(30)");
    NEAR(E1D("sin(x)", 270.0), -1.0, 1e-9, "DEG sin(270)");
    NEAR(E1D("sin(2x)", 45.0), 1.0, 1e-9, "DEG sin(2x)");
    NEAR(E1D("sin(180)", 0.0), 0.0, 1e-9, "DEG sin(180)");
    NEAR(E1D("cos(x)", 60.0), 0.5, 1e-9, "DEG cos(60)");
    NEAR(E1D("cos(x)", 90.0), 0.0, 1e-9, "DEG cos(90)");
    NEAR(E1D("cos(x)", 180.0), -1.0, 1e-9, "DEG cos(180)");
    NEAR(E1D("tan(x)", 45.0), 1.0, 1e-9, "DEG tan(45)");
    NEAR(E1D("cot(x)", 45.0), 1.0, 1e-9, "DEG cot(45)");
    NEAR(E1D("sec(x)", 60.0), 2.0, 1e-9, "DEG sec(60)");
    NEAR(E1D("csc(x)", 30.0), 2.0, 1e-9, "DEG csc(30)");
    NEAR(E1D("sin(x)^2+cos(x)^2", 37.0), 1.0, 1e-12, "DEG 恒等式");
    NEAR(E1D("tan(x)/sin(x)*cos(x)", 37.0), 1.0, 1e-9, "DEG tan 恒等式");

    NEAR(E1D("asin(x)", 1.0), 90.0, 1e-9, "DEG asin(1)");
    NEAR(E1D("acos(x)", 0.5), 60.0, 1e-9, "DEG acos(0.5)");
    NEAR(E1D("atan(x)", 1.0), 45.0, 1e-9, "DEG atan(1)");
    NEAR(E1D("asin(x)", 0.0), 0.0, 1e-9, "DEG asin(0)");
    NEAR(E1D("asin(sin(x))", 30.0), 30.0, 1e-9, "DEG asin(sin(30))");

    NEAR(eval2_at("atan2(y,x)", 1.0, 1.0, true, __LINE__), 45.0, 1e-9, "DEG atan2(1,1)");
    NEAR(eval2_at("atan2(y,x)", -1.0, 1.0, true, __LINE__), 135.0, 1e-9, "DEG atan2(-1,1)");
    NEAR(eval2_at("atan2(y,x)", 0.0, 1.0, true, __LINE__), 90.0, 1e-9, "DEG atan2(0,1)");

    /* 双曲函数不受角度制影响：逐位相同 */
    {
        static const char *srcs[] = {
            "sinh(x)", "cosh(x)", "tanh(x)", "coth(x)",
            "asinh(x)", "acosh(x)", "atanh(x)"
        };
        static const double vals[] = {-0.5, 0.0, 1.0, 1.5, 3.0};
        size_t s;

        for (s = 0; s < sizeof(srcs) / sizeof(srcs[0]); s++) {
            fd_expr *rad;
            fd_expr *deg;
            fd_expr_error err;

            rad = fd_expr_compile(srcs[s], VARS_X, 1, false, NULL, 0, &err);
            deg = fd_expr_compile(srcs[s], VARS_X, 1, true, NULL, 0, &err);
            g_checks++;
            if (!rad || !deg) {
                g_failures++;
                fprintf(stderr, "FAIL line %d: %s 编译失败\n", __LINE__, srcs[s]);
                fd_expr_free(rad);
                fd_expr_free(deg);
                continue;
            }
            for (size_t v = 0; v < sizeof(vals) / sizeof(vals[0]); v++) {
                double r = fd_expr_eval(rad, &vals[v]);
                double d = fd_expr_eval(deg, &vals[v]);
                g_checks++;
                if (r != d && !(isnan(r) && isnan(d))) {
                    g_failures++;
                    fprintf(stderr, "FAIL line %d: %s(%g) 被角度制影响：%g vs %g\n",
                            __LINE__, srcs[s], vals[v], r, d);
                }
            }
            fd_expr_free(rad);
            fd_expr_free(deg);
        }
    }

    /* deg/rad 两个显式换算函数不受开关影响 */
    NEAR(E1D("deg(π)", 0.0), 180.0, 1e-12, "DEG deg(π)");
    NEAR(E1D("rad(180)", 0.0), TEST_PI, 1e-12, "DEG rad(180)");
    NEAR(E1D("sin(deg(x))", TEST_PI / 2.0), 1.0, 1e-9, "DEG sin(deg(x))");
    NEAR(E1("sin(deg(x))", TEST_PI / 2.0), sin(TEST_PI / 2.0 * (180.0 / TEST_PI)),
         0.0, "RAD sin(deg(x))");

    /* 弧度模式仍是老行为 */
    NEAR(E1("sin(x)", TEST_PI / 2.0), 1.0, 1e-9, "RAD sin(π/2)");
    g_checks++;
    if (fabs(E1("sin(x)", 90.0) - 1.0) <= 0.1) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: RAD 下 sin(90) 不该等于 1\n", __LINE__);
    }

    /* 角度制是编译期开关：两个表达式共存、交替求值互不影响 */
    {
        fd_expr *d = fd_expr_compile("sin(x)+cos(x)", VARS_X, 1, true, NULL, 0, NULL);
        fd_expr *r = fd_expr_compile("sin(x)+cos(x)", VARS_X, 1, false, NULL, 0, NULL);
        g_checks++;
        if (!d || !r) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: DEG/RAD 共存编译失败\n", __LINE__);
        } else {
            for (int k = 0; k < 3; k++) {
                double v;
                g_checks++;
                if (!fd_expr_is_degrees(d) || fd_expr_is_degrees(r)) {
                    g_failures++;
                    fprintf(stderr, "FAIL line %d: is_degrees 不对\n", __LINE__);
                }
                v = 45.0;
                NEAR(fd_expr_eval(d, &v), sqrt(2.0), 1e-9, "DEG 共存");
                v = 0.0;
                NEAR(fd_expr_eval(r, &v), 1.0, 1e-9, "RAD 共存");
            }
        }
        fd_expr_free(d);
        fd_expr_free(r);
    }
}

static void test_parameters(void)
{
    static const double xs[] = {-3.0, -0.5, 0.0, 1.0, 7.25};

    for (size_t k = 0; k < sizeof(xs) / sizeof(xs[0]); k++) {
        NEAR(E1("a*x+b", xs[k]), xs[k] + 1.0, 0.0, "a*x+b 默认参数");
    }

    NEAR(E1("a", 0.0), 1.0, 0.0, "a 默认值");
    NEAR(E1("d", 0.0), 1.0, 0.0, "d 默认值");
    NEAR(E1("a+b+c+d", 0.0), 4.0, 0.0, "a+b+c+d");
    NEAR(E1("a*b*c*d", 0.0), 1.0, 0.0, "a*b*c*d");
    NEAR(E1("2a", 0.0), 2.0, 0.0, "2a");
    NEAR(E1("ax", 2.0), 2.0, 0.0, "ax");
    NEAR(E1("xa", 2.0), 2.0, 0.0, "xa");
    NEAR(E1("ab", 0.0), 1.0, 0.0, "ab");

    {
        fd_param p[2];
        fd_expr *f;

        p[0].name = "a";
        p[0].value = 2.0;
        p[1].name = "b";
        p[1].value = -3.0;

        f = compile_at("a*x+b", VARS_X, 1, false, p, 2, __LINE__);
        if (f) {
            double x = 2.0;
            double x0 = 0.0;
            NEAR(fd_expr_eval(f, &x), 1.0, 0.0, "a=2,b=-3 @2");
            NEAR(fd_expr_eval(f, &x0), -3.0, 0.0, "a=2,b=-3 @0");
            g_checks++;
            if (fd_expr_var_count(f) != 1) {
                g_failures++;
                fprintf(stderr, "FAIL line %d: var_count 应为 1\n", __LINE__);
            }
            fd_expr_free(f);
        }

        f = compile_at("a*x+b+c+d", VARS_X, 1, false, p, 2, __LINE__);
        if (f) {
            double x = 2.0;
            NEAR(fd_expr_eval(f, &x), 3.0, 0.0, "a=2,b=-3,c=d=1 @2");
            fd_expr_free(f);
        }

        /* 等号写法同样透传参数 */
        {
            fd_expr_error err;
            fd_expr *eq = fd_expr_compile_equation("y=a*x+b", VARS_XY, 2, false,
                                                   p, 2, &err);
            g_checks++;
            if (!eq) {
                g_failures++;
                fprintf(stderr, "FAIL line %d: 等式参数绑定：%s\n", __LINE__, err.message);
            } else {
                double args[2] = {2.0, 1.0};
                NEAR(fd_expr_eval(eq, args), 0.0, 0.0, "y=a*x+b 等式");
                fd_expr_free(eq);
            }
        }
    }

    /* 调用方自己把 a 声明成采样变量时，变量优先 */
    {
        fd_expr *e = compile_at("a", VARS_A, 1, false, NULL, 0, __LINE__);
        if (e) {
            double v = 7.0;
            NEAR(fd_expr_eval(e, &v), 7.0, 0.0, "变量 a 优先");
            g_checks++;
            if (fd_expr_var_count(e) != 1) {
                g_failures++;
                fprintf(stderr, "FAIL line %d: 变量 a 的 var_count\n", __LINE__);
            }
            fd_expr_free(e);
        }
    }

    /* 极坐标 / 参数方程上下文里 a b c d 仍可用 */
    {
        fd_expr *e = compile_at("a*θ+b", VARS_THETA, 1, false, NULL, 0, __LINE__);
        if (e) {
            double v = 2.0;
            NEAR(fd_expr_eval(e, &v), 3.0, 0.0, "a*θ+b");
            fd_expr_free(e);
        }
    }
    {
        fd_expr *e = compile_at("θ+t", VARS_THETA_T, 2, false, NULL, 0, __LINE__);
        if (e) {
            double args[2] = {3.0, 5.0};
            NEAR(fd_expr_eval(e, args), 8.0, 0.0, "θ+t");
            fd_expr_free(e);
        }
    }
    /* 隐函数上下文：a、b、c 与采样变量 x、y 混用 */
    {
        fd_expr *e = compile_at("a*x+b*y+c", VARS_XY, 2, false, NULL, 0, __LINE__);
        if (e) {
            double args[2] = {2.0, 2.0};
            NEAR(fd_expr_eval(e, args), 5.0, 0.0, "a*x+b*y+c");
            fd_expr_free(e);
        }
    }

    /* 没有用户自定义函数调用：a(x) 就是 a*(x) */
    {
        fd_expr *e = compile_at("a(x)", VARS_X, 1, false, NULL, 0, __LINE__);
        if (e) {
            double v = 3.0;
            NEAR(fd_expr_eval(e, &v), 3.0, 0.0, "a(x) = a*x");
            fd_expr_free(e);
        }
    }
    {
        fd_param pa;
        fd_expr *e;

        pa.name = "a";
        pa.value = 2.0;
        e = compile_at("2a(x)", VARS_X, 1, false, &pa, 1, __LINE__);
        if (e) {
            double v = 3.0;
            NEAR(fd_expr_eval(e, &v), 12.0, 0.0, "2a(x) = 2*a*x");
            fd_expr_free(e);
        }
    }
}

static void test_strip_prefix(void)
{
    char buf[64];

    fd_expr_strip_explicit_prefix("y = x^2", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "x^2") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: strip \"y = x^2\" -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("Y = 2x", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "2x") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: strip \"Y = 2x\" -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("f(x) = sin(x)", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "sin(x)") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: strip \"f(x) = ...\" -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("F ( X ) = x+1", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "x+1") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: strip \"F ( X ) = ...\" -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("   y=  x+1   ", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "x+1") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: strip 前后空白 -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("x^2", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "x^2") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: 不该乱剥前缀 -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("r = 1", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "r = 1") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: 只剥 y=/f(x)= -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("y=", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: \"y=\" 应剥成空串 -> \"%s\"\n", __LINE__, buf);
    }

    fd_expr_strip_explicit_prefix("f(x)=", buf, sizeof(buf));
    g_checks++;
    if (strcmp(buf, "") != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: \"f(x)=\" 应剥成空串 -> \"%s\"\n", __LINE__, buf);
    }

    /* 前缀剥离后的结果可以直接交给引擎 */
    fd_expr_strip_explicit_prefix("y = x^2", buf, sizeof(buf));
    NEAR(E1(buf, 3.0), 9.0, 0.0, "strip 后编译");
}

static void test_eval_robustness(void)
{
    fd_expr *e = fd_expr_compile("x+1", VARS_X, 1, false, NULL, 0, NULL);

    g_checks++;
    if (!e) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: x+1 编译失败\n", __LINE__);
    } else {
        double v = 2.0;
        NEAR(fd_expr_eval(e, &v), 3.0, 0.0, "x+1 @2");
        NEAR(fd_expr_eval(e, NULL), NAN, 0.0, "args=NULL 返回 NAN 而不是崩溃");
        g_checks++;
        if (fd_expr_var_count(e) != 1) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: var_count\n", __LINE__);
        }
        g_checks++;
        if (fd_expr_is_degrees(e)) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: 不该是 DEG\n", __LINE__);
        }
        fd_expr_free(e);
    }

    NEAR(fd_expr_eval(NULL, NULL), NAN, 0.0, "fd_expr_eval(NULL)");
    fd_expr_free(NULL);

    g_checks++;
    if (fd_expr_var_count(NULL) != 0) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: var_count(NULL)\n", __LINE__);
    }
    g_checks++;
    if (fd_expr_is_degrees(NULL)) {
        g_failures++;
        fprintf(stderr, "FAIL line %d: is_degrees(NULL)\n", __LINE__);
    }

    /* 反复编译失败不应泄漏/崩溃（配合 ASan 可进一步验证） */
    for (int k = 0; k < 100; k++) {
        fd_expr_error err;
        fd_expr *bad = fd_expr_compile("sin(", VARS_X, 1, false, NULL, 0, &err);
        g_checks++;
        if (bad) {
            g_failures++;
            fprintf(stderr, "FAIL line %d: 非法表达式竟然成功\n", __LINE__);
            fd_expr_free(bad);
        }
    }
}

int main(void)
{
    test_precedence();
    test_implicit_and_numbers();
    test_functions_without_parens();
    test_builtin_functions();
    test_constants();
    test_unicode_symbols();
    test_special_values();
    test_equations();
    test_errors();
    test_degrees();
    test_parameters();
    test_strip_prefix();
    test_eval_robustness();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
