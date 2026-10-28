#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "model.h"
#include "app.h"
#include "ui_tour.h"

static lv_obj_t *g_root;
static ui_screen g_screen = UI_SCR_GRAPH;
static lv_coord_t g_w = 466;
static lv_coord_t g_h = 466;

/* 预创建界面：屏幕容器常驻，只有内容变化（g_gen 变了）才重建。
 * 但最多只留最近用过的 2 屏（LRU），其余销毁——7 个屏全常驻会把 LVGL 堆吃满，
 * 而在小屏上真正来回切的通常只有「列表 ↔ 绘图/编辑」这两屏。 */
#define FD_SCREEN_CACHE 2
static lv_obj_t *g_screen_obj[UI_SCREEN_COUNT];
static unsigned g_screen_gen[UI_SCREEN_COUNT];
static ui_screen g_cache[FD_SCREEN_CACHE]; /**< 最近用过的屏，[0] 最新 */
static int g_cache_n;
static unsigned g_gen;

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

/* 现有字库的 5 个字号，按屏幕短边选档。 */
static const lv_font_t *const k_fonts[5] = {
        &font_cn_14, &font_cn_16, &font_cn_20, &font_cn_24, &font_cn_28,
};
static const int k_font_sizes[5] = {14, 16, 20, 24, 28};

