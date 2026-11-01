#!/bin/bash
# 板端 485 联调门禁：持续轮询期间采 [WHEEL] lag + 进程 CPU + 内存/fd 采样
# 用法（板端）：board_485.sh <秒数>
set -u
SECS=${1:-60}
PID=$(pgrep -f "[X]GuiWindowDemo_Test" | head -1)
if [ -z "$PID" ]; then echo "NO_DEMO"; exit 1; fi
echo "### pid=$PID 采样 ${SECS}s"
echo "--- 起始 ---"
awk '/VmData|VmRSS/ {print "  "$0}' /proc/$PID/status
echo "  fd=$(ls /proc/$PID/fd 2>/dev/null | wc -l)"
U0=$(awk '{print $14+$15}' /proc/$PID/stat)
T0=$(date +%s%N)
i=0
while [ $i -lt "$SECS" ]; do
    sleep 10
    i=$((i+10))
    U=$(awk '{print $14+$15}' /proc/$PID/stat)
    printf "  t=%3ds cpu=%5.1f%%  VmData=%s VmRSS=%s fd=%s\n" "$i" \
        "$(awk -v a="$U0" -v b="$U" 'BEGIN{print (b-a)*10/10}')" \
        "$(awk '/VmData/{print $2" "$3}' /proc/$PID/status)" \
        "$(awk '/VmRSS/{print $2" "$3}' /proc/$PID/status)" \
        "$(ls /proc/$PID/fd 2>/dev/null | wc -l)"
done
echo "--- 结束 ---"
