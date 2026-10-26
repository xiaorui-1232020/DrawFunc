#ifndef FD_PLOT_H
#define FD_PLOT_H

#include <stdbool.h>
#include <stdint.h>

#include "expr.h"
#include "viewport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FD_MAX_FUNCTIONS 32
#define FD_SRC_CAP 256

typedef enum {
    FD_PLOT_EXPLICIT = 0,
    FD_PLOT_POLAR = 1,
    FD_PLOT_PARAMETRIC = 2,
    FD_PLOT_IMPLICIT = 3
} fd_plot_mode;

typedef enum {
    FD_QUALITY_FAST = 0,
    FD_QUALITY_NORMAL = 1,
    FD_QUALITY_HIGH = 2
} fd_quality;

typedef struct {
    int id;
    fd_plot_mode mode;
    char src1[FD_SRC_CAP];
    char src2[FD_SRC_CAP];
    double t_min, t_max;
    int color_index;
    bool visible;
    int degrees; /**< -1 跟随全局；0 = RAD；1 = DEG */
} fd_function;

typedef struct {
    float *xy; /**< 交错存放 x0,y0,x1,y1...，屏幕像素 */
    int count; /**< 点数（不是 float 数） */
    int function_id;
    uint8_t color_index;
} fd_polyline;

typedef struct {
    fd_polyline *lines;
    int line_count;
    fd_viewport viewport; /**< 这批曲线是按哪个视口采样的 */

    int *error_ids;
    char (*error_msgs)[FD_EXPR_ERR_CAP];
    int error_count;
} fd_plot_result;

void fd_plot_result_free(fd_plot_result *r);

/**
 * 采样所有可见函数。
 *
 * @param default_degrees 全局角度单位（函数自己的 degrees 优先）
 * @param params a,b,c,d 的取值（4 项）
 */
bool fd_plot(const fd_function *fns, int fn_count,
             const fd_viewport *vp, fd_quality q,
             bool default_degrees, const double params[4],
             fd_plot_result *out);

/** 列表里显示的一行摘要（写进 buf）。 */
void fd_function_summary(const fd_function *fn, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FD_PLOT_H */
