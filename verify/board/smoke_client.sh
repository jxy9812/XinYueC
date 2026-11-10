#!/bin/bash
# verify/board/smoke_client.sh —— 板上冒烟第二轮：TCP 重探 + 退出语义 + HsfClient。
# 由 WSL scp 上板执行：bash /tmp/smoke_client.sh
cd /root/Desktop/XinYueC/bin || exit 1

echo "=== [1] 残留进程清理确认"
pgrep -af "HsfServer --no-io" || echo no_leftover

echo "=== [2] 重新起服务并做 TCP 探测（bash /dev/tcp）"
nohup ./HsfServer --no-io 6300 > /tmp/hsf_smoke2.log 2>&1 &
sleep 4
SPID=$(pgrep -f "HsfServer --no-io 6300" | head -1)
echo "PID=$SPID"
timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/6300' && echo TCP_CONNECT_OK

echo "=== [3] SIGTERM 退出语义（等 8s 看收尾日志）"
[ -n "$SPID" ] && kill -TERM "$SPID"
for i in 1 2 3 4 5 6 7 8; do
    kill -0 "$SPID" 2>/dev/null || break
    sleep 1
done
if kill -0 "$SPID" 2>/dev/null; then
    echo "TERM_TIMEOUT（改 INT 再试 5s）"
    kill -INT "$SPID"
    for i in 1 2 3 4 5; do
        kill -0 "$SPID" 2>/dev/null || break
        sleep 1
    done
    kill -0 "$SPID" 2>/dev/null && { echo "INT_TIMEOUT（SIGKILL）"; kill -9 "$SPID"; } || echo "INT_EXIT_OK"
else
    echo "TERM_EXIT_OK"
fi
echo "--- 服务端日志尾 6 行："
tail -6 /tmp/hsf_smoke2.log

echo "=== [4] HsfClient 冒烟（无 X 时应优雅失败）"
ls /tmp/.X11-unix/ 2>/dev/null || echo "no_X11_socket"
DISPLAY=${DISPLAY:-} timeout 20 ./HsfClient --no-settings --no-login \
    --shot /tmp/hsf_smoke.png --size 800x480 2>&1 | head -8
echo "client_rc=$?"
ls -la /tmp/hsf_smoke.png 2>/dev/null || echo "no_png（无显示环境时的预期）"

echo "=== [5] hsfserverpy 状态（必须仍 active）"
systemctl is-active hsfserverpy
echo "=== SMOKE2_DONE"
