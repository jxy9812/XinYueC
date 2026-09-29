#!/usr/bin/env bash
# =============================================================================
# merge_gate.sh —— 标题栏控件化（XTitleBar）五路合流构建与门禁（E2 任务书产物）
#
# 【门什么】五路并行改动（A 装饰判定/XGuiConfig、B XTitleBar 控件、C XWidget/
#   XDockWidget 挂载 API、D 平台策略+XWindow 抑制位+posix 驱动、E1 demo+目验
#   门）合流后的可构建性与仓库既有质量口径，步骤与依据：
#   [0] 前置：新建 .c（XTitleBar*.c）未被 file(GLOB_RECURSE) 感知，先 touch
#       根 CMakeLists.txt（GLOB 重扫；约束文档既定纪律）。
#   [1] 默认配置全量构建：cmake --build build --target all（Debug，产物 bin/）。
#   [2] 双配置语法：XGUI_CUSTOM_TITLEBAR_ON=0 全量再编译。该宏不在根
#       CMakeLists 的 option 转发表内，直接 -D 只进缓存不产生编译定义，
#       故经 CMAKE_C_FLAGS 注入；独立构建目录 out/build/titlebar-off +
#       产物目录 out/bin/titlebar-off，绝不覆盖 bin/ 的默认配置产物。
#       范围=静态库（Src+Drive+Library 全量编译一遍）+ demo + 三套件——
#       即该宏可能影响的全部编译单元；共享库目标省略（同一批源重复编译，
#       对「语法可裁剪」无增量信息）。
#   [3] 仓库三套件回归（XGuiRegression/XLineControl_Acceptance/XGuiGpu，
#       final_gate.sh 同款）+ autotest 双模式（常规 + XGUI_CSD=1，
#       quick_check.sh「标题栏快检」同款——CSD=1 直接驱动本次改造的
#       框架自绘条路径）。断言：退出码 0 + 无 FAIL] 行 + 成功标志行。
#   [4] Release 门：.zcode/final_gate_release.sh（-O2 → bin-release/，
#       API×3 + autotest×3 + 三套件 + 基准，Debug 全绿 ≠ Release 安全）。
#       断言其打印的各 EXIT=0 / REG_FAILS=0；--apitest 的 10 条失败为
#       既有基线（见 API_BASELINE_FAILS，2026-09-29 于门禁前产物实测
#       2756P/10F，均为 completer/IME 弹层、XSplitter、焦点链族——五路
#       未触碰的模块；state.md 2026-09-27 已登记「无头环境已知受限族」），
#       只对「新增失败断言」判红。
#   [5] ASan 快照（XGui.md「ASan 快照法」口径）：build-asan/ 增量构建
#       三套件 + autotest 双模式（CSD=1 专门照新改的默认条 deleteLater
#       生命周期与抑制位路径）。硬失败=ERROR: AddressSanitizer（内存
#       错误/越界/UAF）；LeakSanitizer 退出持有按既有基线记录不判失败
#       （回归夹具持有 ~176KB/09-27 新签名、Mesa 连接级 100KB 为 XGui.md
#       §8.0g8 终态矩阵与 state.md 已登记的存量语义）。
#   [6] git diff --check（final_gate.sh 尾步骤同款）；空白问题属风格，
#       只记录不判死（风格问题按任务书只记录不修）。
#
# 【怎么跑】bash Test/XGuiDemo/titlebar_gate/merge_gate.sh
#   每步只在摘要里打一行 [PASS]/[FAIL]，详细日志全部落
#   out/titlebar_gate/merge_<时间戳>/；任一步失败立即非零退出（fail-fast）。
# 【显示器】套件/autotest 一律 DISPLAY=:99（构建门专用常驻 Xvfb；:98 归
#   titlebar_gate.sh 目验门，本脚本绝不触碰、也不启动任何 Xvfb）。
# =============================================================================
set -u

# 仓库根自定位：本脚本在 Test/XGuiDemo/titlebar_gate/（三层），须上溯三级。
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO_ROOT" || exit 99
TS="$(date +%Y%m%d_%H%M%S)"
LOGROOT="$REPO_ROOT/out/titlebar_gate/merge_$TS"
mkdir -p "$LOGROOT" || exit 99

J=8
export DISPLAY=:99

PASS=0; FAIL=0
step_pass() { PASS=$((PASS+1)); echo "[PASS] $*"; }
step_fail() { FAIL=$((FAIL+1)); echo "[FAIL] $*"; }
die() { step_fail "$*"; echo "MERGE_GATE=FAILED (PASS=$PASS FAIL=$FAIL, logs: $LOGROOT)"; exit 1; }

