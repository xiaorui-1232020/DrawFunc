/**
 * 绘图主界面：网格、坐标轴、曲线，以及平移/缩放/点选/表冠。
 *
 * 曲线不走 LVGL 的矢量控件，而是直接把像素画进一块 canvas 缓冲区：
 * 一次重绘只有几次 malloc，没有成千上万个 draw task，手表上才跑得动。
 */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "../engine/cmath.h"
#include "../engine/fd_features.h"
#include "../engine/plot.h"
#include "../engine/viewport.h"
#include "../src/platform.h"
#include "../src/subst.h"
#include "geo.h"
#include "model.h"

/* lodepng 是 LVGL 自带的，编译在 lvgl 静态库里。 */
#include "src/libs/lodepng/lodepng.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FD_GRAPH_HIT_PX 16.0f
#define FD_TAP_MS 260
#define FD_DOUBLE_TAP_MS 340

/* ------------------------------------------------------------------ */
/* 状态                                                                */
/* ------------------------------------------------------------------ */

static lv_obj_t *g_root;
static lv_obj_t *g_canvas;
static lv_obj_t *g_numbers;
static lv_obj_t *g_card;
static lv_obj_t *g_card_dot;
static lv_obj_t *g_card_name;
static lv_obj_t *g_card_expr;
static lv_obj_t *g_card_err;
static lv_obj_t *g_card_roots;
static lv_obj_t *g_card_ext;
static lv_obj_t *g_card_point;
static lv_coord_t g_card_h = 52;

/* 点击曲线后记住那个点的世界坐标（卡片里显示「点：(x, y)」）。 */
static int g_tap_id;
static bool g_tap_valid;
static double g_tap_wx, g_tap_wy;

/* 底部「− 滑动条 ＋」缩放栏（设置里打开才创建）。 */
static lv_obj_t *g_zoom_bar;
static lv_obj_t *g_zoom_slider;
static float g_zoom_depth; /**< 滑轨当前相对位移，-1..1 */

static uint32_t *g_px;
static int g_stride_px;
static int g_w, g_h;

static fd_plot_result g_plot;
static bool g_plot_valid;

/* 参数滑动条播放 */
static lv_timer_t *g_play_timer;
static int g_play_dir = 1;
static bool g_force_fast;

/* 手势 */
static bool g_drag;
static bool g_pinching;
static float g_last_x, g_last_y;
static float g_pinch_dist;
static float g_down_x, g_down_y;
static uint32_t g_down_tick;
static bool g_moved;
static uint32_t g_last_tap_tick;
static float g_last_tap_x, g_last_tap_y;

/* 几何工具 */
typedef enum {
    FD_TOOL_MOVE = 0,
    FD_TOOL_PLOT,
    FD_TOOL_DISTANCE,
    FD_TOOL_AREA,
    FD_TOOL_ANGLE,
    FD_TOOL_AXIS_SYM,
    FD_TOOL_CENTER_SYM,
    FD_TOOL_MIDPOINT,
    FD_TOOL_PERP,
    FD_TOOL_PERP_BISECTOR,
    FD_TOOL_PARALLEL,
    FD_TOOL_BISECTOR,
    FD_TOOL_TANGENT,
    FD_TOOL_SEGMENT,
    FD_TOOL_LINE,
    FD_TOOL_RAY,
    FD_TOOL_COUNT
} fd_tool;

typedef enum { TK_NONE, TK_POINT, TK_LINE, TK_CURVE, TK_ANY } step_kind;

typedef struct {
    const char *label;
    int section; /**< 0 基本 / 1 度量 / 2 变换 / 3 作图 / 4 直线 */
    int steps;   /**< 固定步数；open = true 时忽略 */
    step_kind kind[3];
    bool open;   /**< 面积：一直点，直到点回第一个点 */
    const char *hint;
} tool_def;

static const char *const k_section_names[5] = {"基本工具", "度量", "变换", "作图",
                                               "直线"};

static const tool_def k_tools[FD_TOOL_COUNT] = {
        [FD_TOOL_MOVE] = {"移动", 0, 0, {TK_NONE}, false, "拖动图像"},
        [FD_TOOL_PLOT] = {"描点", 0, 0, {TK_POINT}, true, "点一下就在那儿定一个点"},
        [FD_TOOL_DISTANCE] = {"距离", 1, 2, {TK_POINT, TK_POINT}, false, "点两个点"},
        [FD_TOOL_AREA] = {"面积", 1, 0, {TK_POINT}, true,
                          "依次点多边形顶点，最后点回第一个点"},
        [FD_TOOL_ANGLE] = {"角度", 1, 3, {TK_POINT, TK_POINT, TK_POINT}, false,
                           "先点顶点，再点两条边上的点"},
        [FD_TOOL_AXIS_SYM] = {"轴对称", 2, 2, {TK_ANY, TK_LINE}, false,
                              "先点对象，再点一条直线当对称轴"},
        [FD_TOOL_CENTER_SYM] = {"中心对称", 2, 2, {TK_ANY, TK_POINT}, false,
                                "先点对象，再点对称中心"},
        [FD_TOOL_MIDPOINT] = {"中点", 3, 2, {TK_POINT, TK_POINT}, false, "点两个点"},
        [FD_TOOL_PERP] = {"垂线", 3, 2, {TK_POINT, TK_LINE}, false,
                          "先点一个点，再点一条直线"},
        [FD_TOOL_PERP_BISECTOR] = {"中垂线", 3, 2, {TK_POINT, TK_POINT}, false,
                                   "点两个点"},
        [FD_TOOL_PARALLEL] = {"平行线", 3, 2, {TK_LINE, TK_POINT}, false,
                              "先点一条参考线，再点要经过的点"},
        [FD_TOOL_BISECTOR] = {"角平分线", 3, 3, {TK_POINT, TK_POINT, TK_POINT}, false,
                              "依次点三个点（第二个是顶点）"},
        [FD_TOOL_TANGENT] = {"切线", 3, 1, {TK_CURVE}, false, "点曲线上的点"},
        [FD_TOOL_SEGMENT] = {"线段", 4, 2, {TK_POINT, TK_POINT}, false, "点两个端点"},
        [FD_TOOL_LINE] = {"直线", 4, 2, {TK_POINT, TK_POINT}, false, "点两个点"},
        [FD_TOOL_RAY] = {"射线", 4, 2, {TK_POINT, TK_POINT}, false,
                         "先点起点，再点经过的另一个点"},
};

typedef enum { SEL_NONE = 0, SEL_POINT, SEL_LINE, SEL_CURVE } sel_kind;

typedef struct {
    sel_kind kind;
    int id;        /**< 几何对象 id；曲线则是 function id */
    double xy[2];  /**< 点坐标 / 曲线上的点 */
    int axis;      /**< SEL_LINE 且 id == 0 时：1 = x 轴，2 = y 轴 */
    double l0[2];  /**< 直线的两个世界坐标点（快照） */
    double l1[2];
} pick_t;

static fd_tool g_tool = FD_TOOL_MOVE;
static int g_step;
static pick_t g_pick[3];
static int g_area_ids[FD_GEO_MAX_PTS];
static int g_area_n;

static lv_obj_t *g_hint;
static lv_obj_t *g_hint_label;
static lv_obj_t *g_panel;

/* ------------------------------------------------------------------ */
/* 像素绘制                                                            */
/* ------------------------------------------------------------------ */

static inline uint32_t rgb(uint32_t c) { return 0xFF000000u | (c & 0xFFFFFFu); }

static inline void put(int x, int y, uint32_t c)
{
    if ((unsigned)x >= (unsigned)g_w || (unsigned)y >= (unsigned)g_h) return;
    g_px[(size_t)y * (size_t)g_stride_px + (size_t)x] = c;
}

static void dot(int x, int y, int width, uint32_t c)
{
    if (width <= 1) {
        put(x, y, c);
        return;
    }
    int half = width / 2;
    for (int dy = -half; dy <= width - 1 - half; dy++) {
        for (int dx = -half; dx <= width - 1 - half; dx++) {
            put(x + dx, y + dy, c);
        }
    }
}

static void vline(int x, int y0, int y1, uint32_t c)
{
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++) put(x, y, c);
}

static void hline(int y, int x0, int x1, uint32_t c)
{
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (int x = x0; x <= x1; x++) put(x, y, c);
}

