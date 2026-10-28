/** 欢迎页与分步引导，见 ui_tour.h。 */
#include "ui_tour.h"

#include <stdio.h>
#include <string.h>

#include "model.h"
#include "ui.h"

static lv_obj_t *g_tour_mask;
static int g_step = -1;
/* 本步是否已经「动手点过」高亮区域（只有 require_tap 的步骤才需要）。 */
static bool g_step_armed;
static lv_area_t g_hi_area;

static void build_step(void);

/** 4 步引导的文案与目标区域（按 466 基准，UI_PX 换算）。 */
typedef struct {
    const char *title;
    const char *body;
    int screen;
    int x, y, w, h; /**< 高亮框，0 宽高表示整屏 */
    bool require_tap; /**< true = 必须点一下高亮区域才能进下一步 */
} tour_step;

static const tour_step k_steps[] = {
        {"绘图区", "单指拖动平移，双指捏合缩放，双击复位视野。\n旋转表冠也能缩放。",
         UI_SCR_GRAPH, 0, 96, 0, -80, false},
        {"顶部工具条", "右上角「工具」里是 16 种作图工具，\n旁边的按钮分别进函数列表、设置和存档。",
         UI_SCR_GRAPH, 0, 0, 0, 96, false},
        {"函数列表", "列表里管理曲线和参数滑动条，\n点行进入编辑器，左滑删除。\n点一下上方的列表区域继续。",
         UI_SCR_LIST, 12, 50, -12, -60, true},
        {"设置", "网格、坐标轴、角度模式、复平面、\n采样质量都在这里，随时可以重看这个引导。\n点一下上方的设置区域继续。",
         UI_SCR_SETTINGS, 12, 50, -12, -100, true},
};

static const int k_step_count = (int)(sizeof(k_steps) / sizeof(k_steps[0]));

static void mark_done(void)
{
    if (!model()->settings.tutorial_done) {
        model()->settings.tutorial_done = true;
        model_checkpoint();
        model_save();
    }
}

static void close_mask(void)
{
    if (g_tour_mask) {
        lv_obj_delete(g_tour_mask);
        g_tour_mask = NULL;
    }
    g_step = -1;
}

void ui_tour_finish(void)
{
    close_mask();
    mark_done();
    ui_show(UI_SCR_GRAPH);
}

bool ui_tour_active(void) { return g_tour_mask != NULL; }

static void step_skip(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_tour_finish();
}

static void step_next(lv_event_t *e);

/** 点遮罩：落在高亮框里才算「动手点过」，然后重建说明卡让按钮可用。 */
static void mask_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_step < 0 || g_step >= k_step_count) return;
    if (g_step_armed) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (p.x < g_hi_area.x1 || p.x > g_hi_area.x2) return;
    if (p.y < g_hi_area.y1 || p.y > g_hi_area.y2) return;
    g_step_armed = true;
    build_step();
}

/** 切屏是延迟执行的，等它落定后再重新盖遮罩。 */
static void tour_rebuild_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    build_step();
}

