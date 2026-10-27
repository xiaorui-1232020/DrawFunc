package com.functiondrawer.watch.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Icon
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Surface
import androidx.compose.material.Text
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.vectorResource
import androidx.compose.foundation.Image
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import com.functiondrawer.watch.R
import com.functiondrawer.watch.ui.theme.FunctionDrawerTheme
import com.functiondrawer.watch.ui.theme.PillShape
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlin.math.min

/**
 * 应用的根：先欢迎流程，之后才是主界面。
 *
 * `onboarded` 是应用级偏好（[AppPrefs]），所以「更多设置 → 开发者模式 → 回到初始界面」
 * 把它改回 false 的下一刻，这一层就会自己切回欢迎页，不需要额外导航。
 *
 * 「进入新手教程」那只一次性标记在这里取走：**只在真正进 [GraphScreen] 的那一次组合里取**，
 * 所以欢迎页本身不会把标记提前吃掉。
 */
@Composable
fun AppRoot(rotary: Flow<Float>) {
    val context = LocalContext.current

    // 预编译只做一次：`finished` 是**进程级**标记，Activity 重建（比如从欢迎页回来、
    // 或者「回到初始界面」之后）不该再看一遍进度条。
    var ready by remember { mutableStateOf(Precompile.finished) }
    var target by remember { mutableStateOf(if (Precompile.finished) 1f else 0f) }
    var label by remember { mutableStateOf(if (Precompile.finished) "" else "准备配色与主题") }
    // 底色第一帧就要对：白天模式下不能先闪一下黑屏
    val dayMode = remember { Store.loadDayMode(context) }

    LaunchedEffect(Unit) {
        if (Precompile.finished) return@LaunchedEffect
        val started = System.currentTimeMillis()
        Precompile.run(context) { p, l ->
            target = p
            label = l
        }
        // 机器再快也至少展示 [Precompile.MIN_SHOW_MS]，否则看起来像"闪一下"
        val rest = Precompile.MIN_SHOW_MS - (System.currentTimeMillis() - started)
        if (rest > 0) delay(rest)
        // 等 100% 那一下的补间动画走完（见 PrecompileScreen 的 tween 时长）
        //
        // 2026-10-27：240 → 340。tween 是 280ms，240 会在条子还没涨到 100%
        // 时就把整屏换掉 —— 用户看到的"进度条显示不对"有一半是这一步造成的。
        delay(340)
        ready = true
    }

    if (!ready) {
        PrecompileScreen(progress = target, label = label, dayMode = dayMode)
        return
    }

    if (!AppPrefs.onboarded) {
        WelcomeFlow()
    } else {
        val autoTutorial = remember { AppPrefs.consumePendingTutorial() }
        GraphScreen(rotary = rotary, autoTutorial = autoTutorial)
    }
}

/**
 * 三屏欢迎流程：欢迎 → 选择屏幕形状 → 要不要现在学一遍。
 *
 * 三屏都住在**同一列**里（标题、说明、按钮各占自己的位置，用间距排开），
 * 不是叠在一张图上各自定位 —— 文字和按钮不会互相遮挡，圆屏上也不会跑出屏幕。
 */
@Composable
private fun WelcomeFlow() {
    val context = LocalContext.current

    // ---- 2026-10-27：初始界面跟随用户已保存的主题 ----
    //
    // 用户原话："初始界面给不同的主题写不同风格的界面。"
    //
    // 所以欢迎页不再固定用夜间 + 实心配色：
    //   * 白天 / 夜间 —— 由 [FunctionDrawerTheme] 的 `dayMode` 决定整套色板；
    //   * 毛玻璃 —— 额外铺一层柔和的斜向渐变底，按钮走 `glassSurface`
    //     （半透明底 + 亮边 + 投影），和主界面同一套玻璃语言；
    //   * 经典 —— 平涂底色 + 实心药丸按钮。
    val saved = remember { Store.load(context) }
    val settings = saved?.settings
    val glass = settings?.themeStyle == ThemeStyle.LIQUID_GLASS
    // 全新安装（还没有存档）时也要用对底色，所以单独读一次白天 / 夜间这一位。
    val dayMode = settings?.dayMode ?: Store.loadDayMode(context)

    FunctionDrawerTheme(dayMode = dayMode, glass = glass) {
        // ⚠️ `FunctionDrawerTheme` 只切全局色板，**不** provide `LocalGlassTheme`
        // （那一层原本只在 `TabDeck` / `GraphScreen` 里给）。欢迎页在主题外壳外面，
        // 所以这里自己 provide 一次，否则下面所有 `LocalGlassTheme.current` 都是 false，
        // 「毛玻璃欢迎页」永远走不到。
        CompositionLocalProvider(LocalGlassTheme provides glass) {
            WelcomeFlowBody()
        }
    }
}

