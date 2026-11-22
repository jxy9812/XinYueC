# -*- coding: utf-8 -*-
"""板端相机管线重构验证（零干扰：只用离线 H.264，不碰相机/服务端）。

对比：
  A 现管线（videoconvert + videorate + videoscale + jpegenc=软编）
  B 全硬（mppvideodec ! mppjpegenc）—— 相机帧率直通
  C 全硬 + videorate 抽帧（看 videorate 在解码器 buffer 上贵不贵）
输出每帧 CPU 与 JPEG 体积。
"""
import os
import subprocess
import time

SRC = "/tmp/bench2.h264"


def ensure_src():
    if os.path.exists(SRC) and os.path.getsize(SRC) > 500000:
        return
    print("生成测试流 704x704@25fps 12s …")
    subprocess.run([
        "gst-launch-1.0", "-q", "videotestsrc", "num-buffers=300",
        "is-live=false", "!", "video/x-raw,width=704,height=704,"
        "framerate=25/1", "!", "videoconvert", "!", "x264enc",
        "tune=zerolatency", "bitrate=2000", "speed-preset=1", "!",
        "h264parse", "!", "video/x-h264,stream-format=byte-stream", "!",
        "filesink", "location=" + SRC], check=True)
    print("  %.1f MB" % (os.path.getsize(SRC) / 1e6))


def run(name, args, secs=6.0):
    print("\n=== %s ===" % name)
    out_path = "/tmp/out_%s.jpg" % name.split()[0]
    full = ["gst-launch-1.0", "-q"] + args + [
        "!", "filesink", "location=" + out_path]
    t0 = time.time()
    p = subprocess.Popen(full, stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE)
    time.sleep(secs)
    # 子进程 CPU（user+sys）
    cpu = 0.0
    try:
        with open("/proc/%d/stat" % p.pid) as fh:
            parts = fh.read().split()
        utime, stime = int(parts[13]), int(parts[14])
        hz = os.sysconf("SC_CLK_TCK")
        cpu = (utime + stime) / hz
    except Exception:
        pass
    p.kill()
    try:
        p.wait(timeout=3)
    except Exception:
        pass
    err = (p.stderr.read() or b"").decode("utf-8", "replace")
    size = os.path.getsize(out_path) if os.path.exists(out_path) else 0
    # 数 JPEG 帧（SOI 标记）
    frames = 0
    try:
        with open(out_path, "rb") as fh:
            data = fh.read()
        frames = data.count(b"\xff\xd8\xff")
    except Exception:
        pass
    print("  wall=%.1fs CPU=%.2fs  帧数=%d  体积=%.1f MB  每帧CPU=%.1f ms"
          % (secs, cpu, frames, size / 1e6,
             (cpu * 1000.0 / frames) if frames else 0))
    for ln in err.splitlines()[:2]:
        if ln.strip():
            print("  gst:", ln.strip()[:120])


def main():
    ensure_src()
    print("源：%s (%.1f MB)" % (SRC, os.path.getsize(SRC) / 1e6))
    base = ["filesrc", "location=" + SRC, "!", "h264parse", "!"]
    run("A 现管线 软编", base + [
        "mppvideodec", "!", "videoconvert", "n-threads=4", "!",
        "video/x-raw,format=I420", "!", "videorate", "!",
        "video/x-raw,framerate=2/1", "!", "videoscale", "!",
        "video/x-raw,width=704,height=704,format=I420", "!",
        "jpegenc", "quality=80"])
    run("B 全硬 直通", base + [
        "mppvideodec", "!", "mppjpegenc", "q-factor=90"])
    run("C 全硬+抽帧", base + [
        "mppvideodec", "!", "videorate", "!",
        "video/x-raw,framerate=2/1", "!", "mppjpegenc", "q-factor=90"])


main()
