#!/bin/bash
# 线程级 CPU 归因：按线程名聚合 12 秒内的 CPU tick 增量
set -u
BIN=/root/Desktop/XinYueC/bin/XGuiWindowDemo_Test
export DISPLAY=:0
cd /root/Desktop/XinYueC/bin

snap() { # $1=pid -> "comm ticks"
    for t in /proc/$1/task/*; do
        c=$(cat "$t/comm" 2>/dev/null | tr -d '\n')
        k=$(awk '{print $14+$15}' "$t/stat" 2>/dev/null || echo 0)
        echo "$c $k"
    done
}

probe() {
    label="$1"; shift
    env "$@" ./XGuiWindowDemo_Test --fullscreen --page 18 >/dev/null 2>&1 &
    P=$!
    sleep 5
    snap "$P" > /tmp/t0.txt
    sleep 12
    snap "$P" > /tmp/t1.txt
    kill "$P" 2>/dev/null
    wait "$P" 2>/dev/null
    echo "### $label"
    awk '{c[$1]+=$2} END{for(k in c) print k, c[k]}' /tmp/t0.txt | sort > /tmp/a0.txt
    awk '{c[$1]+=$2} END{for(k in c) print k, c[k]}' /tmp/t1.txt | sort > /tmp/a1.txt
    join /tmp/a0.txt /tmp/a1.txt | awk '{d=$3-$2; if(d>0) printf "  %-22s %4d ticks  (%5.1f%% core/12s)\n", $1, d, d*10/120}'
}

probe software XGUI_RENDER_BACKEND=software
probe gpufast XGPU_FASTPATH_EXT=1
