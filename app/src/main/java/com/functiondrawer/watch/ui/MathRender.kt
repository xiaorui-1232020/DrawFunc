// 手表 UI 会用到少量实验性 Compose 接口，文件级统一 OptIn。
@file:OptIn(androidx.compose.ui.ExperimentalComposeUiApi::class)

package com.functiondrawer.watch.ui

import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import com.functiondrawer.watch.engine.Latex
import kotlin.math.max
import kotlin.math.min

/**
 * 表达式输入的**数学排版引擎**（对标 GeoGebra 的输入行观感）。
 *
 * ## 为什么要自己写
 *
 * 用户 2026-10-17 的原话：
 *
 * > "最后再给输入框加个 LaTeX 数学语法的显示，看一大堆符号太难受了，
 * >  所有的语法显示对标 geogebra。"
 *
 * 现在输入框里是等宽字体的原始源码：`sqrt(x^2+1)/2` 就是这 15 个字符，
 * 分式是一条斜杠、根号是四个字母。手表屏幕本来就小，一串这样的字符根本读不出
 * "这是一个分式、分子里有个根号"。
 *
 * 项目不能引入 KaTeX / MathJax（无网络、无 WebView、包体也不允许），
 * 所以这里做一个**够用的子集**：把项目里实际会出现的写法排成二维公式。
 *
 * ## 支持的语法（覆盖本 App 的输入法能打出来的全部）
 *
 * | 输入 | 排成 | 说明 |
 * |---|---|---|
 * | `a/b` | 分式（横线 + 上下两行） | 分子取左操作数、分母取右操作数 |
 * | `x^2` / `x^(n+1)` | 上标 | 指数整体缩小并抬高 |
 * | `sqrt(x)` / `√(x)` | 根号（带勾和上横线） | 被开方部分整体在横线下 |
 * | `sin(x)` | `sin` 正体 + 括号 | 函数名一律正体，和变量区分 |
 * | `pi` / `theta` / `alpha` … | π / θ / α | 希腊字母 |
 * | `*` | `·` | 中间点，比星号像数学 |
 * | `abs(x)` / `|x|` | 竖线 | |
 * | 单字母变量 | **斜体** | 数学惯例：变量斜、数字和函数名正 |
 * | 数字 | 正体 | |
 *
 * ## 为什么是"排版 + 光标"一起做
 *
 * 光标不能再用"等宽字估算 x 坐标"那一套了：一个 `^2` 只占半个字宽、
 * 一个分式要跨两行。所以布局阶段就为**每一个源字符**记下一个 [Stop]
 * （x + 上下沿），光标直接读它 —— 这也是"可自选光标"能成立的前提。
 *
 * ## 结构
 *
 * 1. [tokenize]：源码 → 带原始下标的 token（下标是光标定位的锚点）；
 * 2. [Parser]：递归下降建树（分式 / 上标 / 根号 / 括号 / 文本）；
 * 3. 测量：每个节点算出 `宽 / 基线上高 / 基线下深`；
 * 4. [MathLayout]：把树摊平成"要画的字形 + 线 + 光标停靠点"三张表。
 *
 * 摊平的好处：绘制阶段没有任何递归，一屏公式就是几十次 `drawText` + 几条线，
 * 手表上也扛得住；而且**布局一次、绘制多次**（重组时不会重排）。
 */

/**
 * 排版用的字体。
 *
 * 这两个 ttf 直接来自 **KaTeX 发行包**（`KaTeX_Main-Regular.ttf` 53KB +
 * `KaTeX_Math-Italic.ttf` 31KB，放在 `assets/fonts/`，合计 88KB）。
 *
 * 为什么是这两个文件、而不是"模仿 KaTeX 自己画矢量"：
 * KaTeX 好看的根本原因不是"矢量绘制"，而是**它对的那套字形**——
 * Computer Modern 的数学字体。数字 `1` 有衬线、`+` 的横竖粗细不等、
 * `x` 的斜体是真正的数学斜体（不是系统字体的伪斜体）。
 * 换成这两个字体，同样的排版代码立刻就像 KaTeX 了；自己拿 Path 画 26 个字母
 * 是不可能画出 CM 那股味道的 —— 光 `a` 的收笔就够画一下午。
 *
 * 传 null 时回退系统字体（`Typeface.SANS_SERIF`），行为与上一版一致。
 */
internal class MathTypefaces(
    val main: Typeface?,
    val italic: Typeface?
) {
    fun of(style: GlyphStyle): Typeface = when (style) {
        GlyphStyle.UPRIGHT -> main ?: Typeface.SANS_SERIF
        GlyphStyle.ITALIC -> italic ?: ITALIC_FALLBACK
    }

    private companion object {
        val ITALIC_FALLBACK: Typeface =
            Typeface.create(Typeface.SANS_SERIF, Typeface.ITALIC)
    }
}

// ---------------------------------------------------------------------------
// 1. 词法
// ---------------------------------------------------------------------------

/** token 的种类。区分它们只为了排版时的正/斜体和函数名识别。 */
private enum class TokKind {
    /** 数字：正体。 */
    NUMBER,

    /** 标识符：可能是变量（斜体）、函数名（正体）、希腊字母（换成符号）。 */
    IDENT,

    /** 运算符 / 括号 / 逗号 / 等号：正体，前后留一点空。 */
    OP,

    /** 源码结束。 */
    END
}

private class Tok(
    val kind: TokKind,
    val text: String,
    /** 这个 token 在**原始源码**里的起始下标 —— 光标定位的锚点。 */
    val start: Int
)

/**
 * 把源码切成 token。
 *
 * 容错优先：认不出的字符一律当成单字符 IDENT 原样画出来，绝不抛异常 ——
 * 这是输入框，用户随时处在"打到一半"的非法状态，排版层必须能画任何东西。
 */
private fun tokenize(src: String): List<Tok> {
    val out = ArrayList<Tok>(src.length + 1)
    var i = 0
    while (i < src.length) {
        val c = src[i]
        when {
            c.isWhitespace() -> i++

            c.isDigit() || (c == '.' && i + 1 < src.length && src[i + 1].isDigit()) -> {
                val s = i
                while (i < src.length && (src[i].isDigit() || src[i] == '.')) i++
                out += Tok(TokKind.NUMBER, src.substring(s, i), s)
            }

            c.isLetter() || c == '_' -> {
                val s = i
                while (i < src.length && (src[i].isLetterOrDigit() || src[i] == '_')) i++
                out += Tok(TokKind.IDENT, src.substring(s, i), s)
            }

            else -> {
                out += Tok(TokKind.OP, c.toString(), i)
                i++
            }
        }
    }
    out += Tok(TokKind.END, "", src.length)
    return out
}

// ---------------------------------------------------------------------------
// 2. 语法树
// ---------------------------------------------------------------------------

private sealed interface MNode

/** 一个叶子文本片段（可能是多字符的函数名，也可能是单个变量）。 */
private class MLeaf(
    val text: String,
    val style: GlyphStyle,
    /** 这段文本覆盖的源码区间 `[start, start + len)`。 */
    val start: Int,
    /**
     * 它在**源码**里占几个字符。
     *
     * 和 [text] 的长度不是一回事：`pi`（2 字符）画成 `π`（1 字符）、
     * `*`（1 字符）画成 `⋅`（1 字符但码点不同）。光标定位必须按源码长度算，
     * 否则光标会越界或者卡在同一个位置不动。
     */
    val sourceLen: Int = text.length
) : MNode

