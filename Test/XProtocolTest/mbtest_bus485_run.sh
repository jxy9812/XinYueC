#!/bin/bash
# 虚拟 RS485 总线（多从站）端到端测试驱动（2026-10-27）。
# 用法: mbtest_bus485_run.sh [引擎:io|epoll|both]
# 流程: 起虚拟总线 hub（1 master pty + N slave pty）→ 起 N 个独立从站进程
#       → 等就绪 → 跑 XVfdBus485Test。
set -u
ENGINE="${1:-io}"
REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
BIN="${BUS485_BIN:-$REPO/bin/XVfdBus485Test}"
OUT="${BUS485_BASE:-/tmp/mbsoak/bus485}"
HUB_PY="$REPO/Test/XProtocolTest/mbtest_bus485_hub.py"
SLAVES_PY="$REPO/Test/XProtocolTest/mbtest_m512_slave.py"
mkdir -p "$OUT"
rm -f "$OUT"/bus_master_path "$OUT"/bus_slave_paths "$OUT"/bus_frames.log 2>/dev/null

run_engine() {
  local eng="$1"
  export XNET_FORCE_EPOLL=$([ "$eng" = "epoll" ] && echo 1 || echo 0)
  cleanup() {
    for p in ${BUS485_PIDS:-}; do kill -9 "$p" 2>/dev/null; done
  }
  trap cleanup EXIT
  BUS485_PIDS=""
  rm -f "$OUT/bus_master_path" "$OUT/bus_slave_paths" "$OUT/bus_frames.log"
  for pid in $(pgrep -f "mbtest_bus485_hub.py|mbtest_m512_slave.py" 2>/dev/null); do
    [ "$pid" != "$$" ] && kill -9 "$pid" 2>/dev/null
  done
  sleep 0.2

  # ① 虚拟总线 hub
  python3 "$HUB_PY" --slaves 2 --units 1,2 --base "$OUT" >> "$OUT/hub-$eng.log" 2>&1 &
  local HPID=$!
  BUS485_PIDS="$BUS485_PIDS $HPID"
  local ok=0
  for i in $(seq 1 60); do
    [ -s "$OUT/bus_master_path" ] && [ -s "$OUT/bus_slave_paths" ] && { ok=1; break; }
    sleep 0.1
  done
  if [ "$ok" != "1" ]; then
    echo "[driver] bus485 hub NOT ready, abort"
    return 9
  fi

  # ② 每个从站进程独占一个 slave pty
  local i=1
  while read -r sp; do
    [ -z "$sp" ] && continue
    python3 "$SLAVES_PY" --unit "$i" --pty "$sp" >> "$OUT/slave-u$i-$eng.log" 2>&1 &
    BUS485_PIDS="$BUS485_PIDS $!"
    i=$((i+1))
  done < "$OUT/bus_slave_paths"
  sleep 0.6

  echo "[driver] engine=$eng hub up (master=$(cat "$OUT/bus_master_path"))" >> "$OUT/driver.log"
  timeout 120 "$BIN" "$(cat "$OUT/bus_master_path")" > "$OUT/test-$eng.log" 2>&1
  local RC=$?
  echo "[driver] engine=$eng TEST_RC=$RC"
  [ $RC -eq 0 ]
}

case "$ENGINE" in
  io)   run_engine io ;;
  epoll) run_engine epoll ;;
  both) run_engine io && run_engine epoll && echo "=== BUS485 CLOSED-LOOP: ALL-PASS (both) ===" || echo "=== BUS485 CLOSED-LOOP: FAIL ===" ;;
  *) echo "usage: $0 [io|epoll|both]"; exit 2 ;;
esac
