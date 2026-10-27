#ifndef FD_VIEWPORT_H
#define FD_VIEWPORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_VP_MIN_UPP 1e-9
#define FD_VP_MAX_UPP 1e9

/** 世界坐标 ↔ 屏幕像素的等比例映射。 */
typedef struct {
    float width_px;
    float height_px;
    double center_x;
    double center_y;
    double units_per_px;
} fd_viewport;

double fd_vp_world_left(const fd_viewport *vp);
double fd_vp_world_right(const fd_viewport *vp);
double fd_vp_world_bottom(const fd_viewport *vp);
double fd_vp_world_top(const fd_viewport *vp);
double fd_vp_world_width(const fd_viewport *vp);
double fd_vp_world_height(const fd_viewport *vp);

float fd_vp_to_screen_x(const fd_viewport *vp, double wx);
float fd_vp_to_screen_y(const fd_viewport *vp, double wy);
double fd_vp_to_world_x(const fd_viewport *vp, double sx);
double fd_vp_to_world_y(const fd_viewport *vp, double sy);

/** 以屏幕点 (fx, fy) 为焦点缩放；factor > 1 表示放大。 */
void fd_vp_zoom(fd_viewport *vp, double factor, float fx, float fy);
/** 拖动平移：手指移动 (dx, dy) 像素。 */
void fd_vp_pan(fd_viewport *vp, float dx, float dy);
void fd_vp_resize(fd_viewport *vp, float w, float h);

/** 数值刻度步长（给网格线用）。 */
double fd_vp_grid_step(const fd_viewport *vp);

/** 让 x 轴正好覆盖 [min_x, max_x] 的初始视口。 */
fd_viewport fd_vp_for_range(float w, float h, double min_x, double max_x);

/**
 * 屏幕空间仿射：p_B = k * p_A + (tx, ty)。
 * 用于把旧视口下采样好的曲线 O(1) 拖到新视口。
 */
typedef struct {
    double k, tx, ty;
} fd_screen_delta;

fd_screen_delta fd_vp_delta_to(const fd_viewport *from, const fd_viewport *to);
bool fd_screen_delta_is_identity(const fd_screen_delta *d);

#ifdef __cplusplus
}
#endif

#endif /* FD_VIEWPORT_H */