/** 横排：若干节点并排。 */
private class MRow(val items: List<MNode>) : MNode

/** 这一行是不是**空的**（只有占位的空叶子）：空参数槽位要画虚线框，靠它判断。 */
private fun MRow.isBlank(): Boolean = items.all { it is MLeaf && it.text.isEmpty() }

/** 分式。 */
private class MFrac(val num: MRow, val den: MRow) : MNode

/** 上标。 */
private class MSup(val base: MNode, val exp: MRow, val expStart: Int = -1) : MNode

/**
 * 根号 —— 带**可选根指数**（`ⁿ√` / `∛` / `root(被开方数,次数)`）。
 *
 * @param body 被开方的内容
 * @param index 根指数；null = 平方根
 * @param indexStart 指数槽位在源码里的起始下标（-1 = 没有这个槽位）——
 *   指数为空时要靠它在左上角那块留一个**虚线占位框**，并且能被"点这儿输入"命中
 * @param bodyStart 被开方块在源码里的起始下标（同上，空的时候留占位框）
 */
private class MSqrt(
    val body: MRow,
    val index: MRow? = null,
    val indexStart: Int = -1,
    val bodyStart: Int = -1
) : MNode

/**
 * 带括号的一组（括号跟随内容缩放，暂时和正文同号）。
 *
 * @param bodyStart 括号里那块内容在源码里的起始下标 —— 括号里是空的时候
 *   （`sin()` 按下去还没输入）靠它画一块**阴影槽位**，并且让"点阴影"能把光标
 *   停到那里；`-1` = 不算槽位（比如 `()` 只是普通括号）
 */
private class MParen(
    val open: String,
    val body: MRow,
    val close: String,
    val bodyStart: Int = -1
) : MNode

/**
 * 对数 `log(真数, 底数)` —— 排成 GeoGebra 那种 **log 右下角带底数** 的样子。
 *
 * 用户原话："其中第二张图 log 底数的位置是光标，到时候你要做阴影。"
 * 也就是说：底数不是一个跟在括号里的普通参数，而是**下标位的一块槽位** ——
 * 按了 `log` 键（插入 `log(,)`）之后，光标先落在真数上，底数留在右下角画一块
 * 阴影，点它才把光标移过去。
 *
 * @param arg 真数
 * @param base 底数；null = 没写底数（这时不画槽位，按普通 `log(x)` 排）
 * @param baseStart 底数槽位在源码里的起始下标（-1 = 没有这个槽位）
 * @param argStart 真数槽位在源码里的起始下标（同上，空的时候画阴影）
 */
private class MLog(
    val arg: MRow,
    val base: MRow? = null,
    val baseStart: Int = -1,
    val argStart: Int = -1
) : MNode

/** 字形风格：正体 / 斜体 / 希腊符号（正体）。 */
internal enum class GlyphStyle { UPRIGHT, ITALIC }

/**
 * 函数名表 —— 这些标识符排成**正体**，并且后面跟括号时不额外加空格。
 *
 * 只收本 App 的键盘/字母表能打出来的（见 `MathKeyboard` 与 `AlphabetModel`）；
 * 多收几个不花钱，但宁可少收也不要误判（用户可能用 `e` 当变量）。
 */
private val FUNCTION_NAMES = setOf(
    "sin", "cos", "tan", "cot", "sec", "csc",
    "asin", "acos", "atan", "acot",
    "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
    "log", "ln", "lg", "exp", "abs", "floor", "ceil",
    "sqrt", "cbrt", "sign", "max", "min", "mod", "gcd", "lcm"
)

/** 希腊字母 / 常量的显示替换。 */
private val SYMBOLS = mapOf(
    "pi" to "π",
    "theta" to "θ",
    "alpha" to "α",
    "beta" to "β",
    "gamma" to "γ",
    "delta" to "δ",
    "epsilon" to "ε",
    "lambda" to "λ",
    "mu" to "μ",
    "sigma" to "σ",
    "phi" to "φ",
    "omega" to "ω",
    "inf" to "∞",
    "infinity" to "∞"
)

/**
 * 递归下降解析器。
 *
 * 优先级（低 → 高）：`+ -` → `* /` → `^` → 原子。
 *
 * `/` 在这一层就被吃成 [MFrac]（而不是先建一棵除法树再"事后转分式"）：
 * 分式的分子/分母各自又是一个 [MRow]，这样 `(a+b)/(c-d)` 天然排成
 * 上下两行带括号的形式，不需要二次变换。
 */
private class Parser(private val toks: List<Tok>, private val srcLen: Int) {

    private var p = 0

    fun parseAll(): MRow {
        val row = parseSum()
        // 解析器可能停在没吃掉的 token 上（语法错误），剩下的原样当文本画出来
        val rest = ArrayList<MNode>()
        while (toks[p].kind != TokKind.END) {
            val t = toks[p]
            rest += MLeaf(t.text, GlyphStyle.UPRIGHT, t.start)
            p++
        }
        return if (rest.isEmpty()) row else MRow(row.items + rest)
    }

    private fun peek(): Tok = toks[p]
    private fun atOp(s: String): Boolean = toks[p].kind == TokKind.OP && toks[p].text == s
    private fun eat(): Tok = toks[p].also { if (p < toks.size - 1) p++ }
    private fun atEnd(): Boolean = toks[p].kind == TokKind.END

    private fun parseSum(): MRow {
        val items = ArrayList<MNode>()
        items += parseProduct()
        while (atOp("+") || atOp("-")) {
            val t = eat()
            items += MLeaf(t.text, GlyphStyle.UPRIGHT, t.start)
            items += parseProduct()
        }
        return MRow(items)
    }

    private fun parseProduct(): MRow {
        var left: MNode = parsePower()
        val items = ArrayList<MNode>()
        while (true) {
            if (atOp("*")) {
                val t = eat()
                items += left
                items += MLeaf("⋅", GlyphStyle.UPRIGHT, t.start)
                left = parsePower()
            } else if (atOp("/")) {
                // 分式：左边（目前累积的这一串）当分子，右边一个"幂"当分母
                val num = MRow(if (items.isEmpty()) listOf(left) else (items + left))
                eat()
                val den = parsePower()
                left = MFrac(num, den as? MRow ?: MRow(listOf(den)))
                items.clear()
            } else {
                break
            }
        }
        return MRow(if (items.isEmpty()) listOf(left) else (items + left))
    }

    private fun parsePower(): MNode {
        val base = parseAtom()
        if (atOp("^")) {
            eat()
            var expStart = -1
            val exp = if (atOp("(")) {
                val open = eat()
                val bodyStart = toks[p].start
                val body = parseSum()
                if (atOp(")")) {
                    val close = eat()
                    MRow(listOf(MParen(open.text, body, close.text, bodyStart)))
                } else {
                    body
                }
            } else {
                expStart = toks[p].start
                MRow(listOf(parseAtom()))
            }
            // `^(...)` 的槽位由括号自己画（见 MParen 的 bodyStart），
            // 所以这里只在"指数不是括号"时把 expStart 传下去。
            return MSup(base, exp, expStart)
        }
        return base
    }

