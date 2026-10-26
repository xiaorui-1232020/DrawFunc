/*
 * 内置函数 / 常量表 —— MathFns.kt 的 C11 移植。
 *
 * 数值语义逐项对齐：
 *   log(x)   = 常用对数；log(b,x) = 以 b 为底
 *   ln/lg/lb = 自然对数 / 以 10 为底 / 以 2 为底
 *   mod      = 数学取模（符号跟随除数）
 *   root     = n 次根（n 为奇数时允许负数）
 *   gamma    = Lanczos g=7, n=9（同一组系数）
 *   erf      = Abramowitz & Stegun 7.1.26（同一近似）
 *   factorial= 非负整数精确连乘，其余走 Γ(n+1)
 */

#include "mathfns.h"

#include <math.h>
#include <string.h>

typedef struct {
    const char *name;
    int id;
} fd_name_id;

static const fd_name_id FD_FN_TABLE[] = {
    {"sin", FD_FN_SIN},
    {"cos", FD_FN_COS},
    {"tan", FD_FN_TAN},
    {"cot", FD_FN_COT},
    {"sec", FD_FN_SEC},
    {"csc", FD_FN_CSC},
    {"asin", FD_FN_ASIN},
    {"acos", FD_FN_ACOS},
    {"atan", FD_FN_ATAN},
    {"atan2", FD_FN_ATAN2},
    {"sinh", FD_FN_SINH},
    {"cosh", FD_FN_COSH},
    {"tanh", FD_FN_TANH},
    {"coth", FD_FN_COTH},
    {"asinh", FD_FN_ASINH},
    {"acosh", FD_FN_ACOSH},
    {"atanh", FD_FN_ATANH},
    {"exp", FD_FN_EXP},
    {"ln", FD_FN_LN},
    {"lg", FD_FN_LG},
    {"lb", FD_FN_LB},
    {"log", FD_FN_LOG},
    {"sqrt", FD_FN_SQRT},
    {"cbrt", FD_FN_CBRT},
    {"abs", FD_FN_ABS},
    {"sign", FD_FN_SIGN},
    {"floor", FD_FN_FLOOR},
    {"ceil", FD_FN_CEIL},
    {"round", FD_FN_ROUND},
    {"trunc", FD_FN_TRUNC},
    {"frac", FD_FN_FRAC},
    {"gamma", FD_FN_GAMMA},
    {"erf", FD_FN_ERF},
    {"sinc", FD_FN_SINC},
    {"deg", FD_FN_DEG},
    {"rad", FD_FN_RAD},
    {"pow", FD_FN_POW},
    {"mod", FD_FN_MOD},
    {"root", FD_FN_ROOT},
    {"hypot", FD_FN_HYPOT},
    {"min", FD_FN_MIN},
    {"max", FD_FN_MAX},
    {"clamp", FD_FN_CLAMP},
};

#define FD_FN_TABLE_LEN (sizeof(FD_FN_TABLE) / sizeof(FD_FN_TABLE[0]))

typedef struct {
    const char *name;
    double value;
} fd_const;

#define FD_CONST_PI 3.14159265358979323846
#define FD_CONST_TAU (2.0 * FD_CONST_PI)
#define FD_CONST_E 2.71828182845904523536
#define FD_CONST_PHI 1.6180339887498949

static const fd_const FD_CONST_TABLE[] = {
    {"pi", FD_CONST_PI},
    {"π", FD_CONST_PI},
    {"tau", FD_CONST_TAU},
    {"τ", FD_CONST_TAU},
    {"e", FD_CONST_E},
    {"phi", FD_CONST_PHI},
    {"φ", FD_CONST_PHI},
    {"inf", INFINITY},
    {"infinity", INFINITY},
    {"nan", NAN},
};

#define FD_CONST_TABLE_LEN (sizeof(FD_CONST_TABLE) / sizeof(FD_CONST_TABLE[0]))

static bool name_eq_exact(const char *name, size_t len, const char *lit)
{
    size_t n = strlen(lit);
    return len == n && memcmp(name, lit, n) == 0;
}

