/** 关于页。 */
#include "ui.h"

static void back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_GRAPH);
}

lv_obj_t *ui_about_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, UI_PX(16), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, UI_PX(10), 0);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), UI_PX(38));
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "关于");
    lv_obj_set_style_text_font(title, ui_font_body(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *back = ui_button(head, "返回", back_clicked);
    lv_obj_set_size(back, UI_PX(62), UI_PX(34));
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *name = lv_label_create(root);
    lv_label_set_text(name, "函数绘图");
    lv_obj_set_style_text_font(name, ui_font_title(), 0);
    lv_obj_set_style_text_color(name, lv_color_hex(FD_C_ACCENT), 0);

    lv_obj_t *ver = lv_label_create(root);
    lv_label_set_text(ver, "LVGL 9.2 / 原生 C 移植  ·  v1.1");
    lv_obj_set_style_text_font(ver, ui_font_small(), 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(FD_C_DIM), 0);

    lv_obj_t *body = lv_label_create(root);
    lv_label_set_text(body,
                      "在手表上随手画出函数曲线。\n"
                      "支持显函数、极坐标、参数方程和隐函数，\n"
                      "参数 a/b/c/d 可以用滑动条实时调整。\n\n"
                      "单指拖动平移，双指捏合缩放，双击复位视图，\n"
                      "旋转表冠也可以缩放。");
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_font(body, ui_font_small(), 0);
    lv_obj_set_style_text_color(body, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_set_style_text_line_space(body, 4, 0);

    lv_obj_t *lic = lv_label_create(root);
    lv_label_set_text(lic, "MIT License");
    lv_obj_set_style_text_font(lic, ui_font_small(), 0);
    lv_obj_set_style_text_color(lic, lv_color_hex(FD_C_DIM), 0);

    return root;
}
