#!/usr/bin/env bash
#
# 在没有 Android SDK 的沙箱/新机器上装一套能构建本项目的工具链。
#
# 装的东西（与 CI 一致）：
#   cmdline-tools 11076708 / platforms;android-34 / build-tools;34.0.0
#   cmake;3.22.1 / ndk;27.2.12479018（r27c）
#   Gradle 8.7 分发包（放到 GRADLE_USER_HOME 的 wrapper/dists 下）
#
# 用法：
#   tools/setup-sandbox-toolchain.sh              # 只装工具链
#   tools/setup-sandbox-toolchain.sh --build      # 装完顺便构建 release + debug
#
# 可用环境变量覆盖：
#   ANDROID_SDK_PATH   工具链落地目录，默认 <仓库根>/.toolchain/android-sdk
#   GRADLE_USER_HOME   Gradle 缓存目录，默认 <仓库根>/.toolchain/gradle-home
#   SDK_MIRROR         下载镜像，默认腾讯云（Google 直连在国内/沙箱常只有几十 KB/s）
#   JAVA_HOME          JDK 17，未设置时尝试用 PATH 里的 java
#
# 注意：Gradle 与 AGP 不读 http_proxy 环境变量，如果出网要走代理，
# 需要在 $GRADLE_USER_HOME/gradle.properties 里写 systemProp.http(s).proxyHost/Port，
# 脚本发现 HTTP_PROXY 时会自动生成这一份配置。

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK=${ANDROID_SDK_PATH:-$REPO_ROOT/.toolchain/android-sdk}
GH=${GRADLE_USER_HOME:-$REPO_ROOT/.toolchain/gradle-home}
MIRROR=${SDK_MIRROR:-https://mirrors.cloud.tencent.com/AndroidSDK}
TMP=${TMPDIR:-/tmp}/fd-sdk-pkg

# 与 Gradle 8.7 的 wrapper 哈希目录保持一致，见 gradle/wrapper/gradle-wrapper.properties
GRADLE_DIST_VER=gradle-8.7
GRADLE_DIST_HASH=bhs2wmbdwecv87pi65oeuq5iu

PKGS=(
  commandlinetools-linux-11076708_latest.zip
  platform-34-ext7_r03.zip
  build-tools_r34-linux.zip
  cmake-3.22.1-linux.zip
  android-ndk-r27c-linux.zip
)

log() { printf '\n>>> %s\n' "$*"; }

mkdir -p "$SDK"/{platforms,build-tools,cmake,ndk} "$GH" "$TMP"

log "下载 SDK 组件（$MIRROR）"
cd "$TMP"
for f in "${PKGS[@]}"; do
  if [ -s "$f" ]; then
    echo "    已有 $f"
  else
    echo "    下载 $f"
    curl -fL --retry 3 -o "$f" "$MIRROR/$f"
  fi
done

log "解包到 $SDK"
[ -d "$SDK/platforms/android-34" ] || unzip -q -o platform-34-ext7_r03.zip -d "$SDK/platforms"

if [ ! -d "$SDK/build-tools/34.0.0" ]; then
  rm -rf "$TMP/bt"
  unzip -q -o build-tools_r34-linux.zip -d "$TMP/bt"
  mv "$TMP/bt/android-14" "$SDK/build-tools/34.0.0" # 压缩包里目录名是 android-14
fi

[ -d "$SDK/cmake/3.22.1" ] || unzip -q -o cmake-3.22.1-linux.zip -d "$SDK/cmake/3.22.1"

if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
  rm -rf "$SDK/cmdline-tools"
  mkdir -p "$SDK/cmdline-tools"
  unzip -q -o commandlinetools-linux-11076708_latest.zip -d "$SDK/cmdline-tools"
  mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
fi

if [ ! -d "$SDK/ndk/27.2.12479018" ]; then
  rm -rf "$TMP/ndk"
  unzip -q -o android-ndk-r27c-linux.zip -d "$TMP/ndk"
  mv "$TMP/ndk/android-ndk-r27c" "$SDK/ndk/27.2.12479018"
  grep -q 'Pkg.Revision = 27.2.12479018' "$SDK/ndk/27.2.12479018/source.properties" \
    || { echo "NDK 版本号不对，检查下载包"; exit 1; }
fi

log "准备 Gradle 分发包"
DIST="$GH/wrapper/dists/$GRADLE_DIST_VER/$GRADLE_DIST_HASH"
if [ ! -f "$DIST/$GRADLE_DIST_VER.zip.ok" ]; then
  mkdir -p "$DIST"
  curl -fL --retry 3 -o "$DIST/$GRADLE_DIST_VER.zip" \
    "https://services.gradle.org/distributions/$GRADLE_DIST_VER.zip"
  unzip -q -t "$DIST/$GRADLE_DIST_VER.zip" >/dev/null # 先校验压缩包完整
  touch "$DIST/$GRADLE_DIST_VER.zip.ok"
fi

# Gradle 只认 systemProp.*，不吃 http_proxy 环境变量
if [ -n "${HTTP_PROXY:-${http_proxy:-}}" ] && [ ! -f "$GH/gradle.properties" ]; then
  log "为 Gradle 生成代理配置"
  proxy=${HTTP_PROXY:-$http_proxy}
  host=$(printf '%s' "$proxy" | sed -E 's#^[a-z]+://##; s#[:/].*$##')
  port=$(printf '%s' "$proxy" | sed -nE 's#^[a-z]+://[^:]+:([0-9]+).*$#\1#p')
  cat > "$GH/gradle.properties" <<EOF
systemProp.http.proxyHost=$host
systemProp.http.proxyPort=${port:-8080}
systemProp.https.proxyHost=$host
systemProp.https.proxyPort=${port:-8080}
systemProp.http.nonProxyHosts=localhost|127.0.0.1
EOF
fi

export ANDROID_HOME="$SDK"
export ANDROID_SDK_ROOT="$SDK"
export GRADLE_USER_HOME="$GH"

cat <<EOF

工具链就绪：
  ANDROID_HOME=$SDK
  GRADLE_USER_HOME=$GH
  NDK=$SDK/ndk/27.2.12479018

构建：
  export ANDROID_HOME=$SDK ANDROID_SDK_ROOT=$SDK GRADLE_USER_HOME=$GH
  sh ./gradlew :app:assembleDebug
  sh ./gradlew :app:assembleRelease -PabiSplits
EOF

if [ "${1:-}" = "--build" ]; then
  log "构建 release（拆 ABI）+ debug"
  cd "$REPO_ROOT"
  sh ./gradlew :app:assembleRelease -PabiSplits
  sh ./gradlew :app:assembleDebug
  ls -la app/build/outputs/apk/release/ app/build/outputs/apk/debug/
fi
