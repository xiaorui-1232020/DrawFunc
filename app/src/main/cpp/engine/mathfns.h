#ifndef FD_MATHFNS_H
#define FD_MATHFNS_H

/*
 * 内置函数 / 常量表（expr.c 的内部依赖，不属于对外 API）。
 *
 * 这张表逐项照搬 Kotlin 的 MathFns.kt：函数名、参数个数、常量名、
 * DEG/RAD 归类、Γ/erf/阶乘/取模/n 次根的数值语义都保持一致。
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_MATH_PI 3.14159265358979323846
#define FD_MATH_E 2.71828182845904523536
#define FD_MATH_PHI 1.6180339887498949

/** 度 → 弧度：三角函数的参数要乘它。 */
#define FD_DEG_TO_RAD (FD_MATH_PI / 180.0)
/** 弧度 → 度：反三角函数的结果要乘它。 */
#define FD_RAD_TO_DEG (180.0 / FD_MATH_PI)

typedef enum {
    FD_FN_SIN = 0,
    FD_FN_COS,
    FD_FN_TAN,
    FD_FN_COT,
    FD_FN_SEC,
    FD_FN_CSC,
    FD_FN_ASIN,
    FD_FN_ACOS,
    FD_FN_ATAN,
    FD_FN_ATAN2,
    FD_FN_SINH,
    FD_FN_COSH,
    FD_FN_TANH,
    FD_FN_COTH,
    FD_FN_ASINH,
    FD_FN_ACOSH,
    FD_FN_ATANH,
    FD_FN_EXP,
    FD_FN_LN,
    FD_FN_LG,
    FD_FN_LB,
    FD_FN_LOG,
    FD_FN_SQRT,
    FD_FN_CBRT,
    FD_FN_ABS,
    FD_FN_SIGN,
    FD_FN_FLOOR,
    FD_FN_CEIL,
    FD_FN_ROUND,
    FD_FN_TRUNC,
    FD_FN_FRAC,
    FD_FN_GAMMA,
    FD_FN_ERF,
    FD_FN_SINC,
    FD_FN_DEG,
    FD_FN_RAD,
    FD_FN_POW,
    FD_FN_MOD,
    FD_FN_ROOT,
    FD_FN_HYPOT,
    FD_FN_MIN,
    FD_FN_MAX,
    FD_FN_CLAMP,
    FD_FN_COUNT
} fd_math_fn;

/** 函数名（大小写不敏感，语义同 Kotlin 的 arityOf(name) ?: arityOf(name.lowercase())）。 */
bool fd_math_lookup(const char *name, size_t len, int *id);

/** 函数名精确匹配（大小写敏感，语义同 Kotlin 的 MathFns.isFunction）。 */
bool fd_math_lookup_exact(const char *name, size_t len, int *id);

bool fd_math_arity_of(int id, int *min_args, int *max_args);

const char *fd_math_fn_name(int id);

bool fd_math_is_forward_trig(int id);
bool fd_math_is_inverse_trig(int id);

/** 常量精确匹配（语义同 Kotlin 的 constantOf(name)）。 */
bool fd_math_constant_exact(const char *name, size_t len, double *out);

/** 常量小写匹配（语义同 Kotlin 的 constantOf(name.lowercase())）。 */
bool fd_math_constant_lower(const char *name, size_t len, double *out);

double fd_math_call(int id, const double *args, int argc);

/** 一元快速路径；语义与 fd_math_call(id, &x, 1) 完全一致。 */
double fd_math_call1(int id, double x);

double fd_math_gamma(double x);
double fd_math_erf(double x);
double fd_math_factorial(double x);
double fd_math_math_mod(double x, double y);
double fd_math_nth_root(double x, double n);
double fd_math_log2(double x);

#ifdef __cplusplus
}
#endif

#endif /* FD_MATHFNS_H */
