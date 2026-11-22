#!/bin/bash
# =============================================================================
# GPU 唯一渲染器重构 —— 阶段验收闭环
#
# 用法:
#   Tools/windows/gpu_stage_verify.sh [stage] [--no-build] [--quick]
#
#     stage   p1 | p2 | p3 | all（缺省 p1）
#     --no-build   跳过构建（只跑验证，用于回归对比）
#     --quick      基准时长为 3s（缺省 5s）
#
# 做了什么（每一步都实测，不用文档结论代替）:
#   1. 构建三个验收目标（MSVC 环境经 gpu_build_targets.bat 加载）
#   2. 离屏逐像素对照：SW/GL/VK 三后端并行 dump，再逐像素 diff
#   2b. AA 线段（M4-1，GL opt-in XGPU_AA_LINE=1）：感知对照（平均墨量
#       ≤1% + 逐通道 >8 占比 ≤20%）+ AA 关闭基线逐位对照
#   3. 渲染到纹理自检（GL/VK 两后端）
#   4. 窗口截图对照：SW 与 GL 的 PNG 字节数须同量级（抓「swap 上屏后
#      截图读到陈旧 CPU」这类回归）
#   5. 图表页截图（静态层纹理复用后画面完整）
#   6. 子树静态层（M6）：三后端自检（builds==1 / hits≥1 / 软件 A/B
#      逐位）+ 缓存命中帧页面像素跨后端逐像素 diff + 整帧性能 A/B 记录
#   7. 性能基准：三后端 × 增量/整帧，与阶段基线比较
#   8. 呈现计数：present>0 / readback≈0（零回读口径）
#   9. CPU 光栅预算：全页 0~16 逐页，ab 与 route 都必须 0
#  10. 冒烟：三后端 XGuiGpu_Test
#
# 退出码: 0=全绿  1=有失败项  2=环境/构建错误
#
# 基线来源: 2026-11-21 P1 落地实测（1000x700 图表页 / Ryzen 9 5950X /
#  AMD RX 6800 XT / console 会话）。换机或换页请同步改下面的阈值常量。
# =============================================================================
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 2

STAGE="${1:-p1}"
shift 2>/dev/null || true
NO_BUILD=0
DUR=5
for a in "$@"; do
    case "$a" in
        --no-build) NO_BUILD=1 ;;
        --quick)    DUR=3 ;;
    esac
done

BIN="$ROOT/bin"
# PowerShell Start-Process 只认 Windows 路径：Git Bash 的 ROOT 是
# /d/code/... 形式，直接拼进 -ArgumentList 会解析成 D:\d\code\...（构建
# 步骤 exit=1 且 b.out 为空——2026-11-22 实测）。统一转成盘符路径。
ROOT_WIN="$(cygpath -w "$ROOT" 2>/dev/null || echo "$ROOT")"
TMP="${TMPDIR:-/tmp}/gpu_verify.$$"
mkdir -p "$TMP" || exit 2
# 同上：-RedirectStandardOutput 的路径也要 Windows 形式（PowerShell 把
# /tmp/... 当成当前盘根下的 \tmp\...，b.out 实际没落到 bash 的 $TMP，
# 构建失败报告因此只剩空日志）。
TMP_WIN="$(cygpath -w "$TMP" 2>/dev/null || echo "$TMP")"
OUTDIR="$ROOT/out/gpu_verify"
mkdir -p "$OUTDIR" 2>/dev/null

TS="$(date +%Y%m%d-%H%M%S)"
REPORT="$OUTDIR/${STAGE}_${TS}.md"

# ---- 阈值（P1 实测基线；阶段推进时按需上调）----
# 性能下限是「抗负载护栏」而非性能目标：本机常与其他应用共用（实测
# msedge/游戏占 30% CPU 时三后端同步下降 10~30%，包括从未改动的软件
# 光栅）。护栏取空载基线的 ~70%，只用于抓「成倍退化」；像素/RT/截图
# 门禁才是正确性判据。空载实测：SW 1490-1520 / GL 670-760 / VK 222-240。
HARD_PIX_MAX=82          # 软件 vs GPU 硬差异像素上限（AA 量化差，与 GL 持平）
SW_FPS_MIN=1000          # 软件增量下限（空载 1470+）
GL_FPS_MIN=450           # GL 增量下限（空载 670+）
VK_FPS_MIN_P1=350        # VK 增量下限（空载 517-565；顶点缓冲扩容前仅 231）
READBACK_MAX=30          # 每 5s readback 次数上限（零回读口径）
SHOT_RATIO_MAX=2         # 三后端截图字节数比值上限（超了说明画面缺失）