# --apitest 既有失败基线（2026-09-29 于门禁前 bin/ 产物实测，见头注 [4]）。
# 判定口径：新增失败断言 → 红；该清单内失败 → 记录放行。
API_BASELINE_FAILS="直发 O 键插入字符|键入后前缀匹配产生候选|键入后内建默认弹层自动创建|弹层 show 后有效可见|弹层可见时 Down 回填下一候选文本|Down 后 currentRow=1|XSplitter 默认均分|setTabOrder 后 nextInFocusChain|previousInFocusChain\(second\)=first|focusNextChild\(顶层\)"

# ---------- [0] 前置：GLOB 重扫 ----------
touch "$REPO_ROOT/CMakeLists.txt"
echo "[0] touch 根 CMakeLists.txt 完成（新源 XTitleBar*.c 触发 GLOB 重扫）"

# ---------- [1] 默认配置全量构建 ----------
cmake --build build --target all -j "$J" > "$LOGROOT/build_default.log" 2>&1
rc=$?
errs=$(grep -cE "error:" "$LOGROOT/build_default.log" 2>/dev/null || true)
warns=$(grep -cE "warning:" "$LOGROOT/build_default.log" 2>/dev/null || true)
if [ "$rc" -ne 0 ] || [ "${errs:-0}" -ne 0 ]; then
  echo "---- build_default.log 尾部 ----"; tail -40 "$LOGROOT/build_default.log"
  die "步骤1 默认配置全量构建：rc=$rc errors=${errs:-?}（log: $LOGROOT/build_default.log）"
fi
step_pass "步骤1 默认配置全量构建 rc=0 errors=0 warnings=${warns:-0}（log: build_default.log）"

# ---------- [2] 双配置语法：XGUI_CUSTOM_TITLEBAR_ON=0 ----------
OFF_DIR="$REPO_ROOT/out/build/titlebar-off"
OFF_BIN="$REPO_ROOT/out/bin/titlebar-off"
cmake -S . -B "$OFF_DIR" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-DXGUI_CUSTOM_TITLEBAR_ON=0" \
      -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$OFF_BIN" \
      > "$LOGROOT/build_off_cfg.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "---- build_off_cfg.log 尾部 ----"; tail -30 "$LOGROOT/build_off_cfg.log"
  die "步骤2 宏关配置 configure 失败 rc=$rc（log: $LOGROOT/build_off_cfg.log）"
fi
cmake --build "$OFF_DIR" --target XinYueCS XGuiWindowDemo_Test \
      XGuiRegression_Test XLineControl_Acceptance_Test XGuiGpu_Test \
      -j "$J" > "$LOGROOT/build_off.log" 2>&1
rc=$?
errs=$(grep -cE "error:" "$LOGROOT/build_off.log" 2>/dev/null || true)
warns=$(grep -cE "warning:" "$LOGROOT/build_off.log" 2>/dev/null || true)
if [ "$rc" -ne 0 ] || [ "${errs:-0}" -ne 0 ]; then
  echo "---- build_off.log 尾部 ----"; tail -40 "$LOGROOT/build_off.log"
  die "步骤2 宏关(XGUI_CUSTOM_TITLEBAR_ON=0)编译：rc=$rc errors=${errs:-?}（log: $LOGROOT/build_off.log）"
fi
step_pass "步骤2 宏关配置语法编译 rc=0 errors=0 warnings=${warns:-0}（产物: out/bin/titlebar-off）"

# ---------- [3] 三套件回归 + autotest 双模式（bin/，Debug） ----------
suite() { # suite <标记> <可执行> <成功标志(可空)> [参数...]
  local tag="$1" exe="$2" marker="$3"; shift 3
  # XGUI_CSD=0：headless 控件语义套件显式退系统条模式——CSD 编译默认
  # 开启（XGUI_CSD_DEFAULT=1，多平台标题栏统一）后，套件顶层窗口带框
  # 架条（真实子控件，占顶部条带），childAt/childrenRect 等按无装饰几
  # 何写就的断言不再成立（默认开启当日实测 14 断言即此）。装饰路径的
  # 行为与生命周期覆盖不缺口：AT 步骤（--autotest 默认即装饰态）+
  # AT_CSD（ASan）+ titlebar_gate 双模式目验专项负责。
  timeout 600 env XGUI_CSD=0 "./bin/$exe" "$@" > "$LOGROOT/${tag}.log" 2>&1
  local src=$?
  local fails marks
  fails=$(grep -c 'FAIL\]' "$LOGROOT/${tag}.log" 2>/dev/null || true)
  marks=0
  if [ -n "$marker" ] && ! grep -q "$marker" "$LOGROOT/${tag}.log"; then
    marks=1
  fi
  if [ "$src" -ne 0 ] || [ "${fails:-0}" -ne 0 ] || [ "$marks" -ne 0 ]; then
    echo "---- ${tag}.log 尾部 ----"; tail -25 "$LOGROOT/${tag}.log"
    die "步骤3 套件 $tag：exit=$src fails=${fails:-?} 标志缺失=$marks（log: $LOGROOT/${tag}.log）"
  fi
  step_pass "步骤3 套件 $tag exit=0 fails=0（PASS=$(grep -c '\[PASS\]' "$LOGROOT/${tag}.log" 2>/dev/null || true)）"
}