/** 一条折线；dash 用像素长度做相位，跨段连续，不会每段重新起笔。 */
static void stroke(const float *xy, int count, uint32_t color, int width, int dash)
{
    if (count < 2 || !xy) return;

    int on_len = 0, off_len = 0;
    if (dash == 1) {
        on_len = 7;
        off_len = 5;
    } else if (dash == 2) {
        on_len = 2;
        off_len = 4;
    }
    int phase = 0;

    for (int i = 0; i + 1 < count; i++) {
        int x0 = (int)lroundf(xy[2 * i]);
        int y0 = (int)lroundf(xy[2 * i + 1]);
        int x1 = (int)lroundf(xy[2 * i + 2]);
        int y1 = (int)lroundf(xy[2 * i + 3]);

        int dx = x1 - x0;
        int dy = y1 - y0;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int steps = adx > ady ? adx : ady;
        int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
        int sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
        int err = (steps > 0 ? adx : 0) - (steps > 0 ? ady : 0);

        int x = x0, y = y0;
        for (int s = 0; s <= steps; s++) {
            bool visible = true;
            if (on_len > 0) {
                visible = phase < on_len;
                phase++;
                if (phase >= on_len + off_len) phase = 0;
            }
            if (visible) dot(x, y, width, color);
            if (s == steps) break;

            int e2 = err * 2;
            if (e2 > -ady) {
                err -= ady;
                x += sx;
            }
            if (e2 < adx) {
                err += adx;
                y += sy;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 采样                                                                */
/* ------------------------------------------------------------------ */

static void rebuild_plot(void)
{
    fd_doc *d = model();
    fd_plot_result next;
    memset(&next, 0, sizeof(next));

    fd_function fns[FD_MAX_CURVES];
    int n = 0;
    for (int i = 0; i < d->curve_count && n < FD_MAX_CURVES; i++) {
        fns[n] = d->curves[i].fn;
        /* x(A)/y(A) 与标量名在送进引擎前换成数值字面量（原文本不变）。 */
        char sub1[FD_SRC_CAP];
        char sub2[FD_SRC_CAP];
        fd_subst_geo(fns[n].src1, sub1, sizeof(sub1));
        fd_subst_geo(fns[n].src2, sub2, sizeof(sub2));
        snprintf(fns[n].src1, sizeof(fns[n].src1), "%s", sub1);
        snprintf(fns[n].src2, sizeof(fns[n].src2), "%s", sub2);
        n++;
    }

    double params[4];
    model_param_values(params);

    fd_quality q = g_force_fast ? FD_QUALITY_FAST : (fd_quality)d->settings.quality;
    if (!fd_plot(fns, n, &d->vp, q,
                 d->settings.angle_degrees, params, &next)) {
        fd_plot_result_free(&next);
        return;
    }

    fd_plot_result_free(&g_plot);
    g_plot = next;
    g_plot_valid = true;
}

/* ------------------------------------------------------------------ */
/* 绘制                                                                */
/* ------------------------------------------------------------------ */

static void fmt_num(char *buf, size_t cap, double v)
{
    if (v == 0.0) v = 0.0; /* 去掉 -0 */
    snprintf(buf, cap, "%g", v);
}

static void fill_bg(void)
{
    uint32_t bg = rgb(FD_C_BG);
    for (int y = 0; y < g_h; y++) {
        uint32_t *row = g_px + (size_t)y * (size_t)g_stride_px;
        for (int x = 0; x < g_w; x++) row[x] = bg;
    }
}

static void add_number(lv_obj_t *parent, int x, int y, const char *text)
{
    if (!parent) return;
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_C_DIM), 0);
    lv_obj_set_pos(l, x, y);
}

static void draw_grid_and_axes(const fd_viewport *vp, const fd_settings *st)
{
    double left = fd_vp_world_left(vp);
    double right = fd_vp_world_right(vp);
    double bottom = fd_vp_world_bottom(vp);
    double top = fd_vp_world_top(vp);
    double step = fd_vp_grid_step(vp);
    if (!(step > 0.0) || !isfinite(step)) return;

    uint32_t grid_c = rgb(FD_C_GRID);
    uint32_t axis_c = rgb(FD_C_AXIS);

    double x0 = ceil(left / step) * step;
    for (int i = 0; i < 64 && x0 <= right; i++, x0 += step) {
        double v = fabs(x0) < step * 1e-6 ? 0.0 : x0;
        float sx = fd_vp_to_screen_x(vp, v);
        int ix = (int)lroundf(sx);
        bool axis = st->show_axes && v == 0.0;
        if (v == 0.0 && !st->show_axes) continue;
        vline(ix, 0, g_h - 1, axis ? axis_c : grid_c);

        if (st->show_numbers && v != 0.0) {
            char buf[32];
            fmt_num(buf, sizeof(buf), v);
            add_number(g_numbers, ix + 2, (int)(g_h / 2) + 3, buf);
        }
    }

    double y0 = ceil(bottom / step) * step;
    for (int i = 0; i < 64 && y0 <= top; i++, y0 += step) {
        double v = fabs(y0) < step * 1e-6 ? 0.0 : y0;
        float sy = fd_vp_to_screen_y(vp, v);
        int iy = (int)lroundf(sy);
        bool axis = st->show_axes && v == 0.0;
        if (v == 0.0 && !st->show_axes) continue;
        hline(iy, 0, g_w - 1, axis ? axis_c : grid_c);

        if (st->show_numbers && v != 0.0) {
            char buf[32];
            fmt_num(buf, sizeof(buf), v);
            add_number(g_numbers, (int)(g_w / 2) + 4, iy - 8, buf);
        }
    }

    if (st->show_axes) {
        /* 坐标轴恰好压在网格线上，重画一遍让颜色更明确。 */
        float ax = fd_vp_to_screen_x(vp, 0.0);
        if (ax >= 0.0f && ax <= (float)(g_w - 1)) vline((int)lroundf(ax), 0, g_h - 1, axis_c);
        float ay = fd_vp_to_screen_y(vp, 0.0);
        if (ay >= 0.0f && ay <= (float)(g_h - 1)) hline((int)lroundf(ay), 0, g_w - 1, axis_c);
    }
}

static void draw_curves(void)
{
    fd_doc *d = model();
    int selected = d->selected_id;

    /* 先画没被选中的，再画选中的，保证选中曲线在最上面。 */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < g_plot.line_count; i++) {
            const fd_polyline *pl = &g_plot.lines[i];
            bool is_sel = (pl->function_id == selected);
            if ((pass == 0) == is_sel) continue;

            const fd_curve *c = model_curve(pl->function_id);
            int width = c ? c->width : 2;
            int dash = c ? c->dash : 0;
            uint32_t color = rgb(model_color(pl->color_index));
            stroke(pl->xy, pl->count, color, width, dash);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 几何作图：绘制                                                      */
/* ------------------------------------------------------------------ */

static void add_tag(int x, int y, const char *text, uint32_t color)
{
    if (!g_numbers) return;
    lv_obj_t *l = lv_label_create(g_numbers);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_cn_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
}

static void world_to_screen(const double *w, float *sx, float *sy)
{
    const fd_viewport *vp = &model()->vp;
    *sx = fd_vp_to_screen_x(vp, w[0]);
    *sy = fd_vp_to_screen_y(vp, w[1]);
}

void screen_to_world(float sx, float sy, double *wx, double *wy)
{
    const fd_viewport *vp = &model()->vp;
    *wx = vp->center_x + ((double)sx - (double)g_w / 2.0) * vp->units_per_px;
    *wy = vp->center_y - ((double)sy - (double)g_h / 2.0) * vp->units_per_px;
}

/** 把无限长直线 / 射线裁剪到画布内；返回 false 表示完全在画布外。 */
static bool clip_inf_line(float bx, float by, float dx, float dy, bool ray,
                          float *ox0, float *oy0, float *ox1, float *oy1)
{
    const float eps = 1e-6f;
    float t0 = ray ? 0.0f : -1e7f;
    float t1 = 1e7f;
    float maxx = (float)(g_w - 1);
    float maxy = (float)(g_h - 1);

    if (fabsf(dx) < eps) {
        if (bx < 0.0f || bx > maxx) return false;
    } else {
        float ta = (0.0f - bx) / dx;
        float tb = (maxx - bx) / dx;
        if (ta > tb) {
            float t = ta;
            ta = tb;
            tb = t;
        }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (fabsf(dy) < eps) {
        if (by < 0.0f || by > maxy) return false;
    } else {
        float ta = (0.0f - by) / dy;
        float tb = (maxy - by) / dy;
        if (ta > tb) {
            float t = ta;
            ta = tb;
            tb = t;
        }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (t0 > t1) return false;

    *ox0 = bx + t0 * dx;
    *oy0 = by + t0 * dy;
    *ox1 = bx + t1 * dx;
    *oy1 = by + t1 * dy;
    return true;
}

/** 直线/射线对象的屏幕端点。 */
static bool geo_line_screen(const fd_geo *g, float *ax, float *ay, float *bx,
                            float *by)
{
    float px, py;
    world_to_screen(g->p[0], &px, &py);

    if (g->kind != FD_GEO_LINE && g->kind != FD_GEO_RAY) {
        world_to_screen(g->p[1], bx, by);
        *ax = px;
        *ay = py;
        return true;
    }

    double end[2] = {g->p[0][0] + g->p[1][0], g->p[0][1] + g->p[1][1]};
    float qx, qy;
    world_to_screen(end, &qx, &qy);
    float dx = qx - px, dy = qy - py;
    if (fabsf(dx) < 1e-6f && fabsf(dy) < 1e-6f) return false;
    return clip_inf_line(px, py, dx, dy, g->kind == FD_GEO_RAY, ax, ay, bx, by);
}

static void draw_arc(float cx, float cy, float r, float a0, float a1,
                     uint32_t color)
{
    float d = a1 - a0;
    while (d > (float)M_PI) d -= (float)(2.0 * M_PI);
    while (d < (float)-M_PI) d += (float)(2.0 * M_PI);
    int steps = (int)(fabsf(d) / 0.05f) + 1;
    if (steps > 200) steps = 200;
    for (int i = 0; i <= steps; i++) {
        float a = a0 + d * (float)i / (float)steps;
        dot((int)lroundf(cx + r * cosf(a)), (int)lroundf(cy + r * sinf(a)), 2,
            color);
    }
}

/** 实心圆（点用；Bresenham 太细，这里直接扫包围盒）。 */
static void fill_circle(float cx, float cy, float r, uint32_t c)
{
    int x0 = (int)floorf(cx - r);
    int x1 = (int)ceilf(cx + r);
    int y0 = (int)floorf(cy - r);
    int y1 = (int)ceilf(cy + r);
    float r2 = r * r;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx;
            float dy = (float)y + 0.5f - cy;
            if (dx * dx + dy * dy <= r2) put(x, y, c);
        }
    }
}

static void draw_geo_point(const fd_geo *g)
{
    float sx, sy;
    world_to_screen(g->p[0], &sx, &sy);
    int x = (int)lroundf(sx);
    int y = (int)lroundf(sy);
    fill_circle(sx, sy, 4.5f, rgb(FD_C_ACCENT));
    fill_circle(sx, sy, 2.0f, rgb(FD_C_BG));
    if (g->name[0]) add_tag(x + 6, y - 18, g->name, FD_C_TEXT);
}

static void draw_geo_segment(const fd_geo *g)
{
    float ax, ay, bx, by;
    if (!geo_line_screen(g, &ax, &ay, &bx, &by)) return;
    float xy[4] = {ax, ay, bx, by};
    stroke(xy, 2, rgb(FD_C_ACCENT), 2, 0);
}

static void draw_geo_distance(const fd_geo *g)
{
    float ax, ay, bx, by;
    world_to_screen(g->p[0], &ax, &ay);
    world_to_screen(g->p[1], &bx, &by);
    float xy[4] = {ax, ay, bx, by};
    stroke(xy, 2, rgb(FD_C_ACCENT), 1, 2);

    char buf[32];
    double len = geo_distance(g->p[0], g->p[1]);
    snprintf(buf, sizeof(buf), "%g", len);
    add_tag((int)lroundf((ax + bx) / 2.0f) + 4,
            (int)lroundf((ay + by) / 2.0f) - 20, buf, FD_C_TEXT);
}

static void draw_geo_angle(const fd_geo *g)
{
    float vx, vy, ax, ay, bx, by;
    world_to_screen(g->p[0], &vx, &vy);
    world_to_screen(g->p[1], &ax, &ay);
    world_to_screen(g->p[2], &bx, &by);

    float l1[4] = {vx, vy, ax, ay};
    float l2[4] = {vx, vy, bx, by};
    stroke(l1, 2, rgb(FD_C_ACCENT), 1, 0);
    stroke(l2, 2, rgb(FD_C_ACCENT), 1, 0);

    float a0 = atan2f(ay - vy, ax - vx);
    float a1 = atan2f(by - vy, bx - vx);
    float r = 26.0f;
    float len1 = sqrtf((ax - vx) * (ax - vx) + (ay - vy) * (ay - vy));
    float len2 = sqrtf((bx - vx) * (bx - vx) + (by - vy) * (by - vy));
    if (len1 < r) r = len1;
    if (len2 < r) r = len2;
    if (r < 8.0f) r = 8.0f;
    draw_arc(vx, vy, r, a0, a1, rgb(FD_C_ACCENT));

    float d = a1 - a0;
    while (d > (float)M_PI) d -= (float)(2.0 * M_PI);
    while (d < (float)-M_PI) d += (float)(2.0 * M_PI);
    float mid = a0 + d * 0.5f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f度", geo_angle_deg(g->p[0], g->p[1], g->p[2]));
    add_tag((int)lroundf(vx + (r + 8.0f) * cosf(mid)),
            (int)lroundf(vy + (r + 8.0f) * sinf(mid)), buf, FD_C_TEXT);
}

static void draw_geo_polygon(const fd_geo *g)
{
    float xy[FD_GEO_MAX_PTS * 2 + 2];
    int n = g->npts;
    if (n < 3) return;
    for (int i = 0; i < n; i++) {
        world_to_screen(g->p[i], &xy[2 * i], &xy[2 * i + 1]);
    }
    xy[2 * n] = xy[0];
    xy[2 * n + 1] = xy[1];
    stroke(xy, n + 1, rgb(FD_C_ACCENT), 2, 0);

    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; i++) {
        cx += xy[2 * i];
        cy += xy[2 * i + 1];
    }
    cx /= (float)n;
    cy /= (float)n;

    char buf[32];
    snprintf(buf, sizeof(buf), "S=%g", geo_polygon_area(g->p, n));
    add_tag((int)lroundf(cx) + 4, (int)lroundf(cy) - 20, buf, FD_C_TEXT);
}

static void draw_geo(void)
{
    fd_doc *d = model();
    for (int i = 0; i < d->geo_count; i++) {
        const fd_geo *g = &d->geos[i];
        if (!g->visible) continue;
        switch (g->kind) {
            case FD_GEO_POINT: draw_geo_point(g); break;
            case FD_GEO_SEGMENT:
            case FD_GEO_LINE:
            case FD_GEO_RAY: draw_geo_segment(g); break;
            case FD_GEO_DISTANCE: draw_geo_distance(g); break;
            case FD_GEO_ANGLE: draw_geo_angle(g); break;
            case FD_GEO_POLYGON: draw_geo_polygon(g); break;
            default: break;
        }
    }
}

/** 工具进行中的已选点/已选线，画个提示。 */
static void draw_tool_preview(void)
{
    if (g_tool == FD_TOOL_MOVE) return;
    uint32_t c = rgb(0xFFFFFF);

    for (int i = 0; i < g_step && i < 3; i++) {
        const pick_t *p = &g_pick[i];
        if (p->kind == SEL_POINT) {
            float sx, sy;
            world_to_screen(p->xy, &sx, &sy);
            draw_arc(sx, sy, 10.0f, 0.0f, (float)(2.0 * M_PI), c);
        } else if (p->kind == SEL_LINE) {
            float ax, ay, bx, by;
            world_to_screen(p->l0, &ax, &ay);
            world_to_screen(p->l1, &bx, &by);
            float xy[4] = {ax, ay, bx, by};
            stroke(xy, 2, c, 1, 2);
        } else if (p->kind == SEL_CURVE) {
            float sx, sy;
            world_to_screen(p->xy, &sx, &sy);
            draw_arc(sx, sy, 10.0f, 0.0f, (float)(2.0 * M_PI), c);
        }
    }

    if (g_tool == FD_TOOL_AREA && g_area_n > 0) {
        float xy[FD_GEO_MAX_PTS * 2];
        for (int i = 0; i < g_area_n; i++) {
            fd_geo *pt = model_geo(g_area_ids[i]);
            if (!pt) continue;
            world_to_screen(pt->p[0], &xy[2 * i], &xy[2 * i + 1]);
        }
        if (g_area_n >= 2) stroke(xy, g_area_n, c, 1, 2);
    }
}


static void set_feature_line(lv_obj_t *label, const char *text)
{
    if (!label) return;
    if (text && text[0]) {
        lv_label_set_text(label, text);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * 信息卡的两行特征：零点、极值（没有极值时有 y 截距就显示截距）。
 * 只对显函数算 —— 极坐标/参数/隐函数没有「零点」这个概念。
 */
static void update_features(const fd_curve *c)
{
    char roots_txt[96] = "";
    char ext_txt[96] = "";
    char point_txt[80] = "";

    if (c && g_tap_valid && g_tap_id == c->fn.id) {
        snprintf(point_txt, sizeof(point_txt), "点：(%.3g, %.3g)", g_tap_wx, g_tap_wy);
    }

    if (c && c->fn.mode == FD_PLOT_EXPLICIT) {
        char body[FD_SRC_CAP];
        fd_expr_strip_explicit_prefix(c->fn.src1, body, sizeof(body));
        if (body[0]) {
            char sub[FD_SRC_CAP];
            fd_subst_geo(body, sub, sizeof(sub));

            double params[4];
            model_param_values(params);
            fd_param ps[4];
            static const char *names[4] = {"a", "b", "c", "d"};
            for (int i = 0; i < 4; i++) {
                ps[i].name = names[i];
                ps[i].value = params[i];
            }
            bool deg = c->fn.degrees < 0 ? model()->settings.angle_degrees
                                         : c->fn.degrees != 0;
            const char *vars[1] = {"x"};
            fd_expr_error err;
            memset(&err, 0, sizeof(err));
            fd_expr *e = fd_expr_compile(sub, vars, 1, deg, ps, 4, &err);
            if (e) {
                fd_features f;
                memset(&f, 0, sizeof(f));
                fd_features_analyze(e, fd_vp_world_left(&model()->vp),
                                    fd_vp_world_right(&model()->vp), 3, &f);

                if (f.root_count > 0) {
                    int n = snprintf(roots_txt, sizeof(roots_txt), "零点：");
                    for (int i = 0; i < f.root_count; i++) {
                        int left = (int)sizeof(roots_txt) - n;
                        if (left < 16) break;
                        n += snprintf(roots_txt + n, (size_t)left, i ? " %.3g" : "%.3g",
                                      f.roots[i]);
                    }
                }
                if (f.extrema_count > 0) {
                    int n = snprintf(ext_txt, sizeof(ext_txt), "极值：");
                    for (int i = 0; i < f.extrema_count && i < 2; i++) {
                        int left = (int)sizeof(ext_txt) - n;
                        if (left < 24) break;
                        n += snprintf(ext_txt + n, (size_t)left, i ? " (%.3g,%.3g)" : "(%.3g,%.3g)",
                                      f.extrema[i].x, f.extrema[i].y);
                    }
                } else if (f.has_y_intercept) {
                    snprintf(ext_txt, sizeof(ext_txt), "y 截距：%.3g", f.y_intercept);
                }
                fd_expr_free(e);
            }
        }
    }

    set_feature_line(g_card_point, point_txt);
    set_feature_line(g_card_roots, roots_txt);
    set_feature_line(g_card_ext, ext_txt);

    lv_coord_t y = 46;
    if (point_txt[0] && g_card_point) {
        lv_obj_set_pos(g_card_point, 30, y);
        y += 18;
    }
    if (roots_txt[0] && g_card_roots) {
        lv_obj_set_pos(g_card_roots, 30, y);
        y += 18;
    }
    if (ext_txt[0] && g_card_ext) lv_obj_set_pos(g_card_ext, 30, y);

    int lines = (point_txt[0] ? 1 : 0) + (roots_txt[0] ? 1 : 0) +
                (ext_txt[0] ? 1 : 0);
    g_card_h = (lv_coord_t)(52 + lines * 18);
    if (g_card) lv_obj_set_height(g_card, g_card_h);
}

static bool cpx_eval_curve(const fd_curve *c, fd_complex *out, fd_expr_error *err)
{
    double params[4];
    model_param_values(params);
    fd_param ps[4];
    static const char *names[4] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; i++) {
        ps[i].name = names[i];
        ps[i].value = params[i];
    }
    fd_complex z = {0.0, 0.0};
    return fd_cpx_eval_text(c->fn.src1, ps, 4, z, out, err);
}

/** 复平面：单位圆 + 每条曲线的求值点（点旁标「名字=结果」）。 */
static void draw_complex(void)
{
    fd_doc *d = model();

    double upp = d->vp.units_per_px;
    if (upp > 0.0) {
        float r = (float)(1.0 / upp);
        if (r >= 3.0f && r <= 3000.0f) {
            float cx = fd_vp_to_screen_x(&d->vp, 0.0);
            float cy = fd_vp_to_screen_y(&d->vp, 0.0);
            draw_arc(cx, cy, r, 0.0f, (float)(2.0 * M_PI), rgb(FD_C_GRID));
        }
    }

    for (int i = 0; i < d->curve_count; i++) {
        const fd_curve *c = &d->curves[i];
        if (!c->fn.visible || c->fn.src1[0] == '\0') continue;

        fd_expr_error err;
        fd_complex v;
        memset(&err, 0, sizeof(err));
        if (!cpx_eval_curve(c, &v, &err)) continue;
        if (!isfinite(v.re) || !isfinite(v.im)) continue;

        float sx = fd_vp_to_screen_x(&d->vp, v.re);
        float sy = fd_vp_to_screen_y(&d->vp, v.im);
        fill_circle(sx, sy, 4.5f, rgb(model_color(c->fn.color_index)));

        char lbl[64];
        char txt[96];
        fd_cpx_label(v, lbl, sizeof(lbl));
        snprintf(txt, sizeof(txt), "%s=%s", c->name, lbl);
        add_tag((int)sx + 7, (int)sy - 18, txt, FD_C_TEXT);
    }
}

static void update_card(void)
{
    fd_curve *c = model_selected_curve();
    if (!g_card) return;
    if (!c) {
        lv_obj_add_flag(g_card, LV_OBJ_FLAG_HIDDEN);
        g_card_h = 52;
        return;
    }
    lv_obj_remove_flag(g_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_card_dot, lv_color_hex(model_color(c->fn.color_index)), 0);
    lv_label_set_text(g_card_name, c->name);

    char buf[FD_SRC_CAP * 2 + 32];
    if (c->fn.mode == FD_PLOT_PARAMETRIC) {
        snprintf(buf, sizeof(buf), "%s , %s", c->fn.src1, c->fn.src2);
    } else {
        snprintf(buf, sizeof(buf), "%s", c->fn.src1);
    }
    lv_label_set_text(g_card_expr, buf);

    if (model()->settings.complex_plane) {
        fd_expr_error err;
        fd_complex v;
        memset(&err, 0, sizeof(err));
        if (!cpx_eval_curve(c, &v, &err)) {
            lv_label_set_text(g_card_err, err.message[0] ? err.message : "表达式无效");
            lv_obj_remove_flag(g_card_err, LV_OBJ_FLAG_HIDDEN);
            set_feature_line(g_card_point, "");
            set_feature_line(g_card_roots, "");
            set_feature_line(g_card_ext, "");
            g_card_h = 52;
            lv_obj_set_height(g_card, g_card_h);
            return;
        }
        lv_obj_add_flag(g_card_err, LV_OBJ_FLAG_HIDDEN);

        char lbl[64];
        char txt[96];
        fd_cpx_label(v, lbl, sizeof(lbl));
        snprintf(txt, sizeof(txt), "结果：%s", lbl);
        set_feature_line(g_card_point, "");
        set_feature_line(g_card_roots, txt);
        set_feature_line(g_card_ext, "");
        g_card_h = 70;
        lv_obj_set_height(g_card, g_card_h);
        return;
    }

    if (g_plot.error_count > 0 && g_plot.error_msgs && g_plot.error_ids) {
        lv_label_set_text(g_card_err, g_plot.error_msgs[0]);
        lv_obj_remove_flag(g_card_err, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_card_err, LV_OBJ_FLAG_HIDDEN);
    }

    update_features(c);
}

static void repaint(void)
{
    if (!g_px) return;
    fd_doc *d = model();

    fill_bg();
    if (g_numbers) lv_obj_clean(g_numbers);

    if (d->settings.complex_plane) {
        /* 复平面模式不跑实数采样：每条表达式按复数求值，结果画成一个点。 */
        draw_grid_and_axes(&d->vp, &d->settings);
        draw_complex();
        draw_geo();
        draw_tool_preview();
        update_card();
        if (g_canvas) lv_obj_invalidate(g_canvas);
        return;
    }

    rebuild_plot();

    draw_grid_and_axes(&d->vp, &d->settings);
    draw_curves();
    draw_geo();
    draw_tool_preview();
    update_card();

    if (g_canvas) lv_obj_invalidate(g_canvas);
}

/* ------------------------------------------------------------------ */
/* 交互                                                                */
/* ------------------------------------------------------------------ */

static int hit_test(float x, float y)
{
    const float limit = FD_GRAPH_HIT_PX * FD_GRAPH_HIT_PX;
    float best = limit;
    int found = 0;

    for (int i = 0; i < g_plot.line_count; i++) {
        const fd_polyline *pl = &g_plot.lines[i];
        for (int k = 0; k < pl->count; k++) {
            float dx = pl->xy[2 * k] - x;
            float dy = pl->xy[2 * k + 1] - y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) {
                best = d2;
                found = pl->function_id;
            }
        }
    }
    return found;
}

/* 点击处可能有多条曲线经过：按距离收集，供「再次点击切换」用。 */
static int g_cycle_ids[FD_MAX_CURVES];
static int g_cycle_n;
static int g_cycle_idx;
static float g_cycle_x, g_cycle_y;
static uint32_t g_cycle_tick;

static int collect_curve_ids(float sx, float sy, int *out, int cap)
{
    float best[FD_MAX_CURVES];
    int n = 0;
    const float lim = FD_GRAPH_HIT_PX * FD_GRAPH_HIT_PX;

    for (int i = 0; i < g_plot.line_count; i++) {
        const fd_polyline *pl = &g_plot.lines[i];
        float bd = lim;
        for (int k = 0; k < pl->count; k++) {
            float dx = pl->xy[2 * k] - sx;
            float dy = pl->xy[2 * k + 1] - sy;
            float d2 = dx * dx + dy * dy;
            if (d2 < bd) bd = d2;
        }
        if (bd >= lim) continue;

        int idx = -1;
        for (int j = 0; j < n; j++) {
            if (out[j] == pl->function_id) {
                idx = j;
                break;
            }
        }
        if (idx >= 0) {
            if (bd < best[idx]) best[idx] = bd;
        } else if (n < cap) {
            out[n] = pl->function_id;
            best[n] = bd;
            n++;
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (best[j] < best[i]) {
                float td = best[i];
                best[i] = best[j];
                best[j] = td;
                int ti = out[i];
                out[i] = out[j];
                out[j] = ti;
            }
        }
    }
    return n;
}

static int collect_complex_ids(float sx, float sy, int *out, int cap)
{
    fd_doc *d = model();
    float best[FD_MAX_CURVES];
    int n = 0;
    const float lim = FD_GRAPH_HIT_PX * FD_GRAPH_HIT_PX;

    for (int i = 0; i < d->curve_count && n < cap; i++) {
        const fd_curve *c = &d->curves[i];
        if (!c->fn.visible || c->fn.src1[0] == '\0') continue;

        fd_expr_error err;
        fd_complex v;
        memset(&err, 0, sizeof(err));
        if (!cpx_eval_curve(c, &v, &err)) continue;
        if (!isfinite(v.re) || !isfinite(v.im)) continue;

        float px = fd_vp_to_screen_x(&d->vp, v.re);
        float py = fd_vp_to_screen_y(&d->vp, v.im);
        float dx = px - sx;
        float dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= lim) continue;

        out[n] = c->fn.id;
        best[n] = d2;
        n++;
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (best[j] < best[i]) {
                float td = best[i];
                best[i] = best[j];
                best[j] = td;
                int ti = out[i];
                out[i] = out[j];
                out[j] = ti;
            }
        }
    }
    return n;
}

