#!/bin/bash
# RK3568 (192.168.0.10) aarch64 一键交叉编译（可选一键部署）
#
# 用法（在 Git Bash 仓库任意目录执行）:
#   Tools/posix/cross-compile-rk3568.sh                  # 同步源码→WSL→编译→GLIBC 验收
#   Tools/posix/cross-compile-rk3568.sh --deploy         # 编译后自动部署板子并重启自启服务
#   Tools/posix/cross-compile-rk3568.sh --target XObjectTest --deploy
#
# 选项:
#   --target T   构建目标（默认 XGuiWindowDemo_Test）
#   --deploy     编译成功后部署到板子 /root/Desktop/XinYueC/bin/ 并重启 run_test.service
#   --worktree   叠加工作区未提交改动参与编译（默认只编译 HEAD 已提交内容；
#                工作区删掉的文件不会从 WSL 副本移除，见输出提示）
#
# 依赖: Windows 侧 Git Bash + WSL 发行版 Ubuntu(root)，
#       内含 /root/toolchain-aarch64.cmake 与 /root/board-sysroot。
# 踩坑实录见 docs/cross-compile-rk3568.md；本脚本固化其中 §4/§6 的结论：
# 源码必须走 git archive（tar 直读 DrvFs 会静默丢文件）、
# 必须补 gitignore 的 CMake_Config.cmake、产物 GLIBC 上限必须 ≤ 2.31。
set -euo pipefail

TARGET=XGuiWindowDemo_Test
DEPLOY=0
WORKTREE=0
WSL_DISTRO=Ubuntu
WSL_SRC=/root/xinyuec-src

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)  TARGET="${2:?--target 需要目标名}"; shift 2 ;;
        --deploy)  DEPLOY=1; shift ;;
        --worktree) WORKTREE=1; shift ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数: $1（--help 看用法）"; exit 1 ;;
    esac
done

REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO"
git rev-parse --git-dir >/dev/null 2>&1 || { echo "错误: $REPO 不是 git 仓库"; exit 1; }
SECONDS=0

# ---------- 0. 环境预检 ----------
wsl -d "$WSL_DISTRO" -u root -- bash -c \
    "test -f /root/toolchain-aarch64.cmake && test -d /root/board-sysroot" \
    || { echo "错误: WSL/$WSL_DISTRO 不可用或缺 /root/toolchain-aarch64.cmake、/root/board-sysroot（见 docs/cross-compile-rk3568.md §2/§3）"; exit 1; }

# ---------- 1. 同步源码: git archive HEAD → WSL ----------
echo "[1/4] 同步源码到 $WSL_SRC ..."
git archive --format=tar HEAD | wsl -d "$WSL_DISTRO" -u root -- bash -c \
    "rm -rf $WSL_SRC && mkdir -p $WSL_SRC && tar xf - -C $WSL_SRC"

# 完整性验收: 文件数必须与 git 跟踪数一致（防静默丢文件复发；
# 须在补 CMake_Config 等本地文件之前做，否则多 1 个对不上）
EXPECT=$(git ls-files | wc -l)
HAVE=$(wsl -d "$WSL_DISTRO" -u root -- bash -c "find $WSL_SRC -type f | wc -l")
[[ "$HAVE" == "$EXPECT" ]] || { echo "错误: WSL 副本文件数 $HAVE != git 跟踪数 $EXPECT，同步不完整"; exit 1; }

# gitignore 的本地配置不在 git archive 里，单独补（缺了 cmake 配置期即挂）
if [[ -f CMake_Config.cmake ]]; then
    wsl -d "$WSL_DISTRO" -u root -- bash -c \
        "cp '/mnt${REPO}/CMake_Config.cmake' '$WSL_SRC/CMake_Config.cmake'"
fi

# --worktree: 工作区未提交改动逐个覆盖（cp 后 cmp 校验，防 DrvFs 读坏）。
# 清单先 mapfile 进数组再循环：wsl.exe 会吸走 stdin，若直接
# `while read < <(git diff ...)`，首条 wsl 之后剩余清单行全部静默消失
# （实测只叠加了清单第 1 个文件，2026-10-28）。
if [[ $WORKTREE == 1 ]]; then
    mapfile -t WT_FILES < <(git diff --name-only --diff-filter=d HEAD)
    for f in "${WT_FILES[@]}"; do
        [[ -n "$f" ]] || continue
        wsl -d "$WSL_DISTRO" -u root -- bash -c \
            "mkdir -p '$WSL_SRC/$(dirname "$f")' && cp '/mnt${REPO}/$f' '$WSL_SRC/$f' && cmp -s '/mnt${REPO}/$f' '$WSL_SRC/$f'" \
            || { echo "错误: 工作区文件同步失败: $f"; exit 1; }
        echo "    叠加工作区改动: $f"
    done
fi

# 工作区删除的文件仍留在 WSL 副本（HEAD 里还在），提示但不阻塞
git diff --name-only --diff-filter=D HEAD | sed 's/^/    注意: 工作区已删除但仍参与编译(未提交): /'