/*
 * 把名字规范化成小写：
 *  - ASCII A-Z → a-z
 *  - Unicode 大写 Π/Τ/Φ → 对应的小写希腊字母（Kotlin 的 lowercase() 也会这么做）
 *  - 其余字节原样复制
 *
 * 返回规范化后的字节数；out 容量不够时返回 (size_t)-1。
 */
static size_t normalize_lower(const char *in, size_t len, char *out, size_t cap)
{
    size_t i = 0;
    size_t d = 0;
    while (i < len) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            if (d + 1 >= cap) return (size_t)-1;
            out[d++] = (char)c;
            i++;
        } else if (i + 1 < len && c == 0xCE &&
                   ((unsigned char)in[i + 1] == 0xA0 || /* Π */
                    (unsigned char)in[i + 1] == 0xA4 || /* Τ */
                    (unsigned char)in[i + 1] == 0xA6)) { /* Φ */
            unsigned char second = (unsigned char)in[i + 1];
            unsigned char lower = (unsigned char)(second + 0x20); /* CF 80/84/86 */
            if (d + 2 >= cap) return (size_t)-1;
            out[d++] = (char)0xCF;
            out[d++] = (char)lower;
            i += 2;
        } else {
            if (d + 1 >= cap) return (size_t)-1;
            out[d++] = in[i];
            i++;
        }
    }
    return d;
}

static bool table_lookup(const fd_name_id *table, size_t count,
                         const char *name, size_t len, int *id)
{
    for (size_t k = 0; k < count; k++) {
        if (name_eq_exact(name, len, table[k].name)) {
            if (id) *id = table[k].id;
            return true;
        }
    }
    return false;
}

static bool table_lookup_lower(const fd_name_id *table, size_t count,
                               const char *name, size_t len, int *id)
{
    char buf[64];
    size_t n = normalize_lower(name, len, buf, sizeof(buf));
    if (n == (size_t)-1) return false;
    return table_lookup(table, count, buf, n, id);
}

bool fd_math_lookup_exact(const char *name, size_t len, int *id)
{
    if (!name) return false;
    return table_lookup(FD_FN_TABLE, FD_FN_TABLE_LEN, name, len, id);
}

bool fd_math_lookup(const char *name, size_t len, int *id)
{
    if (!name) return false;
    if (table_lookup(FD_FN_TABLE, FD_FN_TABLE_LEN, name, len, id)) return true;
    return table_lookup_lower(FD_FN_TABLE, FD_FN_TABLE_LEN, name, len, id);
}

bool fd_math_arity_of(int id, int *min_args, int *max_args)
{
    int lo = 1;
    int hi = 1;

    switch (id) {
    case FD_FN_LOG:
        lo = 1;
        hi = 2;
        break;
    case FD_FN_ATAN2:
    case FD_FN_POW:
    case FD_FN_MOD:
    case FD_FN_ROOT:
        lo = 2;
        hi = 2;
        break;
    case FD_FN_HYPOT:
    case FD_FN_MIN:
    case FD_FN_MAX:
        lo = 1;
        hi = 8;
        break;
    case FD_FN_CLAMP:
        lo = 3;
        hi = 3;
        break;
    default:
        if (id < 0 || id >= FD_FN_COUNT) return false;
        break;
    }

    if (min_args) *min_args = lo;
    if (max_args) *max_args = hi;
    return true;
}

const char *fd_math_fn_name(int id)
{
    for (size_t k = 0; k < FD_FN_TABLE_LEN; k++) {
        if (FD_FN_TABLE[k].id == id) return FD_FN_TABLE[k].name;
    }
    return "?";
}

bool fd_math_is_forward_trig(int id)
{
    switch (id) {
    case FD_FN_SIN:
    case FD_FN_COS:
    case FD_FN_TAN:
    case FD_FN_COT:
    case FD_FN_SEC:
    case FD_FN_CSC:
        return true;
    default:
        return false;
    }
}

bool fd_math_is_inverse_trig(int id)
{
    switch (id) {
    case FD_FN_ASIN:
    case FD_FN_ACOS:
    case FD_FN_ATAN:
    case FD_FN_ATAN2:
        return true;
    default:
        return false;
    }
}