FAIL=0
declare -a ROWS=()

note() { ROWS+=("$1"); echo "$1"; }
fail() { note "- ❌ $1"; FAIL=1; }
pass() { note "- ✅ $1"; }

# ---------- 1. 构建 ----------
if [ "$NO_BUILD" -eq 0 ]; then
    echo "=== [1/10] 构建 ==="
    LOG="$TMP/build.log"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
      "& { \$p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','$ROOT_WIN\\Tools\\windows\\gpu_build_targets.bat' -Wait -NoNewWindow -PassThru -RedirectStandardOutput '$TMP_WIN\\b.out' -RedirectStandardError '$TMP_WIN\\b.err'; exit \$p.ExitCode }" \
      >/dev/null 2>&1
    RC=$?
    cp "$TMP/b.out" "$LOG" 2>/dev/null
    cat "$TMP/b.err" >> "$LOG" 2>/dev/null
    if [ "$RC" -ne 0 ]; then
        echo "=== 构建失败，错误摘要 ==="
        iconv -f GBK -t UTF-8 "$LOG" 2>/dev/null | grep -aE "error C[0-9]+|error LNK|fatal error" | head -12
        note "# GPU 阶段验收 ${STAGE} (${TS})"
        note ""
        note "## 结论：构建失败，未进入验证"
        fail "构建（exit=$RC）"
        printf '%s\n' "${ROWS[@]}" > "$REPORT"
        echo "报告: $REPORT"
        exit 2
    fi
    pass "构建（三目标）"
else
    pass "构建（已跳过 --no-build）"
fi

# ---------- 2. 离屏逐像素 ----------
echo "=== [2/10] 逐像素对照 ==="
rm -f "$TMP"/px_*.raw
XGUI_RENDER_BACKEND=software timeout 120 "$BIN/XGuiGpuParity_Test.exe" --dump "$TMP/px_sw.raw" >/dev/null 2>&1 &
XGUI_RENDER_BACKEND=gpu      timeout 120 "$BIN/XGuiGpuParity_Test.exe" --dump "$TMP/px_gl.raw" >/dev/null 2>&1 &
XGUI_RENDER_BACKEND=vulkan   timeout 150 "$BIN/XGuiGpuParity_Test.exe" --dump "$TMP/px_vk.raw" >/dev/null 2>&1 &
wait

for pair in "sw:gl" "sw:vk"; do
    a="${pair%%:*}"; b="${pair##*:}"
    if [ ! -s "$TMP/px_$a.raw" ] || [ ! -s "$TMP/px_$b.raw" ]; then
        fail "逐像素 $a vs $b：dump 缺失"
        continue
    fi
    # 只取 px= 之后的数字：阈值文字里本身含 ">3"，整串取数字会把 3 也抓出来
    line=$("$BIN/XGuiGpuParity_Test.exe" --diff "$TMP/px_$a.raw" "$TMP/px_$b.raw" 2>&1 | grep -oE "hard\(>3/chan\) px=[0-9]+" | sed 's/.*px=//' | tail -1)
    if [ -z "$line" ]; then
        fail "逐像素 $a vs $b：比对无输出"
    elif [ "$line" -le "$HARD_PIX_MAX" ]; then
        pass "逐像素 $a vs $b：hard=${line}px (≤${HARD_PIX_MAX})"
    else
        fail "逐像素 $a vs $b：hard=${line}px > ${HARD_PIX_MAX}（像素回归）"
    fi
done

