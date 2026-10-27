// 注意：在 Kotlin DSL 里 `java` 会解析成 Gradle 的 JavaPluginExtension，
// 而不是 java 包，所以 Properties 必须显式 import。
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

// 可选：构建时加 `-PabiSplits`，按 ABI 拆成三个包并额外产出 universal 包。
val abiSplits = providers.gradleProperty("abiSplits").isPresent

android {
    namespace = "com.functiondrawer.watch"

    // 只能用本机已装的 android-34（SDK 管理器不能联网补装 35/36）。
    compileSdk = 34

    // LVGL 是 C 库，界面全部在 NDK 侧；这里锁定已验证过的 NDK/CMake 版本。
    ndkVersion = "27.2.12479018"

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    defaultConfig {
        applicationId = "com.functiondrawer.watch"
        // 目标设备是 Android 11 的原屏手表，但用户要求**支持范围放大到 Android 7.0**
        // （API 24）—— 国产原屏表里跑 7.x/8.x 的机型不少。
        //
        // 降下来会牵出"只有高版本才有"的 API，逐个都做了处理（改这一行时一起查过一遍）：
        //   * `View.setSystemGestureExclusionRects`（API 29）→ GraphScreen 里判版本，
        //     低版本不做（这个能力 7.x 上没有等价替代），设置页那一行也会拦下并提示；
        //   * `MotionEvent.AXIS_SCROLL`（API 26）→ MainActivity 里 26 以下不列这个轴；
        //   * 沉浸式全屏本来就用的是 AndroidX 的 `WindowCompat`/`WindowInsetsControllerCompat`
        //     （兼容封装，API 30 以下自动退化成 `systemUiVisibility`），**没有**直接调
        //     平台那个 API 30 的 `setDecorFitsSystemWindows`；
        //   * `LocalConfiguration.isScreenRound`（API 23）、`Bitmap.compress`、
        //     `getExternalFilesDir`、`Uri.encode/decode`、`String.format` 等都在 24 以下就有。
        // lint 的 `abortOnError = false` 保持不变（低版本分支只靠上面的显式守卫，不靠 lint）。
        // 注意：改动后**尚未**跑 Gradle 构建验证（构建要联网、且会写工作区外目录）。
        minSdk = 24
        targetSdk = 34
        // ---- 版本号（用户定的规则，2026-10-04）----
        // 用户原话："每次我说要上传你再更新 versionCode，不然中间太多小版本了，版本号跳跃很快。"
        // 所以**不要**做"每次构建自增"，也不要在小改动里动这两个数：
        //   * 平时改代码 → 保持原样，反复编译出来的包版本号都一样（方便对照、也方便覆盖安装）；
        //   * 用户说"要上传了"（或点名要改号）→ 才提一档并把 versionCode **加一**
        //     （第三方商店要求每次上传的 versionCode 必须比上一版大）。
        // 历史：1.0 / 1 → **1.0fix / 2**（用户 2026-10-05 点名："把安装包版本号升到 1.0fix，
        // versioncode 变成 2"）→ **1.1 / 3**（用户 2026-10-05 点名："签名完之后再安装、
        // 升版本号和 versioncode"，选项里选的是 `1.1 / 3`）。
        versionCode = 3
        versionName = "1.1"

        externalNativeBuild {
            cmake {
                // LVGL 只需要 C11；不链 C++ 运行时，包更小。
                arguments += "-DANDROID_STL=none"
            }
        }

        ndk {
            // 目标手表是 32 位；带上 arm64 以便在新设备/模拟器上也能跑。
            abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86_64")
        }
    }

    if (abiSplits) {
        splits {
            abi {
                isEnable = true
                reset()
                include("x86_64", "armeabi-v7a", "arm64-v8a")
                isUniversalApk = true
            }
        }
    }

    // release 签名：密钥路径与口令放在项目根目录的 keystore.properties 里
    // （该文件已被 .gitignore 忽略，不会进版本库）。
    // 文件不存在时也能正常构建，只是产出的 release 包未签名。
    val keystorePropsFile = rootProject.file("keystore.properties")
    val keystoreProps = Properties().apply {
        if (keystorePropsFile.exists()) keystorePropsFile.inputStream().use { load(it) }
    }
    signingConfigs {
        if (keystorePropsFile.exists()) {
            create("release") {
                storeFile = rootProject.file(keystoreProps.getProperty("storeFile"))
                storePassword = keystoreProps.getProperty("storePassword")
                keyAlias = keystoreProps.getProperty("keyAlias")
                keyPassword = keystoreProps.getProperty("keyPassword")

                // ---- 签名方案：v1 + v2 + v3 都要（用户报"安卓 7 装不了"）----
                // 现象：`apksigner verify` 显示 v1=false / v2=true / v3=false —— 只有 v2。
                // 为什么只在 Android 7 上出问题：Android 7.0（API 24）才开始支持 v2 方案，
                // 而**老的安装器/国产 ROM 的包管理器很多会先去验 JAR 签名（v1）**，
                // v1 缺失时就报"解析软件包时出现问题 / 应用未安装"。
                // v3 是 Android 9 起的分级签名（key rotation 用），加上它不花钱、兼容性只增不减。
                // 结论：三个方案同时开启 —— 覆盖 API 24 及以上的所有设备与各家商店的要求。
                enableV1Signing = true
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        release {
            // R8 对 Compose 是安全的（不需要额外 keep 规则），
            // 能把 release 包从 ~19MB 压到 1.58MB —— 手表存储紧张，这很重要。
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            if (keystorePropsFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
        debug {
            // 调试包不混淆，方便反复装到表上试。
            isMinifyEnabled = false
            // **用和 release 同一套签名**：debug 默认签名和 release 不一样，
            // 直接 `adb install -r` 会因为签名不匹配失败，只能先卸载 ——
            // 那会把表上已经存好的函数和设置一起清掉。
            // 签成同一个 key 之后 debug 包可以直接覆盖安装，数据保留。
            // （debug 包不需要 R8，构建要快得多，所以日常迭代只装 debug。）
            if (keystorePropsFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        // 界面全部由 C + LVGL 绘制，没有任何 Compose / View 布局。
        buildConfig = false
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    // MainActivity 只用到 WindowCompat / WindowInsetsControllerCompat，其余都在 C 侧。
    implementation(libs.androidx.core.ktx)
}
