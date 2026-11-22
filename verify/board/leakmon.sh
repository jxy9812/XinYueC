#!/bin/sh
# verify/board/leakmon.sh —— 板端 HSF C 栈内存采样（泄漏监测用）。
#
# 用法（在板上执行）:
#   sh /tmp/leakmon.sh [输出CSV] [采样间隔秒]
#   sh /tmp/leakmon.sh /root/Desktop/XinYueC/bin/leakmon.csv 10
#
# 采什么：
#   ts    Unix 秒
#   tag   进程名（HsfServer / HsfClient）
#   pid   进程号（进程重启后 pid 会变 ⇒ 判读时按 pid 分段）
#   rss   VmRSS kB
#   data  VmData kB   ← 进程实际占用（匿名映射+堆），比 RSS 稳
#   size  VmSize kB   ← 虚拟大小（mmap 泄漏会先在这里露头）
#   thr   线程数
#   fds   打开的 fd 数（会话/套接字泄漏的探针）
#
# 为什么不用工作集判泄漏：工作集有 ~200KB 抖动且受页回收影响，
# 判不出几 KB~几十 KB/轮的泄漏（AGENTS.md「泄漏结论必须用堆计数」）。
# 本脚本给的是**趋势**，最终判据仍看进程自己的 [heap] 行
# （服务端 HSF_HEAP_TRACE / 客户端 XHSF_HEAP_TRACE，每 5s 一条）。
#
# 纪律：pkill 的模式会匹配到远端 shell 自己的 cmdline ⇒ 本脚本只 pgrep，
# 不杀任何进程。
OUT="${1:-/root/Desktop/XinYueC/bin/leakmon.csv}"
INTERVAL="${2:-10}"

if [ ! -f "$OUT" ]; then
    echo "ts,tag,pid,rss_kB,data_kB,size_kB,threads,fds" > "$OUT"
fi

sample_one() {
    _tag="$1"; _pid="$2"
    [ -r "/proc/$_pid/status" ] || return 0
    _rss=$(awk '/^VmRSS:/{print $2}' "/proc/$_pid/status" 2>/dev/null)
    _data=$(awk '/^VmData:/{print $2}' "/proc/$_pid/status" 2>/dev/null)
    _size=$(awk '/^VmSize:/{print $2}' "/proc/$_pid/status" 2>/dev/null)
    _thr=$(awk '/^Threads:/{print $2}' "/proc/$_pid/status" 2>/dev/null)
    _fds=$(ls "/proc/$_pid/fd" 2>/dev/null | wc -l)
    echo "$(date +%s),$_tag,$_pid,$_rss,$_data,$_size,$_thr,$_fds" >> "$OUT"
}

while :; do
    for _tag in HsfServer HsfClient; do
        for _pid in $(pgrep -x "$_tag" 2>/dev/null); do
            sample_one "$_tag" "$_pid"
        done
    done
    sleep "$INTERVAL"
done
