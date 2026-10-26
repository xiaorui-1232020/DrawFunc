# FunctionDrawer · 手表函数图像绘制器（LVGL 版）

给 **Android 11「原屏」手表**（跑完整 Android，不是 Wear OS）写的开源 DrawFunc App。
界面不再是 Jetpack Compose，而是**整屏由 C + LVGL 绘制**，Android 侧只剩一个
`SurfaceView` 宿主。数学引擎、文档模型、界面、绘制全部在 NDK 里。

当前代码在 **`lvgl` 分支**（`main` 保留旧的 Compose 实现）。

```
y = f(x)       显函数
r = f(θ)       极坐标
(x(t), y(t))   参数方程
F(x, y) = 0    隐函数（marching squares）
```

---

## 一、当前状态

| 产物 | 大小 | 说明 |
|---|---|---|
| `app/build/outputs/apk/debug/app-debug.apk` | **9,619,159 B（≈9.2 MiB）** | 含两个 ABI 的 `libfunctiondrawer.so` |
| `app/build/outputs/apk/release/app-release.apk` | **4,475,883 B（≈4.3 MiB）** | 已配置 release 签名，v1 + v2 + v3 三签（见第四节） |

已验证：

- `./gradlew :app:assembleDebug :app:assembleRelease` → **BUILD SUCCESSFUL**；
- 表达式引擎主机测试 **661 项，0 失败**；
- 特征点分析主机测试 **40 项，0 失败**（另跑过 ASan/UBSan）；
- 界面冒烟测试：**7 个屏幕全部渲染成功，导出 17 张 PNG**，几何作图 / 存档往返 / 字母键盘路径均走通。

**尚未在真表上运行过**（本次开发环境无 `/dev/kvm`、无 adb）。界面与交互已在主机端
用内存显示 + 合成触摸事件验证，但表冠、真实触摸采样率、GPU/内存压力仍需真机确认。

版本号：`versionName 1.1` / `versionCode 3`，`minSdk 24` / `targetSdk 34` / `compileSdk 34`。

---

## 二、架构：SurfaceView + ANativeWindow

LVGL 官方没有 Android 移植仓库（只提供 SDL / Linux framebuffer 等 PC 驱动），
所以这套显示与输入驱动是**自己写的**，约 300 行 C：

```
MainActivity.kt（唯一 Kotlin 文件）
        │  surfaceCreated / surfaceChanged / surfaceDestroyed
        │  touch / crown / key
        ▼
platform_android.c
  ├─ ANativeWindow_fromSurface → ANativeWindow_setBuffersGeometry(RGBA_8888)
  ├─ 渲染线程：lv_timer_handler() 每 4ms 一次，独占 LVGL
  ├─ flush_cb：ANativeWindow_lock → 逐行做 BGR↔RGB 交换 → unlockAndPost
  ├─ 双队列输入：
  │     g_raw_q → app_touch()  → 绘图手势（平移/捏合/双击/长按/工具点选）
  │     g_lv_q  → LVGL indev read_cb → 按钮、列表、键盘
  └─ 所有 JNI 入口先拿 g_lock，UI 线程与渲染线程不会互相踩
```

几个关键决定：

- **渲染线程持有 LVGL 的唯一所有权**。LVGL 不是线程安全的，而 Android 的
  `surfaceCreated` 在 UI 线程回调，所以显示创建、`lv_timer_handler`、
  `lv_obj_clean` 全部收敛到渲染线程；JNI 只负责把事件塞进队列或加锁改状态。
- **`LV_DISPLAY_RENDER_MODE_FULL` + `LV_COLOR_FORMAT_ARGB8888`**。整屏一块
  缓冲区，flush 时整块拷进 `ANativeWindow_Buffer`。LVGL 的 ARGB8888 在内存里是
  `B,G,R,A`，Android 要 `R,G,B,A`，交换在拷贝循环里顺手做掉。
- **触摸事件双投递**。同一份事件既给绘图手势队列，也给 LVGL indev 队列：
  绘图屏需要原始坐标做手势识别，其它屏幕需要 LVGL 的点击/长按语义。
