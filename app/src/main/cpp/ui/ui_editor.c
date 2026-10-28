/**
 * 表达式编辑器：自绘数学键盘（4 页）+ 输入框 + 实时编译校验。
 */
#include "ui.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmath.h"
#include "expr.h"
#include "model.h"
#include "plot.h"
#include "subst.h"

#define FD_EDITOR_COLS 5
#define FD_ALPHA_COLS 6
#define FD_ALPHA_ROWS 6

static lv_obj_t *g_ta1;
static lv_obj_t *g_ta2;
static lv_obj_t *g_ta2_row;
static lv_obj_t *g_ta_active;
static lv_obj_t *g_err;
static lv_obj_t *g_grid;
static lv_obj_t *g_tabs;
static int g_page;
static int g_curve_id;
/* 字母页的大小写：0 小写 / 1 大写 / 2 大写锁定（再点一次回到小写）。 */
static int g_letter_case;

/* ------------------------------------------------------------------ */
/* 键盘布局                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *label;
    const char *insert; /**< NULL = 用 label；特殊键见 key_clicked */
} key_def;

#define K(l) { l, NULL }

static const key_def k_page0[] = {
        K("7"), K("8"), K("9"), K("÷"), K("退"),
        K("4"), K("5"), K("6"), K("×"), K("("),
        K("1"), K("2"), K("3"), K("-"), K(")"),
        K("0"), K("."), K("x"), K("+"), K("^"),
};

static const key_def k_page1[] = {
        K("sin"), K("cos"), K("tan"), K("√"), K("ln"),
        K("asin"), K("acos"), K("atan"), K("log"), K("exp"),
        K("sinh"), K("cosh"), K("tanh"), K("abs"), K("floor"),
        K("ceil"), K("round"), K("π"), K("e"), K("t"),
};

static const key_def k_page2[] = {
        K("("), K(")"), K("+"), K("-"), K("×"),
        K("÷"), K("^"), K("="), K("<"), K(">"),
        K(","), K("."), K("!"), K("|"), K("θ"),
        K("π"), K("e"), K("x"), K("y"), K("清"),
};

static const key_def k_page3[] = {
        K("返回"), K("a"), K("b"), K("c"), K("d"), K("e"),
        K("f"), K("g"), K("h"), K("i"), K("j"), K("k"),
        K("l"), K("m"), K("n"), K("o"), K("p"), K("q"),
        K("r"), K("s"), K("t"), K("u"), K("v"), K("w"),
        K("x"), K("y"), K("z"), K(""), K(""), K(""),
        K("小写"), K("退"), K("("), K(")"), K(","), K(""),
};

static const char *const k_case_names[3] = {"小写", "大写", "锁定"};

static const char *const k_tabs[] = {"数字", "函数", "符号", "字母"};

static const key_def *page_keys(int page)
{
    switch (page) {
        case 1: return k_page1;
        case 2: return k_page2;
        case 3: return k_page3;
        default: return k_page0;
    }
}

/* ------------------------------------------------------------------ */
/* 校验                                                                */
/* ------------------------------------------------------------------ */

static bool validate_text(const char *src, fd_plot_mode mode, bool degrees,
                          char *msg, size_t cap)
{
    /* 先做几何量替换：x(A)/y(A) 和距离/角/面积对象名换成数值字面量。 */
    char subst[FD_SRC_CAP];
    fd_subst_geo(src, subst, sizeof(subst));
    src = subst;

    /* 复平面模式：按复数表达式校验。 */
    if (model()->settings.complex_plane) {
        double values[4];
        model_param_values(values);
        fd_param ps[4];
        static const char *pnames[4] = {"a", "b", "c", "d"};
        for (int i = 0; i < 4; i++) {
            ps[i].name = pnames[i];
            ps[i].value = values[i];
        }
        fd_expr_error cerr;
        memset(&cerr, 0, sizeof(cerr));
        fd_cpx_node *ce = fd_cpx_compile(src, ps, 4, &cerr);
        if (!ce) {
            snprintf(msg, cap, "%s", cerr.message[0] ? cerr.message : "表达式无效");
            return false;
        }
        fd_cpx_free(ce);
        return true;
    }

    double params[4];
    model_param_values(params);
    fd_param ps[4];
    static const char *names[4] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; i++) {
        ps[i].name = names[i];
        ps[i].value = params[i];
    }

    fd_expr_error err;
    memset(&err, 0, sizeof(err));
    fd_expr *e = NULL;

    switch (mode) {
        case FD_PLOT_EXPLICIT: {
            char body[FD_SRC_CAP];
            fd_expr_strip_explicit_prefix(src, body, sizeof(body));
            if (body[0] == '\0') {
                snprintf(msg, cap, "还没写表达式");
                return false;
            }
            const char *vars[1] = {"x"};
            e = fd_expr_compile(body, vars, 1, degrees, ps, 4, &err);
            break;
        }
        case FD_PLOT_POLAR: {
            const char *vars[2] = {"θ", "t"};
            e = fd_expr_compile(src, vars, 2, degrees, ps, 4, &err);
            break;
        }
        case FD_PLOT_PARAMETRIC: {
            const char *vars[1] = {"t"};
            e = fd_expr_compile(src, vars, 1, degrees, ps, 4, &err);
            break;
        }
        case FD_PLOT_IMPLICIT: {
            const char *vars[2] = {"x", "y"};
            e = fd_expr_compile_equation(src, vars, 2, degrees, ps, 4, &err);
            break;
        }
    }

    if (!e) {
        snprintf(msg, cap, "%s", err.message[0] ? err.message : "表达式无效");
        return false;
    }
    fd_expr_free(e);
    return true;
}

