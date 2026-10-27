/**
 * 主机端 UI 冒烟测试：用一块内存显示 + 合成输入把每个界面都跑一遍。
 *
 * 手表/模拟器不在 CI 里，这个程序负责回答两个问题：
 *  1. 每个屏幕能不能建起来、画出来（不崩、不越界）；
 *  2. 常见交互路径（新建曲线、切屏、改设置）走一遍会不会踩到悬空对象。
 *
 * 它把每屏都导出成 PNG（默认 /tmp/fd_ui 下），可以直接看图。
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "geo.h"
#include "lvgl.h"
#include "model.h"
#include "src/libs/lodepng/lodepng.h"
#include "ui.h"

#define FD_W 466
#define FD_H 466

static uint8_t *g_out;   /* 最终画面（ARGB8888，内存序 B,G,R,A） */
static uint8_t *g_draw;  /* LVGL 的绘制缓冲 */
static uint32_t g_tick;
static const char *g_out_dir = "/tmp/fd_ui";

/* ---- 输入队列 ---------------------------------------------------- */

typedef struct {
    uint8_t action;
    uint8_t count;
    float x0, y0, x1, y1;
} smoke_event;

#define QCAP 256
static smoke_event g_q[QCAP];
static int g_head, g_tail;

static lv_indev_state_t action_state(int action)
{
    switch (action) {
        case 0:
        case 2:
        case 5:
            return LV_INDEV_STATE_PRESSED;
        default:
            return LV_INDEV_STATE_RELEASED;
    }
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static lv_point_t last = {0, 0};
    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    LV_UNUSED(indev);

    data->continue_reading = false;
    if (g_head != g_tail) {
        smoke_event e = g_q[g_head];
        g_head = (g_head + 1) % QCAP;
        last.x = (int32_t)e.x0;
        last.y = (int32_t)e.y0;
        last_state = action_state(e.action);
        data->continue_reading = (g_head != g_tail);
        /* 真机上触摸事件同时进 LVGL 队列和绘图手势队列，这里也照做一份。 */
        ui_touch(e.action, e.count, e.x0, e.y0, e.x1, e.y1);
    }
    data->point = last;
    data->state = last_state;
}

static void push_event(int action, int count, float x0, float y0, float x1, float y1)
{
    int next = (g_tail + 1) % QCAP;
    if (next == g_head) return;
    g_q[g_tail] = (smoke_event){(uint8_t)action, (uint8_t)count, x0, y0, x1, y1};
    g_tail = next;
}

/* ---- 显示 -------------------------------------------------------- */

