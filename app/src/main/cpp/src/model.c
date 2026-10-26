#include "model.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "expr.h"
#include "viewport.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint32_t k_palette[FD_PALETTE_COUNT] = {
        0x7FD8FF, /* 天蓝 */
        0xFFB4AB, /* 珊瑚 */
        0xB7F397, /* 嫩绿 */
        0xD0BCFF, /* 淡紫 */
        0xFFD8A6, /* 暖橙 */
        0x8FF0E4, /* 青绿 */
        0xFFF2A0, /* 奶油黄 */
        0xF8A8D8, /* 樱花粉 */
        0xA8C7FF, /* 长春花蓝 */
        0x9EE8A8, /* 薄荷 */
        0xFFC2A0, /* 蜜桃 */
        0xC8E6A0  /* 抹茶 */
};

static fd_doc g_doc;
static fd_doc g_hist[FD_UNDO_DEPTH];
static int g_hist_len;
static int g_hist_pos;
static char g_files_dir[512];

/* ------------------------------------------------------------------ */
/* 基础                                                                */
/* ------------------------------------------------------------------ */

fd_doc *model(void) { return &g_doc; }

uint32_t model_color(int color_index)
{
    int i = color_index % FD_PALETTE_COUNT;
    if (i < 0) i += FD_PALETTE_COUNT;
    return k_palette[i];
}

void model_set_screen(int32_t w, int32_t h)
{
    if (w <= 1 || h <= 1) return;
    g_doc.screen_w = w;
    g_doc.screen_h = h;
    if (!g_doc.view_initialized || g_doc.vp.width_px <= 1.0f) {
        g_doc.vp = fd_vp_for_range((float)w, (float)h, -10.0, 10.0);
        g_doc.view_initialized = true;
    } else {
        fd_vp_resize(&g_doc.vp, (float)w, (float)h);
    }
}

static void doc_defaults(fd_doc *d)
{
    memset(d, 0, sizeof(*d));
    d->settings.show_grid = true;
    d->settings.show_axes = true;
    d->settings.show_numbers = false;
    d->settings.angle_degrees = false;
    d->settings.quality = FD_QUALITY_HIGH;
    d->settings.crown_zoom = true;
    d->next_id = 1;
    d->geo_next_id = 1;
    d->selected_id = 0;
    d->screen_w = 0;
    d->screen_h = 0;
}

void model_init(void)
{
    doc_defaults(&g_doc);

    fd_curve *c = &g_doc.curves[0];
    c->fn.id = 1;
    c->fn.mode = FD_PLOT_EXPLICIT;
    strcpy(c->fn.src1, "sin(x)");
    c->fn.src2[0] = '\0';
    c->fn.t_min = 0.0;
    c->fn.t_max = 2.0 * M_PI;
    c->fn.color_index = 0;
    c->fn.visible = true;
    c->fn.degrees = -1;
    strcpy(c->name, "a(x)");
    c->width = 2;
    c->dash = 0;
    g_doc.curve_count = 1;
    g_doc.selected_id = 1;
    g_doc.next_id = 2;

    g_hist[0] = g_doc;
    g_hist_len = 1;
    g_hist_pos = 0;
}

void model_deinit(void)
{
    memset(&g_doc, 0, sizeof(g_doc));
    memset(g_hist, 0, sizeof(g_hist));
    g_hist_len = 0;
    g_hist_pos = 0;
}

/* ------------------------------------------------------------------ */
/* 撤销 / 重做                                                          */
/* ------------------------------------------------------------------ */

void model_checkpoint(void)
{
    if (g_hist_len <= 0) {
        g_hist[0] = g_doc;
        g_hist_len = 1;
        g_hist_pos = 0;
    }
    /* 丢掉 redo 分支 */
    g_hist_len = g_hist_pos + 1;
    if (g_hist_len >= FD_UNDO_DEPTH) {
        memmove(&g_hist[0], &g_hist[1], sizeof(g_hist[0]) * (FD_UNDO_DEPTH - 1));
        g_hist_len = FD_UNDO_DEPTH - 1;
        g_hist_pos = g_hist_len - 1;
    }
    g_hist[g_hist_len] = g_doc;
    g_hist_len++;
    g_hist_pos = g_hist_len - 1;
}

