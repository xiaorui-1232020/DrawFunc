#ifndef FD_FEATURES_H
#define FD_FEATURES_H

#include <stdbool.h>

#include "expr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 每类特征最多返回多少个（UI 卡片放不下更多）。 */
#define FD_FEATURE_MAX 5

typedef struct {
    double x;
    double y;
    bool is_max;
} fd_extremum;

typedef struct {
    double roots[FD_FEATURE_MAX];
    int root_count;
    fd_extremum extrema[FD_FEATURE_MAX];
    int extrema_count;
    double y_intercept;
    bool has_y_intercept;
    bool has_discontinuity;
} fd_features;

/**
 * 求 f 在 [x_min, x_max] 上的零点、极值点、y 截距。
 *
 * 与 Kotlin 版 FeatureFinder 行为一致：
 *  - 全程不抛异常，任何非法输入返回空结果；
 *  - 求值次数有硬上界（FD_FEATURES_MAX_EVALS），预算耗尽后安静降级。
 *
 * @param f      已编译的表达式；变量数必须 <= 1
 * @param limit  每类最多保留多少个（<=0 表示不保留）
 */
void fd_features_analyze(const fd_expr *f, double x_min, double x_max,
                         int limit, fd_features *out);

#ifdef __cplusplus
}
#endif

#endif /* FD_FEATURES_H */