/** 曲线折线上离点击位置最近的一个采样点（世界坐标）。 */
static bool nearest_curve_point(int id, float sx, float sy, double *wx, double *wy)
{
    fd_doc *d = model();
    float best = FD_GRAPH_HIT_PX * FD_GRAPH_HIT_PX;
    bool found = false;

    for (int i = 0; i < g_plot.line_count; i++) {
        const fd_polyline *pl = &g_plot.lines[i];
        if (pl->function_id != id) continue;
        for (int k = 0; k < pl->count; k++) {
            float dx = pl->xy[2 * k] - sx;
            float dy = pl->xy[2 * k + 1] - sy;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) {
                best = d2;
                *wx = fd_vp_to_world_x(&d->vp, pl->xy[2 * k]);
                *wy = fd_vp_to_world_y(&d->vp, pl->xy[2 * k + 1]);
                found = true;
            }
        }
    }
    return found;
}

/** 复平面：离点击位置最近的结果点。 */
static int complex_hit(float sx, float sy)
{
    fd_doc *d = model();
    float best = FD_GRAPH_HIT_PX * FD_GRAPH_HIT_PX;
    int found = 0;

    for (int i = 0; i < d->curve_count; i++) {
        const fd_curve *c = &d->curves[i];
        if (!c->fn.visible || c->fn.src1[0] == '\0') continue;
        fd_expr_error err;
        fd_complex v;
        memset(&err, 0, sizeof(err));
        if (!cpx_eval_curve(c, &v, &err)) continue;
        if (!isfinite(v.re) || !isfinite(v.im)) continue;
        float px = fd_vp_to_screen_x(&d->vp, v.re);
        float py = fd_vp_to_screen_y(&d->vp, v.im);
        float dx = px - sx;
        float dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            found = c->fn.id;
        }
    }
    return found;
}

