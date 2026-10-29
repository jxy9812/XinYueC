#!/bin/bash
# 511M/512M/513M 变频器 XVfd 闭环测试驱动（2026-10-27）。
# 用法: mbtest_m512_run.sh [引擎:io|epoll|both]
# 流程: 起 511M 族模拟从站(python, pty, 双站) → 等就绪 → 跑 XVfdM512Test。
set -u
ENGINE="${1:-io}"
REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
BIN="${M512_BIN:-$REPO/bin/XinYueC_Static}"
SLAVES_PY="$REPO/Test/XProtocolTest/mbtest_m512_slave.py"
OUT="${M512_BASE:-/tmp/mbsoak/m512}"
mkdir -p "$OUT"
rm -f "$OUT"/fault_u1 "$OUT"/fault_u2 "$OUT"/mute "$OUT"/pty_path 2>/dev/null

run_engine() {
  local eng="$1"
  export XNET_FORCE_EPOLL=$([ "$eng" = "epoll" ] && echo 1 || echo 0)
  rm -f "$OUT/pty_path"
  for pid in $(pgrep -f "mbtest_m512_slave.py" 2>/dev/null); do
    [ "$pid" != "$$" ] && kill -9 "$pid" 2>/dev/null
  done
  sleep 0.2
  python3 "$SLAVES_PY" >> "$OUT/slave-$eng.log" 2>&1 &
  local SPID=$!
  local ok=0
  for i in $(seq 1 60); do
    [ -s "$OUT/pty_path" ] && { ok=1; break; }
    sleep 0.1
  done
  if [ "$ok" != "1" ]; then
    echo "[driver] m512 slave NOT ready, abort"
    kill -9 $SPID 2>/dev/null
    return 9
  fi
  timeout 120 "$BIN" --test xvfd-m512 > "$OUT/test-$eng.log" 2>&1
  local RC=$?
  kill -9 $SPID 2>/dev/null
  echo "[driver] engine=$eng TEST_RC=$RC"
  [ $RC -eq 0 ]
}

case "$ENGINE" in
  io)   run_engine io ;;
  epoll) run_engine epoll ;;
  both)
    # 退出码必须如实上报：原实现 `run_engine io && run_engine epoll &&
    # echo ALL-PASS || echo FAIL` 的收尾 echo 恒为退出码 0，门禁把失败
    # 当成功。对齐 mbtest_campaign_run.sh 的 FAIL 累计口径。
    FAIL=0
    run_engine io    || FAIL=1
    run_engine epoll || FAIL=1
    echo "=== M512 CLOSED-LOOP: $([ $FAIL = 0 ] && echo ALL-PASS || echo FAIL) (both) ==="
    exit $FAIL
    ;;
  *) echo "usage: $0 [io|epoll|both]"; exit 2 ;;
esac
