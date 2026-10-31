#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
屏幕键盘 / 性能悬浮层「背景色挖洞」检测器（Windows 桌面）。

背景
----
GPU 渲染（--gpu）下，键盘页右下角会被挖出一块 **窗口底色（0xF5F5F5）** 矩形：
悬浮层的暗底板和白字、以及压在上面的键盘键帽都不见了。肉眼像「悬浮窗盖住键盘」，
实测内容却是纯底色。软件渲染（--software）与 `XGPU_PRESENT=swap` 均正常。

判据（像素法，无需读框架内部状态）
----------------------------------
正常帧右下角（悬浮层矩形内）是键帽灰 0xE0E0E0 / 白 0xFFFFFF；
异常帧会出现成片的 0xF5F5F5。
本脚本抓 N 次客户区，统计该矩形内 0xF5F5F5 像素数：
    正常 ≈ 6.5k~7.5k（键盘底色本来就有这么多）
    异常 ≈ 16k~17.5k（多出 210×78 的纯色块）

用法
----
    python Tools/windows/kb_overlay_probe.py                 # 默认 GPU 模式 10 次采样
    python Tools/windows/kb_overlay_probe.py --mode software # 软件渲染对照（应全绿）
    python Tools/windows/kb_overlay_probe.py --runs 20 --interval 0.6
    python Tools/windows/kb_overlay_probe.py --env "XGPU_PRESENT=swap"

退出码：0=全部正常（修复已生效）；1=检出挖洞（问题仍在）。
取样帧（异常时最差的那帧）存为 `kb_probe_worst.ppm`（PPM，无需 Pillow 即可查看/转 PNG）。
"""
import argparse
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

# 采样区域：窗口 1250x875 时的悬浮层矩形（右下角，避开状态栏）。
# 实际按窗口尺寸等比取自比例区，见 sample_rect()。
WINDOW_TITLE = "XinYueC 控件可视化测试"
BASE_ENV = {
    "XGUI_KB_AUTOSHOW": "1",
    "XGUI_DEMO_WINDOW_SIZE": "1250x875",
}
NORMAL_MAX = 12000   # 低于此视为正常
BROKEN_MIN = 14000   # 高于此视为异常


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def find_window(title_part):
    found = []

    def cb(hwnd, _):
        n = user32.GetWindowTextLengthW(hwnd)
        if n and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            if title_part in buf.value:
                r = wintypes.RECT()
                user32.GetClientRect(hwnd, ctypes.byref(r))
                found.append((r.right * r.bottom, hwnd))
        return True

    CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(CB(cb), 0)
    found.sort(reverse=True)
    return found[0][1] if found else 0


def grab_client(hwnd):
    """抓窗口客户区，返回 (w, h, bytes-BGRA-topdown)。"""
    r = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    ok = gdi32.BitBlt(mdc, 0, 0, w, h, hdc, 0, 0, 0x00CC0020)  # SRCCOPY
    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h          # 负=自上而下
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    lines = 0
    if ok:
        lines = gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    if not ok or lines != h:
        return 0, 0, b""
    return w, h, buf.raw


def sample_rect(w, h):
    """右下角悬浮层矩形（按 1250x875 实测比例推；无键盘页时退回默认）。"""
    x0 = int(w * 0.72)
    y0 = int(h * 0.80)
    return x0, y0, w - 1, int(h * 0.97)


def count_plate(w, h, raw):
    """统计采样区内 0xF5F5F5（窗口底色）像素数。"""
    if not raw:
        return -1
    x0, y0, x1, y1 = sample_rect(w, h)
    n = 0
    bpl = w * 4
    for y in range(y0, y1):
        base = y * bpl
        row = raw[base + x0 * 4: base + x1 * 4]
        # BGRA，每 4 字节一个像素；0xF5 三通道相等且 alpha 非 0
        for i in range(0, len(row), 4):
            if row[i] == 0xF5 and row[i + 1] == 0xF5 and row[i + 2] == 0xF5:
                n += 1
    return n


def save_ppm(path, w, h, raw):
    px = bytearray(w * h * 3)
    for i in range(w * h):
        b, g, r = raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2]
        px[i * 3], px[i * 3 + 1], px[i * 3 + 2] = r, g, b
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(px))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join("bin", "XGuiWindowDemo_Test.exe"))
    ap.add_argument("--mode", choices=["gpu", "software"], default="gpu")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--warmup", type=float, default=3.0)
    ap.add_argument("--env", action="append", default=[],
                    help='附加环境变量，如 --env "XGPU_PRESENT=swap"（可多次）')
    ap.add_argument("--page", default="9", help="页索引（9=键盘演示）")
    args = ap.parse_args()

    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        print("找不到可执行文件: %s" % exe)
        return 2

    env = dict(os.environ)
    env.update(BASE_ENV)
    for kv in args.env:
        k, _, v = kv.partition("=")
        env[k.strip()] = v.strip()

    cmd = [exe, "--page", args.page, "--" + ("gpu" if args.mode == "gpu" else "software")]
    print("启动: %s (%s)" % (" ".join(cmd), ", ".join(args.env) or "默认"))
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe), env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(args.warmup)
        hwnd = find_window(WINDOW_TITLE)
        if not hwnd:
            print("未找到窗口（标题含『%s』）" % WINDOW_TITLE)
            return 2
        hits = 0
        worst = -1
        worst_raw = None
        worst_size = (0, 0)
        for i in range(args.runs):
            w, h, raw = grab_client(hwnd)
            n = count_plate(w, h, raw) if w else -1
            state = "?"
            if n >= 0:
                if n >= BROKEN_MIN:
                    state = "挖洞"
                    hits += 1
                elif n <= NORMAL_MAX:
                    state = "正常"
                if n > worst:
                    worst, worst_raw, worst_size = n, raw, (w, h)
            print("  采样 %2d/%d  %dx%d  底色像素=%-6d  %s"
                  % (i + 1, args.runs, w, h, n, state))
            time.sleep(args.interval)
        print("-" * 62)
        print("命中 %d/%d 次，最差一帧底色像素=%d（正常<=%d，异常>=%d）"
              % (hits, args.runs, worst, NORMAL_MAX, BROKEN_MIN))
        if worst_raw:
            save_ppm("kb_probe_worst.ppm", worst_size[0], worst_size[1], worst_raw)
            print("最差帧已存: kb_probe_worst.ppm")
        if hits:
            print("结论：问题仍在（右下角出现成片窗口底色，键盘/悬浮层内容丢失）")
            return 1
        print("结论：全部正常")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