/** 顶部按钮条 / 工具提示条 / 底部信息卡 + 缩放栏所占的区域不参与手势，避免误触。 */
static bool in_ui_band(float y)
{
    if (y < 56.0f) return true;
    if (g_tool != FD_TOOL_MOVE && y < 96.0f) return true;
    if (g_card && y > (float)(g_h - 16 - g_card_h)) return true;
    if (g_zoom_bar && y > (float)(g_h - 52)) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* 几何工具：拾取与提交                                                */
/* ------------------------------------------------------------------ */

static float seg_dist2(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax;
    float dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (len2 > 1e-6f) {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    float qx = ax + t * dx;
    float qy = ay + t * dy;
    float ex = px - qx;
    float ey = py - qy;
    return ex * ex + ey * ey;
}

static fd_geo *pick_point_obj(float sx, float sy, float tol)
{
    fd_doc *d = model();
    fd_geo *best = NULL;
    float bd = tol * tol;
    for (int i = 0; i < d->geo_count; i++) {
        fd_geo *g = &d->geos[i];
        if (g->kind != FD_GEO_POINT || !g->visible) continue;
        float px, py;
        world_to_screen(g->p[0], &px, &py);
        float dx = px - sx;
        float dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bd) {
            bd = d2;
            best = g;
        }
    }
    return best;
}

static fd_geo *pick_line_obj(float sx, float sy, float tol)
{
    fd_doc *d = model();
    fd_geo *best = NULL;
    float bd = tol * tol;
    for (int i = 0; i < d->geo_count; i++) {
        fd_geo *g = &d->geos[i];
        if (!g->visible) continue;
        if (g->kind != FD_GEO_SEGMENT && g->kind != FD_GEO_LINE &&
            g->kind != FD_GEO_RAY) {
            continue;
        }
        float ax, ay, bx, by;
        if (!geo_line_screen(g, &ax, &ay, &bx, &by)) continue;
        float d2 = seg_dist2(sx, sy, ax, ay, bx, by);
        if (d2 <= bd) {
            bd = d2;
            best = g;
        }
    }
    return best;
}

/** 把几何直线（或坐标轴）填进 pick_t。 */
static void fill_line_pick(const fd_geo *g, pick_t *out)
{
    out->kind = SEL_LINE;
    out->id = g->id;
    out->axis = 0;
    if (g->kind == FD_GEO_SEGMENT) {
        out->l0[0] = g->p[0][0];
        out->l0[1] = g->p[0][1];
        out->l1[0] = g->p[1][0];
        out->l1[1] = g->p[1][1];
    } else {
        out->l0[0] = g->p[0][0];
        out->l0[1] = g->p[0][1];
        out->l1[0] = g->p[0][0] + g->p[1][0];
        out->l1[1] = g->p[0][1] + g->p[1][1];
    }
}

static bool pick_line(float sx, float sy, pick_t *out)
{
    fd_geo *g = pick_line_obj(sx, sy, 16.0f);
    if (g) {
        fill_line_pick(g, out);
        return true;
    }

    fd_doc *d = model();
    if (!d->settings.show_axes) return false;

    float axis_y = fd_vp_to_screen_y(&d->vp, 0.0);
    float axis_x = fd_vp_to_screen_x(&d->vp, 0.0);
    if (fabsf(sy - axis_y) <= 16.0f) {
        out->kind = SEL_LINE;
        out->id = 0;
        out->axis = 1;
        screen_to_world(0.0f, axis_y, &out->l0[0], &out->l0[1]);
        screen_to_world((float)g_w, axis_y, &out->l1[0], &out->l1[1]);
        return true;
    }
    if (fabsf(sx - axis_x) <= 16.0f) {
        out->kind = SEL_LINE;
        out->id = 0;
        out->axis = 2;
        screen_to_world(axis_x, 0.0f, &out->l0[0], &out->l0[1]);
        screen_to_world(axis_x, (float)g_h, &out->l1[0], &out->l1[1]);
        return true;
    }
    return false;
}

static bool pick_any(float sx, float sy, pick_t *out)
{
    fd_geo *pt = pick_point_obj(sx, sy, 20.0f);
    if (pt) {
        out->kind = SEL_POINT;
        out->id = pt->id;
        out->xy[0] = pt->p[0][0];
        out->xy[1] = pt->p[0][1];
        return true;
    }

    fd_geo *ln = pick_line_obj(sx, sy, 14.0f);
    if (ln) {
        fill_line_pick(ln, out);
        return true;
    }

    int fn = hit_test(sx, sy);
    if (fn) {
        out->kind = SEL_CURVE;
        out->id = fn;
        screen_to_world(sx, sy, &out->xy[0], &out->xy[1]);
        return true;
    }
    return false;
}

/** 在曲线上取一点及该处的切线方向（直接从已采样折线取，四种模式都适用）。 */
static bool pick_curve(float sx, float sy, pick_t *out)
{
    int fn = hit_test(sx, sy);
    if (!fn) return false;

    const fd_polyline *best = NULL;
    int bi = -1;
    float bd = 1e9f;
    for (int i = 0; i < g_plot.line_count; i++) {
        const fd_polyline *pl = &g_plot.lines[i];
        if (pl->function_id != fn) continue;
        for (int k = 0; k < pl->count; k++) {
            float dx = pl->xy[2 * k] - sx;
            float dy = pl->xy[2 * k + 1] - sy;
            float d2 = dx * dx + dy * dy;
            if (d2 < bd) {
                bd = d2;
                best = pl;
                bi = k;
            }
        }
    }
    if (!best || bi < 0 || best->count < 2) return false;

    int i0 = bi > 0 ? bi - 1 : bi;
    int i1 = bi + 1 < best->count ? bi + 1 : bi;
    screen_to_world(best->xy[2 * i0], best->xy[2 * i0 + 1], &out->l0[0], &out->l0[1]);
    screen_to_world(best->xy[2 * i1], best->xy[2 * i1 + 1], &out->l1[0], &out->l1[1]);

    double dir[2];
    geo_unit_dir(out->l0, out->l1, dir);
    out->kind = SEL_CURVE;
    out->id = fn;
    out->xy[0] = out->l0[0];
    out->xy[1] = out->l0[1];
    out->l0[0] = dir[0];
    out->l0[1] = dir[1];
    out->l1[0] = dir[0];
    out->l1[1] = dir[1];
    return true;
}

/** 取一个点：优先吸附已有的点，否则在空白处新建。 */
static bool take_point(float sx, float sy, pick_t *out)
{
    fd_geo *p = pick_point_obj(sx, sy, 20.0f);
    if (!p) {
        p = model_add_geo(FD_GEO_POINT);
        if (!p) {
            ui_toast("作图对象已达上限");
            return false;
        }
        screen_to_world(sx, sy, &p->p[0][0], &p->p[0][1]);
        p->npts = 1;
    }
    out->kind = SEL_POINT;
    out->id = p->id;
    out->xy[0] = p->p[0][0];
    out->xy[1] = p->p[0][1];
    return true;
}

static void add_line_geo(fd_geo_kind kind, const double *base, const double *dir)
{
    fd_geo *g = model_add_geo(kind);
    if (!g) return;
    g->npts = 2;
    g->p[0][0] = base[0];
    g->p[0][1] = base[1];
    if (kind == FD_GEO_LINE || kind == FD_GEO_RAY) {
        double u[2];
        geo_unit_dir(base, dir, u);
        g->p[1][0] = u[0];
        g->p[1][1] = u[1];
    } else {
        g->p[1][0] = dir[0];
        g->p[1][1] = dir[1];
    }
}

static void add_point_geo(const double *xy)
{
    fd_geo *g = model_add_geo(FD_GEO_POINT);
    if (!g) return;
    g->npts = 1;
    g->p[0][0] = xy[0];
    g->p[0][1] = xy[1];
}

/** 轴对称 / 中心对称后的直线；center 非空时按点对称。 */
static void transform_line(const pick_t *src, const double *l0, const double *l1,
                           const double *center)
{
    double a[2], b[2];
    if (center) {
        a[0] = 2.0 * center[0] - src->l0[0];
        a[1] = 2.0 * center[1] - src->l0[1];
        b[0] = 2.0 * center[0] - src->l1[0];
        b[1] = 2.0 * center[1] - src->l1[1];
    } else {
        geo_reflect_point(src->l0, l0, l1, a);
        geo_reflect_point(src->l1, l0, l1, b);
    }

    fd_geo_kind kind = FD_GEO_LINE;
    fd_geo *orig = src->id ? model_geo(src->id) : NULL;
    if (orig && orig->kind == FD_GEO_SEGMENT) kind = FD_GEO_SEGMENT;
    else if (orig && orig->kind == FD_GEO_RAY) kind = FD_GEO_RAY;
    add_line_geo(kind, a, b);
}

static void update_hint(void);

static const char *step_requirement(void)
{
    const tool_def *td = &k_tools[g_tool];
    if (g_tool == FD_TOOL_PLOT) return td->hint;
    if (g_tool == FD_TOOL_AREA) {
        return g_area_n == 0 ? "依次点多边形顶点" : "继续点顶点，点回第一个点结束";
    }
    if (g_step >= td->steps) return td->hint;
    switch (td->kind[g_step]) {
        case TK_POINT: return "点一个点（空白处会新建点）";
        case TK_LINE: return "点一条直线/线段（坐标轴也行）";
        case TK_CURVE: return "点曲线上的点";
        case TK_ANY: return "点一个点、曲线或直线";
        default: return td->hint;
    }
}

static void tool_commit(void)
{
    const pick_t *a = &g_pick[0];
    const pick_t *b = &g_pick[1];
    const pick_t *c = &g_pick[2];

    switch (g_tool) {
        case FD_TOOL_DISTANCE: {
            fd_geo *g = model_add_geo(FD_GEO_DISTANCE);
            if (g) {
                g->npts = 2;
                g->p[0][0] = a->xy[0];
                g->p[0][1] = a->xy[1];
                g->p[1][0] = b->xy[0];
                g->p[1][1] = b->xy[1];
            }
            break;
        }
        case FD_TOOL_ANGLE: {
            /* 和旧版一致：三个点量角时，先按线段规则建出两条边（自动命名），
             * 角度对象本身只保存顶点和两条边的方向。 */
            add_line_geo(FD_GEO_SEGMENT, a->xy, b->xy);
            add_line_geo(FD_GEO_SEGMENT, a->xy, c->xy);

            fd_geo *g = model_add_geo(FD_GEO_ANGLE);
            if (g) {
                g->npts = 3;
                g->p[0][0] = a->xy[0];
                g->p[0][1] = a->xy[1];
                g->p[1][0] = b->xy[0];
                g->p[1][1] = b->xy[1];
                g->p[2][0] = c->xy[0];
                g->p[2][1] = c->xy[1];
            }
            break;
        }
        case FD_TOOL_MIDPOINT: {
            double m[2] = {(a->xy[0] + b->xy[0]) * 0.5,
                           (a->xy[1] + b->xy[1]) * 0.5};
            add_point_geo(m);
            break;
        }
        case FD_TOOL_SEGMENT:
        case FD_TOOL_LINE:
        case FD_TOOL_RAY: {
            fd_geo_kind kind = g_tool == FD_TOOL_SEGMENT
                                       ? FD_GEO_SEGMENT
                                       : (g_tool == FD_TOOL_LINE ? FD_GEO_LINE
                                                                 : FD_GEO_RAY);
            if (kind == FD_GEO_SEGMENT) {
                add_line_geo(FD_GEO_SEGMENT, a->xy, b->xy);
            } else {
                add_line_geo(kind, a->xy, b->xy);
            }
            break;
        }
        case FD_TOOL_PERP: {
            double dir[2], perp[2];
            geo_unit_dir(b->l0, b->l1, dir);
            geo_perp(dir, perp);
            add_line_geo(FD_GEO_LINE, a->xy, perp);
            break;
        }
        case FD_TOOL_PERP_BISECTOR: {
            double m[2] = {(a->xy[0] + b->xy[0]) * 0.5,
                           (a->xy[1] + b->xy[1]) * 0.5};
            double dir[2], perp[2];
            geo_unit_dir(a->xy, b->xy, dir);
            geo_perp(dir, perp);
            add_line_geo(FD_GEO_LINE, m, perp);
            break;
        }
        case FD_TOOL_PARALLEL: {
            double dir[2];
            geo_unit_dir(a->l0, a->l1, dir);
            add_line_geo(FD_GEO_LINE, b->xy, dir);
            break;
        }
        case FD_TOOL_BISECTOR: {
            /* 第二个点是顶点 */
            double u1[2], u2[2], dir[2];
            geo_unit_dir(b->xy, a->xy, u1);
            geo_unit_dir(b->xy, c->xy, u2);
            dir[0] = u1[0] + u2[0];
            dir[1] = u1[1] + u2[1];
            double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
            if (len < 1e-9) {
                geo_perp(u1, dir);
            } else {
                dir[0] /= len;
                dir[1] /= len;
            }
            add_line_geo(FD_GEO_LINE, b->xy, dir);
            break;
        }
        case FD_TOOL_TANGENT: {
            double end[2] = {a->xy[0] + a->l0[0], a->xy[1] + a->l0[1]};
            add_line_geo(FD_GEO_LINE, a->xy, end);
            break;
        }
        case FD_TOOL_AXIS_SYM: {
            if (a->kind == SEL_POINT) {
                double out[2];
                geo_reflect_point(a->xy, b->l0, b->l1, out);
                add_point_geo(out);
            } else if (a->kind == SEL_LINE) {
                transform_line(a, b->l0, b->l1, NULL);
            } else {
                ui_toast("暂时只支持点或直线的轴对称");
                return;
            }
            break;
        }
        case FD_TOOL_CENTER_SYM: {
            if (a->kind == SEL_POINT) {
                double out[2] = {2.0 * b->xy[0] - a->xy[0],
                                 2.0 * b->xy[1] - a->xy[1]};
                add_point_geo(out);
            } else if (a->kind == SEL_LINE) {
                transform_line(a, NULL, NULL, b->xy);
            } else {
                ui_toast("暂时只支持点或直线的中心对称");
                return;
            }
            break;
        }
        default:
            break;
    }

    model_checkpoint();
    g_step = 0;
    g_area_n = 0;
    memset(g_pick, 0, sizeof(g_pick));
    update_hint();
    repaint();
}

static void tool_tap(float sx, float sy)
{
    const tool_def *td = &k_tools[g_tool];

    if (g_tool == FD_TOOL_PLOT) {
        pick_t p;
        memset(&p, 0, sizeof(p));
        if (!take_point(sx, sy, &p)) return;
        model_checkpoint();
        repaint();
        return;
    }

    if (g_tool == FD_TOOL_AREA) {
        if (g_area_n >= 3) {
            fd_geo *first = model_geo(g_area_ids[0]);
            if (first) {
                float fx, fy;
                world_to_screen(first->p[0], &fx, &fy);
                float dx = sx - fx;
                float dy = sy - fy;
                if (dx * dx + dy * dy <= 18.0f * 18.0f) {
                    fd_geo *g = model_add_geo(FD_GEO_POLYGON);
                    if (g) {
                        g->npts = g_area_n;
                        for (int i = 0; i < g_area_n; i++) {
                            fd_geo *pt = model_geo(g_area_ids[i]);
                            if (!pt) continue;
                            g->p[i][0] = pt->p[0][0];
                            g->p[i][1] = pt->p[0][1];
                        }
                    }
                    model_checkpoint();
                    g_area_n = 0;
                    update_hint();
                    repaint();
                    ui_toast("面积已算出");
                    return;
                }
            }
        }
        if (g_area_n >= FD_GEO_MAX_PTS) {
            ui_toast("顶点太多了，点回第一个点收尾");
            return;
        }
        pick_t p;
        memset(&p, 0, sizeof(p));
        if (!take_point(sx, sy, &p)) return;
        g_area_ids[g_area_n++] = p.id;
        update_hint();
        repaint();
        return;
    }

    if (td->steps <= 0 || g_step >= td->steps) return;

    pick_t pk;
    memset(&pk, 0, sizeof(pk));
    bool ok = false;
    switch (td->kind[g_step]) {
        case TK_POINT: ok = take_point(sx, sy, &pk); break;
        case TK_LINE:
            ok = pick_line(sx, sy, &pk);
            if (!ok) ui_toast("请点一条直线/线段（坐标轴也行）");
            break;
        case TK_CURVE:
            ok = pick_curve(sx, sy, &pk);
            if (!ok) ui_toast("请点曲线上的点");
            break;
        case TK_ANY:
            ok = pick_any(sx, sy, &pk);
            if (!ok) ui_toast("请点一个点、曲线或直线");
            break;
        default:
            return;
    }
    if (!ok) return;

    g_pick[g_step] = pk;
    g_step++;
    if (g_step >= td->steps) {
        tool_commit();
    } else {
        update_hint();
        repaint();
    }
}

static void tool_set(fd_tool t)
{
    g_tool = t;
    g_step = 0;
    g_area_n = 0;
    memset(g_pick, 0, sizeof(g_pick));
    update_hint();
    repaint();
}

/** 移动工具下长按：删掉点到的东西。 */
static bool geo_delete_at(float sx, float sy)
{
    fd_geo *pt = pick_point_obj(sx, sy, 18.0f);
    if (pt) {
        int id = pt->id;
        char name[16];
        snprintf(name, sizeof(name), "%s", pt->name);
        model_delete_geo(id);
        model_checkpoint();
        char buf[48];
        snprintf(buf, sizeof(buf), "已删除点 %s", name);
        ui_toast(buf);
        repaint();
        return true;
    }
    fd_geo *ln = pick_line_obj(sx, sy, 14.0f);
    if (ln) {
        model_delete_geo(ln->id);
        model_checkpoint();
        ui_toast("已删除直线");
        repaint();
        return true;
    }
    return false;
}


void ui_graph_touch(int action, int count, float x0, float y0, float x1, float y1)
{
    if (!g_px) return;
    /* 工具面板是全屏浮层，它的点击交给 LVGL，绘图手势不参与。 */
    if (g_panel && !lv_obj_has_flag(g_panel, LV_OBJ_FLAG_HIDDEN)) return;

    switch (action) {
        case 0: /* DOWN */
            g_drag = !in_ui_band(y0) && g_tool == FD_TOOL_MOVE;
            g_pinching = false;
            g_moved = false;
            g_down_x = x0;
            g_down_y = y0;
            g_last_x = x0;
            g_last_y = y0;
            g_down_tick = lv_tick_get();
            break;

        case 5: /* POINTER_DOWN */
            g_pinching = true;
            g_drag = false;
            g_moved = true;
            {
                float dx = x1 - x0;
                float dy = y1 - y0;
                g_pinch_dist = sqrtf(dx * dx + dy * dy);
            }
            break;

        case 2: /* MOVE */
            if (count >= 2) {
                float dx = x1 - x0;
                float dy = y1 - y0;
                float dist = sqrtf(dx * dx + dy * dy);
                if (g_pinching && g_pinch_dist > 4.0f && dist > 4.0f) {
                    double factor = (double)dist / (double)g_pinch_dist;
                    if (factor > 0.2 && factor < 5.0) {
                        model_zoom(factor, (x0 + x1) / 2.0, (y0 + y1) / 2.0);
                        repaint();
                    }
                }
                g_pinch_dist = dist;
                g_pinching = true;
            } else if (g_drag) {
                float dx = x0 - g_last_x;
                float dy = y0 - g_last_y;
                if (fabsf(dx) > 0.5f || fabsf(dy) > 0.5f) {
                    model_pan(dx, dy);
                    repaint();
                }
                g_last_x = x0;
                g_last_y = y0;
            }
            if (fabsf(x0 - g_down_x) > 8.0f || fabsf(y0 - g_down_y) > 8.0f) g_moved = true;
            break;

        case 6: /* POINTER_UP */
            g_pinching = false;
            g_pinch_dist = 0.0f;
            g_drag = (count <= 1) && !in_ui_band(y0);
            g_last_x = x0;
            g_last_y = y0;
            break;

        case 1: /* UP */
            if (!g_moved && !in_ui_band(g_down_y)) {
                uint32_t elapsed = lv_tick_elaps(g_down_tick);
                if (g_tool != FD_TOOL_MOVE) {
                    tool_tap(g_down_x, g_down_y);
                } else if (elapsed > 700) {
                    /* 长按删除作图对象 */
                    geo_delete_at(g_down_x, g_down_y);
                } else if (elapsed < FD_TAP_MS) {
                    uint32_t now = lv_tick_get();
                    bool dbl =
                            (uint32_t)(now - g_last_tap_tick) < FD_DOUBLE_TAP_MS &&
                            fabsf(g_down_x - g_last_tap_x) < 30.0f &&
                            fabsf(g_down_y - g_last_tap_y) < 30.0f;
                    if (dbl) {
                        model_reset_view();
                        ui_toast("视图已复位");
                        g_last_tap_tick = 0;
                        g_cycle_n = 0;
                        repaint();
                    } else {
                        g_last_tap_tick = now;
                        g_last_tap_x = g_down_x;
                        g_last_tap_y = g_down_y;

                        bool complex = model()->settings.complex_plane;
                        bool same = g_cycle_n > 0 &&
                                    fabsf(g_down_x - g_cycle_x) <= 24.0f &&
                                    fabsf(g_down_y - g_cycle_y) <= 24.0f &&
                                    (uint32_t)(now - g_cycle_tick) > FD_DOUBLE_TAP_MS &&
                                    (uint32_t)(now - g_cycle_tick) <= 3000u;
                        if (same) {
                            /* 同一点再次点击：切换到下一条经过这里的曲线。 */
                            g_cycle_idx = (g_cycle_idx + 1) % g_cycle_n;
                        } else {
                            g_cycle_n = complex
                                                ? collect_complex_ids(g_down_x, g_down_y,
                                                                      g_cycle_ids, FD_MAX_CURVES)
                                                : collect_curve_ids(g_down_x, g_down_y,
                                                                    g_cycle_ids, FD_MAX_CURVES);
                            g_cycle_idx = 0;
                            g_cycle_x = g_down_x;
                            g_cycle_y = g_down_y;
                        }
                        g_cycle_tick = now;

                        if (g_cycle_n > 0) {
                            int id = g_cycle_ids[g_cycle_idx];
                            model_select(id);
                            g_tap_id = id;
                            g_tap_valid = !complex &&
                                          nearest_curve_point(id, g_down_x, g_down_y,
                                                              &g_tap_wx, &g_tap_wy);
                            repaint();
                        } else if (g_tap_valid) {
                            g_tap_valid = false;
                            g_tap_id = 0;
                            repaint();
                        }
                    }
                }
            }
            g_drag = false;
            g_pinching = false;
            break;

        default:
            break;
    }
}

void ui_graph_crown(float delta)
{
    if (!g_px || delta == 0.0f) return;
    fd_doc *d = model();
    if (d->settings.crown_zoom) {
        double factor = delta > 0.0f ? 1.12 : (1.0 / 1.12);
        model_zoom(factor, g_w / 2.0, g_h / 2.0);
    } else {
        model_pan(0.0, delta * 10.0);
    }
    repaint();
}

void ui_graph_invalidate(void)
{
    if (g_px) repaint();
}

/** 90ms 一拍：正在播放的参数往返滑动，播放期间强制 FAST 采样。 */
static void play_tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!g_px || !g_canvas) return;

    fd_doc *d = model();
    fd_slider *s = NULL;
    for (int i = 0; i < d->slider_count; i++) {
        if (d->sliders[i].playing) {
            s = &d->sliders[i];
            break;
        }
    }
    if (!s) {
        g_play_dir = 1;
        return;
    }

    double step = s->step > 0.0 ? s->step : 0.1;
    double next = s->value + step * (double)g_play_dir;
    if (next > s->max) {
        g_play_dir = -1;
        next = s->max - (next - s->max);
    }
    if (next < s->min) {
        g_play_dir = 1;
        next = s->min + (s->min - next);
    }
    if (next > s->max) next = s->max;
    if (next < s->min) next = s->min;
    s->value = next;

    g_force_fast = true;
    repaint();
    g_force_fast = false;
}

