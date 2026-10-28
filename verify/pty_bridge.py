#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pty 桥：把两条 pty 从端桥接成一对虚拟串口（板端联调用）。

用法（板子/任何 Linux）:
    python3 pty_bridge.py
输出两行：
    BRIDGE-A /dev/pts/X
    BRIDGE-B /dev/pts/Y

分别交给两个进程打开（如 modbus_rtu_slave_sim.py 用 A、
modbus_rtu_master_sweep.py 用 B），字节双向透传——等效一对
com0com 虚拟串口。Ctrl+C 退出。

实现：os.openpty 两次，两个 master fd 之间 select 互拷；
termios 把从端置 raw（关回显/关行规程，否则 0x03 等字节被吃）。
"""
import os
import select
import sys
import termios
import tty


def make_end():
    mfd, sfd = os.openpty()
    # 从端置 raw：Modbus RTU 是二进制透传，不能让 tty 规程吃字节
    tty.setraw(sfd)
    termios.tcflush(sfd, termios.TCIOFLUSH)
    path = os.ttyname(sfd)
    # 从端 fd 必须由本进程持有：pts 节点在「无人打开」时会被内核回收
    # （/dev/pts/N 直接消失），测试进程随后按路径 open 会 ENOENT。
    return mfd, sfd, path


def main():
    a_fd, a_keep, a_path = make_end()
    b_fd, b_keep, b_path = make_end()
    print("BRIDGE-A %s" % a_path, flush=True)
    print("BRIDGE-B %s" % b_path, flush=True)
    try:
        while True:
            r, _, _ = select.select([a_fd, b_fd], [], [], 1.0)
            if a_fd in r:
                data = os.read(a_fd, 4096)
                if data:
                    os.write(b_fd, data)
            if b_fd in r:
                data = os.read(b_fd, 4096)
                if data:
                    os.write(a_fd, data)
    except KeyboardInterrupt:
        pass
    finally:
        os.close(a_fd)
        os.close(b_fd)
        os.close(a_keep)
        os.close(b_keep)
    return 0


if __name__ == "__main__":
    sys.exit(main())