    private fun parseAtom(): MNode {
        if (atEnd()) return MLeaf("", GlyphStyle.UPRIGHT, srcLen)
        val t = toks[p]

        // 右括号 / 逗号是**边界**，不是原子：空参数（`√()`、`root(,)`、`sin()`）
        // 里如果把它们当单字符吃掉，退出时 `atOp(")")` 就再也对不上了。
        if (t.kind == TokKind.OP && (t.text == ")" || t.text == ",")) {
            return MLeaf("", GlyphStyle.UPRIGHT, t.start)
        }

        // 一元负号：`-x`，符号和变量并排
        if (t.kind == TokKind.OP && t.text == "-") {
            eat()
            return MRow(listOf(MLeaf("−", GlyphStyle.UPRIGHT, t.start), parseAtom()))
        }

        if (t.kind == TokKind.OP && t.text == "(") {
            val open = eat()
            val bodyStart = toks[p].start
            val body = parseSum()
            if (atOp(")")) {
                val close = eat()
                return MParen(open.text, body, close.text, bodyStart)
            }
            return MParen(open.text, body, "", bodyStart)
        }

        if (t.kind == TokKind.OP && t.text == "|") {
            val open = eat()
            val bodyStart = toks[p].start
            val body = parseSum()
            if (atOp("|")) {
                val close = eat()
                return MParen(open.text, body, close.text, bodyStart)
            }
            return MParen(open.text, body, "", bodyStart)
        }

        // √ / ∛：后面跟括号时，这对括号是**隐式**的 —— 不画出来，只画一根开方横线
        //（用户原话："让输入根号之后在解析里面自动带根号……还要加一个看不见的括号"）。
        if (t.kind == TokKind.OP && (t.text == "√" || t.text == "∛")) {
            val root = eat()
            // ∛ 的指数是固定的 3：它**不占源码位置**（sourceLen = 0），
            // 所以退格删不掉它，屏幕上也不出现一个可编辑的 "3"。
            val degree = if (root.text == "∛") {
                MRow(listOf(MLeaf("3", GlyphStyle.UPRIGHT, root.start, sourceLen = 0)))
            } else {
                null
            }
            if (atOp("(")) {
                eat()
                // 指数槽位只对真正的 n 次根号有意义；√ / ∛ 的左上角没有槽位
                val idxStart = if (root.text == "∛") root.start else -1
                val bodyStart = toks[p].start
                val body = parseSum()
                if (atOp(")")) eat()
                return MSqrt(body, degree, idxStart, bodyStart)
            }
            return MSqrt(MRow(listOf(parseAtom())), degree)
        }

        if (t.kind == TokKind.NUMBER) {
            eat()
            return MLeaf(t.text, GlyphStyle.UPRIGHT, t.start)
        }

        if (t.kind == TokKind.IDENT) {
            val lower = t.text.lowercase()
            // root(被开方数, 次数) 吃成**带根指数的根号**（ⁿ√ 键插的就是它）。
            // 和 sqrt 一样是结构，不能按普通函数名排。
            if (lower == "root" && p + 1 < toks.size &&
                toks[p + 1].kind == TokKind.OP && toks[p + 1].text == "("
            ) {
                eat() // root
                eat() // (
                val bodyStart = toks[p].start
                val body = parseSum()
                var index: MRow? = null
                var indexStart = -1
                if (atOp(",")) {
                    eat()
                    indexStart = toks[p].start
                    val idx = parseSum()
                    index = if (idx.isBlank()) null else idx
                }
                if (atOp(")")) eat()
                return MSqrt(body, index, indexStart, bodyStart)
            }
            // sqrt(...) 吃成根号：它和 `^` 一样是**结构**，不能按普通函数名排
            if (lower == "sqrt" && p + 1 < toks.size &&
                toks[p + 1].kind == TokKind.OP && toks[p + 1].text == "("
            ) {
                eat() // sqrt
                eat() // (
                val body = parseSum()
                if (atOp(")")) eat()
                return MSqrt(body)
            }
            // abs(...) 排成竖线，比 `abs` 三个字母更像数学
            if (lower == "abs" && p + 1 < toks.size &&
                toks[p + 1].kind == TokKind.OP && toks[p + 1].text == "("
            ) {
                eat()
                eat()
                val bodyStart = toks[p].start
                val body = parseSum()
                if (atOp(")")) eat()
                return MParen("|", body, "|", bodyStart)
            }
            // 向上/向下取整排成 ⌈x⌉ / ⌊x⌋（LaTeX 的 \lceil…\rceil、\lfloor…\rfloor）。
            // 用户原话："向上取整和向下取整函数没有 LaTeX 语法的符号表达，只有 ceil() 和 floor()"。
            // 和 sqrt/abs 一样是**结构**：丢掉函数名，只留括号，
            // 括号本身会按内容高度放大（见 [Measurer] 的 MParen 分支）。
            if ((lower == "ceil" || lower == "floor") && p + 1 < toks.size &&
                toks[p + 1].kind == TokKind.OP && toks[p + 1].text == "("
            ) {
                eat()
                eat()
                val bodyStart = toks[p].start
                val body = parseSum()
                if (atOp(")")) eat()
                return if (lower == "ceil") {
                    MParen("⌈", body, "⌉", bodyStart)
                } else {
                    MParen("⌊", body, "⌋", bodyStart)
                }
            }
            // log(底数, 真数)：底数排成右下角下标（GeoGebra 同款），
            // 而不是跟在逗号后面的第二个参数 —— 见 [MLog]。
            // 引擎的约定是 `log(b, x)`（第一个参数是底数，见 `MathFns`），
            // 所以逗号前的那个才是底数、逗号后的那个进括号。
            // `log(x)` 也走这里（baseStart = -1，不画底数槽位）。
            if (lower == "log" && p + 1 < toks.size &&
                toks[p + 1].kind == TokKind.OP && toks[p + 1].text == "("
            ) {
                eat() // log
                eat() // (
                val firstStart = toks[p].start
                var arg = parseSum()
                var argStart = firstStart
                var base: MRow? = null
                var baseStart = -1
                if (atOp(",")) {
                    eat()
                    baseStart = firstStart
                    base = if (arg.isBlank()) null else arg
                    argStart = toks[p].start
                    arg = parseSum()
                }
                if (atOp(")")) eat()
                return MLog(arg, base, baseStart, argStart)
            }
            eat()
            SYMBOLS[lower]?.let { sym ->
                // ⚠️ 希腊字母**必须用斜体字体**：KaTeX_Main-Regular 里没有 α/β/π/θ，
                // 只有 KaTeX_Math-Italic 有。用正体会画成一串豆腐块。
                // `∞` 是唯一的例外 —— 它在 Main 里，Math-Italic 反而没有。
                val style = if (sym == "∞") GlyphStyle.UPRIGHT else GlyphStyle.ITALIC
                return MLeaf(sym, style, t.start, t.text.length)
            }
            return MLeaf(
                t.text,
                if (lower in FUNCTION_NAMES) GlyphStyle.UPRIGHT else GlyphStyle.ITALIC,
                t.start
            )
        }

        // 其它单字符运算符
        eat()
        return MLeaf(
            when (t.text) {
                // KaTeX_Main 里没有 U+00B7（·），但有 U+22C5（⋅，真正的数算子点）
                "*" -> "⋅"
                "-" -> "−"
                else -> t.text
            },
            GlyphStyle.UPRIGHT,
            t.start
        )
    }
}

