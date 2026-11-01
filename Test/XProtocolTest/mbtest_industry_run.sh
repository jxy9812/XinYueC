#!/bin/sh
# ==================== Industry 组合回归驱动（2026-10-31） ====================
# 起 mbtest_hub_slaves.py 从站拓扑（已知寄存器初值：DI0=1、IR0=2530，
# HR10/12/13 可写）→ 跑主程序收编入口 --test xindustry-modbus：
#   ① TCP 环回组合（端口参数，缺省跑）
#   ② RTU 组合（INDUSTRY_RTU_PTY=从站 pty 路径）
# 用法：REPO=/root/xinyuec-src bash Test/XProtocolTest/mbtest_industry_run.sh
# 返回码=测试失败数（两段取大）。

REPO="${REPO:-/root/xinyuec-src}"
BIN="${INDUSTRY_BIN:-$REPO/bin/XinYueC_Static}"
SLAVES_PY="$REPO/Test/XProtocolTest/mbtest_hub_slaves.py"
OUT="${INDUSTRY_BASE:-/tmp/mbsoak/industry}"
# 从站脚本 BASE 硬编码 /tmp/mbsoak/hub（rtu_pty_path 写在那里）。
PTY_PATH_FILE="/tmp/mbsoak/hub/rtu_pty_path"
P1=15331
P2=15332

[ -x "$BIN" ] || { echo "[driver] binary not found: $BIN"; exit 2; }
mkdir -p "$OUT"

# 清理残留从站（精确 PID 扫描，禁 pkill -f 自杀坑）
for pid in $(pgrep -f "mbtest_hub_slaves.py"); do
    [ "$pid" != "$$" ] && kill -9 "$pid" 2>/dev/null
done
sleep 0.2

setsid nohup python3 "$SLAVES_PY" "$P1" "$P2" >> "$OUT/slaves.log" 2>&1 < /dev/null &
SPID=$!
i=0
while [ "$i" -lt 60 ]; do
    [ -s "$PTY_PATH_FILE" ] && \
    ss -tln 2>/dev/null | grep -q ":$P1 " && \
    ss -tln 2>/dev/null | grep -q ":$P2 " && break
    sleep 0.1
    i=$((i + 1))
done
if ! [ -s "$PTY_PATH_FILE" ] || ! ss -tln 2>/dev/null | grep -q ":$P1 "; then
    echo "[driver] slaves NOT ready, abort"
    kill -9 "$SPID" 2>/dev/null
    exit 9
fi
echo "[driver] slaves up (pid=$SPID)"

export XNET_FORCE_EPOLL=0
export INDUSTRY_RTU_PTY=$(cat "$PTY_PATH_FILE")
timeout 180 "$BIN" --test xindustry-modbus > "$OUT/test.log" 2>&1
RC=$?
kill -9 "$SPID" 2>/dev/null
tail -5 "$OUT/test.log"
echo "=== INDUSTRY COMBO: RC=$RC ==="
exit $RC