void ui_graph_detach(void)
{
    if (g_play_timer) {
        lv_timer_delete(g_play_timer);
        g_play_timer = NULL;
    }
    g_play_dir = 1;
    g_root = NULL;
    g_canvas = NULL;
    g_numbers = NULL;
    g_card = NULL;
    g_card_dot = NULL;
    g_card_name = NULL;
    g_card_expr = NULL;
    g_card_err = NULL;
    g_card_roots = NULL;
    g_card_ext = NULL;
    g_card_point = NULL;
    g_tap_valid = false;
    g_tap_id = 0;
    g_zoom_bar = NULL;
    g_zoom_slider = NULL;
    g_zoom_depth = 0.0f;
    g_hint = NULL;
    g_hint_label = NULL;
    g_panel = NULL;
    g_tool = FD_TOOL_MOVE;
    g_step = 0;
    g_area_n = 0;
    memset(g_pick, 0, sizeof(g_pick));
    g_drag = false;
    g_pinching = false;
    g_moved = false;
}

/* ------------------------------------------------------------------ */
/* 导出                                                                */
/* ------------------------------------------------------------------ */

static void sanitize(const char *title, char *out, size_t cap)
{
    char tmp[80];
    size_t n = 0;

    if (title) {
        for (const char *p = title; *p && n + 1 < sizeof(tmp); p++) {
            unsigned char ch = (unsigned char)*p;
            if (ch < 0x20 || ch == 0x7F) {
                tmp[n++] = '_';
            } else if (ch < 0x80 && strchr("\\/:*?\"<>|", (int)ch)) {
                tmp[n++] = '_';
            } else {
                tmp[n++] = (char)ch; /* 中文等 UTF-8 字节原样保留 */
            }
        }
    }
    tmp[n] = '\0';

    char *s = tmp;
    while (*s == ' ' || *s == '.' || *s == '_') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '.' || e[-1] == '_')) *--e = '\0';

    if (*s == '\0') {
        snprintf(out, cap, "graph.png");
        return;
    }

    if (strlen(s) > (cap > 5 ? cap - 5 : strlen(s))) s[cap - 5] = '\0';

    size_t len = strlen(s);
    if (len >= 4 && strcasecmp(s + len - 4, ".png") == 0) {
        snprintf(out, cap, "%s", s);
    } else {
        snprintf(out, cap, "%s.png", s);
    }
}

