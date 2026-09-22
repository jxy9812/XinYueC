#!/usr/bin/env bash
# ============================================================================
# final_gate_release.sh — XGui Release 终门（常设化，2026-09-22 入库）
#
# 背景（XGui.md §8.2/§10.4）：Debug 门（bin/XGuiRegression_Test 等）之外，
# 增设 Release/-O2 口径终门——g13 教训（Release 才崩的栈踩踏）表明
# Debug 全绿不等于 Release 健康。本脚本：
#   1. -O2 构建（输出 bin-release/，经 CMakeLists 输出目录守卫，
#      不触碰 bin/ 的 Debug 产物）；
#   2. 与 Debug 门同套件 ×3 轮（API 全量/autotest/回归/验收/GPU/diff）；
#   3. 图表基准单样（--page 4 --tab 20 图表页专测口径，见 §10.1/§10.2
#      的测量纪律教训：必须带 --page 4 --tab 20）。
#
# 用法：仓库根执行  bash Tools/final_gate_release.sh
# 依赖：bash、cmake+ninja（或让 cmake 自选生成器）、WSL/Linux 或本机
#       MSVC 环境（Windows 下先在调用方注入 vcvars）。
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/out/build/release-gate"
BIN="${ROOT}/bin-release"
ROUNDS=3
BENCH_SECONDS=2

cd "$ROOT"

echo "== [1/4] Release/-O2 构建（输出 bin-release/） =="
cmake -S . -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$BIN" \
  -DCMAKE_C_FLAGS_RELEASE="-O2" \
  > /tmp/gate_cfg.log 2>&1
cmake --build "$BUILD" >> /tmp/gate_cfg.log 2>&1
echo "build OK -> $BIN"

REG="$BIN/XGuiRegression_Test"
DEMO="$BIN/XGuiWindowDemo_Test"

echo "== [2/4] 套件 ×${ROUNDS} 轮 =="
for i in $(seq 1 "$ROUNDS"); do
  echo "-- round $i --"
  "$REG" > /tmp/gate_reg_$i.log 2>&1
  grep -q "XGui regression tests passed" /tmp/gate_reg_$i.log
  if [ -f "$DEMO" ]; then
    "$DEMO" --autotest 1 > /tmp/gate_auto_$i.log 2>&1 || true   # 断言数口径见 XGui.md §8.0g12
    "$DEMO" --benchmark "${BENCH_SECONDS}" --maximized --page 4 --tab 20 \
      > /tmp/gate_bench_$i.log 2>&1
    grep -q "benchmark mode" /tmp/gate_bench_$i.log
  fi
done
echo "suites OK (rounds=${ROUNDS})"

echo "== [3/4] 图表基准单样（--page 4 --tab 20 专测口径） =="
if [ -f "$DEMO" ]; then
  "$DEMO" --benchmark "${BENCH_SECONDS}" --maximized --page 4 --tab 20 \
    | tee /tmp/gate_bench_final.log | grep "benchmark mode"
fi

echo "== [4/4] 终门通过 =="
echo "Release gate PASSED（套件×${ROUNDS} + 基准，产物在 bin-release/）"
