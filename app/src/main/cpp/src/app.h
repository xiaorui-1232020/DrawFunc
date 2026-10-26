#ifndef FD_APP_H
#define FD_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 屏幕尺寸变化（像素）。 */
void app_set_size(int32_t w, int32_t h);

/* 触屏原始事件；坐标是屏幕像素。 */
void app_touch(int action, int count, float x0, float y0, float x1, float y1);

/* 表冠滚动量（正负表示方向）。 */
void app_crown(float delta);

/* 按键；返回 true 表示已消费。 */
bool app_key(int key_code);

/* 首次进入时建界面。 */
void app_init(void);
void app_deinit(void);

/* Java 侧的 filesDir，用于自动保存和存档。 */
void app_set_files_dir(const char *dir);

#ifdef __cplusplus
}
#endif

#endif /* FD_APP_H */
