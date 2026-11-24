#!/usr/bin/env bash
# 一次编译并运行三个模块的单元测试，任何一项失败则整体退出码非 0。
set -u

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULES=("01-can-codec" "02-uds-diagnostics" "03-pump-fsm")
FAILED=0

echo "=============================================="
echo " 汽车电子泵控制 ECU 软件模块 - 全量测试"
echo "=============================================="

for mod in "${MODULES[@]}"; do
    echo
    echo ">>> ${mod}"
    make -C "${ROOT_DIR}/${mod}" clean >/dev/null 2>&1
    if make -C "${ROOT_DIR}/${mod}" test >"/tmp/${mod}.log" 2>&1; then
        grep -E "checks" "/tmp/${mod}.log" || true
    else
        echo "    [FAIL] 编译或测试失败，日志如下："
        sed -n '1,40p' "/tmp/${mod}.log"
        FAILED=$((FAILED + 1))
    fi
done

echo
echo "----------------------------------------------"
echo "模块数: ${#MODULES[@]}    失败模块: ${FAILED}"
if [ "${FAILED}" -eq 0 ]; then
    echo "[结论] 全部模块编译通过，单元测试全绿。"
    exit 0
else
    echo "[结论] 存在失败模块，见上方输出。"
    exit 1
fi
