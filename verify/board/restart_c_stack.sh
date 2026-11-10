#!/bin/sh
# verify/board/restart_c_stack.sh —— 板端 C 栈（HsfServer + HsfClient）重启。
#
# 用法（在板上执行）:
#   sh /tmp/restart_c_stack.sh <port> [both|server|client|stop|stop-client]
#   sh /tmp/restart_c_stack.sh 6100 server        # 只起服务端（复检前用）
#   sh /tmp/restart_c_stack.sh 6100 client        # 只拉客户端（复检后用）
#   sh /tmp/restart_c_stack.sh 6100 stop          # 全停（部署前腾出二进制）
#   sh /tmp/restart_c_stack.sh 6100 stop-client   # 只停客户端（腾出登录账号）
#
# 为什么不用 systemd：板上只有两个 Python 服务的单元
# （hsfserverpy / hsfclientpy），C 栈一直是手工 nohup 起的
# ⇒ 重启脚本按**同样的口径**来，别另造一套自启机制。
#
# 纪律（踩过的坑都固化在这里）:
#   · 停进程用 pgrep 的**括号写法**并排除 $$：pkill 的模式会匹配到
#     「远端 shell 自己的 cmdline」（ssh 'bash -c ...' 整串都在里面），
#     把 shell 自己杀掉，后续命令全部静默消失；
#   · 服务端先起，端口**真的在听**之后才拉客户端（客户端连不上会立刻退出）；
#   · 日志落在 bin/hsf_server.log / bin/hsf_client.log（现场可 tail）；
#   · 串口归属：服务端独占 ttyS3/ttyS4 ⇒ 起 C 服务端前必须确保
#     hsfserverpy（Python 服务端）没在跑，否则两边抢同一个串口。
set -u

PORT="${1:-6100}"
WHICH="${2:-both}"
BIN="/root/Desktop/XinYueC/bin"
SLOG="$BIN/hsf_server.log"
CLOG="$BIN/hsf_client.log"

log() { echo "[restart] $*"; }

# ---- 停进程 ----
# [口径] 优先用 `pkill -x <进程名>`（按 comm 精确匹配）：**不受命令行文本影响**
#   ⇒ 远端 shell 自己的 cmdline 里就算出现 "HsfServer" 也不会被误杀
#   （2026-11-10 实锅：pgrep -f "[H]sfServer.old" 撞上命令里那句
#    `rm -f .../.HsfServer.old` ⇒ 自杀式把远端 shell 打死，后续命令全丢）。
#   括号写法只作兜底（进程名被截断成 15 字符时用得上）。
kill_matching() {
    _name="$1"
    _pat="$2"
    pkill -x "$_name" 2>/dev/null && return 0
    for _p in $(pgrep -f "$_pat" 2>/dev/null); do
        [ "$_p" = "$$" ] && continue
        kill "$_p" 2>/dev/null || true
    done
}

wait_gone() {
    _name="$1"; _n="${2:-10}"
    _i=0
    while [ "$_i" -lt "$_n" ]; do
        pgrep -x "$_name" >/dev/null 2>&1 || return 0
        sleep 1
        _i=$((_i + 1))
    done
    return 1
}

port_listening() {
    (ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -q ":$PORT[[:space:]]"
}

# ---- 停 Python 服务端（它占着串口）----
# 只有「起 C 服务端」时才需要；已停则无事。
stop_py_server() {
    if systemctl is-active --quiet hsfserverpy 2>/dev/null; then
        log "停 hsfserverpy（C 服务端要独占 ttyS3/ttyS4）"
        systemctl stop hsfserverpy 2>/dev/null || true
        sleep 1
    fi
}

stop_client() {
    if pgrep -x HsfClient >/dev/null 2>&1; then
        log "停 HsfClient"
        kill_matching HsfClient "[H]sfClient"
        wait_gone HsfClient 8 || kill_matching HsfClient "[H]sfClient"
    fi
}

stop_all() {
    stop_client
    if pgrep -x HsfServer >/dev/null 2>&1; then
        log "停 HsfServer"
        kill_matching HsfServer "[H]sfServer"
        wait_gone HsfServer 10 || kill_matching HsfServer "[H]sfServer"
    fi
}

start_server() {
    stop_py_server
    if pgrep -x HsfServer >/dev/null 2>&1; then
        log "停旧 HsfServer"
        kill_matching HsfServer "[H]sfServer"
        wait_gone HsfServer 10 || kill_matching HsfServer "[H]sfServer"
    fi
    cd "$BIN" || exit 1
    log "起 HsfServer（端口 $PORT，日志 $SLOG）"
    nohup ./HsfServer "$PORT" > "$SLOG" 2>&1 &
    _i=0
    while [ "$_i" -lt 20 ]; do
        if port_listening; then
            log "端口 $PORT 已在听"
            return 0
        fi
        sleep 1
        _i=$((_i + 1))
    done
    log "错误：20s 内端口 $PORT 没起来"
    tail -20 "$SLOG" 2>/dev/null
    return 1
}

start_client() {
    cd "$BIN" || exit 1
    log "起 HsfClient（DISPLAY=:0，日志 $CLOG）"
    DISPLAY=:0 nohup ./HsfClient --login "root:root@127.0.0.1:$PORT" \
        > "$CLOG" 2>&1 &
    sleep 3
    pgrep -x HsfClient >/dev/null 2>&1 \
        && log "HsfClient 已起" \
        || { log "错误：HsfClient 没起来"; tail -20 "$CLOG" 2>/dev/null; return 1; }
    return 0
}

case "$WHICH" in
    server) start_server ;;
    client) start_client ;;
    stop)   stop_all ;;
    stop-client) stop_client ;;
    both)   start_server && start_client ;;
    *)      echo "用法: $0 <port> [both|server|client]"; exit 2 ;;
esac

echo "[restart] 当前进程:"
pgrep -ax HsfServer 2>/dev/null || echo "  （HsfServer 未起）"
pgrep -ax HsfClient 2>/dev/null || echo "  （HsfClient 未起）"
echo "[restart] 当前监听:"
(ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -E ":$PORT[[:space:]]" \
    || echo "  （端口 $PORT 未监听）"
echo "[restart] DONE"
