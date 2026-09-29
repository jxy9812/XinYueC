#!/usr/bin/env bash
# =============================================================================
# titlebar_gate.sh —— 窗口装饰双模式冒烟门（精简版）
#
# 【门什么】XGUI_CSD 双模式下的标题栏形态与原生装饰抑制（对标 Qt CSD）：
#   - XGUI_CSD=1：框架自绘条上屏（☰/标题/右键钮簇像素在案）+ 平台层
#     _MOTIF_WM_HINTS decorations=0（抑制原生 WM 装饰，消除双栏）；
#   - XGUI_CSD=0：系统条模式——框架不越权（decorations=DECOR_ALL；
#     Xvfb 无 WM 时「无任何条」正是该模式的正确表现）。
#   页面取 --page 1（单窗无叠窗，Xvfb 无 WM 摆位=请求位 40,40,800x600，
#   像素坐标确定）。最大化/最小化的 WM 状态级验收在 mmfix_diag/diag.sh
#   （真桌面 ：0，本脚本 Xvfb 无 WM 无法验证状态类语义）。
#
# 【怎么跑】顺序：
#   0) 清本仓库遗留 demo 进程（ps+grep 匹配，禁 pkill -x）；二进制先拷
#      out/ 再以 out/ 路径运行（产物与源分离）。
#   1) Xvfb :98（:99 留给构建门；被占则等待，不抢显示器）。
#   2) 对 csd in (1,0) 各一轮：
#      a. XGUI_CSD=$csd 启动 --page 1，stdbuf -o0 日志落盘。
#      b. 等屏稳：连捕两帧 xwd md5 一致才继续（Xvfb 空闲 PAINT 延迟
#         0.7~4s，不做固定 sleep 猜测）。
#      c. xwd -id 抓客户窗 → xwdtopnm → ffmpeg raw rgb24 → dd+od 盒内
#         像素断言：
#         - CSD=1：条带 ☰ 钮区（局部 8..24,7..23）深色字形 >5；
#           标题文本区（局部 100..500,8..24）深色字形 >20；
#         - CSD=0：同区域深色字形 <5（页面顶无条带）。
#      d. xprop -id 读 _MOTIF_WM_HINTS：CSD=1 decorations=0x0；
#         CSD=0 decorations 含 DECOR_ALL(0x1)。
#   3) 聚合 PASS/FAIL，FAIL>0 退出码 1。
#
# 【产物】out/titlebar_gate/<时间戳>/{csd1,csd0}/ 下日志与帧留档。
# 【环境基线（2026-09-29 本机）】Xvfb/xwd/xwdtopnm/ffmpeg 可用；
#   xcap/ppm2png/ImageMagick/PIL 不存在。
# =============================================================================
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT"
TS="$(date +%Y%m%d_%H%M%S)"
WORKROOT="out/titlebar_gate/$TS"
mkdir -p "$WORKROOT/csd1" "$WORKROOT/csd0"
PASS=0; FAIL=0

gate_pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
gate_fail() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); echo "$1" >> "$WORKROOT/fails.txt"; }

# 前置自检：工具链
for t in Xvfb xwd xwdtopnm ffmpeg xprop xdotool; do
  command -v "$t" >/dev/null || { echo "缺工具: $t"; exit 1; }
done

# ---------- 前置：二进制先拷 out/ ----------
pkill -9 -f XGuiWindowDemo 2>/dev/null   # 清上轮 demo（TERM 可能不被事件环及时处理）
sleep 0.5
cmake --build build --target XGuiWindowDemo_Test -j"$(nproc)" \
  > "$WORKROOT/build.log" 2>&1 || { echo "构建失败"; tail -20 "$WORKROOT/build.log"; exit 1; }
cp bin/XGuiWindowDemo_Test out/XGuiWindowDemo_Test

# ---------- Xvfb :98 ----------
pkill -f "Xvfb :98" 2>/dev/null   # 清上轮残留（本脚本自起自管的目验显示器）
sleep 0.5
rm -f /tmp/.X98-lock
if [ -f /tmp/.X98-lock ] && kill -0 "$(cat /tmp/.X98-lock 2>/dev/null)" 2>/dev/null; then
  echo ":98 被占，等待（最长 120s）..."
  for i in $(seq 1 60); do
    [ -f /tmp/.X98-lock ] || break; sleep 2
  done
fi
rm -f /tmp/.X98-lock
(Xvfb :98 -screen 0 1280x960x24 -nolisten tcp &>/dev/null &)
XVFB_PID=""
for i in $(seq 1 30); do
  [ -f /tmp/.X98-lock ] && break; sleep 0.5
done
sleep 1
DISPLAY=:98 xdpyinfo >/dev/null 2>&1 || {
  echo "Xvfb :98 未就绪（启动失败/被占用）"; tail -5 /tmp/tb_xvfb.log 2>/dev/null; exit 1
}

