# -*- coding: utf-8 -*-
"""板端相机管线 CPU 分段实测（**零干扰**：不碰相机、不碰服务端）。

用 x264enc 造一段 704×704@25fps H.264（模拟相机推全帧率子码流），
再用 filesrc 回放，逐段量 CPU，定位 mppvideodec/videoconvert/jpegenc
到底谁贵。

用法：python3 /tmp/cpu_bench.py
"""
import os
import subprocess
import sys
import time

TEST_H264 = "/tmp/bench_704_25fps.h264"


def gen_stream():
    if os.path.exists(TEST_H264) and os.path.getsize(TEST_H264) > 200000:
        print("测试流已存在：%s (%.1f MB)"
              % (TEST_H264, os.path.getsize(TEST_H264) / 1e6))
        return
    print("生成测试流（704x704@25fps 20s）…")
    cmd = ["gst-launch-1.0", "-q", "videotestsrc", "num-buffers=500",
           "is-live=false", "!", "video/x-raw,width=704,height=704,"
           "framerate=25/1", "!", "videoconvert",
           "!", "x264enc", "tune=zerolatency", "bitrate=1500", "speed-preset=1",
           "!", "h264parse", "!", "video/x-h264,stream-format=byte-stream",
           "!", "filesink", "location=" + TEST_H264]
    subprocess.run(cmd, check=True)
    print("生成完成：%.1f MB" % (os.path.getsize(TEST_H264) / 1e6))


CASES = [
    ("A 只解码 mppvideodec",
     "filesrc location=%s ! h264parse ! mppvideodec ! fakesink sync=false"),
    ("B 解码+软件换色 videoconvert",
     "filesrc location=%s ! h264parse ! mppvideodec ! videoconvert ! "
     "video/x-raw,format=I420 ! fakesink sync=false"),
    ("C 解码+换色+JPEG 编码",
     "filesrc location=%s ! h264parse ! mppvideodec ! videoconvert ! "
     "video/x-raw,format=I420 ! jpegenc quality=80 ! fakesink sync=false"),
    ("D 完整相机管线（同步放 2fps）",
     "filesrc location=%s ! h264parse ! mppvideodec ! videoconvert "
     "n-threads=4 ! video/x-raw,format=I420 ! videorate ! "
     "video/x-raw,framerate=2/1 ! videoscale ! "
     "video/x-raw,width=704,height=704,format=I420 ! jpegenc quality=80 ! "
     "fakesink sync=false"),
]


def run_case(name, tpl):
    print("\n=== %s ===" % name)
    proc = subprocess.Popen(tpl % TEST_H264, shell=True,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    time.sleep(2.0)                       # 等管线起稳（解析/首帧）
    # 采样 3 次取最大（%CPU 是累计均值，短窗口下用 top 单次）
    peaks = []
    thread_hot = ""
    for _ in range(3):
        out = subprocess.run(
            ["top", "-bn1", "-H", "-p", str(proc.pid)],
            capture_output=True, text=True).stdout
        best = 0.0
        best_name = ""
        for line in out.splitlines()[7:]:
            cols = line.split()
            if len(cols) < 12:
                continue
            try:
                cpu = float(cols[8])
            except ValueError:
                continue
            if cpu > best:
                best, best_name = cpu, cols[-1]
        if best:
            peaks.append(best)
            thread_hot = best_name
        time.sleep(1.5)
    total = 0.0
    try:
        out = subprocess.run(["ps", "-o", "%cpu=", "-p", str(proc.pid)],
                             capture_output=True, text=True).stdout
        total = float(out.strip() or 0)
    except ValueError:
        pass
    try:
        proc.terminate()
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
    print("  进程总 CPU=%5.1f%%   最热线程=%s（%.1f%%）"
          % (total, thread_hot or "-", max(peaks) if peaks else 0))


def main():
    gen_stream()
    for name, tpl in CASES:
        run_case(name, tpl)
    print("\n注意：%CPU 是进程生命周期均值；热线程用 top 单次采样。")


main()
