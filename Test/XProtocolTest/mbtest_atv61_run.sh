#!/bin/bash
# ATV61 变频器 Modbus RTU 闭环测试驱动（2026-10-26）。
# 用法: mbtest_atv61_run.sh [引擎:io|epoll|both]
# 流程: 起 ATV61 模拟从站(python, pty 虚拟串口) → 等就绪 → 跑 XVfdAtv61Test。
# 产物: /tmp/mbsoak/atv61/{slave.log,test-<engine>.log,driver.log}

set -u
ENGINE="${1:-io}"
REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
BIN="${ATV61_BIN:-$REPO/bin/XinYueC_Static}"
SLAVES_PY="$REPO/Test/XProtocolTest/mbtest_atv61_slave.py"
OUT="${ATV61_BASE:-/tmp/mbsoak/atv61}"
mkdir -p "$OUT"
rm -f "$OUT"/fault "$OUT"/clear "$OUT"/pty_path 2>/dev/null

run_engine() {
  local eng="$1"
  export XNET_FORCE_EPOLL=$([ "$eng" = "epoll" ] && echo 1 || echo 0)
  rm -f "$OUT/pty_path"
  for pid in $(pgrep -f "mbtest_atv61_slave.py" 2>/dev/null); do
    [ "$pid" != "$$" ] && kill -9 "$pid" 2>/dev/null
  done
  sleep 0.2
  python3 "$SLAVES_PY" >> "$OUT/slave-$eng.log" 2>&1 &
  local SPID=$!
  # 就绪预检：pty 路径文件生成即放行
  local ok=0
  for i in $(seq 1 60); do
    [ -s "$OUT/pty_path" ] && { ok=1; break; }
    sleep 0.1
  done
  if [ "$ok" != "1" ]; then
    echo "[driver] atv61 slave NOT ready, abort"
    kill -9 $SPID 2>/dev/null
    return 9
  fi
  echo "[driver] engine=$eng slave up (pid=$SPID, pty=$(cat "$OUT/pty_path"))" >> "$OUT/driver.log"
  timeout 90 "$BIN" --test xvfd-atv61 > "$OUT/test-$eng.log" 2>&1
  local RC=$?
  kill -9 "$SPID" 2>/dev/null
  echo "[driver] engine=$eng TEST_RC=$RC" | tee -a "$OUT/driver.log"
  return $RC
}

FAIL=0
for e in "$ENGINE" epoll io; do
  run_engine "$e" || FAIL=1
  [ "$ENGINE" != "both" ] && break
done

tail -40 "$OUT/test-$ENGINE.log" 2>/dev/null
echo "=== ATV61 CLOSED-LOOP: $([ $FAIL = 0 ] && echo ALL-PASS || echo FAIL) ($ENGINE) ==="
exit $FAIL
