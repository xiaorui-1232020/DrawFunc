/** 函数列表：选中、显隐、删除、新建、参数滑动条，以及进入存档/设置/关于。 */
#include "ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/plot.h"
#include "model.h"

#define FD_SLIDER_RANGE 1000

static void row_open(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(row);
    if (id == 0) return;
    model_select(id);
    ui_show(UI_SCR_EDIT);
}

static void row_props(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (id == 0) return;
    model_select(id);
    ui_show(UI_SCR_PROPS);
}

static void row_visible(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    fd_curve *c = model_curve(id);
    if (!c) return;
    c->fn.visible = !c->fn.visible;
    model_checkpoint();
    ui_refresh_later();
}

static void row_delete(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (!model_curve(id)) return;
    model_delete_curve(id);
    model_sync_sliders();
    model_checkpoint();
    ui_toast("已删除");
    ui_refresh_later();
}

static void add_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model()->curve_count >= FD_MAX_CURVES) {
        ui_toast("曲线数量已达上限");
        return;
    }
    int id = model_add_curve(FD_PLOT_EXPLICIT);
    fd_curve *c = model_curve(id);
    if (c && model()->settings.complex_plane && c->fn.src1[0] == '\0') {
        snprintf(c->fn.src1, sizeof(c->fn.src1), "1+i");
    }
    model_checkpoint();
    model_select(id);
    ui_show(UI_SCR_EDIT);
}

static void back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_GRAPH);
}

static void archive_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_ARCHIVE);
}

static void settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_SETTINGS);
}

static void about_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_ABOUT);
}

static lv_obj_t *small_button(lv_obj_t *parent, const char *text,
                              lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = ui_button(parent, text, cb);
    lv_obj_set_height(b, 34);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_radius(b, 11, 0);
    lv_obj_set_user_data(b, data);
    return b;
}

/* ---- 列表行左滑删除 ---- */
static lv_coord_t g_swipe_x0;
static bool g_swipe_tracking;

static void row_swipe_start(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    g_swipe_x0 = p.x;
    g_swipe_tracking = true;
}

static void row_swipe_cancel(lv_event_t *e)
{
    LV_UNUSED(e);
    g_swipe_tracking = false;
}

static void row_swipe_end(lv_event_t *e)
{
    if (!g_swipe_tracking) return;
    g_swipe_tracking = false;

    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    /* 只认左滑，而且要滑过约 1/3 行宽（466 屏上约 120px）。 */
    if (g_swipe_x0 - p.x < 120) return;

    lv_obj_t *row = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(row);
    if (!model_curve(id)) return;
    model_delete_curve(id);
    model_sync_sliders();
    model_checkpoint();
    ui_toast("已左滑删除");
    ui_refresh_later();
}