bool model_undo(void)
{
    if (g_hist_pos <= 0) return false;
    g_hist_pos--;
    g_doc = g_hist[g_hist_pos];
    return true;
}

bool model_redo(void)
{
    if (g_hist_pos + 1 >= g_hist_len) return false;
    g_hist_pos++;
    g_doc = g_hist[g_hist_pos];
    return true;
}

bool model_can_undo(void) { return g_hist_pos > 0; }
bool model_can_redo(void) { return g_hist_pos + 1 < g_hist_len; }

/* ------------------------------------------------------------------ */
/* 曲线                                                                */
/* ------------------------------------------------------------------ */

fd_curve *model_curve(int id)
{
    for (int i = 0; i < g_doc.curve_count; i++) {
        if (g_doc.curves[i].fn.id == id) return &g_doc.curves[i];
    }
    return NULL;
}

fd_curve *model_selected_curve(void)
{
    fd_curve *c = model_curve(g_doc.selected_id);
    if (c) return c;
    return g_doc.curve_count > 0 ? &g_doc.curves[0] : NULL;
}

void model_select(int id)
{
    if (model_curve(id)) g_doc.selected_id = id;
}

void model_next_name(char *out, size_t cap)
{
    for (int n = 1; n < 1000; n++) {
        char buf[FD_NAME_CAP];
        snprintf(buf, sizeof(buf), "f%d", n);
        bool used = false;
        for (int i = 0; i < g_doc.curve_count; i++) {
            if (strcmp(g_doc.curves[i].name, buf) == 0) {
                used = true;
                break;
            }
        }
        if (!used) {
            snprintf(out, cap, "%s", buf);
            return;
        }
    }
    snprintf(out, cap, "f");
}

int model_add_curve(fd_plot_mode mode)
{
    if (g_doc.curve_count >= FD_MAX_CURVES) return 0;

    fd_curve *c = &g_doc.curves[g_doc.curve_count];
    memset(c, 0, sizeof(*c));
    c->fn.id = g_doc.next_id++;
    c->fn.mode = mode;
    c->fn.t_min = 0.0;
    c->fn.t_max = 2.0 * M_PI;
    c->fn.color_index = g_doc.curve_count % FD_PALETTE_COUNT;
    c->fn.visible = true;
    c->fn.degrees = -1;
    c->width = 2;
    c->dash = 0;
    model_next_name(c->name, sizeof(c->name));

    switch (mode) {
        case FD_PLOT_EXPLICIT:
            strcpy(c->fn.src1, "x");
            break;
        case FD_PLOT_POLAR:
            strcpy(c->fn.src1, "1");
            break;
        case FD_PLOT_PARAMETRIC:
            strcpy(c->fn.src1, "cos(t)");
            strcpy(c->fn.src2, "sin(t)");
            break;
        case FD_PLOT_IMPLICIT:
            strcpy(c->fn.src1, "x^2+y^2=1");
            break;
    }

    g_doc.curve_count++;
    g_doc.selected_id = c->fn.id;
    return c->fn.id;
}

