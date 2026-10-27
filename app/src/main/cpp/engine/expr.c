/*
 * 表达式引擎（词法 + 递归下降解析 + AST 求值 + 释放）—— Expression.kt / Lexer.kt 的 C11 移植。
 *
 * 设计约束：
 *  - 严格 C11，只依赖 libc/libm，不引用 LVGL；
 *  - 不用异常 / longjmp 做正常控制流，所有失败路径返回 NULL 并填 fd_expr_error；
 *  - 编译期把 DEG 换算、可绑定参数 a/b/c/d 全部折进 AST，求值期不读任何全局状态，
 *    因此同一个 fd_expr 可以被多线程并发求值。
 *
 * 明确不支持：用户自定义函数调用（Kotlin 的 CallableFn / Node.UserCall 那条链路）。
 * 本实现没有可调用函数表，遇到 `a(x)` 时按加这个功能之前的旧行为处理：
 * `a` 是可绑定参数（缺省 1.0），再与括号里的 `x` 隐式相乘 —— 即 `a(x)` = `a * (x)`。
 * 只有内置函数名（sin、log、hypot……）后跟括号时才按函数调用解析。
 */

#include "expr.h"
#include "mathfns.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== */
/* UTF-8 辅助                                                            */
/* ===================================================================== */

/** 解码一个 UTF-8 码点，返回消耗的字节数；非法序列按 U+FFFD、消耗 1 字节。 */
static size_t fd_utf8_decode(const char *s, size_t len, size_t i, uint32_t *cp)
{
    unsigned char c = (unsigned char)s[i];

    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < len &&
        ((unsigned char)s[i + 1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)((unsigned char)s[i + 1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < len &&
        ((unsigned char)s[i + 1] & 0xC0) == 0x80 &&
        ((unsigned char)s[i + 2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(c & 0x0F) << 12) |
              ((uint32_t)((unsigned char)s[i + 1] & 0x3F) << 6) |
              (uint32_t)((unsigned char)s[i + 2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < len &&
        ((unsigned char)s[i + 1] & 0xC0) == 0x80 &&
        ((unsigned char)s[i + 2] & 0xC0) == 0x80 &&
        ((unsigned char)s[i + 3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(c & 0x07) << 18) |
              ((uint32_t)((unsigned char)s[i + 1] & 0x3F) << 12) |
              ((uint32_t)((unsigned char)s[i + 2] & 0x3F) << 6) |
              (uint32_t)((unsigned char)s[i + 3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

static size_t fd_utf8_seq_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/** 拷贝一段 UTF-8 文本到定长缓冲，保证不切断多字节字符。 */
static void fd_utf8_copy(char *dst, size_t cap, const char *src)
{
    size_t srclen = strlen(src);
    size_t i = 0;
    size_t d = 0;

    if (cap == 0) return;

    while (i < srclen && d + 1 < cap) {
        size_t n = fd_utf8_seq_len((unsigned char)src[i]);
        if (i + n > srclen) break;
        if (d + n > cap - 1) break;
        memcpy(dst + d, src + i, n);
        d += n;
        i += n;
    }
    dst[d] = '\0';
}

static bool fd_is_space_cp(uint32_t c)
{
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
        return true;
    }
    /* 对齐 Java Character.isWhitespace（Kotlin isBlank 的底层实现）：
     * 不含 NBSP(00A0)、NARROW NBSP(202F)、FIGURE SPACE(2007)。 */
    if (c == 0x1C || c == 0x1D || c == 0x1E || c == 0x1F) return true;
    if (c == 0x1680) return true;
    if (c >= 0x2000 && c <= 0x2006) return true;
    if (c >= 0x2008 && c <= 0x200A) return true;
    return c == 0x2028 || c == 0x2029 || c == 0x205F || c == 0x3000;
}

static bool fd_is_blank(const char *s)
{
    size_t len;
    size_t i = 0;

    if (!s) return true;

    len = strlen(s);
    while (i < len) {
        uint32_t cp;
        size_t adv = fd_utf8_decode(s, len, i, &cp);
        if (!fd_is_space_cp(cp)) return false;
        i += adv;
    }
    return true;
}

/**
 * Kotlin 的 Char.isLetter() 子集：
 *  - ASCII 字母 / 下划线；
 *  - 非 ASCII 中，排除数学符号、标点、箭头、图形符号等明显的非字母块。
 * 这样 π/θ/τ/φ 以及中文字符都会成为 IDENT（Kotlin 里它们也是字母），
 * 而 × · ⋅ ÷ − √ ∛ ∞ 走符号表。
 */
static bool fd_is_ident_start(uint32_t c)
{
    if (c == '_') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c < 0x80) return false;

    switch (c) {
    case 0x00D7: /* × */
    case 0x00B7: /* · */
    case 0x00F7: /* ÷ */
    case 0x22C5: /* ⋅ */
    case 0x2212: /* − */
    case 0x221A: /* √ */
    case 0x221B: /* ∛ */
    case 0x221E: /* ∞ */
    case 0xFFFD:
        return false;
    default:
        break;
    }

    if ((c >= 0x00A0 && c <= 0x00BF) ||
        (c >= 0x2000 && c <= 0x206F) ||
        (c >= 0x2070 && c <= 0x209F) ||
        (c >= 0x20A0 && c <= 0x20CF) ||
        (c >= 0x2190 && c <= 0x23FF) ||
        (c >= 0x2500 && c <= 0x27BF) ||
        (c >= 0x3000 && c <= 0x303F) ||
        (c >= 0xFE00 && c <= 0xFE0F) ||
        (c >= 0xFE30 && c <= 0xFE4F) ||
        (c >= 0xFF01 && c <= 0xFF20) ||
        (c >= 0xFF3B && c <= 0xFF40) ||
        (c >= 0xFF5B && c <= 0xFF65)) {
        return false;
    }
    return true;
}

static bool fd_is_ident_part(uint32_t c)
{
    if (c >= '0' && c <= '9') return true;
    return fd_is_ident_start(c);
}

/* ===================================================================== */
/* 报错                                                                  */
/* ===================================================================== */

static void fd_error_setv(fd_expr_error *err, int pos, const char *fmt, va_list ap)
{
    char tmp[512];

    if (!err) return;
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    fd_utf8_copy(err->message, sizeof(err->message), tmp);
    err->position = pos;
}

static void fd_error_set(fd_expr_error *err, int pos, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fd_error_setv(err, pos, fmt, ap);
    va_end(ap);
}

static void fd_error_clear(fd_expr_error *err)
{
    if (err) {
        err->message[0] = '\0';
        err->position = -1;
    }
}

/* ===================================================================== */
/* 词法                                                                  */
/* ===================================================================== */

typedef enum {
    TOK_NUMBER,
    TOK_IDENT,
    TOK_OP,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_BAR,
    TOK_EQUALS,
    TOK_EOF
} fd_token_type;

typedef struct {
    fd_token_type type;
    const char *text; /* 指向源串或静态字面量，不保证 NUL 结尾 */
    int text_len;     /* UTF-8 字节数 */
    int pos;          /* 字符下标（码点，不是字节） */
    double value;
} fd_token;

typedef struct {
    const char *src;
    size_t len;
    size_t byte;
    int cp;
    fd_token *toks;
    int ntok;
    int cap;
    bool failed;
    fd_expr_error *err;
} fd_lexer;

static uint32_t fd_lex_peek(const fd_lexer *lx)
{
    uint32_t cp = 0;
    if (lx->byte >= lx->len) return 0;
    fd_utf8_decode(lx->src, lx->len, lx->byte, &cp);
    return cp;
}

static void fd_lex_advance(fd_lexer *lx)
{
    uint32_t cp;
    if (lx->byte >= lx->len) return;
    lx->byte += fd_utf8_decode(lx->src, lx->len, lx->byte, &cp);
    lx->cp++;
}

static void fd_lex_error(fd_lexer *lx, int pos, const char *fmt, ...)
{
    va_list ap;

    if (lx->failed) return;
    lx->failed = true;

    va_start(ap, fmt);
    fd_error_setv(lx->err, pos, fmt, ap);
    va_end(ap);
}

static bool fd_lex_push(fd_lexer *lx, fd_token_type type, const char *text,
                        int text_len, int pos, double value)
{
    if (lx->ntok >= lx->cap) {
        int new_cap = (lx->cap == 0) ? 32 : lx->cap * 2;
        fd_token *grown = (fd_token *)realloc(lx->toks, (size_t)new_cap * sizeof(fd_token));
        if (!grown) {
            fd_lex_error(lx, 0, "内存不够");
            return false;
        }
        lx->toks = grown;
        lx->cap = new_cap;
    }

    lx->toks[lx->ntok].type = type;
    lx->toks[lx->ntok].text = text;
    lx->toks[lx->ntok].text_len = text_len;
    lx->toks[lx->ntok].pos = pos;
    lx->toks[lx->ntok].value = value;
    lx->ntok++;
    return true;
}

static bool fd_lex_number(fd_lexer *lx, int start_cp)
{
    size_t start = lx->byte;
    size_t text_len;
    char *tmp;
    char *end = NULL;
    double value;

    while (fd_lex_peek(lx) >= '0' && fd_lex_peek(lx) <= '9') fd_lex_advance(lx);

    if (fd_lex_peek(lx) == '.') {
        fd_lex_advance(lx);
        while (fd_lex_peek(lx) >= '0' && fd_lex_peek(lx) <= '9') fd_lex_advance(lx);
    }

    /*
     * 指数部分只有在后面确实跟着数字时才吞掉。
     * 否则 "2e" 应该被理解成 2*e（欧拉数），而不是一个残缺的科学计数法。
     */
    if (fd_lex_peek(lx) == 'e' || fd_lex_peek(lx) == 'E') {
        size_t save_byte = lx->byte;
        int save_cp = lx->cp;
        uint32_t cp;

        fd_lex_advance(lx);
        cp = fd_lex_peek(lx);
        if (cp == '+' || cp == '-') fd_lex_advance(lx);
        if (fd_lex_peek(lx) >= '0' && fd_lex_peek(lx) <= '9') {
            while (fd_lex_peek(lx) >= '0' && fd_lex_peek(lx) <= '9') fd_lex_advance(lx);
        } else {
            lx->byte = save_byte;
            lx->cp = save_cp;
        }
    }

    text_len = lx->byte - start;
    tmp = (char *)malloc(text_len + 1);
    if (!tmp) {
        fd_lex_error(lx, start_cp, "内存不够");
        return false;
    }
    memcpy(tmp, lx->src + start, text_len);
    tmp[text_len] = '\0';

    value = strtod(tmp, &end);
    if (end != tmp + text_len) {
        fd_lex_error(lx, start_cp, "数字格式不正确：%.*s", (int)text_len, tmp);
        free(tmp);
        return false;
    }
    free(tmp);

    return fd_lex_push(lx, TOK_NUMBER, lx->src + start, (int)text_len, start_cp, value);
}

static bool fd_lex_ident(fd_lexer *lx)
{
    size_t start = lx->byte;
    int start_cp = lx->cp;

    fd_lex_advance(lx);
    while (fd_is_ident_part(fd_lex_peek(lx))) fd_lex_advance(lx);

    return fd_lex_push(lx, TOK_IDENT, lx->src + start,
                       (int)(lx->byte - start), start_cp, 0.0);
}

static bool fd_lex_symbol(fd_lexer *lx)
{
    uint32_t c = fd_lex_peek(lx);
    size_t start_byte = lx->byte;
    int start_cp = lx->cp;

    /* 双字符运算符 ** 视同 ^ */
    if (c == '*' && lx->byte + 1 < lx->len && lx->src[lx->byte + 1] == '*') {
        fd_lex_advance(lx);
        fd_lex_advance(lx);
        return fd_lex_push(lx, TOK_OP, "^", 1, start_cp, 0.0);
    }

    fd_lex_advance(lx);

    switch (c) {
    case '+':
    case '-':
        return fd_lex_push(lx, TOK_OP, (c == '+') ? "+" : "-", 1, start_cp, 0.0);
    case 0x2212: /* − U+2212 真减号 */
        return fd_lex_push(lx, TOK_OP, "-", 1, start_cp, 0.0);
    case '*':
    case 0x00D7: /* × */
    case 0x00B7: /* · */
    case 0x22C5: /* ⋅ */
        return fd_lex_push(lx, TOK_OP, "*", 1, start_cp, 0.0);
    case '/':
    case 0x00F7: /* ÷ */
        return fd_lex_push(lx, TOK_OP, "/", 1, start_cp, 0.0);
    case '^':
        return fd_lex_push(lx, TOK_OP, "^", 1, start_cp, 0.0);
    case '%':
        return fd_lex_push(lx, TOK_OP, "%", 1, start_cp, 0.0);
    case '!':
        return fd_lex_push(lx, TOK_OP, "!", 1, start_cp, 0.0);
    case '(':
    case '[':
        return fd_lex_push(lx, TOK_LPAREN, "(", 1, start_cp, 0.0);
    case ')':
    case ']':
        return fd_lex_push(lx, TOK_RPAREN, ")", 1, start_cp, 0.0);
    case ',':
    case ';':
        return fd_lex_push(lx, TOK_COMMA, ",", 1, start_cp, 0.0);
    case '|':
        return fd_lex_push(lx, TOK_BAR, "|", 1, start_cp, 0.0);
    case '=':
        return fd_lex_push(lx, TOK_EQUALS, "=", 1, start_cp, 0.0);
    case 0x221A: /* √ */
        return fd_lex_push(lx, TOK_IDENT, "sqrt", 4, start_cp, 0.0);
    case 0x221B: /* ∛ */
        return fd_lex_push(lx, TOK_IDENT, "cbrt", 4, start_cp, 0.0);
    case 0x221E: /* ∞ */
        return fd_lex_push(lx, TOK_IDENT, "inf", 3, start_cp, 0.0);
    default:
        break;
    }

    fd_lex_error(lx, start_cp, "看不懂这个字符：'%.*s'",
                 (int)(lx->byte - start_byte), lx->src + start_byte);
    return false;
}

static bool fd_lex_run(fd_lexer *lx)
{
    for (;;) {
        uint32_t c;

        while (lx->byte < lx->len) {
            uint32_t ws = fd_lex_peek(lx);
            if (ws != ' ' && ws != '\t' && ws != '\n' && ws != '\r') break;
            fd_lex_advance(lx);
        }
        if (lx->byte >= lx->len) break;

        c = fd_lex_peek(lx);
        if ((c >= '0' && c <= '9') ||
            (c == '.' && lx->byte + 1 < lx->len &&
             lx->src[lx->byte + 1] >= '0' && lx->src[lx->byte + 1] <= '9')) {
            if (!fd_lex_number(lx, lx->cp)) return false;
        } else if (fd_is_ident_start(c)) {
            if (!fd_lex_ident(lx)) return false;
        } else {
            if (!fd_lex_symbol(lx)) return false;
        }
    }

    return fd_lex_push(lx, TOK_EOF, "", 0, lx->cp, 0.0);
}

/* ===================================================================== */
/* 语法树                                                                */
/* ===================================================================== */

typedef enum {
    NODE_CONST,
    NODE_VAR,
    NODE_NEG,
    NODE_ADD,
    NODE_SUB,
    NODE_MUL,
    NODE_DIV,
    NODE_POW,
    NODE_MOD,
    NODE_FACT,
    NODE_CALL
} fd_node_kind;

typedef struct fd_node fd_node;

struct fd_node {
    fd_node_kind kind;
    double value; /* NODE_CONST */
    int index;    /* NODE_VAR 的变量下标 / NODE_CALL 的函数 id */
    int argc;
    fd_node **args;
};

static fd_node *fd_node_alloc(fd_node_kind kind, int argc)
{
    fd_node *n = (fd_node *)calloc(1, sizeof(fd_node));
    if (!n) return NULL;

    n->kind = kind;
    n->argc = argc;
    if (argc > 0) {
        n->args = (fd_node **)calloc((size_t)argc, sizeof(fd_node *));
        if (!n->args) {
            free(n);
            return NULL;
        }
    }
    return n;
}

static void fd_node_free(fd_node *n)
{
    int i;

    if (!n) return;
    for (i = 0; i < n->argc; i++) fd_node_free(n->args[i]);
    free(n->args);
    free(n);
}

static fd_node *fd_node_const(double value)
{
    fd_node *n = fd_node_alloc(NODE_CONST, 0);
    if (n) n->value = value;
    return n;
}

static fd_node *fd_node_var(int index)
{
    fd_node *n = fd_node_alloc(NODE_VAR, 0);
    if (n) n->index = index;
    return n;
}

/** 取走 l/r 的所有权；失败时负责释放它们。 */
static fd_node *fd_node_binary(fd_node_kind kind, fd_node *l, fd_node *r)
{
    fd_node *n = fd_node_alloc(kind, 2);
    if (!n) {
        fd_node_free(l);
        fd_node_free(r);
        return NULL;
    }
    n->args[0] = l;
    n->args[1] = r;
    return n;
}

/** 取走 child 的所有权；失败时负责释放它。 */
static fd_node *fd_node_unary(fd_node_kind kind, fd_node *child)
{
    fd_node *n = fd_node_alloc(kind, 1);
    if (!n) {
        fd_node_free(child);
        return NULL;
    }
    n->args[0] = child;
    return n;
}

/** 取走 args 数组和每个子节点的所有权；失败时负责释放它们。 */
static fd_node *fd_node_call(int fn_id, fd_node **args, int argc)
{
    fd_node *n = fd_node_alloc(NODE_CALL, argc);
    int i;

    if (!n) {
        for (i = 0; i < argc; i++) fd_node_free(args[i]);
        free(args);
        return NULL;
    }

    n->index = fn_id;
    for (i = 0; i < argc; i++) n->args[i] = args[i];
    free(args);
    return n;
}

/* ===================================================================== */
/* 递归下降解析                                                          */
/* ===================================================================== */

typedef struct {
    const fd_token *toks;
    int ntok;
    int p;
    const char *const *vars;
    int nvars;
    bool degrees;
    double params[4];
    int inside_bars;
    bool failed;
    fd_expr_error *err;
} fd_parser;

static const fd_token *fd_peek(const fd_parser *ps)
{
    return &ps->toks[ps->p];
}

static void fd_p_error(fd_parser *ps, int pos, const char *fmt, ...)
{
    va_list ap;

    if (ps->failed) return;
    ps->failed = true;

    va_start(ap, fmt);
    fd_error_setv(ps->err, pos, fmt, ap);
    va_end(ap);
}

static bool fd_match_op(fd_parser *ps, char op)
{
    const fd_token *t = fd_peek(ps);
    if (t->type == TOK_OP && t->text_len == 1 && t->text[0] == op) {
        ps->p++;
        return true;
    }
    return false;
}

static int fd_find_var(const fd_parser *ps, const char *name, int len)
{
    int i;

    for (i = 0; i < ps->nvars; i++) {
        const char *v = ps->vars[i];
        size_t vl;
        if (!v) continue;
        vl = strlen(v);
        if (vl == (size_t)len && memcmp(v, name, vl) == 0) return i;
    }
    return -1;
}

static bool fd_find_param(const fd_parser *ps, const char *name, int len, double *out)
{
    int idx;

    if (len != 1) return false;
    idx = (unsigned char)name[0] - 'a';
    if (idx < 0 || idx > 3) return false;
    if (out) *out = ps->params[idx];
    return true;
}

static void fd_append_str(char *buf, size_t cap, size_t *d, const char *s)
{
    size_t n;

    if (!s || *d >= cap - 1) return;
    n = strlen(s);
    if (n > cap - 1 - *d) n = cap - 1 - *d;
    memcpy(buf + *d, s, n);
    *d += n;
    buf[*d] = '\0';
}

static void fd_build_hint(const fd_parser *ps, const char *name, int len,
                          char *out, size_t cap)
{
    size_t last_start = 0;
    size_t i = 0;

    if (cap == 0) return;
    out[0] = '\0';

    /* 找最后一个码点的起始字节：Kotlin 的 name.dropLast(1) / name.last() */
    while (i < (size_t)len) {
        size_t n = fd_utf8_seq_len((unsigned char)name[i]);
        if (i + n > (size_t)len) n = 1;
        last_start = i;
        i += n;
    }

    if (len > 1) {
        int ignored = 0;
        if (fd_math_lookup_exact(name, last_start, &ignored)) {
            snprintf(out, cap, "函数要加括号，比如 %.*s(%.*s)",
                     (int)last_start, name, (int)((size_t)len - last_start),
                     name + last_start);
            return;
        }
    }

    {
        size_t d = 0;
        int k;

        fd_append_str(out, cap, &d, "可用的变量：");
        for (k = 0; k < ps->nvars; k++) {
            if (k > 0) fd_append_str(out, cap, &d, "、");
            fd_append_str(out, cap, &d, ps->vars[k]);
        }
        fd_append_str(out, cap, &d, "；可绑定参数：a、b、c、d");
    }
}

static fd_node *fd_parse_sum(fd_parser *ps);
static fd_node *fd_parse_product(fd_parser *ps);
static fd_node *fd_parse_unary(fd_parser *ps);
static fd_node *fd_parse_power(fd_parser *ps);
static fd_node *fd_parse_postfix(fd_parser *ps);
static fd_node *fd_parse_primary(fd_parser *ps);

static void fd_unexpected_error(fd_parser *ps, const fd_token *t)
{
    switch (t->type) {
    case TOK_RPAREN:
        fd_p_error(ps, t->pos, "多余的右括号");
        break;
    case TOK_OP:
        fd_p_error(ps, t->pos, "运算符 '%.*s' 的位置不对", t->text_len, t->text);
        break;
    case TOK_EQUALS:
        fd_p_error(ps, t->pos, "一个表达式里只能有一个等号");
        break;
    case TOK_COMMA:
        fd_p_error(ps, t->pos, "逗号只能出现在函数参数里");
        break;
    case TOK_NUMBER:
        fd_p_error(ps, t->pos, "数字 '%.*s' 前面缺少运算符", t->text_len, t->text);
        break;
    case TOK_IDENT:
        fd_p_error(ps, t->pos, "名字 '%.*s' 前面缺少运算符", t->text_len, t->text);
        break;
    default:
        fd_p_error(ps, t->pos, "表达式在这里断掉了");
        break;
    }
}

static bool fd_starts_operand(const fd_parser *ps, const fd_token *t)
{
    switch (t->type) {
    case TOK_IDENT:
    case TOK_LPAREN:
        return true;
    case TOK_BAR:
        /* 已经在 |...| 内部时，BAR 只能是闭合符 */
        return ps->inside_bars == 0;
    default:
        return false;
    }
}

/**
 * 建函数调用节点 —— DEG/RAD 换算唯一的落点。
 *
 *  * 正向三角 sin/cos/tan/cot/sec/csc —— 包参数：sin(a) → sin(a * π/180)
 *  * 反向三角 asin/acos/atan/atan2 —— 包结果：asin(a) → asin(a) * 180/π
 *  * 其它函数原样不动
 */
static fd_node *fd_make_call(fd_parser *ps, int fn_id, fd_node **args, int argc)
{
    int i;

    if (!ps->degrees) return fd_node_call(fn_id, args, argc);

    if (fd_math_is_forward_trig(fn_id)) {
        for (i = 0; i < argc; i++) {
            fd_node *c = fd_node_const(FD_DEG_TO_RAD);
            fd_node *m;

            if (!c) {
                int j;
                for (j = 0; j < argc; j++) fd_node_free(args[j]);
                free(args);
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            m = fd_node_binary(NODE_MUL, args[i], c);
            if (!m) {
                /* fd_node_binary 失败时已经释放了 args[i] 和 c */
                int j;
                for (j = 0; j < argc; j++) {
                    if (j != i) fd_node_free(args[j]);
                }
                free(args);
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            args[i] = m;
        }
        return fd_node_call(fn_id, args, argc);
    }

    if (fd_math_is_inverse_trig(fn_id)) {
        fd_node *call = fd_node_call(fn_id, args, argc);
        fd_node *c;
        fd_node *m;

        if (!call) {
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
        c = fd_node_const(FD_RAD_TO_DEG);
        if (!c) {
            fd_node_free(call);
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
        m = fd_node_binary(NODE_MUL, call, c);
        if (!m) {
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
        return m;
    }

    return fd_node_call(fn_id, args, argc);
}

static fd_node *fd_parse_call_args(fd_parser *ps, int fn_id, int min_args,
                                   int max_args, int name_pos)
{
    fd_node **args = NULL;
    int argc = 0;
    int cap = 0;
    char arity_desc[24];

    ps->p++; /* 吃掉 '(' */

    if (fd_peek(ps)->type == TOK_RPAREN) {
        fd_p_error(ps, name_pos, "函数 %s 缺少参数", fd_math_fn_name(fn_id));
        return NULL;
    }

    for (;;) {
        fd_node *arg = fd_parse_sum(ps);
        const fd_token *t;

        if (!arg) {
            int i;
            for (i = 0; i < argc; i++) fd_node_free(args[i]);
            free(args);
            return NULL;
        }

        if (argc >= cap) {
            int new_cap = (cap == 0) ? 4 : cap * 2;
            fd_node **grown = (fd_node **)realloc(args, (size_t)new_cap * sizeof(fd_node *));
            if (!grown) {
                int i;
                fd_node_free(arg);
                for (i = 0; i < argc; i++) fd_node_free(args[i]);
                free(args);
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            args = grown;
            cap = new_cap;
        }
        args[argc++] = arg;

        t = fd_peek(ps);
        if (t->type == TOK_COMMA) {
            ps->p++;
            continue;
        }
        if (t->type == TOK_RPAREN) {
            ps->p++;
            break;
        }

        {
            int i;
            for (i = 0; i < argc; i++) fd_node_free(args[i]);
            free(args);
        }
        fd_p_error(ps, t->pos, "函数 %s 的参数列表少了一个逗号或右括号",
                   fd_math_fn_name(fn_id));
        return NULL;
    }

    if (argc < min_args || argc > max_args) {
        int i;
        for (i = 0; i < argc; i++) fd_node_free(args[i]);
        free(args);

        if (min_args == max_args) {
            snprintf(arity_desc, sizeof(arity_desc), "%d", min_args);
        } else {
            snprintf(arity_desc, sizeof(arity_desc), "%d~%d", min_args, max_args);
        }
        fd_p_error(ps, name_pos, "函数 %s 需要 %s 个参数，现在给了 %d 个",
                   fd_math_fn_name(fn_id), arity_desc, argc);
        return NULL;
    }

    return fd_make_call(ps, fn_id, args, argc);
}

/**
 * 把未知多字母名字拆成单个字符的乘积（`xy`、`ax`、`2πx` 里的 `πx`……）。
 * 只要有一个字符既不是采样变量、也不是可绑定参数或常量，就返回 NULL。
 */
static fd_node *fd_try_split( fd_parser *ps, const char *name, int len)
{
    fd_node *node = NULL;
    size_t i = 0;

    /* 至少两个码点才拆（Kotlin 用 name.length < 2 判断） */
    {
        size_t count = 0;
        size_t j = 0;
        while (j < (size_t)len) {
            size_t n = fd_utf8_seq_len((unsigned char)name[j]);
            if (j + n > (size_t)len) n = 1;
            j += n;
            count++;
        }
        if (count < 2) return NULL;
    }

    while (i < (size_t)len) {
        char one[5];
        size_t n = fd_utf8_seq_len((unsigned char)name[i]);
        fd_node *child = NULL;
        int vi;

        if (i + n > (size_t)len) n = 1;
        memcpy(one, name + i, n);
        one[n] = '\0';

        vi = fd_find_var(ps, one, (int)n);
        if (vi >= 0) {
            child = fd_node_var(vi);
        } else {
            double pv;
            double cv;

            if (fd_find_param(ps, one, (int)n, &pv)) {
                child = fd_node_const(pv);
            } else if (fd_math_constant_exact(one, n, &cv)) {
                child = fd_node_const(cv);
            } else {
                fd_node_free(node);
                return NULL;
            }
        }

        if (!child) {
            fd_node_free(node);
            return NULL;
        }

        if (!node) {
            node = child;
        } else {
            fd_node *m = fd_node_binary(NODE_MUL, node, child);
            if (!m) return NULL;
            node = m;
        }

        i += n;
    }

    return node;
}

static fd_node *fd_parse_ident(fd_parser *ps, const fd_token *t)
{
    const char *name = t->text;
    int len = t->text_len;
    int vi;
    double value;
    int fn_id;

    /* 1) 采样变量优先 */
    vi = fd_find_var(ps, name, len);
    if (vi >= 0) return fd_node_var(vi);

    /* 2) 可绑定参数 a b c d：编译期折成常量 */
    if (fd_find_param(ps, name, len, &value)) return fd_node_const(value);

    /* 3) 常量（先精确、再小写） */
    if (fd_math_constant_exact(name, (size_t)len, &value)) return fd_node_const(value);
    if (fd_math_constant_lower(name, (size_t)len, &value)) return fd_node_const(value);

    /* 4) 内置函数 */
    if (fd_math_lookup(name, (size_t)len, &fn_id)) {
        int min_args = 1;
        int max_args = 1;

        fd_math_arity_of(fn_id, &min_args, &max_args);

        if (fd_peek(ps)->type == TOK_LPAREN) {
            return fd_parse_call_args(ps, fn_id, min_args, max_args, t->pos);
        }

        /* 允许省括号的写法：sin x、√x、ln x */
        if (!(min_args <= 1 && 1 <= max_args)) {
            fd_p_error(ps, t->pos, "函数 %s 需要 %d 个参数，要写括号",
                       fd_math_fn_name(fn_id), min_args);
            return NULL;
        }

        {
            fd_node *arg = fd_parse_unary(ps);
            fd_node **args;

            if (!arg) return NULL;
            args = (fd_node **)malloc(sizeof(fd_node *));
            if (!args) {
                fd_node_free(arg);
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            args[0] = arg;
            return fd_make_call(ps, fn_id, args, 1);
        }
    }

    /* 5) 兜底：把未知名字拆成单个变量的乘积 */
    {
        fd_node *split = fd_try_split(ps, name, len);
        if (split) return split;
    }

    /* 6) 报错 + 可用名字提示 */
    {
        char hint[512];
        fd_build_hint(ps, name, len, hint, sizeof(hint));
        fd_p_error(ps, t->pos, "不认识的名字 '%.*s'。%s", len, name, hint);
    }
    return NULL;
}

static fd_node *fd_parse_primary(fd_parser *ps)
{
    const fd_token *t = fd_peek(ps);

    switch (t->type) {
    case TOK_NUMBER:
        ps->p++;
        return fd_node_const(t->value);

    case TOK_LPAREN: {
        fd_node *inner;

        ps->p++;
        inner = fd_parse_sum(ps);
        if (!inner) return NULL;
        if (fd_peek(ps)->type != TOK_RPAREN) {
            fd_p_error(ps, fd_peek(ps)->pos, "括号没有闭合");
            fd_node_free(inner);
            return NULL;
        }
        ps->p++;
        return inner;
    }

    case TOK_BAR: {
        fd_node *inner;
        fd_node **args;

        ps->p++;
        ps->inside_bars++;
        inner = fd_parse_sum(ps);
        ps->inside_bars--;

        if (!inner) return NULL;
        if (fd_peek(ps)->type != TOK_BAR) {
            fd_p_error(ps, fd_peek(ps)->pos, "绝对值符号 | 没有成对出现");
            fd_node_free(inner);
            return NULL;
        }
        ps->p++;

        args = (fd_node **)malloc(sizeof(fd_node *));
        if (!args) {
            fd_node_free(inner);
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
        args[0] = inner;
        return fd_make_call(ps, FD_FN_ABS, args, 1);
    }

    case TOK_IDENT:
        ps->p++;
        return fd_parse_ident(ps, t);

    case TOK_EOF:
        fd_p_error(ps, t->pos, "表达式不完整");
        return NULL;

    default:
        fd_unexpected_error(ps, t);
        return NULL;
    }
}

static fd_node *fd_parse_postfix(fd_parser *ps)
{
    fd_node *n = fd_parse_primary(ps);

    if (!n) return NULL;
    while (fd_match_op(ps, '!')) {
        n = fd_node_unary(NODE_FACT, n);
        if (!n) {
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
    }
    return n;
}

static fd_node *fd_parse_power(fd_parser *ps)
{
    fd_node *base = fd_parse_postfix(ps);

    if (!base) return NULL;

    /* 幂右结合，且指数允许一元负号：2^-1、2^3^2 */
    if (fd_match_op(ps, '^')) {
        fd_node *exponent = fd_parse_unary(ps);
        if (!exponent) {
            fd_node_free(base);
            return NULL;
        }
        base = fd_node_binary(NODE_POW, base, exponent);
        if (!base) {
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
    }
    return base;
}

static fd_node *fd_parse_unary(fd_parser *ps)
{
    if (fd_match_op(ps, '-')) {
        fd_node *child = fd_parse_unary(ps);
        fd_node *n;

        if (!child) return NULL;
        n = fd_node_unary(NODE_NEG, child);
        if (!n) {
            fd_p_error(ps, 0, "内存不够");
            return NULL;
        }
        return n;
    }
    if (fd_match_op(ps, '+')) return fd_parse_unary(ps);
    return fd_parse_power(ps);
}

static fd_node *fd_parse_product(fd_parser *ps)
{
    fd_node *left = fd_parse_unary(ps);

    if (!left) return NULL;

    for (;;) {
        fd_node *right;
        fd_node *n;

        if (fd_match_op(ps, '*')) {
            right = fd_parse_unary(ps);
            if (!right) {
                fd_node_free(left);
                return NULL;
            }
            n = fd_node_binary(NODE_MUL, left, right);
            if (!n) {
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            left = n;
        } else if (fd_match_op(ps, '/')) {
            right = fd_parse_unary(ps);
            if (!right) {
                fd_node_free(left);
                return NULL;
            }
            n = fd_node_binary(NODE_DIV, left, right);
            if (!n) {
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            left = n;
        } else if (fd_match_op(ps, '%')) {
            right = fd_parse_unary(ps);
            if (!right) {
                fd_node_free(left);
                return NULL;
            }
            n = fd_node_binary(NODE_MOD, left, right);
            if (!n) {
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            left = n;
        } else if (fd_starts_operand(ps, fd_peek(ps))) {
            /* 隐式乘法：紧跟一个能开启操作数的 token */
            right = fd_parse_unary(ps);
            if (!right) {
                fd_node_free(left);
                return NULL;
            }
            n = fd_node_binary(NODE_MUL, left, right);
            if (!n) {
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            left = n;
        } else {
            return left;
        }
    }
}

static fd_node *fd_parse_sum(fd_parser *ps)
{
    fd_node *left = fd_parse_product(ps);

    if (!left) return NULL;

    for (;;) {
        fd_node *right;
        fd_node *n;

        if (fd_match_op(ps, '+')) {
            right = fd_parse_product(ps);
            if (!right) {
                fd_node_free(left);
                return NULL;
            }
            n = fd_node_binary(NODE_ADD, left, right);
            if (!n) {
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            left = n;
        } else if (fd_match_op(ps, '-')) {
            right = fd_parse_product(ps);
            if (!right) {
                fd_node_free(left);
                return NULL;
            }
            n = fd_node_binary(NODE_SUB, left, right);
            if (!n) {
                fd_p_error(ps, 0, "内存不够");
                return NULL;
            }
            left = n;
        } else {
            return left;
        }
    }
}

static fd_node *fd_parse(fd_parser *ps)
{
    fd_node *root = fd_parse_sum(ps);

    if (!root) return NULL;
    if (fd_peek(ps)->type != TOK_EOF) {
        fd_unexpected_error(ps, fd_peek(ps));
        fd_node_free(root);
        return NULL;
    }
    return root;
}

/* ===================================================================== */
/* 求值                                                                  */
/* ===================================================================== */

static double fd_safe_div(double x, double y)
{
    if (y == 0.0) {
        if (x == 0.0 || isnan(x)) return NAN;
        return signbit(x) != signbit(y) ? -INFINITY : INFINITY;
    }
    return x / y;
}

static double fd_eval_node(const fd_node *n, const double *args, int nargs)
{
    int i;

    if (!n) return NAN;

    switch (n->kind) {
    case NODE_CONST:
        return n->value;

    case NODE_VAR:
        if (!args || n->index < 0 || n->index >= nargs) return NAN;
        return args[n->index];

    case NODE_NEG:
        return -fd_eval_node(n->args[0], args, nargs);

    case NODE_ADD:
        return fd_eval_node(n->args[0], args, nargs) +
               fd_eval_node(n->args[1], args, nargs);

    case NODE_SUB:
        return fd_eval_node(n->args[0], args, nargs) -
               fd_eval_node(n->args[1], args, nargs);

    case NODE_MUL:
        return fd_eval_node(n->args[0], args, nargs) *
               fd_eval_node(n->args[1], args, nargs);

    case NODE_DIV:
        return fd_safe_div(fd_eval_node(n->args[0], args, nargs),
                           fd_eval_node(n->args[1], args, nargs));

    case NODE_POW:
        return pow(fd_eval_node(n->args[0], args, nargs),
                   fd_eval_node(n->args[1], args, nargs));

    case NODE_MOD:
        return fd_math_math_mod(fd_eval_node(n->args[0], args, nargs),
                                fd_eval_node(n->args[1], args, nargs));

    case NODE_FACT:
        return fd_math_factorial(fd_eval_node(n->args[0], args, nargs));

    case NODE_CALL: {
        double buf[8];

        for (i = 0; i < n->argc && i < 8; i++) {
            buf[i] = fd_eval_node(n->args[i], args, nargs);
        }
        return fd_math_call(n->index, buf, n->argc);
    }

    default:
        return NAN;
    }
}

/* ===================================================================== */
/* 对外 API                                                              */
/* ===================================================================== */

struct fd_expr {
    fd_node *root;
    int var_count;
    bool degrees;
};

static bool fd_params_valid(const fd_param *params, int nparams, fd_expr_error *err)
{
    int i;

    if (!params) nparams = 0;
    for (i = 0; i < nparams; i++) {
        const char *name = params[i].name;
        if (!name || (strlen(name) != 1) ||
            (name[0] != 'a' && name[0] != 'b' && name[0] != 'c' && name[0] != 'd')) {
            fd_error_set(err, 0,
                         "不认识的参数名：%s；可绑定的只有 a、b、c、d",
                         name ? name : "");
            return false;
        }
    }
    return true;
}

static void fd_resolve_params(const fd_param *params, int nparams, double out[4])
{
    int i;

    for (i = 0; i < 4; i++) out[i] = 1.0; /* ParameterSet.DEFAULT_VALUE */
    if (!params) return;
    for (i = 0; i < nparams; i++) {
        const char *name = params[i].name;
        if (!name || strlen(name) != 1) continue;
        if (name[0] >= 'a' && name[0] <= 'd') {
            out[name[0] - 'a'] = params[i].value;
        }
    }
}

static fd_expr *fd_compile_internal(const char *source,
                                    const char *const *vars, int nvars,
                                    bool degrees,
                                    const fd_param *params, int nparams,
                                    fd_expr_error *err)
{
    fd_lexer lx;
    fd_parser ps;
    fd_node *root;
    fd_expr *e;

    fd_error_clear(err);

    if (!fd_params_valid(params, nparams, err)) return NULL;

    if (!source) source = "";
    if (fd_is_blank(source)) {
        fd_error_set(err, 0, "表达式是空的");
        return NULL;
    }

    if (!vars) nvars = 0;
    if (nvars < 0) nvars = 0;
    if (nparams < 0) nparams = 0;

    memset(&lx, 0, sizeof(lx));
    lx.src = source;
    lx.len = strlen(source);
    lx.err = err;

    if (!fd_lex_run(&lx)) {
        free(lx.toks);
        return NULL;
    }

    memset(&ps, 0, sizeof(ps));
    ps.toks = lx.toks;
    ps.ntok = lx.ntok;
    ps.p = 0;
    ps.vars = vars;
    ps.nvars = nvars;
    ps.degrees = degrees;
    ps.err = err;
    fd_resolve_params(params, nparams, ps.params);

    root = fd_parse(&ps);
    free(lx.toks);

    if (!root) return NULL;

    e = (fd_expr *)calloc(1, sizeof(fd_expr));
    if (!e) {
        fd_node_free(root);
        fd_error_set(err, 0, "内存不够");
        return NULL;
    }

    e->root = root;
    e->var_count = nvars;
    e->degrees = degrees;

    fd_error_clear(err);
    return e;
}

static int fd_find_top_equals(const char *s, size_t *byte_off)
{
    size_t len;
    size_t i = 0;
    int depth = 0;
    int cp = 0;

    if (!s) return -1;
    len = strlen(s);

    while (i < len) {
        char c = s[i];

        if (c == '(' || c == '[') {
            depth++;
        } else if (c == ')' || c == ']') {
            depth--;
        } else if (c == '=' && depth == 0) {
            if (byte_off) *byte_off = i;
            return cp;
        }

        {
            uint32_t ignored;
            i += fd_utf8_decode(s, len, i, &ignored);
            cp++;
        }
    }
    return -1;
}

fd_expr *fd_expr_compile(const char *source,
                         const char *const *vars, int nvars,
                         bool degrees,
                         const fd_param *params, int nparams,
                         fd_expr_error *err)
{
    return fd_compile_internal(source, vars, nvars, degrees, params, nparams, err);
}

fd_expr *fd_expr_compile_equation(const char *source,
                                  const char *const *vars, int nvars,
                                  bool degrees,
                                  const fd_param *params, int nparams,
                                  fd_expr_error *err)
{
    size_t off = 0;
    int idx;
    const char *rhs;
    char *synth;
    size_t lhs_len;
    size_t rhs_len;
    size_t d = 0;
    fd_expr *result;

    fd_error_clear(err);

    if (!fd_params_valid(params, nparams, err)) return NULL;
    if (!source) source = "";

    idx = fd_find_top_equals(source, &off);
    if (idx < 0) return fd_compile_internal(source, vars, nvars, degrees,
                                            params, nparams, err);

    rhs = source + off + 1;
    if (fd_is_blank(rhs)) {
        fd_error_set(err, idx, "等号右边是空的");
        return NULL;
    }

    lhs_len = off;
    rhs_len = strlen(rhs);

    synth = (char *)malloc(lhs_len + rhs_len + 8);
    if (!synth) {
        fd_error_set(err, idx, "内存不够");
        return NULL;
    }

    synth[d++] = '(';
    memcpy(synth + d, source, lhs_len);
    d += lhs_len;
    synth[d++] = ')';
    synth[d++] = '-';
    synth[d++] = '(';
    memcpy(synth + d, rhs, rhs_len);
    d += rhs_len;
    synth[d++] = ')';
    synth[d] = '\0';

    result = fd_compile_internal(synth, vars, nvars, degrees, params, nparams, err);
    free(synth);
    return result;
}

double fd_expr_eval(const fd_expr *e, const double *args)
{
    if (!e) return NAN;
    return fd_eval_node(e->root, args, e->var_count);
}

int fd_expr_var_count(const fd_expr *e)
{
    return e ? e->var_count : 0;
}

bool fd_expr_is_degrees(const fd_expr *e)
{
    return e ? e->degrees : false;
}

void fd_expr_free(fd_expr *e)
{
    if (!e) return;
    fd_node_free(e->root);
    free(e);
}

int fd_expr_top_level_equals(const char *s)
{
    return fd_find_top_equals(s, NULL);
}

static bool fd_trim_space(char c)
{
    return (unsigned char)c <= ' ';
}

const char *fd_expr_strip_explicit_prefix(const char *src, char *buf, size_t cap)
{
    size_t len;
    size_t i;
    size_t content;
    size_t end;
    size_t d = 0;

    if (!buf || cap == 0) return buf;
    buf[0] = '\0';
    if (!src) return buf;

    len = strlen(src);

    i = 0;
    while (i < len && fd_trim_space(src[i])) i++;

    content = i;

    /* y = ... */
    if (i < len && (src[i] == 'y' || src[i] == 'Y')) {
        size_t k = i + 1;
        while (k < len && fd_trim_space(src[k])) k++;
        if (k < len && src[k] == '=') {
            k++;
            while (k < len && fd_trim_space(src[k])) k++;
            content = k;
        } else {
            content = i;
        }
    } else if (i < len && (src[i] == 'f' || src[i] == 'F')) {
        /* f(x) = ...（大小写不敏感，括号内可以有空格） */
        size_t k = i + 1;
        while (k < len && fd_trim_space(src[k])) k++;
        if (k < len && src[k] == '(') {
            k++;
            while (k < len && fd_trim_space(src[k])) k++;
            if (k < len && (src[k] == 'x' || src[k] == 'X')) {
                k++;
                while (k < len && fd_trim_space(src[k])) k++;
                if (k < len && src[k] == ')') {
                    k++;
                    while (k < len && fd_trim_space(src[k])) k++;
                    if (k < len && src[k] == '=') {
                        k++;
                        while (k < len && fd_trim_space(src[k])) k++;
                        content = k;
                    }
                }
            }
        }
    }

    end = len;
    while (end > content && fd_trim_space(src[end - 1])) end--;

    i = content;
    while (i < end && d + 1 < cap) {
        size_t n = fd_utf8_seq_len((unsigned char)src[i]);
        if (i + n > end) n = 1;
        if (d + n > cap - 1) break;
        memcpy(buf + d, src + i, n);
        d += n;
        i += n;
    }
    buf[d] = '\0';
    return buf;
}
