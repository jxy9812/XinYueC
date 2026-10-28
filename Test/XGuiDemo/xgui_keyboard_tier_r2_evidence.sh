#!/bin/bash
# 目验 r2 取证：round-2 清单所列 9 案在**现行二进制**上的全量重拍。
# 背景：out/kb_adapt_1028/ 的 r2 清单产物 mtime=03:03~03:05（v4 修复落
# 地前），1280x800-docked.log 仍记 w=832 h=177（现行=684x264）——清单
# 为陈旧产物复述。本脚本逐案重摄至 out/kb_adapt_1028_r2/。
# 用法: xgui_keyboard_tier_r2_evidence.sh <outdir>
set -u
OUT="${1:-/home/xinyue/Code/XinYueC/out/kb_adapt_1028_r2}"
DEMO="/home/xinyue/Code/XinYueC/bin/XGuiWindowDemo_Test"
REPO="/home/xinyue/Code/XinYueC"
DISP="${DISP:-77}"
mkdir -p "$OUT"

# run_direct <name> <geom> [env...]：常规案（自截自退）。
run_direct() {
  local name="$1" geom="$2"; shift 2
  Xvfb :$DISP -screen 0 ${geom}x24 -nolisten tcp >/dev/null 2>&1 &
  local xv=$!
  sleep 1.0
  ( cd "$REPO" && env DISPLAY=:$DISP XGUI_CSD=0 "$@" timeout 40 \
      "$DEMO" --page 9 --screenshot "$OUT/$name.png" > "$OUT/$name.log" 2>&1 )
  sleep 0.3; kill $xv 2>/dev/null; wait $xv 2>/dev/null
  echo "== $name"
  grep -m1 "XKB-GEO widget" "$OUT/$name.log" 2>/dev/null || echo "(no geo)"
}

