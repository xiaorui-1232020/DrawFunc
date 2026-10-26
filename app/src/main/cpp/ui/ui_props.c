/** 曲线属性：颜色、线宽、线型、显隐、角度模式，以及撤销/重做/删除。 */
#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "model.h"

static int g_id;

static fd_curve *cur(void)
{
    return model_curve(g_id);
}

static void back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_LIST);
}

static void color_clicked(lv_event_t *e)
{
    lv_obj_t *dot = lv_event_get_current_target(e);
    fd_curve *c = cur();
    if (!c) return;
    c->fn.color_index = (int)(intptr_t)lv_obj_get_user_data(dot);
    model_checkpoint();
    ui_refresh_later();
}

static void width_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    fd_curve *c = cur();
    if (!c) return;
    c->width = (int)(intptr_t)lv_obj_get_user_data(btn);
    model_checkpoint();
    ui_graph_invalidate();
    ui_refresh_later();
}

static void dash_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    fd_curve *c = cur();
    if (!c) return;
    c->dash = (int)(intptr_t)lv_obj_get_user_data(btn);
    model_checkpoint();
    ui_refresh_later();
}

static void angle_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    fd_curve *c = cur();
    if (!c) return;
    c->fn.degrees = (int)(intptr_t)lv_obj_get_user_data(btn);
    model_checkpoint();
    ui_refresh_later();
}

static void visible_changed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    fd_curve *c = cur();
    if (!c) return;
    c->fn.visible = lv_obj_has_state(sw, LV_STATE_CHECKED);
    model_checkpoint();
    ui_refresh_later();
}

static void undo_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model_undo()) {
        ui_toast("已撤销");
        ui_graph_invalidate();
        ui_refresh_later();
    } else {
        ui_toast("没有可撤销的操作");
    }
}

static void redo_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model_redo()) {
        ui_toast("已重做");
        ui_graph_invalidate();
        ui_refresh_later();
    } else {
        ui_toast("没有可重做的操作");
    }
}

static void delete_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!cur()) return;
    model_delete_curve(g_id);
    model_sync_sliders();
    model_checkpoint();
    ui_toast("已删除");
    ui_show(UI_SCR_LIST);
}

static lv_obj_t *chip(lv_obj_t *parent, const char *text, bool on,
                      lv_event_cb_t cb, int data)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_min_width(b, 46, 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
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

lv_obj_t *ui_props_create(lv_obj_t *parent)
{
    fd_curve *c = model_selected_curve();
    if (!c) {
        lv_obj_t *root = lv_obj_create(parent);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
        lv_obj_center(ui_label(root, "没有选中的曲线", FD_C_DIM));
        return root;
    }
    g_id = c->fn.id;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 10, 0);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), 36);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, c->name);
    lv_obj_set_style_text_font(title, &font_cn_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *back = ui_button(head, "返回", back_clicked);
    lv_obj_set_size(back, 62, 34);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 颜色 */
    lv_obj_t *panel = ui_panel(root, LV_PCT(100), 136);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_style_pad_column(panel, 8, 0);

    for (int i = 0; i < FD_PALETTE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(panel);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 48, 48);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(model_color(i)), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        if (c->fn.color_index == i) {
            lv_obj_set_style_border_width(d, 3, 0);
            lv_obj_set_style_border_color(d, lv_color_hex(FD_C_TEXT), 0);
        }
        lv_obj_set_user_data(d, (void *)(intptr_t)i);
        ui_set_click(d, color_clicked);
    }

    /* 线宽 */
    lv_obj_t *wrow = lv_obj_create(root);
    lv_obj_remove_style_all(wrow);
    lv_obj_set_size(wrow, LV_PCT(100), 40);
    lv_obj_remove_flag(wrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *wl = lv_label_create(wrow);
    lv_label_set_text(wl, "线宽");
    lv_obj_set_style_text_font(wl, &font_cn_16, 0);
    lv_obj_set_style_text_color(wl, lv_color_hex(FD_C_DIM), 0);

    lv_obj_t *wchips = lv_obj_create(wrow);
    lv_obj_remove_style_all(wchips);
    lv_obj_set_size(wchips, LV_SIZE_CONTENT, 40);
    lv_obj_remove_flag(wchips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wchips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(wchips, 8, 0);
    for (int w = 1; w <= 4; w++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", w);
        chip(wchips, buf, c->width == w, width_clicked, w);
    }

    /* 线型 */
    lv_obj_t *drow = lv_obj_create(root);
    lv_obj_remove_style_all(drow);
    lv_obj_set_size(drow, LV_PCT(100), 40);
    lv_obj_remove_flag(drow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(drow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *dl = lv_label_create(drow);
    lv_label_set_text(dl, "线型");
    lv_obj_set_style_text_font(dl, &font_cn_16, 0);
    lv_obj_set_style_text_color(dl, lv_color_hex(FD_C_DIM), 0);

    lv_obj_t *dchips = lv_obj_create(drow);
    lv_obj_remove_style_all(dchips);
    lv_obj_set_size(dchips, LV_SIZE_CONTENT, 40);
    lv_obj_remove_flag(dchips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dchips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dchips, 8, 0);
    chip(dchips, "实线", c->dash == 0, dash_clicked, 0);
    chip(dchips, "虚线", c->dash == 1, dash_clicked, 1);
    chip(dchips, "点线", c->dash == 2, dash_clicked, 2);

    /* 角度 */
    lv_obj_t *arow = lv_obj_create(root);
    lv_obj_remove_style_all(arow);
    lv_obj_set_size(arow, LV_PCT(100), 40);
    lv_obj_remove_flag(arow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(arow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(arow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *al = lv_label_create(arow);
    lv_label_set_text(al, "角度");
    lv_obj_set_style_text_font(al, &font_cn_16, 0);
    lv_obj_set_style_text_color(al, lv_color_hex(FD_C_DIM), 0);

    lv_obj_t *achips = lv_obj_create(arow);
    lv_obj_remove_style_all(achips);
    lv_obj_set_size(achips, LV_SIZE_CONTENT, 40);
    lv_obj_remove_flag(achips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(achips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(achips, 8, 0);
    chip(achips, "跟随", c->fn.degrees < 0, angle_clicked, -1);
    chip(achips, "弧度", c->fn.degrees == 0, angle_clicked, 0);
    chip(achips, "角度", c->fn.degrees == 1, angle_clicked, 1);

    ui_switch_row(root, "显示这条曲线", c->fn.visible, visible_changed);

    /* 底部动作 */
    lv_obj_t *foot = lv_obj_create(root);
    lv_obj_remove_style_all(foot);
    lv_obj_set_size(foot, LV_PCT(100), 40);
    lv_obj_remove_flag(foot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *undo = ui_button(foot, "撤销", undo_clicked);
    lv_obj_set_size(undo, 74, 34);
    lv_obj_add_flag(undo, LV_OBJ_FLAG_CLICKABLE);
    if (!model_can_undo()) lv_obj_add_state(undo, LV_STATE_DISABLED);

    lv_obj_t *redo = ui_button(foot, "重做", redo_clicked);
    lv_obj_set_size(redo, 74, 34);
    if (!model_can_redo()) lv_obj_add_state(redo, LV_STATE_DISABLED);

    lv_obj_t *del = ui_button(foot, "删除", delete_clicked);
    lv_obj_set_size(del, 74, 34);
    lv_obj_set_style_bg_color(del, lv_color_hex(0x3A2024), 0);
    lv_obj_t *dll = lv_obj_get_child(del, 0);
    if (dll) lv_obj_set_style_text_color(dll, lv_color_hex(FD_C_DANGER), 0);

    return root;
}