- **表冠不做 action/source 过滤**。`MainActivity.dispatchGenericMotionEvent`
  逐个滚轮轴（`AXIS_SCROLL` / `VSCROLL` / `HSCROLL` / `WHEEL`）取值，
  取第一个非零量。原屏手表的表冠上报方式不统一（有的只有 `REL_WHEEL`），
  逐个试才不会永远拿到 0。
- **没有 Compose、没有 Android UI 组件**。`app/build.gradle.kts` 里
  `buildFeatures { buildConfig = false }`，依赖只剩 `androidx.core.ktx`。
  R8 规则只保留 `native` 方法名（JNI 静态查找需要）。

---

## 三、目录结构

```
app/src/main/
├── java/com/functiondrawer/watch/
│   └── MainActivity.kt             唯一的 Kotlin 文件：SurfaceView 宿主 + 输入转发
└── cpp/
    ├── CMakeLists.txt              LVGL 静态库 + libfunctiondrawer.so
    ├── lv_conf.h                   LVGL 9.2.2 配置（32 位色、8MB 堆、lodepng、stdio FS）
    ├── engine/                     ★ 纯 C11 数学引擎，零 Android 依赖（4138 行）
    │   ├── expr.c / expr.h         词法 + 递归下降/优先级爬升 + 求值 + 中文报错
    │   ├── mathfns.c/.h            函数表：三角/双曲/Γ/erf/阶乘/取整…
    │   ├── viewport.c/.h           世界↔屏幕映射、缩放焦点保持、网格步长
    │   ├── plot.c/.h               四种采样算法 + 断线 + 屏幕像素折线
    │   └── fd_features.c/.h        零点/极值/截距分析（有主机测试，见「已知差异」）
    ├── src/                        应用层与平台层（1583 行）
    │   ├── app.c/.h                屏幕尺寸、触摸/表冠/按键入口
    │   ├── model.c/.h              文档模型、撤销栈、文本存档、几何对象序列化
    │   ├── geo.c/.h                几何工具数学（距离/面积/角/反射/单位方向/垂直）
    │   └── platform_android.c      JNI + ANativeWindow 显示/输入驱动
    ├── ui/                         LVGL 界面（不含字体 3979 行）
    │   ├── ui.c/.h                 屏幕路由、主题色、通用控件、toast
    │   ├── ui_graph.c              绘图屏：网格/曲线/手势/信息卡/工具面板
    │   ├── ui_list.c               函数列表、参数滑动条、播放
    │   ├── ui_editor.c             编辑器 + 四页自绘键盘 + 字母表
    │   ├── ui_props.c              颜色/线宽/线型/显隐/角度 + 撤销/重做/删除
    │   ├── ui_archive.c            存档列表、载入、删除、导出 PNG
    │   ├── ui_settings.c           网格/坐标轴/刻度/角度/表冠/采样质量
    │   ├── ui_about.c              关于页
    │   └── font_cn_14/16/20.c      中文字库（各约 2.6/3.0/4.0 MB 源码）
    ├── tests/                      主机端测试（1628 行）
    │   ├── expr_test.c             + build_expr.sh
    │   ├── fd_features_test.c      + build_fd_features.sh
    │   └── ui_smoke.c              + build_ui_host.sh（内存显示 + 导出 PNG）
    └── third_party/lvgl/           LVGL 9.2.2（25 MB，已删 .git/docs/demos/examples/tests）
```

---

## 四、构建

### 前置

| 组件 | 版本 / 位置 | 说明 |
|---|---|---|
| JDK | 17 及以上 | AGP 要求 17+；本沙箱默认的 JDK 25 已验证可构建 |
| Android SDK | `platforms;android-34`、`build-tools;34.0.0` | `compileSdk 34` |
| NDK | `27.2.12479018` | `app/build.gradle.kts` 里锁定 |
| CMake | `3.22.1` | 同上 |
| Gradle | 8.7（wrapper） | |

本开发环境把 SDK 与 Gradle 缓存放在 `/workspace/.toolchain`（该目录不在版本库里）：