# ---------- 2b. 渲染到纹理自检（P3） ----------
# ---------- 2b. AA 线段（M4-1/M4-2，GL+VK opt-in） ----------
# AA 线从「CPU 覆盖率光栅 + drawAlphaBitmap 上传」换成 GPU 解析覆盖率后，
# 边缘像素值必然与 CPU 4x4 面积采样不同 ⇒ 逐位口径不适用。判据（感知）：
#   ① 平均墨量（亮度均值）相对差 ≤1%（描边断裂/整段缺失会显著偏移，
#      这是真正的结构闸——顶点序 bug 的缺块即被此闸拦下）；
#   ② 逐通道差 >8 的像素占比 ≤20%（边缘密集场景的算法差异兜底）。
# 默认路径（XGPU_AA_LINE 未置）完全不变，本段只锁 opt-in 路径。
echo "=== [2b/10] AA 线段（GL/VK opt-in） ==="
rm -f "$TMP"/aa_*.raw
XGUI_RENDER_BACKEND=software timeout 90 \
    "$BIN/XGuiGpuParity_Test.exe" --aa-lines-dump "$TMP/aa_sw.raw" \
    >"$TMP/aa_sw.log" 2>&1
XGPU_AA_LINE=1 XGUI_RENDER_BACKEND=gpu timeout 90 \
    "$BIN/XGuiGpuParity_Test.exe" --aa-lines-dump "$TMP/aa_gl.raw" \
    >"$TMP/aa_gl.log" 2>&1
if [ -s "$TMP/aa_sw.raw" ] && [ -s "$TMP/aa_gl.raw" ]; then
    aa_line=$("$BIN/XGuiGpuParity_Test.exe" --diff-perc \
                  "$TMP/aa_sw.raw" "$TMP/aa_gl.raw" 2>&1 | tail -1)
    if echo "$aa_line" | grep -q " PASS"; then
        pass "AA 线段感知：$(echo "$aa_line" | sed 's/^aa-line-perc: //')"
    else
        fail "AA 线段感知：$(echo "$aa_line" | sed 's/^aa-line-perc: //')"
    fi
    # 对照：AA 关闭时 GL 与 SW 必须逐位一致（over8 必须 0）——证明上面的
    # 差异确实来自 opt-in 路径，不是基线漂移。
    XGUI_RENDER_BACKEND=gpu timeout 90 \
        "$BIN/XGuiGpuParity_Test.exe" --aa-lines-dump "$TMP/aa_gl_off.raw" \
        >"$TMP/aa_gl_off.log" 2>&1
    if [ -s "$TMP/aa_gl_off.raw" ]; then
        aa_off=$("$BIN/XGuiGpuParity_Test.exe" --diff-perc \
                      "$TMP/aa_sw.raw" "$TMP/aa_gl_off.raw" 2>&1 | tail -1)
        if echo "$aa_off" | grep -q "over8=0 "; then
            pass "AA 关闭基线：$(echo "$aa_off" | sed 's/^aa-line-perc: //')"
        else
            fail "AA 关闭基线：$(echo "$aa_off" | sed 's/^aa-line-perc: //')"
        fi
    else
        fail "AA 关闭基线：dump 缺失"
    fi
    # [M4-2] VK 同口径：解析覆盖率管线（SPIR-V fwidth）+ 关闭基线。
    XGPU_AA_LINE=1 XGUI_RENDER_BACKEND=vulkan timeout 120         "$BIN/XGuiGpuParity_Test.exe" --aa-lines-dump "$TMP/aa_vk.raw"         >"$TMP/aa_vk.log" 2>&1
    if [ -s "$TMP/aa_vk.raw" ]; then
        aa_vk=$("$BIN/XGuiGpuParity_Test.exe" --diff-perc                     "$TMP/aa_sw.raw" "$TMP/aa_vk.raw" 2>&1 | tail -1)
        if echo "$aa_vk" | grep -q " PASS"; then
            pass "AA 线段感知 VK：$(echo "$aa_vk" | sed 's/^aa-line-perc: //')"
        else
            fail "AA 线段感知 VK：$(echo "$aa_vk" | sed 's/^aa-line-perc: //')"
        fi
        XGUI_RENDER_BACKEND=vulkan timeout 120             "$BIN/XGuiGpuParity_Test.exe" --aa-lines-dump "$TMP/aa_vk_off.raw"             >"$TMP/aa_vk_off.log" 2>&1
        if [ -s "$TMP/aa_vk_off.raw" ]; then
            aa_vk_off=$("$BIN/XGuiGpuParity_Test.exe" --diff-perc                             "$TMP/aa_sw.raw" "$TMP/aa_vk_off.raw" 2>&1 | tail -1)
            if echo "$aa_vk_off" | grep -q "over8=0 "; then
                pass "AA 关闭基线 VK：$(echo "$aa_vk_off" | sed 's/^aa-line-perc: //')"
            else
                fail "AA 关闭基线 VK：$(echo "$aa_vk_off" | sed 's/^aa-line-perc: //')"
            fi
        else
            fail "AA 关闭基线 VK：dump 缺失"
        fi
    else
        fail "AA 线段感知 VK：dump 缺失"
    fi