static bool const_lookup(const char *name, size_t len, double *out,
                         bool ignore_case)
{
    char buf[32];
    const char *key = name;
    size_t key_len = len;
    size_t i;

    if (!name) return false;

    if (ignore_case) {
        size_t n = normalize_lower(name, len, buf, sizeof(buf));
        if (n == (size_t)-1) return false;
        key = buf;
        key_len = n;
    }

    for (i = 0; i < FD_CONST_TABLE_LEN; i++) {
        if (name_eq_exact(key, key_len, FD_CONST_TABLE[i].name)) {
            if (out) *out = FD_CONST_TABLE[i].value;
            return true;
        }
    }
    return false;
}

bool fd_math_constant_exact(const char *name, size_t len, double *out)
{
    return const_lookup(name, len, out, false);
}

bool fd_math_constant_lower(const char *name, size_t len, double *out)
{
    return const_lookup(name, len, out, true);
}

/* ---- 数值实现 ---- */

static double fd_sign(double x)
{
    if (isnan(x)) return x;
    if (x > 0.0) return 1.0;
    if (x < 0.0) return -1.0;
    return x; /* ±0 原样返回，和 kotlin.math.sign 一致 */
}

/* Lanczos 系数，g=7、n=9 —— 与 Kotlin 版本同一组数。 */
static const double FD_LANCZOS_G[9] = {
    0.99999999999980993,
    676.5203681218851,
    -1259.1392167224028,
    771.32342877765313,
    -176.61502916214059,
    12.507343278686905,
    -0.13857109526572012,
    9.9843695780195716e-6,
    1.5056327351493116e-7,
};

double fd_math_gamma(double x)
{
    if (isnan(x)) return NAN;

    if (x < 0.5) {
        /* 反射公式，避开负半轴上的极点 */
        double s = sin(FD_MATH_PI * x);
        if (s == 0.0) return NAN;
        return FD_MATH_PI / (s * fd_math_gamma(1.0 - x));
    }

    {
        double z = x - 1.0;
        double acc = FD_LANCZOS_G[0];
        double t;
        int i;

        for (i = 1; i < 9; i++) acc += FD_LANCZOS_G[i] / (z + (double)i);
        t = z + 7.5;
        return sqrt(2.0 * FD_MATH_PI) * pow(t, z + 0.5) * exp(-t) * acc;
    }
}

/* Abramowitz & Stegun 7.1.26，最大绝对误差约 1.5e-7。 */
double fd_math_erf(double x)
{
    double ax, t, y;

    if (isnan(x)) return NAN;
    if (x == 0.0) return 0.0;

    ax = fabs(x);
    t = 1.0 / (1.0 + 0.3275911 * ax);
    y = 1.0 -
        (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t -
          0.284496736) * t + 0.254829592) * t * exp(-ax * ax);
    return (x < 0.0) ? -y : y;
}

double fd_math_factorial(double x)
{
    if (x < 0.0 && x == floor(x)) return NAN;

    if (x == floor(x) && x <= 170.0) {
        double r = 1.0;
        int k = 2;
        int n = (int)x;
        while (k <= n) {
            r *= (double)k;
            k++;
        }
        return r;
    }

    return fd_math_gamma(x + 1.0);
}

/* 数学意义上的取模：结果符号跟随除数（-1 mod 3 = 2）。 */
double fd_math_math_mod(double x, double y)
{
    if (y == 0.0) return NAN;
    return x - y * floor(x / y);
}

/* x 的 n 次根；n 为奇数时允许负数（cbrt(-8) = -2）。 */
double fd_math_nth_root(double x, double n)
{
    if (n == 0.0) return NAN;
    if (x < 0.0 && fabs(fmod(n, 2.0)) == 1.0) {
        return -pow(-x, 1.0 / n);
    }
    return pow(x, 1.0 / n);
}

double fd_math_log2(double x)
{
    return log(x) / log(2.0);
}

static double fd_min_args(const double *a, int argc)
{
    double m = a[0];
    int i;
    for (i = 1; i < argc; i++) {
        double v = a[i];
        if (isnan(m) || isnan(v)) return NAN; /* Kotlin 的 min() 遇 NaN 返回 NaN */
        if (v < m) m = v;
    }
    if (isnan(m)) return NAN;
    return m;
}

