#include "cmath.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_ZERO ((fd_complex){0.0, 0.0})
#define C_ONE ((fd_complex){1.0, 0.0})
#define C_I ((fd_complex){0.0, 1.0})

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

/* ------------------------------------------------------------------ */
/* 四则与初等函数                                                      */
/* ------------------------------------------------------------------ */

static fd_complex c_make(double re, double im)
{
    if (re == 0.0) re = 0.0; /* -0.0 → 0.0，避免 arg/label 走上半平面以外的分支 */
    if (im == 0.0) im = 0.0;
    fd_complex v = {re, im};
    return v;
}

static fd_complex c_add(fd_complex a, fd_complex b)
{
    return c_make(a.re + b.re, a.im + b.im);
}

static fd_complex c_sub(fd_complex a, fd_complex b)
{
    return c_make(a.re - b.re, a.im - b.im);
}

static fd_complex c_mul(fd_complex a, fd_complex b)
{
    return c_make(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

/** Smith 算法：避免中间量溢出。 */
static fd_complex c_div(fd_complex a, fd_complex b)
{
    if (fabs(b.re) >= fabs(b.im)) {
        if (b.re == 0.0 && b.im == 0.0) return c_make(NAN, NAN);
        double r = b.im / b.re;
        double d = b.re + b.im * r;
        return c_make((a.re + a.im * r) / d, (a.im - a.re * r) / d);
    }
    if (b.re == 0.0 && b.im == 0.0) return c_make(NAN, NAN);
    double r = b.re / b.im;
    double d = b.re * r + b.im;
    return c_make((a.re * r + a.im) / d, (a.im * r - a.re) / d);
}

static fd_complex c_neg(fd_complex a)
{
    return c_make(-a.re, -a.im);
}

static double c_abs(fd_complex a)
{
    return hypot(a.re, a.im);
}

static fd_complex c_exp(fd_complex a)
{
    double e = exp(a.re);
    return c_make(e * cos(a.im), e * sin(a.im));
}

static fd_complex c_log(fd_complex a)
{
    if (a.re == 0.0 && a.im == 0.0) return c_make(-INFINITY, 0.0);
    return c_make(log(c_abs(a)), atan2(a.im, a.re));
}

static fd_complex c_pow(fd_complex a, fd_complex b)
{
    if (a.re == 0.0 && a.im == 0.0) {
        if (b.im == 0.0 && b.re > 0.0) return C_ZERO;
        if (b.im == 0.0 && b.re == 0.0) return C_ONE;
    }
    return c_exp(c_mul(b, c_log(a)));
}

/** 主值平方根：直接用代数式，避免 exp(0.5·log) 在负实轴上选到 -i。 */
static fd_complex c_sqrt(fd_complex a)
{
    if (a.im == 0.0) {
        if (a.re >= 0.0) return c_make(sqrt(a.re), 0.0);
        return c_make(0.0, sqrt(-a.re));
    }
    double r = hypot(a.re, a.im);
    double re = sqrt((r + a.re) * 0.5);
    double im = sqrt((r - a.re) * 0.5);
    return c_make(re, a.im < 0.0 ? -im : im);
}

static fd_complex c_sin(fd_complex a)
{
    return c_make(sin(a.re) * cosh(a.im), cos(a.re) * sinh(a.im));
}

static fd_complex c_cos(fd_complex a)
{
    return c_make(cos(a.re) * cosh(a.im), -sin(a.re) * sinh(a.im));
}

static fd_complex c_tan(fd_complex a)
{
    return c_div(c_sin(a), c_cos(a));
}

static fd_complex c_sinh(fd_complex a)
{
    return c_make(sinh(a.re) * cos(a.im), cosh(a.re) * sin(a.im));
}

static fd_complex c_cosh(fd_complex a)
{
    return c_make(cosh(a.re) * cos(a.im), sinh(a.re) * sin(a.im));
}

/* ------------------------------------------------------------------ */
/* 词法                                                                */
/* ------------------------------------------------------------------ */

typedef enum { T_NUM, T_IDENT, T_OP, T_LP, T_RP, T_COMMA, T_BAR, T_EOF } tok_type;

typedef struct {
    tok_type type;
    double num;
    char text[16];
    int pos;
} token;

typedef struct {
    const char *src;
    int p;
    token tok;
    fd_expr_error *err;
    bool failed;
} lexer;

static void fail(lexer *lx, int pos, const char *msg)
{
    if (lx->failed) return;
    lx->failed = true;
    if (lx->err) {
        lx->err->position = pos;
        snprintf(lx->err->message, sizeof(lx->err->message), "%s", msg);
    }
}

static void lex_next(lexer *lx)
{
    const char *s = lx->src;
    token *t = &lx->tok;
    memset(t, 0, sizeof(*t));

    while (isspace((unsigned char)s[lx->p])) lx->p++;
    t->pos = lx->p;

    if (s[lx->p] == '\0') {
        t->type = T_EOF;
        return;
    }

    char c = s[lx->p];
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)s[lx->p + 1]))) {
        char *end = NULL;
        t->num = strtod(s + lx->p, &end);
        t->type = T_NUM;
        lx->p = (int)(end - s);
        return;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        int n = 0;
        while ((isalnum((unsigned char)s[lx->p]) || s[lx->p] == '_') &&
               n + 1 < (int)sizeof(t->text)) {
            t->text[n++] = s[lx->p++];
        }
        t->text[n] = '\0';
        t->type = T_IDENT;
        return;
    }

    /* π / τ：UTF-8 两字节，直接当标识符 */
    if ((unsigned char)c == 0xCF && (unsigned char)s[lx->p + 1] == 0x80) {
        snprintf(t->text, sizeof(t->text), "π");
        t->type = T_IDENT;
        lx->p += 2;
        return;
    }
    if ((unsigned char)c == 0xCF && (unsigned char)s[lx->p + 1] == 0x84) {
        snprintf(t->text, sizeof(t->text), "τ");
        t->type = T_IDENT;
        lx->p += 2;
        return;
    }

    switch (c) {
        case '(': t->type = T_LP; break;
        case ')': t->type = T_RP; break;
        case ',': t->type = T_COMMA; break;
        case '|': t->type = T_BAR; break;
        case '+': case '-': case '*': case '/': case '^':
            t->type = T_OP;
            t->text[0] = c;
            break;
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "不认识的字符 '%c'", c);
            fail(lx, lx->p, msg);
            t->type = T_EOF;
            return;
        }
    }
    lx->p++;
}

