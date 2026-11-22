#!/bin/sh
# verify/board/desktop_churn.sh —— 桌面 C 客户端「连接→断开」反复压测。
#
# 用法（Windows Git Bash）:
#   sh verify/board/desktop_churn.sh <轮数> [每次在线秒数] [间隔秒数] [日志]
#
# 每次循环 = 启动一个桌面客户端实例 → 无头登录板端 6100 → 在线 N 秒 →
# 自动退出（客户端走正常收手路径：发断开帧 → 服务端回收会话）。
# 目的：给**板端服务端**制造反复的会话创建/销毁，暴露「每轮会话后堆
# 回不到基线」的按会话泄漏；同时让**板端客户端**持续收推送。
#
# 注意：客户端窗口会在桌面上闪现（GUI 程序）。若要减少干扰可调小 --size。
set -u
ROOT=/f/Code/CMake/XinYueC
N="${1:-100}"
HOLD="${2:-6}"
GAP="${3:-2}"
LOG="${4:-$ROOT/out/leak_churn.log}"

cd "$ROOT" || exit 1
: > "$LOG"
i=1
while [ "$i" -le "$N" ]; do
    ts=$(date +%s)
    echo "=== cycle $i start $(date -u +%H:%M:%S)" >> "$LOG"
    timeout 90 ./bin/HsfClient.exe "$HOLD" \
        --login root:root@192.168.1.249:6100 --no-settings --size 800x600 \
        >> "$LOG" 2>&1
    rc=$?
    echo "=== cycle $i end ts=$ts rc=$rc $(date -u +%H:%M:%S)" >> "$LOG"
    sleep "$GAP"
    i=$((i + 1))
done
echo "=== churn done $(date -u +%H:%M:%S)" >> "$LOG"
