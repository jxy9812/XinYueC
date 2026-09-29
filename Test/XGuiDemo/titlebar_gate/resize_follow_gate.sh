#!/usr/bin/env bash
# =============================================================================
# resize_follow_gate.sh —— CSD=1 标题条随窗改尺寸「实时跟随」目验门
#                            （+ CSD=0 回归快检一帧）
#
# 【门什么】XGUI_CSD=1 --page 1、Xvfb :98 无 WM 摆位 800x600@(40,40)
#   （xgui_window_demo.c:3344 setGeometry(40,40,800,600)，无 WM 时
#   X11 窗摆位=请求位）下：
#   A. 右缘(E)命中带步进拖拽：mousemove 到 (839,300)（窗内 (799,260)，
#      E 带内、无子控件）→ mousedown → 5 步 mousemove_relative --sync 各
#      +20px；每步屏稳抓帧断言「窗宽=800+20k 且 条带右缘≈当前窗宽」
#      ——中途帧必须断言（证明实时跟随而非松手后矫正）；mouseup 后复验
#      （900 宽保持、条带仍跟随）。
#   A2. 双方向角步进拖拽：按下 (839,40)，5 步各 (+20,+20)；每步断言
#      窗宽=800+20k / 窗高=600−20k / 窗顶缘 y=40+20k / 条带右缘跟随
#      ——N 向移动窗口原点，条钉顶层 (0,0) 的语义被真实考验。
#   B. 外部改尺寸（WM 等价）：xdotool windowsize 直改客户窗 900x700，
#      屏稳后断言条带跟随（state.md F-④「无 WM 外部 resize 陈旧呈现+
#      几何锁死」的回归探针）。
#   C. XGUI_CSD=0 回归快检一帧：页面顶无框架条带 + 装饰交 WM
#      （照 titlebar_gate.sh csd0 断言：☰ 区深色 <5 + DECOR_ALL）。
#
# 【SE→NE 场景替代（2026-09-29 裁定；非放宽，头注存档源码依据）】
#   任务书原拟 SE 角 (839,639) 按下，按源码推导必然无法武装改尺寸：
#   - XWindowDecoration.c:1364-1392：边缘带按下仅当 childAt 返回
#     空/顶层/条控件时才进入 m_resizing，否则按「子控件优先」放行
#     （XWindowDecoration.c:41-45 注释同旨：改尺寸只在无子控件接住的
#     空白区接管，等价桌面 WM 帧外命中）；
#   - xgui_window_demo.c:1469-1487（demo_layout_chrome，每次 resizeEvent
#     重排，VDemoWin_resizeEvent xgui_window_demo.c:2016-2024）：状态栏
#     子控件恒占 (0, h-26, w, 26) 全宽底部 26px——600 高时 rows 574..599
#     全覆盖，S 带（h-8=592..599）无一像素可武装；其上 rows 504..573
#     (x≥590) 又被性能悬浮层占（XPerformanceOverlay 210x70 右下锚定
#     xgui_window_demo.c:619-649；XWidget_childAt 不滤
#     TransparentForMouseEvents，XWidget.c:3975-4017 只跳过 Hidden/窗口型）；
#   - 唯一可武装的双方向角=NE：顶带 y<8 落在条控件上，条控件自身视作
#     空白（XWindowDecoration.c:1364-1367「N 向窄带照常接管」），
#     hitChild==m_bar 放行 → zone=E|N。
#   裁定：以 NE 内角 (+20,+20) 等价替代（双方向 mask + 条带随双边跟随
#   全保留）；SE 不写设计恒红的断言（恒红门禁是噪音）。
#   按下点自源码细化为 (839,40)（窗内 y=0）：裁定基准点 (839,47) 窗内
#   y=7 落 ✕ 钮矩形 [776..799]x[3..26]（按钮 24x24 钉 y=(30-24)/2=3、
#   Close 右起 x=w-24，XCommonStyle.c:2050-2094），会被按钮武装分支先
#   行吞掉（XWindowDecoration.c:1355-1361 hitTest 先于边缘带）甚至松手
#   误关窗；y=0 行在 N 带（y<8，XWindowDecoration.c:619）且在按钮簇外
#   （hitTestComplexControl 逐钮矩形判定，XCommonStyle.c:3855-3884，
#   (799,0) 不落任何钮/标签矩形 → SC_None）→ 边缘带照常接管。
#
# 【断言口径（全部自源码推导，出处随行标注）】
#   1. 条带高 barH=30：条控件高=XWidget_init 对子控件预置几何 100x30
#      （XWidget.c:2271-2272），默认条经 XTitleBar_create_ex→
#      XTitleBar_init→XWidget_init（XTitleBar.c:277-292）继承该预置；
#      装饰模块按条控件实际高度钉位 (0,0,宿主宽,30)（XWindowDecoration.c:
#      436-447 xwd_pinBarGeometry，仅 0 高才回退度量，xwd_stripHeight
#      同口径 412-420）；绘制矩形高=条控件真实几何（XTitleBar.c:
#      371-378 buildOption rectH=XWidget_height，「预置 100x30、度量
#      28」白缝修复注释在案）；RESIZE 唯一漏斗（XWidget.c:2865-2878）
#      尾部 XWindowDecoration_syncBarGeometry（XWindowDecoration.c:
#      1263）同帧重钉 → 抓图（客户窗=顶层 1:1）局部 row 29=条带末行
#      即规线行。XTitleBar_defaultHeight()=28（XTitleBar.c:336-349，
#      度量 XCommonStyle.c:3044-3049、XFusionStyle.c:386,406；样式链
#      XFusionStyle.c:356-368 回落 XCommonStyle，XStyle.c:693-701 缺省
#      样式）仅为未定尺寸兜底，非钉定值。
#   2. 条带像素签名=底部 1px 规线：xcs_drawTitleBar 在条矩形 (0,0,w,30)
#      末行 y=29 以 palette Dark 全宽 fillRect [0,w-1]（XCommonStyle.c:
#      2200-2207）。row 29 纯净性：按钮钮体 y=3..26、字形 rows 9..21
#      （钮体布局 XCommonStyle.c:2050-2094、字形 fill 2147-2180）；标题
#      文本垂直居中 ≈rows 7..23；demo 深蓝标题基底自 row sysbarH=30 起
#      （demo_sysbarH=XWindowDecoration_marginsFor().top=挂载条实际高，
#      xgui_window_demo.c:687-693，基底填充 :711）。
#      规线匹配窗 [134,184]（=159±25）：Dark=(159,159,159)（XPalette.c:64
#      浅色 Fusion），容得 RGB565 往返 ≈(156,158,156)；排除条底
#      Window=(239,239,239)（Δ80，XPalette.c:57,87）、demo 深蓝基底
#      (31,78,121)（r/b 出窗）、黑/白字形。配色组=Active（聚焦窗，
#      XTitleBar.c:382-384）；本机 COLOR_SCHEME 未设 → 浅色调色板
#      （Drive/Posix/Graphics/XPlatformTheme_posix.c:57 仅
#      COLOR_SCHEME=dark|prefer-dark 才深色；运行前实测为空）。
#   3. 边缘命中带 8px：XWD_RESIZE_ZONE=24 仅 fbdev 触屏口径，原生窗
#      已挂（Xvfb X11）收窄 8px（XWindowDecoration.c:47-49,605-622）
#      → E 带=全局 x∈[832,839]（窗 x 40..839），N 带=窗内 y∈[0,7]。
#   4. 步进期望几何恰值：xwd_applyResize 按「距按下点总位移」整数运算
#      （XWindowDecoration.c:888-954，E: width+=dx；N: y+=dy/height-=dy），
#      demo 未设最小尺寸（XWindow_minimumSize 兜底 1px，XWindow.c:1433）
#      、面板 1280x960 无边界钳制触发 → E 拖 宽=800+20k；NE 拖
#      宽=800+20k / 高=600−20k / y=40+20k / x=40 不变。
#   5. 容差：TOL_EDGE=3（条带右缘规线像素 vs 当前窗宽）——fillRect
#      精确覆盖 [0,w-1]、X11 客户窗=顶层控件 1:1 像素（titlebar_gate
#      局部坐标断言同前提）、xwd 抓图宽=当前 X11 窗宽；±3 只吸收抓图/
#      转换边界舍入，20px 级失跟随必红。TOL_STEP=1（中途步进窗宽等
#      期望 800+20k）——全链整数运算无舍入，±1 仅防读数瞬态。
#   6. CSD=0：页面顶 ☰ 区 (局部 8..24,7..23) 深色字形 <5 + xprop
#      _MOTIF_WM_HINTS 含 DECOR_ALL(0x1)（逐字照 titlebar_gate.sh:129-150
#      csd0 口径；demo 深蓝基底 b=121 恰在 <120 之外，不构成假阳）。
#
# 【重校准记录（2026-09-30）】RULE_ROW 27→29、断言口径 28→30：
#   依据一（源码链）：条控件经 XWidget_init 继承子控件预置几何 30 高
#   （XWidget.c:2271-2272→XTitleBar.c:277-292），xwd_pinBarGeometry 按
#   条控件实际高度钉位（XWindowDecoration.c:436-447），buildOption 以
#   控件几何高为绘制矩形（XTitleBar.c:371-378 白缝修复注释自证「预置
#   100x30、度量 28」）——样式度量 28（XCommonStyle.c:3044-3049）仅是
#   未定尺寸兜底；规线画在条矩形末行 height-1（XCommonStyle.c:
#   2200-2207）→ row 29。旧头注按度量链推 barH=28/row27，未计控件
#   预置几何这一环，出处失效。
#   依据二（运行档案）：out/titlebar_gate/resize_20260930_031251/
#   fails.txt 全帧「row27 规线 left=-1 right=-1 count=0」（row27 落条带
#   内部 Window 底色行，断言已失效恒红）；对同批存档帧 e_final.raw
#   （900px）按规线灰窗行扫实测 row29 left=0 right=899 count=900
#   （row26-28/30-31 全空）——规线实际就在 row29。
#   纪律：非放宽——旧 row27 恒空=断言死，row29 恢复全宽规线实证断言；
#   场景/步进/容差/匹配窗均不动；本次仅改 RULE_ROW 与头注出处行号
#   （重校准当日全部头注出处已按当前工作树源码逐一实读核对）。
#
# 【怎么跑】顺序（纪律照抄 titlebar_gate.sh）：
#   0) 工具自检；pkill -9 -f XGuiWindowDemo 清残留（具体模式匹配，
#      禁 pkill -x）；构建 XGuiWindowDemo_Test（构建门 :99 不触碰）；
#      二进制先拷 out/ 再以 out/ 路径运行（产物与源分离）。
#   1) 自起 Xvfb :98 1280x960x24 -nolisten tcp（被占则等待，不抢）。
#   2) 场景 A→A2→B 各自重新拉起 demo（XGUI_CSD=1 --page 1，stdbuf -o0
#      日志落盘），C 拉起 XGUI_CSD=0；每场景先断言基线（初始几何
#      800x600@(40,40) + 条带 ☰ 字形在案），再驱动/断言。
#   3) 屏稳判定：连捕两帧 xwd md5 一致才继续（Xvfb 空闲 PAINT 延迟
#      0.7~4s，禁止固定 sleep 猜测）；像素断言 xwd -id → xwdtopnm →
#      ffmpeg raw rgb24 → python3 逐字节行扫（titlebar_gate 同款手法，
#      盒内无 ImageMagick/PIL）。
#   4) 聚合 PASS/FAIL：FAIL>0 退出 1。断言失败即 FAIL——脚本自身不
#      放宽、不跳步、不造假变绿；单步失败记录后继续走完场景取全证据。
# 【产物】out/titlebar_gate/resize_<时间戳>/ 下：
#   build.log、fails.txt、csd1/{a_east,a2_ne,b_ext}/（每步
#   <前缀>.f1/f2.xwd 与 .ppm/.raw 帧、demo_stdout.log、geo.log）、csd0/。
# 【环境基线（2026-09-29 本机）】Xvfb/xwd/xwdtopnm/ffmpeg/xdotool/
#   python3 可用；xcap/ppm2png/ImageMagick/PIL 不存在；COLOR_SCHEME
#   未设（浅色 Fusion 调色板）。
# =============================================================================
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT"
TS="$(date +%Y%m%d_%H%M%S)"
WORKROOT="out/titlebar_gate/resize_$TS"
mkdir -p "$WORKROOT/csd1/a_east" "$WORKROOT/csd1/a2_ne" "$WORKROOT/csd1/b_ext" "$WORKROOT/csd0"
PASS=0; FAIL=0

