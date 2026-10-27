package com.functiondrawer.watch.engine

/**
 * LaTeX 源码 → 本引擎认识的普通表达式的**翻译层**。
 *
 * ## 为什么要它（2026-10-27）
 *
 * 用户原话："LaTeX语法没有实装。"
 *
 * 之前只在**显示**上做了半个子集（`MathRender` 把 `a/b` 排成分式、`sqrt(x)`
 * 排成根号），但**输入/求值**这一侧一个 LaTeX 命令都不认：写
 * `\frac{1}{2}`、`\sqrt{x}`、`\pi` 只会得到"看不懂这个字符：'\'"，函数文件
 * 里带 LaTeX 的表达式也进不了引擎。这条就是补上输入侧的翻译。
 *
 * ## 翻译规则（只做"能求值"的子集，不做排版）
 *
 * | LaTeX | 翻译成 |
 * |---|---|
 * | `\frac{a}{b}` / `\dfrac` / `\tfrac` | `(a)/(b)` |
 * | `\sqrt{x}` | `sqrt(x)` |
 * | `\sqrt[n]{x}` | `root(x,n)` |
 * | `x^{2}` / `x^{n+1}` | `x^(2)` / `x^(n+1)` |
 * | `\pi` `\tau` `\varphi` | `pi` `tau` `phi` |
 * | `\alpha` `\theta` … | 保留原希腊字母（引擎不认识会给出可读报错） |
 * | `\sin` `\cos` `\ln` `\log` … | `sin` `cos` `ln` `log`（后面照旧可省括号） |
 * | `\cdot` `\times` `\div` | `*` `*` `/` |
 * | `\left(` `\right)` `\bigl[` … | 直接丢掉命令，只留括号 |
 * | `\mathrm{…}` `\text{…}` `\operatorname{…}` | 只留花括号里的内容 |
 * | `\,` `\;` `\!` `\quad` | 空格或直接删掉 |
 * | `\{` `\}` `\%` | `(` `)` `%` |
 *
 * 不认识的命令**静默删掉**：LaTeX 里大量命令（`\displaystyle`、`\limits` …）
 * 只是排版提示，删掉比报"看不懂"更接近用户预期；真正的未知标识符
 * （`\foo`）删掉后也不会有歧义。
 *
 * 没有 `\` 也没有 `{` 的普通表达式**原样返回**，一个字符都不动 ——
 * 所以老表达式的求值和光标下标完全不受影响（见 [expand] 的快速路径）。
 */
internal object Latex {

    /** 花括号包参数时用的展开函数（递归）。 */
    private fun expandRange(src: String, from: Int, to: Int): String {
        val out = StringBuilder(to - from)
        var i = from
        while (i < to) {
            val c = src[i]
            when {
                c == '\\' -> i = command(src, i, to, out)

                // 裸的组：LaTeX 的 `{...}` 只表示分组，本身不产生字符
                c == '{' -> {
                    val end = matchingBrace(src, i, to)
                    out.append(expandRange(src, i + 1, minOf(end, to)))
                    i = end + 1
                }

                // 多余的右括号：丢掉，避免把后面的整段都吞掉
                c == '}' -> i++

                // 上标写成 `^{...}`：引擎只认 `^(...)`
                c == '^' -> {
                    out.append('^')
                    i++
                    if (i < to && src[i] == '{') {
                        val end = matchingBrace(src, i, to)
                        out.append('(')
                        out.append(expandRange(src, i + 1, minOf(end, to)))
                        out.append(')')
                        i = end + 1
                    }
                }

                else -> {
                    out.append(c)
                    i++
                }
            }
        }
        return out.toString()
    }

    /**
     * 读一个参数：`{...}` 取括号里的内容；否则取**一个原子**
     * （单个字符，或一个完整的 `\命令`，比如 `\frac12`、`\sqrt\pi`）。
     *
     * @return 展开后的文本 + 下一个待处理下标
     */
    private fun argument(src: String, at: Int, to: Int): Pair<String, Int> {
        var i = at
        while (i < to && src[i].isWhitespace()) i++
        if (i >= to) return "" to i
        if (src[i] == '{') {
            val end = matchingBrace(src, i, to)
            return expandRange(src, i + 1, minOf(end, to)) to (end + 1)
        }
        if (src[i] == '\\') {
            val sb = StringBuilder()
            val next = command(src, i, to, sb)
            return sb.toString() to next
        }
        return src[i].toString() to (i + 1)
    }

    /** 找到与 [open] 处的 `{` 配对的 `}`（跳过 `\{` / `\}`）；找不到就返回 [to]。 */
    private fun matchingBrace(src: String, open: Int, to: Int): Int {
        var depth = 0
        var i = open
        while (i < to) {
            when (src[i]) {
                '\\' -> i += 2
                '{' -> {
                    depth++
                    i++
                }
                '}' -> {
                    depth--
                    if (depth == 0) return i
                    i++
                }
                else -> i++
            }
        }
        return to
    }