```bash
export ANDROID_HOME=/workspace/.toolchain/android-sdk
export GRADLE_USER_HOME=/workspace/.toolchain/gradle-home

sh ./gradlew :app:assembleDebug
sh ./gradlew :app:assembleRelease
```

如果你本机的 SDK / JDK 在别处，改环境变量或在根目录 `local.properties` 里写
`sdk.dir=...` 即可。**不要**假设 `~/.gradle` 里有可用的 Gradle 发行版——
wrapper 会重新下载 130 MB 的 `gradle-8.7-bin.zip`。

`app/build.gradle.kts` 里的几个硬约定：

- `abiFilters = armeabi-v7a, arm64-v8a`：目标手表是 32 位 ARM，带上 arm64 便于新设备；
- `arguments += "-DANDROID_STL=none"`：全是 C，不链 C++ 运行时；
- `externalNativeBuild.cmake.version = "3.22.1"`，`ndkVersion = "27.2.12479018"`；
- `keystore.properties` 存在时，debug 与 release **共用同一套 release 签名**
  （签名一致才能 `adb install -r` 覆盖安装，不会清掉表上已有的函数和设置）；
- release 打开 R8 + 资源压缩，体积从 ~19 MB 降到 ~4.4 MB。

### 签名

签名材料放在仓库根目录，**都不入库**（`.gitignore` 已排除）：

```
keystore.properties              # 口令与路径
keystore/functiondrawer-release.jks   # PKCS12，RSA 2048，有效期 10000 天
```

`keystore.properties` 格式：

```properties
storeFile=keystore/functiondrawer-release.jks
storePassword=<口令>
keyAlias=functiondrawer
keyPassword=<口令>
```

重新生成一套（换机或丢失时）：

```bash
keytool -genkeypair -keystore keystore/functiondrawer-release.jks -alias functiondrawer \
  -keyalg RSA -keysize 2048 -validity 10000 -storetype PKCS12 \
  -dname "CN=FunctionDrawer, OU=Watch, O=FunctionDrawer, L=Shanghai, ST=Shanghai, C=CN"
```

**v1 签名的坑**：AGP 会忽略 `enableV1Signing = true`，`assembleRelease` 出来的包只有
v2 + v3。Android 7 那批只认 JAR 签名的老安装器会装不上，所以构建后再跑一次
`tools/sign-release.ps1` 用 apksigner 补成三签（本仓库的发布包就是这样做的）。
校验时必须显式加 `--min-sdk-version 21`，否则 apksigner 只检 minSdk 需要的档位，
会把明明存在的 v1 报成 `false`。

### 安装到表上

`tools/` 下保留了三个 Windows 脚本（`install-to-watch.ps1`、`verify-apk.ps1`、
`sign-release.ps1`），用法与旧版一致：优先装 release 包，手表存储紧张。

```powershell
powershell -ExecutionPolicy Bypass -File tools\install-to-watch.ps1
```

---

## 五、主机端验证（不需要 Gradle、不需要模拟器）

三个脚本都是普通 C11 程序，Linux / macOS 上直接跑：

```bash
# 1) 表达式引擎：661 项断言
bash app/src/main/cpp/tests/build_expr.sh

# 2) 特征点分析：40 项断言（零点/极值/截距/相切零点）
bash app/src/main/cpp/tests/build_fd_features.sh

# 3) 界面冒烟：内存显示 + 合成触摸，逐屏渲染并导出 PNG
bash app/src/main/cpp/tests/build_ui_host.sh /tmp/fd_ui_shots
```

三个脚本都支持：

- `SANITIZE=1` 开启 ASan + UBSan；
- `OUT_DIR=...` 指定中间产物目录；
- `CC=clang` / `gcc` 换编译器。

界面冒烟测试做的事：

1. 用 `lv_display_create` 建一块内存显示，flush 时直接写 PNG（lodepng），
   不接任何 Android API；
2. 用合成 indev 把点击送给 LVGL，同时把同一份事件喂给绘图手势；
3. 依次走：绘图屏 → 列表 → 编辑器（四页键盘 + 字母页大小写切换）→
   属性屏 → 存档页 → 设置 → 关于；