// ---------------------------------------------------------------------------
// 3. 测量
// ---------------------------------------------------------------------------

/** 测量结果：宽 + 基线上方高 + 基线下方深。 */
internal class Size(val w: Float, val asc: Float, val desc: Float) {
    val h: Float get() = asc + desc
}

/** 一个待绘制的字形。 */
internal class Glyph(
    val text: String,
    /** 相对整个公式左上角的 x。 */
    val x: Float,
    /** 基线 y（相对整个公式左上角）。 */
    val baseline: Float,
    val sizePx: Float,
    val style: GlyphStyle
)

/** 一条待绘制的直线（分数线 / 根号横线）。 */
internal class Rule(val x1: Float, val y1: Float, val x2: Float, val y2: Float, val width: Float)

/**
 * 一条待描边的路径 —— 根号那个勾。
 *
 * 为什么不是几条 `drawLine`：勾的拐角必须是**圆角**，两段直线接起来
 * 会在折点处露出一个尖角，放大后很刺眼（上一版就是两条直线，看起来像"对勾"）。
 * 这里用 `Path` + `StrokeJoin.Round`，拐点自然收圆。
 */
internal class PathRule(val path: Path, val width: Float)

/**
 * 空槽位的画法。
 *
 * 用户原话分得很清楚：
 * > "我是说图标用虚线框，实际运算用阴影框，点击阴影位置输入之后不显示阴影。"
 *
 * 所以同一个"空参数槽位"有两种画法，由调用方选：
 *   * [SHADOW] —— **实际运算**（输入框里的表达式）：一块半透明阴影；
 *   * [DASHED] —— **图标**（键盘上 `ⁿ√` 那种键帽）：一圈虚线框。
 */
internal enum class SlotStyle { SHADOW, DASHED, NONE }

/**
 * 一个**阴影占位块**（实际运算用，见 [SlotStyle.SHADOW]）。
 *
 * 它**有占位体积**：测量阶段就按它的宽高预留位置，所以阴影左右不会多出
 * "可选光标的位置"（用户原话："阴影没有占位体积，请不要让阴影左边和右边可以选择光标"）。
 */
internal class Placeholder(
    val x: Float,
    val y: Float,
    val w: Float,
    val h: Float
)

/**
 * 一个**占位框**（虚线或细实线）。
 *
 * 用在"按了键、但槽位还空着"的地方：n 次根号的根指数位置、根号下面那块。
 * 图标用虚线框（见 [SlotStyle.DASHED]），实际运算用 [Placeholder] 画阴影。
 */
internal class Outline(
    val x: Float,
    val y: Float,
    val w: Float,
    val h: Float,
    val dashed: Boolean,
    val width: Float
)

/**
 * 一个光标停靠点。
 *
 * @param index 源码下标（0..len，插入位置）
 * @param x 竖线的 x
 * @param top 竖线上沿
 * @param bottom 竖线下沿
 */
internal class Stop(val index: Int, val x: Float, val top: Float, val bottom: Float)

/**
 * 排版结果。**不可变**：`remember(src, size)` 缓存住，绘制阶段只读。
 *
 * 坐标系统一为"相对公式左上角"，基线的绝对 y 由调用方决定 ——
 * 这样同一个公式可以画在输入框里、也可以画在别处。
 */
internal class MathLayout(
    val width: Float,
    val ascent: Float,
    val descent: Float,
    private val glyphs: List<Glyph>,
    private val rules: List<Rule>,
    private val paths: List<PathRule>,
    private val outlines: List<Outline>,
    private val placeholders: List<Placeholder>,
    private val stops: List<Stop>,
    /** 字形来源（KaTeX 的 ttf；null = 系统字体）。 */
    private val fonts: MathTypefaces
) {

    val height: Float get() = ascent + descent

    /**
     * 绘制用的 Paint —— **跟着 layout 一起缓存**。
     *
     * 不能放在 `draw()` 里 new：Canvas 的绘制 lambda 每帧都会跑，
     * 一帧一个 Paint 在手表上就是持续的 GC 压力（这个 App 已经在
     * `GraphCanvas` 里为了同一个理由复用过 Paint）。
     * 布局对象本身是 `remember` 住的，所以它活多久、Paint 就活多久。
     */
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    /** 源码长度对应的"末尾光标"x（没有 stop 时的兜底）。 */
    private val endX: Float get() = stops.lastOrNull()?.x ?: width

    /**
     * 画出来。
     *
     * @param origin 公式左上角在画布上的位置
     * @param color 文字和线共用的颜色（跟随主题）
     * @param caretIndex 光标在源码里的下标；传 null 不画光标
     * @param caretColor 光标颜色
     * @param caretAlpha 闪烁透明度
     */
    fun draw(
        scope: DrawScope,
        origin: Offset,
        color: Color,
        caretIndex: Int? = null,
        caretColor: Color = Color.White,
        caretAlpha: Float = 1f,
        caretWidth: Float = 1.5f
    ) {
        scope.drawIntoCanvas { canvas ->
            val native = canvas.nativeCanvas
            // ---- 空槽位阴影：画在**最底下**（文字盖在上面）----
            //
            // 用户要求"点击阴影位置输入之后不显示阴影"：槽位非空时
            // [layoutMath] 根本不会往 [placeholders] 里放东西，所以这里不需要任何判断。
            if (placeholders.isNotEmpty()) {
                paint.style = Paint.Style.FILL
                paint.pathEffect = null
                paint.color = color.copy(alpha = 0.16f).toArgbCompat()
                for (ph in placeholders) {
                    native.drawRoundRect(
                        origin.x + ph.x,
                        origin.y + ph.y,
                        origin.x + ph.x + ph.w,
                        origin.y + ph.y + ph.h,
                        ph.h * 0.34f,
                        ph.h * 0.34f,
                        paint
                    )
                }
            }
            for (g in glyphs) {
                paint.textSize = g.sizePx
                paint.typeface = typefaceOf(g.style)
                paint.color = color.toArgbCompat()
                native.drawText(g.text, origin.x + g.x, origin.y + g.baseline, paint)
            }
            paint.color = color.toArgbCompat()
            paint.style = Paint.Style.FILL
            for (r in rules) {
                paint.strokeWidth = r.width
                native.drawLine(
                    origin.x + r.x1, origin.y + r.y1,
                    origin.x + r.x2, origin.y + r.y2,
                    paint
                )
            }
            if (paths.isNotEmpty()) {
                paint.style = Paint.Style.STROKE
                paint.strokeJoin = Paint.Join.ROUND
                paint.strokeCap = Paint.Cap.ROUND
                canvas.save()
                canvas.translate(origin.x, origin.y)
                for (pr in paths) {
                    paint.strokeWidth = pr.width
                    native.drawPath(pr.path, paint)
                }
                canvas.restore()
                paint.style = Paint.Style.FILL
            }
            if (outlines.isNotEmpty()) {
                paint.style = Paint.Style.STROKE
                for (o in outlines) {
                    paint.strokeWidth = o.width
                    paint.pathEffect = if (o.dashed) {
                        DashPathEffect(floatArrayOf(o.width * 2.6f, o.width * 1.9f), 0f)
                    } else {
                        null
                    }
                    native.drawRect(
                        origin.x + o.x, origin.y + o.y,
                        origin.x + o.x + o.w, origin.y + o.y + o.h,
                        paint
                    )
                }
                paint.pathEffect = null
                paint.style = Paint.Style.FILL
            }
            if (caretIndex != null) {
                val s = stopFor(caretIndex)
                paint.color = caretColor.copy(alpha = caretColor.alpha * caretAlpha).toArgbCompat()
                paint.strokeWidth = caretWidth
                native.drawLine(
                    origin.x + s.x, origin.y + s.top,
                    origin.x + s.x, origin.y + s.bottom,
                    paint
                )
            }
        }
    }

    /**
     * 离某个源码下标最近的光标停靠点。
     *
     * 停靠点是**按源码顺序**登记的（分子在前、分母在后），所以直接找
     * `index` 相等或第一个更大的那一个；找不到就是末尾。
     */
    private fun stopFor(index: Int): Stop {
        var best = stops.lastOrNull() ?: Stop(0, endX, 0f, height)
        for (s in stops) {
            if (s.index >= index) {
                // 取"最接近"的：相同 index 里取第一个（同一位置可能有两个停靠点）
                best = s
                break
            }
            best = s
        }
        return best
    }

    /**
     * 屏幕 x（相对公式左边）→ 最近的源码下标。
     *
     * 拖动光标用：找 x 距离最近的那个停靠点。返回它的 index。
     */
    fun indexAt(x: Float): Int {
        if (stops.isEmpty()) return 0
        var best = stops[0]
        var bestD = kotlin.math.abs(x - best.x)
        for (s in stops) {
            val d = kotlin.math.abs(x - s.x)
            // 同样近时**取后面的**：光标放在字符右侧更符合"点到哪就到哪"的直觉
            if (d <= bestD) {
                best = s
                bestD = d
            }
        }
        return best.index
    }

    /** 光标 x（相对公式左边），用于横向滚动。 */
    fun caretX(index: Int): Float = stopFor(index).x

    private fun typefaceOf(style: GlyphStyle): Typeface = fonts.of(style)
}