/* ------------------------------------------------------------------ */
/* 语法树                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    N_CONST,
    N_Z,
    N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_NEG,
    N_FN1, N_FN2, N_ABS
} node_kind;

typedef enum {
    F_ABS, F_ARG, F_CONJ, F_RE, F_IM,
    F_EXP, F_LN, F_LOG, F_SQRT, F_SIN, F_COS, F_TAN, F_SINH, F_COSH,
    F_POW_FN, F_LOG2
} fn_id;

struct fd_cpx_node {
    node_kind kind;
    fd_complex c;
    fn_id fn;
    fd_cpx_node *a;
    fd_cpx_node *b;
};

static fd_cpx_node *node_new(node_kind k)
{
    fd_cpx_node *n = (fd_cpx_node *)calloc(1, sizeof(*n));
    if (n) n->kind = k;
    return n;
}

static fd_complex eval_node(const fd_cpx_node *n, fd_complex z)
{
    if (!n) return c_make(NAN, NAN);
    switch (n->kind) {
        case N_CONST: return n->c;
        case N_Z: return z;
        case N_ADD: return c_add(eval_node(n->a, z), eval_node(n->b, z));
        case N_SUB: return c_sub(eval_node(n->a, z), eval_node(n->b, z));
        case N_MUL: return c_mul(eval_node(n->a, z), eval_node(n->b, z));
        case N_DIV: return c_div(eval_node(n->a, z), eval_node(n->b, z));
        case N_POW: return c_pow(eval_node(n->a, z), eval_node(n->b, z));
        case N_NEG: return c_neg(eval_node(n->a, z));
        case N_ABS: return c_make(c_abs(eval_node(n->a, z)), 0.0);
        case N_FN1: {
            fd_complex v = eval_node(n->a, z);
            switch (n->fn) {
                case F_ABS: return c_make(c_abs(v), 0.0);
                case F_ARG: return c_make(atan2(v.im, v.re), 0.0);
                case F_CONJ: return c_make(v.re, -v.im);
                case F_RE: return c_make(v.re, 0.0);
                case F_IM: return c_make(v.im, 0.0);
                case F_EXP: return c_exp(v);
                case F_LN: return c_log(v);
                case F_LOG: return c_div(c_log(v), c_make(log(10.0), 0.0));
                case F_SQRT: return c_sqrt(v);
                case F_SIN: return c_sin(v);
                case F_COS: return c_cos(v);
                case F_TAN: return c_tan(v);
                case F_SINH: return c_sinh(v);
                case F_COSH: return c_cosh(v);
                default: return c_make(NAN, NAN);
            }
        }
        case N_FN2: {
            fd_complex x = eval_node(n->a, z);
            fd_complex y = eval_node(n->b, z);
            switch (n->fn) {
                case F_POW_FN: return c_pow(x, y);
                case F_LOG2: return c_div(c_log(y), c_log(x));
                default: return c_make(NAN, NAN);
            }
        }
    }
    return c_make(NAN, NAN);
}

/* ------------------------------------------------------------------ */
/* 解析                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    lexer lx;
    const fd_param *params;
    int nparams;
    int bar_depth;
    fd_cpx_node *root;
} parser;

static fd_cpx_node *parse_sum(parser *ps);

static void advance(parser *ps)
{
    lex_next(&ps->lx);
}

static bool accept_op(parser *ps, char op)
{
    if (ps->lx.tok.type == T_OP && ps->lx.tok.text[0] == op) {
        advance(ps);
        return true;
    }
    return false;
}

static bool expect(parser *ps, tok_type type, const char *what)
{
    if (ps->lx.tok.type != type) {
        char msg[64];
        snprintf(msg, sizeof(msg), "这里需要一个%s", what);
        fail(&ps->lx, ps->lx.tok.pos, msg);
        return false;
    }
    advance(ps);
    return true;
}

static int fn_lookup(const char *name, fn_id *id, int *min_args, int *max_args)
{
    struct {
        const char *name;
        fn_id id;
        int min_args, max_args;
    } table[] = {
            {"abs", F_ABS, 1, 1},
            {"arg", F_ARG, 1, 1},
            {"conj", F_CONJ, 1, 1},
            {"conjugate", F_CONJ, 1, 1},
            {"re", F_RE, 1, 1},
            {"im", F_IM, 1, 1},
            {"exp", F_EXP, 1, 1},
            {"ln", F_LN, 1, 1},
            {"log", F_LOG, 1, 2},
            {"sqrt", F_SQRT, 1, 1},
            {"sin", F_SIN, 1, 1},
            {"cos", F_COS, 1, 1},
            {"tan", F_TAN, 1, 1},
            {"sinh", F_SINH, 1, 1},
            {"cosh", F_COSH, 1, 1},
            {"pow", F_POW_FN, 2, 2},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) != 0) continue;
        *id = table[i].id;
        *min_args = table[i].min_args;
        *max_args = table[i].max_args;
        return 1;
    }
    return 0;
}

static bool const_lookup(const char *name, fd_complex *out)
{
    if (strcmp(name, "i") == 0) { *out = C_I; return true; }
    if (strcmp(name, "pi") == 0 || strcmp(name, "π") == 0) {
        *out = c_make(M_PI, 0.0);
        return true;
    }
    if (strcmp(name, "tau") == 0 || strcmp(name, "τ") == 0) {
        *out = c_make(2.0 * M_PI, 0.0);
        return true;
    }
    if (strcmp(name, "e") == 0) { *out = c_make(M_E, 0.0); return true; }
    if (strcmp(name, "inf") == 0 || strcmp(name, "infinity") == 0) {
        *out = c_make(INFINITY, 0.0);
        return true;
    }
    if (strcmp(name, "nan") == 0) { *out = c_make(NAN, NAN); return true; }
    return false;
}

static fd_cpx_node *parse_primary(parser *ps)
{
    token *t = &ps->lx.tok;

    if (t->type == T_NUM) {
        fd_cpx_node *n = node_new(N_CONST);
        n->c = c_make(t->num, 0.0);
        advance(ps);
        return n;
    }

    if (t->type == T_LP) {
        advance(ps);
        fd_cpx_node *n = parse_sum(ps);
        expect(ps, T_RP, "右括号");
        return n;
    }

    if (t->type == T_BAR) {
        advance(ps);
        ps->bar_depth++;
        fd_cpx_node *inner = parse_sum(ps);
        ps->bar_depth--;
        expect(ps, T_BAR, "绝对值结尾的 |");
        fd_cpx_node *n = node_new(N_ABS);
        n->a = inner;
        return n;
    }

    if (t->type == T_IDENT) {
        char name[16];
        snprintf(name, sizeof(name), "%s", t->text);
        int pos = t->pos;
        advance(ps);

        if (ps->lx.tok.type == T_LP) {
            fn_id id;
            int min_args, max_args;
            if (!fn_lookup(name, &id, &min_args, &max_args)) {
                char msg[96];
                snprintf(msg, sizeof(msg), "不认识的函数 '%s'", name);
                fail(&ps->lx, pos, msg);
                return NULL;
            }
            advance(ps);
            fd_cpx_node *a = parse_sum(ps);
            fd_cpx_node *b = NULL;
            if (ps->lx.tok.type == T_COMMA) {
                advance(ps);
                b = parse_sum(ps);
            }
            if (!expect(ps, T_RP, "右括号")) {
                fd_cpx_free(a);
                fd_cpx_free(b);
                return NULL;
            }
            if (b && max_args < 2) {
                char msg[96];
                snprintf(msg, sizeof(msg), "'%s' 只接受 1 个参数", name);
                fail(&ps->lx, pos, msg);
                fd_cpx_free(a);
                fd_cpx_free(b);
                return NULL;
            }
            if (!b && min_args > 1) {
                char msg[96];
                snprintf(msg, sizeof(msg), "'%s' 需要 2 个参数", name);
                fail(&ps->lx, pos, msg);
                fd_cpx_free(a);
                return NULL;
            }
            if (b && id == F_LOG) {
                fd_cpx_node *n = node_new(N_FN2);
                n->fn = F_LOG2;
                n->a = a;
                n->b = b;
                return n;
            }
            if (b) {
                fd_cpx_node *n = node_new(N_FN2);
                n->fn = id;
                n->a = a;
                n->b = b;
                return n;
            }
            fd_cpx_node *n = node_new(N_FN1);
            n->fn = id;
            n->a = a;
            return n;
        }

        if (strcmp(name, "z") == 0) return node_new(N_Z);

        fd_complex cv;
        if (const_lookup(name, &cv)) {
            fd_cpx_node *n = node_new(N_CONST);
            n->c = cv;
            return n;
        }
        for (int i = 0; i < ps->nparams; i++) {
            if (ps->params[i].name && strcmp(ps->params[i].name, name) == 0) {
                fd_cpx_node *n = node_new(N_CONST);
                n->c = c_make(ps->params[i].value, 0.0);
                return n;
            }
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "不认识的名字 '%s'", name);
        fail(&ps->lx, pos, msg);
        return NULL;
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "这里需要一个数、z 或常量");
    fail(&ps->lx, t->pos, msg);
    return NULL;
}

static fd_cpx_node *parse_power(parser *ps)
{
    fd_cpx_node *base = parse_primary(ps);
    if (!base) return NULL;
    if (accept_op(ps, '^')) {
        fd_cpx_node *rhs = parse_power(ps);
        if (!rhs) {
            fd_cpx_free(base);
            return NULL;
        }
        fd_cpx_node *n = node_new(N_POW);
        n->a = base;
        n->b = rhs;
        return n;
    }
    return base;
}

static fd_cpx_node *parse_unary(parser *ps)
{
    if (accept_op(ps, '-')) {
        fd_cpx_node *inner = parse_unary(ps);
        if (!inner) return NULL;
        fd_cpx_node *n = node_new(N_NEG);
        n->a = inner;
        return n;
    }
    if (accept_op(ps, '+')) return parse_unary(ps);
    return parse_power(ps);
}

static bool starts_operand(parser *ps)
{
    switch (ps->lx.tok.type) {
        case T_NUM:
        case T_IDENT:
        case T_LP:
            return true;
        case T_BAR:
            return ps->bar_depth == 0;
        default:
            return false;
    }
}

static fd_cpx_node *parse_product(parser *ps)
{
    fd_cpx_node *left = parse_unary(ps);
    if (!left) return NULL;

    for (;;) {
        if (accept_op(ps, '*')) {
            fd_cpx_node *right = parse_unary(ps);
            if (!right) {
                fd_cpx_free(left);
                return NULL;
            }
            fd_cpx_node *n = node_new(N_MUL);
            n->a = left;
            n->b = right;
            left = n;
        } else if (accept_op(ps, '/')) {
            fd_cpx_node *right = parse_unary(ps);
            if (!right) {
                fd_cpx_free(left);
                return NULL;
            }
            fd_cpx_node *n = node_new(N_DIV);
            n->a = left;
            n->b = right;
            left = n;
        } else if (starts_operand(ps)) {
            fd_cpx_node *right = parse_unary(ps);
            if (!right) {
                fd_cpx_free(left);
                return NULL;
            }
            fd_cpx_node *n = node_new(N_MUL);
            n->a = left;
            n->b = right;
            left = n;
        } else {
            break;
        }
    }
    return left;
}

static fd_cpx_node *parse_sum(parser *ps)
{
    fd_cpx_node *left = parse_product(ps);
    if (!left) return NULL;

    for (;;) {
        if (accept_op(ps, '+')) {
            fd_cpx_node *right = parse_product(ps);
            if (!right) {
                fd_cpx_free(left);
                return NULL;
            }
            fd_cpx_node *n = node_new(N_ADD);
            n->a = left;
            n->b = right;
            left = n;
        } else if (accept_op(ps, '-')) {
            fd_cpx_node *right = parse_product(ps);
            if (!right) {
                fd_cpx_free(left);
                return NULL;
            }
            fd_cpx_node *n = node_new(N_SUB);
            n->a = left;
            n->b = right;
            left = n;
        } else {
            break;
        }
    }
    return left;
}

void fd_cpx_free(fd_cpx_node *e)
{
    if (!e) return;
    fd_cpx_free(e->a);
    fd_cpx_free(e->b);
    free(e);
}

fd_cpx_node *fd_cpx_compile(const char *src, const fd_param *params, int nparams,
                            fd_expr_error *err)
{
    if (err) {
        err->message[0] = '\0';
        err->position = -1;
    }
    if (!src) {
        if (err) snprintf(err->message, sizeof(err->message), "表达式为空");
        return NULL;
    }

    parser ps;
    memset(&ps, 0, sizeof(ps));
    ps.lx.src = src;
    ps.lx.err = err;
    ps.params = params;
    ps.nparams = nparams;

    advance(&ps);
    fd_cpx_node *root = parse_sum(&ps);
    if (!root || ps.lx.failed) {
        fd_cpx_free(root);
        if (err && err->message[0] == '\0') {
            snprintf(err->message, sizeof(err->message), "表达式无效");
            err->position = ps.lx.tok.pos;
        }
        return NULL;
    }
    if (ps.lx.tok.type != T_EOF) {
        if (err) {
            snprintf(err->message, sizeof(err->message), "表达式结尾多出内容");
            err->position = ps.lx.tok.pos;
        }
        fd_cpx_free(root);
        return NULL;
    }
    return root;
}

fd_complex fd_cpx_eval(const fd_cpx_node *e, fd_complex z)
{
    return eval_node(e, z);
}

bool fd_cpx_eval_text(const char *src, const fd_param *params, int nparams,
                      fd_complex z, fd_complex *out, fd_expr_error *err)
{
    fd_cpx_node *e = fd_cpx_compile(src, params, nparams, err);
    if (!e) return false;
    *out = fd_cpx_eval(e, z);
    fd_cpx_free(e);
    return true;
}

/* ------------------------------------------------------------------ */
/* 标签                                                                */
/* ------------------------------------------------------------------ */

