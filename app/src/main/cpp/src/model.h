/**
 * 文档模型：曲线、参数滑动条、设置、视口，以及撤销/重做与持久化。
 *
 * 所有字段都是纯 C 数据，UI 只读它、改它，然后调 ui_refresh()。
 * 渲染线程独占 LVGL，所以这里不需要加锁。
 */
#ifndef FD_MODEL_H
#define FD_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "geo.h"
#include "plot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FD_MAX_CURVES 24
#define FD_MAX_SLIDERS 12
#define FD_NAME_CAP 24
#define FD_UNDO_DEPTH 24

/* 曲线配色 12 种，和原 Kotlin 的 CurvePalette 一致。 */
#define FD_PALETTE_COUNT 12

typedef struct {
    bool show_grid;
    bool show_axes;
    bool show_numbers;
    bool angle_degrees;
    int quality; /**< fd_quality */
    bool crown_zoom; /**< true = 表冠缩放；false = 表冠上下平移 */
    bool show_zoom_bar; /**< 底部「− 滑动条 ＋」缩放栏（默认关） */
    bool show_presets; /**< 列表里的「示例」块（默认开） */
    bool complex_plane; /**< 复平面模式：曲线表达式按复数求值，结果画成点 */
    bool tutorial_done; /**< 新手引导是否已完成（首次启动展示欢迎页） */
} fd_settings;

typedef struct {
    fd_function fn; /**< id / mode / src1 / src2 / t_min,t_max / color_index / visible / degrees */
    char name[FD_NAME_CAP];
    int width; /**< 1..4 像素 */
    int dash;  /**< 0 实线，1 虚线，2 点线 */
} fd_curve;

typedef struct {
    int id;
    char param[8];
    double value;
    double min;
    double max;
    double step;
    bool playing;
} fd_slider;

typedef struct {
    fd_curve curves[FD_MAX_CURVES];
    int curve_count;
    fd_slider sliders[FD_MAX_SLIDERS];
    int slider_count;
    fd_geo geos[FD_GEO_MAX];
    int geo_count;
    int geo_next_id;
    fd_settings settings;
    fd_viewport vp;
    bool view_initialized;
    int selected_id;
    int next_id;
    int32_t screen_w;
    int32_t screen_h;
} fd_doc;

void model_init(void);
void model_deinit(void);
fd_doc *model(void);
void model_set_screen(int32_t w, int32_t h);

uint32_t model_color(int color_index);

/* ---- 曲线 ---- */
int model_add_curve(fd_plot_mode mode);
void model_delete_curve(int id);
fd_curve *model_curve(int id);
fd_curve *model_selected_curve(void);
void model_select(int id);

/* ---- 滑动条 ---- */
fd_slider *model_slider(const char *param);
fd_slider *model_slider_by_id(int id);
void model_sync_sliders(void); /**< 按当前表达式里用到的参数补/删滑动条 */
void model_param_values(double out[4]); /**< a,b,c,d 的当前值（缺省 1） */

/* ---- 视口 ---- */
void model_reset_view(void);
void model_zoom(double factor, double fx, double fy);
void model_pan(double dx, double dy);

/* ---- 撤销 / 重做 ---- */
void model_checkpoint(void);
bool model_undo(void);
bool model_redo(void);
bool model_can_undo(void);
bool model_can_redo(void);

/* ---- 持久化 ---- */
void model_set_files_dir(const char *dir);
const char *model_files_dir(void);
bool model_save(void);      /**< 写 autosave.fd */
bool model_load(void);      /**< 读 autosave.fd；不存在返回 false */
bool model_has_saved(void);

/* ---- 存档 ---- */
#define FD_ARCHIVE_MAX 32
typedef struct {
    char file[64];
    char title[48];
} fd_archive_entry;

int model_archive_list(fd_archive_entry *out, int cap);
bool model_archive_save(const char *title);
bool model_archive_load(const char *file);
bool model_archive_delete(const char *file);

/* ---- 工具 ---- */
void model_next_name(char *out, size_t cap);

/* ---- 几何作图 ---- */
/** 新建一个几何对象（自动分配 id 与名字）；满了返回 NULL。 */
fd_geo *model_add_geo(fd_geo_kind kind);
fd_geo *model_geo(int id);
void model_delete_geo(int id);
/** 清除全部几何对象。 */
void model_clear_geo(void);

#ifdef __cplusplus
}
#endif

#endif /* FD_MODEL_H */
