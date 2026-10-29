#!/bin/bash
# 战役 1028 XProtocol 端到端联调驱动（对齐 mbtest_hub_run.sh 风格）。
# 用法: mbtest_campaign_run.sh [io|epoll|both]   （缺省 io）
# env : REPO        仓库根（缺省 /home/xinyue/Code/XinYueC）
#       CAMPAIGN_BIN 被测二进制（缺省 $REPO/bin/XProtocolCampaignTest）
#       CAMPAIGN_BASE 产物根（pty 路径/帧日志/控制文件/测试日志全在此，
#                     缺省 /tmp/mbsoak/campaign1028）
# 流程: 起多模式虚拟从站(python, 3 pty + 1 tcp) → 就绪预检 → 跑
#       XProtocolCampaignTest（S1..S10）→ 清理；引擎切 XNET_FORCE_EPOLL。
# 端口: TCP 固定 15431（战役专用段 15431/15432）。
# 清理: pgrep 拿 PID 再 kill（禁 pkill -f 自杀坑）。退出码=成败。

set -u
ENGINE="${1:-io}"
REPO="${REPO:-/home/xinyue/Code/XinYueC}"
CAMPAIGN_BIN="${CAMPAIGN_BIN:-$REPO/bin/XProtocolCampaignTest}"
BASE="${CAMPAIGN_BASE:-/tmp/mbsoak/campaign1028}"
SLAVES_PY="$REPO/Test/XProtocolTest/mbtest_campaign_slave.py"
PORT=15431
TIMEOUT_SEC=150

[ -x "$CAMPAIGN_BIN" ] || { echo "[driver] binary not found: $CAMPAIGN_BIN"; exit 2; }
[ -f "$SLAVES_PY" ] || { echo "[driver] slaves py not found: $SLAVES_PY"; exit 2; }
mkdir -p "$BASE"

stop_slaves() {
  # 清理从站残留（精确 PID 扫描；跳过自身与直接父进程）
  for pid in $(pgrep -f "mbtest_campaign_slave.py" 2>/dev/null); do
    [ "$pid" != "$$" ] && [ "$pid" != "$PPID" ] && kill -9 "$pid" 2>/dev/null
  done
  sleep 0.2
}

run_engine() {
  local eng="$1"
  export XNET_FORCE_EPOLL=$([ "$eng" = "epoll" ] && echo 1 || echo 0)
  stop_slaves
  # 清上一轮控制文件与就绪标记
  rm -f "$BASE"/bus1_mute_u* "$BASE"/bus1_badcrc_u* "$BASE"/bus1_swalloww_u* \
        "$BASE"/bus2_mute_u* "$BASE"/bus2_badcrc_u* "$BASE"/bus2_closem \
        "$BASE"/bus2_closed "$BASE"/bus3_closem "$BASE"/bus3_closed \
        "$BASE"/ready \
        "$BASE"/pty1_path "$BASE"/pty2_path "$BASE"/pty3_path 2>/dev/null
  python3 "$SLAVES_PY" --base "$BASE" --tcp-port "$PORT" \
    >> "$BASE/slaves-$eng.log" 2>&1 &
  local SPID=$!
  # 就绪预检：ready 标记 + 三条 pty 路径文件 + TCP 端口在听
  local ready=0
  for i in $(seq 1 80); do
    if [ -s "$BASE/ready" ] \
       && [ -s "$BASE/pty1_path" ] && [ -s "$BASE/pty2_path" ] \
       && [ -s "$BASE/pty3_path" ] \
       && ss -tln 2>/dev/null | grep -q ":$PORT "; then ready=1; break; fi
    sleep 0.1
  done
  if [ "$ready" != "1" ]; then
    echo "[driver] engine=$eng slaves NOT ready, abort" | tee -a "$BASE/driver.log"
    kill -9 "$SPID" 2>/dev/null
    stop_slaves
    return 9
  fi
  echo "[driver] engine=$eng slaves up (pid=$SPID, base=$BASE, port=$PORT)" \
    | tee -a "$BASE/driver.log"
  timeout "$TIMEOUT_SEC" "$CAMPAIGN_BIN" "$BASE" "$PORT" \
    > "$BASE/test-$eng.log" 2>&1
  local RC=$?
  kill -9 "$SPID" 2>/dev/null
  stop_slaves
  echo "[driver] engine=$eng TEST_RC=$RC" | tee -a "$BASE/driver.log"
  return $RC
}

FAIL=0
if [ "$ENGINE" = "both" ]; then
  run_engine io    || FAIL=1
  run_engine epoll || FAIL=1
else
  run_engine "$ENGINE" || FAIL=1
fi

tail -3 "$BASE/test-$ENGINE.log" 2>/dev/null
echo "=== CAMPAIGN S1..S10: $([ $FAIL = 0 ] && echo ALL-PASS || echo FAIL) ($ENGINE) ==="
exit $FAIL
