#!/bin/bash
# 虚拟键盘四档尺寸适配目验矩阵（2026-10-28 v3 定稿 §6；r3 §11 全表）。
# 用法: xgui_keyboard_tier_shoot.sh <outdir>
# 流程: 逐案 Xvfb（几何=案例宿主）→ bin/XGuiWindowDemo_Test（--page 9
#       --screenshot 自截自退 / 紧凑案 autoSeconds 存活+exit PID）→
#       解析 XKB-GEO 行做程序化几何断言 (tolerance ±2)；**紧凑悬浮案
#       窗矩形以 xwininfo 直读为准（教训⑦：紧凑窗=真 X 顶层，禁日志
#       推算/参照系偏移；XKB-GEO widget 行仅作框架侧对账）** → 可选
#       BASELINE_DIR 下的 #9 legacy 像素 diff。
# 环境约定: 全案换算 mm2px(mm10)=mm10×dpi/254、dp(px)=px×dpi/160；
#       除注明外 XKB_FORCE_DPI=160。XKB_METRICS 显式 auto（demo main
#       兜底会对「未设」置 legacy，故档位案必须显式 auto）。
# 坐标口径: 紧凑窗全局位=宿主窗全局位 (40,40)+宿主内偏移（demo 主窗
#       无 WM 直落请求位）；停靠键盘=宿主内子控件，XKB-GEO widget 行即
#       几何直录（非推算）。
set -u
OUT="${1:?outdir}"
DEMO="/home/xinyue/Code/XinYueC/bin/XGuiWindowDemo_Test"
REPO="/home/xinyue/Code/XinYueC"
BASELINE_DIR="${KB_TIER_BASELINE_DIR:-}"   # 含 9.png 的「改动前基线」目录
DISP_BASE="${DISP_BASE:-73}"
HOST_X=40   # demo 主窗全局位（无 WM；既有 #5/#6 期望值同口径）
HOST_Y=40
PASS=0
FAIL=0
mkdir -p "$OUT"

# expect_geo <log> <x> <y> <w> <h> <what>：XKB-GEO widget 行 ±2 容差。
expect_geo() {
  local log="$1" wx="$2" wy="$3" ww="$4" wh="$5" what="$6"
  local line ax ay aw ah
  line=$(grep -m1 "^XKB-GEO widget " "$log" 2>/dev/null)
  if [ -z "$line" ]; then
    echo "[tier][FAIL] $what: no XKB-GEO widget line"; FAIL=$((FAIL+1)); return
  fi
  ax=$(echo "$line" | sed -n 's/.*x=\(-\?[0-9]*\).*/\1/p')
  ay=$(echo "$line" | sed -n 's/.*y=\(-\?[0-9]*\).*/\1/p')
  aw=$(echo "$line" | sed -n 's/.*w=\([0-9]*\).*/\1/p')
  ah=$(echo "$line" | sed -n 's/.*h=\([0-9]*\).*/\1/p')
  if [ "$((ax>wx?ax-wx:wx-ax))" -le 2 ] && [ "$((ay>wy?ay-wy:wy-ay))" -le 2 ] \
     && [ "$((aw>ww?aw-ww:ww-aw))" -le 2 ] && [ "$((ah>wh?ah-wh:wh-ah))" -le 2 ]; then
    echo "[tier][PASS] $what (got x=$ax y=$ay w=$aw h=$ah)"; PASS=$((PASS+1))
  else
    echo "[tier][FAIL] $what (want x=$wx y=$wy w=$ww h=$wh, got $line)"; FAIL=$((FAIL+1))
  fi
}

