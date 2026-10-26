# FunctionDrawer 的 R8 规则。
#
# 界面全在 C + LVGL 侧，Java 只剩一个 MainActivity 作为 SurfaceView / 输入的宿主，
# 所以规则很少。
#
# native 方法名不能混淆：JNI 按 Java_com_functiondrawer_watch_MainActivity_<方法名>
# 静态查找符号，名字一变 C 侧就找不到入口（真机上表现为 UnsatisfiedLinkError）。
# AGP 默认规则里已经包含这一条，这里显式写出来，避免以后有人删掉默认规则时踩坑。
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

-keepattributes SourceFile,LineNumberTable
