/**
 * 复数运算 + 复数表达式求值（复平面模式用）。
 *
 * 只负责数学：词法 / 递归下降解析 / 求值 / 结果标签。不依赖 LVGL，也不碰文件。
 * 语义对齐旧版 ComplexMath.kt：i / pi / e 常量、参数 a~d 编译期折常量、主变量 z、
 * `-2^2 = -4`、`2^3^2 = 512`、`2i` 隐式乘法、`|z|` 绝对值。
 */
#ifndef FD_CMATH_H
#define FD_CMATH_H

#include <stdbool.h>
#include <stddef.h>

#include "expr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double re;
    double im;
} fd_complex;

typedef struct fd_cpx_node fd_cpx_node;

/**
 * 编译复数表达式。
 *
 * @param params 参数名（a/b/c/d）与取值；编译期折成常量，求值路径里没有查表。
 * @return 失败返回 NULL，err 写入中文消息与字符下标。
 */
fd_cpx_node *fd_cpx_compile(const char *src, const fd_param *params, int nparams,
                            fd_expr_error *err);

/** 求值；z 是主变量。 */
fd_complex fd_cpx_eval(const fd_cpx_node *e, fd_complex z);

void fd_cpx_free(fd_cpx_node *e);

/** 结果标签："2+3i" / "-i" / "0" / "1.5-0.5i"；超大或过小走科学计数法。 */
void fd_cpx_label(fd_complex v, char *buf, size_t cap);

/** 纯文本求值（测试与提示用）：失败返回 false。 */
bool fd_cpx_eval_text(const char *src, const fd_param *params, int nparams,
                      fd_complex z, fd_complex *out, fd_expr_error *err);

#ifdef __cplusplus
}
#endif

#endif /* FD_CMATH_H */