/* ------------------------------------------------------------------ */
/* 键盘                                                                */
/* ------------------------------------------------------------------ */

/* 根号 / 绝对值类按键插下的「空骨架」：退格时一次还回去，不留下半截括号。 */
static const char *const k_structural[] = {
        "sqrt()", "cbrt()", "abs()", "√()", "∛()", "root(,)",
};

/** 光标左边正好是一个完整空骨架时整段删掉；返回 true 表示已处理。 */
static bool delete_structural_before(lv_obj_t *ta)
{
    if (!ta) return false;
    const char *txt = lv_textarea_get_text(ta);
    uint32_t pos = lv_textarea_get_cursor_pos(ta);
    if (!txt || pos == 0) return false;

    for (size_t i = 0; i < sizeof(k_structural) / sizeof(k_structural[0]); i++) {
        size_t len = strlen(k_structural[i]);
        if (pos < len || strncmp(txt + pos - len, k_structural[i], len) != 0) continue;

        char buf[FD_SRC_CAP];
        size_t head = pos - len;
        size_t tail = strlen(txt + pos);
        if (head + tail + 1 > sizeof(buf)) return false;
        memcpy(buf, txt, head);
        memcpy(buf + head, txt + pos, tail + 1);
        lv_textarea_set_text(ta, buf);
        lv_textarea_set_cursor_pos(ta, (int32_t)head);
        return true;
    }
    return false;
}

static void insert_text(const char *text)
{
    if (!g_ta_active) return;
    char buf[32];
    bool want_inside = false;
    /* 函数键插 name() 并把光标放进括号里，少按两次。 */
    if (strcmp(text, "sin") == 0 || strcmp(text, "cos") == 0 ||
        strcmp(text, "tan") == 0 || strcmp(text, "asin") == 0 ||
        strcmp(text, "acos") == 0 || strcmp(text, "atan") == 0 ||
        strcmp(text, "sinh") == 0 || strcmp(text, "cosh") == 0 ||
        strcmp(text, "tanh") == 0 || strcmp(text, "log") == 0 ||
        strcmp(text, "ln") == 0 || strcmp(text, "exp") == 0 ||
        strcmp(text, "abs") == 0 || strcmp(text, "floor") == 0 ||
        strcmp(text, "ceil") == 0 || strcmp(text, "round") == 0) {
        snprintf(buf, sizeof(buf), "%s()", text);
        want_inside = true;
    } else if (strcmp(text, "√") == 0) {
        snprintf(buf, sizeof(buf), "sqrt()");
        want_inside = true;
    } else {
        snprintf(buf, sizeof(buf), "%s", text);
    }
    lv_textarea_add_text(g_ta_active, buf);
    if (want_inside) lv_textarea_cursor_left(g_ta_active);
}

static void refresh_error(void);

static void build_keypad(void);
static void set_page(int page);

static void key_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    const char *text = (const char *)lv_obj_get_user_data(btn);
    if (!text || !text[0]) return;

    if (strcmp(text, "返回") == 0) {
        set_page(0);
        return;
    }
    if (strcmp(text, "小写") == 0 || strcmp(text, "大写") == 0 ||
        strcmp(text, "锁定") == 0) {
        /* 小写 → 大写 → 锁定 → 小写；打一个字母不自动回落。 */
        g_letter_case = (g_letter_case + 1) % 3;
        build_keypad();
        return;
    }

    if (strcmp(text, "退") == 0) {
        /* 根号 / 绝对值类的空骨架整段删掉，否则会先露出半截括号。 */
        if (!delete_structural_before(g_ta_active)) lv_textarea_delete_char(g_ta_active);
    } else if (strcmp(text, "清") == 0) {
        lv_textarea_set_text(g_ta_active, "");
    } else {
        insert_text(text);
    }
    refresh_error();
}

