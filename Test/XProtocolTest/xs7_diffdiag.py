#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xs7_diffdiag.py —— snap7 权威实现差分诊断（run/stop + 块操作）。

目的：用 python-snap7（官方 snap7.dll 包装）对 192.168.1.251 (S7-1200, rack0 slot1)
做差分实验，定性『XinYueC C 库缺陷(ours)』还是『S7-1200 设备能力限制(device)』。

子命令：
  runstop   实验1：确认 RUN → plc_stop() → 记录 → 立即恢复 RUN（hot/cold+重连重试）并验证
  blocks    实验2/3：list_blocks()（SZL 0x0D91 等价）与 get_block_info('DB',1)，只读无副作用
  all       依次执行 blocks → runstop（blocks 只读先行，避免停机期间做块实验）

安全纪律（最高优先）：
  - runstop 在 plc_stop() 之后无论成败都进入恢复流程：先 plc_hot_start()，失败则
    重连（disconnect+connect）后再试 hot/cold，最多 MAX_RESTORE_ATTEMPTS 轮，
    直到 get_cpu_state()=='S7CpuStatusRun' 才算恢复成功。
  - 若 CPU 初始不在 RUN，不执行 stop，只尝试恢复到初始状态。
  - 恢复结果如实打印并写入退出码（恢复失败 exit 3）。