# expect_metrics <log> <track> <device> <orient> <dpi> <keyW> <what>
expect_metrics() {
  local log="$1" trk="$2" dev="$3" ori="$4" dpi="$5" keyW="$6" what="$7"
  local line
  line=$(grep -m1 "^XKB-GEO metrics " "$log" 2>/dev/null)
  if [ -z "$line" ]; then
    echo "[tier][FAIL] $what: no XKB-GEO metrics line"; FAIL=$((FAIL+1)); return
  fi
  if echo "$line" | grep -q "track=$trk device=$dev orient=$ori dpi=$dpi " \
     && [ "$(( $(echo "$line" | sed -n 's/.*keyW=\([0-9]*\).*/\1/p') > keyW \
               ? $(echo "$line" | sed -n 's/.*keyW=\([0-9]*\).*/\1/p') - keyW \
               : keyW - $(echo "$line" | sed -n 's/.*keyW=\([0-9]*\).*/\1/p') ))" -le 2 ]; then
    echo "[tier][PASS] $what ($line)"; PASS=$((PASS+1))
  else
    echo "[tier][FAIL] $what (want track=$trk device=$dev orient=$ori dpi=$dpi keyW~$keyW, got $line)"; FAIL=$((FAIL+1))
  fi
}

# expect_keyshape <log> <wantW> <wantH> <what>：首键（i=0）w/h ±2，
# 教训①验收带换算（键面比 1.3~1.5 由期望值本身承载：53x38=1.39、
# 79x57=1.39）。
expect_keyshape() {
  local log="$1" ww="$2" wh="$3" what="$4"
  local line aw ah
  line=$(grep -m1 "^XKB-GEO key i=0 " "$log" 2>/dev/null)
  if [ -z "$line" ]; then
    echo "[tier][FAIL] $what: no XKB-GEO key line"; FAIL=$((FAIL+1)); return
  fi
  aw=$(echo "$line" | sed -n 's/.*w=\([0-9]*\).*/\1/p')
  ah=$(echo "$line" | sed -n 's/.*h=\([0-9]*\).*/\1/p')
  if [ "$((aw>ww?aw-ww:ww-aw))" -le 2 ] && [ "$((ah>wh?ah-wh:wh-ah))" -le 2 ]; then
    echo "[tier][PASS] $what (key ${aw}x${ah})"; PASS=$((PASS+1))
  else
    echo "[tier][FAIL] $what (want key ${ww}x${wh}, got: $line)"; FAIL=$((FAIL+1))
  fi
}

# expect_fraction <h> <hostH> <loPct> <hiPct> <what>：教训②占屏带。
expect_fraction() {
  local h="$1" hostH="$2" lo="$3" hi="$4" what="$5"
  local pct=$(( h * 100 / hostH ))
  if [ "$pct" -ge "$lo" ] && [ "$pct" -le "$hi" ]; then
    echo "[tier][PASS] $what (kbH=$h hostH=$hostH -> ${pct}% in [${lo},${hi}]%)"; PASS=$((PASS+1))
  else
    echo "[tier][FAIL] $what (kbH=$h hostH=$hostH -> ${pct}% NOT in [${lo},${hi}]%)"; FAIL=$((FAIL+1))
  fi
}

# xwininfo_has <disp> <W> <H> <X> <Y>：root 树直读是否存在 WxH+X+Y 的
# 顶层窗（紧凑悬浮 Popup；教训⑦地面真值）。
xwininfo_has() {
  local disp="$1" ww="$2" wh="$3" wx="$4" wy="$5"
  DISPLAY=:$disp timeout 5 xwininfo -root -tree 2>/dev/null \
    | grep -qE "[[:space:]]${ww}x${wh}\+${wx}\+${wy}[[:space:]]"
}

# expect_xwin <name> <W> <H> <X> <Y> <what>：run_case_alive 存活期
# xwininfo 直读结果断言（±0：X 服务器记账无舍入）。
expect_xwin() {
  local name="$1" ww="$2" wh="$3" wx="$4" wy="$5" what="$6"
  if [ "$(cat "$OUT/$name.xwin" 2>/dev/null)" = "YES" ]; then
    echo "[tier][PASS] $what (xwininfo ${ww}x${wh}+${wx}+${wy})"; PASS=$((PASS+1))
  else
    echo "[tier][FAIL] $what (xwininfo 直读无 ${ww}x${wh}+${wx}+${wy} 顶层窗)"; FAIL=$((FAIL+1))
  fi
}