    /** 处理一个 `\命令`，把结果写进 [out]，返回命令之后的下标。 */
    private fun command(src: String, at: Int, to: Int, out: StringBuilder): Int {
        var i = at + 1
        if (i >= to) return i

        // 单字符命令：`\{` `\}` `\%` `\ ` `\,`
        if (!src[i].isLetter()) {
            when (val c = src[i]) {
                '{' -> out.append('(')
                '}' -> out.append(')')
                '%' -> out.append('%')
                ' ', ',', ';', ':', '!', '\\' -> out.append(' ')
                else -> out.append(c)
            }
            return i + 1
        }

        val start = i
        while (i < to && src[i].isLetter()) i++
        val name = src.substring(start, i)

        when (name) {
            // ---- 结构 ----
            "frac", "dfrac", "tfrac" -> {
                val (a, i1) = argument(src, i, to)
                val (b, i2) = argument(src, i1, to)
                out.append('(').append(a).append(")/(").append(b).append(')')
                return i2
            }

            "sqrt" -> {
                // 可选的根指数：[n]
                var j = i
                while (j < to && src[j].isWhitespace()) j++
                var index: String? = null
                if (j < to && src[j] == '[') {
                    val end = src.indexOf(']', j + 1).let { if (it < 0) to else it }
                    index = expandRange(src, j + 1, minOf(end, to))
                    j = end + 1
                }
                val (body, next) = argument(src, j, to)
                if (index.isNullOrBlank()) {
                    out.append("sqrt(").append(body).append(')')
                } else {
                    out.append("root(").append(body).append(',').append(index).append(')')
                }
                return next
            }

            // ---- 只留括号 / 只留内容 ----
            "left", "right", "middle", "big", "Big", "bigg", "Bigg",
            "bigl", "bigr", "Bigl", "Bigr", "biggl", "biggr" -> {
                // `\left.` / `\right.` 是"这一侧不画括号"，连点一起吞掉
                var j = i
                while (j < to && src[j].isWhitespace()) j++
                if (j < to && src[j] == '.') return j + 1
                return i
            }

            "mathrm", "mathbf", "mathit", "mathsf", "mathtt", "text",
            "textbf", "textit", "textrm", "operatorname", "displaystyle",
            "textstyle", "scriptstyle" -> {
                val (body, next) = argument(src, i, to)
                out.append(body)
                return next
            }

            // ---- 运算符号 ----
            "cdot", "times", "ast" -> {
                out.append('*')
                return i
            }
            "div" -> {
                out.append('/')
                return i
            }

            // ---- 空白 ----
            "quad", "qquad", "enspace", "thinspace" -> {
                out.append(' ')
                return i
            }

            // ---- 希腊字母 / 常量 ----
            in GREEK -> {
                out.append(GREEK.getValue(name))
                return i
            }
        }

        // ---- 函数名（照旧可以省括号：`\sin x` → `sin x`）----
        FUNCTIONS[name]?.let { fn ->
            out.append(fn)
            return i
        }

        // 不认识的命令：删掉（见文件头说明）
        return i
    }

    /** 希腊字母 / 常量：能直接进引擎的转成名字，其余保留原字符。 */
    private val GREEK: Map<String, String> = mapOf(
        "pi" to "pi",
        "tau" to "tau",
        "phi" to "phi",
        "varphi" to "phi",
        "epsilon" to "epsilon",
        "varepsilon" to "epsilon",
        "theta" to "theta",
        "alpha" to "alpha",
        "beta" to "beta",
        "gamma" to "gamma",
        "delta" to "delta",
        "lambda" to "lambda",
        "mu" to "mu",
        "sigma" to "sigma",
        "omega" to "omega",
        "infty" to "inf",
        "infinity" to "inf"
    )

    /**
     * LaTeX 函数名 → 引擎函数名。
     *
     * 只收 [MathFns] 真的有的（`\gcd` 这种引擎没有的就别假装支持）。
     */
    private val FUNCTIONS: Map<String, String> = mapOf(
        "sin" to "sin",
        "cos" to "cos",
        "tan" to "tan",
        "cot" to "cot",
        "sec" to "sec",
        "csc" to "csc",
        "arcsin" to "asin",
        "arccos" to "acos",
        "arctan" to "atan",
        "sinh" to "sinh",
        "cosh" to "cosh",
        "tanh" to "tanh",
        "coth" to "coth",
        "ln" to "ln",
        "log" to "log",
        "lg" to "lg",
        "exp" to "exp",
        "abs" to "abs",
        "min" to "min",
        "max" to "max",
        "floor" to "floor",
        "ceil" to "ceil",
        "round" to "round"
    )

    /**
     * 把 LaTeX 源码翻译成普通表达式。
     *
     * 不含 `\` 也不含 `{` 时**原样返回**：这是热路径上的快速判断，
     * 也保证老表达式的每个字符下标（光标、报错位置）一点没变。
     */
    fun expand(source: String): String {
        val hasCommand = source.indexOf('\\') >= 0
        val hasGroup = source.indexOf('{') >= 0
        if (!hasCommand && !hasGroup) return source
        return expandRange(source, 0, source.length)
    }
}
