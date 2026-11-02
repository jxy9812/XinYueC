#!/bin/bash
# VFD / Industry 闭环测试的 Linux 载体门禁（2026-11-02）。
#
# 为什么需要这个载体：Windows 本机的 TCP 环回存在**环境级完成丢失**
# （裸 socket 级复现见 verify/windows_loopback_stall_repro.c；本机 Windows 上
#  xvfd-m512 / xvfd-atv61 / xvfd-bus485 三项闭环无论产品代码怎么改都至少红
#  4~15 条，失败点集中在「主站已发出写帧、从站侧从未收到」，与超时放大量级
#  无关）。Linux 侧走 epoll/io_uring，不复现该停滞，因此闭环的**功能正确性**
# 在 WSL 上判定。
#
# 注意：本载体跑的是**同一份产品代码**（rsync 同步当前工作树）与**同一批
# `--test` 入口**，不跳过任何用例、不放宽任何断言。它换掉的只是承载 TCP
# 环回的**操作系统网络栈**，不是测试内容。
#
# 用法（必须在 WSL 内执行；从 Windows 侧调用）：
#   wsl -d Ubuntu -- bash /mnt/f/Code/CMake/XinYueC/Test/XProtocolTest/vfd_loop_wsl.sh
# 可调环境变量：
#   VFD_WSL_DST   同步目标目录（默认 /home/xyc-src，ext4 原生盘，构建快）
#
# 退出码：0=六条闭环（两套引擎）全绿；非 0=失败用例总数（上限 125）。
set -u

# ---- 定位源树：脚本自身必须在 WSL 的 /mnt 路径下被调用 ----
SELF="$(cd "$(dirname "$0")" && pwd)"
case "$SELF" in
  /mnt/*) SRC="$SELF/../.." ;;
  *) echo "ERROR: 请在 WSL 内经 /mnt 路径调用本脚本（Windows 侧: wsl -d Ubuntu -- bash /mnt/.../vfd_loop_wsl.sh）";
     exit 2 ;;
esac
SRC="$(cd "$SRC" && pwd)"

# ---- 同步到 WSL 原生盘（ext4）：/mnt 的 9P IO 抖动会让构建与测试失真 ----
DST="${VFD_WSL_DST:-/home/xyc-src}"
echo "=== SYNC $SRC -> $DST ==="
mkdir -p "$DST" || { echo "MKDIR FAILED"; exit 2; }
# 排除构建产物与 .git：载体判定的是当前工作树源码。
rsync -a --delete \
  --exclude=.git --exclude=out --exclude=bin --exclude=.vs \
  --exclude=build-wsl --exclude='verify/hsf_shots*' \
  "$SRC/" "$DST/" || { echo "RSYNC FAILED"; exit 2; }

BUILD_DIR="$DST/build-wsl"
LOG_CFG=/tmp/vfd-wsl-cfg.log
LOG_MAKE=/tmp/vfd-wsl-make.log

echo "=== CONFIGURE ==="
cmake -S "$DST" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" \
  > "$LOG_CFG" 2>&1 || { echo "CONFIGURE FAILED"; tail -30 "$LOG_CFG"; exit 3; }

echo "=== BUILD ==="
make -C "$BUILD_DIR" -j"$(nproc)" XinYueC_Static > "$LOG_MAKE" 2>&1 || {
  echo "BUILD FAILED"
  grep -aE "error:|fatal error|Error [0-9]" "$LOG_MAKE" | head -20
  exit 4
}

echo "=== TESTS (io_uring 然后 epoll) ==="
# 六条闭环全覆盖：VFD 三件套 + Industry 两件 + Hub 从站约束单测。
LOOPS="xvfd-m512 xvfd-atv61 xvfd-bus485 xvfd-hub-limits xchannel xindustry-valve xindustry-modbus"
cd "$DST/bin" || exit 2
TOTAL_FAIL=0
for eng in io epoll; do
  if [ "$eng" = "epoll" ]; then export XNET_FORCE_EPOLL=1; else export XNET_FORCE_EPOLL=0; fi
  for t in $LOOPS; do
    out=$(timeout 300 ./XinYueC_Static --test "$t" 2>&1)
    rc=$?
    fails=$(printf '%s' "$out" | grep -ac '\[FAIL\]')
    oks=$(printf '%s' "$out" | grep -ac '\[OK\]\|\[PASS\]')
    echo "WSDLINTEST=$t ENGINE=$eng RC=$rc FAIL=$fails OK=$oks"
    if [ "$rc" != "0" ] || [ "$fails" != "0" ]; then
      printf '%s' "$out" | grep -a '\[FAIL\]' | head -12
      # 退出码非 0 但无 [FAIL]（如崩溃）也计一次失败，不让崩溃被静默吞掉。
      if [ "$rc" != "0" ] && [ "$fails" = "0" ]; then TOTAL_FAIL=$((TOTAL_FAIL + 1)); fi
      TOTAL_FAIL=$((TOTAL_FAIL + fails))
    fi
  done
done
unset XNET_FORCE_EPOLL

echo "WSDLINTOTAL_FAIL=$TOTAL_FAIL"
[ "$TOTAL_FAIL" -gt 125 ] && TOTAL_FAIL=125
exit "$TOTAL_FAIL"