抓帧：默认经本机 TCP 中转（127.0.0.1:PORT → PLC:102），按 TPKT 边界逐 PDU 记录
双向 hex（--no-proxy 直连）。S7 PDU 解析仅做读侧标注（rosctr/错误域/参数区），
不修改任何字节——中转是透明的。
"""

import argparse
import socket
import sys
import threading
import time

import snap7
from snap7.client import Client
from snap7.common import error_text

PLC_IP = "192.168.1.251"
PLC_PORT = 102
RACK, SLOT = 0, 1
PROXY_BIND = ("127.0.0.1", 11102)

RUN_STATE = "S7CpuStatusRun"
MAX_RESTORE_ATTEMPTS = 6        # 恢复最多轮数（hot→重连→hot→cold→…）
STOP_WAIT_SEC = 5.0             # stop 后观察状态迁移时长
RESTORE_WAIT_SEC = 10.0         # 每次启动尝试后等待 RUN 时长

# snap7.types.cpu_statuses：0=Unknown 4=Stop 8=Run（Cli_GetPlcStatus 原始值）
from snap7.types import cpu_statuses  # noqa: E402


def hx(data):
    return " ".join("%02X" % b for b in bytearray(data))


# ============================ TCP 中转抓帧 ============================

class TpktRelay:
    """透明 TCP 中转，按 TPKT(0x03 0x00 + 2B 长度) 边界记录完整 PDU 的双向 hex。"""

    def __init__(self, bind, target):
        self.bind = bind
        self.target = target
        self.lock = threading.Lock()
        self.frames = []          # [(dir, bytes)]，dir: 'C>S' 客户端→PLC / 'S>C' PLC→客户端
        self.sock = None
        self.up = False

    def log_frame(self, direction, data):
        with self.lock:
            self.frames.append((direction, bytes(data)))
        print("  [wire %s %3dB] %s" % (direction, len(data), hx(data)))

    @staticmethod
    def _tpkt_split(buf):
        """按 TPKT 头切出完整 PDU，返回 (完整PDU列表, 剩余不完整字节)。"""
        out = []
        while len(buf) >= 4:
            if buf[0] != 0x03 or buf[1] != 0x00:
                # 非 TPKT（不应发生）：丢弃 1 字节防卡死
                out.append(bytes(buf[:1]))
                buf = buf[1:]
                continue
            total = (buf[2] << 8) | buf[3]
            if total < 7 or len(buf) < total:
                break
            out.append(bytes(buf[:total]))
            buf = buf[total:]
        return out, buf

    def _pump(self, src, dst, direction, peer_label):
        buf = b""
        try:
            while True:
                chunk = src.recv(4096)
                if not chunk:
                    break
                done, buf = self._tpkt_split(buf + chunk)
                for pdu in done:
                    if len(pdu) > 4 and pdu[0:2] == b"\x03\x00":
                        self.log_frame(direction, pdu)
                    else:
                        self.log_frame(direction + "?", pdu)
                dst.sendall(chunk)
        except OSError:
            pass
        finally:
            try:
                dst.shutdown(socket.SHUT_WR)
            except OSError:
                pass

    def _serve(self, cli):
        try:
            upstream = socket.create_connection(self.target, timeout=10)
            upstream.settimeout(None)   # 清除 connect 期超时，长空闲不误断（stop/run 观察期）
        except OSError as e:
            print("[relay] 上游连接失败: %s" % e)
            cli.close()
            return
        t1 = threading.Thread(target=self._pump, args=(cli, upstream, "C>S", "client"), daemon=True)
        t2 = threading.Thread(target=self._pump, args=(upstream, cli, "S>C", "plc"), daemon=True)
        t1.start(); t2.start(); t1.join(); t2.join()
        cli.close(); upstream.close()

    def start(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(self.bind)
        self.sock.listen(4)
        self.up = True
        threading.Thread(target=self._accept_loop, daemon=True).start()
        print("[relay] %s:%d -> %s:%d 抓帧中" % (self.bind[0], self.bind[1], self.target[0], self.target[1]))

    def _accept_loop(self):
        while self.up:
            try:
                cli, addr = self.sock.accept()
            except OSError:
                return
            print("[relay] 客户端接入 %s:%s" % (addr[0], addr[1]))
            threading.Thread(target=self._serve, args=(cli,), daemon=True).start()

    def stop(self):
        self.up = False
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass


# ============================ 连接 ============================

def connect_client(host, rack, slot, port=PLC_PORT):
    c = Client()
    # 与 C 库/snap7 惯例一致：local TSAP = 0x01<<8|rack，remote TSAP = 0x03<<8|slot
    c.set_connection_params(host, (0x01 << 8) | rack, (0x03 << 8) | slot)
    # connect(..., tcpport) 内部 set_param(RemotePort) 后再 Cli_ConnectTo（顺序正确）
    c.connect(host, rack, slot, port)
    if not c.get_connected():
        raise RuntimeError("snap7 connect 失败")
    print("[snap7] connected %s:%d rack=%d slot=%d (python-snap7 %s)"
          % (host, port, rack, slot, snap7.__version__))
    return c


def reconnect_client(c, host, port):
    """重连（新通道，新 pduRef 序列），用于恢复流程。"""
    try:
        c.disconnect()
    except Exception:
        pass
    try:
        c.destroy()
    except Exception:
        pass
    return connect_client(host, RACK, SLOT, port)


def describe_native(ret):
    if ret == 0:
        return "0 (成功)"
    try:
        return "%d (0x%06X) %r" % (ret, ret, error_text(ret, "client"))
    except Exception as e:
        return "%d (0x%06X) <error_text 失败: %s>" % (ret, ret, e)


def poll_state(c, seconds, want=None):
    """轮询 CPU 状态直到 want 或超时，返回 (最后状态字符串, 是否达到 want)。"""
    deadline = time.time() + seconds
    last = None
    while time.time() < deadline:
        try:
            last = c.get_cpu_state()
        except Exception as e:
            last = "<get_cpu_state 异常: %s>" % e
        print("  [state] %s" % last)
        if want and last == want:
            return last, True
        time.sleep(0.8)
    return last, bool(want and last == want)


# ============================ 实验 1：run/stop ============================

def cmd_runstop(args):
    relay = None
    host = PLC_IP
    port = PLC_PORT
    if not args.no_proxy:
        relay = TpktRelay(PROXY_BIND, (PLC_IP, PLC_PORT))
        relay.start()
        host = PROXY_BIND[0]
        port = PROXY_BIND[1]

    restore_ok = False
    c = None
    try:
        c = connect_client(host, RACK, SLOT, port)

        # ---- 步骤 1：确认当前状态 ----
        state0 = c.get_cpu_state()
        raw0 = [k for k, v in cpu_statuses.items() if v == state0]
        print("[runstop] 步骤1 初始状态: %s (raw=%s)" % (state0, raw0))

        if state0 != RUN_STATE:
            print("[runstop] 初始非 RUN —— 按纪律不执行 stop，直接尝试恢复 RUN")
        else:
            # ---- 步骤 2：plc_stop() ----
            print("[runstop] 步骤2 plc_stop() ...")
            try:
                ret = c.plc_stop()
                print("[runstop] plc_stop() 返回 %s" % describe_native(ret))
            except Exception as e:
                print("[runstop] plc_stop() 异常: %r" % e)
            time.sleep(0.5)
            poll_state(c, STOP_WAIT_SEC)

        # ---- 步骤 3：恢复纪律（无条件执行）----
        print("[runstop] 步骤3 恢复流程（hot 优先，失败重连重试，再败 cold）")
        for attempt in range(1, MAX_RESTORE_ATTEMPTS + 1):
            mode = "hot" if attempt % 2 == 1 else "cold"
            print("[runstop] 恢复第 %d 轮 (%s_start) ..." % (attempt, mode))
            try:
                fn = c.plc_hot_start if mode == "hot" else c.plc_cold_start
                ret = fn()
                print("[runstop] plc_%s_start() 返回 %s" % (mode, describe_native(ret)))
            except Exception as e:
                print("[runstop] plc_%s_start() 异常: %r" % (mode, e))
            last, ok = poll_state(c, RESTORE_WAIT_SEC, want=RUN_STATE)
            if ok:
                restore_ok = True
                print("[runstop] 恢复成功：CPU 在 RUN（第 %d 轮，%s_start）" % (attempt, mode))
                break
            # 重连后换新通道再试（模拟 open_channel 重试路径）
            print("[runstop] 未达 RUN，重连后再试 ...")
            try:
                c = reconnect_client(c, host, port)
            except Exception as e:
                print("[runstop] 重连失败: %r —— 继续重试" % e)
                time.sleep(2.0)
    finally:
        # ---- 兜底：即使前面任何一步抛异常也要确保恢复 ----
        if not restore_ok and c is not None:
            print("[runstop] 兜底恢复：仍不在 RUN，继续尝试 ...")
            for attempt in range(1, MAX_RESTORE_ATTEMPTS + 1):
                mode = "hot" if attempt % 2 == 1 else "cold"
                try:
                    fn = c.plc_hot_start if mode == "hot" else c.plc_cold_start
                    fn()
                except Exception as e:
                    print("[runstop] 兜底 %s_start 异常: %r" % (mode, e))
                last, ok = poll_state(c, RESTORE_WAIT_SEC, want=RUN_STATE)
                if ok:
                    restore_ok = True
                    print("[runstop] 兜底恢复成功（第 %d 轮）" % attempt)
                    break
                try:
                    c = reconnect_client(c, host, port)
                except Exception:
                    time.sleep(2.0)

    # ---- 抓帧摘要：S7 PDU 层标注 ----
    if relay:
        summarize_control_frames(relay)
        relay.stop()

    if c:
        try:
            final = c.get_cpu_state()
        except Exception as e:
            final = "<异常 %r>" % e
        print("[runstop] 最终状态: %s" % final)
        restore_ok = restore_ok and (final == RUN_STATE)
        try:
            c.disconnect(); c.destroy()
        except Exception:
            pass
    print("[runstop] 恢复纪律结论: %s" % ("RUN 已恢复并验证" if restore_ok else "恢复失败!"))
    return 0 if restore_ok else 3


def annotate_s7(pdu):
    """TPKT+COTP 之后的 S7 PDU 读侧标注（不改字节）。"""
    # 找 COTP：TPKT 4B + COTP(长度字节 L: 1B L + L 字节)
    if len(pdu) < 4:
        return ""
    cotp_len = pdu[4]
    s7 = pdu[5 + cotp_len:]
    if len(s7) < 10 or s7[0] != 0x32:
        return ""
    rosctr = s7[1]
    param_len = (s7[6] << 8) | s7[7]
    data_len = (s7[8] << 8) | s7[9]
    tag = "S7 rosctr=0x%02X paramLen=%d dataLen=%d" % (rosctr, param_len, data_len)
    if rosctr == 0x01 and param_len >= 1:
        func = s7[10]
        tag += " func=0x%02X(%s) param=%s" % (
            func, {0x28: "RUN", 0x29: "STOP"}.get(func, "?"),
            hx(s7[10:10 + min(param_len, 26)]))
    elif rosctr in (0x02, 0x03) and len(s7) >= 12:
        tag += " 错误域 errorClass=0x%02X errorCode=0x%02X" % (s7[10], s7[11])
        if param_len >= 1:
            tag += " func回显=0x%02X" % s7[12]
    elif rosctr == 0x07:
        tag += " Ack_Data(用户数据)"
    return tag


def summarize_control_frames(relay):
    print("[runstop] ===== 中转抓帧摘要（完整 TPKT PDU + S7 标注）=====")
    for d, pdu in relay.frames:
        print("  [%s] %s\n        %s" % (d, hx(pdu), annotate_s7(pdu)))


# ============================ 实验 2/3：块操作 ============================

def cmd_blocks(args):
    relay = None
    host = PLC_IP
    if not args.no_proxy:
        relay = TpktRelay(PROXY_BIND, (PLC_IP, PLC_PORT))
        relay.start()
        host = PROXY_BIND[0]
    try:
        c = connect_client(host, RACK, SLOT, relay and PROXY_BIND[1] or PLC_PORT)
        state = c.get_cpu_state()
        print("[blocks] CPU 状态: %s" % state)

        # ---- 实验 2：list_blocks（≈ SZL 0x0D91）----
        print("[blocks] 实验2 list_blocks() ...")
        try:
            bl = c.list_blocks()
            print("[blocks] list_blocks 成功: OB=%d FB=%d FC=%d SFB=%d SFC=%d DB=%d SDB=%d"
                  % (bl.OBCount, bl.FBCount, bl.FCCount, bl.SFBCount,
                     bl.SFCCount, bl.DBCount, bl.SDBCount))
            verdict2 = "success"
        except Exception as e:
            print("[blocks] list_blocks 异常: %r" % e)
            verdict2 = "exception: %s" % e

        # 补充：直接 read_szl(0x0D91) 原始调用
        print("[blocks] 实验2b read_szl(0x0D91,0) ...")
        try:
            szl = c.read_szl(0x0D91, 0x0000)
            hdr_len = getattr(szl.Header, "LengthDR", None)
            print("[blocks] read_szl 成功: Header=%r Data[:32]=%r"
                  % (hdr_len, bytes(szl.Data)[:32]))
            verdict2b = "success"
        except Exception as e:
            print("[blocks] read_szl(0x0D91) 异常: %r" % e)
            verdict2b = "exception: %s" % e

        # ---- 实验 3：get_block_info('DB',1) ----
        print("[blocks] 实验3 get_block_info('DB',1) ...")
        try:
            bi = c.get_block_info("DB", 1)
            print("[blocks] get_block_info 成功: type=%d num=%d lang=%d MC7Size=%d loadSize=%d"
                  % (bi.BlkType, bi.BlkNumber, bi.BlkLang, bi.MC7Size, bi.LoadSize))
            verdict3 = "success"
        except Exception as e:
            print("[blocks] get_block_info 异常: %r" % e)
            verdict3 = "exception: %s" % e

        if relay:
            print("[blocks] ===== 中转抓帧摘要 =====")
            for d, pdu in relay.frames:
                print("  [%s] %s\n        %s" % (d, hx(pdu), annotate_s7(pdu)))
            relay.stop()

        print("[blocks] 结论: list_blocks=%s; read_szl_0x0D91=%s; get_block_info_DB1=%s"
              % (verdict2, verdict2b, verdict3))
        try:
            c.disconnect(); c.destroy()
        except Exception:
            pass
        return 0
    finally:
        if relay:
            relay.stop()


def main(argv):
    p = argparse.ArgumentParser(description="snap7 权威差分诊断（run/stop + 块操作）")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("runstop"); s.set_defaults(fn=cmd_runstop)
    s.add_argument("--no-proxy", action="store_true", help="直连不走中转（不抓帧）")
    s = sub.add_parser("blocks"); s.set_defaults(fn=cmd_blocks)
    s.add_argument("--no-proxy", action="store_true", help="直连不走中转（不抓帧）")
    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