# run_case <name> <geom> [env k=v ...]：起 Xvfb→demo 自截自退→收 Xvfb。
run_case() {
  local name="$1" geom="$2"; shift 2
  local disp=$((DISP_BASE)) log="$OUT/$name.log" png="$OUT/$name.png"
  Xvfb :$disp -screen 0 ${geom}x24 -nolisten tcp >/dev/null 2>&1 &
  local xv=$!
  sleep 1.0
  ( cd "$REPO" && env DISPLAY=:$disp XGUI_CSD=0 "$@" timeout 40 \
      "$DEMO" --page 9 --screenshot "$png" > "$log" 2>&1 )
  sleep 0.3
  kill $xv 2>/dev/null; wait $xv 2>/dev/null
  if [ ! -s "$png" ]; then
    echo "[tier][FAIL] $name: no screenshot"; FAIL=$((FAIL+1))
  fi
}

# run_case_alive <name> <geom> <seconds> <W> <H> <X> <Y> [env k=v ...]：
# 存活形态（无 --screenshot；demo 按位置参数秒数自退）——**存活期内**
# 轮询 xwininfo 直读紧凑窗矩形（教训⑦：X 服务器真值，Xvfb 收掉后查不
# 到）。命中记 $OUT/$name.xwin=YES/NO。subshell 内 exec 让 $! 即 demo
# 进程，收尾 kill PID（禁 pkill -f；demo 已自退时 kill 为无害 no-op）。
run_case_alive() {
  local name="$1" geom="$2" secs="$3" ww="$4" wh="$5" wx="$6" wy="$7"; shift 7
  local disp=$((DISP_BASE)) log="$OUT/$name.log" i=0 found=0
  Xvfb :$disp -screen 0 ${geom}x24 -nolisten tcp >/dev/null 2>&1 &
  local xv=$!
  sleep 1.0
  ( cd "$REPO" && exec env DISPLAY=:$disp XGUI_CSD=0 "$@" \
      timeout $((secs+20)) "$DEMO" --page 9 "$secs" > "$log" 2>&1 ) &
  local dm=$!
  # 存活期轮询（demo 启动+钩子注入需 1~2s；上限 secs 内每 0.5s 一查）。
  while [ "$i" -lt $((secs*2)) ]; do
    if xwininfo_has "$disp" "$ww" "$wh" "$wx" "$wy"; then found=1; break; fi
    sleep 0.5
    i=$((i+1))
  done
  if [ "$found" -eq 1 ]; then echo YES > "$OUT/$name.xwin"
  else echo NO > "$OUT/$name.xwin"; fi
  kill $dm 2>/dev/null; wait $dm 2>/dev/null
  kill $xv 2>/dev/null; wait $xv 2>/dev/null
}

# ==================== 案例矩阵（r3 §11；期望值按公式现算） ====================
# 1) 480x960 phone 竖屏停靠 PinyinFull：h=205（mm2px(326)=205）全宽；
#    键 47x40 近方（rowH=(205-4-40)/4=40，全键行1 单位键=476/10≈47）。
run_case 1_phone_480x960_docked 480x960 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=480x960
expect_geo "$OUT/1_phone_480x960_docked.log" 0 755 480 205 \
  "#1 480x960 phone docked h=205 全宽"
expect_metrics "$OUT/1_phone_480x960_docked.log" calibrated phone portrait 160 47 \
  "#1 metrics calibrated/phone/portrait/160 keyW~47"

# 2) 960x480 phone 横屏停靠（r3）：h=188（mm2px(300)=300×160/254=
#    188.97→188，教训①增高一档）；参考行定宽 10×53+4=534 居中 x=213；
#    键 53x38=1.39 入带 1.3~1.5。
run_case 2_phone_960x480_docked 960x480 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=960x480
expect_geo "$OUT/2_phone_960x480_docked.log" 213 292 534 188 \
  "#2 960x480 phone landscape docked h=188 定宽 534 居中（r3 键比 7/5）"
expect_keyshape "$OUT/2_phone_960x480_docked.log" 53 38 \
  "#2 键面比 53x38=1.39 ∈[1.3,1.5]（教训①门禁）"

