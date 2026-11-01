#!/bin/bash
# 板端双重负载泄漏门禁：
#   ① 板内主站（Modbus TCP）轮询 192.168.0.12:502（页面隐藏后继续轮询）
#   ② 板内从站（Modbus RTU, ttyS3）供桌面主站轮询
#   采样 VmData / VmRSS / fd / 线程数；对照板端日志的主站轮询活性
# 用法（板端）：board_leak_gate.sh <秒数> [poll_ms]
set -u
SECS=${1:-600}
POLL_MS=${2:-20}
BIN=/root/Desktop/XinYueC/bin/XGuiWindowDemo_Test
export DISPLAY=:0

export XGUI_DEMO_MB_HOST=192.168.0.12
export XGUI_DEMO_MB_PORT=502
export XGUI_DEMO_MB_POLL=$POLL_MS
export XGUI_DEMO_MB_AUTOCONNECT=1
export XGUI_DEMO_MB_SLAVE_SERIAL=/dev/ttyS3
export XGUI_DEMO_MB_SLAVE_BAUD=115200
export XGUI_DEMO_MB_AUTOSTART=slave
export XGUI_DEMO_MB_POLLTRACE=1
# 框架自身记账变量连续采样（V5 口径：systemBytes 恒定）
export XGUI_REMOTE_BENCH_TRACE=1

cd /root/Desktop/XinYueC/bin
LOG=/tmp/leakgate_$$.log
"$BIN" --fullscreen --page 17 --switch-page 18 --switch-page-ms 3000 > "$LOG" 2>&1 &
PID=$!
sleep 6
if ! kill -0 "$PID" 2>/dev/null; then
    echo "DEMO_DIED"; tail -20 "$LOG"; exit 1
fi
echo "### pid=$PID 采样 ${SECS}s（主站轮询 ${POLL_MS}ms，目标 192.168.0.12:502）"
echo "--- 起始 t=0 ---"
awk '/VmData|VmRSS|Threads/ {print "  "$0}' /proc/$PID/status
echo "  fd=$(ls /proc/$PID/fd 2>/dev/null | wc -l)"
U0=$(awk '{print $14+$15}' /proc/$PID/stat)
T0=$(date +%s%N)
i=0
while [ "$i" -lt "$SECS" ]; do
    sleep 10
    i=$((i+10))
    U=$(awk '{print $14+$15}' /proc/$PID/stat)
    printf "  t=%3ds cpu=%5.1f%%  %s  %s  fd=%s thr=%s\n" "$i" \
        "$(awk -v a="$U0" -v b="$U" -v s0="$T0" -v s1="$(date +%s%N)" 'BEGIN{print (b-a)*1000/((s1-s0)/1000000)}')" \
        "$(awk '/VmData/{print "VmData="$2$3}' /proc/$PID/status)" \
        "$(awk '/VmRSS/{print "VmRSS="$2$3}' /proc/$PID/status)" \
        "$(ls /proc/$PID/fd 2>/dev/null | wc -l)" \
        "$(awk '/Threads/{print $2}' /proc/$PID/status)"
done
U1=$(awk '{print $14+$15}' /proc/$PID/stat)
T1=$(date +%s%N)
echo "--- 结束 ---"
awk -v a="$U0" -v b="$U1" -v s0="$T0" -v s1="$T1" 'BEGIN{printf "  稳态 CPU=%.1f%% 单核\n", (b-a)*1000/((s1-s0)/1000000)}'
echo "--- 主站轮询活性（POLLTRACE 尾部）---"
grep -E "TICKGAP|TX |RX |poll|master" "$LOG" | tail -6
echo "--- 从站应答/串口相关日志 ---"
grep -iE "slave|ttyS3|serial|modbus" "$LOG" | tail -6
echo "--- 框架记账 MEMPROF sys 序列（V5 口径：systemBytes）---"
grep -oE "sys=[0-9]+" "$LOG" | awk -F= 'NR==1{first=$2} {last=$2; if(min==""||$2<min)min=$2; if($2>max)max=$2; n++} END{if(n)printf "  样本=%d 首=%d 末=%d 最小=%d 最大=%d 跨度=%d
", n, first, last, min, max, max-min; else print "  （无记账样本）"}'
echo "--- 记账行尾部 ---"
grep -E "MEMPROF" "$LOG" | tail -3
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
echo "LOG=$LOG"