/** 字母页第 i 格显示什么：字母按当前大小写模式取值，其余用固定标签。 */
static const char *alpha_label(int i)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    static char buf[26][4];
    static char case_buf[8];

    if (i == 0) return "返回";
    if (i >= 1 && i <= 26) {
        char ch = letters[i - 1];
        if (g_letter_case != 0) ch = (char)toupper((unsigned char)ch);
        snprintf(buf[i - 1], sizeof(buf[0]), "%c", ch);
        return buf[i - 1];
    }
    if (i == 30) {
        snprintf(case_buf, sizeof(case_buf), "%s", k_case_names[g_letter_case]);
        return case_buf;
    }
    return k_page3[i].label;
}

static void build_keypad(void)
{
    lv_obj_clean(g_grid);

    bool alpha = (g_page == 3);
    int cols = alpha ? FD_ALPHA_COLS : FD_EDITOR_COLS;
    int rows = alpha ? FD_ALPHA_ROWS : 4;
    int key_h = alpha ? UI_PX(32) : UI_PX(38);
    const key_def *keys = page_keys(g_page);

    lv_obj_set_size(g_grid, cols * UI_PX(62) + (cols - 1) * UI_PX(5),
                    rows * key_h + (rows - 1) * UI_PX(5));
    lv_obj_set_flex_flow(g_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_grid, UI_PX(5), 0);
    lv_obj_set_style_pad_column(g_grid, UI_PX(5), 0);

    for (int i = 0; i < cols * rows; i++) {
        const char *label = alpha ? alpha_label(i) : keys[i].label;
        lv_obj_t *b = lv_button_create(g_grid);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, UI_PX(62), key_h);
        /* 空格子只占位，保证每排对齐（字母页 x y z 那排只有 3 个）。 */
        if (!label[0]) continue;
        lv_obj_set_style_bg_color(b, lv_color_hex(FD_C_PANEL2), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, UI_PX(10), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_user_data(b, (void *)label);
        lv_obj_add_event_cb(b, key_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, label);
        lv_obj_set_style_text_font(l, ui_font_body(), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
        lv_obj_center(l);

        if (strcmp(label, "退") == 0 || strcmp(label, "清") == 0) {
            lv_obj_set_style_bg_color(b, lv_color_hex(0x33272C), 0);
            lv_obj_set_style_text_color(l, lv_color_hex(FD_C_DANGER), 0);
        } else if (strcmp(label, "返回") == 0) {
            lv_obj_set_style_bg_color(b, lv_color_hex(0x243038), 0);
            lv_obj_set_style_text_color(l, lv_color_hex(FD_C_ACCENT), 0);
        } else if (strcmp(label, "大写") == 0 || strcmp(label, "锁定") == 0) {
            /* 强调色表示"键盘现在不是默认的小写状态"。 */
            lv_obj_set_style_bg_color(b, lv_color_hex(0x1B3B48), 0);
            lv_obj_set_style_text_color(l, lv_color_hex(FD_C_ACCENT), 0);
        }
    }
}

static void paint_tabs(void)
{
    if (!g_tabs) return;
    uint32_t n = lv_obj_get_child_count(g_tabs);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(g_tabs, (int32_t)i);
        bool on = ((int)(intptr_t)lv_obj_get_user_data(c)) == g_page;
        lv_obj_set_style_bg_color(c, lv_color_hex(on ? 0x1B3B48 : FD_C_PANEL2), 0);
        lv_obj_t *l = lv_obj_get_child(c, 0);
        if (l) {
            lv_obj_set_style_text_color(
                    l, lv_color_hex(on ? FD_C_ACCENT : FD_C_DIM), 0);
        }
    }
}

static void set_page(int page)
{
    g_page = page;
    build_keypad();
    paint_tabs();
}

static void tab_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    set_page((int)(intptr_t)lv_obj_get_user_data(btn));
}

/* ------------------------------------------------------------------ */
/* 提交                                                                */
/* ------------------------------------------------------------------ */

