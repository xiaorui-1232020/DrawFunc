/**
 * Android 平台层：SurfaceView 提供的 ANativeWindow + LVGL 显示/输入驱动。
 *
 * 渲染线程模型：
 *   - 所有 LVGL 调用都在渲染线程里做，UI 线程只把事件塞进队列；
 *   - 队列分两份：raw（手势检测用）与 lvgl（indev 用），互不干扰；
 *   - flush 时把 LVGL 的 ARGB8888 帧缓冲交换 R/B 后拷进 Surface（ANativeWindow
 *     的 RGBA_8888 内存序与 LVGL 32 位色的字节序相反）。
 */
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "app.h"

#define TAG "FunctionDrawer"
#define TOUCH_QUEUE_CAP 64

typedef struct {
    int action;
    int count;
    float x0, y0, x1, y1;
} touch_event_t;

static ANativeWindow *g_window = NULL;
static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_running = false;
static volatile bool g_initialized = false;
static lv_display_t *g_display = NULL;
static lv_indev_t *g_indev = NULL;
static uint8_t *g_framebuffer = NULL;
static int32_t g_width = 0;
static int32_t g_height = 0;

static touch_event_t g_raw_queue[TOUCH_QUEUE_CAP];
static int g_raw_head = 0, g_raw_tail = 0;
static touch_event_t g_lv_queue[TOUCH_QUEUE_CAP];
static int g_lv_head = 0, g_lv_tail = 0;
static pthread_mutex_t g_queue_lock = PTHREAD_MUTEX_INITIALIZER;

static void queue_push(touch_event_t *queue, int *head, int *tail, const touch_event_t *ev) {
    int next = (*head + 1) % TOUCH_QUEUE_CAP;
    if (next == *tail) {
        /* 队列满：丢最旧的一条，保证最新手势不丢。 */
        *tail = (*tail + 1) % TOUCH_QUEUE_CAP;
    }
    queue[*head] = *ev;
    *head = next;
}

static bool queue_pop(touch_event_t *queue, int *head, int *tail, touch_event_t *out) {
    if (*tail == *head) return false;
    *out = queue[*tail];
    *tail = (*tail + 1) % TOUCH_QUEUE_CAP;
    return true;
}

static uint32_t tick_cb(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    if (g_window) {
        ANativeWindow_Buffer abuf;
        if (ANativeWindow_lock(g_window, &abuf, NULL) == 0) {
            const uint32_t *src = (const uint32_t *)px_map;
            uint32_t *dst = (uint32_t *)abuf.bits;
            const int32_t w = g_width;
            const int32_t h = g_height;
            for (int32_t y = 0; y < h; y++) {
                uint32_t *drow = dst + (size_t)y * abuf.stride;
                const uint32_t *srow = src + (size_t)y * w;
                for (int32_t x = 0; x < w; x++) {
                    uint32_t v = srow[x];
                    /* ARGB8888（字节序 B,G,R,A）→ RGBA_8888（字节序 R,G,B,A）。 */
                    drow[x] = (v & 0xFF00FF00u) |
                              ((v & 0x000000FFu) << 16) |
                              ((v >> 16) & 0x000000FFu);
                }
            }
            ANativeWindow_unlockAndPost(g_window);
        }
    }
    lv_display_flush_ready(disp);
}

static lv_indev_state_t g_last_state = LV_INDEV_STATE_RELEASED;
static lv_point_t g_last_point = {0, 0};

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    touch_event_t ev;
    bool has = false;
    pthread_mutex_lock(&g_queue_lock);
    has = queue_pop(g_lv_queue, &g_lv_head, &g_lv_tail, &ev);
    bool more = (g_lv_head != g_lv_tail);
    pthread_mutex_unlock(&g_queue_lock);

    if (has) {
        g_last_point.x = (int32_t)ev.x0;
        g_last_point.y = (int32_t)ev.y0;
        switch (ev.action) {
            case 0: /* ACTION_DOWN */
            case 2: /* ACTION_MOVE */
                g_last_state = LV_INDEV_STATE_PRESSED;
                break;
            case 1: /* ACTION_UP */
            case 3: /* ACTION_CANCEL */
            case 6: /* ACTION_POINTER_UP */
                g_last_state = LV_INDEV_STATE_RELEASED;
                break;
            case 5: /* ACTION_POINTER_DOWN */
                g_last_state = LV_INDEV_STATE_PRESSED;
                break;
            default:
                break;
        }
    }
    data->point = g_last_point;
    data->state = g_last_state;
    data->continue_reading = more;
}

