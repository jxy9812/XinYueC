#!/usr/bin/env bash
# HSF 板端（RK3568）一键链路：同步源码 → 交叉编译 → 部署 → 重启 → 复检。
#
# 用法（Windows Git Bash，仓库任意目录）:
#   Tools/posix/hsf-board-deploy.sh                  # 全流程（默认）
#   Tools/posix/hsf-board-deploy.sh --build-only    # 只编译
#   Tools/posix/hsf-board-deploy.sh --deploy-only   # 只部署+重启+复检
#   Tools/posix/hsf-board-deploy.sh --verify-only   # 只复检（不编译不部署）
#   Tools/posix/hsf-board-deploy.sh --reconfig      # 强制清构建缓存重配（换串口宏时用）
#
# 选项:
#   --board IP        板地址（默认 192.168.1.249；另一块网卡 192.168.0.10）
#   --port N          服务端端口（默认 6100）
#   --bus-a PORT      路 A 串口（默认 /dev/ttyS3：变频器站2 + 温湿度站4，115200）
#   --bus-b PORT      路 B 串口（默认 /dev/ttyS4：浊度站1/2 + 流量计站5，9600）
#   --user U          登录账号（默认 root）
#   --password P      HSF 账号口令（默认 root；登录服务端用）
#   --ssh-password P  板端 SSH 口令（默认 hmklj20250803，或用环境变量
#                     HSF_BOARD_SSH_PASSWORD 覆盖）
#   --seconds N       复检采样秒数（默认 40；须 > 2× 采集周期才见得到「抖」）
#   --poll-ms N       期望的温湿度采集周期（默认 10000，用于核对 staleMs ≥ 周期）
#   --wsl-distro NAME WSL 发行版（默认 Ubuntu）
#   --wsl-src DIR     WSL 源码树（默认 /root/xinyuec-board）
#   --build DIR       WSL 构建目录（默认 /root/xboard-hsf）
#
# 依赖:
#   · Windows 侧 Git Bash + WSL(Ubuntu, root)：内含 aarch64-linux-gnu-gcc、
#     /root/board-sysroot、/root/isoc23_shim.o（见 verify/board/build_board.sh）；
#   · 板端 SSH 密码认证：本脚本用 SSH_ASKPASS 自动化（板端口令可用环境变量
#     HSF_BOARD_PASSWORD 覆盖；本仓库是实验室内网板子，口令写在脚本里只为
#     一键化，生产环境请改走密钥）。
#
# 与既有脚本的分工（别重复造）:
#   · Tools/posix/cross-compile-rk3568.sh —— XGui demo 的一键链路（另一棵源码树
#     /root/xinyuec-src、另一套目标），**不**负责 HSF；
#   · verify/board/build_board.sh —— WSL 内的 HSF 编译（本脚本调用它）；
#   · verify/board/restart_c_stack.sh —— 板端重启（本脚本上传后调用）；
#   · verify/board/verify_temphum_stale.py —— 温湿度「数据过期」复检客户端。
set -euo pipefail

BOARD="192.168.1.249"
PORT="6100"
BUS_A="/dev/ttyS3"
BUS_B="/dev/ttyS4"
USER_NAME="root"
PASSWORD="root"          # HSF 账号口令（登录服务端用，与 SSH 口令是两回事）
SSH_PASSWORD=""          # 板端 SSH 口令（默认取 HSF_BOARD_PASSWORD）
SECONDS_SAMPLE="40"
POLL_MS="10000"
WSL_DISTRO="Ubuntu"
WSL_SRC="/root/xinyuec-board"
BUILD_DIR="/root/xboard-hsf"
RECONFIG=0
DO_BUILD=1
DO_DEPLOY=1
DO_VERIFY=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --board)       BOARD="$2"; shift 2 ;;
        --port)        PORT="$2"; shift 2 ;;
        --bus-a)       BUS_A="$2"; shift 2 ;;
        --bus-b)       BUS_B="$2"; shift 2 ;;
        --user)        USER_NAME="$2"; shift 2 ;;
        --password)    PASSWORD="$2"; shift 2 ;;
        --ssh-password) SSH_PASSWORD="$2"; shift 2 ;;
        --seconds)     SECONDS_SAMPLE="$2"; shift 2 ;;
        --poll-ms)     POLL_MS="$2"; shift 2 ;;
        --wsl-distro)  WSL_DISTRO="$2"; shift 2 ;;
        --wsl-src)     WSL_SRC="$2"; shift 2 ;;
        --build)       BUILD_DIR="$2"; shift 2 ;;
        --reconfig)    RECONFIG=1; shift ;;
        --build-only)  DO_DEPLOY=0; DO_VERIFY=0; shift ;;
        --deploy-only) DO_BUILD=0; shift ;;
        --verify-only) DO_BUILD=0; DO_DEPLOY=0; shift ;;
        -h|--help)     sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数: $1（--help 看用法）"; exit 1 ;;
    esac