static void refresh_error(void)
{
    if (!g_err) return;
    fd_curve *c = model_curve(g_curve_id);
    if (!c) return;

    bool deg = c->fn.degrees < 0 ? model()->settings.angle_degrees : c->fn.degrees != 0;
    char msg[FD_EXPR_ERR_CAP];
    msg[0] = '\0';

    const char *t1 = lv_textarea_get_text(g_ta1);
    bool ok = validate_text(t1 ? t1 : "", c->fn.mode, deg, msg, sizeof(msg));

    if (ok && c->fn.mode == FD_PLOT_PARAMETRIC) {
        const char *t2 = g_ta2 ? lv_textarea_get_text(g_ta2) : "";
        ok = validate_text(t2 ? t2 : "", FD_PLOT_PARAMETRIC, deg, msg, sizeof(msg));
    }

    if (ok) {
        lv_obj_add_flag(g_err, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_err, msg);
        lv_obj_remove_flag(g_err, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_changed(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    g_ta_active = ta;
    refresh_error();
}

static void commit_and_back(void)
{
    fd_curve *c = model_curve(g_curve_id);
    if (!c) {
        ui_show(UI_SCR_LIST);
        return;
    }

    bool deg = c->fn.degrees < 0 ? model()->settings.angle_degrees : c->fn.degrees != 0;
    char msg[FD_EXPR_ERR_CAP];
    msg[0] = '\0';

    const char *t1 = lv_textarea_get_text(g_ta1);
    if (!validate_text(t1 ? t1 : "", c->fn.mode, deg, msg, sizeof(msg))) {
        refresh_error();
        ui_toast(msg);
        return;
    }
    if (c->fn.mode == FD_PLOT_PARAMETRIC) {
        const char *t2 = g_ta2 ? lv_textarea_get_text(g_ta2) : "";
        if (!validate_text(t2 ? t2 : "", FD_PLOT_PARAMETRIC, deg, msg, sizeof(msg))) {
            ui_toast(msg);
            return;
        }
        snprintf(c->fn.src2, sizeof(c->fn.src2), "%s", t2 ? t2 : "");
    }

    snprintf(c->fn.src1, sizeof(c->fn.src1), "%s", t1 ? t1 : "");
    model_sync_sliders();
    model_checkpoint();
    model_save();
    ui_show(UI_SCR_LIST);
}

/* ------------------------------------------------------------------ */
/* 界面                                                                */
/* ------------------------------------------------------------------ */

static void back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_LIST);
}

static void done_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    commit_and_back();
}

static void apply_mode_defaults(fd_curve *c, fd_plot_mode mode)
{
    c->fn.mode = mode;
    switch (mode) {
        case FD_PLOT_EXPLICIT:
            snprintf(c->fn.src1, sizeof(c->fn.src1), "x");
            c->fn.src2[0] = '\0';
            break;
        case FD_PLOT_POLAR:
            snprintf(c->fn.src1, sizeof(c->fn.src1), "1");
            c->fn.src2[0] = '\0';
            c->fn.t_min = 0.0;
            c->fn.t_max = 6.283185307179586;
            break;
        case FD_PLOT_PARAMETRIC:
            snprintf(c->fn.src1, sizeof(c->fn.src1), "cos(t)");
            snprintf(c->fn.src2, sizeof(c->fn.src2), "sin(t)");
            c->fn.t_min = 0.0;
            c->fn.t_max = 6.283185307179586;
            break;
        case FD_PLOT_IMPLICIT:
            snprintf(c->fn.src1, sizeof(c->fn.src1), "x^2+y^2=1");
            c->fn.src2[0] = '\0';
            break;
    }
}

static void mode_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int mode = (int)(intptr_t)lv_obj_get_user_data(btn);
    fd_curve *c = model_curve(g_curve_id);
    if (!c) return;
    if ((int)c->fn.mode == mode) return;
    apply_mode_defaults(c, (fd_plot_mode)mode);
    model_checkpoint();
    ui_refresh_later();
}

static lv_obj_t *make_ta(lv_obj_t *parent, const char *text, const char *hint)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_remove_style_all(ta);
    lv_obj_set_size(ta, LV_PCT(100), UI_PX(40));
    lv_obj_set_style_bg_color(ta, lv_color_hex(FD_C_PANEL), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ta, UI_PX(10), 0);
    lv_obj_set_style_pad_hor(ta, UI_PX(10), 0);
    lv_obj_set_style_pad_ver(ta, UI_PX(8), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(FD_C_TEXT), 0);
    /* 占位提示用浅灰，和真正输入的文字区分开。 */
    lv_obj_set_style_text_color(ta, lv_color_hex(FD_C_DIM),
                                LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(ta, ui_font_title(), 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 240);
    lv_textarea_set_placeholder_text(ta, hint);
    lv_textarea_set_text(ta, text ? text : "");
    lv_obj_add_event_cb(ta, ta_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ta, ta_changed, LV_EVENT_CLICKED, NULL);
    return ta;
}

