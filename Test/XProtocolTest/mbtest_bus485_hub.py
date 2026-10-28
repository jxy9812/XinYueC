#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""虚拟 RS485 总线 hub（多从站联调用）。

真实 RS485 半双工多从站总线的关键语义：
  ① 主站发的帧**广播**到总线上所有从站（每个从站都收到同一份字节）；
  ② 只有地址（站号）匹配的从站应答，不匹配的必须保持静默；
  ③ 应答期间总线被占用，其他从站不得同时应答（真实硬件会冲突）；
  ④ 帧间需 ≥3.5 字符静默（由从站/主站各自把握）。

本 hub 用一组 pty 复刻以上语义：
  · 创建 1 个 master pty（交给被测主站程序当串口）
  · 创建 N 个 slave pty（每个模拟从站程序独占一个）
  · master → 所有 slave：广播（真实总线广播语义）
  · slave → master：一次只转发一个从站的完整应答（串行化，避免字节交织）；
    若两个从站在同一时刻都有字节待发 → 记为**应答冲突**（真实总线的冲突场景）
  · 帧流日志 $BASE/bus_frames.log：逐条记录 "M>S" / "S<unit>>M"，供 C 端断言顺序

用法：
  python3 mbtest_bus485_hub.py --slaves 2 [--base /tmp/mbsoak/bus485]
产物：
  $BASE/bus_master_path    master pty 路径（被测主站打开它）
  $BASE/bus_slave_paths   每个从站的 pty 路径（一行一个，顺序=从站序号）
  $BASE/bus_frames.log    帧流日志
"""

import argparse
import os
import select
import struct
import time

BASE = "/tmp/mbsoak/bus485"


def log(fmt, *args):
    print("[bus485 %08.3f] %s" % (time.time() % 100, fmt % args if args else fmt), flush=True)


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)


def read_exact(fd, n, timeout=0.2):
    """尽力读取 n 字节（超时返回已读部分）。"""
    buf = b""
    deadline = time.time() + timeout
    while len(buf) < n:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        r, _, _ = select.select([fd], [], [], remaining)
        if not r:
            break
        try:
            chunk = os.read(fd, n - len(buf))
        except OSError:
            break
        if not chunk:
            break
        buf += chunk
    return buf


def frame_len(buf):
    """按 Modbus RTU 判定帧长；不足返回 None。"""
    if len(buf) < 4:
        return None
    fc = buf[1]
    if fc in (3, 6):
        return 8
    if fc == 16:
        if len(buf) < 7:
            return None
        return 9 + buf[6]
    return 8


def main():
    global BASE
    ap = argparse.ArgumentParser()
    ap.add_argument("--slaves", type=int, default=2)
    ap.add_argument("--units", default="",
                    help="各从站对应的 Modbus 站号（逗号分隔，如 1,2）；缺省按 1..N")
    ap.add_argument("--base", default=BASE)
    args = ap.parse_args()
    BASE = args.base
    if args.units:
        UNIT_IDS = [int(x) for x in args.units.split(",") if x.strip()]
    else:
        UNIT_IDS = [i + 1 for i in range(args.slaves)]
    os.makedirs(BASE, exist_ok=True)

    import tty

    # ---- 创建 pty ----
    master, master_slave = os.openpty()
    tty.setraw(master)
    master_name = os.ttyname(master_slave)
    with open(BASE + "/bus_master_path", "w") as f:
        f.write(master_name)

    slave_fds = []
    slave_names = []
    for i in range(args.slaves):
        m, s = os.openpty()
        tty.setraw(m)
        slave_fds.append(m)
        slave_names.append(os.ttyname(s))
    with open(BASE + "/bus_slave_paths", "w") as f:
        for n in slave_names:
            f.write(n + "\n")
    log("BUS READY master=%s slaves=%s units=%s", master_name, slave_names, UNIT_IDS)

    frame_log = open(BASE + "/bus_frames.log", "a", buffering=1)
    # 从站侧的 pty 端由模拟从站程序打开；hub 持有 master 端与各 slave 的 master 端
    # （与 master_slave 对端、slave_names 对端配套：模拟程序打开的是 slave_names 里的路径，
    #  hub 用 master_fd 与 slave_fds 收发）
    master_fd = master_slave
    # hub 需要读写 master 侧：master 端（master_fd 的对端）是给从站/被测端的路径
    # 约定：被测主站打开 master_name（= master 端），hub 读 master_fd。
    hub_master_fd = master
    for i, fd in enumerate(slave_fds):
        try:
            os.set_blocking(fd, False)
        except OSError:
            pass
    try:
        os.set_blocking(hub_master_fd, False)
    except OSError:
        pass

    master_buf = b""
    slave_buf = [b"" for _ in slave_fds]
    busy_slave = -1        # 当前占用总线（应答中）的从站序号
    conflicts = 0

    while True:
        rlist = [hub_master_fd] + [fd for i, fd in enumerate(slave_fds) if i != busy_slave]
        try:
            rl, _, _ = select.select(rlist, [], [], 0.05)
        except OSError:
            break

        # ---- master → slaves（广播） ----
        if hub_master_fd in rl:
            try:
                data = os.read(hub_master_fd, 256)
            except OSError:
                data = b""
            if data:
                master_buf += data
                frame_log.write("M>S %s\n" % data.hex())
                for fd in slave_fds:
                    try:
                        os.write(fd, data)
                    except OSError:
                        pass

        # ---- slaves → master（串行化，一次一个从站） ----
        for i, fd in enumerate(slave_fds):
            if i == busy_slave:
                continue
            if fd not in rl:
                continue
            try:
                data = os.read(fd, 256)
            except OSError:
                data = b""
            if not data:
                continue
            if busy_slave >= 0:
                # 总线已被占用却又收到应答 → 真实 485 的冲突场景
                conflicts += 1
                uid_i = UNIT_IDS[i] if i < len(UNIT_IDS) else (i + 1)
                uid_b = UNIT_IDS[busy_slave] if busy_slave < len(UNIT_IDS) else (busy_slave + 1)
                log("CONFLICT slave%d answered while slave%d busy", uid_i, uid_b)
                frame_log.write("CONFLICT %d %d\n" % (uid_i, uid_b))
            else:
                busy_slave = i
            try:
                os.write(hub_master_fd, data)
            except OSError:
                pass
            uid = UNIT_IDS[i] if i < len(UNIT_IDS) else (i + 1)
            frame_log.write("S%d>M %s\n" % (uid, data.hex()))

        # ---- 总线占用释放判定：静默窗（3.5 字符 ≈ 5ms @38400）后释放 ----
        if busy_slave >= 0:
            # 简单处理：下一轮若该从站无新数据则视为应答结束
            try:
                r2, _, _ = select.select([slave_fds[busy_slave]], [], [], 0.005)
                if not r2:
                    busy_slave = -1
            except OSError:
                busy_slave = -1

        if conflicts and conflicts % 50 == 1:
            log("conflicts=%d", conflicts)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
