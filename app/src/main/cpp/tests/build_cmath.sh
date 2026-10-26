#!/usr/bin/env bash
#
# 编译并运行复数表达式求值的 C11 测试（复平面模式）。
#
# 用法：bash app/src/main/cpp/tests/build_cmath.sh
# 可选环境变量：
#   CC        编译器（默认 cc）
#   SANITIZE  设为 1 时额外开启 ASan/UBSan
#   OUT_DIR   可执行文件输出目录（默认 /tmp/fd_cmath_test）

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CPP_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
CC=${CC:-cc}
OUT_DIR=${OUT_DIR:-/tmp/fd_cmath_test}
BIN="${OUT_DIR}/cmath_test"

CFLAGS=(-std=gnu11 -O2 -Wall -Wextra
        -I "${CPP_DIR}/engine" -I "${CPP_DIR}/src" -I "${SCRIPT_DIR}")
if [ "${SANITIZE:-0}" = "1" ]; then
    CFLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

mkdir -p "${OUT_DIR}"

set -e
"${CC}" "${CFLAGS[@]}" \
    "${CPP_DIR}/engine/cmath.c" \
    "${SCRIPT_DIR}/cmath_test.c" \
    -o "${BIN}" \
    -lm

echo "built ${BIN}"
set +e
"${BIN}"
status=$?
if [ ${status} -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "TESTS FAILED (exit ${status})" >&2
fi
exit ${status}