lv_obj_t *ui_editor_create(lv_obj_t *parent)
{
    fd_curve *c = model_selected_curve();
    if (!c) {
        lv_obj_t *root = lv_obj_create(parent);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
        lv_obj_t *l = ui_label(root, "先新建一条曲线", FD_C_DIM);
        lv_obj_center(l);
        return root;
    }

    g_curve_id = c->fn.id;
    g_page = 0;
    g_letter_case = 0;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, UI_PX(10), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, UI_PX(6), 0);

    /* 顶栏 */
    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), 36);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, c->name);
    lv_obj_set_style_text_font(title, ui_font_body(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *done = ui_button(head, "完成", done_clicked);
    lv_obj_set_size(done, UI_PX(62), UI_PX(34));
    lv_obj_align(done, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *back = ui_button(head, "返回", back_clicked);
    lv_obj_set_size(back, UI_PX(62), UI_PX(34));
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -UI_PX(70), 0);

    /* 类型选择 */
    static const char *const mode_names[] = {"y=f(x)", "r(θ)", "x(t),y(t)", "F(x,y)"};
    lv_obj_t *modes = lv_obj_create(root);
    lv_obj_remove_style_all(modes);
    lv_obj_set_size(modes, LV_PCT(100), UI_PX(34));
    lv_obj_remove_flag(modes, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modes, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    for (int m = 0; m < 4; m++) {
        lv_obj_t *b = lv_button_create(modes);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, UI_PX(96), UI_PX(30));
        bool on = ((int)c->fn.mode == m);
        lv_obj_set_style_bg_color(b, lv_color_hex(on ? 0x1B3B48 : FD_C_PANEL2), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, UI_PX(10), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)m);
        lv_obj_add_event_cb(b, mode_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, mode_names[m]);
        lv_obj_set_style_text_font(l, ui_font_body(), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(on ? FD_C_ACCENT : FD_C_DIM), 0);
        lv_obj_center(l);
    }

    /* 复平面模式下按复数求值，类型切换没有意义，整行隐藏。 */
    if (model()->settings.complex_plane) lv_obj_add_flag(modes, LV_OBJ_FLAG_HIDDEN);

    /* 输入框 */
    g_ta1 = make_ta(root, c->fn.src1,
                    model()->settings.complex_plane
                            ? "例如：2+3i"
                            : (c->fn.mode == FD_PLOT_EXPLICIT ? "例如：sin(x)" : "表达式"));
    g_ta2 = NULL;
    g_ta2_row = NULL;
    if (c->fn.mode == FD_PLOT_PARAMETRIC && !model()->settings.complex_plane) {
        lv_obj_t *row = lv_obj_create(root);
        g_ta2_row = row;
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), UI_PX(40));
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t *tag = lv_label_create(row);
        lv_label_set_text(tag, "y(t)=");
        lv_obj_set_style_text_font(tag, ui_font_body(), 0);
        lv_obj_set_style_text_color(tag, lv_color_hex(FD_C_DIM), 0);

        g_ta2 = make_ta(row, c->fn.src2, "例如：sin(t)");
        lv_obj_set_width(g_ta2, UI_PX(320));
    }

    g_err = lv_label_create(root);
    lv_label_set_text(g_err, "");
    lv_label_set_long_mode(g_err, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_err, LV_PCT(100));
    lv_obj_set_style_text_font(g_err, ui_font_small(), 0);
    lv_obj_set_style_text_color(g_err, lv_color_hex(FD_C_DANGER), 0);
    lv_obj_add_flag(g_err, LV_OBJ_FLAG_HIDDEN);

    /* 页签 */
    lv_obj_t *tabs = lv_obj_create(root);
    g_tabs = tabs;
    lv_obj_remove_style_all(tabs);
    lv_obj_set_size(tabs, LV_PCT(100), UI_PX(32));
    lv_obj_remove_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(tabs);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, UI_PX(84), UI_PX(28));
        bool on = (i == 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(on ? 0x1B3B48 : FD_C_PANEL2), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, UI_PX(9), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, tab_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, k_tabs[i]);
        lv_obj_set_style_text_font(l, ui_font_body(), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(on ? FD_C_ACCENT : FD_C_DIM), 0);
        lv_obj_center(l);
    }

    /* 键区：宽度由 build_keypad 按页设置，固定宽度才能锁住每排的列数。 */
    g_grid = lv_obj_create(root);
    lv_obj_remove_style_all(g_grid);
    lv_obj_set_style_bg_opa(g_grid, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(g_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(g_grid, 0, 0);
    build_keypad();

    g_ta_active = g_ta1;
    refresh_error();
    return root;
}