# 3) 800x1280 tablet 竖屏（r3）：全宽 w=800 x=0、h=384（mm2px(600)=377
#    被域钳下界 1280×30%=384 抬入带，占屏 30.0%）。
run_case 3_tablet_800x1280_docked 800x1280 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=800x1280
expect_geo "$OUT/3_tablet_800x1280_docked.log" 0 896 800 384 \
  "#3 800x1280 tablet portrait 全宽 w=800 h=384（r3 平板竖 600mm10+域钳）"
expect_fraction 384 1280 30 36 \
  "#3 占屏比 384/1280=30% ∈[30,36]%（教训②门禁）"

# 4) 1280x800 tablet 横屏文本盘（EnglishFull，r3）：h=264（域钳带内
#    33%）；参考行 10×79+4=794 居中 x=243；键 79x57=1.39。
run_case 4_tablet_1280x800_english 1280x800 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=1280x800
expect_geo "$OUT/4_tablet_1280x800_english.log" 243 536 794 264 \
  "#4 1280x800 tablet landscape 文本盘定宽 794 居中 x=243 h=264（r3 删封宽下限）"
expect_keyshape "$OUT/4_tablet_1280x800_english.log" 79 57 \
  "#4 键面比 79x57=1.39 ∈[1.3,1.5]（教训①门禁；窄盘独立定宽见⑧ 断言族）"

# 5) 960x480 phone 紧凑悬浮（PANEL=float 经工具栏槽 0 真路径切换，r3）：
#    compactW=528（55%）；行数键驱动高 4+32+4×37=184（横带 dp32，4 行
#    款——LAYOUT=english 对齐设计基线；缺省时 demo 默认框落 User1 引
#    擎表 5 行 44 键，行数驱动高公式给出 221 系公式正确行为）。
#    窗位=宿主全局右下（宿主窗在 (40,40)）：x=40+960-528=472、
#    y=40+480-184=336。xwininfo 直读窗矩形在宿主内（教训⑦门禁）。
run_case_alive 5_phone_960x480_compact 960x480 7 528 184 472 336 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_PANEL=float XGUI_DEMO_WINDOW_SIZE=960x480
expect_geo "$OUT/5_phone_960x480_compact.log" 472 336 528 184 \
  "#5 960x480 phone compact 528x184 行数键驱动高（框架侧对账）"
expect_xwin 5_phone_960x480_compact 528 184 472 336 \
  "#5 xwininfo 直读 528x184+472+336 且在宿主内（教训⑦门禁）"

# 6) 1280x800 tablet 紧凑（r3）：compactW=704（55%）、行数键驱动高
#    4+32+4×40=196；x=40+1280-704=616、y=40+800-196=644。
run_case_alive 6_tablet_1280x800_compact 1280x800 7 704 196 616 644 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_PANEL=float XGUI_DEMO_WINDOW_SIZE=1280x800
expect_geo "$OUT/6_tablet_1280x800_compact.log" 616 644 704 196 \
  "#6 1280x800 tablet compact 704x196 行数键驱动高（框架侧对账）"
expect_xwin 6_tablet_1280x800_compact 704 196 616 644 \
  "#6 xwininfo 直读 704x196+616+644 且在宿主内（教训⑦门禁）"

# 7) 480x960 phone T9：条高=dp(40)=40（竖屏带不变；键区起点 y=42）；
#    图标盒 dp 上限 26（首键=左列标点键，keyW 取行单位铺满值，仅目验）。
run_case 7_phone_480x960_t9 480x960 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=t9 \
  XGUI_DEMO_WINDOW_SIZE=480x960
line=$(grep -m1 "^XKB-GEO key i=0 " "$OUT/7_phone_480x960_t9.log")
if echo "$line" | grep -q "y=42 "; then
  echo "[tier][PASS] #7 T9 条高 dp(40)=40（键区起点 y=42）"; PASS=$((PASS+1))
else
  echo "[tier][FAIL] #7 T9 键区起点 expect y=42, got: $line"; FAIL=$((FAIL+1))