static void build_row(lv_obj_t *list, fd_curve *c)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 58);
    lv_obj_set_style_bg_color(row, lv_color_hex(FD_C_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(row, (void *)(intptr_t)c->fn.id);
    ui_set_click(row, row_open);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(model_color(c->fn.color_index)), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, -8);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, c->name);
    lv_obj_set_style_text_font(name, &font_cn_16, 0);
    lv_obj_set_style_text_color(name,
                                lv_color_hex(c->fn.visible ? FD_C_TEXT : FD_C_DIM), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 18, 2);

    char summary[FD_SRC_CAP * 2 + 16];
    if (model()->settings.complex_plane) {
        snprintf(summary, sizeof(summary), "%s", c->fn.src1);
    } else {
        fd_function_summary(&c->fn, summary, sizeof(summary));
    }
    lv_obj_t *expr = lv_label_create(row);
    lv_label_set_text(expr, summary);
    lv_label_set_long_mode(expr, LV_LABEL_LONG_DOT);
    lv_obj_set_width(expr, 190);
    lv_obj_set_style_text_font(expr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(expr, lv_color_hex(FD_C_DIM), 0);
    lv_obj_align(expr, LV_ALIGN_TOP_LEFT, 18, 24);

    lv_obj_t *vis = small_button(row, c->fn.visible ? "隐" : "显", row_visible,
                                 (void *)(intptr_t)c->fn.id);
    lv_obj_align(vis, LV_ALIGN_RIGHT_MID, -66, 0);

    lv_obj_t *del = small_button(row, "删", row_delete, (void *)(intptr_t)c->fn.id);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(del, lv_color_hex(0x3A2024), 0);
    lv_obj_t *dl = lv_obj_get_child(del, 0);
    if (dl) lv_obj_set_style_text_color(dl, lv_color_hex(FD_C_DANGER), 0);

    if (c->fn.id == model()->selected_id) {
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(FD_C_ACCENT), 0);
    }

    /* 长按直接进属性页 */
    lv_obj_set_user_data(row, (void *)(intptr_t)c->fn.id);
    lv_obj_add_event_cb(row, row_props, LV_EVENT_LONG_PRESSED, NULL);

    /* 左滑删除 */
    lv_obj_add_event_cb(row, row_swipe_start, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(row, row_swipe_end, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(row, row_swipe_cancel, LV_EVENT_PRESS_LOST, NULL);
}

/* ------------------------------------------------------------------ */
/* 参数滑动条                                                          */
/* ------------------------------------------------------------------ */

static void slider_value_text(fd_slider *s, char *buf, size_t cap)
{
    if (fabs(s->value - round(s->value)) < 1e-9) {
        snprintf(buf, cap, "%g", s->value);
    } else {
        snprintf(buf, cap, "%.2f", s->value);
    }
}

/** 把 0..FD_SLIDER_RANGE 的滑块位置换成参数值，并按 step 吸附。 */
static double slider_to_value(const fd_slider *s, int32_t pos)
{
    double t = (double)pos / (double)FD_SLIDER_RANGE;
    double v = s->min + t * (s->max - s->min);
    if (s->step > 0.0) v = round(v / s->step) * s->step;
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;
    return v;
}

static int32_t value_to_slider(const fd_slider *s)
{
    if (!(s->max > s->min)) return 0;
    double t = (s->value - s->min) / (s->max - s->min);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return (int32_t)lround(t * (double)FD_SLIDER_RANGE);
}

typedef struct {
    int id;
    lv_obj_t *value_label;
    lv_obj_t *play_button;
} slider_row_ctx;

static void slider_ctx_delete(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void slider_changed(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    slider_row_ctx *ctx = (slider_row_ctx *)lv_event_get_user_data(e);
    fd_slider *s = model_slider_by_id(ctx->id);
    if (!s) return;

    s->value = slider_to_value(s, lv_slider_get_value(sl));
    char buf[24];
    slider_value_text(s, buf, sizeof(buf));
    lv_label_set_text(ctx->value_label, buf);
}

static void slider_play_clicked(lv_event_t *e)
{
    slider_row_ctx *ctx = (slider_row_ctx *)lv_event_get_user_data(e);
    bool will_play = false;
    for (int i = 0; i < model()->slider_count; i++) {
        fd_slider *s = &model()->sliders[i];
        if (s->id == ctx->id) {
            will_play = !s->playing;
            s->playing = will_play;
        } else if (will_play) {
            s->playing = false; /* 同一时刻只播放一条 */
        }
    }
    /* 播放动画在绘图屏里跑，这里只切状态。 */
    ui_refresh_later();
}

static void slider_row_long_press(lv_event_t *e)
{
    slider_row_ctx *ctx = (slider_row_ctx *)lv_event_get_user_data(e);
    fd_doc *d = model();
    for (int i = 0; i < d->slider_count; i++) {
        if (d->sliders[i].id != ctx->id) continue;
        memmove(&d->sliders[i], &d->sliders[i + 1],
                sizeof(d->sliders[0]) * (size_t)(d->slider_count - i - 1));
        d->slider_count--;
        model_checkpoint();
        ui_toast("已删除滑动条");
        ui_refresh_later();
        return;
    }
}

static void build_slider_row(lv_obj_t *list, fd_slider *s, int content_w)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, content_w, 44);
    lv_obj_set_style_bg_color(row, lv_color_hex(FD_C_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, s->param);
    lv_obj_set_style_text_font(name, &font_cn_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

    /* 右侧固定：数值 62 + 播放 32 + 删除 30 + 三条缝 */
    const int right_reserved = 62 + 32 + 30 + 6 + 6 + 6;
    int slider_w = content_w - 16 - 22 - right_reserved;
    if (slider_w < 40) slider_w = 40;

    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_width(value, 62);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(FD_C_DIM), 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -(32 + 30 + 12), 0);

    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_width(sl, slider_w);
    lv_obj_set_height(sl, 8);
    lv_slider_set_range(sl, 0, FD_SLIDER_RANGE);
    lv_slider_set_value(sl, value_to_slider(s), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x2A3542), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(FD_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 5, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 22, 0);

    lv_obj_t *play = small_button(row, s->playing ? "停" : "播", NULL, NULL);
    lv_obj_set_size(play, 32, 30);
    lv_obj_set_style_pad_hor(play, 0, 0);
    lv_obj_align(play, LV_ALIGN_RIGHT_MID, -36, 0);

    lv_obj_t *del = small_button(row, "删", NULL, NULL);
    lv_obj_set_size(del, 30, 30);
    lv_obj_set_style_pad_hor(del, 0, 0);
    lv_obj_set_style_bg_color(del, lv_color_hex(0x3A2024), 0);
    lv_obj_t *dl = lv_obj_get_child(del, 0);
    if (dl) lv_obj_set_style_text_color(dl, lv_color_hex(FD_C_DANGER), 0);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 上下文挂在行上，行销毁时（LV_EVENT_DELETE）跟着释放。 */
    slider_row_ctx *ctx = (slider_row_ctx *)malloc(sizeof(*ctx));
    if (!ctx) return;
    ctx->id = s->id;
    ctx->value_label = value;
    ctx->play_button = play;
    lv_obj_add_event_cb(row, slider_ctx_delete, LV_EVENT_DELETE, ctx);

    char buf[24];
    slider_value_text(s, buf, sizeof(buf));
    lv_label_set_text(value, buf);

    lv_obj_add_event_cb(sl, slider_changed, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(play, slider_play_clicked, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(del, slider_row_long_press, LV_EVENT_LONG_PRESSED, ctx);
}

/* ------------------------------------------------------------------ */
/* 列表                                                                */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 示例库                                                              */
/* ------------------------------------------------------------------ */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    const char *label;
    fd_plot_mode mode;
    const char *src1;
    const char *src2; /**< 参数方程的第二个式子；NULL 表示没有 */
    double t_max;     /**< > 0 时覆盖默认 t 范围 */
} preset_def;

static const preset_def k_presets[] = {
        {"正弦波", FD_PLOT_EXPLICIT, "sin(x)", NULL, 0.0},
        {"抛物线", FD_PLOT_EXPLICIT, "x^2/4", NULL, 0.0},
        {"高斯钟形", FD_PLOT_EXPLICIT, "exp(-x^2/2)", NULL, 0.0},
        {"双曲线一支", FD_PLOT_EXPLICIT, "1/x", NULL, 0.0},
        {"绝对值", FD_PLOT_EXPLICIT, "|x|-2", NULL, 0.0},
        {"三次曲线", FD_PLOT_EXPLICIT, "x^3/3-x", NULL, 0.0},
        {"单位圆", FD_PLOT_IMPLICIT, "x^2+y^2=1", NULL, 0.0},
        {"双曲线", FD_PLOT_IMPLICIT, "x^2-y^2=1", NULL, 0.0},
        {"笛卡尔叶", FD_PLOT_IMPLICIT, "x^3+y^3=3xy", NULL, 0.0},
        {"摆线", FD_PLOT_PARAMETRIC, "t-sin(t)", "1-cos(t)", 6.0 * M_PI},
};

static void preset_clicked(lv_event_t *e)
{
    const preset_def *p = (const preset_def *)lv_event_get_user_data(e);
    if (!p) return;
    if (model()->curve_count >= FD_MAX_CURVES) {
        ui_toast("曲线数量已达上限");
        return;
    }

    int id = model_add_curve(p->mode);
    fd_curve *c = model_curve(id);
    if (!c) return;
    snprintf(c->fn.src1, sizeof(c->fn.src1), "%s", p->src1);
    if (p->src2) snprintf(c->fn.src2, sizeof(c->fn.src2), "%s", p->src2);
    if (p->t_max > 0.0) {
        c->fn.t_min = 0.0;
        c->fn.t_max = p->t_max;
    }
    model_sync_sliders();
    model_checkpoint();
    ui_toast("已添加示例");
    ui_refresh_later();
}

static void build_presets(lv_obj_t *list)
{
    lv_obj_t *title = ui_label(list, "示例", FD_C_DIM);
    lv_obj_set_style_pad_top(title, 8, 0);

    lv_obj_t *grid = lv_obj_create(list);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);

    int content_w = (int)ui_width() - 24;
    if (content_w < 200) content_w = 200;
    int btn_w = (content_w - 6) / 2;

    for (size_t i = 0; i < sizeof(k_presets) / sizeof(k_presets[0]); i++) {
        lv_obj_t *b = small_button(grid, k_presets[i].label, preset_clicked,
                                   (void *)&k_presets[i]);
        lv_obj_set_width(b, btn_w);
        lv_obj_set_height(b, 32);
        lv_obj_set_style_pad_hor(b, 4, 0);
    }
}

/* ------------------------------------------------------------------ */
/* 几何对象行                                                          */
/* ------------------------------------------------------------------ */

static const char *geo_type_label(fd_geo_kind k)
{
    switch (k) {
        case FD_GEO_POINT: return "点";
        case FD_GEO_SEGMENT: return "线段";
        case FD_GEO_LINE: return "直线";
        case FD_GEO_RAY: return "射线";
        case FD_GEO_ANGLE: return "角";
        case FD_GEO_POLYGON: return "面积";
        case FD_GEO_DISTANCE: return "距离";
        default: return "对象";
    }
}

static void geo_visible_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    fd_geo *g = model_geo(id);
    if (!g) return;
    g->visible = !g->visible;
    model_checkpoint();
    ui_refresh_later();
}