else
    fail "AA 线段感知：dump 缺失（SW 或 GL）"
fi

echo "=== [3/10] 渲染到纹理自检 ==="
for rt_bk in gpu vulkan; do
    rt_out=$(XGUI_RENDER_BACKEND="$rt_bk" timeout 120 "$BIN/XGuiGpuParity_Test.exe" --rt-test 2>&1)
    rt_rc=$?
    if [ "$rt_rc" -eq 0 ]; then
        pass "渲染到纹理 ${rt_bk}：$(echo "$rt_out" | grep -oE 'PASS.*|SKIP.*')"
    else
        fail "渲染到纹理 ${rt_bk} 失败：$(echo "$rt_out" | tail -2 | tr '\n' ' ')"
    fi
done

# ---------- 3. 截图 ----------
echo "=== [4/10] 窗口截图 ==="
rm -f "$TMP"/shot_*.png
XGUI_RENDER_BACKEND=software timeout 120 "$BIN/XGuiWindowDemo_Test.exe" --screenshot "$TMP/shot_sw.png" --page 4 >/dev/null 2>&1 &
XGUI_RENDER_BACKEND=gpu      timeout 120 "$BIN/XGuiWindowDemo_Test.exe" --screenshot "$TMP/shot_gl.png" --page 4 >/dev/null 2>&1 &
XGUI_RENDER_BACKEND=vulkan   timeout 150 "$BIN/XGuiWindowDemo_Test.exe" --screenshot "$TMP/shot_vk.png" --page 4 >/dev/null 2>&1 &
wait
if [ -s "$TMP/shot_sw.png" ] && [ -s "$TMP/shot_gl.png" ] && [ -s "$TMP/shot_vk.png" ]; then
    sz_sw=$(stat -c%s "$TMP/shot_sw.png" 2>/dev/null || echo 0)
    sz_gl=$(stat -c%s "$TMP/shot_gl.png" 2>/dev/null || echo 0)
    sz_vk=$(stat -c%s "$TMP/shot_vk.png" 2>/dev/null || echo 0)
    if [ "$sz_sw" -gt 0 ] && [ "$sz_gl" -gt 0 ] && [ "$sz_vk" -gt 0 ]; then
        ratio=0
        for sz in "$sz_gl" "$sz_vk"; do
            if [ "$sz" -ge "$sz_sw" ]; then r=$((sz * 10 / sz_sw)); else r=$((sz_sw * 10 / sz)); fi
            [ "$r" -gt "$ratio" ] && ratio=$r
        done
        if [ "$ratio" -le $((SHOT_RATIO_MAX * 10)) ]; then
            pass "截图 SW=${sz_sw}B GL=${sz_gl}B VK=${sz_vk}B（同量级）"
        else
            fail "截图 SW=${sz_sw}B GL=${sz_gl}B VK=${sz_vk}B（差 >${SHOT_RATIO_MAX}x，画面可能缺失）"
        fi
    else
        fail "截图字节数为 0"
    fi
else
    fail "截图未产出（SW/GL/VK 有缺失）"
fi

# ---------- 3b. 图表页静态层（P3-2） ----------
echo "=== [5/10] 图表页截图 ==="
rm -f "$TMP"/chart_*.png
XGUI_RENDER_BACKEND=software timeout 120 "$BIN/XGuiWindowDemo_Test.exe" --screenshot "$TMP/chart_sw.png" --page 12 >/dev/null 2>&1 &
XGUI_RENDER_BACKEND=gpu      timeout 120 "$BIN/XGuiWindowDemo_Test.exe" --screenshot "$TMP/chart_gl.png" --page 12 >/dev/null 2>&1 &
wait
if [ -s "$TMP/chart_sw.png" ] && [ -s "$TMP/chart_gl.png" ]; then
    c_sw=$(stat -c%s "$TMP/chart_sw.png" 2>/dev/null || echo 0)
    c_gl=$(stat -c%s "$TMP/chart_gl.png" 2>/dev/null || echo 0)
    if [ "$c_sw" -gt 0 ] && [ "$c_gl" -gt 0 ]; then
        if [ "$c_gl" -ge "$c_sw" ]; then cr=$((c_gl * 10 / c_sw)); else cr=$((c_sw * 10 / c_gl)); fi
        if [ "$cr" -le $((SHOT_RATIO_MAX * 10)) ]; then
            pass "图表页 SW=${c_sw}B GL=${c_gl}B（静态层纹理复用后画面完整）"
        else
            fail "图表页 SW=${c_sw}B GL=${c_gl}B（差 >${SHOT_RATIO_MAX}x，静态层可能丢失）"
        fi
    else
        fail "图表页截图字节数为 0"
    fi
