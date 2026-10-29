#!/bin/bash
# RK3506 (rk3506-buildroot, adb 序列号 94b6bbcaebf928a9) armhf 一键交叉编译（可选一键部署+自启）
#
# 用法（在 Git Bash 仓库任意目录执行）:
#   Tools/posix/cross-compile-rk3506.sh                    # 同步源码→WSL→编译→GLIBC 验收
#   Tools/posix/cross-compile-rk3506.sh --deploy           # 编译后部署板上并杀旧起新
#   Tools/posix/cross-compile-rk3506.sh --deploy --reboot  # 部署后整机重启，冷启动验证自启
#   Tools/posix/cross-compile-rk3506.sh --deploy --shot    # 部署后抓屏转 PNG（build-rk3506/fb_last.png）
#   Tools/posix/cross-compile-rk3506.sh --target XObjectTest --deploy
#
# 选项:
#   --target T   构建目标（默认 XGuiWindowDemo_Test）
#   --deploy     编译成功后 strip 并 adb 部署到 /userdata/gui/，杀旧实例拉起新实例，
#                并幂等安装 /oem/RkLunch.sh 自启脚本（首次安装自动备份 .bak）
#   --reboot     部署后整机重启，等待回连并验证自启（rk_demo 不在、demo 在跑）
#   --shot       结束时 drm_dump 抓屏转 PNG（本地 python 转换，依赖 python+stdlib）
#   --worktree   叠加工作区未提交改动与未跟踪新文件参与编译（默认只编译 HEAD
#                已提交内容；工作区删掉的文件不会从 WSL 副本移除，见输出提示）
#
# 环境（均可用环境变量覆盖）:
#   WSL_DISTRO       WSL 发行版（默认 Ubuntu-22.04，需 gcc-arm-linux-gnueabihf/cmake/ninja）
#   RK3506_ADB       adb 可执行文件路径（默认 D:\Program Files\Android\SDK\platform-tools\adb.exe）
#   RK3506_SERIAL    adb 序列号（默认 94b6bbcaebf928a9）
#   RK3506_GLIBC_MAX 板子 glibc 红线（默认 2.38）
#
# 踩坑实录见 docs/cross-compile-rk3506.md；本脚本固化其结论：
# 源码必须走 git archive（tar 直读 DrvFs 会静默丢文件）、必须补 gitignore 的
# CMake_Config.cmake、产物 GLIBC 上限 ≤ 2.38、adb 远端 POSIX 路径一律加
# MSYS_NO_PATHCONV=1（MSYS 会把 /data/... 改写成 C:/Program Files/Git/data/...）、
# 根分区 squashfs 只读，自启只能经 /oem/RkLunch.sh（kill rk_demo + 拉起 demo）。
set -euo pipefail

TARGET=XGuiWindowDemo_Test
DEPLOY=0
WORKTREE=0
REBOOT=0
SHOT=0
WSL_DISTRO="${WSL_DISTRO:-Ubuntu-22.04}"
WSL_SRC=/root/xinyuec-src
WSL_OUT=/root/out-rk
GLIBC_MAX="${RK3506_GLIBC_MAX:-2.38}"
ADB="${RK3506_ADB:-/d/Program Files/Android/SDK/platform-tools/adb.exe}"
SER="${RK3506_SERIAL:-94b6bbcaebf928a9}"
# 板上路径（/data -> userdata 软链，用 /data 短一些；adb 调用必须 MSYS_NO_PATHCONV=1）
APP_DIR=/userdata/gui
APP=/data/gui/XGuiWindowDemo
LUNCH=/oem/RkLunch.sh
LUNCH_MARK='XGuiDemo'
# 本地暂存（gitignore 的 build-rk3506/，相对路径 push 可同时规避路径改写）
# 注意 RKOUT 在 REPO 求值之后才能展开（set -u），见下方赋值

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)  TARGET="${2:?--target 需要目标名}"; shift 2 ;;
        --deploy)  DEPLOY=1; shift ;;
        --worktree) WORKTREE=1; shift ;;
        --reboot)  REBOOT=1; shift ;;
        --shot)    SHOT=1; shift ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数: $1（--help 看用法）"; exit 1 ;;
    esac
done

REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO"
git rev-parse --git-dir >/dev/null 2>&1 || { echo "错误: $REPO 不是 git 仓库"; exit 1; }
RKOUT="$REPO/build-rk3506"
SECONDS=0
mkdir -p "$RKOUT"