/** `Color` → `Int`（避免为了一个 toArgb 引入 graphics 包的重载歧义）。 */
private fun Color.toArgbCompat(): Int = android.graphics.Color.argb(
    (alpha * 255f).toInt().coerceIn(0, 255),
    (red * 255f).toInt().coerceIn(0, 255),
    (green * 255f).toInt().coerceIn(0, 255),
    (blue * 255f).toInt().coerceIn(0, 255)
)

// ---------------------------------------------------------------------------
// 4. 布局
// ---------------------------------------------------------------------------

/**
 * 排版入口。
 *
 * @param source 源码（`fn.src1` 原文）
 * @param baseSizePx 正文字号（px）
 * @param maxWidthPx 可用宽度；超出时**整体等比缩小**（下限 [MIN_SCALE]），
 *   仍超出则交给调用方裁剪/滚动 —— 排版层不裁，因为它不知道滚动偏移。
 */
internal fun layoutMath(
    source: String,
    baseSizePx: Float,
    maxWidthPx: Float,
    fonts: MathTypefaces = MathTypefaces(null, null),
    /** 空槽位画法：输入框用阴影（[SlotStyle.SHADOW]），键帽图标用虚线（[SlotStyle.DASHED]）。 */
    slot: SlotStyle = SlotStyle.SHADOW
): MathLayout {
    // 先翻 LaTeX 再排版：`\frac{a}{b}` → `(a)/(b)`、`\sqrt[n]{x}` → `root(x,n)`。
    // 没有 `\` / `{` 时 [Latex.expand] 原样返回，所以普通输入（以及键盘图标
    // 那种 `root(x,)` 的固定串）的排版和光标下标完全不变。
    val cooked = Latex.expand(source)
    val root = Parser(tokenize(cooked), cooked.length).parseAll()

    val measurer = Measurer(baseSizePx, fonts, slot)
    var size = measurer.measure(root)
    var scale = 1f
    if (maxWidthPx > 1f && size.w > maxWidthPx) {
        scale = (maxWidthPx / size.w).coerceIn(MIN_SCALE, 1f)
    }

    val s = MathLayoutBuilder(baseSizePx * scale, cooked.length, fonts, slot)
    val laid = s.layoutItems(root.items, 0f, 0f, baseSizePx * scale)
    // 停靠点按源码下标排序：根号的**指数**画在左边、源码下标却在后面，
    // 不排序会让 [MathLayout.stopFor] 的"第一个不小于目标的停靠点"提前命中。
    s.stops.sortBy { it.index }
    return MathLayout(
        width = laid.size.w,
        ascent = laid.size.asc,
        descent = laid.size.desc,
        glyphs = s.glyphs,
        rules = s.rules,
        paths = s.paths,
        outlines = s.outlines,
        placeholders = s.placeholders,
        stops = s.stops,
        fonts = fonts
    )
}

/**
 * 括号的字号：内容越高、括号越大。
 *
 * 正常一行（`sin(x)`）时 = 原字号；含分式/根号时最多放大到 1.9 倍。
 * 上限是必要的 —— 一个三层嵌套的分式会让括号大到抢戏。
 */
private fun parenSizeFor(body: Size, sizePx: Float): Float {
    val natural = sizePx * 0.94f
    val scale = (body.h / natural).coerceIn(1f, 1.9f)
    return sizePx * scale
}

/** 自动缩小的下限：再小就谁也看不清了，宁可由调用方裁剪。 */
private const val MIN_SCALE = 0.55f

/** 上下标的相对字号。 */
private const val SCRIPT_SCALE = 0.66f

/** 分式内部的相对字号（GeoGebra 也会把分式里的字缩一点）。 */
private const val FRAC_SCALE = 0.92f

/**
 * **数学轴高度**（TeX 的 `axis_height`）：分数线就画在这条线上。
 *
 * 这是分式位置的唯一参照 —— 不是"贴着分子顶部"，而是**固定在基线上方 0.25em**。
 * 上一版用 `baseline - num.h - pad`，导致分子里的东西越多、整条分数线越往上跑，
 * 一个 `(a+b)/c` 会飘到半空中，这是"看起来丑"的主要原因之一。
 */
private const val AXIS_HEIGHT = 0.25f

/** 分子/分母与分数线之间的间隙（相对当前字号）。 */
private const val FRAC_GAP = 0.12f

/** 上标相对基线的抬升（相对当前字号）。 */
private const val SUP_RISE = 0.42f

/** 根号横线与被开方内容之间的留白（相对当前字号）。 */
private const val SQRT_PAD = 0.10f

/** 根指数（`ⁿ√` 的 n）的字号，相对正文 —— 和 TeX 的 script size 同一档。 */
private const val ROOT_INDEX_SCALE = 0.58f

/** 空根指数槽位的宽度（相对指数字号）：左上角那块虚线框。 */
private const val ROOT_SLOT_W = 0.42f

/** 根指数与根号勾之间的水平间距（相对正文字号）。 */
private const val ROOT_INDEX_GAP = 0.05f

/**
 * 空槽位的宽度（相对当前字号）。
 *
 * 它同时是**测量宽度**和**绘制宽度** —— 一个来源，所以"阴影没有占位体积"
 * 不会发生：阴影占多宽，光标就只能在它里面/两侧的合法位置停。
 */