/**
 * 欢迎流程的三屏（已套好主题）。
 *
 * 白天 / 夜间不单独传参：`FunctionDrawerTheme` 已经把色板切成对应的那一套，
 * [rememberWelcomeStyle] 直接从 `GlassTokens.isLight` 读回来（见那边）。
 */
@Composable
private fun WelcomeFlowBody() {
    val context = LocalContext.current
    // 欢迎页的四套外观（Material / 毛玻璃 × 白天 / 夜间），见 [rememberWelcomeStyle]。
    val style = rememberWelcomeStyle()
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .background(MaterialTheme.colors.background)
    ) {
            // 底纹由外观决定：Material 平涂、毛玻璃铺一层强调色的柔光（见 [WelcomeStyle.wash]）。
            style.wash?.let { brush ->
                Box(Modifier.fillMaxSize().background(brush))
            }
            val density = LocalDensity.current
            val sizePx = min(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat())
            val sizeDp = with(density) { sizePx.toDp() }
            val metrics = remember(sizePx, sizeDp.value, density) {
                WatchMetrics(sizeDp = sizeDp, sizePx = sizePx, density = density)
            }
            val geometry = rememberScreenGeometry(maxWidth, maxHeight, metrics)
            val isRound = isRoundScreen()

            // 内容宽度：圆屏按弧宽收；非圆屏铺宽，但**始终不超过短边的 0.78** ——
            // 这样即使有人在圆表上先选了「方屏」，这一屏也仍然完整地落在圆里。
            val contentWidth = geometry.rowWidth(metrics, 0.78f, isRound)
                .coerceAtMost(metrics.sizeDp * 0.78f)
            val gap = metrics.sizeDp * 0.018f

            var page by remember { mutableStateOf(0) }
            var shape by remember { mutableStateOf(AppPrefs.screenShape) }

            fun finish(learnTutorial: Boolean) {
                if (learnTutorial) AppPrefs.requestTutorial()
                AppPrefs.setOnboarded(context, true)
            }

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    // 上下留白让内容在圆屏上离表壳远一点；内容再长也能滚到。
                    .padding(vertical = metrics.sizeDp * 0.06f),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                AnimatedContent(
                    targetState = page,
                    transitionSpec = {
                        (fadeIn(tween(durationMillis = 180)) +
                            slideInHorizontally(tween(durationMillis = 220)) { it / 8 })
                            .togetherWith(fadeOut(tween(durationMillis = 140)))
                    },
                    label = "welcome"
                ) { current ->
                    Column(
                        modifier = Modifier.width(contentWidth),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(gap)
                    ) {
                        when (current) {
                            0 -> {
                                AppMark(size = metrics.sizeDp * 0.26f)
                                Text(
                                    text = "DrawFunc",
                                    fontSize = metrics.fontTitle,
                                    color = KeyPalette.text,
                                    textAlign = TextAlign.Center
                                )
                                Text(
                                    text = "画函数的手表工具。拖动画布，转动表冠。",
                                    fontSize = metrics.fontTiny,
                                    color = KeyPalette.textMuted,
                                    textAlign = TextAlign.Center
                                )
                                Spacer(Modifier.height(gap))
                                WelcomeButton(
                                    text = "开始",
                                    accent = true,
                                    metrics = metrics,
                                    style = style,
                                    onClick = { page = 1 }
                                )
                            }

                            1 -> {
                                Text(
                                    text = "选择屏幕形状",
                                    fontSize = metrics.fontBody,
                                    color = KeyPalette.text,
                                    textAlign = TextAlign.Center
                                )
                                Text(
                                    text = "布局会按这个形状收边。之后想改，" +
                                        "在「更多设置 → 手表适配」里换。",
                                    fontSize = metrics.fontTiny,
                                    color = KeyPalette.textMuted,
                                    textAlign = TextAlign.Center
                                )
                                WelcomeChoice(
                                    text = "方屏",
                                    selected = shape == ScreenShape.SQUARE,
                                    metrics = metrics,
                                    style = style,
                                    onClick = {
                                        shape = ScreenShape.SQUARE
                                        AppPrefs.setScreenShape(context, ScreenShape.SQUARE)
                                    }
                                )
                                WelcomeChoice(
                                    text = "圆屏",
                                    selected = shape == ScreenShape.ROUND,
                                    metrics = metrics,
                                    style = style,
                                    onClick = {
                                        shape = ScreenShape.ROUND
                                        AppPrefs.setScreenShape(context, ScreenShape.ROUND)
                                    }
                                )
                                Spacer(Modifier.height(gap))
                                WelcomeButton(
                                    text = "下一步",
                                    accent = true,
                                    metrics = metrics,
                                    style = style,
                                    onClick = { page = 2 }
                                )
                            }

                            else -> {
                                Text(
                                    text = "要现在学一遍吗",
                                    fontSize = metrics.fontBody,
                                    color = KeyPalette.text,
                                    textAlign = TextAlign.Center
                                )
                                Text(
                                    text = "会带着你新建一条函数、看它画出来，再试一下缩放。",
                                    fontSize = metrics.fontTiny,
                                    color = KeyPalette.textMuted,
                                    textAlign = TextAlign.Center
                                )
                                Spacer(Modifier.height(gap))
                                WelcomeButton(
                                    text = "进入新手教程",
                                    accent = true,
                                    metrics = metrics,
                                    style = style,
                                    onClick = { finish(true) }
                                )
                                WelcomeButton(
                                    text = "先自己摸索",
                                    accent = false,
                                    metrics = metrics,
                                    style = style,
                                    onClick = { finish(false) }
                                )
                            }
                        }
                    }
                }
            }
    }
}

