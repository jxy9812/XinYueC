#!/bin/bash
# 对话框族竖屏截图驱动（2026-10-26 竖屏适配战役）。
# 用法: xgui_dialog_portrait_shoot.sh <outdir>
# 流程: Xvfb 480x800 → 探针（单相位进程，argv=相位名）→ 等OPEN/READY →
#       xwd 按窗口名抓对话框 → 收口（静态阻塞相=xdotool windowclose；
#       实例 open 相=touch go 触发探针 quit-on-go）。
set -u
OUT="${1:?outdir}"
DISPLAY_NO="${DISPLAY_NO:-76}"
BIN="/home/xinyue/Code/XinYueC/bin/XDialogPortraitProbe"
PHASES="fileopen filesave dir input color progress msgbox"
# 相位名 → 对话框窗口标题（CSD 窗口的 WM_NAME，xwd -name 按此抓窗）
title_of() {
  case "$1" in
    fileopen) echo "打开文件" ;;
    filesave) echo "另存为" ;;
    dir)      echo "选择文件夹" ;;
    input)    echo "输入" ;;
    color)    echo "设置颜色" ;;
    progress) echo "" ;;
    msgbox)   echo "" ;;
  esac
}

mkdir -p "$OUT"
rm -f /tmp/xfdlg/go 2>/dev/null; mkdir -p /tmp/xfdlg

Xvfb :$DISPLAY_NO -screen 0 ${SCREEN_GEOM:-480x800}x24 -nolisten tcp >/dev/null 2>&1 &
XVFB=$!
sleep 1.2

for name in $PHASES; do
  rm -f /tmp/xfdlg/go
  DISPLAY=:$DISPLAY_NO "$BIN" "$name" ${WIN_GEOM_ARG:-} > "$OUT/probe-$name.log" 2>&1 &
  PROBE=$!
  ok=0
  for i in $(seq 1 60); do
    if grep -qE "(OPEN|READY) $name" "$OUT/probe-$name.log" 2>/dev/null; then ok=1; break; fi
    if ! kill -0 $PROBE 2>/dev/null; then break; fi
    sleep 0.1
  done
  if [ "$ok" != 1 ]; then echo "[shoot] TIMEOUT waiting $name"; kill -9 $PROBE 2>/dev/null; continue; fi
  sleep 0.9   # 渲染稳定
  TITLE=$(title_of "$name")
  if [ -n "$TITLE" ]; then
    DISPLAY=:$DISPLAY_NO xwd -name "$TITLE" -silent 2>/dev/null | xwdtopnm 2>/dev/null | pnmtopng 2>/dev/null > "$OUT/$name.png"
  else
    DISPLAY=:$DISPLAY_NO xwd -root -silent 2>/dev/null | xwdtopnm 2>/dev/null | pnmtopng 2>/dev/null > "$OUT/$name.png"
  fi
  echo "[shoot] $name -> $name.png ($(wc -c < "$OUT/$name.png") bytes)"
  # 收口：静态阻塞相（WM_NAME 乱码按名搜不到 → 枚举取 id 最大者
  # windowclose；WM 关闭→closeEvent→reject→exec 退出）；实例 open 相
  # （quit-on-go）= touch go。
  case "$name" in
    progress|msgbox)
      touch /tmp/xfdlg/go
      ;;
    *)
      WID=$(DISPLAY=:$DISPLAY_NO xdotool search --name "." 2>/dev/null | sort -n | tail -1)
      [ -n "$WID" ] && DISPLAY=:$DISPLAY_NO xdotool windowclose "$WID" 2>/dev/null
      for i in $(seq 1 40); do
        grep -q "DONE $name" "$OUT/probe-$name.log" 2>/dev/null && break
        kill -0 $PROBE 2>/dev/null || break
        sleep 0.1
      done
      ;;
  esac
  for i in $(seq 1 30); do kill -0 $PROBE 2>/dev/null || break; sleep 0.1; done
  kill -9 $PROBE 2>/dev/null
done

kill -9 $XVFB 2>/dev/null
echo "[shoot] done -> $OUT"