fi

# 8) 1280x800 tablet（r3）：与 #4 同式（794x264 居中）——r3 删平板 50%
#    封宽下限后出图=自然单位式宽度两侧留白（各 243px）；窄盘三连拍
#    （独立定宽近方键 71x57，XKeyboardTest ⑨ 节 288/359/430 程序化
#    断言，教训③门禁）。
run_case 8_tablet_1280x800 1280x800 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=1280x800
expect_geo "$OUT/8_tablet_1280x800.log" 243 536 794 264 \
  "#8 1280x800 tablet 文本盘出图（窄盘独立定宽=XKeyboardTest ⑨ 节覆盖）"

# 8b) 480x960 phone 默认框出图（焦点恒默认框；窄盘行为变化核验=
#     XKeyboardTest ⑨ 节 ⑧b 断言 204/138 已过）。
run_case 8b_phone_480x960 480x960 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=480x960
expect_geo "$OUT/8b_phone_480x960.log" 0 755 480 205 \
  "#8b 480x960 默认框出图（窄盘断言在 XKeyboardTest ⑨ 节）"

# 9) 800x600 legacy（脚本显式 legacy + demo main 兜底双保险）：与改动前
#    基线（KB_TIER_BASELINE_DIR/9.png，HEAD 键盘模块产出）逐像素 diff。
#    教训⑥：legacy 轨豁免搜狗比例标准（本几何断言即现状公式零漂移锁）。
run_case 9_legacy_800x600 800x600 \
  XKB_METRICS=legacy XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 \
  XGUI_DEMO_WINDOW_SIZE=800x600
expect_geo "$OUT/9_legacy_800x600.log" 0 300 800 300 \
  "#9 legacy 800x600 停靠 h=600/2=300（现状公式零漂移，教训⑥豁免口径）"
if [ -n "$BASELINE_DIR" ] && [ -s "$BASELINE_DIR/9.png" ]; then
  # 解码像素级比对（PNG 容器字节因 zlib 压缩/filter 选择可不同——
  # 同一二进制两次运行亦然；判据=解码后 RGB 逐像素相等）。
  if python3 - "$BASELINE_DIR/9.png" "$OUT/9_legacy_800x600.png" <<'PYEOF'
import sys, zlib, struct
def pixels(p):
    d=open(p,'rb').read(); pos=8; idat=b''
    while pos<len(d):
        ln,typ=struct.unpack('>I4s',d[pos:pos+8]); data=d[pos+8:pos+8+ln]
        if typ==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',data[:10])
        if typ==b'IDAT': idat+=data
        pos+=12+ln
    bpp=4 if ct==6 else 3
    raw=zlib.decompress(idat); stride=w*bpp
    out=bytearray(); prev=bytearray(stride); pos=0
    for y in range(h):
        f=raw[pos]; pos+=1
        line=bytearray(raw[pos:pos+stride]); pos+=stride
        if f==1:
            for i in range(bpp,stride): line[i]=(line[i]+line[i-bpp])&0xff
        elif f==2:
            for i in range(stride): line[i]=(line[i]+prev[i])&0xff
        elif f==3:
            for i in range(stride):
                a=line[i-bpp] if i>=bpp else 0
                line[i]=(line[i]+((a+prev[i])>>1))&0xff
        elif f==4:
            for i in range(stride):
                a=line[i-bpp] if i>=bpp else 0
                b=prev[i]; c=prev[i-bpp] if i>=bpp else 0
                pp=a+b-c; pa=abs(pp-a); pb=abs(pp-b); pc=abs(pp-c)
                pr=a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[i]=(line[i]+pr)&0xff
        out+=line; prev=line
    return w,h,bpp,bytes(out)
w1,h1,b1,g1=pixels(sys.argv[1]); w2,h2,b2,g2=pixels(sys.argv[2])
sys.exit(0 if (w1,h1)==(w2,h2) and g1==g2 else 1)
PYEOF
  then
    echo "[tier][PASS] #9 legacy 解码像素 diff 为空（与改动前基线一致）"; PASS=$((PASS+1))
  else
    echo "[tier][FAIL] #9 legacy 解码像素 diff 非空（baseline=$BASELINE_DIR/9.png）"; FAIL=$((FAIL+1))
  fi
