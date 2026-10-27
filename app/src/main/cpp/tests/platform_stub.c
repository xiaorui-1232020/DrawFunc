/** 主机端桩：没有 Android 相册，导出走应用私有目录。 */
#include "platform.h"

bool platform_save_png(const char *name, const unsigned char *data, size_t len)
{
    (void)name;
    (void)data;
    (void)len;
    return false;
}
