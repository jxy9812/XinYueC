#!/bin/bash
# 板端输入延迟测量：xclick 注入 → 画面响应（XWD 帧变化）的时间差。
# 用法（板端执行）：board_input_latency.sh <标签> <点击X> <点击Y> <轮数>
# 输出：每轮的「注入 → 帧变化」毫秒数。
LABEL="$1"; CX="$2"; CY="$3"; N="${4:-10}"
export DISPLAY=:0
TMP=/tmp/inplat.$$
mkdir -p "$TMP"

echo "[$LABEL] 轮数=$N 点击=($CX,$CY)"
for i in $(seq 1 "$N"); do
    xwd -root -silent -out "$TMP/pre.xwd" 2>/dev/null
    # 记录注入时刻（毫秒）
    T0=$(date +%s%3N)
    xclick "$CX" "$CY"
    # 轮询等待画面变化（XWD 帧与注入前不同 = 输入已被处理并重绘）
    for _ in $(seq 1 100); do
        xwd -root -silent -out "$TMP/post.xwd" 2>/dev/null
        if ! cmp -s "$TMP/pre.xwd" "$TMP/post.xwd"; then
            T1=$(date +%s%3N)
            echo "  round $i: $((T1 - T0)) ms"
            break
        fi
        sleep 0.02
    done
    sleep 0.3
done
rm -rf "$TMP"