bool ui_graph_export_png(const char *title, char *out_path, size_t cap)
{
    if (!g_px || out_path == NULL || cap == 0) return false;

    /* 存档页导出时绘图屏已经被销毁，这里按当前模型重画一遍像素缓冲区。 */
    repaint();

    unsigned char *rgba = (unsigned char *)malloc((size_t)g_w * (size_t)g_h * 4u);
    if (!rgba) return false;

    for (int y = 0; y < g_h; y++) {
        const uint32_t *src = g_px + (size_t)y * (size_t)g_stride_px;
        unsigned char *dst = rgba + (size_t)y * (size_t)g_w * 4u;
        for (int x = 0; x < g_w; x++) {
            uint32_t c = src[x];
            dst[0] = (unsigned char)((c >> 16) & 0xFFu); /* R */
            dst[1] = (unsigned char)((c >> 8) & 0xFFu);  /* G */
            dst[2] = (unsigned char)(c & 0xFFu);         /* B */
            dst[3] = (unsigned char)((c >> 24) & 0xFFu); /* A */
            dst += 4;
        }
    }

    char name[64];
    sanitize(title, name, sizeof(name));

    /* 先编码到内存：相册（MediaStore）与私有目录两条路共用同一份 PNG。 */
    unsigned char *png = NULL;
    size_t png_len = 0;
    unsigned err = lodepng_encode32(&png, &png_len, rgba, (unsigned)g_w, (unsigned)g_h);
    free(rgba);
    if (err != 0 || !png) {
        lv_free(png);
        return false;
    }

    /* 1) 系统相册：Pictures/DrawFunc（Android 10+，MediaStore，免权限） */
    if (platform_save_png(name, png, png_len)) {
        lv_free(png);
        snprintf(out_path, cap, "Pictures/DrawFunc/%s", name);
        return true;
    }

    /* 2) 退回应用私有目录 archives/ */
    const char *dir = model_files_dir();
    if (!dir || dir[0] == '\0') {
        lv_free(png);
        return false;
    }

    char fs_path[768];
    snprintf(fs_path, sizeof(fs_path), "%s/archives/%s", dir, name);

    char path[800];
    snprintf(path, sizeof(path), "%s/archives", dir);
    mkdir(path, 0700); /* 已存在会返回 -1，忽略即可 */

    FILE *f = fopen(fs_path, "wb");
    if (!f) {
        lv_free(png);
        return false;
    }
    size_t wrote = fwrite(png, 1, png_len, f);
    fclose(f);
    lv_free(png);
    if (wrote != png_len) return false;

    snprintf(out_path, cap, "%s", fs_path);
    return true;
}