/** 离目标像素最近的一档字号下标。 */
static int nearest_font_idx(float want)
{
    int best = 0;
    float best_d = 1e9f;
    for (int i = 0; i < 5; i++) {
        float d = (float)k_font_sizes[i] - want;
        if (d < 0) d = -d;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

const lv_font_t *ui_font_small(void)
{
    /* 小/正文/标题按 14/16/20 起步，随 scale 一起变大，并保持严格递增。 */
    int t = nearest_font_idx(20.0f * ui_scale());
    int b = nearest_font_idx(16.0f * ui_scale());
    int s = nearest_font_idx(14.0f * ui_scale());
    if (b > t - 1) b = t - 1;
    if (s > b - 1) s = b - 1;
    if (b < 1) b = 1;
    if (s < 0) s = 0;
    return k_fonts[s];
}

const lv_font_t *ui_font_body(void)
{
    int t = nearest_font_idx(20.0f * ui_scale());
    int b = nearest_font_idx(16.0f * ui_scale());
    if (b > t - 1) b = t - 1;
    if (b < 1) b = 1;
    return k_fonts[b];
}

const lv_font_t *ui_font_title(void)
{
    return k_fonts[nearest_font_idx(20.0f * ui_scale())];
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
        case UI_SCR_WELCOME: return ui_welcome_create(parent);
        default: return NULL;
    }
}

/**
 * 建/重建某个屏幕的容器内容。
 * 重建前先 detach 绘图屏的静态指针，避免 lv_obj_clean 之后悬空。
 */
static void build_screen_into(ui_screen s)
{
    if (!g_root) return;
    lv_obj_t *c = g_screen_obj[s];
    if (c) {
        if (s == UI_SCR_GRAPH) ui_graph_detach();
        lv_obj_clean(c);
    } else {
        c = lv_obj_create(g_root);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        g_screen_obj[s] = c;
    }
    build_screen(s, c);
    g_screen_gen[s] = g_gen;
}

/** 销毁某屏的容器；绘图屏先 detach 静态指针，避免 lv_obj_delete 后悬空。 */
static void evict_screen(ui_screen s)
{
    if (!g_screen_obj[s]) return;
    if (s == UI_SCR_GRAPH) ui_graph_detach();
    lv_obj_delete(g_screen_obj[s]);
    g_screen_obj[s] = NULL;
    g_screen_gen[s] = 0;
}

/** 把 s 提到 LRU 队首；队满时淘汰队尾那一屏。 */
static void cache_touch(ui_screen s)
{
    int pos = -1;
    for (int i = 0; i < g_cache_n; i++) {
        if (g_cache[i] == s) {
            pos = i;
            break;
        }
    }

    if (pos >= 0) {
        for (int i = pos; i > 0; i--) g_cache[i] = g_cache[i - 1];
        g_cache[0] = s;
        return;
    }

    if (g_cache_n == FD_SCREEN_CACHE) {
        evict_screen(g_cache[FD_SCREEN_CACHE - 1]);
        g_cache_n--;
    }
    for (int i = g_cache_n; i > 0; i--) g_cache[i] = g_cache[i - 1];
    g_cache[0] = s;
    g_cache_n++;
}

static void show_screen(ui_screen s)
{
    g_screen = s;
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        if (g_screen_obj[i]) lv_obj_add_flag(g_screen_obj[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!g_screen_obj[s] || g_screen_gen[s] != g_gen) build_screen_into(s);
    if (g_screen_obj[s]) lv_obj_remove_flag(g_screen_obj[s], LV_OBJ_FLAG_HIDDEN);
    cache_touch(s);
}

void ui_refresh(void)
{
    if (g_refresh_timer) {
        lv_timer_delete(g_refresh_timer);
        g_refresh_timer = NULL;
    }
    g_gen++;
    build_screen_into(g_screen);
    if (g_screen_obj[g_screen]) {
        lv_obj_remove_flag(g_screen_obj[g_screen], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    g_refresh_timer = NULL;
    ui_refresh();
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
    show_screen(g_pending_screen);
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

static void glass_style(lv_obj_t *b);

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
    lv_obj_set_style_text_font(b, ui_font_body(), 0);
    glass_style(b);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(l, ui_font_body(), 0);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

/* ------------------------------------------------------------------ */
/* 毛玻璃 + 长按提示圈                                                  */
/* ------------------------------------------------------------------ */

/** 半透明 + 细亮边 + 极淡投影，做出「磨砂玻璃」的观感。 */
static void glass_style(lv_obj_t *b)
{
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x3E4C5C), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(b, UI_PX(8), 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(b, UI_PX(2), 0);
}

static lv_obj_t *g_ring;
static lv_timer_t *g_ring_timer;
static uint32_t g_ring_t0;

static void ring_stop(void)
{
    if (g_ring_timer) {
        lv_timer_delete(g_ring_timer);
        g_ring_timer = NULL;
    }
    if (g_ring) {
        lv_obj_delete(g_ring);
        g_ring = NULL;
    }
}

static void ring_timer_cb(lv_timer_t *t)
{
    if (!g_ring) {
        lv_timer_delete(t);
        g_ring_timer = NULL;
        return;
    }
    uint32_t el = lv_tick_elaps(g_ring_t0);
    int a = (int)((el * 360u) / 700u);
    if (a > 360) a = 360;
    lv_arc_set_end_angle(g_ring, a);
    if (a >= 360) {
        lv_timer_delete(t);
        g_ring_timer = NULL;
    }
}

static void ring_pressed(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    ring_stop();

    lv_area_t a;
    lv_obj_get_coords(btn, &a);
    int pad = UI_PX(5);
    lv_obj_t *arc = lv_arc_create(lv_layer_top());
    g_ring = arc;
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, (a.x2 - a.x1 + 1) + pad * 2, (a.y2 - a.y1 + 1) + pad * 2);
    lv_obj_set_pos(arc, a.x1 - pad, a.y1 - pad);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, UI_PX(3), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2E3C4A), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, UI_PX(3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(FD_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    g_ring_t0 = lv_tick_get();
    g_ring_timer = lv_timer_create(ring_timer_cb, 33, NULL);
}

static void ring_released(lv_event_t *e)
{
    LV_UNUSED(e);
    ring_stop();
}

/** 图标键按下时绕一圈动画，松手 / 长按触发后收掉。 */
static void ring_attach(lv_obj_t *b)
{
    lv_obj_add_event_cb(b, ring_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(b, ring_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(b, ring_released, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(b, ring_released, LV_EVENT_LONG_PRESSED, NULL);
}

lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, UI_PX(42), UI_PX(42));
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_C_PANEL2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_text_font(b, ui_font_body(), 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(l, ui_font_body(), 0);
    lv_obj_center(l);
    glass_style(b);
    ring_attach(b);

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
    lv_obj_set_style_text_color(g_root, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_font(g_root, ui_font_body(), 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);

    memset(g_screen_obj, 0, sizeof(g_screen_obj));
    memset(g_screen_gen, 0, sizeof(g_screen_gen));
    g_gen = 0;

    /* 首次启动先看欢迎页，之后直接进绘图屏。 */
    show_screen(model()->settings.tutorial_done ? UI_SCR_GRAPH : UI_SCR_WELCOME);
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
    memset(g_screen_obj, 0, sizeof(g_screen_obj));
    memset(g_screen_gen, 0, sizeof(g_screen_gen));
    g_cache_n = 0;
}

/* ------------------------------------------------------------------ */
/* 输入                                                                */
/* ------------------------------------------------------------------ */

void ui_touch(int action, int count, float x0, float y0, float x1, float y1)
{
    if (ui_tour_active()) return; /* 引导遮罩期间不让绘图手势穿透 */
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
