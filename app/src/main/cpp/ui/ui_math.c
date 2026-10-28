/** 轻量数学排版实现，见 ui_math.h。 */
#include "ui_math.h"

#include <stdio.h>
#include <string.h>

#include "ui.h"

static uint32_t g_color;

/** 取一段 {...} 的内容（src 指向 '{' 之后），返回右花括号之后的位置。 */
static const char *take_group(const char *src, char *out, size_t cap)
{
    int depth = 1;
    size_t n = 0;
    while (*src && depth > 0) {
        char c = *src++;
        if (c == '{') {
            depth++;
            if (depth > 1 && n + 1 < cap) out[n++] = c;
        } else if (c == '}') {
            depth--;
            if (depth == 0) break;
            if (n + 1 < cap) out[n++] = c;
        } else if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return src;
}

static lv_obj_t *line_obj(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_height(o, 1);
    lv_obj_set_width(o, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_70, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/** 一个「行高」容器：内部内容按顶部/底部对齐，用来做上下标。 */
static lv_obj_t *atom_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, UI_PX(22));
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void add_text(lv_obj_t *row, const char *text)
{
    if (!text || !text[0]) return;
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, ui_font_small(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(g_color), 0);
}

static void add_script(lv_obj_t *row, const char *text, bool up)
{
    if (!text || !text[0]) return;
    lv_obj_t *box = atom_box(row);
    lv_obj_t *l = lv_label_create(box);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, ui_font_small(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(g_color), 0);
    lv_obj_align(l, up ? LV_ALIGN_TOP_LEFT : LV_ALIGN_BOTTOM_LEFT, 0, up ? -UI_PX(3) : 0);
}

static void add_frac(lv_obj_t *row, const char *num, const char *den)
{
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, UI_PX(1), 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *n = lv_label_create(col);
    lv_label_set_text(n, num);
    lv_obj_set_style_text_font(n, ui_font_small(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(g_color), 0);

    line_obj(col, g_color);

    lv_obj_t *d = lv_label_create(col);
    lv_label_set_text(d, den);
    lv_obj_set_style_text_font(d, ui_font_small(), 0);
    lv_obj_set_style_text_color(d, lv_color_hex(g_color), 0);
}

static void add_sqrt(lv_obj_t *row, const char *body)
{
    lv_obj_t *wrap = lv_obj_create(row);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_column(wrap, UI_PX(1), 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *r = lv_label_create(wrap);
    lv_label_set_text(r, "√");
    lv_obj_set_style_text_font(r, ui_font_small(), 0);
    lv_obj_set_style_text_color(r, lv_color_hex(g_color), 0);

    lv_obj_t *rad = lv_obj_create(wrap);
    lv_obj_remove_style_all(rad);
    lv_obj_set_size(rad, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(rad, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(rad, 1, 0);
    lv_obj_set_style_border_color(rad, lv_color_hex(g_color), 0);
    lv_obj_set_style_border_opa(rad, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(rad, UI_PX(2), 0);
    lv_obj_remove_flag(rad, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(rad);
    lv_label_set_text(l, body);
    lv_obj_set_style_text_font(l, ui_font_small(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(g_color), 0);
}

/* \pi / \theta 之类的简单命令。 */
static const char *math_command(const char *s, char *sym, size_t cap)
{
    struct { const char *cmd; const char *sym; } k_map[] = {
            {"\\pi", "π"}, {"\\theta", "θ"}, {"\\alpha", "α"},
            {"\\beta", "β"}, {"\\gamma", "γ"}, {"\\infty", "∞"},
    };
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        size_t n = strlen(k_map[i].cmd);
        if (strncmp(s, k_map[i].cmd, n) == 0) {
            snprintf(sym, cap, "%s", k_map[i].sym);
            return s + n;
        }
    }
    return NULL;
}

void ui_math_set_text(lv_obj_t *cont, const char *expr, uint32_t color)
{
    if (!cont) return;
    g_color = color;
    lv_obj_clean(cont);
    if (!expr || !expr[0]) return;

    char buf[256];
    size_t run = 0;
    const char *s = expr;

    while (*s) {
        char g1[128];
        char g2[128];

        if (*s == '^' || *s == '_') {
            bool up = (*s == '^');
            s++;
            if (*s == '{') {
                s = take_group(s + 1, g1, sizeof(g1));
            } else if (*s) {
                g1[0] = *s++;
                g1[1] = '\0';
            } else {
                g1[0] = '\0';
            }
            add_script(cont, g1, up);
            continue;
        }
        if (strncmp(s, "\\frac", 5) == 0) {
            s += 5;
            while (*s == ' ') s++;
            if (*s == '{') s = take_group(s + 1, g1, sizeof(g1)); else g1[0] = '\0';
            while (*s == ' ') s++;
            if (*s == '{') s = take_group(s + 1, g2, sizeof(g2)); else g2[0] = '\0';
            add_frac(cont, g1, g2);
            continue;
        }
        if (strncmp(s, "\\sqrt", 5) == 0) {
            s += 5;
            while (*s == ' ') s++;
            if (*s == '{') s = take_group(s + 1, g1, sizeof(g1)); else g1[0] = '\0';
            add_sqrt(cont, g1);
            continue;
        }
        if (*s == '\\') {
            const char *next = math_command(s, g1, sizeof(g1));
            if (next) {
                if (run > 0) {
                    buf[run] = '\0';
                    add_text(cont, buf);
                    run = 0;
                }
                add_text(cont, g1);
                s = next;
                continue;
            }
        }

        if (run + 2 >= sizeof(buf)) {
            buf[run] = '\0';
            add_text(cont, buf);
            run = 0;
        }
        buf[run++] = *s++;
    }
    if (run > 0) {
        buf[run] = '\0';
        add_text(cont, buf);
    }
}

lv_obj_t *ui_math_create(lv_obj_t *parent, const char *expr, uint32_t color)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, UI_PX(1), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    /* 底部对齐 = 这一行共用同一条基线。 */
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    ui_math_set_text(row, expr, color);
    return row;
}
