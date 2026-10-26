/**
 * 几何作图对象：点、线段、直线、射线、角、多边形、距离。
 *
 * 这些对象由「工具」在绘图屏上创建，坐标全部是图纸坐标系（和曲线一致）。
 * 直线/射线只存"基点 + 单位方向"，渲染时再按视口裁剪；线段/多边形存真实端点。
 */
#ifndef FD_GEO_H
#define FD_GEO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_GEO_MAX 48
#define FD_GEO_MAX_PTS 8

typedef enum {
    FD_GEO_POINT = 0,
    FD_GEO_SEGMENT, /**< 线段：p[0] ~ p[1] */
    FD_GEO_LINE,    /**< 直线：p[0] 是基点，p[1] 是单位方向 */
    FD_GEO_RAY,     /**< 射线：p[0] 是起点，p[1] 是单位方向 */
    FD_GEO_ANGLE,   /**< 角：p[0] 顶点，p[1]/p[2] 两条边上的点 */
    FD_GEO_POLYGON, /**< 多边形（面积） */
    FD_GEO_DISTANCE /**< 两点间距离：p[0] ~ p[1] */
} fd_geo_kind;

typedef struct {
    int id;
    fd_geo_kind kind;
    int color_index;
    bool visible;
    int npts;
    double p[FD_GEO_MAX_PTS][2];
    char name[12];
} fd_geo;

/* ---- 纯数学小工具 ---- */
double geo_distance(const double *a, const double *b);
double geo_polygon_area(const double (*pts)[2], int n);
/** 顶点 v 处、两条边分别指向 a / b 的夹角（0~180 度）。 */
double geo_angle_deg(const double *v, const double *a, const double *b);
/** 点 p 关于过 l0、l1 的直线做轴对称。 */
void geo_reflect_point(const double *p, const double *l0, const double *l1,
                       double out[2]);
/** a→b 的单位方向；长度退化时返回 (1,0)。 */
void geo_unit_dir(const double *a, const double *b, double out[2]);
/** 二维垂直方向（逆时针 90 度）。 */
void geo_perp(const double *d, double out[2]);

#ifdef __cplusplus
}
#endif

#endif /* FD_GEO_H */