4. 打开工具面板、用「线段」造对象、再造 10 个几何对象，
   存档 → 清空 → 读回，核对对象数量与画面；
5. 导出 PNG 并校验文件存在。

产物（`/tmp/fd_ui_shots/`）：`01_graph` … `17_editor_upper`，共 17 张截图，
外加 `data/archives/smoke.png` 与 `data/autosave.fd`。

---

## 六、中文字体

LVGL 自带字体没有中文，所以用 `lv_font_conv` 从 Noto Sans CJK SC 生成三档点阵：

```bash
node node_modules/lv_font_conv/lv_font_conv.js \
  --font NotoSansCJKsc-Regular.otf \
  --size 16 --bpp 4 --format lvgl \
  -r 0x20-0x7F \
  -r 0x3000-0x303F -r 0xFF00-0xFFEF -r 0x2000-0x206F \
  -r 0x2190-0x21FF -r 0x2200-0x22FF -r 0x2300-0x23FF \
  --symbols "<GB2312 一级字库 3755 字>" \
  -o app/src/main/cpp/ui/font_cn_16.c \
  --lv-include lvgl.h
```

区间含义：ASCII、全角标点、全角字符、通用标点、箭头、数学运算符、杂项技术符号。
`--symbols` 直接列出 3755 个一级汉字（生成文件的头部注释里保存了完整参数）。

三档字号：`font_cn_14` / `font_cn_16` / `font_cn_20`，均为 `4bpp`，
`lv_font_conv` 默认开 **RLE 压缩**，所以 `lv_conf.h` 里必须
`LV_USE_FONT_COMPRESSED 1`，否则字显示成乱码。

覆盖范围是 **GB2312 一级字库（3755 字）+ ASCII + 常用全角标点/数学符号**。
二级字库和不常用的生僻字不在里面，界面文案不要用。

---

## 七、界面与操作

共 7 个屏幕，全部由 `ui.c` 路由，切屏时 `lv_obj_clean` 重建（绘图屏先调
`ui_graph_detach()` 把控件指针置空，但保留像素缓冲区）。

| 屏幕 | 内容 |
|---|---|
| 绘图 | 网格/坐标轴/刻度、曲线、几何对象、工具提示条、底部信息卡、顶部按钮条 |
| 函数列表 | 曲线行（名字 + 解析式摘要、显/隐、删除，长按进属性）、参数滑动条、播放键、新建、存档/设置/关于入口 |
| 编辑器 | 四种模式切换、表达式输入框（参数方程两个）、四页自绘键盘、错误提示、完成 |
| 属性 | 颜色（12 色）、线宽（1–4 px）、线型（实线/虚线/点线）、显隐、角度模式、撤销/重做/删除 |
| 存档 | 「保存当前」「导出 PNG」、存档列表（载入/删） |
| 设置 | 显示网格、坐标轴、刻度数值、三角函数按角度、表冠缩放、采样质量（快/中/高）、复位视图、保存 |
| 关于 | 版本、许可、绘图模式说明 |

### 绘图屏手势

| 手势 | 效果 |
|---|---|
| 单指拖动 | 平移（曲线与网格用同一个视口重画） |
| 双指捏合 | 以两指中点为中心缩放 |
| 双击画布 | 复位视野到 `x ∈ [-10, 10]` |
| 单击曲线 | 选中该曲线，底部信息卡显示名字 / 解析式 / 错误 |
| 长按（>700ms）空白/对象 | 删除附近的作图对象 |
| 旋转表冠 | 默认缩放（设置里可改成上下平移） |
| 系统返回键 | 非绘图屏返回上一屏；绘图屏不消费（交给系统） |

顶部 56px 按钮条、工具提示条（96px）、底部信息卡（92px）不参与绘图手势，
避免点按钮时把画布拖走。

### 几何工具（16 个，5 个分区）

