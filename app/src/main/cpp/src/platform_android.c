/**
 * Android 平台层：SurfaceView → ANativeWindow → LVGL 显示/输入驱动。
 *
 * 渲染线程持有 LVGL 的唯一所有权；所有 JNI 入口都先拿 g_lock，
 * 所以 UI 线程改界面、渲染线程跑 lv_timer_handler 不会互相踩。
 */
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "lvgl.h"

#define FD_INPUT_QUEUE 128

typedef struct {
    uint8_t action;
    uint8_t count;
    float x0, y0, x1, y1;
} fd_input_event;

static ANativeWindow *g_window;
static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_running;
static int g_thread_started;
static lv_display_t *g_disp;
static lv_indev_t *g_indev;
static uint8_t *g_fb;
static int32_t g_w, g_h;

static fd_input_event g_raw_q[FD_INPUT_QUEUE];
static int g_raw_head, g_raw_tail;
static fd_input_event g_lv_q[FD_INPUT_QUEUE];
static int g_lv_head, g_lv_tail;

static uint32_t tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (g_window) {
        ANativeWindow_Buffer buf;
        if (ANativeWindow_lock(g_window, &buf, NULL) == 0) {
            const int32_t w = lv_area_get_width(area);
            const int32_t h = lv_area_get_height(area);
            const uint32_t *src = (const uint32_t *)px_map;
            uint32_t *base = (uint32_t *)buf.bits;
            for (int32_t y = 0; y < h; y++) {
                uint32_t *dst = base + (size_t)(area->y1 + y) * buf.stride + area->x1;
                const uint32_t *s = src + (size_t)y * w;
                for (int32_t x = 0; x < w; x++) {
                    /* LVGL 的 ARGB8888 在内存里是 B,G,R,A；Android 要 R,G,B,A。 */
                    uint32_t c = s[x];
                    dst[x] = (c & 0xFF00FF00u) | ((c & 0x000000FFu) << 16) |
                             ((c >> 16) & 0x000000FFu);
                }
            }
            ANativeWindow_unlockAndPost(g_window);
        }
    }
    lv_display_flush_ready(disp);
}

static lv_indev_state_t action_state(int action)
{
    switch (action) {
        case 0: /* ACTION_DOWN */
        case 2: /* ACTION_MOVE */
        case 5: /* ACTION_POINTER_DOWN */
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
    if (g_lv_head != g_lv_tail) {
        fd_input_event e = g_lv_q[g_lv_head];
        g_lv_head = (g_lv_head + 1) % FD_INPUT_QUEUE;
        last.x = (int32_t)e.x0;
        last.y = (int32_t)e.y0;
        last_state = action_state(e.action);
        data->continue_reading = (g_lv_head != g_lv_tail);
    }
    data->point = last;
    data->state = last_state;
}

static void push_event(int action, int count,
                       float x0, float y0, float x1, float y1)
{
    int next = (g_raw_tail + 1) % FD_INPUT_QUEUE;
    if (next != g_raw_head) {
        g_raw_q[g_raw_tail] = (fd_input_event){
                (uint8_t)action, (uint8_t)count, x0, y0, x1, y1};
        g_raw_tail = next;
    }
    next = (g_lv_tail + 1) % FD_INPUT_QUEUE;
    if (next != g_lv_head) {
        g_lv_q[g_lv_tail] = (fd_input_event){
                (uint8_t)action, (uint8_t)count, x0, y0, x1, y1};
        g_lv_tail = next;
    }
}

static void display_start_locked(void)
{
    if (g_disp || g_w <= 1 || g_h <= 1) return;

    if (!lv_is_initialized()) {
        lv_init();
        lv_tick_set_cb(tick_cb);
    }

    g_fb = (uint8_t *)malloc((size_t)g_w * (size_t)g_h * 4u);
    if (!g_fb) return;

    g_disp = lv_display_create(g_w, g_h);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(g_disp, g_fb, NULL, (uint32_t)((size_t)g_w * g_h * 4u),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_disp, flush_cb);

    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev, indev_read_cb);

    app_set_size(g_w, g_h);
    app_init();
}