gate_pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
gate_fail() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); echo "$1" >> "$WORKROOT/fails.txt"; }
note() { echo "  [note] $*" >&2; }   # 诊断细节走 stderr（约束文档纪律）

# ---------- 口径常量（推导见头注） ----------
WIN_X0=40; WIN_Y0=40; WIN_W0=800; WIN_H0=600   # demo 请求位 xgui_window_demo.c:3344
RULE_ROW=29                                     # 条带末行=条控件几何高(30)-1 规线行（2026-09-30 重校准，头注§1）
TOL_EDGE=3                                      # 条带右缘 vs 窗宽（头注 §5）
TOL_STEP=1                                      # 中途步进期望（头注 §5）
STEPS=5; STEP_PX=20                             # 任务书步进口径
E_PRESS_X=839; E_PRESS_Y=300                    # 窗内 (799,260)：E 带且无子控件
NE_PRESS_X=839; NE_PRESS_Y=40                   # 窗内 (799,0)：E|N 带且按钮簇外

# ---------- 前置自检：工具链 ----------
for t in Xvfb xwd xwdtopnm ffmpeg xprop xdotool python3 md5sum; do
  command -v "$t" >/dev/null || { echo "缺工具: $t"; exit 1; }
done

# ---------- 前置：二进制先拷 out/ ----------
pkill -9 -f XGuiWindowDemo 2>/dev/null   # 清上轮 demo（事件环不处理 TERM，一律 -9）
sleep 0.5
cmake --build build --target XGuiWindowDemo_Test -j"$(nproc)" \
  > "$WORKROOT/build.log" 2>&1 || { echo "构建失败"; tail -20 "$WORKROOT/build.log"; exit 1; }
