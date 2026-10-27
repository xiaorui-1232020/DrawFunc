#!/usr/bin/env bash
#
# 编译并运行特征点分析（零点/极值/截距）的 C11 测试。
#
# 用法：bash app/src/main/cpp/tests/build_features.sh
# 可选环境变量：
#   CC        编译器（默认 gcc）
#   SANITIZE  设为 1 时额外开启 ASan/UBSan
#   OUT_DIR   可执行文件输出目录（默认 /tmp/fd_features_test）

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CPP_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
ENGINE_DIR="${CPP_DIR}/engine"
CC=${CC:-gcc}
OUT_DIR=${OUT_DIR:-/tmp/fd_features_test}
BIN="${OUT_DIR}/features_test"

CFLAGS=(-std=c11 -O2 -Wall -Wextra -I "${ENGINE_DIR}")
if [ "${SANITIZE:-0}" = "1" ]; then
    CFLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

mkdir -p "${OUT_DIR}"

set -e
"${CC}" "${CFLAGS[@]}" \
    "${ENGINE_DIR}/expr.c" \
    "${ENGINE_DIR}/mathfns.c" \
    "${ENGINE_DIR}/fd_features.c" \
    "${SCRIPT_DIR}/fd_features_test.c" \
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