static void build_step(void)
{
    if (g_step < 0 || g_step >= k_step_count) return;
    const tour_step *st = &k_steps[g_step];

    if (g_tour_mask) {
        lv_obj_delete(g_tour_mask);
        g_tour_mask = NULL;
    }

    lv_coord_t w = ui_width();
    lv_coord_t h = ui_height();
    lv_obj_t *mask = lv_obj_create(lv_layer_top());
    g_tour_mask = mask;
    lv_obj_remove_style_all(mask);
    lv_obj_set_size(mask, w, h);
    lv_obj_set_pos(mask, 0, 0);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mask, mask_clicked, LV_EVENT_CLICKED, NULL);

    /* 高亮框：只在遮罩上描一圈亮边，不做真正的挖洞（LVGL 没有遮罩挖洞）。 */
    lv_obj_t *hi = lv_obj_create(mask);
    lv_obj_remove_style_all(hi);
    lv_coord_t hw = st->w == 0 ? w - UI_PX(8) : w + UI_PX(st->w);
    lv_coord_t hh = st->h < 0 ? h + UI_PX(st->h) - UI_PX(st->y)
                              : (st->h == 0 ? h : UI_PX(st->h));
    if (hw < 1) hw = 1;
    if (hh < 1) hh = 1;
    lv_obj_set_size(hi, hw, hh);
    lv_obj_set_pos(hi, UI_PX(st->x), UI_PX(st->y));
    /* 记下高亮区域（屏幕坐标），供 mask_clicked 判断「点到了没有」。 */
    g_hi_area.x1 = UI_PX(st->x);
    g_hi_area.y1 = UI_PX(st->y);
    g_hi_area.x2 = (lv_coord_t)(g_hi_area.x1 + hw - 1);
    g_hi_area.y2 = (lv_coord_t)(g_hi_area.y1 + hh - 1);
    /* 不需要动手的步骤直接算「已点过」；需要动手的保留当前状态（重建时不能清掉）。 */
    if (!st->require_tap) g_step_armed = true;
    lv_obj_set_style_bg_opa(hi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hi, 2, 0);
    lv_obj_set_style_border_color(hi, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_set_style_radius(hi, UI_PX(18), 0);
    lv_obj_remove_flag(hi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(hi, LV_OBJ_FLAG_CLICKABLE);

    /* 说明卡：底部一张面板，标题 + 正文 + 跳过/下一步。 */
    lv_obj_t *card = ui_panel(mask, w - UI_PX(24), UI_PX(150));
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -UI_PX(14));
    lv_obj_set_style_pad_all(card, UI_PX(12), 0);

    char title[64];
    snprintf(title, sizeof(title), "第 %d/%d 步 · %s", g_step + 1, k_step_count,
             st->title);
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, ui_font_body(), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *b = lv_label_create(card);
    lv_label_set_text(b, st->body);
    lv_obj_set_width(b, LV_PCT(100));
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(b, ui_font_small(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 0, UI_PX(28));

    lv_obj_t *skip = ui_button(card, "跳过", step_skip);
    lv_obj_set_size(skip, UI_PX(66), UI_PX(32));
    lv_obj_align(skip, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    const char *next_txt = !g_step_armed ? "先点高亮区"
                                         : (g_step == k_step_count - 1 ? "完成" : "下一步");
    lv_obj_t *next = ui_button(card, next_txt, step_next);
    lv_obj_set_size(next, UI_PX(92), UI_PX(32));
    lv_obj_set_style_bg_color(next, lv_color_hex(0x18323C), 0);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *nl = lv_obj_get_child(next, 0);
    if (nl) lv_obj_set_style_text_color(nl, lv_color_hex(FD_C_ACCENT), 0);
}

static void step_next(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!g_step_armed) {
        ui_toast("先点一下高亮区域");
        return;
    }
    if (g_step + 1 >= k_step_count) {
        ui_tour_finish();
        return;
    }
    g_step++;
    g_step_armed = false; /* 新的一步重新计「动手」 */
    ui_show((ui_screen)k_steps[g_step].screen);
    lv_timer_t *t = lv_timer_create(tour_rebuild_cb, 40, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void ui_tour_start(void)
{
    close_mask();
    g_step = 0;
    g_step_armed = false;
    ui_show(UI_SCR_GRAPH);
    lv_timer_t *t = lv_timer_create(tour_rebuild_cb, 40, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ------------------------------------------------------------------ */
/* 欢迎页                                                              */
/* ------------------------------------------------------------------ */

static void welcome_start(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_tour_start();
}

static void welcome_skip(lv_event_t *e)
{
    LV_UNUSED(e);
    mark_done();
    ui_show(UI_SCR_GRAPH);
}

lv_obj_t *ui_welcome_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(FD_C_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, UI_PX(18), 0);
    lv_obj_set_style_pad_row(root, UI_PX(10), 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部小徽标：一个圆 + 曲线示意，纯 LVGL 控件画。 */
    lv_obj_t *badge = lv_obj_create(root);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, UI_PX(76), UI_PX(76));
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x12202A), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wave = lv_label_create(badge);
    lv_label_set_text(wave, "≈");
    lv_obj_set_style_text_font(wave, ui_font_title(), 0);
    lv_obj_set_style_text_color(wave, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_center(wave);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "FunctionDrawer");
    lv_obj_set_style_text_font(title, ui_font_title(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_TEXT), 0);

    lv_obj_t *sub = lv_label_create(root);
    lv_label_set_text(sub, "手表上的函数图像绘制器");
    lv_obj_set_style_text_font(sub, ui_font_small(), 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(FD_C_DIM), 0);

    const char *bullets[3] = {
            "· 显函数 / 极坐标 / 参数方程 / 隐函数",
            "· 16 种几何工具，可代入表达式",
            "· 复平面、参数滑动条、PNG 导出",
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *l = lv_label_create(root);
        lv_label_set_text(l, bullets[i]);
        lv_obj_set_style_text_font(l, ui_font_small(), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(FD_C_DIM), 0);
    }

    lv_obj_t *spacer = lv_obj_create(root);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, UI_PX(6));
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);

    lv_obj_t *start = ui_button(root, "开始使用", welcome_start);
    lv_obj_set_size(start, UI_PX(180), UI_PX(40));
    lv_obj_set_style_bg_color(start, lv_color_hex(0x18323C), 0);
    lv_obj_t *sl = lv_obj_get_child(start, 0);
    if (sl) lv_obj_set_style_text_color(sl, lv_color_hex(FD_C_ACCENT), 0);

    lv_obj_t *skip = ui_button(root, "跳过", welcome_skip);
    lv_obj_set_size(skip, UI_PX(120), UI_PX(32));
    lv_obj_set_style_bg_opa(skip, LV_OPA_TRANSP, 0);
    lv_obj_t *kl = lv_obj_get_child(skip, 0);
    if (kl) lv_obj_set_style_text_color(kl, lv_color_hex(FD_C_DIM), 0);

    return root;
}