else
  echo "[tier][SKIP] #9 像素 diff（未设 KB_TIER_BASELINE_DIR，几何断言已过）"
fi

# 10) 320x240 phone 极小宿主（demo env 宽下限 320；r3）：横屏 cap
#     240×50%=120 绑定（target 188 先被绑）→ h=120 y=120；定宽
#     rowH=(116-32)/4=21、键 29、10×29+4=294 居中 x=13。480 宽以下
#     极小宿主由 XKeyboardTest ⑨ 节 hostH=200/150/31 真值表覆盖。
run_case 10_phone_320x240 320x240 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=320x240
expect_geo "$OUT/10_phone_320x240.log" 13 120 294 120 \
  "#10 320x240 极小宿主 h=120（cap 绑定）参考行定宽 294 居中不崩"

# 12) 480x960 FORCE_DPI=403：target=326×403/254=517 → cap 960×46%=441 绑
#     （r3 竖屏零漂移：phone 竖档未动）。
run_case 12_phone_480x960_dpi403 480x960 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=403 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=480x960
expect_geo "$OUT/12_phone_480x960_dpi403.log" 0 519 480 441 \
  "#12 403dpi h=441（cap 960×46%=441 绑定，竖屏零漂移）"

# 13) 1280x900 平板横屏（r3 域钳下界案）：h=270（base 264 被域钳
#     hostH×30%=270 抬入带）定宽 10×81+4=814 居中 x=233。
run_case 13_tablet_1280x900_docked 1280x900 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_DEMO_WINDOW_SIZE=1280x900
expect_geo "$OUT/13_tablet_1280x900_docked.log" 233 630 814 270 \
  "#13 1280x900 tablet docked 定宽 814 居中 x=233 h=270（域钳下界抬）"

# 14) 1280x900 平板紧凑（r3）：704x196 行数键驱动高（4 行款，LAYOUT=
#     english 对齐设计基线）；x=616、y=40+900-196=744。xwininfo 直读。
run_case_alive 14_tablet_1280x900_compact 1280x900 7 704 196 616 744 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_PANEL=float XGUI_DEMO_WINDOW_SIZE=1280x900
expect_geo "$OUT/14_tablet_1280x900_compact.log" 616 744 704 196 \
  "#14 1280x900 tablet compact 704x196（框架侧对账）"
expect_xwin 14_tablet_1280x900_compact 704 196 616 744 \
  "#14 xwininfo 直读 704x196+616+744 且在宿主内（教训⑦门禁）"

# 15) 600x960 tablet 竖屏域钳上界案（r3 新增）：h=345（mm2px(600)=377
#     被 960×36%=345 上界绑，占屏 35.9% ∈[30,36]%）。
run_case 15_tablet_600x960_clamp 600x960 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=tablet XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_DEMO_WINDOW_SIZE=600x960
expect_geo "$OUT/15_tablet_600x960_clamp.log" 0 615 600 345 \
  "#15 600x960 tablet portrait 域钳上界 h=345（36% 绑）"
expect_fraction 345 960 30 36 \
  "#15 占屏比 345/960≈35.9% ∈[30,36]%（教训②门禁）"

# 16) 480x960 phone 竖屏 Special 精简款（r3 新增，教训④门禁）：
#     LAYOUT=english 先落款型主表（默认框 User1 引擎表无「符」键），
#     XGUI_KB_MODE=special 真路径点「符」切 Special → 手机竖屏命中
#     精简款（5 行 10 列 42 键）；断言=rows 行 + 末键右缘 ≤478 +
#     单位键宽 ≥47px（47px=7.4mm）。
run_case 16_phone_480x960_special 480x960 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_MODE=special XGUI_DEMO_WINDOW_SIZE=480x960
log16="$OUT/16_phone_480x960_special.log"
if grep -q "^XKB-GEO rows=5 buttons=42$" "$log16"; then
  echo "[tier][PASS] #16 Special 精简款 rows=5 buttons=42（10 列）"; PASS=$((PASS+1))