static void *render_thread(void *arg) {
    (void)arg;
    while (g_running) {
        pthread_mutex_lock(&g_lock);
        touch_event_t ev;
        while (true) {
            bool has;
            pthread_mutex_lock(&g_queue_lock);
            has = queue_pop(g_raw_queue, &g_raw_head, &g_raw_tail, &ev);
            pthread_mutex_unlock(&g_queue_lock);
            if (!has) break;
            app_touch(ev.action, ev.count, ev.x0, ev.y0, ev.x1, ev.y1);
        }
        lv_timer_handler();
        pthread_mutex_unlock(&g_lock);
        usleep(2000);
    }
    return NULL;
}

static void stop_render_thread(void) {
    if (g_running) {
        g_running = false;
        pthread_join(g_thread, NULL);
    }
}

static void release_display(void) {
    if (g_display) {
        lv_display_delete(g_display);
        g_display = NULL;
        g_indev = NULL;
    }
    free(g_framebuffer);
    g_framebuffer = NULL;
}

static bool ensure_display(int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return false;
    if (!g_initialized) {
        lv_init();
        lv_tick_set_cb(tick_cb);
        g_initialized = true;
    }
    if (g_display && w == g_width && h == g_height) return true;

    release_display();
    g_width = w;
    g_height = h;

    g_display = lv_display_create(w, h);
    if (!g_display) return false;
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_ARGB8888);

    size_t bytes = (size_t)w * (size_t)h * 4u;
    g_framebuffer = (uint8_t *)malloc(bytes);
    if (!g_framebuffer) return false;
    memset(g_framebuffer, 0, bytes);
    lv_display_set_buffers(g_display, g_framebuffer, NULL, (uint32_t)bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_display, flush_cb);

    if (!g_indev) {
        g_indev = lv_indev_create();
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(g_indev, indev_read_cb);
        lv_indev_set_display(g_indev, g_display);
    }

    app_set_size(w, h);
    return true;
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSurfaceCreated(JNIEnv *env, jobject thiz,
                                                                jobject surface) {
    (void)thiz;
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    pthread_mutex_lock(&g_lock);
    if (g_window) ANativeWindow_release(g_window);
    g_window = win;
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSurfaceChanged(JNIEnv *env, jobject thiz,
                                                                jint width, jint height) {
    (void)env;
    (void)thiz;
    pthread_mutex_lock(&g_lock);
    stop_render_thread();
    if (g_window) {
        ANativeWindow_setBuffersGeometry(g_window, width, height, WINDOW_FORMAT_RGBA_8888);
    }
    if (ensure_display(width, height)) {
        app_init();
        if (!g_running) {
            g_running = true;
            pthread_create(&g_thread, NULL, render_thread, NULL);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeSurfaceDestroyed(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    pthread_mutex_lock(&g_lock);
    stop_render_thread();
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = NULL;
    }
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeTouch(JNIEnv *env, jobject thiz, jint action,
                                                       jint count, jfloat x0, jfloat y0,
                                                       jfloat x1, jfloat y1) {
    (void)env;
    (void)thiz;
    touch_event_t ev = {action, count, x0, y0, x1, y1};
    pthread_mutex_lock(&g_queue_lock);
    queue_push(g_raw_queue, &g_raw_head, &g_raw_tail, &ev);
    /* 双指及以上的事件不进 LVGL：那是画布手势，不能让按钮误触。 */
    if (count < 2) {
        queue_push(g_lv_queue, &g_lv_head, &g_lv_tail, &ev);
    }
    pthread_mutex_unlock(&g_queue_lock);
}

JNIEXPORT void JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeCrown(JNIEnv *env, jobject thiz, jfloat delta) {
    (void)env;
    (void)thiz;
    pthread_mutex_lock(&g_lock);
    app_crown(delta);
    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT jboolean JNICALL
Java_com_functiondrawer_watch_MainActivity_nativeKey(JNIEnv *env, jobject thiz, jint key_code) {
    (void)env;
    (void)thiz;
    pthread_mutex_lock(&g_lock);
    bool handled = app_key(key_code);
    pthread_mutex_unlock(&g_lock);
    return handled ? JNI_TRUE : JNI_FALSE;
}