else
    fail "图表页截图未产出（SW 或 GL 缺失）"
fi

# ---------- 5b. 子树静态层（M6 场景保留） ----------
# 三后端各跑一次 `--static-layer-probe`（**page 3**：输入页含小控件，
# 增量重建断言才有目标；page 4 是整块滚区+图表，无小控件）：进程内先
# 缓存关直画取参考帧、再缓存开取命中帧（首建 builds=1、hits≥1），然后
# 对小控件 updateRect 验证**增量重建**（builds+1、inc=1、重绘像素=该
# 矩形面积、输出与失效前逐位一致），最后显式整层失效（builds+1、inc
# 不增）——终态 builds=3/inc=1，自检行末的 builds= 即终态计数。软件
# 后端要求首建 A/B 逐位一致，GPU 后端按 <0.5% 硬差防退化（字形光栅差
# 为既有量化）；增量 A/B 三后端都要求硬逐位一致（内容未变、只重画局部）。
# 命中帧页面像素按 parity 头格式落盘，再做 SW vs GL/VK 逐像素 diff——
# 缓存内容三后端同源（建层恒软件光栅，GPU 只多一次纹理回贴），故锁
# 「0 硬差」；LAYER_HARD_PIX_MAX 为跨机器留的极小余量。
echo "=== [6/10] 子树静态层 A/B ==="
LAYER_HARD_PIX_MAX=256
rm -f "$TMP"/layer_*.raw
layer_ok=1
for bk in software gpu vulkan; do
    XGUI_RENDER_BACKEND="$bk" timeout 180 \
        "$BIN/XGuiWindowDemo_Test.exe" --static-layer-probe --page 3 \
        --probe-dump "$TMP/layer_$bk.raw" >"$TMP/layer_$bk.log" 2>&1
    layer_rc=$?
    layer_line=$(grep -a "static-layer-probe] builds=" "$TMP/layer_$bk.log" | tail -1)
    if [ "$layer_rc" -eq 0 ] && echo "$layer_line" | grep -q " PASS"; then
        pass "静态层 $bk：$(echo "$layer_line" | sed 's/.*static-layer-probe] //')"
    else
        fail "静态层 $bk 自检失败（exit=$layer_rc）：$layer_line"
        layer_ok=0
    fi
    layer_inc=$(grep -a "incremental builds=" "$TMP/layer_$bk.log" | tail -1)
    if [ -n "$layer_inc" ]; then
        note "- ℹ️ 静态层增量 $bk：$(echo "$layer_inc" | sed 's/.*static-layer-probe] //')"
    fi
done
if [ "$layer_ok" -eq 1 ]; then
    for pair in "software:gpu" "software:vulkan"; do
        a="${pair%%:*}"; b="${pair##*:}"
        if [ ! -s "$TMP/layer_$a.raw" ] || [ ! -s "$TMP/layer_$b.raw" ]; then
            fail "静态层逐像素 $a vs $b：dump 缺失"
            continue
        fi
        lpx=$("$BIN/XGuiGpuParity_Test.exe" --diff "$TMP/layer_$a.raw" \
                  "$TMP/layer_$b.raw" 2>&1 \
              | grep -oE "hard\(>3/chan\) px=[0-9]+" | sed 's/.*px=//' | tail -1)
        if [ -z "$lpx" ]; then
            fail "静态层逐像素 $a vs $b：比对无输出"
        elif [ "$lpx" -le "$LAYER_HARD_PIX_MAX" ]; then
            pass "静态层逐像素 $a vs $b：hard=${lpx}px (≤${LAYER_HARD_PIX_MAX})"
        else
            fail "静态层逐像素 $a vs $b：hard=${lpx}px > ${LAYER_HARD_PIX_MAX}（缓存像素回归）"
        fi
    done
