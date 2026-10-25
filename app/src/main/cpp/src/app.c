/**
 * 应用入口（骨架阶段）：先只验证 LVGL 在 SurfaceView 上跑通。
 * 绘图/列表/编辑等界面在后续步骤里替换掉这里的内容。
 */
#include "lvgl/lvgl.h"
#include "app.h"

static lv_obj_t *g_root = NULL;

void app_init(void) {
    g_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x1F2124), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(g_root);
    lv_label_set_text(title, "FunctionDrawer");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sub = lv_label_create(g_root);
    lv_label_set_text(sub, "LVGL 9.2 / ANativeWindow");
    lv_obj_set_style_text_color(sub, lv_color_hex(0xCCFFFFFF), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 18);
}

void app_set_size(int32_t w, int32_t h) {
    (void)w;
    (void)h;
}

void app_touch(int action, int count, float x0, float y0, float x1, float y1) {
    (void)action;
    (void)count;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
}

void app_crown(float delta) {
    (void)delta;
}

bool app_key(int key_code) {
    (void)key_code;
    return false;
}
