/**
 * LVGL 9.2 配置（FunctionDrawer Android 移植）
 *
 * 只写和默认值不同的项；其余由 LVGL 的 lv_conf_internal.h 补默认值。
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* 32 位色：Surface 用 RGBA_8888，flush 时做一次 R/B 交换。 */
#define LV_COLOR_DEPTH 32

/* 8MB：隐函数/多曲线的绘制缓冲 + 字体缓存，手表上够用。 */
#define LV_MEM_SIZE (8 * 1024 * 1024U)

/* 466×466 的手表屏，按 ~320dpi 取 130 比较接近 dp。 */
#define LV_DPI_DEF 130

#define LV_DEF_REFR_PERIOD 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0

/* 用到的控件（全部显式打开，避免依赖不同版本的默认值）。 */
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_ARC 1
#define LV_USE_LINE 1
#define LV_USE_CANVAS 1
#define LV_USE_TEXTAREA 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* PNG 导出的编码器 */
#define LV_USE_LODEPNG 1

/* lodepng 通过 LVGL 的文件系统接口写文件；用 stdio 驱动，盘符 A。
 * 代码里传 "A:/绝对路径" 给 lodepng。 */
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_STDIO_PATH ""
#define LV_FS_STDIO_CACHE_SIZE 0

/* 内置字体：仅作为兜底；中文由 ui/font_cn_*.c 注册的字体接管。 */
/* lv_font_conv 生成的字体是 RLE 压缩的（bitmap_format = 1），必须打开解码。 */
#define LV_USE_FONT_COMPRESSED 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1

#endif /* LV_CONF_H */
