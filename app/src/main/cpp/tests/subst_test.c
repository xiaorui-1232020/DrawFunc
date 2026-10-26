/**
 * 主机端测试：x(A)/y(A) 与标量（距离/角/面积）代入表达式。
 *
 * 只测纯文本替换规则，不碰界面与 LVGL。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "geo.h"
#include "model.h"
#include "subst.h"

static int g_fail;

static void expect(const char *src, const char *want)
{
    char out[512];
    fd_subst_geo(src, out, sizeof(out));
    if (strcmp(out, want) != 0) {
        printf("  FAIL: %s -> %s (want %s)\n", src, out, want);
        g_fail++;
    }
}

int main(void)
{
    model_init();

    fd_geo *a = model_add_geo(FD_GEO_POINT);
    a->p[0][0] = 1.5;
    a->p[0][1] = 2.5;

    fd_geo *b = model_add_geo(FD_GEO_POINT);
    b->p[0][0] = -3.0;
    b->p[0][1] = 0.25;

    fd_geo *d = model_add_geo(FD_GEO_DISTANCE);
    d->p[0][0] = 0.0;
    d->p[0][1] = 0.0;
    d->p[1][0] = 3.0;
    d->p[1][1] = 4.0;

    fd_geo *poly = model_add_geo(FD_GEO_POLYGON);
    poly->npts = 3;
    poly->p[0][0] = 0.0;
    poly->p[0][1] = 0.0;
    poly->p[1][0] = 2.0;
    poly->p[1][1] = 0.0;
    poly->p[2][0] = 0.0;
    poly->p[2][1] = 2.0;

    if (strcmp(a->name, "A") != 0 || strcmp(b->name, "B") != 0) {
        printf("  FAIL: point names %s %s\n", a->name, b->name);
        g_fail++;
    }
    if (strcmp(d->name, "f") != 0 || strcmp(poly->name, "g") != 0) {
        printf("  FAIL: scalar names %s %s\n", d->name, poly->name);
        g_fail++;
    }

    /* 取坐标 */
    expect("x(A)", "(1.5)");
    expect("y(A)", "(2.5)");
    expect("y( B )", "(0.25)");
    expect("2x(A)", "2(1.5)");
    expect("x(A)+y(A)", "(1.5)+(2.5)");
    expect("x ( A )", "(1.5)"); /* 括号内外都容忍空格 */

    /* 不认的写法原样保留 */
    expect("x(B", "x(B");
    expect("x()", "x()");
    expect("x(a)", "x(a)");
    expect("X(A)", "X(A)");
    expect("ax(A)", "ax(A)");
    expect("x(A1)", "x(A1)");

    /* 标量名 */
    expect("f", "(5)");
    expect("f*2", "(5)*2");
    expect("x^2+g", "x^2+(2)");
    expect("sin(x)+f", "sin(x)+(5)");
    expect("f1", "f1");   /* 不是已知对象 */
    expect("F", "F");     /* 标量名区分大小写 */

    if (g_fail == 0) {
        printf("subst: all assertions passed\n");
        return 0;
    }
    printf("subst: %d failure(s)\n", g_fail);
    return 1;
}