else
  echo "[tier][FAIL] #16 Special 精简款 rows/buttons 不是 5/42: $(grep -m1 '^XKB-GEO rows=' "$log16")"; FAIL=$((FAIL+1))
fi
awk '
  /^XKB-GEO key / {
    w=""; h=""; x="";
    for (i = 1; i <= NF; i++) {
      if (substr($i,1,2)=="x=") x=substr($i,3);
      if (substr($i,1,2)=="w=") w=substr($i,3);
      if (substr($i,1,2)=="h=") h=substr($i,3);
    }
    if (w=="" || h=="" || x=="") next;
    # substr 产出是字符串（非 strnum）——"105"<"90"/"105"<"47" 按字典序
    # 为真，min 会走下游。显式 +0 转数值再比较。
    x+=0; w+=0; h+=0;
    r=x+w; if (r>maxr) maxr=r;
    if (w<90 && (minw==0 || w<minw)) minw=w;
  }
  END {
    ok = (maxr>0 && maxr<=478 && minw>=47);
    print (ok ? "TIER16OK maxRight=" maxr " minUnitW=" minw \
              : "TIER16BAD maxRight=" maxr " minUnitW=" minw);
  }' "$log16" > "$OUT/16_scan.txt"
if grep -q "^TIER16OK" "$OUT/16_scan.txt"; then
  echo "[tier][PASS] #16 末键右缘≤478 且单位键宽≥47 ($(cat "$OUT/16_scan.txt"))"; PASS=$((PASS+1))
else
  echo "[tier][FAIL] #16 键区扫描越缘/过窄 ($(cat "$OUT/16_scan.txt"))"; FAIL=$((FAIL+1))
fi

# 17) 480x960 compact 内点「符」切 Special 随动（r3 新增，教训⑤/⑦
#     门禁）：LAYOUT=english（4 行）→ PANEL=float 紧凑 396x156 →
#     COMPACT_MODE=special 真路径点「符」切 Special → 精简款 5 行 →
#     xkb_compactFloatResize 行数驱动 396x156→396x184 + min 钳入宿主
#     y=40+960-184=816（底缘 816+184=1000=宿主底）。xwininfo 直读窗
#     矩形 (124,816,396,184)=局部 (84,776)+宿主全局 (40,40)——高度与
#     y 同时变化即随动+钳入宿主的真窗实证。
run_case_alive 17_phone_480x960_compact_special 480x960 8 396 184 124 816 \
  XKB_METRICS=auto XKB_FORCE_DEVICE=phone XKB_FORCE_DPI=160 \
  XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 XGUI_KB_LAYOUT=english \
  XGUI_KB_PANEL=float XGUI_KB_COMPACT_MODE=special \
  XGUI_DEMO_WINDOW_SIZE=480x960
expect_geo "$OUT/17_phone_480x960_compact_special.log" 124 816 396 184 \
  "#17 compact 内切 Special 随动 396x184（框架侧对账：随动+钳入宿主）"
expect_xwin 17_phone_480x960_compact_special 396 184 124 816 \
  "#17 xwininfo 直读 396x184+124+816（底缘 1000=宿主底，教训⑦门禁）"

# #11/#19 overlay（对话框外置浮层）：demo 无对话框编辑框无头入口，浮层
# 几何由 XKeyboardTest 既有对话框浮层断言族（legacy 轨逐值锁，⑦ 节）
# 与 r3 §5.2 m_hostW/H=scope 同步（bonus #19 无头入口缺席时如实 SKIP，
# 沿用 #11 先例）；本脚本不出图（如实声明）。
echo "[tier][SKIP] #11 overlay：无头入口缺席，回归套件浮层断言族覆盖"
echo "[tier][SKIP] #19 bonus overlay 精简款：demo 无对话框无头入口（与 #11 同因，如实 SKIP）"

echo "[tier] pass=$PASS fail=$FAIL -> $OUT"
[ "$FAIL" -eq 0 ]