| 分区 | 工具 |
|---|---|
| 基本工具 | 移动、描点 |
| 度量 | 距离、面积、角度 |
| 变换 | 轴对称、中心对称 |
| 作图 | 中点、垂线、中垂线、平行线、角平分线、切线 |
| 直线 | 线段、直线、射线 |

工具面板是全屏浮层，点「工具」按钮展开。每个工具有固定的选取步骤
（提示条实时显示「下一步选什么」）；面积一直点到点回第一个点才收尾。
**没选到就不退出**：轴对称/中心对称/垂线/平行线/切线在选中合法对象前保持当前工具。

直线与射线存的是「基点 + 单位方向」，渲染时按视口做参数裁剪，
所以缩放/平移后不会「画到屏幕外就断掉」。

### 键盘

- 4 个页签：`数字` / `函数` / `符号` / `字母`；
- 前三页固定 **5 列 × 4 行**：数字页是数字/运算符/`(` `)`/`退`，
  函数页是 `sin cos tan √ ln log exp …`，符号页是括号、比较符、`!` `|`、`θ` 和 `清`；
- 字母页是 **6 列 × 6 行**：第 1 格「返回」，`a`~`z` 顺序排列，
  最后两排是「大小写切换 + `退` `(` `)` `,`」；
- 大小写三态：**小写 → 大写 → 锁定 → 小写**。打一个字母不自动回落，
  和旧版 `AlphabetModel` 的语义一致；
- 删字符用 `退`（数字页与字母页都有），整行清空用符号页的 `清`；
- 函数键自动补左括号（`sin` → `sin(`），少按一次。

### 参数与滑动条

表达式里出现「既不是函数名、也不是采样变量」的小写字母（`a`~`d`），
确定时自动生成 `-5 ~ 5`、步长 `0.1` 的滑动条。参数值在
`fd_expr_compile` 时**折进语法树常量**，所以改值要重新采样一次。
滑动条左边的播放键回到绘图屏做往返播放（播放期间用 `FD_QUALITY_FAST` 降精度）。

### 持久化与导出

文本格式，首行 `FD1`，字段用制表符分隔并转义：

```
<filesDir>/autosave.fd                  自动保存（编辑器「完成」/ 设置页「保存」/ 存档页写）
<filesDir>/archives/<标题>.fd           存档
<filesDir>/archives/<标题>.png          导出的图像
```

- 写文件走「临时文件 + `rename`」，写到一半断电不会毁掉旧存档；
- 保存内容：设置、视口、曲线（模式/表达式/颜色/线宽/线型/角度）、
  参数滑动条、几何对象、选中项；
- 读档时**保留当前屏幕尺寸**，只恢复存档里的视口
  （文件里不存分辨率，否则换设备/换方向会把平移缩放重置）；
- PNG 由 lodepng 编码，走 LVGL 的 `lv_fs` stdio 驱动（盘符 `'A'`），
  路径写成 `"A:/abs/path.png"`。

---

## 八、表达式语法

支持省略乘号、右结合幂、绝对值、阶乘、Unicode 数学符号：

```
2x            隐式乘法
3sin(x)       = 3·sin(x)
(x+1)(x-1)    = x² − 1
2^3^2         512        （幂右结合）
-2^2          −4         （一元负号优先级低于幂）
|x|           绝对值
5!            阶乘（非整数走 Γ(n+1)）
2×3  6÷2      手写符号也能吃
```

函数：`sin cos tan cot sec csc`、`asin acos atan atan2`、`sinh cosh tanh coth`、
`asinh acosh atanh`、`exp ln lg lb log`、`sqrt cbrt root`、`abs sign`、
`floor ceil round trunc frac`、`gamma erf sinc`、`pow hypot mod min max clamp`、`deg rad`

常量：`pi π e tau τ phi φ inf ∞ nan`

约定：`log(x)` 以 10 为底（与 Desmos 一致），`log(b,x)` 以 b 为底，`ln` 为自然对数。
写错变量名会明确报错（中文说明 + 字符下标），不会悄悄当成 0。

隐函数额外支持最外层 `左边=右边`，编译时移项成 `(左边)-(右边)`。