else
    note "- ⏭ 静态层逐像素：自检未全过，跳过 dump 比对"
fi
# 性能 A/B（仅记录，机器负载波动大）：整帧基准下静态页容器是否把
# 「逐控件重绘」收敛成一次纹理回贴。page 4 是无定时器静态页。
for lbk in gpu vulkan; do
    l_off=$(XGUI_RENDER_BACKEND="$lbk" timeout 90 "$BIN/XGuiWindowDemo_Test.exe" \
                --benchmark 3 --benchmark-full --page 4 2>&1 \
            | grep -oE "fps=[0-9.]+" | head -1 | cut -d= -f2)
    l_on=$(XGUI_RENDER_BACKEND="$lbk" timeout 90 "$BIN/XGuiWindowDemo_Test.exe" \
               --benchmark 3 --benchmark-full --page 4 --static-layer-page 4 2>&1 \
           | grep -oE "fps=[0-9.]+" | head -1 | cut -d= -f2)
    note "- ℹ️ 静态层整帧 ${lbk}：off=${l_off:-0} on=${l_on:-0} FPS"
done

# ---------- 4+5. 性能与呈现计数 ----------
echo "=== [7/10] 性能基准 ==="
bench_one() {
    local bk="$1" mode="$2" lbl="$3"
    local out fps rb pr
    out=$(XGPU_PROF=1 XGUI_RENDER_BACKEND="$bk" timeout 180 "$BIN/XGuiWindowDemo_Test.exe" \
            --benchmark "$DUR" --page 4 --tab 20 $mode 2>&1)
    fps=$(echo "$out" | grep -oE "fps=[0-9.]+" | head -1 | cut -d= -f2)
    rb=$(echo "$out"  | grep -oE "readback=[0-9]+" | head -1 | cut -d= -f2)
    pr=$(echo "$out"  | grep -oE "present=[0-9]+"  | head -1 | cut -d= -f2)
    echo "${fps:-0} ${rb:-0} ${pr:-0}"
}

read -r SW_INC SW_INC_RB _        <<< "$(bench_one software '' '')"
read -r GL_INC GL_INC_RB GL_INC_PR <<< "$(bench_one gpu '' '')"
read -r VK_INC VK_INC_RB VK_INC_PR <<< "$(bench_one vulkan '' '')"
read -r SW_FULL _ _              <<< "$(bench_one software '--benchmark-full' '')"
read -r GL_FULL _ _              <<< "$(bench_one gpu '--benchmark-full' '')"
read -r VK_FULL _ _              <<< "$(bench_one vulkan '--benchmark-full' '')"

check_fps() { # name value min
    local v="${2%.*}"
    if [ "${v:-0}" -ge "$3" ]; then pass "$1 = $2 FPS (≥$3)"
    else fail "$1 = $2 FPS < $3（性能回归）"; fi
}
check_fps "软件 增量" "$SW_INC" "$SW_FPS_MIN"
check_fps "GL 增量"   "$GL_INC" "$GL_FPS_MIN"
check_fps "VK 增量"   "$VK_INC" "$VK_FPS_MIN_P1"
note "- ℹ️ 整帧：SW=${SW_FULL} GL=${GL_FULL} VK=${VK_FULL} FPS"

echo "=== [8/10] 零回读口径 ==="
for t in "GL:${GL_INC_RB}:${GL_INC_PR}" "VK:${VK_INC_RB}:${VK_INC_PR}"; do
    n="${t%%:*}"; rest="${t#*:}"; rb="${rest%%:*}"; pr="${rest##*:}"
    if [ "${pr:-0}" -gt 0 ]; then pass "$n present=${pr} > 0（GPU 直通上屏）"
    else fail "$n present=0（未走 GPU 直通）"; fi
    if [ "${rb:-0}" -le "$READBACK_MAX" ]; then pass "$n readback=${rb} ≤ ${READBACK_MAX}（零回读）"
    else fail "$n readback=${rb} > ${READBACK_MAX}（回读未收敛）"; fi