# raw rgb24 像素盒扫描：box(x,y,w,h) 内满足条件式的像素数（条件用 shell 求值 r g b）
scan_count() { # scan_count <raw文件> <x> <y> <w> <h> <imgw> <表达式(r,g,b)>
  local f="$1" bx="$2" by="$3" bw="$4" bh="$5" iw="$6" expr="$7"
  python3 - "$f" "$bx" "$by" "$bw" "$bh" "$iw" "$expr" <<'PYEOF'
import sys, struct
f, bx, by, bw, bh, iw, expr = sys.argv[1], *map(int, sys.argv[2:6]), int(sys.argv[6]), sys.argv[7]
data = open(f, 'rb').read()
stride = iw * 3
cnt = 0
for yy in range(by, by + bh):
    row = data[yy*stride + bx*3 : yy*stride + (bx+bw)*3]
    for xx in range(0, len(row), 3):
        r, g, b = row[xx], row[xx+1], row[xx+2]
        if eval(expr): cnt += 1
print(cnt)
PYEOF
}

run_mode() { # run_mode <1|0>
  local csd="${1:?缺模式参数}"
  local dir="$WORKROOT/csd$csd"
  echo "-- 模式 XGUI_CSD=$csd --"
  DISPLAY=:98 XGUI_CSD=$csd stdbuf -o0 ./out/XGuiWindowDemo_Test --page 1 \
    > "$dir/demo_stdout.log" 2>&1 &
  local demo_pid=$!
  local wid=""
  for i in $(seq 1 30); do
    wid=$(DISPLAY=:98 xdotool search --onlyvisible --name "XinYueC" 2>/dev/null | head -1)
    [ -n "$wid" ] && break; sleep 1
  done
  if [ -z "$wid" ]; then
    gate_fail "CSD=$csd 主窗未上屏"; kill $demo_pid 2>/dev/null; return
  fi
  # 等屏稳：连捕两帧 md5 一致
  local m1="" m2=""
  for i in $(seq 1 20); do
    DISPLAY=:98 xwd -id "$wid" -silent > "$dir/f1.xwd" 2>/dev/null
    sleep 1.2
    DISPLAY=:98 xwd -id "$wid" -silent > "$dir/f2.xwd" 2>/dev/null
    m1=$(md5sum < "$dir/f1.xwd" | cut -d' ' -f1)
    m2=$(md5sum < "$dir/f2.xwd" | cut -d' ' -f1)
    [ "$m1" = "$m2" ] && break
  done
  [ "$m1" = "$m2" ] && gate_pass "CSD=$csd 屏稳（连续两帧一致）" \
                    || gate_fail "CSD=$csd 屏未稳"
  # 转原始 rgb24（xwdtopnm → ffmpeg）
  xwdtopnm "$dir/f2.xwd" 2>/dev/null | ffmpeg -y -loglevel error \
    -i - -f rawvideo -pix_fmt rgb24 "$dir/frame.raw" 2>/dev/null
  # 像素断言：主窗客户区局部坐标（窗内 800 宽）
  local dark_hamburger dark_title
  dark_hamburger=$(scan_count "$dir/frame.raw" 8 7 16 16 800 'r<120 and g<120 and b<120')
  dark_title=$(scan_count "$dir/frame.raw" 100 8 400 16 800 'r<120 and g<120 and b<120')
  if [ "$csd" = "1" ]; then
    [ "$dark_hamburger" -gt 5 ] && gate_pass "CSD=1 条带 ☰ 钮字形在案（$dark_hamburger > 5）" \
                       || gate_fail "CSD=1 条带 ☰ 钮字形缺失（$dark_hamburger ≤ 5）"
    [ "$dark_title" -gt 20 ] && gate_pass "CSD=1 条带标题文本在案（$dark_title > 20）" \
                      || gate_fail "CSD=1 条带标题文本缺失（$dark_title ≤ 20）"
    local hints
    hints=$(DISPLAY=:98 xprop -id "$wid" _MOTIF_WM_HINTS 2>/dev/null | grep -oE '= [0-9x, a-f]+')
    case "$hints" in
      *" 0x0,"*|*" 0x0"*) gate_pass "CSD=1 原生装饰已抑制（decorations=0）$hints" ;;
      *) gate_fail "CSD=1 原生装饰未抑制：$hints" ;;
    esac
  else
    [ "$dark_hamburger" -lt 5 ] && gate_pass "CSD=0 无框架越权条带（☰ 区深色 $dark_hamburger < 5）" \
                       || gate_fail "CSD=0 出现框架越权条带（☰ 区深色 $dark_hamburger ≥ 5）"
    local hints
    hints=$(DISPLAY=:98 xprop -id "$wid" _MOTIF_WM_HINTS 2>/dev/null | grep -oE '= [0-9x, a-f]+')
    case "$hints" in
      *" 0x1,"*) gate_pass "CSD=0 装饰交 WM（DECOR_ALL）$hints" ;;
      *) gate_fail "CSD=0 装饰提示异常：$hints" ;;
    esac
  fi
  cp "$dir/f2.xwd" "$dir/frame.xwd" 2>/dev/null
  kill -9 $demo_pid 2>/dev/null   # demo 事件环不处理 TERM：一律 -9
  sleep 0.3
  echo "  [note] CSD=$csd demo 已停，留档 $dir"
}

run_mode 1
run_mode 0

pkill -f "Xvfb :98" 2>/dev/null
rm -f /tmp/.X98-lock
echo "== 汇总: PASS=$PASS FAIL=$FAIL（明细 $WORKROOT）=="
[ "$FAIL" -eq 0 ]