# ---------- 0. 环境预检 ----------
[[ -x "$ADB" ]] || { echo "错误: 找不到 adb: $ADB（设 RK3506_ADB 覆盖）"; exit 1; }
wsl -d "$WSL_DISTRO" -u root -- bash -c \
    'command -v arm-linux-gnueabihf-gcc >/dev/null && command -v cmake >/dev/null && command -v ninja >/dev/null' \
    || { echo "错误: WSL/$WSL_DISTRO 缺 arm-linux-gnueabihf-gcc/cmake/ninja（apt-get install -y gcc-arm-linux-gnueabihf cmake ninja-build）"; exit 1; }
if [[ $DEPLOY == 1 ]]; then
    [[ "$("$ADB" -s "$SER" get-state 2>/dev/null | tr -d '\r')" == "device" ]] \
        || { echo "错误: adb 设备 $SER 不在线"; exit 1; }
fi

# ---------- 1. 同步源码: git archive HEAD → WSL ----------
echo "[1/5] 同步源码到 $WSL_SRC ..."
git archive --format=tar HEAD | wsl -d "$WSL_DISTRO" -u root -- bash -c \
    "rm -rf $WSL_SRC && mkdir -p $WSL_SRC && tar xf - -C $WSL_SRC"

# 完整性验收：以 WSL 侧落地事实为准（tar 清单级比对全是显示假阳性，勿用——
# 目录条目、MSYS 管道 CRLF、core.quotepath 中文八进制转义三重坑，见文档坑③）
EXPECT=$(git ls-files | wc -l)
HAVE=$(wsl -d "$WSL_DISTRO" -u root -- bash -c "find $WSL_SRC -type f | wc -l")
[[ "$HAVE" == "$EXPECT" ]] || { echo "错误: WSL 副本文件数 $HAVE != git 跟踪数 $EXPECT，同步不完整"; exit 1; }

# gitignore 的本地配置不在 git archive 里，单独补（缺了 cmake 配置期即挂）
if [[ -f CMake_Config.cmake ]]; then
    wsl -d "$WSL_DISTRO" -u root -- bash -c \
        "cp '/mnt${REPO}/CMake_Config.cmake' '$WSL_SRC/CMake_Config.cmake'"
fi

# --worktree: 工作区未提交改动逐个覆盖（cp 后 cmp 校验，防 DrvFs 读坏）。
# 清单先 mapfile 进数组再循环：wsl.exe 会吸走 stdin，直接 while read 会丢清单
# （RK3568 同款坑）；-z + read -d '' 防中文路径八进制转义被当真实文件名。
if [[ $WORKTREE == 1 ]]; then
    mapfile -d '' -t WT_FILES < <(
        git -c core.quotepath=false diff --name-only --diff-filter=d HEAD -z
        git -c core.quotepath=false ls-files --others --exclude-standard -z
    )
    for f in "${WT_FILES[@]}"; do
        [[ -n "$f" ]] || continue
        wsl -d "$WSL_DISTRO" -u root -- bash -c \
            "mkdir -p '$WSL_SRC/$(dirname "$f")' && cp '/mnt${REPO}/$f' '$WSL_SRC/$f' && cmp -s '/mnt${REPO}/$f' '$WSL_SRC/$f'" \
            || { echo "错误: 工作区文件同步失败: $f"; exit 1; }
        echo "    叠加工作区改动: $f"
    done
fi
git diff --name-only --diff-filter=D HEAD | sed 's/^/    注意: 工作区已删除但仍参与编译(未提交): /'

# ---------- 2. 编译 ----------
# WSL 侧路径写死在 heredoc 字面量里（独立 argv 传 POSIX 路径会被 MSYS 改写）；
# 每次先删旧日志再建（cmake 失败时 && 不执行，tail 旧日志会误判，RK3568 同款坑）
echo "[2/5] 交叉编译 $TARGET ..."
wsl -d "$WSL_DISTRO" -u root -- bash -s "$TARGET" << 'WSLEOF'
set -e
rm -f /root/conf-rk.log /root/build-rk.log
cmake -B /root/xinyuec-src/build-rk -S /root/xinyuec-src -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/root/xinyuec-src/Drive/Posix/arm_none_linux_gnueabihf.toolchain.cmake \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/root/out-rk \
    -DXPLATFORM_FBDEV_ON=ON -DXPLATFORM_DRM_ON=ON -DXPLATFORM_FBINPUT_ON=ON \
    -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=ON \
    -DCMAKE_BUILD_TYPE=Release > /root/conf-rk.log 2>&1 \
    || { echo "cmake 配置失败:"; tail -20 /root/conf-rk.log; exit 1; }
if ! cmake --build /root/xinyuec-src/build-rk --target "$1" > /root/build-rk.log 2>&1; then
    echo "ninja 失败:"; tail -40 /root/build-rk.log; exit 1
fi
WSLEOF

