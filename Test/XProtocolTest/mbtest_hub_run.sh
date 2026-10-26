#!/bin/bash
# XModbus 聚合设备闭环测试驱动（2026-10-26 战役）。
# 用法: mbtest_hub_run.sh [引擎:io|epoll]
# 流程: 起三从站拓扑(python) → 等就绪 → 跑 XModbusHubTest → 双引擎各一轮。
# 产物: /tmp/mbsoak/hub/{slaves.log,test-<engine>.log,driver.log}

set -u
ENGINE="${1:-io}"
REPO=/home/xinyue/Code/XinYueC
BIN="$REPO/bin/XModbusHubTest"
SLAVES_PY="$REPO/Test/XProtocolTest/mbtest_hub_slaves.py"
OUT=/tmp/mbsoak/hub
mkdir -p "$OUT"
rm -f "$OUT"/pause* "$OUT"/down* "$OUT"/di_* 2>/dev/null

P1=15331; P2=15332

run_engine() {
  local eng="$1"
  export XNET_FORCE_EPOLL=$([ "$eng" = "epoll" ] && echo 1 || echo 0)
  rm -rf "$OUT/rtu_pty_path"
  pkill -f "mbtest_hub_slaves.py" 2>/dev/null && sleep 0.3
  # 清理本脚本族残留（精确 PID 扫描，禁 pkill -f 自杀坑）
  for pid in $(pgrep -f "hub_slaves.py"); do [ "$pid" != "$$" ] && kill -9 "$pid" 2>/dev/null; done
  sleep 0.2
  python3 "$SLAVES_PY" "$P1" "$P2" >> "$OUT/slaves-$eng.log" 2>&1 &
  local SPID=$!
  # 就绪预检：pty + 双端口都在听才放行
  for i in $(seq 1 50); do
    [ -s "$OUT/rtu_pty_path" ] && \
    ss -tln 2>/dev/null | grep -q ":$P1 " && \
    ss -tln 2>/dev/null | grep -q ":$P2 " && break
    sleep 0.1
  done
  ss -tln 2>/dev/null | grep -q ":$P1 " && ss -tln 2>/dev/null | grep -q ":$P2 " \
    || { echo "[driver] slaves NOT listening, abort"; kill -9 $SPID; return 9; }
  echo "[driver] engine=$eng slaves up (pid=$SPID)" >> "$OUT/driver.log"
  timeout 90 "$BIN" "$P1" "$P2" > "$OUT/test-$eng.log" 2>&1
  local RC=$?
  kill -9 "$SPID" 2>/dev/null
  echo "[driver] engine=$eng TEST_RC=$RC" | tee -a "$OUT/driver.log"
  return $RC
}

FAIL=0
for e in "$ENGINE" epoll io; do
  run_engine "$e" || FAIL=1
  [ "$ENGINE" != "both" ] && break   # 单引擎模式只跑指定引擎
done

tail -3 "$OUT/test-$ENGINE.log" 2>/dev/null
echo "=== HUB CLOSED-LOOP: $([ $FAIL = 0 ] && echo ALL-PASS || echo FAIL) ($ENGINE) ==="
exit $FAIL
