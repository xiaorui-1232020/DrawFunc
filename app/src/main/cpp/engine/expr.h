#ifndef FD_EXPR_H
#define FD_EXPR_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_EXPR_ERR_CAP 192
#define FD_MAX_VARS 4

typedef struct {
    char message[FD_EXPR_ERR_CAP];
    int position; /**< 0-based character index; -1 if unknown */
} fd_expr_error;

typedef struct fd_expr fd_expr;

typedef struct {
    const char *name;
    double value;
} fd_param;

/**
 * 编译表达式。
 *
 * @param vars     采样变量名（按求值实参顺序）
 * @param degrees  true = DEG（三角函数按度理解，编译期折进语法树）
 * @param params   可绑定参数 a/b/c/d 的取值（缺省 1.0）
 * @param err      失败时写入中文报错和字符下标；可为 NULL
 */
fd_expr *fd_expr_compile(const char *source,
                         const char *const *vars, int nvars,
                         bool degrees,
                         const fd_param *params, int nparams,
                         fd_expr_error *err);

/** 同 fd_expr_compile，但把最外层 `左边=右边` 移项成 `(左边)-(右边)`。 */
fd_expr *fd_expr_compile_equation(const char *source,
                                  const char *const *vars, int nvars,
                                  bool degrees,
                                  const fd_param *params, int nparams,
                                  fd_expr_error *err);

double fd_expr_eval(const fd_expr *e, const double *args);

int fd_expr_var_count(const fd_expr *e);
bool fd_expr_is_degrees(const fd_expr *e);
void fd_expr_free(fd_expr *e);

/** 找到不在括号里的第一个 '='，返回下标；找不到返回 -1。 */
int fd_expr_top_level_equals(const char *s);

/** 剥掉 `y=` / `f(x)=` 前缀，返回 buf（buf 至少 cap 字节）。 */
const char *fd_expr_strip_explicit_prefix(const char *src, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FD_EXPR_H */