---

## 九、关键实现

**1. 引擎与界面彻底分离。**
`engine/` 是纯 C11 + libm，零 Android 依赖，能在主机上单独编译测试。
采样输出的是**屏幕像素折线**，界面层只负责把像素写进画布。

**2. 曲线不用 LVGL 的矢量控件。**
`lv_canvas` 的 ARGB8888 缓冲区直接写像素：Bresenham 画线，
虚线/点线的相位按**像素累计长度**推进（不是按线段），所以缩放后虚线间隔不会跳。
这样一条几万点的隐函数曲线也只是一次缓冲区写入，不产生成千上万个 LVGL 对象。

**3. 四种模式用三套采样算法。**
- 显函数：沿屏幕每一列取一个样本；相邻样本跳变超过 2 倍屏高就断线，
  否则 `1/x`、`tan(x)` 会被画成假竖线；
- 极坐标/参数方程：先在 θ/t 上均匀取样，再对「屏幕上看起来不够直」的区间
  递归二分（按中点离弦的距离判断）；
- 隐函数：网格 + marching squares，对产生线段的单元递归细分，
  细分时**重新求值**而不是线性插值。

**4. 参数在编译期折进表达式。**
`fd_expr_compile` 拿到 `a/b/c/d` 的当前值后直接替换成常量节点，
`DEG` 模式也在编译期把三角函数的参数乘上 `π/180`。
求值热路径里没有「查参数表」这一步。

**5. 几何对象存「不依赖视口」的世界坐标。**
点、线段、多边形存世界坐标；直线/射线存基点 + 单位方向。
绘制时才按当前视口裁剪，所以缩放、平移不会让对象变形或漂移。

**6. 撤销是整份文档快照。**
`FD_UNDO_DEPTH = 24`，`model_checkpoint()` 每次修改后把整个 `fd_doc`
（曲线、滑动条、几何对象、设置、视口）压栈，`model_undo/redo` 整体换回。
文档很小（几十 KB），快照最简单也最不容易漏字段。

---

## 十、已知限制与旧 Compose 版的差异

| 项 | 说明 |
|---|---|
| **复平面模式未移植** | 旧版有一个「设置 → 坐标系 → 复平面」模式（`ComplexMath.kt` 1056 行：复数类型 + 复数表达式求值，复平面里只有描点和数值运算，结果是画成一个点）。C/LVGL 侧**没有**对应实现。这是本次改造唯一整块缺失的功能，后续需要时再补。 |
| **信息卡没接零点/顶点** | `fd_features.c` 已把旧版 `FeatureFinder` 移植过来并有 40 项测试，但绘图屏的底部信息卡目前只显示**名字 / 解析式 / 错误**，还没有把零点、极值、截距渲染上去。 |
| 未在真表验证 | 显示驱动、表冠、触摸采样率都只在主机端用内存显示 + 合成事件验证过。 |
| 隐函数精度 | 靠网格发现零点，比网格还细的特征会漏。设置里提高采样质量可缓解，代价是求值次数上升。 |
| 中文字库是子集 | 只有 GB2312 一级字库 + ASCII + 常用符号，生僻字显示为空。 |
| 界面尺寸按 466×466 设计 | 大量控件用固定像素（键 62px、行高 54px、面板 12px 边距）。LVGL 的 flex 布局会让它在其它尺寸上仍可用，但没有做圆屏弧宽自适应，也没针对手机比例专门排版。 |
| 参数方程 t 范围 | 编辑器里是预设区间，不能任意输入。 |
| 没有表格/交点求解/导数/积分 | 不在当前范围内。 |

> 旧 Compose 版的 `ui/`、`engine/` Kotlin 源码与 15 个 Kotlin 单测已从 `lvgl` 分支删除，
> 需要对照时看 `main` 分支。

---

## 十一、许可证

MIT（见 `LICENSE`）。如需改为 GPL-3.0 / Apache-2.0 请告知。

第三方：LVGL 9.2.2（MIT）、lodepng（zlib）、Noto Sans CJK SC（SIL OFL 1.1）。