/* ------------------------------------------------------------------ */
/* 界面                                                                */
/* ------------------------------------------------------------------ */

static void card_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model_selected_curve()) ui_show(UI_SCR_PROPS);
}

static void list_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show(UI_SCR_LIST);
}

static void add_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model()->curve_count >= FD_MAX_CURVES) {
        ui_toast("曲线数量已达上限");
        return;
    }
    int id = model_add_curve(FD_PLOT_EXPLICIT);
    model_checkpoint();
    model_select(id);
    ui_show(UI_SCR_EDIT);
}

/* ------------------------------------------------------------------ */
/* 工具界面                                                            */
/* ------------------------------------------------------------------ */

static lv_obj_t *add_chip(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                          void *data)
{
    lv_obj_t *b = ui_button(parent, text, NULL);
    lv_obj_set_height(b, 32);
    lv_obj_set_style_pad_hor(b, 11, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    return b;
}

static void tool_chip_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_panel) lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
    tool_set((fd_tool)idx);
}

static void tools_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_panel) lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

static void panel_close_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_panel) lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

static void clear_geo_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (model()->geo_count == 0) {
        ui_toast("还没有作图对象");
        return;
    }
    model_clear_geo();
    model_checkpoint();
    repaint();
    ui_toast("已清除作图");
}

static void hint_exit_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    tool_set(FD_TOOL_MOVE);
}

static void update_hint(void)
{
    if (!g_hint) return;
    if (g_tool == FD_TOOL_MOVE) {
        lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_HIDDEN);

    const tool_def *td = &k_tools[g_tool];
    char buf[192];
    if (!td->open && g_step > 0 && g_step < td->steps) {
        snprintf(buf, sizeof(buf), "%s %d/%d：%s", td->label, g_step + 1,
                 td->steps, step_requirement());
    } else {
        snprintf(buf, sizeof(buf), "%s：%s", td->label, step_requirement());
    }
    lv_label_set_text(g_hint_label, buf);
}

