/** 设置页。 */
#include "ui.h"

#include <stdint.h>

#include "model.h"

static void back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_GRAPH);
}

static void grid_changed(lv_event_t *e)
{
    model()->settings.show_grid = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    model_checkpoint();
    ui_graph_invalidate();
}

static void axes_changed(lv_event_t *e)
{
    model()->settings.show_axes = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    model_checkpoint();
    ui_graph_invalidate();
}

static void numbers_changed(lv_event_t *e)
{
    model()->settings.show_numbers = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    model_checkpoint();
    ui_graph_invalidate();
}

static void degrees_changed(lv_event_t *e)
{
    model()->settings.angle_degrees =
            lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    model_checkpoint();
    ui_graph_invalidate();
}

static void crown_changed(lv_event_t *e)
{
    model()->settings.crown_zoom = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    model_checkpoint();
}

static void quality_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    model()->settings.quality = (int)(intptr_t)lv_obj_get_user_data(btn);
    model_checkpoint();
    ui_graph_invalidate();
    ui_refresh_later();
}

static void reset_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    model_reset_view();
    ui_graph_invalidate();
    ui_toast("视图已复位");
}

static void autosave_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model_save()) {
        ui_toast("已保存");
    } else {
        ui_toast("保存失败");
    }
}

static lv_obj_t *chip(lv_obj_t *parent, const char *text, bool on,
                      lv_event_cb_t cb, int data)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 78, 34);
    lv_obj_set_style_bg_color(b, lv_color_hex(on ? 0x1B3B48 : FD_C_PANEL2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 11, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_user_data(b, (void *)(intptr_t)data);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_cn_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(on ? FD_C_ACCENT : FD_C_DIM), 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    fd_settings *st = &model()->settings;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 6, 0);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), 38);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "设置");
    lv_obj_set_style_text_font(title, &font_cn_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *back = ui_button(head, "返回", back_clicked);
    lv_obj_set_size(back, 62, 34);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);

    ui_switch_row(root, "显示网格", st->show_grid, grid_changed);
    ui_switch_row(root, "显示坐标轴", st->show_axes, axes_changed);
    ui_switch_row(root, "显示刻度数值", st->show_numbers, numbers_changed);
    ui_switch_row(root, "三角函数按角度", st->angle_degrees, degrees_changed);
    ui_switch_row(root, "表冠缩放", st->crown_zoom, crown_changed);

    lv_obj_t *qrow = lv_obj_create(root);
    lv_obj_remove_style_all(qrow);
    lv_obj_set_size(qrow, LV_PCT(100), 40);
    lv_obj_remove_flag(qrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(qrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(qrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ql = lv_label_create(qrow);
    lv_label_set_text(ql, "采样质量");
    lv_obj_set_style_text_font(ql, &font_cn_16, 0);
    lv_obj_set_style_text_color(ql, lv_color_hex(FD_C_DIM), 0);

    lv_obj_t *qchips = lv_obj_create(qrow);
    lv_obj_remove_style_all(qchips);
    lv_obj_set_size(qchips, LV_SIZE_CONTENT, 40);
    lv_obj_remove_flag(qchips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(qchips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(qchips, 6, 0);
    chip(qchips, "快", st->quality == FD_QUALITY_FAST, quality_clicked, FD_QUALITY_FAST);
    chip(qchips, "中", st->quality == FD_QUALITY_NORMAL, quality_clicked,
         FD_QUALITY_NORMAL);
    chip(qchips, "高", st->quality == FD_QUALITY_HIGH, quality_clicked,
         FD_QUALITY_HIGH);

    lv_obj_t *foot = lv_obj_create(root);
    lv_obj_remove_style_all(foot);
    lv_obj_set_size(foot, LV_PCT(100), 40);
    lv_obj_remove_flag(foot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *reset = ui_button(foot, "复位视图", reset_clicked);
    lv_obj_set_size(reset, 130, 36);

    lv_obj_t *save = ui_button(foot, "保存", autosave_clicked);
    lv_obj_set_size(save, 100, 36);
    lv_obj_set_style_bg_color(save, lv_color_hex(0x18323C), 0);
    lv_obj_t *sl = lv_obj_get_child(save, 0);
    if (sl) lv_obj_set_style_text_color(sl, lv_color_hex(FD_C_ACCENT), 0);

    return root;
}