# ---------- 3. GLIBC 红线验收（≤ 2.38，板子 Buildroot glibc 2.38）----------
echo "[3/5] GLIBC 符号版本验收（红线 $GLIBC_MAX）..."
MAXV=$(wsl -d "$WSL_DISTRO" -u root -- bash -s "$TARGET" << 'WSLEOF'
arm-linux-gnueabihf-readelf --dyn-syms -W /root/out-rk/"$1" \
    | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1 | sed 's/^GLIBC_//'
WSLEOF
)
[[ -n "$MAXV" ]] || { echo "错误: readelf 未取到 GLIBC 版本（产物不存在或非 ARM ELF）"; exit 1; }
if [[ $(printf '%s\n%s\n' "$MAXV" "$GLIBC_MAX" | sort -V | tail -1) != "$GLIBC_MAX" ]]; then
    echo "错误: $TARGET 需要 GLIBC_$MAXV > 板子的 $GLIBC_MAX"; exit 1
fi
echo "    产物 GLIBC 上限 $MAXV ≤ $GLIBC_MAX 通过"

# strip（产物留 WSL /root/out-rk，strip 副本经 build-rk3506/ 中转到板上）
STRIPPED="${TARGET}.stripped"
wsl -d "$WSL_DISTRO" -u root -- bash -s "$TARGET" "$STRIPPED" << 'WSLEOF'
set -e
cp "/root/out-rk/$1" "/root/out-rk/$2"
arm-linux-gnueabihf-strip "/root/out-rk/$2"
WSLEOF
wsl -d "$WSL_DISTRO" -u root -- bash -c "cp '/root/out-rk/$STRIPPED' '/mnt${REPO}/build-rk3506/$STRIPPED'"
echo "    strip 完成: $(stat -c%s "$RKOUT/$STRIPPED") 字节"

# ---------- 4.（--deploy）部署板上：杀旧 → push → md5 → 自启脚本 → 拉起 ----------
if [[ $DEPLOY == 1 ]]; then
    echo "[4/5] 部署到 $SER ..."
    # 杀旧实例（text file busy 防御）+ 顺手清 rk_demo（自启脚本未装/手工场景）
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell \
        'killall XGuiWindowDemo 2>/dev/null; killall rk_demo 2>/dev/null; killall rk_demo_test 2>/dev/null; sleep 1' || true

    # push（相对路径本地源 + MSYS_NO_PATHCONV，双保险；.new 中转防半程文件）
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" push "build-rk3506/$STRIPPED" "$APP.new" > /dev/null
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell \
        "mv $APP.new $APP && chmod +x $APP"

    LOC_MD5=$(md5sum "$RKOUT/$STRIPPED" | awk '{print $1}')
    BRD_MD5=$(MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell "md5sum $APP" | awk '{print $1}')
    [[ "$LOC_MD5" == "$BRD_MD5" ]] || { echo "错误: md5 不一致 local=$LOC_MD5 board=$BRD_MD5"; exit 1; }
    echo "    md5 校验一致: $LOC_MD5"

    # 自启脚本幂等安装：无标记才首次安装并备份原文件；有标记但内容漂移则刷新
    generate_lunch() {
        cat << 'EOF'
#!/bin/sh
echo "The user's boot script is executed here"

# ---- XGuiDemo 开机自启（2026-10-29） ----
# 出厂演示 GUI rk_demo 由 pre_init/S99lv_demo 在 rcS.early 阶段拉起，
# 根分区为 squashfs 只读镜像无法删除脚本，这里在启动收尾阶段结束它，
# 并拉起 XGuiWindowDemo（可写 userdata 分区）。
killall rk_demo 2>/dev/null
killall rk_demo_test 2>/dev/null

if [ -x /userdata/gui/XGuiWindowDemo ]; then
    (cd /userdata/gui && ./XGuiWindowDemo > /tmp/xguidemo.log 2>&1 &)
fi
EOF
    }
    generate_lunch > "$RKOUT/RkLunch.sh"
    if MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell "grep -q '$LUNCH_MARK' $LUNCH 2>/dev/null"; then
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell "cat $LUNCH" | tr -d '\r' > "$RKOUT/RkLunch.onboard"
        if ! cmp -s "$RKOUT/RkLunch.sh" "$RKOUT/RkLunch.onboard"; then
            MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" push "build-rk3506/RkLunch.sh" "$LUNCH" > /dev/null
            echo "    自启脚本内容有漂移，已刷新（.bak 备份保持不动）"
        else
            echo "    自启脚本已是最新，跳过"
        fi
        rm -f "$RKOUT/RkLunch.onboard"
    else
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell "cp $LUNCH $LUNCH.bak 2>/dev/null || true"
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" push "build-rk3506/RkLunch.sh" "$LUNCH" > /dev/null
        echo "    自启脚本已安装（原文件备份 $LUNCH.bak）"
    fi
    # CRLF 防御（busybox sed 兜底剥 \r）
    CRN=$(MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell "tr -cd '\r' < $LUNCH | wc -c" | tr -dc '0-9')
    if [[ -n "$CRN" && "$CRN" != "0" ]]; then
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell "sed -i 's/\r//g' $LUNCH"
        echo "    警告: 检测到 CRLF 已自动剥离（$CRN 个 \\r）"
    fi

    # 拉起并验活（板上固定名 XGuiWindowDemo，与 RkLunch.sh 的探测路径一致）。
    # adb shell 偶发误报 255（大文件 push 后 USB 瞬断，远端实际已拉起——
    # 2026-10-29 实测），这里不以其退出码定生死，交给下方 check_app_up。
    echo "    拉起 demo ..."
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell \
        "(cd $APP_DIR && ./XGuiWindowDemo > /tmp/xguidemo.log 2>&1 &)" || true
    sleep 4
    check_app_up() {
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell 'ps | grep -v grep | grep XGuiWindowDemo' > /dev/null 2>&1
    }
    if check_app_up; then
        echo "    demo 进程在跑，启动日志尾部："
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell 'tail -6 /tmp/xguidemo.log' | sed 's/^/        /'
    else
        echo "错误: 板上未发现 demo 进程，日志："
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell 'cat /tmp/xguidemo.log 2>/dev/null' | tail -20 | sed 's/^/        /'
        exit 1
    fi
