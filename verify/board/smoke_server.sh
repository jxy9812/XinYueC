#!/bin/sh
# verify/board/smoke_server.sh —— 在板上冒烟 HsfServer（不经串口）。
# 由 WSL scp 上板执行：sh /tmp/smoke_server.sh
# 步骤：ldd 缺库检查 → --no-io 6300 起服务 → 端口/TCP 探测 → SIGINT 干净退出
#       → 确认 hsfserverpy 未受影响。
cd /root/Desktop/XinYueC/bin || exit 1

echo "=== [1] ldd 缺库检查"
missing_srv=$(ldd ./HsfServer 2>/dev/null | grep -c "not found")
missing_cli=$(ldd ./HsfClient 2>/dev/null | grep -c "not found")
echo "HsfServer missing=$missing_srv  HsfClient missing=$missing_cli"
ldd ./HsfServer 2>/dev/null | grep "not found"
ldd ./HsfClient 2>/dev/null | grep "not found"

echo "=== [2] 起服务 HsfServer --no-io 6300"
nohup ./HsfServer --no-io 6300 > /tmp/hsf_smoke.log 2>&1 &
sleep 4
SPID=$(pgrep -f "HsfServer --no-io 6300" | head -1)
echo "PID=$SPID"
ps -o pid,comm,args -p "$SPID" 2>/dev/null | tail -1

echo "=== [3] 监听端口"
(ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -w 6300

echo "=== [4] TCP 连接探测"
timeout 2 sh -c 'exec 3<>/dev/tcp/127.0.0.1/6300' && echo TCP_CONNECT_OK

echo "=== [5] 服务端启动日志"
cat /tmp/hsf_smoke.log

echo "=== [6] SIGINT 干净退出"
[ -n "$SPID" ] && kill -INT "$SPID"
sleep 3
if kill -0 "$SPID" 2>/dev/null; then
    echo "STILL_RUNNING（强杀）"
    kill -9 "$SPID"
else
    echo "EXITED_OK"
fi
tail -3 /tmp/hsf_smoke.log

echo "=== [7] hsfserverpy 状态（必须仍 active）"
systemctl is-active hsfserverpy
echo "=== SMOKE_DONE"
