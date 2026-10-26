#!/bin/bash
# 真机 192.168.1.117 验证（2026-10-25 断线重连战役）。
# 用法: mbtest_real_device.sh <bin> <io|epoll> <日志目录>
#
# ① 拒连重试: 连 117:15099(关闭端口) 18s —— 主站必须持续重试
#    (Connecting 跃迁 >= 6 次, 状态机不得卡死在 Connecting);
# ② 真实轮询: 连 117:502 10s —— OK 应答 >= 4 且无断链。
# 两项全过 exit 0。

BIN="${1:?bin}"; ENGINE="${2:?engine}"; OUT="${3:?outdir}"
mkdir -p "$OUT"
export XNET_FORCE_EPOLL=$([ "$ENGINE" = "epoll" ] && echo 1 || echo 0)

# ① 关闭端口拒连重试
timeout 25 "$BIN" 192.168.1.117 15099 1 8 soak 18 > "$OUT/refusal-$ENGINE.log" 2>&1
CYCLES=$(grep -c "device state -> 1" "$OUT/refusal-$ENGINE.log")
echo "[realdev] engine=$ENGINE refusal cycles=$CYCLES (need>=6)" | tee -a "$OUT/summary.log"

# ② 真实 502 轮询
timeout 20 "$BIN" 192.168.1.117 502 1 8 soak 10 > "$OUT/poll-$ENGINE.log" 2>&1
SOAK=$(grep "SOAK 结果" "$OUT/poll-$ENGINE.log" | tail -1)
OK=$(echo "$SOAK" | sed -n 's/.*ok=\([0-9]*\).*/\1/p')
DISC=$(echo "$SOAK" | sed -n 's/.*disc=\([0-9]*\).*/\1/p')
echo "[realdev] engine=$ENGINE poll ok=${OK:-0} disc=${DISC:-?} (need ok>=4)" | tee -a "$OUT/summary.log"

if [ "${CYCLES:-0}" -ge 6 ] && [ "${OK:-0}" -ge 4 ]; then exit 0; fi
exit 1