static void geo_delete_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (!model_geo(id)) return;
    model_delete_geo(id);
    model_checkpoint();
    ui_toast("已删除");
    ui_refresh_later();
}

static void geo_info_text(const fd_geo *g, char *buf, size_t cap)
{
    switch (g->kind) {
        case FD_GEO_DISTANCE:
            snprintf(buf, cap, "距离 %.3g", geo_distance(g->p[0], g->p[1]));
            break;
        case FD_GEO_ANGLE:
            snprintf(buf, cap, "角 %.1f°", geo_angle_deg(g->p[0], g->p[1], g->p[2]));
            break;
        case FD_GEO_POLYGON:
            snprintf(buf, cap, "面积 %.3g",
                     fabs(geo_polygon_area((const double(*)[2])g->p, g->npts)));
            break;
        default:
            snprintf(buf, cap, "%s", geo_type_label(g->kind));
            break;
    }
}

static void build_geo_row(lv_obj_t *list, const fd_geo *g)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 46);
    lv_obj_set_style_bg_color(row, lv_color_hex(FD_C_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(model_color(g->color_index)), 0);
    lv_obj_set_style_bg_opa(dot, g->visible ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, g->name);
    lv_obj_set_style_text_font(name, &font_cn_16, 0);
    lv_obj_set_style_text_color(name,
                                lv_color_hex(g->visible ? FD_C_TEXT : FD_C_DIM), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 18, 0);

    char info[40];
    geo_info_text(g, info, sizeof(info));
    lv_obj_t *desc = lv_label_create(row);
    lv_label_set_text(desc, info);
    lv_obj_set_style_text_font(desc, &font_cn_14, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(FD_C_DIM), 0);
    lv_obj_align(desc, LV_ALIGN_LEFT_MID, 58, 0);

    lv_obj_t *vis = small_button(row, g->visible ? "隐" : "显", geo_visible_clicked,
                                 (void *)(intptr_t)g->id);
    lv_obj_set_size(vis, 48, 30);
    lv_obj_set_style_pad_hor(vis, 0, 0);
    lv_obj_align(vis, LV_ALIGN_RIGHT_MID, -58, 0);

    lv_obj_t *del = small_button(row, "删", geo_delete_clicked,
                                 (void *)(intptr_t)g->id);
    lv_obj_set_size(del, 30, 30);
    lv_obj_set_style_pad_hor(del, 0, 0);
    lv_obj_set_style_bg_color(del, lv_color_hex(0x3A2024), 0);
    lv_obj_t *dl = lv_obj_get_child(del, 0);
    if (dl) lv_obj_set_style_text_color(dl, lv_color_hex(FD_C_DANGER), 0);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
}