void model_delete_curve(int id)
{
    for (int i = 0; i < g_doc.curve_count; i++) {
        if (g_doc.curves[i].fn.id != id) continue;
        memmove(&g_doc.curves[i], &g_doc.curves[i + 1],
                sizeof(g_doc.curves[0]) * (size_t)(g_doc.curve_count - i - 1));
        g_doc.curve_count--;
        if (g_doc.selected_id == id) {
            g_doc.selected_id = g_doc.curve_count > 0 ? g_doc.curves[0].fn.id : 0;
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* 几何作图                                                            */
/* ------------------------------------------------------------------ */

/** 点的名字：A..Z，然后 A1..B1.. */
static void geo_auto_point_name(char *out, size_t cap)
{
    for (int i = 0; i < 26 * 99; i++) {
        char buf[12];
        if (i < 26) snprintf(buf, sizeof(buf), "%c", 'A' + i);
        else snprintf(buf, sizeof(buf), "%c%d", 'A' + (i % 26), i / 26);
        bool used = false;
        for (int k = 0; k < g_doc.geo_count; k++) {
            if (g_doc.geos[k].kind == FD_GEO_POINT &&
                strcmp(g_doc.geos[k].name, buf) == 0) {
                used = true;
                break;
            }
        }
        if (!used) {
            snprintf(out, cap, "%s", buf);
            return;
        }
    }
    snprintf(out, cap, "P");
}

fd_geo *model_add_geo(fd_geo_kind kind)
{
    if (g_doc.geo_count >= FD_GEO_MAX) return NULL;
    fd_geo *g = &g_doc.geos[g_doc.geo_count++];
    memset(g, 0, sizeof(*g));
    g->id = g_doc.geo_next_id++;
    g->kind = kind;
    g->visible = true;
    if (kind == FD_GEO_POINT) geo_auto_point_name(g->name, sizeof(g->name));
    return g;
}

fd_geo *model_geo(int id)
{
    for (int i = 0; i < g_doc.geo_count; i++) {
        if (g_doc.geos[i].id == id) return &g_doc.geos[i];
    }
    return NULL;
}

void model_delete_geo(int id)
{
    for (int i = 0; i < g_doc.geo_count; i++) {
        if (g_doc.geos[i].id != id) continue;
        memmove(&g_doc.geos[i], &g_doc.geos[i + 1],
                sizeof(g_doc.geos[0]) * (size_t)(g_doc.geo_count - i - 1));
        g_doc.geo_count--;
        return;
    }
}

void model_clear_geo(void)
{
    g_doc.geo_count = 0;
}

/* ------------------------------------------------------------------ */
/* 滑动条 / 参数                                                        */
/* ------------------------------------------------------------------ */

fd_slider *model_slider(const char *param)
{
    for (int i = 0; i < g_doc.slider_count; i++) {
        if (strcmp(g_doc.sliders[i].param, param) == 0) return &g_doc.sliders[i];
    }
    return NULL;
}

fd_slider *model_slider_by_id(int id)
{
    for (int i = 0; i < g_doc.slider_count; i++) {
        if (g_doc.sliders[i].id == id) return &g_doc.sliders[i];
    }
    return NULL;
}

/** 把标识符 token 拆出来，收集单字母 a/b/c/d。 */
static void collect_params(const char *src, bool seen[4])
{
    if (!src) return;
    const char *p = src;
    while (*p) {
        unsigned char ch = (unsigned char)*p;
        if (!(isalpha(ch) || ch == '_')) {
            p++;
            continue;
        }
        const char *start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
        size_t len = (size_t)(p - start);
        if (len == 1) {
            char c = (char)tolower((unsigned char)*start);
            if (c >= 'a' && c <= 'd') seen[c - 'a'] = true;
        }
    }
}

void model_sync_sliders(void)
{
    bool seen[4] = {false, false, false, false};
    for (int i = 0; i < g_doc.curve_count; i++) {
        collect_params(g_doc.curves[i].fn.src1, seen);
        if (g_doc.curves[i].fn.mode == FD_PLOT_PARAMETRIC) {
            collect_params(g_doc.curves[i].fn.src2, seen);
        }
    }

    /* 删掉不再用到的 */
    for (int i = 0; i < g_doc.slider_count;) {
        int idx = g_doc.sliders[i].param[0] - 'a';
        bool keep = idx >= 0 && idx < 4 && seen[idx];
        if (keep) {
            i++;
        } else {
            memmove(&g_doc.sliders[i], &g_doc.sliders[i + 1],
                    sizeof(g_doc.sliders[0]) * (size_t)(g_doc.slider_count - i - 1));
            g_doc.slider_count--;
        }
    }

    /* 补上新出现的 */
    for (int k = 0; k < 4; k++) {
        if (!seen[k]) continue;
        char name[2] = {(char)('a' + k), '\0'};
        if (model_slider(name)) continue;
        if (g_doc.slider_count >= FD_MAX_SLIDERS) continue;
        fd_slider *s = &g_doc.sliders[g_doc.slider_count++];
        memset(s, 0, sizeof(*s));
        s->id = g_doc.next_id++;
        snprintf(s->param, sizeof(s->param), "%s", name);
        s->value = 1.0;
        s->min = -10.0;
        s->max = 10.0;
        s->step = 0.1;
        s->playing = false;
    }
}

void model_param_values(double out[4])
{
    for (int i = 0; i < 4; i++) out[i] = 1.0;
    for (int i = 0; i < g_doc.slider_count; i++) {
        int idx = g_doc.sliders[i].param[0] - 'a';
        if (idx >= 0 && idx < 4) out[idx] = g_doc.sliders[i].value;
    }
}

/* ------------------------------------------------------------------ */
/* 视口                                                                */
/* ------------------------------------------------------------------ */

void model_reset_view(void)
{
    if (g_doc.screen_w <= 1 || g_doc.screen_h <= 1) return;
    g_doc.vp = fd_vp_for_range((float)g_doc.screen_w, (float)g_doc.screen_h, -10.0, 10.0);
    g_doc.view_initialized = true;
}

void model_zoom(double factor, double fx, double fy)
{
    if (!isfinite(factor) || factor <= 0.0) return;
    fd_vp_zoom(&g_doc.vp, factor, (float)fx, (float)fy);
}

void model_pan(double dx, double dy)
{
    fd_vp_pan(&g_doc.vp, (float)dx, (float)dy);
}

/* ------------------------------------------------------------------ */
/* 序列化                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} strbuf;

static void sb_reserve(strbuf *sb, size_t extra)
{
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t next = sb->cap ? sb->cap : 1024;
    while (next < sb->len + extra + 1) next *= 2;
    char *p = (char *)realloc(sb->buf, next);
    if (!p) return;
    sb->buf = p;
    sb->cap = next;
}

static void sb_printf(strbuf *sb, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    sb_reserve(sb, (size_t)n);
    if (!sb->buf) return;
    memcpy(sb->buf + sb->len, tmp, (size_t)n);
    sb->len += (size_t)n;
    sb->buf[sb->len] = '\0';
}

/** 写入一个字段：转义 \t \n \\。 */
static void sb_field(strbuf *sb, const char *s)
{
    if (!s) return;
    for (const char *p = s; *p; p++) {
        char out[3] = {0, 0, 0};
        switch (*p) {
            case '\t': out[0] = '\\'; out[1] = 't'; break;
            case '\n': out[0] = '\\'; out[1] = 'n'; break;
            case '\\': out[0] = '\\'; out[1] = '\\'; break;
            default: out[0] = *p; break;
        }
        sb_reserve(sb, 2);
        if (!sb->buf) return;
        memcpy(sb->buf + sb->len, out, strlen(out));
        sb->len += strlen(out);
        sb->buf[sb->len] = '\0';
    }
}

static void sb_unescape(char *s)
{
    char *w = s;
    for (char *p = s; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 't') *w++ = '\t';
            else if (*p == 'n') *w++ = '\n';
            else *w++ = *p;
        } else {
            *w++ = *p;
        }
    }
    *w = '\0';
}

static void serialize_doc(strbuf *sb, const fd_doc *d, const char *title)
{
    sb_printf(sb, "FD1\n");
    sb_printf(sb, "meta\t");
    sb_field(sb, title ? title : "");
    sb_printf(sb, "\n");
    sb_printf(sb, "settings\t%d\t%d\t%d\t%d\t%d\t%d\n",
              d->settings.show_grid ? 1 : 0,
              d->settings.show_axes ? 1 : 0,
              d->settings.show_numbers ? 1 : 0,
              d->settings.angle_degrees ? 1 : 0,
              d->settings.quality,
              d->settings.crown_zoom ? 1 : 0);
    sb_printf(sb, "viewport\t%.17g\t%.17g\t%.17g\t%d\n",
              d->vp.center_x, d->vp.center_y, d->vp.units_per_px,
              d->view_initialized ? 1 : 0);

    for (int i = 0; i < d->curve_count; i++) {
        const fd_curve *c = &d->curves[i];
        sb_printf(sb, "curve\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.17g\t%.17g\t",
                  c->fn.id, (int)c->fn.mode, c->fn.color_index, c->width, c->dash,
                  c->fn.visible ? 1 : 0, c->fn.degrees, c->fn.t_min, c->fn.t_max);
        sb_field(sb, c->name);
        sb_printf(sb, "\t");
        sb_field(sb, c->fn.src1);
        sb_printf(sb, "\t");
        sb_field(sb, c->fn.src2);
        sb_printf(sb, "\n");
    }

    for (int i = 0; i < d->slider_count; i++) {
        const fd_slider *s = &d->sliders[i];
        sb_printf(sb, "slider\t%d\t", s->id);
        sb_field(sb, s->param);
        sb_printf(sb, "\t%.17g\t%.17g\t%.17g\t%.17g\t%d\n",
                  s->value, s->min, s->max, s->step, s->playing ? 1 : 0);
    }

    for (int i = 0; i < d->geo_count; i++) {
        const fd_geo *g = &d->geos[i];
        sb_printf(sb, "geo\t%d\t%d\t%d\t%d\t%d\t", g->id, (int)g->kind,
                  g->color_index, g->visible ? 1 : 0, g->npts);
        sb_field(sb, g->name);
        for (int k = 0; k < g->npts; k++) {
            sb_printf(sb, "\t%.17g\t%.17g", g->p[k][0], g->p[k][1]);
        }
        sb_printf(sb, "\n");
    }

    sb_printf(sb, "next\t%d\n", d->next_id);
    sb_printf(sb, "geonext\t%d\n", d->geo_next_id);
    sb_printf(sb, "selected\t%d\n", d->selected_id);
}

static int split_tabs(char *line, char **fields, int cap)
{
    int n = 0;
    char *p = line;
    while (n < cap) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

static bool parse_doc(char *text, fd_doc *out)
{
    doc_defaults(out);

    char *save = NULL;
    bool saw_header = false;
    int max_id = 0;

    for (char *line = strtok_r(text, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        /* 去掉行尾 \r */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r')) line[--len] = '\0';
        if (!saw_header) {
            if (strcmp(line, "FD1") != 0) return false;
            saw_header = true;
            continue;
        }

        char *f[32];
        int n = split_tabs(line, f, 32);
        if (n == 0) continue;

        if (strcmp(f[0], "settings") == 0 && n >= 7) {
            out->settings.show_grid = atoi(f[1]) != 0;
            out->settings.show_axes = atoi(f[2]) != 0;
            out->settings.show_numbers = atoi(f[3]) != 0;
            out->settings.angle_degrees = atoi(f[4]) != 0;
            out->settings.quality = atoi(f[5]);
            out->settings.crown_zoom = atoi(f[6]) != 0;
        } else if (strcmp(f[0], "viewport") == 0 && n >= 5) {
            out->vp.center_x = atof(f[1]);
            out->vp.center_y = atof(f[2]);
            out->vp.units_per_px = atof(f[3]);
            out->view_initialized = atoi(f[4]) != 0;
        } else if (strcmp(f[0], "curve") == 0 && n >= 12) {
            if (out->curve_count >= FD_MAX_CURVES) continue;
            fd_curve *c = &out->curves[out->curve_count++];
            memset(c, 0, sizeof(*c));
            c->fn.id = atoi(f[1]);
            c->fn.mode = (fd_plot_mode)atoi(f[2]);
            c->fn.color_index = atoi(f[3]);
            c->width = atoi(f[4]);
            c->dash = atoi(f[5]);
            c->fn.visible = atoi(f[6]) != 0;
            c->fn.degrees = atoi(f[7]);
            c->fn.t_min = atof(f[8]);
            c->fn.t_max = atof(f[9]);
            snprintf(c->name, sizeof(c->name), "%s", f[10]);
            sb_unescape(c->name);
            snprintf(c->fn.src1, sizeof(c->fn.src1), "%s", f[11]);
            sb_unescape(c->fn.src1);
            if (n >= 13) {
                snprintf(c->fn.src2, sizeof(c->fn.src2), "%s", f[12]);
                sb_unescape(c->fn.src2);
            }
            if (c->fn.id > max_id) max_id = c->fn.id;
        } else if (strcmp(f[0], "slider") == 0 && n >= 8) {
            if (out->slider_count >= FD_MAX_SLIDERS) continue;
            fd_slider *s = &out->sliders[out->slider_count++];
            memset(s, 0, sizeof(*s));
            s->id = atoi(f[1]);
            snprintf(s->param, sizeof(s->param), "%s", f[2]);
            sb_unescape(s->param);
            s->value = atof(f[3]);
            s->min = atof(f[4]);
            s->max = atof(f[5]);
            s->step = atof(f[6]);
            s->playing = atoi(f[7]) != 0;
            if (s->id > max_id) max_id = s->id;
        } else if (strcmp(f[0], "geo") == 0 && n >= 7) {
            int npts = atoi(f[5]);
            if (out->geo_count >= FD_GEO_MAX) continue;
            if (npts < 1) continue;
            if (npts > FD_GEO_MAX_PTS) npts = FD_GEO_MAX_PTS;
            if (n < 6 + 2 * npts) continue;
            fd_geo *g = &out->geos[out->geo_count++];
            memset(g, 0, sizeof(*g));
            g->id = atoi(f[1]);
            g->kind = (fd_geo_kind)atoi(f[2]);
            g->color_index = atoi(f[3]);
            g->visible = atoi(f[4]) != 0;
            g->npts = npts;
            snprintf(g->name, sizeof(g->name), "%s", f[6]);
            sb_unescape(g->name);
            for (int k = 0; k < npts; k++) {
                g->p[k][0] = atof(f[7 + 2 * k]);
                g->p[k][1] = atof(f[8 + 2 * k]);
            }
            if (g->id >= out->geo_next_id) out->geo_next_id = g->id + 1;
        } else if (strcmp(f[0], "geonext") == 0 && n >= 2) {
            int v = atoi(f[1]);
            if (v > out->geo_next_id) out->geo_next_id = v;
        } else if (strcmp(f[0], "next") == 0 && n >= 2) {
            int v = atoi(f[1]);
            if (v > out->next_id) out->next_id = v;
        } else if (strcmp(f[0], "selected") == 0 && n >= 2) {
            out->selected_id = atoi(f[1]);
        }
    }

    if (!saw_header) return false;
    if (max_id + 1 > out->next_id) out->next_id = max_id + 1;
    if (out->selected_id == 0 && out->curve_count > 0) {
        out->selected_id = out->curves[0].fn.id;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* 文件                                                                */
/* ------------------------------------------------------------------ */

void model_set_files_dir(const char *dir)
{
    snprintf(g_files_dir, sizeof(g_files_dir), "%s", dir ? dir : "");
}

const char *model_files_dir(void) { return g_files_dir; }

static bool write_text_file(const char *path, const char *text, size_t len)
{
    char tmp[640];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(text, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

static char *read_text_file(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = (long)got;
    return buf;
}

static void autosave_path(char *out, size_t cap)
{
    snprintf(out, cap, "%s/autosave.fd", g_files_dir);
}

/**
 * 把解析出来的文档装进当前状态。
 *
 * 文件里不存屏幕尺寸（width_px / height_px），所以不能走 model_set_screen：
 * 它看到 width_px 为 0 会把视口重建成默认的 -10..10，存档里的平移缩放就丢了。
 */
static void adopt_loaded_doc(fd_doc *loaded)
{
    int32_t sw = g_doc.screen_w;
    int32_t sh = g_doc.screen_h;

    g_doc = *loaded;
    g_doc.screen_w = sw;
    g_doc.screen_h = sh;
    g_doc.vp.width_px = (float)sw;
    g_doc.vp.height_px = (float)sh;
    if (sw <= 1 || sh <= 1 || g_doc.vp.units_per_px <= 0.0 ||
        !isfinite(g_doc.vp.units_per_px)) {
        g_doc.vp = fd_vp_for_range((float)sw, (float)sh, -10.0, 10.0);
        g_doc.view_initialized = true;
    }

    g_hist[0] = g_doc;
    g_hist_len = 1;
    g_hist_pos = 0;
}

bool model_save(void)
{
    if (g_files_dir[0] == '\0') return false;
    strbuf sb = {0};
    serialize_doc(&sb, &g_doc, "自动保存");
    char path[640];
    autosave_path(path, sizeof(path));
    bool ok = sb.buf && write_text_file(path, sb.buf, sb.len);
    free(sb.buf);
    return ok;
}

bool model_has_saved(void)
{
    if (g_files_dir[0] == '\0') return false;
    char path[640];
    autosave_path(path, sizeof(path));
    return access(path, F_OK) == 0;
}

bool model_load(void)
{
    if (g_files_dir[0] == '\0') return false;
    char path[640];
    autosave_path(path, sizeof(path));
    char *text = read_text_file(path, NULL);
    if (!text) return false;

    fd_doc loaded;
    bool ok = parse_doc(text, &loaded);
    free(text);
    if (!ok) return false;

    adopt_loaded_doc(&loaded);
    return true;
}

/* ------------------------------------------------------------------ */
/* 存档                                                                */
/* ------------------------------------------------------------------ */

static void archive_dir(char *out, size_t cap)
{
    snprintf(out, cap, "%s/archives", g_files_dir);
}

static bool ensure_archive_dir(void)
{
    if (g_files_dir[0] == '\0') return false;
    char dir[640];
    archive_dir(dir, sizeof(dir));
    if (mkdir(dir, 0700) == 0) return true;
    return errno == EEXIST;
}

int model_archive_list(fd_archive_entry *out, int cap)
{
    if (!out || cap <= 0 || g_files_dir[0] == '\0') return 0;
    char dir[640];
    archive_dir(dir, sizeof(dir));
    DIR *dp = opendir(dir);
    if (!dp) return 0;

    int count = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL && count < cap) {
        size_t len = strlen(de->d_name);
        if (len < 4 || strcmp(de->d_name + len - 3, ".fd") != 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        char title[48] = "";
        char *text = read_text_file(path, NULL);
        if (text) {
            char first[128];
            snprintf(first, sizeof(first), "%s", text);
            char *f[4];
            if (split_tabs(first, f, 4) >= 2 && strcmp(f[0], "meta") == 0) {
                snprintf(title, sizeof(title), "%s", f[1]);
                sb_unescape(title);
            }
            free(text);
        }
        snprintf(out[count].file, sizeof(out[count].file), "%s", de->d_name);
        snprintf(out[count].title, sizeof(out[count].title), "%s",
                 title[0] ? title : de->d_name);
        count++;
    }
    closedir(dp);

    /* 新的排前面：文件名里带时间戳，按名字倒序即可 */
    for (int i = 1; i < count; i++) {
        fd_archive_entry key = out[i];
        int j = i - 1;
        while (j >= 0 && strcmp(out[j].file, key.file) < 0) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

bool model_archive_save(const char *title)
{
    if (!ensure_archive_dir()) return false;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    char dir[640];
    archive_dir(dir, sizeof(dir));
    char file[64];
    snprintf(file, sizeof(file), "g%04d%02d%02d-%02d%02d%02d.fd",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    strbuf sb = {0};
    serialize_doc(&sb, &g_doc, title);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    bool ok = sb.buf && write_text_file(path, sb.buf, sb.len);
    free(sb.buf);
    return ok;
}

bool model_archive_load(const char *file)
{
    if (!file || strchr(file, '/') || strchr(file, '\\')) return false;
    char dir[640];
    archive_dir(dir, sizeof(dir));
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    char *text = read_text_file(path, NULL);
    if (!text) return false;

    fd_doc loaded;
    bool ok = parse_doc(text, &loaded);
    free(text);
    if (!ok) return false;

    adopt_loaded_doc(&loaded);
    return true;
}

bool model_archive_delete(const char *file)
{
    if (!file || strchr(file, '/') || strchr(file, '\\')) return false;
    char dir[640];
    archive_dir(dir, sizeof(dir));
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    return remove(path) == 0;
}
