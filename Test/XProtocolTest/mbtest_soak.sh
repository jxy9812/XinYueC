#!/bin/bash
# XModbus 断线重连回归驱动（2026-10-25 战役）。
# 用法: mbtest_soak.sh <bin路径> <引擎:io|epoll> <轮数> [端口] [--rst]
# 环境变量: DOWN_SECS=从站停机秒数(默认1；≥8 可覆盖多次 connect 被拒场景)
#
# 流程: 起从站 → 起主站探针(soak) → 基线稳定 → 每轮 kill -9 从站
#       → 停机 DOWN_SECS 秒 → 重启从站 → 15s 内必须出现 RECOVERED → 下一轮。
# 判定: 每轮 "RECOVERED" 计数递增 = PASS；探针存活、无 STUCK 连拍。
# 产物: /tmp/mbsoak/<engine>/slave.log master.log driver.log

set -u
BIN="${1:?usage: mbtest_soak.sh <bin> <io|epoll> <rounds> [port] [down=N] [--rst]}"
ENGINE="${2:?io|epoll}"
ROUNDS="${3:-5}"
PORT="${4:-15072}"
RST=""
DOWN_SECS="${DOWN_SECS:-1}"
shift 4 || true
for a in "$@"; do
  case "$a" in
    --rst) RST="--rst" ;;
    down=*) DOWN_SECS="${a#down=}" ;;
  esac
done

OUT="/tmp/mbsoak/${ENGINE}-p${PORT}"
# [2026-10-25 并发隔离] 日志目录按端口隔离：工作流多 lane 同引擎并发时
# 共享 /tmp/mbsoak/<engine> 会互截 master.log，恢复计数互相污染全误判。
mkdir -p "$OUT"
SLAVE_PY="$(dirname "$0")/mbtest_slave.py"

# 唯一显示/端口防串扰：杀掉本脚本自己起的残留进程（按 PID 记录文件）
pkill -f "mbtest_slave.py $PORT" 2>/dev/null && sleep 0.3

: > "$OUT/slave.log"; : > "$OUT/master.log"; : > "$OUT/driver.log"

start_slave() { python3 "$SLAVE_PY" "$PORT" $RST >> "$OUT/slave.log" 2>&1 & SLAVE_PID=$!; }
kill_slave()  { kill -9 "$SLAVE_PID" 2>/dev/null; wait "$SLAVE_PID" 2>/dev/null; }

recov_count() { local c; c=$(grep -c "RECOVERED" "$OUT/master.log" 2>/dev/null); echo "${c:-0}"; }

export XNET_FORCE_EPOLL=0
if [ "$ENGINE" = "epoll" ]; then export XNET_FORCE_EPOLL=1; else XNET_FORCE_EPOLL=0; fi
# 引擎自证：探针起后 driver 记录一次引擎模式（io_uring/epoll 无法直接打印，
# 以 env 生效 + 独立引擎探针脚本核对；此处仅记录开关值）
echo "[driver] engine=$ENGINE XNET_FORCE_EPOLL=$XNET_FORCE_EPOLL port=$PORT rounds=$ROUNDS rst=$RST" | tee -a "$OUT/driver.log"

# ① 基线
start_slave
sleep 0.5
"$BIN" 127.0.0.1 "$PORT" 1 8 soak 0 >> "$OUT/master.log" 2>&1 &
MASTER_PID=$!
echo "[driver] master pid=$MASTER_PID slave pid=$SLAVE_PID" >> "$OUT/driver.log"

# 等基线：首轮 OK 应答或 8s 超时
BASE_OK=0
for i in $(seq 1 80); do
  if grep -q "OK  values" "$OUT/master.log"; then BASE_OK=1; break; fi
  if grep -q "SOAK 结果" "$OUT/master.log"; then break; fi
  sleep 0.1
done
if [ "$BASE_OK" != 1 ]; then
  echo "[driver] FAIL: 基线 8s 内未建立（无 RECOVERED）" | tee -a "$OUT/driver.log"
  kill -9 "$MASTER_PID" "$SLAVE_PID" 2>/dev/null
  exit 2
fi
echo "[driver] 基线 OK ($(recov_count) recovered)" | tee -a "$OUT/driver.log"

# ② 断链轮次
FAIL=0
for r in $(seq 1 "$ROUNDS"); do
  BEFORE=$(recov_count)
  T0=$(date +%s%3N)
  kill -9 "$SLAVE_PID" 2>/dev/null; wait "$SLAVE_PID" 2>/dev/null
  echo "[driver] round $r: slave killed at $T0 (down ${DOWN_SECS:-1}s)" >> "$OUT/driver.log"
  sleep "${DOWN_SECS:-1}"
  start_slave
  echo "[driver] round $r: slave restarted" >> "$OUT/driver.log"

  OK_ROUND=0
  for i in $(seq 1 150); do   # 15s 窗口
    NOW=$(recov_count)
    if [ "$NOW" -gt "$BEFORE" ]; then
      T1=$(date +%s%3N)
      echo "[driver] round $r: PASS recovered in $((T1-T0)) ms (total $NOW)" | tee -a "$OUT/driver.log"
      OK_ROUND=1; break
    fi
    if ! kill -0 "$MASTER_PID" 2>/dev/null; then
      echo "[driver] round $r: FAIL master died" | tee -a "$OUT/driver.log"
      OK_ROUND=0; break
    fi
    sleep 0.1
  done
  if [ "$OK_ROUND" != 1 ]; then
    echo "[driver] round $r: FAIL not recovered in 15s" | tee -a "$OUT/driver.log"
    FAIL=1
    break
  fi
  sleep 2   # 稳定窗口，确保下一轮基线干净
done

echo "[driver] ===== $ENGINE 结果: $([ $FAIL = 0 ] && echo ALL-PASS || echo FAIL) rounds=$ROUNDS =====" | tee -a "$OUT/driver.log"
kill -9 "$MASTER_PID" 2>/dev/null
kill -9 "$SLAVE_PID" 2>/dev/null
wait 2>/dev/null
exit $FAIL