# run_click <name> <geom> <label-hex> [env...]：交互案（xdotool 点指定
# 键帽后 xwd 抓窗）。label-hex 为键帽 UTF-8 字节的 grep -P 转义（如 符
# =\xe7\xac\xa6、123=123）。
run_click() {
  local name="$1" geom="$2" label="$3"; shift 3
  Xvfb :$DISP -screen 0 ${geom}x24 -nolisten tcp >/dev/null 2>&1 &
  local xv=$!
  sleep 1.0
  ( cd "$REPO" && env DISPLAY=:$DISP XGUI_CSD=0 "$@" timeout 25 \
      "$DEMO" --page 9 > "$OUT/$name.log" 2>&1 ) &
  local demo=$!
  local wx wy kx ky
  for i in $(seq 1 50); do
    grep -q "XKB-GEO key" "$OUT/$name.log" 2>/dev/null && break
    sleep 0.2
  done
  # 键盘 widget 原点（宿主窗位于 (40,40)；dock 键盘 x=kbX,y=hostH-kbH）。
  wx=$(grep -m1 "^XKB-GEO widget" "$OUT/$name.log" | sed -n 's/.*x=\(-\?[0-9]*\).*/\1/p')
  wy=$(grep -m1 "^XKB-GEO widget" "$OUT/$name.log" | sed -n 's/.*y=\([0-9]*\).*/\1/p')
  # 目标键矩形中心（键盘局部坐标）。
  local line
  line=$(grep -m1 "label=$label\$" "$OUT/$name.log" | grep "^XKB-GEO key")
  kx=$(echo "$line" | sed -n 's/.*x=\([0-9]*\).*/\1/p')
  ky=$(echo "$line" | sed -n 's/.*y=\([0-9]*\).*/\1/p')
  local kw
  kw=$(echo "$line" | sed -n 's/.*w=\([0-9]*\).*/\1/p')
  echo "== $name click ($label) widget=($wx,$wy) key=($kx,$ky,$kw)"
  if [ -n "$wx" ] && [ -n "$kx" ]; then
    # 全局坐标钳到屏内（XTEST 对出屏 move 失败回退指针初值=屏心）；
    # 钳后仍落在目标键矩形内（行高 40~55px，出屏量 ≤40px）。
    local gx gy sw sh
    gx=$((40 + wx + kx + kw / 2))
    gy=$((40 + wy + ky + 20))
    sw=${geom%x*}; sh=${geom#*x}
    [ "$gx" -gt "$((sw - 1))" ] && gx=$((sw - 1))
    [ "$gy" -gt "$((sh - 1))" ] && gy=$((sh - 1))
    echo "   click global=($gx,$gy) (clamped to $geom)"
    DISPLAY=:$DISP xdotool mousemove "$gx" "$gy"
    sleep 0.2
    DISPLAY=:$DISP xdotool mousedown 1
    sleep 0.1
    DISPLAY=:$DISP xdotool mouseup 1
    sleep 1.2
  fi
  # 抓 demo 窗本体（xwd -root 对子控件后备库刷出不可靠，评审同款按窗抓）。
  local wid
  wid=$(DISPLAY=:$DISP xdotool search --onlyvisible --name "." 2>/dev/null | tail -1)
  if [ -n "$wid" ]; then
    DISPLAY=:$DISP xwd -id "$wid" -silent 2>/dev/null | xwdtopnm 2>/dev/null | \
      pnmtopng 2>/dev/null > "$OUT/$name.png"
  else
    DISPLAY=:$DISP xwd -root -silent 2>/dev/null | xwdtopnm 2>/dev/null | \
      pnmtopng 2>/dev/null > "$OUT/$name.png"
  fi
  kill $demo 2>/dev/null; wait $demo 2>/dev/null
  sleep 0.3; kill $xv 2>/dev/null; wait $xv 2>/dev/null
}

CAL="XKB_METRICS=auto XKB_FORCE_DPI=160"

# ---- r1 F1 同尺寸：1280x900 停靠（calibrated）+ legacy 现状对照 ----
run_direct 1280x900-docked_CAL 1280x900 $CAL XKB_FORCE_DEVICE=tablet \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=1280x900
run_direct 1280x900-docked_LEGACY_noenv 1280x900 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=1280x900

# ---- r1 F2 同尺寸：1280x900 紧凑（calibrated）+ legacy 现状对照 ----
run_direct 1280x900-float_CAL 1280x900 $CAL XKB_FORCE_DEVICE=tablet \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_PANEL=float XGUI_DEMO_WINDOW_SIZE=1280x900
run_direct 1280x900-float_LEGACY_noenv 1280x900 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_PANEL=float \
  XGUI_DEMO_WINDOW_SIZE=1280x900

# ---- r1 F3：360x640 符号布局（xdotool 点「符」切 Special）----
run_click 360x640-symbols 360x640 "符" $CAL \
  XKB_FORCE_DEVICE=phone XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 \
  XGUI_KB_LAYOUT=english XGUI_DEMO_WINDOW_SIZE=360x640

# ---- r1 F4：640x360 手机横屏停靠 ----
run_direct 640x360-docked 640x360 $CAL XKB_FORCE_DEVICE=phone \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=640x360

# ---- r1 F5：800x1280 平板竖屏 ----
run_direct 800x1280-docked 800x1280 $CAL XKB_FORCE_DEVICE=tablet \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=800x1280

# ---- r1 F6：1280x800 平板横屏停靠 ----
run_direct 1280x800-docked 1280x800 $CAL XKB_FORCE_DEVICE=tablet \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=1280x800

# ---- r1 F7：1280x800 紧凑 ----
run_direct 1280x800-compact 1280x800 $CAL XKB_FORCE_DEVICE=tablet \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_PANEL=float XGUI_DEMO_WINDOW_SIZE=1280x800

# ---- r1 F8：1280x800 符号布局（点「符」；Special 3x4 参考行 14 单位
#      → kbW=14×68+4=956 居中 x=162）----
run_click 1280x800-symbols 1280x800 "符" $CAL \
  XKB_FORCE_DEVICE=tablet XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 \
  XGUI_KB_LAYOUT=english XGUI_DEMO_WINDOW_SIZE=1280x800

# ---- r1 F9：1280x800 数字布局（点「123」；Number 最大行 5 单位 →
#      kbW=5×68+4=344 居中 x=468，setMode 即重定位根修）----
run_click 1280x800-number 1280x800 "123" $CAL XKB_FORCE_DEVICE=tablet \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=1280x800

echo "[r2-evidence] done -> $OUT"