private const val SLOT_W = 0.52f

/** 空槽位的高度（相对当前字号）—— 用户要求"确保它有一个半字符的高度"。 */
private const val SLOT_H = 0.74f

/** 测量器：只算尺寸，不产生绘制指令。 */
private class Measurer(
    private val base: Float,
    private val fonts: MathTypefaces,
    /** 空槽位画不画 —— 画的话测量宽度里要给它留位置（见 [SLOT_W]）。 */
    private val slot: SlotStyle
) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    fun measure(n: MNode, sizePx: Float = base): Size = when (n) {
        is MLeaf -> measureLeaf(n, sizePx)
        is MRow -> measureRow(n, sizePx)
        is MFrac -> {
            val num = measureRow(n.num, sizePx * FRAC_SCALE)
            val den = measureRow(n.den, sizePx * FRAC_SCALE)
            val axis = sizePx * AXIS_HEIGHT
            val gap = sizePx * FRAC_GAP
            Size(
                w = max(num.w, den.w),
                asc = axis + gap + num.h,
                desc = (den.h + gap - axis).coerceAtLeast(sizePx * 0.05f)
            )
        }
        is MSup -> {
            val b = measure(n.base, sizePx)
            val eSize = sizePx * SCRIPT_SCALE
            val e = measureRow(n.exp, eSize)
            // 空指数：给它一块槽位的宽度（否则 `^` 按下去什么都看不到）
            val eW = if (slot != SlotStyle.NONE && n.exp.isBlank()) {
                max(e.w, eSize * SLOT_W)
            } else {
                e.w
            }
            Size(
                w = b.w + eW,
                asc = max(b.asc, e.h + sizePx * SUP_RISE),
                desc = b.desc
            )
        }
        is MSqrt -> {
            val body = measureRow(n.body, sizePx)
            val pad = sizePx * SQRT_PAD
            val hook = sizePx * 0.62f
            // 根指数：坐在左上角，所以整棵根号的宽度要加上它、高度也要往上顶
            val idxSize = sizePx * ROOT_INDEX_SCALE
            val idxW = when {
                n.index != null -> measureRow(n.index, idxSize).w
                n.indexStart >= 0 -> idxSize * ROOT_SLOT_W
                else -> 0f
            }
            val idxGap = if (idxW > 0f) sizePx * ROOT_INDEX_GAP else 0f
            Size(
                w = idxW + idxGap + hook + body.w + pad,
                asc = body.h + pad * 2 + (if (idxW > 0f) idxSize * 0.78f else 0f),
                desc = sizePx * 0.12f
            )
        }
        is MParen -> {
            val body = measureRow(n.body, sizePx)
            // 空括号（`sin()`）：括号里那块阴影也要占宽度 ——
            // 这就是"阴影有占位体积、左右不会多出光标位"的实现点。
            val bodyW = if (slot != SlotStyle.NONE && n.body.isBlank() && n.bodyStart >= 0) {
                max(body.w, sizePx * SLOT_W)
            } else {
                body.w
            }
            // 括号按内容高度放大（`(a/b)` 里的括号必须比单个字母高）。
            // TeX 是靠 Size1~4 四套字形做的，我们只有一套 ttf，所以按比例放大字号 ——
            // 笔画会略粗一点，但比"括号明显比内容矮"好得多。
            val pSize = parenSizeFor(body, sizePx)
            val ow = textWidth(n.open, pSize, GlyphStyle.UPRIGHT)
            val cw = if (n.close.isEmpty()) 0f else textWidth(n.close, pSize, GlyphStyle.UPRIGHT)
            Size(w = ow + bodyW + cw, asc = body.asc, desc = body.desc)
        }
        is MLog -> {
            val body = measureRow(n.arg, sizePx)
            // 真数是空的时候给槽位留宽度（否则括号里的阴影没有体积）
            val bodyW = if (slot != SlotStyle.NONE && n.arg.isBlank() && n.argStart >= 0) {
                max(body.w, sizePx * SLOT_W)
            } else {
                body.w
            }
            val pSize = parenSizeFor(body, sizePx)
            val ow = textWidth("(", pSize, GlyphStyle.UPRIGHT)
            val cw = textWidth(")", pSize, GlyphStyle.UPRIGHT)
            val nameW = textWidth("log", sizePx, GlyphStyle.UPRIGHT)
            // 底数是右下角的下标：占一行下方的高度，宽度同样要给槽位留位置
            val baseSize = sizePx * SCRIPT_SCALE
            val baseW = when {
                n.base != null -> measureRow(n.base, baseSize).w
                n.baseStart >= 0 && slot != SlotStyle.NONE -> baseSize * SLOT_W
                else -> 0f
            }
            val baseBottom = if (baseW > 0f) sizePx * 0.45f + baseSize * 0.22f else 0f
            Size(
                w = nameW + baseW + ow + bodyW + cw,
                asc = body.asc,
                desc = max(body.desc, baseBottom)
            )
        }
    }

    private fun measureLeaf(n: MLeaf, sizePx: Float): Size {
        if (n.text.isEmpty()) return Size(0f, sizePx * 0.78f, sizePx * 0.22f)
        val w = textWidth(n.text, sizePx, n.style)
        return Size(w, sizePx * 0.78f, sizePx * 0.22f)
    }

    fun measureRow(row: MRow, sizePx: Float): Size {
        var w = 0f
        var asc = 0f
        var desc = 0f
        for (item in row.items) {
            val s = measure(item, sizePx)
            w += s.w
            asc = max(asc, s.asc)
            desc = max(desc, s.desc)
        }
        if (row.items.isEmpty()) return Size(0f, sizePx * 0.78f, sizePx * 0.22f)
        return Size(w, asc, desc)
    }

    fun textWidth(text: String, sizePx: Float, style: GlyphStyle): Float {
        paint.textSize = sizePx
        paint.typeface = fonts.of(style)
        return paint.measureText(text)
    }
}

