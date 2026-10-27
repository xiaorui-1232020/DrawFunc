/**
 * 平台相关的少量能力（Android 侧实现在 platform_android.c，主机端测试给桩）。
 */
#ifndef FD_PLATFORM_H
#define FD_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 把一张 PNG 存进系统相册（MediaStore.Images，Pictures/DrawFunc）。
 * 返回 false 表示不支持或失败，调用方应退回写应用私有目录。
 */
bool platform_save_png(const char *name, const unsigned char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FD_PLATFORM_H */
