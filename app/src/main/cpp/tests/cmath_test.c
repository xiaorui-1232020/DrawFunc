/**
 * 主机端测试：复数表达式求值与结果标签。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cmath.h"

static int g_fail;

static void near_case(const char *src, fd_complex z, double want_re, double want_im)
{
    fd_expr_error err;
    memset(&err, 0, sizeof(err));
    fd_complex v;
    if (!fd_cpx_eval_text(src, NULL, 0, z, &v, &err)) {
        printf("  FAIL: %s -> compile error: %s (pos %d)\n", src, err.message,
               err.position);
        g_fail++;
        return;
    }
    if (fabs(v.re - want_re) > 1e-9 || fabs(v.im - want_im) > 1e-9) {
        printf("  FAIL: %s -> (%g, %g), want (%g, %g)\n", src, v.re, v.im, want_re,
               want_im);
        g_fail++;
    }
}

static void label_case(const char *src, const char *want)
{
    fd_expr_error err;
    memset(&err, 0, sizeof(err));
    fd_complex v;
    if (!fd_cpx_eval_text(src, NULL, 0, (fd_complex){0.0, 0.0}, &v, &err)) {
        printf("  FAIL: %s -> compile error\n", src);
        g_fail++;
        return;
    }
    char buf[128];
    fd_cpx_label(v, buf, sizeof(buf));
    if (strcmp(buf, want) != 0) {
        printf("  FAIL: label(%s) -> \"%s\", want \"%s\"\n", src, buf, want);
        g_fail++;
    }
}

static void error_case(const char *src)
{
    fd_expr_error err;
    memset(&err, 0, sizeof(err));
    fd_complex v;
    if (fd_cpx_eval_text(src, NULL, 0, (fd_complex){0.0, 0.0}, &v, &err)) {
        printf("  FAIL: %s should fail, got (%g, %g)\n", src, v.re, v.im);
        g_fail++;
    }
}

int main(void)
{
    fd_complex zero = {0.0, 0.0};

    /* 四则 */
    near_case("(1+2i)*(3+4i)", zero, -5.0, 10.0);
    near_case("(1+i)/(1-i)", zero, 0.0, 1.0);
    near_case("2i", zero, 0.0, 2.0);
    near_case("3-4i", zero, 3.0, -4.0);
    near_case("i^2", zero, -1.0, 0.0);
    near_case("2^3", zero, 8.0, 0.0);
    near_case("2^3^2", zero, 512.0, 0.0);
    near_case("-2^2", zero, -4.0, 0.0);

    /* 常量与函数 */
    near_case("pi", zero, M_PI, 0.0);
    near_case("e", zero, M_E, 0.0);
    near_case("sqrt(-1)", zero, 0.0, 1.0);
    near_case("exp(i*pi)", zero, -1.0, 0.0);
    near_case("ln(e)", zero, 1.0, 0.0);
    near_case("log(100)", zero, 2.0, 0.0);
    near_case("log(2,8)", zero, 3.0, 0.0);
    near_case("abs(3+4i)", zero, 5.0, 0.0);
    near_case("arg(i)", zero, M_PI / 2.0, 0.0);
    near_case("conj(2-3i)", zero, 2.0, 3.0);
    near_case("re(2-3i)", zero, 2.0, 0.0);
    near_case("im(2-3i)", zero, -3.0, 0.0);
    near_case("sin(0)", zero, 0.0, 0.0);
    near_case("cos(0)", zero, 1.0, 0.0);
    near_case("pow(2,3)", zero, 8.0, 0.0);
    near_case("|3+4i|", zero, 5.0, 0.0);

    /* 主变量 z */
    near_case("z^2", (fd_complex){1.0, 1.0}, 0.0, 2.0);
    near_case("z+i", (fd_complex){1.0, 1.0}, 1.0, 2.0);

    /* 参数编译期折叠 */
    {
        fd_param ps[1] = {{"a", 2.0}};
        fd_expr_error err;
        memset(&err, 0, sizeof(err));
        fd_complex v;
        if (!fd_cpx_eval_text("a+i", ps, 1, zero, &v, &err)) {
            printf("  FAIL: param a\n");
            g_fail++;
        } else if (fabs(v.re - 2.0) > 1e-9 || fabs(v.im - 1.0) > 1e-9) {
            printf("  FAIL: param a -> (%g,%g)\n", v.re, v.im);
            g_fail++;
        }
    }

    /* 标签 */
    label_case("2+3i", "2+3i");
    label_case("2-3i", "2-3i");
    label_case("i", "i");
    label_case("-i", "-i");
    label_case("0", "0");
    label_case("1.5-0.5i", "1.5-0.5i");
    label_case("1.234+0i", "1.23");

    /* 错误 */
    error_case("");
    error_case("1+");
    error_case("foo+1");
    error_case("sin(1,2)");
    error_case("(1+2i");
    error_case("1 2 +");
    /* 下面三个覆盖出错时释放已解析子树的路径（ASan 下可查泄漏）。 */
    error_case("2^");
    error_case("sin(1");
    error_case("pow(1)");

    if (g_fail == 0) {
        printf("cmath: all assertions passed\n");
        return 0;
    }
    printf("cmath: %d failure(s)\n", g_fail);
    return 1;
}
