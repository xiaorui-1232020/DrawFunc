/**
 * 轻量数学排版（LaTeX 子集）：把表达式排成「基线对齐」的一组 LVGL 控件。
 *
 * 支持：
 *   x^{2} / x^2        上标（小字号、抬高，容器底部对齐）
 *   a_{i} / a_i        下标
 *   \frac{a}{b}        分式（分子 / 细线 / 分母，整体在行内垂直居中）
 *   \sqrt{x}           根号（√ 加顶部横线）
 *   \pi \theta         希腊字母
 *
 * 其余字符原样按正文输出。渲染结果是一个横向 flex 容器，
 * 所有原子按底部对齐，因此普通文字、上下标能共用同一条基线。
 */
#ifndef FD_UI_MATH_H
#define FD_UI_MATH_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建数学排版容器；expr 为空时只建一个空容器。 */
lv_obj_t *ui_math_create(lv_obj_t *parent, const char *expr, uint32_t color);

/** 重新排版（会清空并重建子控件）。 */
void ui_math_set_text(lv_obj_t *cont, const char *expr, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* FD_UI_MATH_H */
