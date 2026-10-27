#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "model.h"
#include "app.h"

static lv_obj_t *g_root;
static ui_screen g_screen = UI_SCR_GRAPH;
static lv_coord_t g_w = 466;
static lv_coord_t g_h = 466;

static ui_screen g_pending_screen;
static bool g_pending_switch;
static lv_timer_t *g_refresh_timer;
static lv_obj_t *g_toast_obj;
static lv_timer_t *g_toast_timer;

/* ------------------------------------------------------------------ */
/* 基础                                                                */
/* ------------------------------------------------------------------ */

lv_obj_t *ui_root(void) { return g_root; }
lv_coord_t ui_width(void) { return g_w; }
lv_coord_t ui_height(void) { return g_h; }
ui_screen ui_current(void) { return g_screen; }

float ui_scale(void)
{
    float s = (float)(g_w < g_h ? g_w : g_h) / 466.0f;
    if (s < 0.75f) s = 0.75f;
    if (s > 2.2f) s = 2.2f;
    return s;
}

lv_coord_t ui_px(int v)
{
    float s = ui_scale();
    return (lv_coord_t)((float)v * s + 0.5f);
}

const lv_font_t *ui_font_small(void)
{
    return ui_scale() >= 1.35f ? &font_cn_16 : &font_cn_14;
}

const lv_font_t *ui_font_body(void)
{
    if (ui_scale() >= 1.35f) return &font_cn_20;
    if (ui_scale() >= 0.95f) return &font_cn_16;
    return &font_cn_14;
}

const lv_font_t *ui_font_title(void)
{
    return &font_cn_20;
}

static lv_obj_t *build_screen(ui_screen s, lv_obj_t *parent)
{
    switch (s) {
        case UI_SCR_GRAPH: return ui_graph_create(parent);
        case UI_SCR_LIST: return ui_list_create(parent);
        case UI_SCR_EDIT: return ui_editor_create(parent);
        case UI_SCR_PROPS: return ui_props_create(parent);
        case UI_SCR_ARCHIVE: return ui_archive_create(parent);
        case UI_SCR_SETTINGS: return ui_settings_create(parent);
        case UI_SCR_ABOUT: return ui_about_create(parent);
        default: return NULL;
    }
}

static void build_current(void)
{
    if (!g_root) return;
    /* 绘图屏的 LVGL 对象马上会被销毁，先断开静态引用，避免悬空。 */
    ui_graph_detach();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(FD_C_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_root, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(g_root, ui_font_body(), 0);
    build_screen(g_screen, g_root);
}

void ui_refresh(void)
{
    if (g_refresh_timer) {
        lv_timer_delete(g_refresh_timer);
        g_refresh_timer = NULL;
    }
    build_current();
}

static void refresh_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    g_refresh_timer = NULL;
    build_current();
}

void ui_refresh_later(void)
{
    if (g_refresh_timer) return;
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 24, NULL);
    lv_timer_set_repeat_count(g_refresh_timer, 1);
}

static void pending_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    if (!g_pending_switch) return;
    g_pending_switch = false;
    g_screen = g_pending_screen;
    build_current();
}

void ui_show(ui_screen s)
{
    g_pending_screen = s;
    g_pending_switch = true;
    lv_timer_t *t = lv_timer_create(pending_timer_cb, 16, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ------------------------------------------------------------------ */
/* Toast                                                               */
/* ------------------------------------------------------------------ */

static void toast_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    g_toast_timer = NULL;
    if (g_toast_obj) {
        lv_obj_delete(g_toast_obj);
        g_toast_obj = NULL;
    }
}

void ui_toast(const char *msg)
{
    if (!g_root || !msg) return;
    if (g_toast_obj) {
        lv_obj_delete(g_toast_obj);
        g_toast_obj = NULL;
    }
    if (g_toast_timer) {
        lv_timer_delete(g_toast_timer);
        g_toast_timer = NULL;
    }

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    g_toast_obj = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x2A3542), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, UI_PX(18), 0);
    lv_obj_set_style_pad_hor(box, UI_PX(18), 0);
    lv_obj_set_style_pad_ver(box, UI_PX(10), 0);
    lv_obj_set_style_text_font(box, ui_font_body(), 0);
    lv_obj_set_width(box, LV_PCT(78));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -UI_PX(46));

    lv_obj_t *l = lv_label_create(box);
    lv_label_set_text(l, msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_center(l);

    g_toast_timer = lv_timer_create(toast_timer_cb, 2200, NULL);
    lv_timer_set_repeat_count(g_toast_timer, 1);
}

/* ------------------------------------------------------------------ */
/* 通用控件                                                            */
/* ------------------------------------------------------------------ */

void ui_set_click(lv_obj_t *obj, lv_event_cb_t cb)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *ui_panel(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, lv_color_hex(FD_C_PANEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, UI_PX(18), 0);
    lv_obj_set_style_pad_all(p, UI_PX(10), 0);
    lv_obj_set_style_text_font(p, ui_font_body(), 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, ui_font_body(), 0);
    return l;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, UI_PX(44));
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_C_PANEL2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, UI_PX(14), 0);
    lv_obj_set_style_pad_hor(b, UI_PX(14), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_text_font(b, ui_font_body(), 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(l, ui_font_body(), 0);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, UI_PX(42), UI_PX(42));
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_C_PANEL2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_text_font(b, ui_font_body(), 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(l, ui_font_body(), 0);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *text, bool value,
                        lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, UI_PX(46));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(row, UI_PX(4), 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(l, ui_font_body(), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, UI_PX(46), UI_PX(26));
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x34404D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(FD_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    if (value) lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return row;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */

void ui_init(void)
{
    g_root = lv_screen_active();
    lv_display_t *disp = lv_display_get_default();
    g_w = (lv_coord_t)lv_display_get_horizontal_resolution(disp);
    g_h = (lv_coord_t)lv_display_get_vertical_resolution(disp);
    lv_obj_set_size(g_root, g_w, g_h);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(FD_C_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(g_root, ui_font_body(), 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    build_current();
}

void ui_deinit(void)
{
    if (g_refresh_timer) {
        lv_timer_delete(g_refresh_timer);
        g_refresh_timer = NULL;
    }
    if (g_toast_timer) {
        lv_timer_delete(g_toast_timer);
        g_toast_timer = NULL;
    }
    g_toast_obj = NULL;
    g_root = NULL;
    g_pending_switch = false;
}

/* ------------------------------------------------------------------ */
/* 输入                                                                */
/* ------------------------------------------------------------------ */

void ui_touch(int action, int count, float x0, float y0, float x1, float y1)
{
    if (g_screen == UI_SCR_GRAPH) {
        ui_graph_touch(action, count, x0, y0, x1, y1);
    }
}

void ui_crown(float delta)
{
    if (g_screen == UI_SCR_GRAPH) {
        ui_graph_crown(delta);
    }
}

bool ui_key(int code)
{
    /* 4 = KEYCODE_BACK */
    if (code == 4) {
        switch (g_screen) {
            case UI_SCR_GRAPH:
                return false;
            case UI_SCR_EDIT:
            case UI_SCR_PROPS:
                ui_show(UI_SCR_LIST);
                return true;
            default:
                ui_show(UI_SCR_GRAPH);
                return true;
        }
    }
    return false;
}