static void trim_zeros(char *s)
{
    char *dot = strchr(s, '.');
    if (!dot) return;
    char *end = s + strlen(s) - 1;
    while (end > dot && *end == '0') *end-- = '\0';
    if (end == dot) *end = '\0';
}

static void fmt_component(double v, char *out, size_t cap)
{
    if (isnan(v)) {
        snprintf(out, cap, "NaN");
        return;
    }
    if (isinf(v)) {
        snprintf(out, cap, "%s", v > 0 ? "∞" : "-∞");
        return;
    }
    if (v == 0.0) {
        snprintf(out, cap, "0");
        return;
    }

    double rounded = round(v * 100.0) / 100.0;
    if (fabs(v) >= 1.0e7 || rounded == 0.0) {
        snprintf(out, cap, "%.2e", v);
        char *e = strchr(out, 'e');
        if (e) {
            char mant[32];
            size_t n = (size_t)(e - out);
            if (n >= sizeof(mant)) n = sizeof(mant) - 1;
            memcpy(mant, out, n);
            mant[n] = '\0';
            trim_zeros(mant);
            int exp = atoi(e + 1);
            snprintf(out, cap, "%se%d", mant, exp);
        }
        return;
    }

    snprintf(out, cap, "%.2f", rounded);
    trim_zeros(out);
    if (strcmp(out, "-0") == 0) snprintf(out, cap, "0");
}

void fd_cpx_label(fd_complex v, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    if (isnan(v.re) || isnan(v.im)) {
        snprintf(buf, cap, "NaN");
        return;
    }
    if (v.im == 0.0) {
        fmt_component(v.re, buf, cap);
        return;
    }

    char mag[40];
    fmt_component(fabs(v.im), mag, sizeof(mag));
    char body[48];
    if (strcmp(mag, "1") == 0) snprintf(body, sizeof(body), "i");
    else snprintf(body, sizeof(body), "%si", mag);

    if (v.re == 0.0) {
        snprintf(buf, cap, "%s%s", v.im < 0.0 ? "-" : "", body);
        return;
    }
    char re[40];
    fmt_component(v.re, re, sizeof(re));
    snprintf(buf, cap, "%s%s%s", re, v.im < 0.0 ? "-" : "+", body);
}