suite REG     XGuiRegression_Test          "XGui regression tests passed"
suite ACC     XLineControl_Acceptance_Test "XLineControl acceptance tests passed"
suite GPU     XGuiGpu_Test                 ""

# autotest 双模式（常规 + XGUI_CSD=1；quick_check.sh 标题栏快检口径）
suite AT XGuiWindowDemo_Test "" --autotest

timeout 120 env XGUI_CSD=1 ./bin/XGuiWindowDemo_Test --autotest > "$LOGROOT/AT_CSD.log" 2>&1
src=$?
fails=$(grep -c 'FAIL\]' "$LOGROOT/AT_CSD.log" 2>/dev/null || true)
if [ "$src" -ne 0 ] || [ "${fails:-0}" -ne 0 ]; then
  echo "---- AT_CSD.log 尾部 ----"; tail -25 "$LOGROOT/AT_CSD.log"
  die "步骤3 autotest(XGUI_CSD=1)：exit=$src fails=${fails:-?}（log: $LOGROOT/AT_CSD.log）"
fi
step_pass "步骤3 autotest XGUI_CSD=1 exit=0 fails=0（PASS=$(grep -c '\[PASS\]' "$LOGROOT/AT_CSD.log" 2>/dev/null || true)）"

# ---------- [4] Release 门 ----------
if [ -f .zcode/final_gate_release.sh ]; then
  bash .zcode/final_gate_release.sh > "$LOGROOT/release_gate.log" 2>&1
  rgrc=$?
  bad=""
  for pat in "CFG_EXIT=0" "BUILD_EXIT=0" "REG_EXIT=0 REG_FAILS=0" "ACC_EXIT=0" "GPU_EXIT=0"; do
    grep -q -- "$pat" "$LOGROOT/release_gate.log" || bad="$bad $pat"
  done
  # API/AT 三轮行解析：.zcode 脚本的 echo 模板写作 "API$i_EXIT=$?"，bash 把
  # $i_EXIT 展开为未定义变量（空），实际行形态是 "API=<exit> PASS=<n> FAIL=<m>"
  # / "AT=<exit> PASS=<n> FAIL=<m>"（三轮同形态）。断言：AT 三轮全 0 失败；
  # API 三轮行齐且 PASS 数一致（失败明细另行按既有基线甄别，见下）。
  at_lines=$(grep -cE "^AT=0 PASS=[0-9]+ FAIL=0$" "$LOGROOT/release_gate.log" 2>/dev/null || true)
  api_passes=$(grep -E "^API=[0-9]+ PASS=[0-9]+ FAIL=[0-9]+$" "$LOGROOT/release_gate.log" 2>/dev/null \
               | sed 's/.*PASS=\([0-9]*\) FAIL=.*/\1/' | sort -u | wc -l)
  api_lines=$(grep -cE "^API=[0-9]+ PASS=[0-9]+ FAIL=[0-9]+$" "$LOGROOT/release_gate.log" 2>/dev/null || true)
  [ "${at_lines:-0}" -eq 3 ] || bad="$bad autotest×3(${at_lines:-0})"
  [ "${api_lines:-0}" -eq 3 ] || bad="$bad apitest行×3(${api_lines:-0})"
  [ "${api_passes:-2}" -eq 1 ] || bad="$bad apitest三轮PASS不一致(${api_passes:-?})"
  if [ -n "$bad" ]; then
    echo "---- release_gate.log 尾部 ----"; tail -30 "$LOGROOT/release_gate.log"
    die "步骤4 Release 门：rc=$rgrc 未达标项=[$bad ]（log: $LOGROOT/release_gate.log）"
  fi
  # --apitest 既有基线判定：剔除基线清单后仍剩 FAIL 行 = 新增失败 → 红
  newfails="$(grep -h '\[FAIL\]' /tmp/fgr_api1.log /tmp/fgr_api2.log /tmp/fgr_api3.log 2>/dev/null \
             | grep -vE "$API_BASELINE_FAILS" || true)"
  if [ -n "$newfails" ]; then
    printf '%s\n' "$newfails" | head -20
    die "步骤4 Release 门 --apitest 出现既有基线之外的新失败断言（见上）"
  fi
  step_pass "步骤4 Release 门 rc=$rgrc 全部 EXIT=0/REG_FAILS=0（apitest 仅既有基线失败，diff-check 见 release_gate.log 尾行）"
