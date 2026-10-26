package com.functiondrawer.watch

import android.app.Activity
import android.graphics.PixelFormat
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * 唯一的 Activity：只负责把 SurfaceView 的生命周期、触摸、表冠和按键
 * 转交给原生层。界面本身完全由 C + LVGL 绘制。
 */
class MainActivity : Activity() {

    private lateinit var surfaceView: GraphSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 沉浸式全屏（API 24 也生效，走 AndroidX 兼容封装）。
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        surfaceView = GraphSurfaceView()
        setContentView(surfaceView)

        nativeSetFilesDir(filesDir.absolutePath)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val amount = scrollAmount(event)
        if (amount != 0f) {
            Log.d(TAG, "表冠: action=${event.action} -> $amount")
            surfaceView.crown(amount)
            return true
        }
        return super.dispatchGenericMotionEvent(event)
    }

    private fun scrollAmount(event: MotionEvent): Float {
        for (axis in SCROLL_AXES) {
            val v = event.getAxisValue(axis)
            if (v != 0f && v.isFinite()) return v
        }
        return 0f
    }

    /** 原生层入口，实现在 cpp/src/platform_android.c。 */
    private external fun nativeSetFilesDir(dir: String)
    private external fun nativeSurfaceCreated(surface: Surface)
    private external fun nativeSurfaceChanged(width: Int, height: Int)
    private external fun nativeSurfaceDestroyed()
    private external fun nativeTouch(
        action: Int, count: Int,
        x0: Float, y0: Float, x1: Float, y1: Float
    )
    private external fun nativeCrown(delta: Float)
    private external fun nativeKey(code: Int): Boolean

    private inner class GraphSurfaceView : SurfaceView(this@MainActivity), SurfaceHolder.Callback {

        init {
            holder.addCallback(this)
            holder.setFormat(PixelFormat.RGBA_8888)
            isFocusable = true
            isFocusableInTouchMode = true
        }

        override fun surfaceCreated(holder: SurfaceHolder) {
            nativeSurfaceCreated(holder.surface)
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            nativeSurfaceChanged(width, height)
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            nativeSurfaceDestroyed()
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            val count = event.pointerCount
            val x0 = event.getX(0)
            val y0 = event.getY(0)
            val x1 = if (count > 1) event.getX(1) else 0f
            val y1 = if (count > 1) event.getY(1) else 0f
            nativeTouch(event.actionMasked, count, x0, y0, x1, y1)
            return true
        }

        override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
            if (nativeKey(keyCode)) return true
            return super.onKeyDown(keyCode, event)
        }

        fun crown(delta: Float) = nativeCrown(delta)
    }

    private companion object {
        const val TAG = "FunctionDrawer"

        /** 表冠轴逐个试，和旧版保持一致（这块表的 source 位不可靠）。 */
        val SCROLL_AXES: IntArray = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            intArrayOf(
                MotionEvent.AXIS_SCROLL,
                MotionEvent.AXIS_VSCROLL,
                MotionEvent.AXIS_HSCROLL,
                MotionEvent.AXIS_WHEEL
            )
        } else {
            intArrayOf(
                MotionEvent.AXIS_VSCROLL,
                MotionEvent.AXIS_HSCROLL,
                MotionEvent.AXIS_WHEEL
            )
        }

        init {
            System.loadLibrary("functiondrawer")
        }
    }
}
