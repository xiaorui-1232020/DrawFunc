#include "app.h"

#include "lvgl.h"
#include "model.h"
#include "ui.h"

static bool g_ui_built;
static bool g_model_ready;
static int32_t g_w = 466;
static int32_t g_h = 466;

void app_set_size(int32_t w, int32_t h)
{
    g_w = w;
    g_h = h;
    model_set_screen(w, h);
}

void app_set_files_dir(const char *dir)
{
    model_set_files_dir(dir);
}

void app_init(void)
{
    if (!g_model_ready) {
        model_init();
        if (model_has_saved()) model_load();
        model_set_screen(g_w, g_h);
        g_model_ready = true;
    }

    if (g_ui_built) return;
    g_ui_built = true;
    ui_init();
}

void app_deinit(void)
{
    if (!g_ui_built) return;
    g_ui_built = false;
    ui_deinit();
}

void app_touch(int action, int count, float x0, float y0, float x1, float y1)
{
    ui_touch(action, count, x0, y0, x1, y1);
}

void app_crown(float delta)
{
    ui_crown(delta);
}

bool app_key(int key_code)
{
    return ui_key(key_code);
}