static void display_stop_locked(void)
{
    app_deinit();
    if (g_indev) {
        lv_indev_delete(g_indev);
        g_indev = NULL;
    }
    if (g_disp) {
        lv_display_delete(g_disp);
        g_disp = NULL;
    }
    free(g_fb);
    g_fb = NULL;
}

static void *render_thread(void *arg)
{
    LV_UNUSED(arg);
    while (g_running) {
        pthread_mutex_lock(&g_lock);
        while (g_raw_head != g_raw_tail) {
            fd_input_event e = g_raw_q[g_raw_head];
            g_raw_head = (g_raw_head + 1) % FD_INPUT_QUEUE;
            app_touch(e.action, e.count, e.x0, e.y0, e.x1, e.y1);
        }
        lv_timer_handler();
        pthread_mutex_unlock(&g_lock);
        usleep(4000);
    }
    return NULL;
}

static void start_thread_locked(void)
{
    if (g_thread_started) return;
    g_running = 1;
    if (pthread_create(&g_thread, NULL, render_thread, NULL) == 0) {
        g_thread_started = 1;
    } else {
        g_running = 0;
    }
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSetFilesDir(JNIEnv *env, jobject thiz,
                                                             jstring dir)
{
    LV_UNUSED(thiz);
    const char *utf = dir ? (*env)->GetStringUTFChars(env, dir, NULL) : NULL;
    pthread_mutex_lock(&g_lock);
    app_set_files_dir(utf);
    pthread_mutex_unlock(&g_lock);
    if (utf) (*env)->ReleaseStringUTFChars(env, dir, utf);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSurfaceCreated(JNIEnv *env, jobject thiz,
                                                                jobject surface)
{
    LV_UNUSED(thiz);
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    pthread_mutex_lock(&g_lock);
    if (g_window) ANativeWindow_release(g_window);
    g_window = win;
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSurfaceChanged(JNIEnv *env, jobject thiz,
                                                                jint width, jint height)
{
    LV_UNUSED(env);
    LV_UNUSED(thiz);
    pthread_mutex_lock(&g_lock);
    if (g_window) {
        ANativeWindow_setBuffersGeometry(g_window, width, height,
                                         WINDOW_FORMAT_RGBA_8888);
    }
    if (g_w != width || g_h != height) {
        if (g_thread_started) {
            g_running = 0;
            pthread_mutex_unlock(&g_lock);
            pthread_join(g_thread, NULL);
            pthread_mutex_lock(&g_lock);
            g_thread_started = 0;
            display_stop_locked();
        }
        g_w = width;
        g_h = height;
    }
    display_start_locked();
    start_thread_locked();
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSurfaceDestroyed(JNIEnv *env, jobject thiz)
{
    LV_UNUSED(env);
    LV_UNUSED(thiz);
    pthread_mutex_lock(&g_lock);
    g_running = 0;
    pthread_mutex_unlock(&g_lock);
    if (g_thread_started) {
        pthread_join(g_thread, NULL);
        g_thread_started = 0;
    }
    pthread_mutex_lock(&g_lock);
    display_stop_locked();
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = NULL;
    }
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeTouch(JNIEnv *env, jobject thiz,
                                                       jint action, jint count,
                                                       jfloat x0, jfloat y0,
                                                       jfloat x1, jfloat y1)
{
    LV_UNUSED(env);
    LV_UNUSED(thiz);
    pthread_mutex_lock(&g_lock);
    push_event(action, count, x0, y0, x1, y1);
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeCrown(JNIEnv *env, jobject thiz, jfloat delta)
{
    LV_UNUSED(env);
    LV_UNUSED(thiz);
    pthread_mutex_lock(&g_lock);
    if (g_disp) app_crown(delta);
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT jboolean JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeKey(JNIEnv *env, jobject thiz, jint code)
{
    LV_UNUSED(env);
    LV_UNUSED(thiz);
    bool used = false;
    pthread_mutex_lock(&g_lock);
    if (g_disp) used = app_key(code);
    pthread_mutex_unlock(&g_lock);
    return used ? JNI_TRUE : JNI_FALSE;
}