static double fd_max_args(const double *a, int argc)
{
    double m = a[0];
    int i;
    for (i = 1; i < argc; i++) {
        double v = a[i];
        if (isnan(m) || isnan(v)) return NAN;
        if (v > m) m = v;
    }
    if (isnan(m)) return NAN;
    return m;
}

double fd_math_call(int id, const double *a, int argc)
{
    if (!a || argc <= 0) return NAN;

    switch (id) {
    case FD_FN_SIN:
        return sin(a[0]);
    case FD_FN_COS:
        return cos(a[0]);
    case FD_FN_TAN:
        return tan(a[0]);
    case FD_FN_COT:
        return 1.0 / tan(a[0]);
    case FD_FN_SEC:
        return 1.0 / cos(a[0]);
    case FD_FN_CSC:
        return 1.0 / sin(a[0]);
    case FD_FN_ASIN:
        return asin(a[0]);
    case FD_FN_ACOS:
        return acos(a[0]);
    case FD_FN_ATAN:
        return atan(a[0]);
    case FD_FN_ATAN2:
        return (argc >= 2) ? atan2(a[0], a[1]) : NAN;
    case FD_FN_SINH:
        return sinh(a[0]);
    case FD_FN_COSH:
        return cosh(a[0]);
    case FD_FN_TANH:
        return tanh(a[0]);
    case FD_FN_COTH:
        return 1.0 / tanh(a[0]);
    case FD_FN_ASINH:
        return asinh(a[0]);
    case FD_FN_ACOSH:
        return acosh(a[0]);
    case FD_FN_ATANH:
        return atanh(a[0]);
    case FD_FN_EXP:
        return exp(a[0]);
    case FD_FN_LN:
        return log(a[0]);
    case FD_FN_LG:
        return log10(a[0]);
    case FD_FN_LB:
        return fd_math_log2(a[0]);
    case FD_FN_LOG:
        return (argc == 1) ? log10(a[0]) : log(a[1]) / log(a[0]);
    case FD_FN_SQRT:
        return sqrt(a[0]);
    case FD_FN_CBRT:
        return cbrt(a[0]);
    case FD_FN_ABS:
        return fabs(a[0]);
    case FD_FN_SIGN:
        return fd_sign(a[0]);
    case FD_FN_FLOOR:
        return floor(a[0]);
    case FD_FN_CEIL:
        return ceil(a[0]);
    case FD_FN_ROUND:
        /* kotlin.math.round 是 Math.rint：四舍六入五成双（ties to even）。 */
        return rint(a[0]);
    case FD_FN_TRUNC:
        return (a[0] < 0.0) ? ceil(a[0]) : floor(a[0]);
    case FD_FN_FRAC:
        return a[0] - floor(a[0]);
    case FD_FN_GAMMA:
        return fd_math_gamma(a[0]);
    case FD_FN_ERF:
        return fd_math_erf(a[0]);
    case FD_FN_SINC:
        return (a[0] == 0.0) ? 1.0 : sin(a[0]) / a[0];
    case FD_FN_DEG:
        return a[0] * 180.0 / FD_MATH_PI;
    case FD_FN_RAD:
        return a[0] * FD_MATH_PI / 180.0;
    case FD_FN_POW:
        return (argc >= 2) ? pow(a[0], a[1]) : NAN;
    case FD_FN_MOD:
        return (argc >= 2) ? fd_math_math_mod(a[0], a[1]) : NAN;
    case FD_FN_ROOT:
        return (argc >= 2) ? fd_math_nth_root(a[0], a[1]) : NAN;
    case FD_FN_HYPOT: {
        double s = 0.0;
        int i;
        for (i = 0; i < argc; i++) s = hypot(s, a[i]);
        return s;
    }
    case FD_FN_MIN:
        return fd_min_args(a, argc);
    case FD_FN_MAX:
        return fd_max_args(a, argc);
    case FD_FN_CLAMP:
        if (argc < 3) return NAN;
        if (a[0] < a[1]) return a[1];
        if (a[0] > a[2]) return a[2];
        return a[0];
    default:
        return NAN;
    }
}

double fd_math_call1(int id, double x)
{
    return fd_math_call(id, &x, 1);
}
