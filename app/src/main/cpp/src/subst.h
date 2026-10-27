/**
 * 表达式里的几何量替换：x(A) / y(A) 取点坐标，距离/角/面积对象的名字当数值。
 *
 * 这些替换发生在**送进引擎之前**：引擎只认识采样变量、参数、常量、函数，
 * 「点」和「标量」是 UI 层的几何对象，所以把文本换成数值字面量是最小改动。
 */
#ifndef FD_SUBST_H
#define FD_SUBST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 从左到右扫描一遍，把认得的写法替换成 `(数值)`：
 *   - `x(A)` / `y(A)`：点 A 的横/纵坐标（括号内外允许空格，点名必须是已知点）；
 *   - 距离 / 角 / 面积对象的名字（如 `f`）：那个度量的当前值。
 *
 * 不认得的写法（未知点名、非标量名字、括号没闭上）原样保留，交给引擎报错。
 * 只做一遍，不递归。out 至少 cap 字节。
 */
void fd_subst_geo(const char *src, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FD_SUBST_H */
