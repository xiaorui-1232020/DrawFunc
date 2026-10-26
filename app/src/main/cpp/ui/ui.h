#ifndef FD_UI_H
#define FD_UI_H

#include <stddef.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 中文点阵（GB2312 一级字库 + ASCII），由 lv_font_conv 生成。 */
LV_FONT_DECLARE(font_cn_14);
LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(font_cn_20);

/* ---- 主题 ---- */
#define FD_C_BG 0x0B0F14
#define FD_C_PANEL 0x151C24
#define FD_C_PANEL2 0x1E2731
#define FD_C_TEXT 0xE8EEF5
#define FD_C_DIM 0x93A1AF
#define FD_C_ACCENT 0x7FD8FF
#define FD_C_DANGER 0xFF8A80
#define FD_C_GRID 0x1B2530
#define FD_C_AXIS 0x5B6B7A

typedef enum {
    UI_SCR_GRAPH = 0,
    UI_SCR_LIST,
    UI_SCR_EDIT,
    UI_SCR_PROPS,
    UI_SCR_ARCHIVE,
    UI_SCR_SETTINGS,
    UI_SCR_ABOUT
} ui_screen;

void ui_init(void);
void ui_deinit(void);
void ui_show(ui_screen s);
ui_screen ui_current(void);
/** 状态变化后重建当前界面。 */
void ui_refresh(void);
void ui_refresh_later(void);

void ui_touch(int action, int count, float x0, float y0, float x1, float y1);
void ui_crown(float delta);
bool ui_key(int code);

void ui_toast(const char *msg);

lv_obj_t *ui_root(void);
lv_coord_t ui_width(void);
lv_coord_t ui_height(void);

/* ---- 通用控件 ---- */
lv_obj_t *ui_panel(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t color);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb);
/** 一个小圆/小方块图标按钮，文本是符号。 */
lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb);
lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *text, bool value,
                        lv_event_cb_t cb);

void ui_set_click(lv_obj_t *obj, lv_event_cb_t cb);

/* ---- 各屏创建函数（供 ui.c 调用） ---- */
lv_obj_t *ui_graph_create(lv_obj_t *parent);
lv_obj_t *ui_list_create(lv_obj_t *parent);
lv_obj_t *ui_editor_create(lv_obj_t *parent);
lv_obj_t *ui_props_create(lv_obj_t *parent);
lv_obj_t *ui_archive_create(lv_obj_t *parent);
lv_obj_t *ui_settings_create(lv_obj_t *parent);
lv_obj_t *ui_about_create(lv_obj_t *parent);

/* ---- 绘图屏对外 ---- */
void ui_graph_touch(int action, int count, float x0, float y0, float x1, float y1);
void ui_graph_crown(float delta);
void ui_graph_invalidate(void);
bool ui_graph_export_png(const char *title, char *out_path, size_t cap);
/** 切屏前调用：LVGL 对象会被销毁，这里把它们置空，但保留像素缓冲区。 */
void ui_graph_detach(void);

#ifdef __cplusplus
}
#endif

#endif /* FD_UI_H */
