// 手表 UI 大量用到实验性接口，文件级统一 OptIn。
@file:OptIn(androidx.compose.ui.ExperimentalComposeUiApi::class)

package com.functiondrawer.watch.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.width
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Surface
import androidx.compose.material.Text
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import com.functiondrawer.watch.engine.PlotMode
import com.functiondrawer.watch.ui.keyboard.KeyAction
import com.functiondrawer.watch.ui.keyboard.KeyPage
import com.functiondrawer.watch.ui.keyboard.LetterCase
import com.functiondrawer.watch.ui.keyboard.editText
import com.functiondrawer.watch.ui.theme.PillShape
import com.functiondrawer.watch.ui.theme.watchShapeOf
import kotlinx.coroutines.flow.Flow

/**
 * 「圆（半径·圆心）」的半径输入面板。
 *
 * 用户原话："圆（半径与圆心）调起的键盘有很多问题，你直接调用函数创建界面的键盘吧，
 * 这样还可以调用其他变量的值，比如设置 d 为半径，如果识别到无法创建就不创建并弹出
 * toast 提示'请输入正确半径'。"
 *
 * 所以这一屏**完整复用函数创建界面的键盘**（[MathKeyboard] + [ExpressionField]，
 * 连纵向分区比例都用同一套），只把顶部中间那颗"模式 chip"换成不可点击的标题「半径」，
 * 并且去掉了模式切换的入口 —— 半径表达式和曲线模式没有关系。
 *
 * 半径是**表达式**而不是一个数字：`2.5`、`d`、`d/2` 都可以，输入时直接引用滑动条。
 * 校验和建圆在 [GraphState.confirmRadiusInput] 里（那里能拿到参数表和可调用函数表）。
 */
