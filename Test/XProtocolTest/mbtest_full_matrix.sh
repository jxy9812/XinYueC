#!/bin/bash
# XModbus 断线重连根修全量回归编排（2026-10-25 战役，工作流缓存重放两连坑后
# 改为本地并行 lane 直跑；矩阵与工作流脚本一致）。
# 用法: mbtest_full_matrix.sh   （产物: /tmp/mbsoak/matrix/，汇总: matrix.log）

REPO=/home/xinyue/Code/XinYueC
SOAK="$REPO/Test/XProtocolTest/mbtest_soak.sh"
REAL="$REPO/Test/XProtocolTest/mbtest_real_device.sh"
EPOLL_BIN=/tmp/mbsoak/bin/probe-epoll
IO_BIN=/tmp/mbsoak/bin/probe-io
M=/tmp/mbsoak/matrix
mkdir -p "$M"

lane_epoll_fin() {  # FIN 矩阵（短/长/超长停机）
  bash "$SOAK" "$EPOLL_BIN" epoll 4 15301 down=2 > "$M/epoll-d2.log" 2>&1
  bash "$SOAK" "$EPOLL_BIN" epoll 4 15302 down=8 > "$M/epoll-d8.log" 2>&1
  bash "$SOAK" "$EPOLL_BIN" epoll 3 15303 down=20 > "$M/epoll-d20.log" 2>&1
  echo "epoll-FIN DONE"
}
lane_io_fin() {
  bash "$SOAK" "$IO_BIN" io 4 15311 down=2 > "$M/io-d2.log" 2>&1
  bash "$SOAK" "$IO_BIN" io 4 15312 down=8 > "$M/io-d8.log" 2>&1
  bash "$SOAK" "$IO_BIN" io 3 15313 down=20 > "$M/io-d20.log" 2>&1
  echo "io-FIN DONE"
}
lane_rst() {        # RST 硬断 + 0.3s 快重启风暴
  bash "$SOAK" "$EPOLL_BIN" epoll 4 15321 down=3 --rst > "$M/epoll-rst.log" 2>&1
  bash "$SOAK" "$IO_BIN" io 4 15322 down=3 --rst > "$M/io-rst.log" 2>&1
  bash "$SOAK" "$EPOLL_BIN" epoll 8 15323 down=0.3 > "$M/epoll-rapid.log" 2>&1
  bash "$SOAK" "$IO_BIN" io 8 15324 down=0.3 > "$M/io-rapid.log" 2>&1
  echo "RST DONE"
}
lane_real() {       # 真机 117：拒连重试 + 502 轮询，双引擎
  bash "$REAL" "$EPOLL_BIN" epoll "$M/realdev" > "$M/realdev-epoll.log" 2>&1
  bash "$REAL" "$IO_BIN" io "$M/realdev" > "$M/realdev-io.log" 2>&1
  echo "REAL DONE"
}
lane_gui() {        # 仓库既有 GUI 回归门（自带锁+构建+Xvfb）
  bash "$REPO/.zcode/wf-gates/wf_regression_gate.sh" "$M/reg_gate.log" > "$M/reg_gate.out" 2>&1
  echo "GUI DONE"
}

lane_epoll_fin & P1=$!
lane_io_fin    & P2=$!
lane_rst       & P3=$!
lane_real      & P4=$!
lane_gui       & P5=$!
wait $P1 $P2 $P3 $P4 $P5

echo "==================== MATRIX SUMMARY ===================="
FAIL=0
for f in "$M"/epoll-d2.log "$M"/epoll-d8.log "$M"/epoll-d20.log \
         "$M"/io-d2.log "$M"/io-d8.log "$M"/io-d20.log \
         "$M"/epoll-rst.log "$M"/io-rst.log "$M"/epoll-rapid.log "$M"/io-rapid.log; do
  line=$(/bin/grep "=====" "$f" | tail -1)
  echo "$line"
  echo "$line" | /bin/grep -q "ALL-PASS" || FAIL=1
done
for f in "$M"/realdev-epoll.log "$M"/realdev-io.log; do
  ok=$(/bin/grep -c "refusal cycles" "$f")
  line=$(/bin/grep "realdev" "$f" | tr '\n' ' ')
  echo "REAL($f): $line"
  [ "$(/bin/grep -c 'refusal cycles' "$f")" -ge 1 ] || FAIL=1
done
echo "--- GUI gate ---"
cat "$M/reg_gate.out" 2>/dev/null | tail -3
/bin/grep -q "REG_RC=0" "$M/reg_gate.out" 2>/dev/null || FAIL=1
echo "==================== RESULT: $([ $FAIL = 0 ] && echo ALL-PASS || echo HAS-FAIL) ===================="