// ---------------------------------------------------------------------------
// 欢迎页的四套外观（2026-10-27 新增）
//
// 用户原话："请为 material 主题和毛玻璃主题的白天模式和夜间模式各写一套
// 欢迎界面和新手教程的样式。"
//
// 这里管欢迎页（标题/副标题用色、底纹、按钮材质），教程那侧的对应实现见
// `Tutorial.kt` 的 [rememberTutorialStyle]。两处都按 (毛玻璃?, 白天?) 四格取一套，
// **不再**在各控件里写 `if (glass && day)` 这种散装判断。
// ---------------------------------------------------------------------------

/**
 * 欢迎页的一套外观。
 *
 * @param glass 是否毛玻璃主题（按钮/选项叠 [glassSurface]）。
 * @param day 白天模式。
 * @param wash 铺在底色上的渐变（null = 平涂）。
 * @param primary 主按钮（「开始」「下一步」）底色。
 * @param secondary 次按钮 / 未选中的选项底色。
 * @param selected 选中的选项底色。
 */
internal class WelcomeStyle(
    val glass: Boolean,
    val day: Boolean,
    val wash: Brush?,
    val primary: Color,
    val secondary: Color,
    val selected: Color
)

/**
 * 当前该用哪一套（四格都是**点名写定**的，不再由各控件自己拼）。
 *
 * | | 白天 | 夜间 |
 * |---|---|---|
 * | **Material** | 平涂浅底；主按钮实心强调色、次按钮灰药丸 | 平涂深底；同一套实心按钮 |
 * | **毛玻璃** | 白 60% 底 + 顶部一点白光往强调色走；按钮半透明白玻璃 | 深底 + 底部强调色柔光；按钮深玻璃 |
 */
@Composable
internal fun rememberWelcomeStyle(): WelcomeStyle {
    val glass = LocalGlassTheme.current
    val day = GlassTokens.isLight
    return remember(glass, day) {
        when {
            // 毛玻璃 · 白天：浅底上"更白"才像玻璃，光从顶部扫下来
            glass && day -> WelcomeStyle(
                glass = true, day = true,
                wash = Brush.verticalGradient(
                    0f to Color.White.copy(alpha = 0.18f),
                    0.55f to KeyPalette.accent.copy(alpha = 0.04f),
                    1f to KeyPalette.accent.copy(alpha = 0.10f)
                ),
                primary = GlassTokens.accent,
                secondary = GlassTokens.surface,
                selected = GlassTokens.accent
            )
            // 毛玻璃 · 夜间：底部强调色回光（和表盘上的玻璃磁贴同一个味道）
            glass -> WelcomeStyle(
                glass = true, day = false,
                wash = Brush.verticalGradient(
                    0f to Color.Transparent,
                    0.55f to KeyPalette.accent.copy(alpha = 0.05f),
                    1f to KeyPalette.accent.copy(alpha = 0.20f)
                ),
                primary = GlassTokens.accent,
                secondary = GlassTokens.surface,
                selected = GlassTokens.accent
            )
            // Material · 白天：平涂，实心按钮
            day -> WelcomeStyle(
                glass = false, day = true,
                wash = null,
                primary = KeyPalette.accent,
                secondary = KeyPalette.normal,
                selected = KeyPalette.accent
            )
            // Material · 夜间：平涂，实心按钮
            else -> WelcomeStyle(
                glass = false, day = false,
                wash = null,
                primary = KeyPalette.accent,
                secondary = KeyPalette.normal,
                selected = KeyPalette.accent
            )
        }
    }
}