@Composable
internal fun RadiusInputPanel(
    metrics: WatchMetrics,
    geometry: ScreenGeometry,
    isRound: Boolean,
    rotary: Flow<Float>,
    onConfirm: (String) -> Unit,
    onCancel: () -> Unit
) {
    var text by remember { mutableStateOf("") }
    var cursor by remember { mutableStateOf(0) }
    // 打开时停在数字页：半径绝大多数是数字。
    var page by remember { mutableStateOf(KeyPage.DIGITS) }
    var showAlphabet by remember { mutableStateOf(false) }
    var letterCase by remember { mutableStateOf(LetterCase.Lower) }

    val keyBase = keyWidthBase(metrics, geometry, isRound)
    val fieldHeight = metrics.sizeDp * RADIUS_FIELD_H
    val rowKeyHeight = metrics.sizeDp * 0.115f
    val fieldGap = keyBase * RADIUS_FIELD_GAP_FRACTION
    val delWidth = keyBase * RADIUS_DEL_W_FRACTION

    fun spanW(yTop: Float, yBottom: Float): Dp =
        bandWidth(metrics, geometry, isRound, yTop, yBottom)

    fun applyKey(action: KeyAction) {
        val (next, nextCursor) = editText(text, cursor, action)
        text = next
        cursor = nextCursor
    }

    // 系统返回键 = 取消（和左上角那颗 × 一样）
    BackHandler { onCancel() }

    Box(
        Modifier
            .fillMaxSize()
            // 这是一层**盖在图像界面上的输入面板**，所以必须是不透明底色：
            // 半透明会让底下的坐标轴/曲线透上来，和键盘叠在一起看不清。
            .background(MaterialTheme.colors.background)
    ) {

        // ---- 顶部：取消 / 半径（不可点击） / 完成 ----
        Row(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .offset(y = geometry.sy(RADIUS_TOP_Y))
                .width(spanW(RADIUS_TOP_Y, RADIUS_TOP_Y + RADIUS_TOP_H))
                .height(metrics.sizeDp * RADIUS_TOP_H),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            WatchIconButton(
                icon = Icons.Filled.Close,
                contentDescription = "取消",
                metrics = metrics,
                modifier = Modifier
                    .width(metrics.sizeDp * 0.16f)
                    .height(metrics.sizeDp * RADIUS_TOP_H * 0.85f),
                onClick = onCancel
            )
            Surface(
                color = KeyPalette.normal,
                shape = PillShape,
                modifier = Modifier
                    .width(keyBase * RADIUS_TITLE_W_FRACTION)
                    .height(metrics.sizeDp * RADIUS_TOP_H * 0.85f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Text(
                        text = "半径",
                        fontSize = metrics.fontSmall,
                        color = KeyPalette.text
                    )
                }
            }
            WatchIconButton(
                icon = Icons.Filled.Check,
                contentDescription = "完成",
                metrics = metrics,
                modifier = Modifier
                    .width(metrics.sizeDp * 0.16f)
                    .height(metrics.sizeDp * RADIUS_TOP_H * 0.85f),
                filled = true,
                onClick = { onConfirm(text) }
            )
        }

        // ---- 表达式输入区 + 退格键 ----
        Row(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .offset(y = geometry.sy(RADIUS_FIELD_Y))
                .width(spanW(RADIUS_FIELD_Y, RADIUS_FIELD_Y + RADIUS_FIELD_H))
                .height(fieldHeight),
            horizontalArrangement = Arrangement.spacedBy(fieldGap)
        ) {
            ExpressionField(
                text = text,
                cursor = cursor,
                hint = "2.5 或 d",
                metrics = metrics,
                focused = true,
                error = null,
                modifier = Modifier.weight(1f).fillMaxHeight(),
                onTap = { },
                onCursorMove = { cursor = it },
                shape = watchShapeOf(fieldHeight)
            )
            BackspaceKey(
                metrics = metrics,
                onKey = ::applyKey,
                modifier = Modifier
                    .width(delWidth)
                    .height(rowKeyHeight)
                    .align(Alignment.CenterVertically),
                shape = PillShape
            )
        }

        // ---- 键盘（和函数创建界面同一套：页签 + 3 行网格）----
        if (showAlphabet) {
            AlphabetKeyboard(
                metrics = metrics,
                letterCase = letterCase,
                onCaseChange = { letterCase = it },
                onInsert = { applyKey(KeyAction.Insert(it)) },
                onClose = { showAlphabet = false },
                rotary = rotary,
                modifier = Modifier.fillMaxSize()
            )
        } else {
            MathKeyboard(
                page = page,
                // 半径和曲线模式无关；用显函数那一套键盘（数字、字母、四则运算都在）
                mode = PlotMode.EXPLICIT,
                metrics = metrics,
                geometry = geometry,
                isRound = isRound,
                tabTopFraction = RADIUS_TAB_Y,
                tabHeightFraction = RADIUS_TAB_H,
                gridTopFraction = RADIUS_GRID_TOP,
                gridBottomFraction = RADIUS_GRID_BOTTOM,
                gapFraction = RADIUS_GAP_FRACTION,
                fieldCenterFraction = RADIUS_FIELD_Y + RADIUS_FIELD_H / 2f,
                rangeLabel = null,
                onSelectPage = { page = it },
                onCycleRange = { },
                onToggleAngleUnit = { },
                angleUnitLabel = "RAD",
                onOpenAlphabet = { showAlphabet = true },
                onKey = ::applyKey,
                modifier = Modifier.fillMaxSize()
            )
        }
    }
}

// -----------------------------------------------------------------------------------
// 纵向分区：**和函数创建界面完全同一套比例**（见 `FunctionEditor.kt` 文件头那段推导）。
//
// 这里刻意复制那五个数字而不是去引用那边的 `private const val`：两边是两屏，
// 将来函数编辑器要调分区时不一定想连半径面板一起动 —— 但**当前**它们必须一致，
// 所以下面这一组数就是那一边的当前值（0.088 / 0.092 / 0.196 / 0.345 / 0.945…）。
// -----------------------------------------------------------------------------------
private const val RADIUS_TOP_Y = 0.088f
private const val RADIUS_TOP_H = 0.092f
private const val RADIUS_FIELD_Y = 0.196f
private const val RADIUS_FIELD_H = 0.345f
private const val RADIUS_TAB_Y = 0.6174f
private const val RADIUS_TAB_H = 0.0739f
private const val RADIUS_GRID_TOP = 0.6993f
private const val RADIUS_GRID_BOTTOM = 0.945f
private const val RADIUS_GAP_FRACTION = 0.008f
private const val RADIUS_DEL_W_FRACTION = 0.155f
private const val RADIUS_FIELD_GAP_FRACTION = 0.02f
private const val RADIUS_TITLE_W_FRACTION = 0.18f