lv_obj_t *ui_list_create(lv_obj_t *parent)
{
    fd_doc *d = model();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 8, 0);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), 40);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "函数列表");
    lv_obj_set_style_text_font(title, &font_cn_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *back = small_button(head, "返回", back_clicked, NULL);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *add = small_button(head, "新建", add_clicked, NULL);
    lv_obj_align(add, LV_ALIGN_RIGHT_MID, -72, 0);
    lv_obj_set_style_bg_color(add, lv_color_hex(0x18323C), 0);
    lv_obj_t *al = lv_obj_get_child(add, 0);
    if (al) lv_obj_set_style_text_color(al, lv_color_hex(FD_C_ACCENT), 0);

    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    if (d->curve_count == 0) {
        lv_obj_t *empty = ui_label(list, "还没有曲线，点「新建」加一条", FD_C_DIM);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_top(empty, 30, 0);
    }

    for (int i = 0; i < d->curve_count; i++) {
        build_row(list, &d->curves[i]);
    }

    if (d->geo_count > 0) {
        lv_obj_t *sep = ui_label(list, "图形", FD_C_DIM);
        lv_obj_set_style_pad_top(sep, 6, 0);
        for (int i = 0; i < d->geo_count; i++) {
            build_geo_row(list, &d->geos[i]);
        }
    }

    if (d->slider_count > 0) {
        lv_obj_t *sep = ui_label(list, "参数", FD_C_DIM);
        lv_obj_set_style_pad_top(sep, 6, 0);

        int content_w = (int)ui_width() - 24;
        if (content_w < 200) content_w = 200;
        for (int i = 0; i < d->slider_count; i++) {
            build_slider_row(list, &d->sliders[i], content_w);
        }
    }

    if (d->settings.show_presets) build_presets(list);

    lv_obj_t *foot = lv_obj_create(root);
    lv_obj_remove_style_all(foot);
    lv_obj_set_size(foot, LV_PCT(100), 36);
    lv_obj_remove_flag(foot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    small_button(foot, "存档", archive_clicked, NULL);
    small_button(foot, "设置", settings_clicked, NULL);
    small_button(foot, "关于", about_clicked, NULL);

    return root;
}