else
    echo "[4/5] 跳过部署（加 --deploy 自动部署）"
    if [[ $REBOOT == 1 || $SHOT == 1 ]]; then
        echo "错误: --reboot/--shot 依赖 --deploy"; exit 1
    fi
fi

# ---------- 5.（--reboot / --shot）冷启动验证 / 抓屏 ----------
if [[ $REBOOT == 1 ]]; then
    echo "[5/5] 整机重启，冷启动验证自启 ..."
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell 'sync; reboot' 2>/dev/null || true
    echo "    等待设备回连 ..."
    UP=0
    for i in $(seq 1 36); do
        sleep 5
        [[ "$("$ADB" -s "$SER" get-state 2>/dev/null | tr -d '\r')" == "device" ]] && { UP=1; break; }
    done
    [[ $UP == 1 ]] || { echo "错误: 重启后 180s 内 adb 未回连"; exit 1; }
    echo "    设备已回连(~$((i*5))s)，等启动流程收尾 ..."
    APPUP=0
    for i in $(seq 1 12); do
        sleep 5
        if MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell 'ps | grep -v grep | grep XGuiWindowDemo' > /dev/null 2>&1; then APPUP=1; break; fi
    done
    [[ $APPUP == 1 ]] || { echo "错误: 重启后 demo 未自启，日志："
        MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell 'cat /tmp/xguidemo.log 2>/dev/null' | tail -20 | sed 's/^/        /'
        exit 1; }
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell \
        'ps | grep -v grep | grep rk_demo && echo "错误: rk_demo 仍在运行" || true' | sed 's/^/    /'
    echo "    自启验证通过：rk_demo 已被接管，XGuiWindowDemo 在跑"
fi

if [[ $SHOT == 1 ]]; then
    echo "[extra] drm_dump 抓屏 → build-rk3506/fb_last.png ..."
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" shell '/userdata/gui/drm_dump > /tmp/fb_last.raw 2>/dev/null; true'
    MSYS_NO_PATHCONV=1 "$ADB" -s "$SER" pull /tmp/fb_last.raw "$RKOUT/fb_last.raw" > /dev/null
    cat > "$RKOUT/raw2png_last.py" << 'PYEOF'
# -*- coding: utf-8 -*-
"""RGB565 raw -> PNG. Usage: raw2png_last.py <raw> <w> <h> <png>"""
import sys, zlib, struct
raw, w, h, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
buf = open(raw, "rb").read()
need = w * h * 2
assert len(buf) >= need, f"need {need}B, got {len(buf)}B"
def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
rgb = bytearray(w * h * 3)
for i in range(w * h):
    p = struct.unpack_from("<H", buf, i * 2)[0]
    rgb[i*3+0] = ((p >> 11) & 0x1f) << 3
    rgb[i*3+1] = ((p >> 5) & 0x3f) << 2
    rgb[i*3+2] = (p & 0x1f) << 3
scan = b"".join(b"\x00" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(scan, 6))
       + chunk(b"IEND", b""))
open(out, "wb").write(png)
print("PNG_OK", out)
PYEOF
    python "$RKOUT/raw2png_last.py" "$RKOUT/fb_last.raw" 800 480 "$RKOUT/fb_last.png"
fi

echo "完成($SECONDS s): WSL $WSL_OUT/${TARGET}（strip: $WSL_OUT/$STRIPPED）"