static uint32_t tick_cb(void) { return g_tick; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    for (int32_t y = 0; y < h; y++) {
        memcpy(g_out + (size_t)(area->y1 + y) * FD_W * 4u + (size_t)area->x1 * 4u,
               px_map + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
    }
    lv_display_flush_ready(disp);
}

/* ---- 工具 -------------------------------------------------------- */

static void pump(int frames)
{
    for (int i = 0; i < frames; i++) {
        g_tick += 8;
        lv_timer_handler();
    }
}

static void dump(const char *name)
{
    unsigned char *rgba = (unsigned char *)malloc((size_t)FD_W * FD_H * 4u);
    if (!rgba) return;
    for (int i = 0; i < FD_W * FD_H; i++) {
        uint32_t c = ((const uint32_t *)g_out)[i];
        rgba[4 * i + 0] = (unsigned char)((c >> 16) & 0xFFu);
        rgba[4 * i + 1] = (unsigned char)((c >> 8) & 0xFFu);
        rgba[4 * i + 2] = (unsigned char)(c & 0xFFu);
        rgba[4 * i + 3] = (unsigned char)((c >> 24) & 0xFFu);
    }

    char path[512];
    snprintf(path, sizeof(path), "A:%s/%s.png", g_out_dir, name);
    unsigned err = lodepng_encode32_file(path, rgba, FD_W, FD_H);
    free(rgba);
    printf("dump %-12s %s (err %u)\n", name, err == 0 ? path : "FAILED", err);
}

static void drag(float x0, float y0, float x1, float y1)
{
    push_event(0, 1, x0, y0, 0, 0);
    pump(2);
    for (int i = 1; i <= 8; i++) {
        float t = (float)i / 8.0f;
        push_event(2, 1, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 0, 0);
        pump(1);
    }
    push_event(1, 1, x1, y1, 0, 0);
    pump(4);
}

static void tap(float x, float y)
{
    push_event(0, 1, x, y, 0, 0);
    pump(2);
    push_event(1, 1, x, y, 0, 0);
    pump(4);
}

/* ---- 主流程 ------------------------------------------------------ */

int main(int argc, char **argv)
{
    if (argc > 1) g_out_dir = argv[1];

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/data'", g_out_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "cannot create %s\n", g_out_dir);
        return 2;
    }

    g_out = (uint8_t *)calloc(1, (size_t)FD_W * FD_H * 4u);
    g_draw = (uint8_t *)calloc(1, (size_t)FD_W * FD_H * 4u);
    if (!g_out || !g_draw) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(FD_W, FD_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(disp, g_draw, NULL, (uint32_t)(FD_W * FD_H * 4u),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);

    char files_dir[512];
    snprintf(files_dir, sizeof(files_dir), "%s/data", g_out_dir);
    app_set_files_dir(files_dir);
    app_set_size(FD_W, FD_H);
    app_init();
    pump(5);

    /* 字体自检：中文、ASCII、常用标点都要能拿到字形，否则界面上会是方块。 */
    {
        lv_font_glyph_dsc_t dsc;
        const uint32_t probes[] = {
                'a', 's', 'i', 'n', 0x5217, 0x8868, 0x65B0, 0x5EFA,
                0xFF0C, 0x3001, 0x3002, 0xFF08, 0xFF09, 0xFF1F, 0xFF01, 0x3000,
                0x00B7, 0x2014, 0x2026, 0x2212,
        };
        int missing = 0;
        for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
            memset(&dsc, 0, sizeof(dsc));
            bool ok = lv_font_get_glyph_dsc(&font_cn_16, &dsc, probes[i], 0);
            printf("font U+%04X %s w=%d\n", (unsigned)probes[i], ok ? "ok" : "MISSING",
                   ok ? (int)dsc.box_w : -1);
            if (!ok) missing++;
        }
        if (missing) printf("FONT MISSING %d\n", missing);
    }

    dump("01_graph");

    /* 拖动 / 捏合 / 双击复位 */
    drag(200, 220, 230, 260);
    dump("02_graph_pan");

    ui_show(UI_SCR_LIST);
    pump(6);
    dump("03_list");

    /* 新建一条曲线 → 编辑器 */
    model_add_curve(FD_PLOT_EXPLICIT);
    model_sync_sliders();
    model_select(model()->curves[model()->curve_count - 1].fn.id);
    ui_show(UI_SCR_EDIT);
    pump(6);
    dump("04_editor");

    model_select(model()->curves[0].fn.id);
    ui_show(UI_SCR_PROPS);
    pump(6);
    dump("05_props");

    ui_show(UI_SCR_ARCHIVE);
    pump(6);
    dump("06_archive");

    ui_show(UI_SCR_SETTINGS);
    pump(6);
    dump("07_settings");

    ui_show(UI_SCR_ABOUT);
    pump(6);
    dump("08_about");

    /* 回到图像页：确认重新建屏后还能画（之前绘图层被销毁过） */
    ui_show(UI_SCR_GRAPH);
    pump(8);
    dump("09_graph_again");

    /* 设一个参数为 a 的曲线，检查滑动条行 */
    {
        fd_curve *c = model_curve(model()->curves[0].fn.id);
        if (c) {
            snprintf(c->fn.src1, sizeof(c->fn.src1), "a*sin(x)");
            model_sync_sliders();
        }
        ui_show(UI_SCR_LIST);
        pump(6);
        dump("10_list_sliders");
    }

    /* 播放参数：切回图像页跑几拍，确认不崩且值在动 */
    {
        if (model()->slider_count > 0) {
            model()->sliders[0].playing = true;
            double before = model()->sliders[0].value;
            ui_show(UI_SCR_GRAPH);
            pump(40);
            double after = model()->sliders[0].value;
            printf("play: %.3f -> %.3f\n", before, after);
            model()->sliders[0].playing = false;
            dump("11_graph_play");
        }
    }

    /* 几何工具：打开面板、选「线段」、点两个点，再直接造一整套对象看绘制与存档往返 */
    {
        tap(301, 30); /* 工具栏第三个按钮「工具」 */
        dump("12_tools");

        tap(48, 341); /* 面板里「线段」 */
        int n0 = model()->geo_count;
        tap(150, 250);
        tap(300, 300);
        printf("tool segment: geo %d -> %d\n", n0, model()->geo_count);
        dump("13_geo_tool");

        fd_geo *g;
        const double pt[][2] = {{-6.0, 3.0}, {-1.0, 5.0}, {3.0, 1.0}, {4.0, -3.0}};
        for (int i = 0; i < 4; i++) {
            g = model_add_geo(FD_GEO_POINT);
            if (!g) continue;
            g->p[0][0] = pt[i][0];
            g->p[0][1] = pt[i][1];
            g->npts = 1;
        }
        g = model_add_geo(FD_GEO_SEGMENT);
        if (g) {
            g->npts = 2;
            g->p[0][0] = -7.0;
            g->p[0][1] = -4.0;
            g->p[1][0] = -2.0;
            g->p[1][1] = -1.5;
        }
        g = model_add_geo(FD_GEO_DISTANCE);
        if (g) {
            g->npts = 2;
            g->p[0][0] = 0.0;
            g->p[0][1] = -5.0;
            g->p[1][0] = 5.0;
            g->p[1][1] = -2.0;
        }
        g = model_add_geo(FD_GEO_ANGLE);
        if (g) {
            g->npts = 3;
            g->p[0][0] = 0.0;
            g->p[0][1] = 2.0;
            g->p[1][0] = 4.0;
            g->p[1][1] = 2.0;
            g->p[2][0] = -1.0;
            g->p[2][1] = 6.0;
        }
        g = model_add_geo(FD_GEO_POLYGON);
        if (g) {
            g->npts = 4;
            g->p[0][0] = 5.0;
            g->p[0][1] = 5.0;
            g->p[1][0] = 8.0;
            g->p[1][1] = 6.0;
            g->p[2][0] = 7.0;
            g->p[2][1] = 1.0;
            g->p[3][0] = 4.5;
            g->p[3][1] = 1.5;
        }
        g = model_add_geo(FD_GEO_LINE);
        if (g) {
            g->npts = 2;
            g->p[0][0] = -2.0; /* 基点 */
            g->p[0][1] = 5.0;
            g->p[1][0] = 0.8; /* 单位方向 */
            g->p[1][1] = -0.6;
        }
        g = model_add_geo(FD_GEO_RAY);
        if (g) {
            g->npts = 2;
            g->p[0][0] = 2.0; /* 起点 */
            g->p[0][1] = -6.0;
            g->p[1][0] = 1.0;
            g->p[1][1] = 0.4;
        }

        ui_graph_invalidate();
        pump(4);
        dump("14_geo");

        int before = model()->geo_count;
        bool saved = model_save();
        model_clear_geo();
        bool loaded = model_load();
        printf("geo save=%d load=%d %d -> %d\n", (int)saved, (int)loaded, before,
               model()->geo_count);
        ui_graph_invalidate();
        pump(4);
        dump("15_geo_reload");
    }

    /* 字母页键盘：切换到「字母」，再点大小写键看标签变化 */
    {
        ui_show(UI_SCR_LIST);
        pump(4);
        model_select(model()->curves[0].fn.id);
        ui_show(UI_SCR_EDIT);
        pump(4);
        tap(392, 158); /* 页签「字母」 */
        dump("16_editor_letters");
        tap(66, 387); /* 左下角大小写键 */
        dump("17_editor_upper");
    }

    /* 缩放栏：打开设置后图像屏底部出现 − / 滑轨 / + */
    model()->settings.show_zoom_bar = true;
    ui_show(UI_SCR_GRAPH);
    pump(6);
    dump("18_graph_zoom_bar");
    model()->settings.show_zoom_bar = false;

    /* 几何对象在列表里的行（名字 + 类型/度量值 + 显隐 + 删除） */
    ui_show(UI_SCR_LIST);
    pump(4);
    dump("19_list_geo");
    ui_show(UI_SCR_GRAPH);
    pump(4);

    /* 导出 PNG 的命名对话框（文本输入 + 键盘） */
    ui_show(UI_SCR_ARCHIVE);
    pump(4);
    ui_archive_open_export_dialog();
    pump(4);
    dump("21_export_dialog");
    ui_archive_close_dialog();

    /* 点击曲线：卡片显示该点坐标 + 解析式 + 零点/极值 */
    ui_show(UI_SCR_GRAPH);
    model_reset_view();
    ui_graph_invalidate();
    pump(4);
    tap(233, 233);
    pump(4);
    dump("22_curve_point");

    /* 复平面：一条复数表达式，结果画成点 */
    {
        int id = model_add_curve(FD_PLOT_EXPLICIT);
        fd_curve *c = model_curve(id);
        if (c) {
            snprintf(c->fn.src1, sizeof(c->fn.src1), "2+3i");
            model_select(id);
        }
        model()->settings.complex_plane = true;
        ui_show(UI_SCR_GRAPH);
        pump(6);
        dump("20_complex");
        model()->settings.complex_plane = false;
    }

    /* PNG 导出（先回到图像屏，导出按模型重画） */
    ui_show(UI_SCR_GRAPH);
    pump(6);
    {
        char path[512];
        if (ui_graph_export_png("smoke", path, sizeof(path))) {
            printf("export   %s\n", path);
        } else {
            printf("export   FAILED\n");
        }
    }

    app_deinit();
    lv_display_delete(disp);
    lv_deinit();

    free(g_out);
    free(g_draw);
    printf("SMOKE OK\n");
    return 0;
}
