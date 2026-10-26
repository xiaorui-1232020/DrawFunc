/** 存档页：保存当前、载入、删除、导出 PNG。 */
#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "model.h"

static fd_archive_entry g_entries[FD_ARCHIVE_MAX];
static int g_count;

static void back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_GRAPH);
}

static void now_label(char *buf, size_t cap)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
}

static void save_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    char title[48];
    now_label(title, sizeof(title));
    if (model_archive_save(title)) {
        ui_toast("已保存");
    } else {
        ui_toast("保存失败");
    }
    ui_refresh_later();
}

static void export_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    char title[48];
    now_label(title, sizeof(title));
    char path[768];
    if (ui_graph_export_png(title, path, sizeof(path))) {
        ui_toast("已导出 PNG");
    } else {
        ui_toast("导出失败");
    }
}

static void load_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= g_count) return;
    if (model_archive_load(g_entries[idx].file)) {
        model_save();
        ui_toast("已载入");
        ui_show(UI_SCR_GRAPH);
    } else {
        ui_toast("载入失败");
    }
}

static void delete_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= g_count) return;
    model_archive_delete(g_entries[idx].file);
    ui_toast("已删除");
    ui_refresh_later();
}

lv_obj_t *ui_archive_create(lv_obj_t *parent)
{
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
    lv_obj_set_size(head, LV_PCT(100), 38);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "存档");
    lv_obj_set_style_text_font(title, &font_cn_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *back = ui_button(head, "返回", back_clicked);
    lv_obj_set_size(back, 62, 34);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *actions = lv_obj_create(root);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), 40);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *save = ui_button(actions, "保存当前", save_clicked);
    lv_obj_set_size(save, 130, 36);
    lv_obj_set_style_bg_color(save, lv_color_hex(0x18323C), 0);
    lv_obj_t *sl = lv_obj_get_child(save, 0);
    if (sl) lv_obj_set_style_text_color(sl, lv_color_hex(FD_C_ACCENT), 0);

    lv_obj_t *exp = ui_button(actions, "导出 PNG", export_clicked);
    lv_obj_set_size(exp, 130, 36);

    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    g_count = model_archive_list(g_entries, FD_ARCHIVE_MAX);

    if (g_count == 0) {
        lv_obj_t *empty = ui_label(list, "还没有存档", FD_C_DIM);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty, 24, 0);
        return root;
    }

    for (int i = 0; i < g_count; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 54);
        lv_obj_set_style_bg_color(row, lv_color_hex(FD_C_PANEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, g_entries[i].title);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 210);
        lv_obj_set_style_text_font(name, &font_cn_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(FD_C_TEXT), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *load = ui_button(row, "载入", load_clicked);
        lv_obj_set_size(load, 58, 32);
        lv_obj_set_user_data(load, (void *)(intptr_t)i);
        lv_obj_align(load, LV_ALIGN_RIGHT_MID, -62, 0);

        lv_obj_t *del = ui_button(row, "删", delete_clicked);
        lv_obj_set_size(del, 44, 32);
        lv_obj_set_user_data(del, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(del, lv_color_hex(0x3A2024), 0);
        lv_obj_t *dl = lv_obj_get_child(del, 0);
        if (dl) lv_obj_set_style_text_color(dl, lv_color_hex(FD_C_DANGER), 0);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    return root;
}