/** 布局产物：摊平的字形表 / 线表 / 光标停靠点表。 */
private class MathLayoutBuilder(
    private val base: Float,
    private val srcLen: Int,
    private val fonts: MathTypefaces,
    /** 空槽位画法（阴影 / 虚线 / 不画），见 [SlotStyle]。 */
    private val slot: SlotStyle
) {

    val glyphs = ArrayList<Glyph>()
    val rules = ArrayList<Rule>()
    val paths = ArrayList<PathRule>()
    val outlines = ArrayList<Outline>()
    val placeholders = ArrayList<Placeholder>()
    val stops = ArrayList<Stop>()

    private val measurer = Measurer(base, fonts, slot)

    /** 画一块空槽位：阴影 / 虚线两选一（[SlotStyle.NONE] 时什么都不做）。 */
    private fun addSlot(x: Float, y: Float, w: Float, h: Float, stroke: Float) {
        when (slot) {
            SlotStyle.NONE -> Unit
            SlotStyle.SHADOW -> placeholders += Placeholder(x, y, w, h)
            SlotStyle.DASHED -> outlines += Outline(
                x = x,
                y = y,
                w = w,
                h = h,
                dashed = true,
                width = max(1f, stroke)
            )
        }
    }

    internal class Laid(val size: Size)

    /**
     * 排一行：逐项往右摆。
     *
     * @param x 这一行的左端
     * @param baseline 这一行的基线
     */
    fun layoutRow(row: MRow, x: Float, baseline: Float, sizePx: Float): Laid =
        layoutItems(row.items, x, baseline, sizePx)

    /**
     * 排一行。
     *
     * ⚠️ [sizePx] **必须显式传**：分式里的字是缩小的（[FRAC_SCALE]）、
     * 上标更小（[SCRIPT_SCALE]）。测量阶段用的是缩小后的字号，
     * 绘制阶段如果还按整体字号画，两边就对不上 —— 分式会顶出横线、
     * 上标会盖住基字。这个参数就是让两边用同一个数。
     */
    fun layoutItems(items: List<MNode>, x: Float, baseline: Float, sizePx: Float): Laid {
        var cx = x
        var asc = 0f
        var desc = 0f
        for (item in items) {
            val s = measurer.measure(item, sizePx)
            layoutNode(item, cx, baseline, sizePx)
            cx += s.w
            asc = max(asc, s.asc)
            desc = max(desc, s.desc)
        }
        // 行末也登记一个停靠点：源码里"这一行结束"的位置
        registerEndStop(items, cx, baseline - asc, baseline + desc)
        return Laid(Size(cx - x, asc, desc))
    }

    /**
     * 行末停靠点：找这一行里**最后一段源码**的结束下标。
     *
     * 不这么做的话，`sin(` 这种"最后一个 token 是括号"的输入，光标会停在
     * 括号的_左边_（因为括号的叶子只登记了起点），用户会以为光标不动。
     */
    private fun registerEndStop(items: List<MNode>, x: Float, top: Float, bottom: Float) {
        val lastEnd = lastSourceIndex(items) ?: return
        // 同一位置已经有停靠点就不再重复（重复会让 `indexAt` 抖动）
        if (stops.any { it.index == lastEnd && kotlin.math.abs(it.x - x) < 0.5f }) return
        stops += Stop(lastEnd, x, top, bottom)
    }

    /** 一行里最靠后的源码下标（含 token 长度）。 */
    private fun lastSourceIndex(items: List<MNode>): Int? {
        var best: Int? = null
        for (item in items) {
            val e = endIndexOf(item)
            if (e != null && (best == null || e > best)) best = e
        }
        return best
    }

    private fun endIndexOf(n: MNode): Int? = when (n) {
        is MLeaf -> if (n.text.isEmpty()) null else n.start + sourceLenOf(n)
        is MRow -> lastSourceIndex(n.items)
        is MFrac -> maxOfNotNull(endIndexOf(n.num), endIndexOf(n.den))
        is MSup -> maxOfNotNull(
            maxOfNotNull(endIndexOf(n.base), endIndexOf(n.exp)),
            // 空指数（`x^` 还没填）：源码下标得算上槽位那一位
            n.expStart.takeIf { it >= 0 }
        )
        is MSqrt -> maxOfNotNull(endIndexOf(n.body), n.index?.let { endIndexOf(it) })
        is MParen -> maxOfNotNull(
            endIndexOf(n.body),
            if (n.close.isEmpty()) null else endIndexOf(n.body)?.plus(1)
        )
        is MLog -> maxOfNotNull(
            // `log(...)` 的右括号也要算一位（和 MParen 同一个口径）
            if (n.arg.isBlank()) n.argStart.takeIf { it >= 0 }
            else endIndexOf(n.arg)?.plus(1),
            // 底数写在逗号后面，所以它一定比真数靠后
            n.base?.let { endIndexOf(it) } ?: n.baseStart.takeIf { it >= 0 }
        )
    }

    private fun maxOfNotNull(a: Int?, b: Int?): Int? = when {
        a == null -> b
        b == null -> a
        else -> max(a, b)
    }

    /**
     * 叶子在源码里占几个字符。
     *
     * 注意**符号替换**：`pi`（2 字符）画成 `π`（1 字符），但光标位置仍按源码算，
     * 所以这里必须回源码长度，不能用显示文本的长度。
     */
    private fun sourceLenOf(n: MLeaf): Int = when (n.text) {
        "π", "θ", "α", "β", "γ", "δ", "ε", "λ", "μ", "σ", "φ", "ω", "∞" -> 2
        "−", "·" -> 1
        else -> n.text.length
    }

    private fun layoutNode(n: MNode, x: Float, baseline: Float, sizePx: Float) {
        when (n) {
            is MLeaf -> {
                if (n.text.isEmpty()) return
                glyphs += Glyph(n.text, x, baseline, sizePx, n.style)
                val w = measurer.textWidth(n.text, sizePx, n.style)
                val top = baseline - sizePx * 0.78f
                val bottom = baseline + sizePx * 0.22f
                // 起点和终点各一个停靠点：拖到字的左半边/右半边都能对上
                stops += Stop(n.start, x, top, bottom)
                stops += Stop(min(n.start + sourceLenOf(n), srcLen), x + w, top, bottom)
            }

            is MRow -> {
                var cx = x
                for (item in n.items) {
                    layoutNode(item, cx, baseline, sizePx)
                    cx += measurer.measure(item, sizePx).w
                }
            }

            is MFrac -> {
                val numSize = sizePx * FRAC_SCALE
                val denSize = sizePx * FRAC_SCALE
                val num = measurer.measure(n.num, numSize)
                val den = measurer.measure(n.den, denSize)
                val axis = sizePx * AXIS_HEIGHT
                val gap = sizePx * FRAC_GAP
                val w = max(num.w, den.w)
                // 分数线固定在数学轴上（基线往上 axis），分式整体因此是"围绕基线"的
                val barY = baseline - axis
                // 分子：**底边**贴在横线上方 gap 处
                layoutRow(n.num, x + (w - num.w) / 2f, barY - gap - num.desc, numSize)
                // 分母：**顶边**贴在横线下方 gap 处（基线 = 顶边 + ascent）
                layoutRow(n.den, x + (w - den.w) / 2f, barY + gap + den.asc, denSize)
                // 分数线粗细取 TeX 的 0.04em（上一版 0.055 偏粗，像一条黑杠）
                rules += Rule(x, barY, x + w, barY, max(1.1f, sizePx * 0.04f))
            }

            is MSup -> {
                val b = measurer.measure(n.base, sizePx)
                layoutNode(n.base, x, baseline, sizePx)
                val eSize = sizePx * SCRIPT_SCALE
                // 指数基线抬高：抬到"基字顶边往下一点"的位置，视觉上贴着右上角
                val eBaseline = baseline - b.asc - sizePx * SUP_RISE + sizePx * 0.62f
                layoutRow(n.exp, x + b.w, eBaseline, eSize)
                if (slot != SlotStyle.NONE && n.exp.isBlank() && n.expStart >= 0) {
                    // 空指数槽位：按了 `^` 还没输入时要有一块阴影，点了能落到这里
                    val sw = eSize * SLOT_W
                    val sh = eSize * SLOT_H
                    val sy = eBaseline - eSize * 0.78f
                    addSlot(x + b.w, sy, sw, sh, max(1f, sizePx * 0.045f))
                    stops += Stop(n.expStart, x + b.w + sw * 0.4f, sy, sy + sh)
                }
            }

            is MSqrt -> {
                val body = measurer.measure(n.body, sizePx)
                val pad = sizePx * SQRT_PAD
                val hook = sizePx * 0.58f
                val stroke = max(1.2f, sizePx * 0.045f)

                // ---- 根指数：坐在左上角（GeoGebra 同款）----
                val idxSize = sizePx * ROOT_INDEX_SCALE
                val idxW = when {
                    n.index != null -> measurer.measureRow(n.index, idxSize).w
                    n.indexStart >= 0 -> idxSize * ROOT_SLOT_W
                    else -> 0f
                }
                val idxGap = if (idxW > 0f) sizePx * ROOT_INDEX_GAP else 0f
                // 根号勾从指数右边开始：指数占了左上角，勾不能压着它
                val left = x + idxW + idxGap

                val top = baseline - body.asc - pad
                val botY = top + body.h + pad * 0.55f
                // 勾的上沿（横线起点）和"肩"的位置，参照 TeX 的 radical 比例
                val shoulderY = top + body.h * 0.52f

                // 一道连续路径（不是三段直线）：拐角靠 StrokeJoin.Round 收圆，
                // 观感是"一支笔写下来的 √"，而不是"对勾 + 横线"
                val path = Path().apply {
                    moveTo(left + hook * 0.02f, shoulderY)
                    lineTo(left + hook * 0.26f, shoulderY - sizePx * 0.045f)
                    lineTo(left + hook * 0.55f, botY)
                    lineTo(left + hook * 0.98f, top)
                }
                paths += PathRule(path, stroke)

                // 横线：从勾顶一直拉到被开方内容末尾（略向右探出一点，像 TeX）
                rules += Rule(
                    left + hook * 0.96f, top,
                    left + hook + body.w + pad * 1.2f, top,
                    stroke
                )

                // 指数基线：底边贴着横线（TeX 的 \sqrt[3] 就是这个位置）
                val idxBaseline = top - idxSize * 0.08f
                val idxBoxTop = idxBaseline - idxSize * 0.78f
                if (n.index != null) {
                    layoutRow(n.index, x, idxBaseline, idxSize)
                } else if (n.indexStart >= 0) {
                    // 空指数槽位：点它可以来这儿输入（用户要的
                    // "把 n 次根号的开次方位置变成可以点击输入 + 用虚线圈着"）
                    addSlot(
                        x = x + idxSize * 0.04f,
                        y = idxBoxTop,
                        w = idxSize * ROOT_SLOT_W,
                        h = idxSize * 0.92f,
                        stroke = max(1f, stroke * 0.75f)
                    )
                    val cy = idxBoxTop + idxSize * 0.46f
                    stops += Stop(n.indexStart, x + idxSize * 0.25f, cy - idxSize * 0.42f, cy + idxSize * 0.42f)
                }

                // ---- 被开方内容 ----
                if (n.body.isBlank()) {
                    // 空被开方数：同样留一块槽位，免得"按了根号什么都没出现"
                    val bx = left + hook + pad * 0.3f
                    val bw = sizePx * 0.55f
                    addSlot(
                        x = bx,
                        y = top + pad * 0.6f,
                        w = bw,
                        h = body.h.coerceAtLeast(sizePx * 0.8f),
                        stroke = max(1f, stroke * 0.75f)
                    )
                    if (n.bodyStart >= 0) {
                        val cy = top + body.h * 0.5f
                        stops += Stop(n.bodyStart, bx + bw / 2f, cy - body.h * 0.4f, cy + body.h * 0.4f)
                    }
                } else {
                    layoutRow(n.body, left + hook, baseline, sizePx)
                }
            }

            is MParen -> {
                val body = measurer.measure(n.body, sizePx)
                val pSize = parenSizeFor(body, sizePx)
                val ow = measurer.textWidth(n.open, pSize, GlyphStyle.UPRIGHT)
                // 空括号（`sin()`）里的槽位要占宽度，否则右括号会压到阴影上
                val bodyW = if (slot != SlotStyle.NONE && n.body.isBlank() && n.bodyStart >= 0) {
                    max(body.w, sizePx * SLOT_W)
                } else {
                    body.w
                }
                glyphs += Glyph(n.open, x, baseline, pSize, GlyphStyle.UPRIGHT)
                layoutRow(n.body, x + ow, baseline, sizePx)
                if (slot != SlotStyle.NONE && n.body.isBlank() && n.bodyStart >= 0) {
                    // 括号里的空槽位：半字符高，点了才能落光标
                    val sh = sizePx * SLOT_H
                    val sy = baseline - sizePx * 0.74f
                    addSlot(x + ow, sy, bodyW, sh, max(1f, sizePx * 0.045f))
                    stops += Stop(
                        n.bodyStart,
                        x + ow + bodyW * 0.4f,
                        sy,
                        sy + sh
                    )
                }
                if (n.close.isNotEmpty()) {
                    glyphs += Glyph(
                        n.close, x + ow + bodyW, baseline, pSize, GlyphStyle.UPRIGHT
                    )
                }
            }
            is MLog -> {
                val body = measurer.measure(n.arg, sizePx)
                val pSize = parenSizeFor(body, sizePx)
                val nameW = measurer.textWidth("log", sizePx, GlyphStyle.UPRIGHT)
                val baseSize = sizePx * SCRIPT_SCALE
                val baseW = when {
                    n.base != null -> measurer.measureRow(n.base, baseSize).w
                    n.baseStart >= 0 && slot != SlotStyle.NONE -> baseSize * SLOT_W
                    else -> 0f
                }
                val ow = measurer.textWidth("(", pSize, GlyphStyle.UPRIGHT)
                val bodyW = if (slot != SlotStyle.NONE && n.arg.isBlank() && n.argStart >= 0) {
                    max(body.w, sizePx * SLOT_W)
                } else {
                    body.w
                }

                glyphs += Glyph("log", x, baseline, sizePx, GlyphStyle.UPRIGHT)

                // ---- 底数：右下角下标（GeoGebra 的 log 就是这个位置）----
                val baseBaseline = baseline + sizePx * 0.45f
                if (n.base != null) {
                    layoutRow(n.base, x + nameW, baseBaseline, baseSize)
                } else if (n.baseStart >= 0 && slot != SlotStyle.NONE) {
                    val sw = baseSize * SLOT_W
                    val sh = baseSize * SLOT_H
                    val sy = baseBaseline - baseSize * 0.78f
                    addSlot(x + nameW, sy, sw, sh, max(1f, sizePx * 0.045f))
                    stops += Stop(n.baseStart, x + nameW + sw * 0.4f, sy, sy + sh)
                }

                val bodyX = x + nameW + baseW
                glyphs += Glyph("(", bodyX, baseline, pSize, GlyphStyle.UPRIGHT)
                layoutRow(n.arg, bodyX + ow, baseline, sizePx)
                if (slot != SlotStyle.NONE && n.arg.isBlank() && n.argStart >= 0) {
                    val sh = sizePx * SLOT_H
                    val sy = baseline - sizePx * 0.74f
                    addSlot(bodyX + ow, sy, bodyW, sh, max(1f, sizePx * 0.045f))
                    stops += Stop(n.argStart, bodyX + ow + bodyW * 0.4f, sy, sy + sh)
                }
                glyphs += Glyph(")", bodyX + ow + bodyW, baseline, pSize, GlyphStyle.UPRIGHT)
            }
        }
    }
}