/** 欢迎页的一颗药丸按钮（主 / 次两种，只差底色）。 */
@Composable
private fun WelcomeButton(
    text: String,
    accent: Boolean,
    metrics: WatchMetrics,
    style: WelcomeStyle,
    onClick: () -> Unit
) {
    Surface(
        // 底色由外观给：毛玻璃是半透明玻璃，Material 是实心药丸。
        color = if (accent) style.primary else style.secondary,
        contentColor = KeyPalette.text,
        shape = PillShape,
        modifier = Modifier
            .fillMaxWidth()
            // 2026-10-27：用户要求"退出新手教程的界面的按钮高度要变成原来的两倍"
            // （那一屏就是这里的"要现在学一遍吗"）。0.14 → 0.28。
            .height(metrics.sizeDp * 0.28f)
            .then(if (style.glass) Modifier.glassSurface(PillShape) else Modifier)
            .clickable(onClick = onClick)
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(
                text = text,
                // 按钮翻倍高，字号也跟着提一档，否则显得空。
                fontSize = metrics.fontBody,
                color = KeyPalette.text,
                textAlign = TextAlign.Center,
                maxLines = 1
            )
        }
    }
}

/** 形状选项：选中时整颗变强调色，并在右端亮一颗对勾。 */
@Composable
private fun WelcomeChoice(
    text: String,
    selected: Boolean,
    metrics: WatchMetrics,
    style: WelcomeStyle,
    onClick: () -> Unit
) {
    Surface(
        // 玻璃主题下：选中 = 强调色底板，未选中 = 玻璃底板。
        color = if (selected) style.selected else style.secondary,
        contentColor = KeyPalette.text,
        shape = PillShape,
        modifier = Modifier
            .fillMaxWidth()
            .height(metrics.sizeDp * 0.17f)
            .then(if (style.glass) Modifier.glassSurface(PillShape) else Modifier)
            .clickable(onClick = onClick)
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = metrics.sizeDp * 0.05f),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = text,
                fontSize = metrics.fontSmall,
                color = KeyPalette.text,
                maxLines = 1,
                modifier = Modifier.weight(1f)
            )
            if (selected) {
                Icon(
                    imageVector = Icons.Filled.Check,
                    contentDescription = null,
                    tint = KeyPalette.text,
                    modifier = Modifier.size(metrics.sizeDp * 0.12f)
                )
            }
        }
    }
}

/**
 * 应用图标（**就是启动器用的那个图标**）。
 *
 * ## 为什么不再自己画（2026-10-27）
 *
 * 用户原话："预编译界面图标还是没有用应用图标，还是你自己画的，请你换成应用图标，
 * 欢迎界面也一样。"
 *
 * 旧版本用 Canvas 现画一个"圆角方块 + 坐标轴 + 正弦"，是**仿**着图标画的仿制品。
 * 现在直接读启动器图标的两个资源：
 *   * 背景 —— `@color/ic_launcher_background`（`#0B1418`，和自适应图标同一色值）；
 *   * 前景 —— `@drawable/ic_launcher_foreground`（自适应图标的前景矢量）。
 *
 * ⚠️ **不能**用 `painterResource(R.mipmap.ic_launcher)`：`mipmap-anydpi-v26` 那份是
 * `<adaptive-icon>`，`painterResource` 只认 VectorDrawable / 位图，运行时会抛
 * `Only VectorDrawables and rasterized asset types are supported`。
 * 所以这里用 `ImageVector.vectorResource` 读**前景矢量**，再自己铺背景 ——
 * 得到的画面和系统裁出来的图标一致（自适应图标就是这么合成的）。
 */
@Composable
internal fun AppMark(size: Dp) {
    val foreground = ImageVector.vectorResource(id = R.drawable.ic_launcher_foreground)
    Box(
        modifier = Modifier
            .size(size)
            // 启动器的图标蒙版是圆角方形（圆表上系统再裁圆），这里用同一比例。
            .clip(RoundedCornerShape(size * 0.28f))
            .background(LauncherIconBackground),
        contentAlignment = Alignment.Center
    ) {
        Image(
            imageVector = foreground,
            contentDescription = null,
            modifier = Modifier.fillMaxSize()
        )
    }
}

/** `@color/ic_launcher_background` 的值 —— Compose 里读 `Color` 资源要绕一圈，这里直接写常量。 */
private val LauncherIconBackground = Color(0xFF0B1418)
