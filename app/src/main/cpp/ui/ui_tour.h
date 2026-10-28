/**
 * 欢迎页 + 分步新手引导。
 *
 * 首次启动（settings.tutorial_done == false）先显示欢迎页；点「开始使用」
 * 进入 4 步引导：绘图区 → 顶部工具条 → 函数列表 → 设置。
 * 每步在 lv_layer_top() 上盖一层暗色遮罩 + 高亮框 + 说明卡，可随时跳过。
 * 走完或跳过后把 tutorial_done 写进文档，设置页可以重新播放。
 */
#ifndef FD_UI_TOUR_H
#define FD_UI_TOUR_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 欢迎页（注册在 ui.c 的屏幕表里）。 */
lv_obj_t *ui_welcome_create(lv_obj_t *parent);

/** 从第 1 步开始播放引导（会先切到绘图屏）。 */
void ui_tour_start(void);
/** 结束引导并标记完成。 */
void ui_tour_finish(void);
/** 引导进行中时返回 true（绘图手势此时不响应）。 */
bool ui_tour_active(void);

#ifdef __cplusplus
}
#endif

#endif /* FD_UI_TOUR_H */
