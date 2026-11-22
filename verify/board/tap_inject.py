# -*- coding: utf-8 -*-
"""往板端触摸屏的 evdev 节点**注入一次真实触摸**（等同手指点一下）。

为什么走 evdev 而不是 XTEST：
    板端 ILITEK 触摸屏同时暴露两个输入设备——
        event3 = "ILITEK ILITEK-TP"      真多点触控（PROP=2 INPUT_PROP_DIRECT）
        event4 = "ILITEK ILITEK-TP Mouse" 驱动层做的指针模拟
    真实手指 ⇒ X 服务器收到 **XI2 触摸** + **核心指针事件**两路。
    XTEST 只能造核心指针事件，**造不出触摸** ⇒ 触摸专属的行为差异（Qt 的
    触摸→鼠标合成、触摸抓取）复现不了。
    直接写 event3 ⇒ 与真手指完全同路。

用法: python3 tap_inject.py <屏幕x> <屏幕y> [按住ms] [设备节点]
"""

import fcntl
import os
import struct
import sys
import time

EV_SYN = 0x00
EV_KEY = 0x01
EV_ABS = 0x03
SYN_REPORT = 0
BTN_TOUCH = 0x14A
ABS_MT_SLOT = 0x2F
ABS_MT_POSITION_X = 0x35
ABS_MT_POSITION_Y = 0x36
ABS_MT_TRACKING_ID = 0x39
ABS_MT_PRESSURE = 0x3A
ABS_MT_TOUCH_MAJOR = 0x30
ABS_X = 0x00
ABS_Y = 0x01

FMT = "llHHi"
SIZE = struct.calcsize(FMT)


def ev(type_, code, value):
    return struct.pack(FMT, 0, 0, type_, code, value)


def get_abs_info(fd, code):
    """EVIOCGABS：读某个 ABS 轴的 min/max。"""
    # _IOC_READ(2) | sizeof(struct input_absinfo)=24 | 'E' | (0x40+code)
    request = (2 << 30) | (24 << 16) | (ord("E") << 8) | (0x40 + code)
    buf = bytearray(24)
    fcntl.ioctl(fd, request, buf, True)
    _v, mn, mx, _f, _fl, _res = struct.unpack("6i", bytes(buf))
    return mn, mx


def main():
    sx = int(sys.argv[1]) if len(sys.argv) > 1 else 640
    sy = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    hold = int(sys.argv[3]) if len(sys.argv) > 3 else 150
    node = sys.argv[4] if len(sys.argv) > 4 else "/dev/input/event3"

    fd = os.open(node, os.O_WRONLY)
    try:
        xmin, xmax = get_abs_info(fd, ABS_MT_POSITION_X)
        ymin, ymax = get_abs_info(fd, ABS_MT_POSITION_Y)
        print("abs X range=%d..%d  Y range=%d..%d"
              % (xmin, xmax, ymin, ymax))
        # [实测标定 2026-11-19] 板端 ILITEK 面板是竖屏原生 + DSI 旋转，
        # X 服务器对它的轴做了 90° 映射（实测：device_x=5175 ⇒ 屏幕 y=547，
        # device_y=10498 ⇒ 屏幕 x=820）：
        #     屏幕 x = device_y / (16384/1280) = device_y / 12.8
        #     屏幕 y = 800 - device_x / (16384/800) = 800 - device_x / 20.48
        # 反解即注入用的设备坐标：
        dx = int((799.0 - sy) * (xmax - xmin) / 799.0)
        dy = int(sx * (ymax - ymin) / 1279.0)
        dx = max(xmin, min(xmax, dx))
        dy = max(ymin, min(ymax, dy))
        print("tap screen(%d,%d) -> device(%d,%d)" % (sx, sy, dx, dy))

        out = b"".join([
            ev(EV_ABS, ABS_MT_SLOT, 0),
            ev(EV_ABS, ABS_MT_TRACKING_ID, 1),
            ev(EV_ABS, ABS_MT_POSITION_X, dx),
            ev(EV_ABS, ABS_MT_POSITION_Y, dy),
            ev(EV_ABS, ABS_MT_PRESSURE, 60),
            ev(EV_KEY, BTN_TOUCH, 1),
            ev(EV_SYN, SYN_REPORT, 0),
        ])
        os.write(fd, out)
        time.sleep(hold / 1000.0)
        out = b"".join([
            ev(EV_KEY, BTN_TOUCH, 0),
            ev(EV_ABS, ABS_MT_PRESSURE, 0),
            ev(EV_ABS, ABS_MT_TRACKING_ID, -1),
            ev(EV_SYN, SYN_REPORT, 0),
        ])
        os.write(fd, out)
        print("tap done")
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
