#!/bin/bash
# HSF board live-process memory watcher for leak triage.
# Samples VmData/VmSize/VmRSS/Threads/fd-count for the three HSF processes.
# Read-only on /proc: does not touch or restart any running program.
# Stop by creating /tmp/hsf_mem_watch.stop
OUT=/tmp/hsf_mem_watch.csv
STOP=/tmp/hsf_mem_watch.stop
INTERVAL=${1:-20}
echo "ts,pid,name,vmdata_kb,vmsize_kb,vmrss_kb,threads,fds" > "$OUT"

find_pids() {
  CLIENT_PID=$(pgrep -f 'python3 -m hsfsrv_client ' | head -1)
  SERVER_PID=$(pgrep -f 'python3 -m hsfsrv$' | head -1)
  WEB_PID=$(pgrep -f 'python3 -m hsfweb ' | head -1)
}

sample_one() {
  local pid="$1" name="$2" ts="$3"
  [ -z "$pid" ] && return
  [ -d "/proc/$pid" ] || return
  local vmdata vmsize vmrss threads fds
  vmdata=$(awk '/^VmData:/{print $2}' /proc/$pid/status 2>/dev/null)
  vmsize=$(awk '/^VmSize:/{print $2}' /proc/$pid/status 2>/dev/null)
  vmrss=$(awk '/^VmRSS:/{print $2}' /proc/$pid/status 2>/dev/null)
  threads=$(awk '/^Threads:/{print $2}' /proc/$pid/status 2>/dev/null)
  fds=$(ls /proc/$pid/fd 2>/dev/null | wc -l)
  echo "$ts,$pid,$name,$vmdata,$vmsize,$vmrss,$threads,$fds" >> "$OUT"
}

rm -f "$STOP"
n=0
while [ ! -f "$STOP" ]; do
  ts=$(date +%s)
  find_pids
  sample_one "$CLIENT_PID" client "$ts"
  sample_one "$SERVER_PID" server "$ts"
  sample_one "$WEB_PID" web "$ts"
  n=$((n+1))
  sleep "$INTERVAL"
done
echo "DONE n=$n" >> "$OUT"