cp bin/XGuiWindowDemo_Test out/XGuiWindowDemo_Test

# ---------- Xvfb :98（目验专用；被占则等待，不抢） ----------
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
for i in $(seq 1 30); do
  [ -f /tmp/.X98-lock ] && break; sleep 0.5
done
sleep 1
DISPLAY=:98 xdpyinfo >/dev/null 2>&1 || {
  echo "Xvfb :98 未就绪（启动失败/被占用）"; exit 1
}

# ---------- 像素工具（xwd→xwdtopnm→ffmpeg raw rgb24→python3 行扫） ----------
scan_count() { # scan_count <raw文件> <x> <y> <w> <h> <imgw> <表达式(r,g,b)>（titlebar_gate 同款）
  local f="$1" bx="$2" by="$3" bw="$4" bh="$5" iw="$6" expr="$7"
  python3 - "$f" "$bx" "$by" "$bw" "$bh" "$iw" "$expr" <<'PYEOF'
import sys
f, bx, by, bw, bh = sys.argv[1], *map(int, sys.argv[2:6])
iw, expr = int(sys.argv[6]), sys.argv[7]
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

pnm_size() { # pnm_size <ppm文件> -> "W H"（P6 头解析；抓图宽随窗实时变化，不可硬编码）
  python3 - "$1" <<'PYEOF'
import sys
f = open(sys.argv[1], 'rb')
assert f.read(2) == b'P6', 'not a P6 ppm'
toks = []
while len(toks) < 2:
    line = f.readline()
    if not line:
        raise SystemExit('bad ppm header')
    line = line.split(b'#')[0].strip()
    if line:
        toks.extend(line.split())
print(toks[0].decode(), toks[1].decode())
PYEOF
}

rule_stats() { # rule_stats <raw> <row> <imgw> -> "left right count"（规线灰 [134,184] 行扫）
  python3 - "$1" "$2" "$3" <<'PYEOF'
import sys
data = open(sys.argv[1], 'rb').read()
row, iw = int(sys.argv[2]), int(sys.argv[3])
lo, hi = 134, 184
stride = iw * 3
left = right = -1
cnt = 0
base = row * stride
for x in range(iw):
    o = base + x * 3
    r, g, b = data[o], data[o+1], data[o+2]
    if lo <= r <= hi and lo <= g <= hi and lo <= b <= hi:
        cnt += 1
        if left < 0:
            left = x
        right = x
print(left, right, cnt)
PYEOF
}

num_close() { # num_close <值> <期望> <容差>
  local d=$(( $1 - $2 )); [ "$d" -lt 0 ] && d=$(( -d ))
  [ "$d" -le "$3" ]
}

win_geo() { # win_geo <wid> -> G_W/G_H/G_X/G_Y（当前 X11 客户窗实时几何）
  local out
  out=$(DISPLAY=:98 xdotool getwindowgeometry --shell "$1" 2>/dev/null) || return 1
  eval "$out"
  G_W=${WIDTH:-}; G_H=${HEIGHT:-}; G_X=${X:-}; G_Y=${Y:-}
  [ -n "$G_W" ] && [ -n "$G_H" ]
}

# ---------- demo 生命周期（stdbuf -o0 日志落盘；-9 停止） ----------
DEMO_PID=""; WID=""
demo_launch() { # demo_launch <csd> <dir> -> 0 成功（WID/DEMO_PID 全局）
  local csd="$1" dir="$2" i
  WID=""
  pkill -9 -f XGuiWindowDemo 2>/dev/null
  sleep 0.5
  DISPLAY=:98 XGUI_CSD=$csd stdbuf -o0 ./out/XGuiWindowDemo_Test --page 1 \
    > "$dir/demo_stdout.log" 2>&1 &
  DEMO_PID=$!
  for i in $(seq 1 30); do
    WID=$(DISPLAY=:98 xdotool search --onlyvisible --name "XinYueC" 2>/dev/null | head -1)
    if [ -n "$WID" ] && DISPLAY=:98 xdotool getwindowgeometry "$WID" >/dev/null 2>&1; then
      return 0
    fi
    WID=""
    sleep 1
  done
  return 1
}
demo_stop() {
  [ -n "$DEMO_PID" ] && kill -9 "$DEMO_PID" 2>/dev/null
  DEMO_PID=""
  sleep 0.3
}

# ---------- 屏稳双帧 md5 判定（禁止固定 sleep 猜测） ----------
STABLE_W=""; STABLE_H=""
wait_stable() { # wait_stable <wid> <dir> <前缀> -> 0 稳 / 1 未稳；产物 <前缀>.{f1,f2}.xwd/.ppm/.raw
  local wid="$1" dir="$2" pfx="$3" i m1 m2
  STABLE_W=""; STABLE_H=""
  for i in $(seq 1 20); do
    DISPLAY=:98 xwd -id "$wid" -silent > "$dir/$pfx.f1.xwd" 2>/dev/null
    sleep 1.2
    DISPLAY=:98 xwd -id "$wid" -silent > "$dir/$pfx.f2.xwd" 2>/dev/null
    m1=$(md5sum < "$dir/$pfx.f1.xwd" 2>/dev/null | cut -d' ' -f1)
    m2=$(md5sum < "$dir/$pfx.f2.xwd" 2>/dev/null | cut -d' ' -f1)
    [ -n "$m1" ] && [ "$m1" = "$m2" ] || continue
    xwdtopnm "$dir/$pfx.f2.xwd" 2>/dev/null > "$dir/$pfx.ppm" || continue
    read -r STABLE_W STABLE_H <<< "$(pnm_size "$dir/$pfx.ppm")"
    [ -n "$STABLE_W" ] && [ -n "$STABLE_H" ] || continue
    ffmpeg -y -loglevel error -i "$dir/$pfx.ppm" -f rawvideo -pix_fmt rgb24 \
      "$dir/$pfx.raw" 2>/dev/null || return 1
    return 0
  done
  return 1
}

# ---------- 跟随断言三组：步进几何 / 抓图一致 / 条带右缘跟随 ----------
assert_follow() { # assert_follow <标记> <dir> <前缀> <期望W> <期望H> <期望X> <期望Y>
  local tag="$1" dir="$2" pfx="$3" expW="$4" expH="$5" expX="$6" expY="$7"
  local l r c
  if ! win_geo "$WID"; then
    gate_fail "$tag 窗几何读取失败"
    return 1
  fi
  # 组1：步进几何（窗宽=800+20k 恰值 ±1；x/y/高同理）
  if num_close "$G_W" "$expW" $TOL_STEP && num_close "$G_H" "$expH" 1 \
     && num_close "$G_X" "$expX" 1 && num_close "$G_Y" "$expY" 1; then
    gate_pass "$tag 几何 ${G_W}x${G_H}@(${G_X},${G_Y}) = 期望 ${expW}x${expH}@(${expX},${expY})（±1）"
  else
    gate_fail "$tag 几何 ${G_W}x${G_H}@(${G_X},${G_Y}) ≠ 期望 ${expW}x${expH}@(${expX},${expY})"
  fi
  # 组2：抓图与实时几何一致（陈旧呈现/未稳定即红）
  if num_close "$STABLE_W" "$G_W" 1 && num_close "$STABLE_H" "$G_H" 1; then
    gate_pass "$tag 抓图 ${STABLE_W}x${STABLE_H} 与实时窗一致（±1）"
  else
    gate_fail "$tag 抓图 ${STABLE_W}x${STABLE_H} 与实时窗 ${G_W}x${G_H} 不一致（陈旧呈现/未稳定）"
  fi
  # 组3：条带右缘跟随——row $RULE_ROW(29) 规线全宽 [0,窗宽-1]（±TOL_EDGE）
  read -r l r c <<< "$(rule_stats "$dir/$pfx.raw" $RULE_ROW "$STABLE_W")"
  echo "$tag geo=${G_W}x${G_H}@(${G_X},${G_Y}) img=${STABLE_W}x${STABLE_H} rule(l=$l r=$r c=$c)" \
    >> "$dir/geo.log"
  if [ "$l" -ge 0 ] && [ "$l" -le $TOL_EDGE ] \
     && [ "$r" -ge "$(( G_W - 1 - TOL_EDGE ))" ] && [ "$r" -le "$(( G_W - 1 + TOL_EDGE ))" ] \
     && [ "$c" -ge "$(( G_W - 2 * TOL_EDGE - 2 ))" ]; then
    gate_pass "$tag 条带右缘跟随：规线 x∈[$l,$r] ≈ [0,$((G_W-1))]（±$TOL_EDGE），规线像素 c=$c"
  else
    gate_fail "$tag 条带未随窗：row$RULE_ROW 规线 left=$l right=$r count=$c vs 窗宽 $G_W（期望 [0,$((G_W-1)) ±$TOL_EDGE]）"
  fi
}

# ---------- csd1 基线：条带在案 + 初始几何 800x600@(40,40) ----------
csd1_baseline() { # csd1_baseline <dir> <前缀>
  local dir="$1" pfx="$2" dark
  if ! wait_stable "$WID" "$dir" "$pfx"; then
    gate_fail "$pfx 基线屏未稳"
    return 1
  fi
  dark=$(scan_count "$dir/$pfx.raw" 8 7 16 16 "$STABLE_W" 'r<120 and g<120 and b<120')
  if [ "$dark" -gt 5 ]; then
    gate_pass "基线条带 ☰ 钮字形在案（$dark > 5）"
  else
    gate_fail "基线条带 ☰ 钮字形缺失（$dark ≤ 5，CSD=1 条带未上屏？）"
  fi
  assert_follow "基线" "$dir" "$pfx" $WIN_W0 $WIN_H0 $WIN_X0 $WIN_Y0
  return 0
}

# ==================== 场景 A：右缘(E)步进拖拽 ====================
scn_a_east() {
  local dir="$WORKROOT/csd1/a_east" k expW
  echo "-- 场景 A 右缘(E)步进拖拽（按下 $E_PRESS_X,$E_PRESS_Y，${STEPS} 步各 +${STEP_PX}px）--"
  if ! demo_launch 1 "$dir"; then gate_fail "A 主窗未上屏"; demo_stop; return; fi
  csd1_baseline "$dir" "e_k0" || { demo_stop; return; }
  DISPLAY=:98 xdotool mousemove --sync $E_PRESS_X $E_PRESS_Y
  DISPLAY=:98 xdotool mousedown 1
  for k in $(seq 1 $STEPS); do
    expW=$(( WIN_W0 + STEP_PX * k ))
    DISPLAY=:98 xdotool mousemove_relative --sync $STEP_PX 0
    if ! wait_stable "$WID" "$dir" "e_k$k"; then
      gate_fail "A 步进 k=$k 屏未稳（宽期望 $expW，中途帧断言缺失即失败）"
      continue
    fi
    assert_follow "A 步进 k=$k（中途帧）" "$dir" "e_k$k" "$expW" $WIN_H0 $WIN_X0 $WIN_Y0
  done
  DISPLAY=:98 xdotool mouseup 1
  if ! wait_stable "$WID" "$dir" "e_final"; then
    gate_fail "A 松手后屏未稳"
  else
    assert_follow "A 松手复验" "$dir" "e_final" \
      $(( WIN_W0 + STEP_PX * STEPS )) $WIN_H0 $WIN_X0 $WIN_Y0
  fi
  demo_stop
}

# ==================== 场景 A2：NE 双方向角步进拖拽（SE 替代，见头注） ====================
scn_a2_ne() {
  local dir="$WORKROOT/csd1/a2_ne" k expW expH expY
  echo "-- 场景 A2 NE 双方向角步进拖拽（按下 $NE_PRESS_X,$NE_PRESS_Y，${STEPS} 步各 +${STEP_PX},+${STEP_PX}）--"
  if ! demo_launch 1 "$dir"; then gate_fail "A2 主窗未上屏"; demo_stop; return; fi
  csd1_baseline "$dir" "n_k0" || { demo_stop; return; }
  DISPLAY=:98 xdotool mousemove --sync $NE_PRESS_X $NE_PRESS_Y
  DISPLAY=:98 xdotool mousedown 1
  for k in $(seq 1 $STEPS); do
    expW=$(( WIN_W0 + STEP_PX * k ))
    expH=$(( WIN_H0 - STEP_PX * k ))
    expY=$(( WIN_Y0 + STEP_PX * k ))
    DISPLAY=:98 xdotool mousemove_relative --sync $STEP_PX $STEP_PX
    if ! wait_stable "$WID" "$dir" "n_k$k"; then
      gate_fail "A2 步进 k=$k 屏未稳（期望 ${expW}x${expH}@y$expY，中途帧断言缺失即失败）"
      continue
    fi
    assert_follow "A2 NE 步进 k=$k（中途帧）" "$dir" "n_k$k" "$expW" "$expH" $WIN_X0 "$expY"
  done
  DISPLAY=:98 xdotool mouseup 1
  if ! wait_stable "$WID" "$dir" "n_final"; then
    gate_fail "A2 松手后屏未稳"
  else
    assert_follow "A2 NE 松手复验" "$dir" "n_final" \
      $(( WIN_W0 + STEP_PX * STEPS )) $(( WIN_H0 - STEP_PX * STEPS )) \
      $WIN_X0 $(( WIN_Y0 + STEP_PX * STEPS ))
  fi
  demo_stop
}

# ==================== 场景 B：外部改尺寸（WM 等价） ====================
scn_b_ext() {
  local dir="$WORKROOT/csd1/b_ext"
  echo "-- 场景 B 外部改尺寸（xdotool windowsize 900x700）--"
  if ! demo_launch 1 "$dir"; then gate_fail "B 主窗未上屏"; demo_stop; return; fi
  csd1_baseline "$dir" "b_k0" || { demo_stop; return; }
  DISPLAY=:98 xdotool windowsize "$WID" 900 700
  if ! wait_stable "$WID" "$dir" "b_final"; then
    gate_fail "B 外部改尺寸后屏未稳"
  else
    assert_follow "B 外部改尺寸" "$dir" "b_final" 900 700 $WIN_X0 $WIN_Y0
  fi
  demo_stop
}

# ==================== 场景 C：CSD=0 回归快检一帧（照 titlebar_gate csd0） ====================
scn_csd0() {
  local dir="$WORKROOT/csd0" dark hints
  echo "-- 场景 C XGUI_CSD=0 回归快检（页面顶无条带）--"
  if ! demo_launch 0 "$dir"; then gate_fail "C 主窗未上屏"; demo_stop; return; fi
  if ! wait_stable "$WID" "$dir" "c_stable"; then
    gate_fail "CSD=0 屏未稳"
    demo_stop
    return
  fi
  dark=$(scan_count "$dir/c_stable.raw" 8 7 16 16 "$STABLE_W" 'r<120 and g<120 and b<120')
  if [ "$dark" -lt 5 ]; then
    gate_pass "CSD=0 无框架越权条带（☰ 区深色 $dark < 5）"
  else
    gate_fail "CSD=0 出现框架越权条带（☰ 区深色 $dark ≥ 5）"
  fi
  hints=$(DISPLAY=:98 xprop -id "$WID" _MOTIF_WM_HINTS 2>/dev/null | grep -oE '= [0-9x, a-f]+')
  case "$hints" in
    *" 0x1,"*) gate_pass "CSD=0 装饰交 WM（DECOR_ALL）$hints" ;;
    *) gate_fail "CSD=0 装饰提示异常：$hints" ;;
  esac
  demo_stop
}

# ==================== 串行执行 ====================
scn_a_east
scn_a2_ne
scn_b_ext
scn_csd0

# ---------- 收尾（只杀自己起的进程：具体模式 + 自管 PID） ----------
pkill -9 -f XGuiWindowDemo 2>/dev/null
pkill -f "Xvfb :98" 2>/dev/null
rm -f /tmp/.X98-lock
echo "== 汇总: PASS=$PASS FAIL=$FAIL（明细 $WORKROOT）=="
[ "$FAIL" -eq 0 ]
