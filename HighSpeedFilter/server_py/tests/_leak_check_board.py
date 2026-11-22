# -*- coding: utf-8 -*-
"""板端 2fps 档稳定性 + 内存泄漏三轮回检（不动服务端，纯只读采样）。

判据（项目既有口径）：
  · **VmData 是 malloc 堆**——泄漏在这里会持续单调增长；RSS 受页缓存/
    mallopt 驻留影响会波动，不能单看 RSS。
  · 三轮增量判据：Δ23 ≤ Δ12（收敛）为正常；**递增**才是泄漏。
  · fd / 线程数必须恒定（相机采集线程、gst 管道、SQLite 连接都不该增长）。
  · 相机帧率应与配置 fps 相符（无连续失败、无丢帧累积暴涨）。
"""
import os
import subprocess
import sys
import time

SVC = "hsfserverpy"
ROUNDS = 3
GAP = 60


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True,
                          text=True).stdout.strip()


def svc_pid():
    return sh("systemctl show %s -p MainPID --value" % SVC)


def read_status(pid):
    out = {}
    try:
        with open("/proc/%d/status" % pid) as fh:
            for ln in fh:
                if ln.startswith(("VmData", "VmRSS", "Threads")):
                    k, v = ln.split(":", 1)
                    out[k] = int(v.split()[0])
    except OSError:
        pass
    return out


def fd_count(pid):
    try:
        return len(os.listdir("/proc/%d/fd" % pid))
    except OSError:
        return -1


def cam_state():
    """通过客户端 bridge 取相机状态（只读，用 admin 会话）。"""
    code = (
        "import sys,time\n"
        "sys.path.insert(0,'/root/Desktop/XinYueC/hsf_client_py')\n"
        "from hsfsrv_client import proto\n"
        "from hsfsrv_client.bridge import LinkBridge\n"
        "b=LinkBridge(); ok,_=b.connect_to('127.0.0.1',6100,'admin','admin')\n"
        "if not ok: print('ERR'); sys.exit(0)\n"
        "r=b.request_sync(proto.CMD_CONFIG_GET) or {}\n"
        "c=r.get('camera') or {}\n"
        "print('%s|%s|%s|%s|%s' % (c.get('online'), c.get('frames'),\n"
        "      c.get('fps'), c.get('actualTransport'), (c.get('error') or '')))\n"
        "b.disconnect_from()\n")
    try:
        return subprocess.run([sys.executable, "-c", code],
                              capture_output=True, text=True,
                              timeout=25).stdout.strip()
    except Exception as exc:
        return "ERR:%s" % exc


def gst_info():
    out = sh("ps -eo pid,pcpu,etime,cmd | grep '[g]st-launch' | head -3")
    return out or "（无 gst）"


def main():
    pid = svc_pid()
    print("服务端 PID=%s  轮间隔=%ds  轮数=%d" % (pid, GAP, ROUNDS))
    print("（VmData=malloc 堆，判泄漏看它；RSS 波动正常）\n")
    rows = []
    for i in range(ROUNDS):
        st = read_status(int(pid)) if pid.isdigit() else {}
        cam = cam_state()
        rows.append({
            "t": time.strftime("%H:%M:%S"),
            "vmdata": st.get("VmData"),
            "rss": st.get("VmRSS"),
            "threads": st.get("Threads"),
            "fds": fd_count(int(pid)) if pid.isdigit() else -1,
            "cam": cam,
        })
        r = rows[-1]
        print("[轮%d %s] VmData=%s KB  VmRSS=%s KB  线程=%s  fd=%s"
              % (i + 1, r["t"], r["vmdata"], r["rss"], r["threads"], r["fds"]))
        print("        相机: %s" % (r["cam"],))
        if i < ROUNDS - 1:
            time.sleep(GAP)

    print("\n=== 泄漏判据 ===")
    d = [r["vmdata"] for r in rows]
    if all(v is not None for v in d):
        d12 = d[1] - d[0]
        d23 = d[2] - d[1]
        print("VmData 增量：Δ12=%+d KB  Δ23=%+d KB" % (d12, d23))
        if d23 <= d12:
            print("  → PASS：增量收敛（Δ23 ≤ Δ12），非泄漏")
        else:
            print("  → **FAIL：增量递增，疑似泄漏**")
    fd = [r["fds"] for r in rows]
    print("fd 数：%s  %s" % (fd, "PASS（恒定）" if len(set(fd)) == 1
                            else "**注意：有增长**"))
    th = [r["threads"] for r in rows]
    print("线程数：%s  %s" % (th, "PASS（恒定）" if len(set(th)) == 1
                             else "**注意：有变化**"))

    # 相机帧率核算（用第一/最后一轮的 frames 差 / 时间跨度）
    try:
        f0 = int(rows[0]["cam"].split("|")[1])
        f2 = int(rows[-1]["cam"].split("|")[1])
        span = ROUNDS * GAP - GAP
        print("相机出帧：%d → %d（%d 秒内 %d 帧 ⇒ %.1f fps，配置 %s fps）"
              % (f0, f2, span, f2 - f0, (f2 - f0) / span if span else 0,
                 rows[-1]["cam"].split("|")[2]))
    except Exception:
        print("相机出帧核算：解析失败")

    print("\n=== gst 进程 ===")
    print(gst_info())
    print("\n=== 负载 ===")
    with open("/proc/loadavg") as fh:
        print(fh.read().strip())


main()
