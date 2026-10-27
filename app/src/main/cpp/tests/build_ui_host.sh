#!/usr/bin/env bash
#
# 主机端 UI 冒烟测试：把 LVGL + 引擎 + 界面编成普通可执行文件，
# 用内存显示把每个屏幕渲染出来并导出 PNG。
#
# 用法：bash app/src/main/cpp/tests/build_ui_host.sh [输出目录]
# 可选环境变量：
#   CC        编译器（默认 cc）
#   SANITIZE  设为 1 时开启 ASan/UBSan（强烈建议跑一遍）
#   OUT_DIR   可执行文件和中间产物目录（默认 /tmp/fd_ui_build）

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CPP_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
LVGL_DIR="${CPP_DIR}/third_party/lvgl"
CC=${CC:-cc}
OUT_DIR=${OUT_DIR:-/tmp/fd_ui_build}
OBJ="${OUT_DIR}/obj"
BIN="${OUT_DIR}/ui_smoke"
SHOT_DIR=${1:-/tmp/fd_ui}

INC=(-I "${CPP_DIR}" -I "${LVGL_DIR}" -I "${CPP_DIR}/engine" \
     -I "${CPP_DIR}/src" -I "${CPP_DIR}/ui" -I "${CPP_DIR}/tests")

CFLAGS=(-std=gnu11 -O2 -DLV_CONF_INCLUDE_SIMPLE)
if [ "${SANITIZE:-0}" = "1" ]; then
    CFLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

mkdir -p "${OBJ}" "${SHOT_DIR}"

shopt -s globstar nullglob
LV_SOURCES=("${LVGL_DIR}/src/"**/*.c)
APP_SOURCES=("${CPP_DIR}/engine/"*.c "${CPP_DIR}/src/model.c" \
             "${CPP_DIR}/src/geo.c" "${CPP_DIR}/src/subst.c" \
             "${CPP_DIR}/src/app.c" "${CPP_DIR}/ui/"*.c "${SCRIPT_DIR}/platform_stub.c" \
             "${SCRIPT_DIR}/ui_smoke.c")

set -e

compile_one() {
    local src=$1
    local obj="${OBJ}/$(echo "${src}" | sed 's|/|_|g').o"
    if [ -f "${obj}" ] && [ "${obj}" -nt "${src}" ] && [ "${obj}" -nt "${CPP_DIR}/lv_conf.h" ]; then
        return
    fi
    if [[ "${src}" == *"/third_party/lvgl/"* ]]; then
        "${CC}" "${CFLAGS[@]}" -w "${INC[@]}" -c "${src}" -o "${obj}"
    else
        "${CC}" "${CFLAGS[@]}" -Wall -Wextra -Wno-unused-parameter \
            "${INC[@]}" -c "${src}" -o "${obj}"
    fi
}

for src in "${LV_SOURCES[@]}" "${APP_SOURCES[@]}"; do
    compile_one "${src}"
done

OBJS=()
for src in "${LV_SOURCES[@]}" "${APP_SOURCES[@]}"; do
    OBJS+=("${OBJ}/$(echo "${src}" | sed 's|/|_|g').o")
done

"${CC}" "${CFLAGS[@]}" "${OBJS[@]}" -o "${BIN}" -lm

echo "built ${BIN}"
set +e
"${BIN}" "${SHOT_DIR}"
status=$?
if [ ${status} -ne 0 ]; then
    echo "UI SMOKE FAILED (exit ${status})"
    exit ${status}
fi
echo "UI SMOKE PASSED"