done

PASSWORD="${HSF_BOARD_PASSWORD:-$PASSWORD}"
SSH_PASSWORD="${HSF_BOARD_SSH_PASSWORD:-${SSH_PASSWORD:-hmklj20250803}}"
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$REPO"
TMP="$REPO/.zcode/tmp"
mkdir -p "$TMP"

step() { echo; echo "========== $* =========="; }
fail() { echo "错误: $*" >&2; exit 1; }

# ---------- 仓库路径 → WSL 路径（Git Bash 的 /f/... 与 Windows 的 F:\... 两种） ----------
to_wsl_path() {
    local p="$1"
    if [[ "$p" == /* ]]; then
        echo "/mnt$p"
    elif [[ "$p" =~ ^[A-Za-z]:[\\/] ]]; then
        local drive="${p:0:1}" rest="${p:3}"
        drive="${drive,,}"
        rest="${rest//\\//}"
        echo "/mnt/$drive$rest"
    else
        echo "$p"
    fi
}
WSL_REPO="$(to_wsl_path "$REPO")"

wsl_do() { wsl -d "$WSL_DISTRO" -u root -- bash -c "$*"; }

# ---------- SSH/SCP 自动化（口令经 SSH_ASKPASS） ----------
ASKPASS="$TMP/askpass_hsf.sh"
printf '#!/bin/sh\necho %s\n' "$SSH_PASSWORD" > "$ASKPASS"
chmod +x "$ASKPASS"
export SSH_ASKPASS="$ASKPASS" SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0
SSH=(ssh -n -o StrictHostKeyChecking=no -o ConnectTimeout=10 "root@$BOARD")
SCP=(scp -o StrictHostKeyChecking=no -o ConnectTimeout=10)

# ==========================================================================
step "[0/5] 环境预检"
command -v wsl >/dev/null || fail "找不到 wsl（需 WSL Ubuntu，root）"
wsl_do "test -f /root/toolchain-aarch64.cmake 2>/dev/null || test -f '$WSL_SRC/verify/board/toolchain-aarch64.cmake'" \
    || fail "WSL 里没有工具链文件（仓库内 verify/board/toolchain-aarch64.cmake 或 /root/toolchain-aarch64.cmake）"
"${SSH[@]}" 'echo board-ok' >/dev/null || fail "板子 $BOARD 连不上（SSH 口令/地址？）"
echo "    WSL=$WSL_DISTRO  源码=$WSL_SRC  构建=$BUILD_DIR"
echo "    板=$BOARD:$PORT  路A=$BUS_A  路B=$BUS_B"

# ==========================================================================
if [[ $DO_BUILD == 1 ]]; then
    step "[1/5] 同步源码到 WSL"
    if ! wsl_do "test -d '$WSL_SRC'"; then
        echo "    WSL 源码树不存在 ⇒ 用 git archive 建基线（HEAD）"
        git archive --format=tar HEAD | \
            wsl -d "$WSL_DISTRO" -u root -- bash -c \
            "rm -rf '$WSL_SRC' && mkdir -p '$WSL_SRC' && tar xf - -C '$WSL_SRC'"
    fi
    # 工作区未提交改动 + 未跟踪新文件：打成一个 tar 覆盖过去。
    # （git archive 只含已跟踪内容；DrvFs 上直读 tar 会静默丢文件，
    #   所以在 Windows 侧先打成 tar 文件再让 WSL 解开。）
    { git diff --name-only HEAD -z; git ls-files --others --exclude-standard -z; } \
        | tr '\0' '\n' | sort -u > "$TMP/sync_list.raw"
    # 只留**实际存在**的文件（工作区里被删掉的文件不在 tar 清单里，
    # 否则 tar 会报 "Cannot stat" 并中断整条链路）。
    : > "$TMP/sync_list.txt"
    while IFS= read -r f; do
        [ -n "$f" ] && [ -e "$f" ] && printf '%s\n' "$f" >> "$TMP/sync_list.txt"
    done < "$TMP/sync_list.raw"
    N=$(wc -l < "$TMP/sync_list.txt")
    echo "    待同步文件 $N 个"
    if [[ "$N" -gt 0 ]]; then
        tar -cf "$TMP/hsf_sync.tar" -T "$TMP/sync_list.txt"
        wsl_do "tar xf '$WSL_REPO/.zcode/tmp/hsf_sync.tar' -C '$WSL_SRC'"
    fi
    # gitignore 的本机配置不在归档里，单独补（缺了 cmake 配置期即挂）
    if [[ -f CMake_Config.cmake ]]; then
        wsl_do "cp '$WSL_REPO/CMake_Config.cmake' '$WSL_SRC/CMake_Config.cmake'"
    fi

    step "[2/5] 交叉编译 aarch64（WSL 内调 verify/board/build_board.sh）"
    # 端口宏走 -D 注入 ⇒ ninja 的依赖图看不到命令行变化；缓存里的源码目录或
    # 串口宏与本次不一致时必须清缓存重配，否则编出来还是上次的端口。
    FORCE=0
    if [[ $RECONFIG == 1 ]]; then
        FORCE=1
    else
        CACHE=$(wsl_do "cat '$BUILD_DIR/CMakeCache.txt' 2>/dev/null | grep -E '^(CMAKE_HOME_DIRECTORY|CMAKE_C_FLAGS):' || true")
        case "$CACHE" in
            *"CMAKE_HOME_DIRECTORY:INTERNAL=$WSL_SRC"*) : ;;
            *) FORCE=1; echo "    缓存的源码目录与本次不一致 ⇒ 重新配置" ;;
        esac
        if [[ $FORCE == 0 ]]; then
            case "$CACHE" in *"$BUS_A"*) : ;; *) FORCE=1; echo "    路 A 端口与缓存不一致 ⇒ 重新配置" ;; esac
            case "$CACHE" in *"$BUS_B"*) : ;; *) FORCE=1; echo "    路 B 端口与缓存不一致 ⇒ 重新配置" ;; esac
        fi
    fi
    echo "    BOARD_FORCE_RECONFIG=$FORCE"
    wsl_do "cd '$WSL_SRC' && BOARD_BUS_A_PORT='$BUS_A' BOARD_BUS_B_PORT='$BUS_B' \
             BOARD_FORCE_RECONFIG=$FORCE sh verify/board/build_board.sh '$BUILD_DIR'" \
        || fail "交叉编译失败（详见上方输出）"
    for T in HsfServer HsfClient; do
        wsl_do "test -x '$BUILD_DIR/bin/$T'" || fail "产物缺失: $BUILD_DIR/bin/$T"
    done
fi

# ==========================================================================
if [[ $DO_DEPLOY == 1 ]]; then
    step "[3/5] 部署到板子"
    # 产物在 WSL 私有目录 ⇒ 先拷到 Windows 可见路径，再由 Windows 侧 scp
    # （WSL 里没有 askpass 凭据，scp 会卡在口令输入上）。
    STAGE="$TMP/board-bin"
    rm -rf "$STAGE"; mkdir -p "$STAGE"
    for T in HsfServer HsfClient; do
        wsl_do "cp '$BUILD_DIR/bin/$T' '$WSL_REPO/.zcode/tmp/board-bin/$T'"
        [[ -f "$STAGE/$T" ]] || fail "产物没落到 Windows 可见路径: $STAGE/$T"
    done
    # **先停板端服务端**：正在运行的二进制不能被覆盖（scp 直写目标会报
    # "dest open ...: Failure" = ETXTBSY）。停掉后再落盘。
    "${SCP[@]}" "$REPO/verify/board/restart_c_stack.sh" "root@$BOARD:/tmp/restart_c_stack.sh"
    "${SSH[@]}" "sh /tmp/restart_c_stack.sh $PORT stop" || true
    TS=$(date +%Y%m%d-%H%M%S)
    for T in HsfServer HsfClient; do
        echo "--- $T"
        "${SSH[@]}" "cd /root/Desktop/XinYueC/bin && cp -p $T $T.bak-$TS 2>/dev/null || true"
        # 先传到临时名再 mv 覆盖（rename 是原子的，不受旧文件占用影响）。
        "${SCP[@]}" "$STAGE/$T" "root@$BOARD:/root/Desktop/XinYueC/bin/.$T.new"
        "${SSH[@]}" "cd /root/Desktop/XinYueC/bin && chmod +x .$T.new && mv -f .$T.new $T"
        LOC=$(md5sum "$STAGE/$T" | awk '{print $1}')
        BRD=$("${SSH[@]}" "md5sum /root/Desktop/XinYueC/bin/$T" | awk '{print $1}')
        [[ "$LOC" == "$BRD" ]] || fail "md5 不一致 local=$LOC board=$BRD"
        echo "    md5 一致 $LOC"
    done
    "${SSH[@]}" "chmod +x /root/Desktop/XinYueC/bin/HsfServer /root/Desktop/XinYueC/bin/HsfClient"

    step "[4/5] 重启板端 C 栈（服务端先起，客户端等复检通过再拉）"
    "${SCP[@]}" "$REPO/verify/board/restart_c_stack.sh" "root@$BOARD:/tmp/restart_c_stack.sh"
    "${SSH[@]}" "sh /tmp/restart_c_stack.sh $PORT server" || fail "板端服务端重启失败"
fi

# ==========================================================================
RC=0
if [[ $DO_VERIFY == 1 ]]; then
    step "[5/5] 复检：温湿度「数据过期」"
    echo "--- 服务端日志里的陈旧阈值（第一手证据）"
    "${SSH[@]}" "grep -E '陈旧阈值已随周期联动|温湿度站装配完成' /root/Desktop/XinYueC/bin/hsf_server.log 2>/dev/null | tail -5" || true

    # 服务端缺省策略「同账并发关」⇒ 板端客户端在线时同账号再登会被拒。
    # 复检前**只停客户端**（服务端要留着推数据；停服务端就采不到帧了），
    # 复检后再把客户端拉起来，保证 root 账号空着。
    "${SCP[@]}" "$REPO/verify/board/restart_c_stack.sh" "root@$BOARD:/tmp/restart_c_stack.sh"
    "${SSH[@]}" "sh /tmp/restart_c_stack.sh $PORT stop-client" || true
    sleep 2

    echo "--- 客户端采样（连盯 ${SECONDS_SAMPLE}s，期望 staleMs ≥ ${POLL_MS}ms）"
    PY=""
    for c in python python3 py; do
        if command -v "$c" >/dev/null 2>&1; then PY="$c"; break; fi
    done
    [[ -n "$PY" ]] || { echo "错误: 找不到 python3（复检客户端要用）"; RC=2; }
    if [[ -n "$PY" ]]; then
        if [[ "$PY" == "py" ]]; then
            "$PY" -3 "$REPO/verify/board/verify_temphum_stale.py" \
                --host "$BOARD" --port "$PORT" --user "$USER_NAME" \
                --password "$PASSWORD" --seconds "$SECONDS_SAMPLE" --poll-ms "$POLL_MS" || RC=$?
        else
            "$PY" "$REPO/verify/board/verify_temphum_stale.py" \
                --host "$BOARD" --port "$PORT" --user "$USER_NAME" \
                --password "$PASSWORD" --seconds "$SECONDS_SAMPLE" --poll-ms "$POLL_MS" || RC=$?
        fi
    fi

    if [[ $DO_DEPLOY == 1 ]]; then
        echo "--- 拉起板端客户端（复检完成，界面回到现场状态）"
        "${SSH[@]}" "sh /tmp/restart_c_stack.sh $PORT client" || true
    fi
fi

echo
if [[ $RC == 0 ]]; then
    echo "=== 一键链路完成：编译 → 部署 → 重启 → 复检 全过 ==="
else
    echo "=== 一键链路结束，但复检未通过（RC=$RC），详见上方输出 ===" >&2
fi
exit $RC