# ---------- 2. 编译 ----------
# 注意: WSL 侧路径在 heredoc 里写死字面量——独立 argv 传 POSIX 路径会被
# MSYS 改写成 Git 安装目录（/root/... → D:/Program Files/Git/root/...）
echo "[2/4] 交叉编译 $TARGET ..."
wsl -d "$WSL_DISTRO" -u root -- bash -s "$TARGET" << 'WSLEOF'
cd /root/xinyuec-src/build-x 2>/dev/null || { mkdir -p /root/xinyuec-src/build-x && cd /root/xinyuec-src/build-x; }
cmake /root/xinyuec-src -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE=/root/toolchain-aarch64.cmake \
     -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/root/xinyuec-src/out-arm > /root/build-cmake.log 2>&1 \
    || { echo "cmake 配置失败:"; tail -20 /root/build-cmake.log; exit 1; }
if ! make -j"$(nproc)" "$1" > /root/build-make.log 2>&1; then
    echo "make 失败:"; tail -40 /root/build-make.log; exit 1
fi
WSLEOF

# ---------- 3. GLIBC 红线验收（≤ 2.31，板子 Debian 11） ----------
echo "[3/4] GLIBC 符号版本验收 ..."
MAXV=$(wsl -d "$WSL_DISTRO" -u root -- bash -s "$TARGET" << 'WSLEOF'
aarch64-linux-gnu-readelf --dyn-syms -W /root/xinyuec-src/out-arm/"$1" \
    | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1 | sed 's/^GLIBC_//'
WSLEOF
)
[[ -n "$MAXV" ]] || { echo "错误: readelf 未取到 GLIBC 版本（产物不存在或非 aarch64 ELF）"; exit 1; }
if [[ $(printf '%s\n2.31\n' "$MAXV" | sort -V | tail -1) != "2.31" ]]; then
    echo "错误: $TARGET 需要 GLIBC_$MAXV > 板子的 2.31（工具链 -I/-B 被宿主库污染，见文档坑③④）"
    exit 1
fi
echo "    产物 GLIBC 上限 $MAXV ≤ 2.31 通过"

# ---------- 4.（可选）部署板子并重启自启 ----------
if [[ $DEPLOY == 1 ]]; then
    echo "[4/4] 部署到 192.168.0.10 并重启自启服务 ..."
    # 板子凭据（测试设备）：root / hmklj20250803（2026-10-28 起改密，
    # 旧 root/root 失效）。可用环境变量 RK3568_PASS 覆盖。
    RK3568_PASS="${RK3568_PASS:-hmklj20250803}"
    wsl -d "$WSL_DISTRO" -u root -- bash -s "$TARGET" "$RK3568_PASS" << 'WSLEOF'
set -e
BOARD=192.168.0.10
PASS="$2"
# -n 必须加: bash -s 从 stdin 读本脚本，ssh 会把剩余脚本字节当自己的 stdin
# 吸走，导致首条 ssh 之后的所有命令静默消失且整体退出 0
SSH="ssh -n -o StrictHostKeyChecking=no -o ConnectTimeout=8 root@$BOARD"
SCP="scp -o StrictHostKeyChecking=no -o ConnectTimeout=8"
printf '#!/bin/sh\necho %s\n' "$PASS" > /tmp/askpass.sh && chmod +x /tmp/askpass.sh
export SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0

$SSH "systemctl stop run_test.service"
# 服务外残留实例兜底清理（pgrep 模式带 [] 防 ssh 自杀，$$ 排除自身）
$SSH 'for p in $(pgrep -f "[X]GuiWindowDemo"); do [ "$p" != "$$" ] && kill "$p" 2>/dev/null || true; done' || true
$SCP "/root/xinyuec-src/out-arm/$1" "root@$BOARD:/root/Desktop/XinYueC/bin/"

LOC=$(md5sum "/root/xinyuec-src/out-arm/$1" | awk '{print $1}')
BRD=$($SSH "md5sum /root/Desktop/XinYueC/bin/$1" | awk '{print $1}')
[[ "$LOC" == "$BRD" ]] || { echo "错误: md5 不一致 local=$LOC board=$BRD"; exit 1; }

$SSH "systemctl start run_test.service"
sleep 8
$SSH "systemctl is-active run_test.service" >/dev/null \
    || { echo "错误: run_test.service 未起来"; $SSH "journalctl -u run_test.service -n 20 --no-pager"; exit 1; }
$SSH "pgrep -a -f XGuiWindowDemo" || { echo "错误: 板上未发现 demo 进程"; exit 1; }
$SSH "journalctl -u run_test.service -n 8 --no-pager | tail -6"
WSLEOF
    echo "    部署完成，demo 应已全屏运行（可用 xwd 截屏确认）"
else
    echo "[4/4] 跳过部署（加 --deploy 自动部署板子）"
fi

echo "完成($SECONDS s): WSL $WSL_SRC/out-arm/$TARGET"