done

# ---------- 6. CPU 光栅预算（「全部 GPU」不变量） ----------
# 稳态不允许每帧的 CPU 光栅化：AA 覆盖图提交数 ab 必须为 0，软件批画布
# 重入 route 必须收敛到个位数/秒级别。口径来自 XGPU_PAINTER_PROF 的
# 5s 聚合行（--benchmark 6 保证至少打印一行）。
#   ab    = drawAlphaBitmap 提交次数（CPU 覆盖率光栅 → GPU 上传）
#   route = 软件局部提交命令数（批画布重入）
# 预算：ab 与 route 都必须 0（「稳态零 CPU 光栅化」不变量）。
# 2026-11-22 起 route 归零的根因是 VK 顶点缓冲 8MB 被 keep-open 跨 flush
# 累积耗尽（rejCap>0 ⇒ XPainter 逐 quad 落画布，单次 flush 4.27ms）；
# 容量改 XGPU_VK_VERTEX_MB（缺省 32MB）后 rejCap=0。**全页覆盖**：0~16
# 全部内置页实测 ab=0/route=0（2026-11-22 审计），17/18 为远程页
# （构建即自动连接/轮询）不纳入本预算，避免门禁依赖网络环境。
CPU_ROUTE_MAX=0
cpu_budget() {
    local page="$1" label="$2" out line ab route
    out=$(XGPU_PAINTER_PROF=1 XGUI_RENDER_BACKEND=vulkan timeout 60 \
          "$BIN/XGuiWindowDemo_Test.exe" --benchmark 6 --benchmark-full \
          --page "$page" 2>&1)
    line=$(printf '%s\n' "$out" | grep -a "xpainter-prof] 5" | tail -1)
    if [ -z "$line" ]; then
        fail "CPU 光栅预算 ${label}：未拿到 profiler 聚合行"
        return
    fi
    ab=$(printf '%s' "$line" | grep -oE "ab=[0-9]+" | tail -1 | cut -d= -f2)
    route=$(printf '%s' "$line" | grep -oE "route=[0-9]+" | head -1 | cut -d= -f2)
    if [ "${ab:-9999}" -eq 0 ] && [ "${route:-9999}" -le "$CPU_ROUTE_MAX" ]; then
        pass "CPU 光栅预算 ${label}：ab=${ab}（须 0），route=${route} ≤ ${CPU_ROUTE_MAX}"
    else
        fail "CPU 光栅预算 ${label}：ab=${ab:-?} route=${route:-?}（ab 须 0，route ≤ ${CPU_ROUTE_MAX}）"
    fi
}
echo "=== [9/10] CPU 光栅预算（全页 0~16） ==="
for _p in $(seq 0 16); do
    cpu_budget "$_p" "page ${_p}"
done

# ---------- 7. 冒烟 ----------
echo "=== [10/10] 冒烟 ==="
for bk in gpu vulkan software; do
    XGUI_RENDER_BACKEND="$bk" timeout 90 "$BIN/XGuiGpu_Test.exe" >/dev/null 2>&1 &
done
wait
for bk in gpu vulkan software; do
    XGUI_RENDER_BACKEND="$bk" timeout 90 "$BIN/XGuiGpu_Test.exe" >"$TMP/smoke_$bk.log" 2>&1
    if [ $? -eq 0 ]; then pass "冒烟 $bk exit=0"; else fail "冒烟 $bk 失败（见 $TMP/smoke_$bk.log）"; fi
done

# ---------- 汇总 ----------
{
    echo "# GPU 阶段验收 ${STAGE} (${TS})"
    echo ""
    echo "- 机器: $(nproc 2>/dev/null) 逻辑核 / 窗口 1000x700 / 图表页 --page 4 --tab 20"
    echo "- 基准时长: ${DUR}s  构建: $([ "$NO_BUILD" -eq 0 ] && echo '是' || echo '跳过')"
    echo ""
    printf '%s\n' "${ROWS[@]}"
    echo ""
    if [ "$FAIL" -eq 0 ]; then echo "## 结论：全绿 ✅"; else echo "## 结论：有失败项 ❌"; fi
} > "$REPORT"

echo ""
echo "================ 结果 ================"
printf '%s\n' "${ROWS[@]}"
echo "====================================="
echo "报告: $REPORT"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