else
  step_pass "步骤4 Release 门脚本不存在，跳过（任务书「如存在」）"
fi

# ---------- [5] ASan 快照 ----------
cmake --build build-asan --target XinYueCS XGuiRegression_Test \
      XLineControl_Acceptance_Test XGuiGpu_Test XGuiWindowDemo_Test \
      -j "$J" > "$LOGROOT/asan_build.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "---- asan_build.log 尾部 ----"; tail -30 "$LOGROOT/asan_build.log"
  die "步骤5 ASan 构建失败 rc=$rc（log: $LOGROOT/asan_build.log）"
fi
asan_run() { # asan_run <标记> <可执行绝对路径> [参数...]（可经 ASAN_ENV 前缀注入环境）
  local tag="$1" exe="$2"; shift 2
  timeout 600 env ASAN_OPTIONS="fast_unwind_on_malloc=0" ${ASAN_ENV:-} "$exe" "$@" \
    > "$LOGROOT/${tag}.log" 2>&1
  local src=$?
  local hard leaks
  hard=$(grep -c "ERROR: AddressSanitizer" "$LOGROOT/${tag}.log" 2>/dev/null || true)
  leaks=$(grep -m1 "SUMMARY: AddressSanitizer" "$LOGROOT/${tag}.log" 2>/dev/null || true)
  if [ "${hard:-0}" -ne 0 ]; then
    echo "---- ${tag}.log ASan 错误 ----"; grep -A5 -m2 "ERROR: AddressSanitizer" "$LOGROOT/${tag}.log"
    die "步骤5 ASan $tag：内存错误 $hard 处（log: $LOGROOT/${tag}.log）"
  fi
  step_pass "步骤5 ASan $tag exit=$src 无内存错误（${leaks:-无泄漏摘要}）"
}

ASAN_BIN="$REPO_ROOT/build-asan/bin"
asan_run ASAN_REG "$ASAN_BIN/XGuiRegression_Test"
asan_run ASAN_ACC "$ASAN_BIN/XLineControl_Acceptance_Test"
asan_run ASAN_GPU "$ASAN_BIN/XGuiGpu_Test"
asan_run ASAN_AT  "$ASAN_BIN/XGuiWindowDemo_Test" --autotest

# CSD=1 变体（显式 env 注入，避免函数前缀赋值的变量残留歧义）
timeout 600 env ASAN_OPTIONS="fast_unwind_on_malloc=0" XGUI_CSD=1 \
  "$ASAN_BIN/XGuiWindowDemo_Test" --autotest > "$LOGROOT/ASAN_AT_CSD.log" 2>&1
src=$?
hard=$(grep -c "ERROR: AddressSanitizer" "$LOGROOT/ASAN_AT_CSD.log" 2>/dev/null || true)
leaks=$(grep -m1 "SUMMARY: AddressSanitizer" "$LOGROOT/ASAN_AT_CSD.log" 2>/dev/null || true)
if [ "${hard:-0}" -ne 0 ]; then
  grep -A5 -m2 "ERROR: AddressSanitizer" "$LOGROOT/ASAN_AT_CSD.log"
  die "步骤5 ASan AT_CSD：内存错误 ${hard:-?} 处（log: $LOGROOT/ASAN_AT_CSD.log）"
fi
step_pass "步骤5 ASan AT_CSD exit=$src 无内存错误（${leaks:-无泄漏摘要}）"

# ---------- [6] diff-check（风格记录，不判死） ----------
if git diff --check > "$LOGROOT/diffcheck.log" 2>&1; then
  step_pass "步骤6 git diff --check 干净"
else
  step_fail "步骤6 git diff --check 有空白类问题（风格记录不判死；log: $LOGROOT/diffcheck.log，$(wc -l < "$LOGROOT/diffcheck.log") 行）"
fi

echo "MERGE_GATE=PASSED (PASS=$PASS FAIL=$FAIL, logs: $LOGROOT)"
exit 0
