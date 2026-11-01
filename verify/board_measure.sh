#!/bin/bash
# 板端呈现链路实测：分档跑 demo，量 CPU + 抓 wprof/xpainter-prof 画像
# 用法（板端）：board_measure.sh "<label> <env...>" ...
set -u
BIN=/root/Desktop/XinYueC/bin/XGuiWindowDemo_Test
PAGE=${PAGE:-18}
RUN=${RUN:-12}

run_one() {
    label="$1"; shift
    env_prefix="$*"
    echo "=== $label ==="
    # shellcheck disable=SC2086
    env DISPLAY=:0 $env_prefix "$BIN" --fullscreen --page "$PAGE" > /tmp/measure_$label.log 2>&1 &
    pid=$!
    sleep 3
    u0=$(awk '{print $14+$15}' /proc/$pid/stat 2>/dev/null || echo 0)
    s0=$(date +%s%N)
    sleep "$RUN"
    u1=$(awk '{print $14+$15}' /proc/$pid/stat 2>/dev/null || echo 0)
    s1=$(date +%s%N)
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    awk -v l="$label" -v a="$u0" -v b="$u1" -v s0="$s0" -v s1="$s1" 'BEGIN{
        ticks=b-a; ms=(s1-s0)/1000000; cpu=ticks*1000/ms;
        printf "CPU=%.1f%% of one core  ticks=%d  ms=%d\n", cpu, ticks, ms;
    }'
    echo "--- 画像摘要 ---"
    grep -E "^\[wprof\]|^\[xpainter-prof\]|^\[gl-info\]" /tmp/measure_$label.log | tail -12
    echo
}

"$@"