static void build_hint(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    g_hint = bar;
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 36);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1A222C), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_hint_label = lv_label_create(bar);
    lv_label_set_text(g_hint_label, "");
    lv_obj_set_width(g_hint_label, LV_PCT(76));
    lv_label_set_long_mode(g_hint_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_hint_label, &font_cn_14, 0);
    lv_obj_set_style_text_color(g_hint_label, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(g_hint_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *exit = ui_button(bar, "退出", hint_exit_clicked);
    lv_obj_set_height(exit, 28);
    lv_obj_set_style_pad_hor(exit, 10, 0);
    lv_obj_align(exit, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

static void build_tool_panel(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    g_panel = p;
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(FD_C_BG), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(p, 12, 0);
    lv_obj_set_style_pad_row(p, 6, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = ui_label(row, "工具", FD_C_TEXT);
    lv_obj_set_style_text_font(title, &font_cn_20, 0);

    lv_obj_t *close = ui_button(row, "关闭", panel_close_clicked);
    lv_obj_set_height(close, 34);

    for (int s = 0; s < 5; s++) {
        lv_obj_t *sec = ui_label(p, k_section_names[s], FD_C_DIM);
        lv_obj_set_style_text_font(sec, &font_cn_14, 0);
        lv_obj_set_height(sec, 16);

        lv_obj_t *wrap = lv_obj_create(p);
        lv_obj_remove_style_all(wrap);
        lv_obj_set_width(wrap, LV_PCT(100));
        lv_obj_set_height(wrap, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_row(wrap, 6, 0);
        lv_obj_set_style_pad_column(wrap, 6, 0);
        lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

        for (int t = 0; t < FD_TOOL_COUNT; t++) {
            if (k_tools[t].section != s) continue;
            add_chip(wrap, k_tools[t].label, tool_chip_clicked,
                     (void *)(intptr_t)t);
        }
    }

    lv_obj_t *clear = add_chip(p, "清除作图", clear_geo_clicked, NULL);
    lv_obj_set_width(clear, LV_PCT(100));
    lv_obj_set_style_bg_color(clear, lv_color_hex(0x3A2024), 0);
    lv_obj_t *cl = lv_obj_get_child(clear, 0);
    if (cl) lv_obj_set_style_text_color(cl, lv_color_hex(FD_C_DANGER), 0);
}

static void build_toolbar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *list = ui_button(bar, "列表", list_clicked);
    lv_obj_set_height(list, 34);
    lv_obj_t *add = ui_button(bar, "新建", add_clicked);
    lv_obj_set_height(add, 34);
    lv_obj_t *tools = ui_button(bar, "工具", NULL);
    lv_obj_set_height(tools, 34);
    lv_obj_add_event_cb(tools, tools_clicked, LV_EVENT_CLICKED, NULL);
}

static void zoom_apply(double factor)
{
    if (g_w <= 0 || g_h <= 0) return;
    model_zoom(factor, g_w / 2.0, g_h / 2.0);
    repaint();
}

/** 按键：固定一大步（1.5 倍），按完滑轨回中点。 */
static void zoom_step_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int dir = (int)(intptr_t)lv_obj_get_user_data(btn);
    g_zoom_depth = 0.0f;
    if (g_zoom_slider) lv_slider_set_value(g_zoom_slider, 50, LV_ANIM_OFF);
    zoom_apply(dir > 0 ? 1.5 : 1.0 / 1.5);
}

/** 滑轨：相对量，只表示「相对上次位置的偏移」，最多一步 ±1.5 倍。 */
static void zoom_slider_changed(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    float depth = ((float)lv_slider_get_value(sl) - 50.0f) / 50.0f;
    double factor = pow(1.5, (double)(depth - g_zoom_depth));
    g_zoom_depth = depth;
    if (fabs(factor - 1.0) > 1e-9) zoom_apply(factor);
}

static void build_zoom_bar(lv_obj_t *parent)
{
    if (!model()->settings.show_zoom_bar) return;

    lv_obj_t *bar = lv_obj_create(parent);
    g_zoom_bar = bar;
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 300, 44);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

    lv_obj_t *minus = ui_button(bar, "−", zoom_step_clicked);
    lv_obj_set_size(minus, 44, 36);
    lv_obj_set_user_data(minus, (void *)(intptr_t)-1);

    lv_obj_t *sl = lv_slider_create(bar);
    g_zoom_slider = sl;
    lv_obj_set_size(sl, 170, 8);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x2A3542), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(FD_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, zoom_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *plus = ui_button(bar, "+", zoom_step_clicked);
    lv_obj_set_size(plus, 44, 36);
    lv_obj_set_user_data(plus, (void *)(intptr_t)1);

    g_zoom_depth = 0.0f;
}

static void build_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    g_card = card;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 244, 52);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A222C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    /* 打开缩放栏时信息卡上移，给底部那一栏让位。 */
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0,
                 model()->settings.show_zoom_bar ? -62 : -16);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    ui_set_click(card, card_clicked);

    g_card_dot = lv_obj_create(card);
    lv_obj_remove_style_all(g_card_dot);
    lv_obj_set_size(g_card_dot, 12, 12);
    lv_obj_set_style_radius(g_card_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_card_dot, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_card_dot, LV_OPA_COVER, 0);
    lv_obj_align(g_card_dot, LV_ALIGN_LEFT_MID, 12, -6);

    g_card_name = lv_label_create(card);
    lv_label_set_text(g_card_name, "a(x)");
    lv_obj_set_style_text_font(g_card_name, &font_cn_16, 0);
    lv_obj_set_style_text_color(g_card_name, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(g_card_name, LV_ALIGN_TOP_LEFT, 30, 6);

    g_card_expr = lv_label_create(card);
    lv_label_set_text(g_card_expr, "sin(x)");
    lv_label_set_long_mode(g_card_expr, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_card_expr, 198);
    lv_obj_set_style_text_font(g_card_expr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_card_expr, lv_color_hex(FD_C_DIM), 0);
    lv_obj_align(g_card_expr, LV_ALIGN_TOP_LEFT, 30, 28);

    g_card_err = lv_label_create(card);
    lv_label_set_text(g_card_err, "");
    lv_obj_add_flag(g_card_err, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(g_card_err, &font_cn_16, 0);
    lv_obj_set_style_text_color(g_card_err, lv_color_hex(FD_C_DANGER), 0);
    lv_obj_align(g_card_err, LV_ALIGN_TOP_LEFT, 30, 28);

    /* 零点 / 极值两行，有内容才显示（卡片高度随之变化）。 */
    g_card_point = lv_label_create(card);
    lv_label_set_text(g_card_point, "");
    lv_obj_add_flag(g_card_point, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(g_card_point, &font_cn_14, 0);
    lv_obj_set_style_text_color(g_card_point, lv_color_hex(FD_C_ACCENT), 0);
    lv_obj_align(g_card_point, LV_ALIGN_TOP_LEFT, 30, 46);

    g_card_roots = lv_label_create(card);
    lv_label_set_text(g_card_roots, "");
    lv_obj_add_flag(g_card_roots, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(g_card_roots, &font_cn_14, 0);
    lv_obj_set_style_text_color(g_card_roots, lv_color_hex(FD_C_TEXT), 0);
    lv_obj_align(g_card_roots, LV_ALIGN_TOP_LEFT, 30, 46);

    g_card_ext = lv_label_create(card);
    lv_label_set_text(g_card_ext, "");
    lv_obj_add_flag(g_card_ext, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(g_card_ext, &font_cn_14, 0);
    lv_obj_set_style_text_color(g_card_ext, lv_color_hex(FD_C_DIM), 0);
    lv_obj_align(g_card_ext, LV_ALIGN_TOP_LEFT, 30, 64);
}

static void ensure_buffer(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (g_px && w == g_w && h == g_h) return;
    free(g_px);
    g_px = NULL;
    /* 多留一点给对齐 */
    size_t bytes = (size_t)w * (size_t)h * 4u + 64u;
    g_px = (uint32_t *)calloc(1, bytes);
    if (!g_px) {
        g_w = g_h = 0;
        return;
    }
    g_w = w;
    g_h = h;
}

lv_obj_t *ui_graph_create(lv_obj_t *parent)
{
    g_root = lv_obj_create(parent);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(g_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_root, 0, 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t w = ui_width();
    lv_coord_t h = ui_height();
    model_set_screen(w, h);
    ensure_buffer(w, h);

    lv_obj_t *canvas = lv_canvas_create(g_root);
    g_canvas = canvas;
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    if (g_px) {
        lv_canvas_set_buffer(canvas, g_px, w, h, LV_COLOR_FORMAT_ARGB8888);
        lv_draw_buf_t *db = lv_canvas_get_draw_buf(canvas);
        if (db && db->data) {
            g_px = (uint32_t *)db->data;
            g_stride_px = (int)(db->header.stride / 4u);
        } else {
            g_stride_px = w;
        }
    }

    g_numbers = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_numbers);
    lv_obj_set_size(g_numbers, w, h);
    lv_obj_set_pos(g_numbers, 0, 0);
    lv_obj_set_style_bg_opa(g_numbers, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(g_numbers, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_numbers, LV_OBJ_FLAG_CLICKABLE);

    build_toolbar(g_root);
    build_zoom_bar(g_root);
    build_card(g_root);
    build_hint(g_root);
    /* 工具面板是全屏浮层，最后建才能盖在其它控件之上。 */
    build_tool_panel(g_root);

    g_tool = FD_TOOL_MOVE;
    g_step = 0;
    g_area_n = 0;
    memset(g_pick, 0, sizeof(g_pick));
    update_hint();

    g_drag = false;
    g_pinching = false;
    g_moved = false;
    g_last_tap_tick = 0;
    g_plot_valid = false;
    g_force_fast = false;

    if (!g_play_timer) g_play_timer = lv_timer_create(play_tick_cb, 90, NULL);

    repaint();
    return g_root;
}
