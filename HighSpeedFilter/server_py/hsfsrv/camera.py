# -*- coding: utf-8 -*-
"""相机采集器：RTSP →（板端硬解）→ JPEG 帧队列。

为什么是这个组合（三条都是实测踩出来的）：

1. **自研最小 RTSP 客户端**（不用 GStreamer 的 rtspsrc）
   本相机（TP-LINK IPC）的 SDP 有 **3 条轨**：video / audio /
   `application/TP-LINK` smart 轨。GStreamer 的 rtspsrc 会**逐条 SETUP**
   ⇒ 本相机之后**一个 RTP 包都不吐**（实测 0 帧）；而只 SETUP **track1**
   （视频轨）的自研客户端，同一时刻实测 3 秒收 3014 帧。
   ⇒ 必须自己握手、只建视频轨。

2. **gst-launch 子进程做硬解硬编**（不在 Python 里驱动 GStreamer）
   板端（RK3568）**没有 PyGObject** ⇒ 进程内驱动不了 GStreamer；
   而板端有 Rockchip MPP 硬件编解码（`libgstrockchipmpp.so`：
   mppvideodec / mppjpegenc）⇒ 子进程 + 管道最省事，且**进程崩了不影响
   主服务**。实测 704x704@2fps 只吃 **11% 单核**（4 核板 ≈3% 总占用）。

3. **帧从 stdin 进、从 stdout 出**
   管道：`fdsrc fd=0 ! h264parse ! mppvideodec ! videoconvert ! videorate
   ! video/x-raw,framerate=N/1 ! videoscale ! video/x-raw,width=W,height=W,
   format=NV12 ! mppjpegenc ! fdsink fd=1`
   - `videorate` 放在 `videoscale` **之前** ⇒ 先降帧再缩放（省 CPU）。
   - `videoscale` 后**必须显式给 format=NV12**：只写 width/height 会协商
     不上，管道**静默卡住、0 帧**（实测过）。
   - 帧率抽帧用 capsfilter `framerate=N/1`。

管道里跑的 RTSP 侧我们只吐 **Annex-B 字节流**（带 00 00 00 01 起始码），
`h264parse` 直接吃。

【相机会话卫生】本相机的会话状态**很敏感**：客户端进程残留不退出时，其它
连接会「能握手但收不到 RTP」⇒ 本模块 close() 必须先 TEARDOWN 再关 socket，
异常退出路径也要走 close()。
"""

import hashlib
import os
import re
import shutil
import socket
import struct
import subprocess
import threading
import time

from . import proto
from hsfsrv import camera_mac

#: RTSP 的 H.264 payload type（本相机实测 96）。
_RTP_PT_H264 = 96
#: JPEG 起始/结束标记。
_JPEG_SOI = b"\xff\xd8"
_JPEG_EOI = b"\xff\xd9"

#: RTSP 读**静默判死**阈值（秒）：连续这么久收不到任何字节 ⇒ 判连接失效、
#: 抛 CameraError 让重连循环接手。
#:
#: [为什么必须有 2026-11-20 板端实锅] 相机重启时 TCP 可能**半死**（收不到
#: FIN/RST，WiFi 掉包也会）⇒ `recv` 每 timeout 秒超时一次、`continue`
#: 永远空转 ⇒ 采集线程永久挂住、**重连循环根本轮不到** ⇒ 现场「重启相机
#: 后画面就没了」且日志一条都没有。
#:
#: [30s 而不是 10s 2026-11-20 板端实测] 板端 WiFi 实测 **15% 丢包、RTT
#: 抖动到 300ms**；TCP 传输 RTP 时丢包靠重传恢复，一次窗口卡死可能十几秒
#: 才回来。判死设 10s ⇒ **每次刚播就被自己砍断**，反而永远出不了帧（实锅：
#: 阈值 10s 时板端连续 5 次「静默超时」，放宽后恢复出帧）。
#: 30s 兼顾：真断线最多等 30s 就重连；弱链丢包有足够时间自愈。
RTSP_SILENT_TIMEOUT_S = 30.0


def _md5(text):
    return hashlib.md5(text.encode("utf-8")).hexdigest()


class CameraError(Exception):
    """采集链路错误（握手失败/解码失败/超时）。"""


# ---------------------------------------------------------------------------
# 最小 RTSP 客户端
# ---------------------------------------------------------------------------

class RtspClient(object):
    """只建**一条视频轨**的最小 RTSP 客户端（Digest 认证 + TCP interleaved）。

    [为什么只建一条轨] 见模块注释第 1 条。
    [为什么自己写而不是用库] 需要精确控制「只 SETUP track1 / 优雅 TEARDOWN /
    退避重连」，且本机与板端都没有可用的 RTSP 库（GStreamer 对本相机无效）。
    """

    def __init__(self, host, port, username, password, path, timeout=5.0):
        self.host = host
        self.port = int(port)
        self.username = username or ""
        self.password = password or ""
        self.path = path
        self.timeout = float(timeout)
        self.sock = None
        self._seq = 0
        self._realm = ""
        self._nonce = ""
        self._session = ""
        self._sdp = ""

    # -- 低层收发 ---------------------------------------------------------

    def _send(self, method, uri, extra="", auth=True):
        self._seq += 1
        req = "%s %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: hsf-camera\r\n" % (
            method, uri, self._seq)
        if auth and self._nonce:
            req += self._auth_header(method, uri)
        if extra:
            req += extra
        req += "\r\n"
        self.sock.sendall(req.encode("utf-8"))
        return self._read_response()

    def _auth_header(self, method, uri):
        ha1 = _md5("%s:%s:%s" % (self.username, self._realm, self.password))
        ha2 = _md5("%s:%s" % (method, uri))
        resp = _md5("%s:%s:%s" % (ha1, self._nonce, ha2))
        return ('Authorization: Digest username="%s", realm="%s", nonce="%s", '
                'uri="%s", response="%s"\r\n' % (
                    self.username, self._realm, self._nonce, uri, resp))

    def _read_response(self):
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise CameraError("RTSP 连接被对端关闭")
            buf += chunk
        head, _, rest = buf.partition(b"\r\n\r\n")
        text = head.decode("utf-8", "replace")
        lines = text.split("\r\n")
        status = lines[0] if lines else ""
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                key, val = line.split(":", 1)
                headers[key.strip().lower()] = val.strip()
        # Digest 挑战（相机会同时下发 Basic 与 Digest ⇒ 只取 Digest 那条）
        chal = headers.get("www-authenticate", "")
        if chal.lower().startswith("digest"):
            m = re.search(r'realm="([^"]*)"', chal)
            if m:
                self._realm = m.group(1)
            m = re.search(r'nonce="([^"]*)"', chal)
            if m:
                self._nonce = m.group(1)
        # Content-Length 指定的 body
        body = rest
        try:
            need = int(headers.get("content-length", "0") or 0)
        except ValueError:
            need = 0
        while len(body) < need:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            body += chunk
        return status, headers, body

    # -- 握手 -------------------------------------------------------------

    def connect(self):
        """DESCRIBE → SETUP(track1) → PLAY；成功后可读 RTP。"""
        self.close()
        self.sock = socket.create_connection((self.host, self.port),
                                            timeout=self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        uri = "rtsp://%s:%d/%s" % (self.host, self.port, self.path)

        status, _hdr, body = self._send("DESCRIBE", uri,
                                        "Accept: application/sdp\r\n",
                                        auth=False)
        if "401" in status:
            # 带 Digest 重发（相机的 nonce 就在这条 401 里）
            status, _hdr, body = self._send("DESCRIBE", uri,
                                            "Accept: application/sdp\r\n")
        if "200" not in status:
            raise CameraError("DESCRIBE 失败：%s" % status)
        self._sdp = body.decode("utf-8", "replace")

        # [只 SETUP 视频轨] 从 SDP 里取第一条 video 轨的 control（track1）
        track = "track1"
        m = re.search(r"m=video[^\n]*\n(?:.*\n)*?a=control:(\S+)", self._sdp)
        if m:
            track = m.group(1)
        setup_uri = "rtsp://%s:%d/%s/%s" % (self.host, self.port, self.path,
                                            track)
        status, headers, _ = self._send(
            "SETUP", setup_uri,
            "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")
        if "200" not in status:
            raise CameraError("SETUP 失败：%s" % status)
        self._session = headers.get("session", "").split(";")[0].strip()

        extra = ""
        if self._session:
            extra += "Session: %s\r\n" % self._session
        extra += "Range: npt=0.000-\r\n"
        status, _hdr, _ = self._send("PLAY", uri, extra)
        if "200" not in status:
            raise CameraError("PLAY 失败：%s" % status)
        return True

    def sdp(self):
        return self._sdp

    def close(self):
        """优雅关闭：先 TEARDOWN，再关 socket（相机会话卫生）。"""
        if self.sock is None:
            return
        try:
            uri = "rtsp://%s:%d/%s" % (self.host, self.port, self.path)
            self._send("TEARDOWN", uri)
        except Exception:
            pass
        try:
            self.sock.close()
        except Exception:
            pass
        self.sock = None

    # -- 僵尸会话清理（2026-11-20 板端实锅）--------------------------------
    #
    # [为什么需要] 相机（TP-LINK）并发 RTSP 会话数很少（2~4）。链路半死时
    # `connect()` 拿到 session 后 socket 就废了 ⇒ `close()` 在**同一个死
    # socket** 上发 TEARDOWN 必然发不出去 ⇒ 相机会话泄漏。泄漏几条后相机
    # 仍接受 SETUP/PLAY 但**不再推流**（流仍指向僵尸会话）⇒ 现场表现为
    # 「重启相机后画面没了，怎么重连都出不了帧，连别的客户端也拉不到」。
    # 实测：停掉全部客户端等 3 分钟（相机会话超时回收）后单次拉流立刻恢复
    # （317 NAL / 798KB / 15s）。
    # ⇒ 清理走**新连接**：RTSP 允许 TEARDOWN 用另一条连接发（只认 Session
    #   头，不要求同一条 TCP）——这样死 socket 上的会话也能清掉。
    @staticmethod
    def teardown_stale(host, port, username, password, path, session,
                       timeout=4.0):
        """用**新连接**给一个可能泄漏的会话发 TEARDOWN；失败静默。

        流程：新连接 → DESCRIBE（拿 nonce，握手里会做 Digest）→ 带
        Session 头发 TEARDOWN。任何一步失败都不抛（清理是尽力而为）。
        """
        if not session:
            return False
        probe = RtspClient(host, port, username, password, path,
                           timeout=timeout)
        try:
            probe.connect()
            uri = "rtsp://%s:%d/%s" % (host, port, path)
            status, _h, _b = probe._send(
                "TEARDOWN", uri, "Session: %s\r\n" % session)
            return "200" in status
        except Exception:                       # noqa: BLE001
            return False
        finally:
            try:
                if probe.sock is not None:
                    probe.sock.close()
            except Exception:                   # noqa: BLE001
                pass
            probe.sock = None

    # -- 读 RTP → Annex-B ES ---------------------------------------------

    def read_es(self, sink, stop_flag):
        """读 interleaved RTP，把 H.264 NAL 重组成 Annex-B 字节流喂 sink。

        [sink] 可调用对象，接收 bytes（连续 ES）。
        [stop_flag] 可调用对象，返回 True 表示要停。
        """
        buf = b""
        pending = None      # FU-A 正在重组的分片
        # [静默判死 2026-11-20 板端实锅] 相机重启/链路断时 TCP 可能半死
        # （收不到 FIN/RST）⇒ 原来 `except socket.timeout: continue` 是
        # **永久空转**：既不抛错也不重连，采集线程挂死，现场「重启相机
        # 画面就没了」且零日志。现在连续 RTSP_SILENT_TIMEOUT_S 秒零字节
        # ⇒ 抛 CameraError，由外层重连循环接手。
        silent_since = time.time()
        while not stop_flag():
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                if time.time() - silent_since >= RTSP_SILENT_TIMEOUT_S:
                    raise CameraError(
                        "RTSP 静默超时（%.0fs 无数据 ⇒ 判连接失效，重连）"
                        % RTSP_SILENT_TIMEOUT_S)
                continue
            except OSError as exc:
                raise CameraError("RTSP 读失败：%s" % exc)
            if not chunk:
                raise CameraError("RTSP 连接被对端关闭")
            silent_since = time.time()
            buf += chunk
            while True:
                idx = buf.find(b"\x24")
                if idx < 0:
                    buf = b""
                    break
                if len(buf) < idx + 4:
                    buf = buf[idx:]
                    break
                length = (buf[idx + 2] << 8) | buf[idx + 3]
                if len(buf) < idx + 4 + length:
                    buf = buf[idx:]
                    break
                payload = buf[idx + 4: idx + 4 + length]
                buf = buf[idx + 4 + length:]
                if not payload:
                    continue
                # RTP 头：V/P/X/CC + M/PT + seq + ts + ssrc
                if len(payload) < 12:
                    continue
                cc = payload[0] & 0x0F
                offset = 12 + cc * 4
                if payload[0] & 0x10 and len(payload) >= offset + 4:
                    ext_len = struct.unpack(">H", payload[offset + 2:offset + 4])[0]
                    offset += 4 + ext_len * 4
                ptype = payload[1] & 0x7F
                if ptype != _RTP_PT_H264:
                    continue
                data = payload[offset:]
                if not data:
                    continue
                nal_type = data[0] & 0x1F
                if nal_type == 24:          # STAP-A（聚合包）
                    pos = 1
                    while pos + 2 <= len(data):
                        nsize = (data[pos] << 8) | data[pos + 1]
                        pos += 2
                        if pos + nsize > len(data):
                            break
                        sink(b"\x00\x00\x00\x01" + data[pos:pos + nsize])
                        pos += nsize
                elif nal_type in (28, 29):   # FU-A / FU-B（分片包）
                    head = data[1]
                    start = bool(head & 0x80)
                    end = bool(head & 0x40)
                    frag_type = head & 0x1F
                    if start:
                        pending = bytearray([(data[0] & 0xE0) | frag_type])
                    if pending is None:
                        continue
                    pending += data[2:]
                    if end:
                        sink(b"\x00\x00\x00\x01" + bytes(pending))
                        pending = None
                elif 1 <= nal_type <= 23:    # 单 NAL
                    sink(b"\x00\x00\x00\x01" + data)


# ---------------------------------------------------------------------------
# JPEG 工具
# ---------------------------------------------------------------------------

def jpeg_size(data):
    """从 JPEG 字节流里读出宽高（SOF0/SOF2 段）；读不出返回 (0, 0)。"""
    idx = 2
    while idx + 9 < len(data):
        if data[idx] != 0xFF:
            idx += 1
            continue
        marker = data[idx + 1]
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            idx += 2
            continue
        if idx + 4 > len(data):
            break
        seg_len = struct.unpack(">H", data[idx + 2:idx + 4])[0]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            if idx + 9 <= len(data):
                height = struct.unpack(">H", data[idx + 5:idx + 7])[0]
                width = struct.unpack(">H", data[idx + 7:idx + 9])[0]
                return width, height
        idx += 2 + seg_len
    return 0, 0


def split_jpeg_stream(data):
    """把连续 JPEG 流切成一张张 JPEG（按 SOI/EOI 标记）。

    [为什么不能按长度切] JPEG 里可能有 0xFFD9 出现在压缩数据中（字节填充），
    但**按 SOI 起、EOI 止**切在实践中可靠（PPM/FFB 场景已验证）；切不出
    就丢弃这一段继续找下一个 SOI。
    """
    frames = []
    pos = 0
    while True:
        start = data.find(_JPEG_SOI, pos)
        if start < 0:
            break
        end = data.find(_JPEG_EOI, start + 2)
        if end < 0:
            break
        frames.append(data[start:end + 2])
        pos = end + 2
    return frames


# ---------------------------------------------------------------------------
# NAL 类型（ES 直传的 GOP 缓冲要用；nal 是含 00000001 起始码的 Annex-B）
# ---------------------------------------------------------------------------

#: NAL 起始码长度（Annex-B 允许 3B/4B；本采集器产出统一 4B，但解析按
#: 跳过前导 0 的方式写，兼容两种）。
def _nal_type(nal, h265):
    """NAL 单元类型：H.264 取 header 低 5 位；H.265 取第 2 字节高 6 位。"""
    body = nal
    while len(body) > 1 and body[0] == 0:
        body = body[1:]
    if len(body) < 2:
        return -1
    if h265:
        return (body[1] >> 1) & 0x3F
    return body[1] & 0x1F


def _nal_is_param_set(t, h265):
    """参数集：H.264 SPS=7/PPS=8；H.265 VPS=32/SPS=33/PPS=34。"""
    return (t in (7, 8)) if not h265 else (t in (32, 33, 34))


def _nal_is_idr(t, h265):
    """IDR 帧：H.264 =5；H.265 =16..21（BLA/IDR 族）。"""
    return (t == 5) if not h265 else (16 <= t <= 21)


def _nal_is_vcl(t, h265):
    """VCL（图像数据）：H.264 =1..5；H.265 =0..31。"""
    return (1 <= t <= 5) if not h265 else (0 <= t <= 31)


# ---------------------------------------------------------------------------
# 采集器
# ---------------------------------------------------------------------------

class CameraFrame(object):
    """一帧画面（JPEG 字节 + 尺寸 + 时间戳）。"""

    __slots__ = ("data", "width", "height", "ts_ms", "seq")

    def __init__(self, data, width, height, ts_ms, seq):
        self.data = data
        self.width = width
        self.height = height
        self.ts_ms = ts_ms
        self.seq = seq


class CameraCollector(object):
    """相机采集器：拉流 →（两路二选一）。

    [transport="stream" —— 2026-11-19 架构转向，**缺省**]
    RTSP 拉到的 H.264/H.265 子码流（Annex-B NAL）**不解码直接转发**：
    sink 回调逐 NAL 送 GOP 缓冲与待发队列，解码在客户端（桌面 PyAV /
    浏览器 WebCodecs）。gst 子进程**整个不启** ⇒ 服务端不再付解码/编码
    CPU（板端实测旧路 gst 58%+服务端 34% 单核），帧率从抽帧后的 2fps
    上到相机自身的 20fps。
    [transport="jpeg" —— 旧路兜底] gst 子进程硬解→软编 JPEG，走
    「读 stdout 切帧」老路径（排查问题/老客户端时可切回）。

    线程模型（两路同构）：一个工作线程「RTSP 连接 + NAL/ES 产出」，jpeg
    路另有一个线程读 gst stdout。
    """

    #: ES 模式：GOP 缓冲/待发队列的字节上限。超过 ⇒ 清队等下一 GOP（缺
    #: 中间 NAL 的流没有解码意义，宁可整段丢也不发残流）。512Kbps × 4s
    #: ≈ 256KB，双倍余量。
    ES_GOP_MAX_BYTES = 512 * 1024
    ES_PENDING_MAX_BYTES = 512 * 1024

    def __init__(self, settings, log=None):
        """:param settings: dict（见 app.py 的 _camera_settings）。"""
        self.settings = dict(settings or {})
        self._log = log or (lambda *a, **k: None)
        self._lock = threading.Lock()
        self._latest = None
        self._seq = 0
        self._frames = 0
        self._max_frame_bytes = 0
        self._online = False
        self._error = ""
        self._stop = threading.Event()
        self._thread = None
        self._proc = None
        # ---- ES 直传（transport=stream）状态 ----
        self._es_mode = (self.settings.get("transport")
                         or proto.CAMERA_TRANSPORT_DEF) \
            == proto.CAMERA_TRANSPORT_STREAM
        self._es_codec = proto.CAMERA_CODEC_H264      # SDP 探测后更新
        self._es_codec_detected = False
        self._es_seq = 0                              # NAL 序号（frameId）
        self._es_gop = []                             # 最近 GOP 的 NAL（自举用）
        self._es_gop_bytes = 0
        self._es_gop_ready = False                    # 见过参数集才入缓冲
        self._es_pending = []                         # 待发布 NAL（drain 消费）
        self._es_pending_bytes = 0
        self._es_nals = 0
        self._es_dropped = 0
        # [MAC 连接 2026-11-20] resolveMode=mac 时连接前解析到的当前 IP
        # （status 回显给界面：「MAC aa:.. → 192.168.1.x」）。
        self._resolved_ip = ""

    # -- 生命周期 ---------------------------------------------------------

    def start(self):
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="camera-collector")
        self._thread.daemon = True
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._teardown_proc()
        thread = self._thread
        if thread and thread.is_alive():
            # [join 必须盖过 socket 读超时 2026-11-19 审查实修] read_es 的
            # recv 阻塞最长 timeoutMs（缺省 5s）才检查一次 stop ⇒ 3s 的
            # join 等不到旧线程 ⇒ _restart 立刻起新线程 ⇒ **两个读线程
            # 共抢一个 RTSP 会话**（本相机单会话 ⇒ 新连接拿不到流）。
            self.join_reader(thread)
        self._thread = None

    def join_reader(self, thread):
        """等旧读线程退出；上限 = 握手超时 + 2s（比 recv 阻塞窗长）。"""
        grace = (int(self.settings.get("timeoutMs") or 5000)) / 1000.0 + 2.0
        thread.join(timeout=grace)
        return not thread.is_alive()

    def apply_settings(self, settings):
        """运行期改配置：重启采集（连接参数变了必须重连）。"""
        with self._lock:
            changed = dict(settings or {}) != self.settings
            self.settings = dict(settings or {})
            # [transport 切换] ES/JPEG 两路的线程产出路径完全不同 ⇒
            # 模式标志必须跟着配置重算（__init__ 只算一次是不够的）。
            self._es_mode = (self.settings.get("transport")
                             or proto.CAMERA_TRANSPORT_DEF) \
                == proto.CAMERA_TRANSPORT_STREAM
            self._es_codec_detected = False
            self._es_gop = []
            self._es_gop_bytes = 0
            self._es_gop_ready = False
            self._es_pending = []
            self._es_pending_bytes = 0
        if changed:
            self._restart()

    def _restart(self):
        self._stop.set()
        self._teardown_proc()
        thread = self._thread
        if thread and thread.is_alive():
            # [同 stop] join 盖过 recv 阻塞窗，旧线程死了才起新的
            # （两线程共抢一个 RTSP 会话 = 本相机单会话 ⇒ 新连接哑掉）。
            self.join_reader(thread)
        self._thread = None
        self.start()

    # -- 状态 -------------------------------------------------------------

    def latest_frame(self):
        with self._lock:
            return self._latest

    #: [ES 模式专用] 取走自上次 drain 后新增的 NAL（发布端每拍调用一次）。
    def drain_nals(self):
        with self._lock:
            out, self._es_pending = self._es_pending, []
            self._es_pending_bytes = 0
            return out

    #: [ES 模式专用] 最近一个完整 GOP（参数集+IDR…，给**新订阅者**自举
    #: 解码用——H.264/H.265 必须从参数集+IDR 起解）。
    def gop_nals(self):
        with self._lock:
            return list(self._es_gop)

    #: SDP 探测到的实际编码（"h264"/"h265"；未探测到给配置期望值）。
    def detected_codec(self):
        with self._lock:
            return self._es_codec if self._es_codec_detected \
                else (self.settings.get("codec")
                      or proto.CAMERA_CODEC_H264)

    def status(self):
        with self._lock:
            st = {
                "enabled": bool(self.settings.get("enabled")),
                "online": bool(self._online),
                # [ES 模式 frames = NAL 计数 2026-11-19 审查实修] ES 直传
                # 不走 _store_frame ⇒ _frames 恒 0 ⇒ 远端设置卡永远显示
                # 「已出 0 帧」（撒谎）。ES 模式给 nalCount（单调、诚实）；
                # 客户端按 actualTransport 区分文案（已收 N NAL）。
                "frames": (int(self._es_nals) if self._es_mode
                           else int(self._frames)),
                "error": str(self._error),
                "width": self._latest.width if self._latest else 0,
                "height": self._latest.height if self._latest else 0,
                "seq": int(self._seq),
                "maxFrameBytes": int(self._max_frame_bytes),
                # [MAC 连接回显 2026-11-20] MAC 模式下最近解析到的 IP
                # （界面状态行显示「MAC → IP」；ip 模式恒空串不显示）。
                "resolvedIp": str(self._resolved_ip),
            }
            if self._es_mode:
                st.update({
                    "transport": proto.CAMERA_TRANSPORT_STREAM,
                    "codec": (self._es_codec if self._es_codec_detected
                              else (self.settings.get("codec")
                                    or proto.CAMERA_CODEC_H264)),
                    "nalCount": int(self._es_nals),
                    "nalSeq": int(self._es_seq),
                    "gopBytes": int(self._es_gop_bytes),
                    "pendingBytes": int(self._es_pending_bytes),
                    "dropped": int(self._es_dropped),
                })
            else:
                st["transport"] = proto.CAMERA_TRANSPORT_JPEG
            return st

    # -- 工作线程 ---------------------------------------------------------

    def _build_cmd(self):
        """构造解码进程的命令（**接口固定**：stdin 收 H.264 ES，stdout 出 JPEG 流）。

        [为什么要有两个后端]
          · gst-mpp —— 板端（RK3568）：Rockchip **硬件解码**（mppvideodec）
            + 软件 JPEG 编码（jpegenc）。实测 704²@2fps **47% 单核**
            （2026-11-20 抽帧前置后；旧顺序不做前置时 295%）。
          · ffmpeg —— 通用兜底：纯软解软编。板端**没装** ffmpeg（libavcodec-dev
            在、CLI 装不上），本机/其它环境通常也没 gst ⇒ 没有它就没法采集。
        [怎么选] 先看环境变量 HSF_CAMERA_BACKEND（gst/ffmpeg）⇒ 再按
        「PATH 里有哪个」自动挑。⇒ 同一份代码在板端与本机都能跑。
        """
        width = int(self.settings.get("width") or proto.CAMERA_WIDTH_DEF)
        fps = int(self.settings.get("fps") or proto.CAMERA_FPS_DEF)
        quality = int(self.settings.get("quality") or proto.CAMERA_QUALITY_DEF)
        backend = (os.environ.get("HSF_CAMERA_BACKEND") or "").strip().lower()
        if backend not in ("gst", "ffmpeg"):
            backend = "gst" if shutil.which("gst-launch-1.0") else "ffmpeg"
        if backend == "gst":
            # ----------------------------------------------------------------
            # [板端管线定稿 2026-11-20：软件编码 + **抽帧前置**]
            #
            # 走过的两条弯路（都实测过）：
            # ① 旧序 `videoconvert ! videorate ! videoscale ! jpegenc`：
            #    CSC 对**解码器 buffer 上的全帧率**做（25fps×147ms）= 295%
            #    单核，板端整机被它吃满。
            # ② 全硬件 `mppvideodec ! mppjpegenc`：CPU 只有 6%，但**画面
            #    偏紫/棕、体积 12KB/帧（软件 107KB）**、q-factor 10~96 只
            #    让体积 11→14KB —— 色度平面被错误解释（NV12 交错当平面），
            #    且 BT.709 流被按 JFIF(BT.601) 显示。**画质不可用**。
            #
            # 定稿方案：**先抽帧、再软转、最后软编**。
            #   mppvideodec(VPU 解码，1.1ms/帧) ! videorate(只按时戳丢帧)
            #   ! videoconvert → I420(只对 2~5fps 做，147ms/帧)
            #   ! jpegenc(6.6ms/帧)
            # 实测 2fps 档：CSC 成本从 295% 降到 ~30% 单核，**颜色正确**。
            #
            # [三条硬约束（实测踩过）]
            # ① **不许给 mppvideodec 加强制格式 capsfilter**（I420/NV21/RGB
            #    实测静默 0 帧）；jpegenc 不吃 NV12，所以 I420 必须由
            #    videoconvert 产出（放在抽帧之后）。
            # ② fps 越低越省：CSC 成本 ∝ 输出帧率（不再 ∝ 相机帧率）。
            # ③ 不做 videoscale（缩放也在解码器 buffer 上贵）——JPEG 输出
            #    跟随相机子码流分辨率；要更省 CPU 请把**相机**子码流设小。
            return [
                "gst-launch-1.0", "-q",
                "fdsrc", "fd=0",
                "!", "h264parse",
                "!", "mppvideodec",
                "!", "videorate",
                "!", "video/x-raw,framerate=%d/1" % fps,
                "!", "videoconvert", "n-threads=2",
                "!", "video/x-raw,format=I420",
                "!", "jpegenc", "quality=%d" % quality,
                "!", "fdsink", "fd=1",
            ]
        # ffmpeg 兜底：-f h264 从 stdin 读 Annex-B；-vf fps=N,scale=W:W 抽帧
        # 并缩放；-f image2pipe -vcodec mjpeg 出连续 JPEG。
        # [质量换算] ffmpeg 的 mjpeg -q:v 是 1~31 **越小越清晰**（与我们的
        # quality 1~100 越大越清晰**方向相反**）⇒ 做一次反向映射。
        ffmpeg = os.environ.get("HSF_FFMPEG") or shutil.which("ffmpeg") \
            or "ffmpeg"
        q_ff = max(1, min(31, 31 - int(quality * 30 // 100)))
        return [
            ffmpeg, "-hide_banner", "-loglevel", "error",
            "-f", "h264", "-i", "pipe:0",
            "-vf", "fps=%d,scale=%d:%d" % (fps, width, width),
            "-f", "image2pipe", "-vcodec", "mjpeg", "-q:v", str(q_ff),
            "pipe:1",
        ]

    def _run(self):
        """工作线程入口：**任何异常都不许静默死**（2026-11-20 板端实锅）。

        [实锅] 板端出现「进程活着、健康在推、但相机线程不见了、零日志」：
        采集线程在某个 try 之外抛了异常 ⇒ 线程直接消失、`_thread.is_alive()`
        False、谁也不知道（App 侧也没有重启它的逻辑）。⇒ 这里用外层
        try/except BaseException 把死因记下来，并交给 `run_forever()` 的
        看门狗（App 每拍调 `ensure_alive()`）拉起。
        """
        try:
            self._run_forever()
        except BaseException as exc:              # noqa: BLE001
            self._set_state(False, "采集线程异常退出：%s: %s"
                            % (type(exc).__name__, exc))
            self._log("相机采集线程异常退出：%s: %s"
                      % (type(exc).__name__, exc))
            import traceback
            _nl = chr(10)
            self._log("相机线程调用栈：" + _nl
                      + _nl.join(traceback.format_exc().splitlines()[-6:]))

    def alive(self):
        """采集线程是否还活着（App 看门狗用）。"""
        return bool(self._thread and self._thread.is_alive())

    def ensure_alive(self):
        """看门狗：已启用但线程死了 ⇒ 重新拉起（返回是否重启过）。"""
        if not self.settings.get("enabled"):
            return False
        if self.alive():
            return False
        self._log("相机采集线程不在（看门狗）⇒ 重新拉起")
        self.start()
        return True

    def _run_forever(self):
        # [重连间隔 2026-11-20] 基础间隔可配（1~3600s），但**连续失败会
        # 按倍数放大**（×2、×4 … 封顶 8 倍）。
        # [为什么要放大 2026-11-20 板端实锅] 相机（TP-LINK）并发 RTSP 会话
        # 数很少：每 10s 重连一次会**把会话槽堆满** ⇒ 相机接受 PLAY 却不
        # 推流（流给了已死的旧会话）⇒ 越重连越拉不到，连本机也拉不到
        # （实测：停掉所有客户端等 3 分钟，会话回收后单次拉流立刻 317 NAL）。
        # ⇒ 连续失败时拉长间隔给相机时间回收会话；一旦连上就回基础间隔。
        fail_streak = 0
        # [僵尸会话清理 2026-11-20] 上一轮失败留下的相机 session id ——
        # 下一轮连接前用**新连接**发 TEARDOWN 清掉（死 socket 上的
        # TEARDOWN 发不出去 ⇒ 会话泄漏 ⇒ 相机之后只接受不复推流）。
        stale_session = ""
        while not self._stop.is_set():
            cfg = dict(self.settings)
            mac_mode = cfg.get("resolveMode") == proto.CAMERA_RESOLVE_MAC
            if not cfg.get("enabled") or (
                    not mac_mode and not cfg.get("ip")):
                self._set_state(False, "未启用")
                time.sleep(1.0)
                continue
            host = cfg.get("ip")
            if mac_mode:
                mac = str(cfg.get("mac") or "")
                if not mac:
                    self._set_state(False, "MAC 模式但未填相机 MAC"
                                           "（远端设置里补上后重新应用）")
                    if not self._wait_reconnect(cfg):
                        return
                    continue
                resolved = camera_mac.resolve_mac_ip(
                    mac, seed_ip=self._resolved_ip or cfg.get("ip"))
                if not resolved:
                    # [回落配置 IP 2026-11-20 板端实锅] ARP 条目会过期
                    # （默认几十秒~几分钟），而网段探测在**弱 WiFi** 下可能
                    # 一轮扫不到（实测：手动 sweep 2.8s 就成了，但服务端
                    # 在相机重启/会话紧张那一轮扫空）⇒ 若配置里带了 IP，
                    # 就先**用 IP 连**：连成功后内核会把 ARP 建起来，下一轮
                    # MAC 解析自然恢复（IP 变了的那天它仍是兜底失效的——
                    # 那时才该报"未解析"）。
                    fallback_ip = str(cfg.get("ip") or "").strip()
                    if fallback_ip:
                        self._log("MAC %s 未解析到 IP ⇒ 暂用配置 IP %s 连接"
                                  "（连上后 ARP 建立，下轮恢复按 MAC 解析）"
                                  % (mac, fallback_ip))
                        resolved = fallback_ip
                    else:
                        self._set_state(False, "MAC %s 未解析到 IP（本机 ARP"
                                               "与网段探测都未命中，且配置未"
                                               "填 IP 可兜底）" % mac)
                        if not self._wait_reconnect(cfg):
                            return
                        continue
                host = resolved
                prev = self._resolved_ip
                with self._lock:
                    self._resolved_ip = resolved
                # [解析结果留痕 2026-11-20] 首次或**变了**才打（相机换网段
                # /换 IP 时这条日志是现场唯一的证据；每轮都打会刷屏）。
                if resolved != prev:
                    self._log("相机地址解析：MAC %s → %s" % (mac, resolved))
            # [僵尸会话清理 2026-11-20] 上一轮失败留下的 session：连接前用
            # **新连接**发 TEARDOWN 清掉（死 socket 上发不出 TEARDOWN ⇒
            # 相机会话泄漏 ⇒ 之后只接受 SETUP/PLAY 却不推流）。
            if stale_session:
                cleaned = RtspClient.teardown_stale(
                    host, cfg.get("port") or 554,
                    cfg.get("username") or "", cfg.get("password") or "",
                    "stream1" if cfg.get("stream")
                    == proto.CAMERA_STREAM_MAIN else "stream2",
                    stale_session)
                self._log("清理上次残留的相机会话 %s：%s"
                          % (stale_session,
                             "已释放" if cleaned else "相机未确认（尽力而为）"))
                stale_session = ""
            client = None
            try:
                path = "stream1" if cfg.get("stream") == proto.CAMERA_STREAM_MAIN \
                    else "stream2"
                client = RtspClient(host, cfg.get("port") or 554,
                                    cfg.get("username") or "",
                                    cfg.get("password") or "",
                                    path,
                                    timeout=(cfg.get("timeoutMs") or 5000) / 1000.0)
                client.connect()
                if self._es_mode:
                    # [SDP 探实际编码] rtpmap 行带 `H264/90000` 或
                    # `H265/90000` —— 转发本来不关心编码，探测只为
                    # ①客户端选解码器（随分片 flags 位下发）②配置与
                    # 实际不一致时给出明确提示。
                    with self._lock:
                        if "H265" in client.sdp().upper():
                            self._es_codec = proto.CAMERA_CODEC_H265
                            self._es_codec_detected = True
                        elif "H264" in client.sdp().upper():
                            self._es_codec = proto.CAMERA_CODEC_H264
                            self._es_codec_detected = True
                    want = (self.settings.get("codec")
                            or proto.CAMERA_CODEC_H264)
                    got = self.detected_codec()
                    if got != want:
                        self._log("相机子码流实际为 %s（配置期望 %s）："
                                  "已按实际编码转发；要换编码请到相机网页"
                                  "端把子码流改过来" % (got, want))
                self._set_state(True, "")
                fail_streak = 0
                if self._es_mode:
                    self._pump_es(client)
                else:
                    self._pump(client)
            except CameraError as exc:
                self._set_state(False, str(exc))
                self._log("相机采集失败：%s" % exc)
            except Exception as exc:      # noqa: BLE001 —— 采集线程不能因异常退出
                self._set_state(False, "%s: %s" % (type(exc).__name__, exc))
                self._log("相机采集异常：%s" % exc)
            finally:
                if client is not None:
                    sess = str(getattr(client, "_session", "") or "")
                    try:
                        client.close()
                    except Exception:
                        pass
                    # 非人工停止的退出 ⇒ 把 session 记下来，下轮用新连接清
                    if sess and not self._stop.is_set():
                        stale_session = sess
                self._teardown_proc()
            if self._stop.is_set():
                break
            # [断线重连可配 2026-11-20] 开 = 固定间隔重试；关 = 停在错误
            # 上（人工重新「应用」才会再试）。首次连接失败同样受它管
            # （启用了一台够不着的相机 + 重连关 ⇒ 报错停住，不打网）。
            fail_streak += 1
            if not self._wait_reconnect(cfg, fail_streak):
                return

    def _wait_reconnect(self, cfg, fail_streak=1):
        """按重连配置等下一次重试；返回 False = 线程退出（重连关闭）。

        [重连关的语义] 连不上/掉线后**停在错误文案上**（状态栏可读），
        采集线程退出；人工改配置点「应用」（apply_settings→_restart）
        才会再试。开 = 等 `reconnectIntervalS` 秒后继续循环。

        [连续失败按倍数放大 2026-11-20 板端实锅] 相机（TP-LINK）并发 RTSP
        会话数很少：固定 10~15s 重连会**把会话槽堆满** ⇒ 相机接受 PLAY
        却不推流（流给了已死的旧会话）⇒ 越重连越拉不到（实测：停掉全部
        客户端等 3 分钟会话回收，单次拉流立刻 317 NAL / 798KB）。
        ⇒ 失败 streak 越大等越久（base×2^(n-1)，封顶 base×8），给相机
        回收会话的时间；一旦连上 fail_streak 归零、回基础间隔。
        """
        if not cfg.get("reconnectEnabled", proto.CAMERA_RECONNECT_ENABLED_DEF):
            interval = "（自动重连已关闭）"
            self._set_state(False, "已断线%s" % interval)
            self._log("相机断线且自动重连已关闭 ⇒ 停止重试"
                      "（重新应用配置后恢复）")
            return False
        base = max(proto.CAMERA_RECONNECT_INTERVAL_S_MIN,
                   min(proto.CAMERA_RECONNECT_INTERVAL_S_MAX,
                       int(cfg.get("reconnectIntervalS")
                           or proto.CAMERA_RECONNECT_INTERVAL_S_DEF)))
        mult = min(8, 1 << min(3, max(0, int(fail_streak) - 1)))
        interval = base * mult
        if mult > 1:
            self._log("相机连续失败 %d 次 ⇒ 本次等 %ds 再试（会话回收余量）"
                      % (fail_streak, interval))
        self._stop.wait(float(interval))
        return not self._stop.is_set()

    def _pump_es(self, client):
        """ES 直传：RTSP 的每个 Annex-B NAL 经 sink 直接入队，不碰 gst。

        [read_es 的 sink 粒度] 它按 NAL 组装完成后**逐 NAL 调一次 sink**
        （STAP-A 分解/ FU-A 收尾/ 单 NAL 各一次）⇒ sink 正是 NAL 回调。
        [断线与停止] 读写失败由 `_run` 的既有 try/except 收口（退避重连）；
        `_on_nal` 自身绝不抛（采集线程不能因队列维护死掉）。
        """
        self._log("相机 ES 直传：codec=%s（NAL 逐条转发，不解码）"
                  % self.detected_codec())
        client.read_es(self._on_nal,
                       lambda: self._stop.is_set())

    def _on_nal(self, nal):
        """一个完整 Annex-B NAL（含 00000001 起始码）到达。

        [GOP 缓冲] H.264/H.265 必须从参数集+IDR 起解 ⇒ 维护「最近一个
        完整 GOP」给新订阅者自举。规则（缓冲 = `_es_gop`）：
          · **参数集**（H264 SPS/PPS=7/8；H265 VPS/SPS/PPS=32/33/34）：
            缓冲里**已有 VCL**（上一 GOP 在进行）或尚未就绪 ⇒ 清空重开；
            否则（缓冲里只有参数集，如 SPS 后的 PPS）⇒ 直接追加——
            **参数集是两条独立 NAL，不能互删**。
          · **IDR**（H264=5；H265=16..21）：缓冲里已有 IDR ⇒ 清空重开
            （没有参数集的流按 IDR 重开）；追加。
          · 其余 VCL：就绪即追加。
          · 中途接入（首个 NAL 不是参数集）⇒ 不入缓冲等下一个参数集
            （`_es_gop_ready`）——只有从参数集起的缓冲才能自举。
        [待发队列] 订阅门控期间照常积攒（有人看时要「GOP+增量」全量），
        超上限整队作废（缺中间 NAL 的流没有解码意义）。
        """
        try:
            with self._lock:
                self._es_nals += 1
                self._es_seq = (self._es_seq + 1) & 0xFFFFFFFF
                h265 = self._es_codec == proto.CAMERA_CODEC_H265
                t = _nal_type(nal, h265)
                if _nal_is_param_set(t, h265):
                    if self._es_gop_has_vcl(h265) or not self._es_gop_ready:
                        self._es_gop = []
                        self._es_gop_bytes = 0
                    self._es_gop_ready = True
                elif _nal_is_idr(t, h265):
                    if self._es_gop_has_idr(h265):
                        self._es_gop = []
                        self._es_gop_bytes = 0
                if self._es_gop_ready:
                    self._es_gop.append(nal)
                    self._es_gop_bytes += len(nal)
                    if self._es_gop_bytes > self.ES_GOP_MAX_BYTES:
                        # GOP 超限（参数集只出现一次的流会让缓冲一直涨到
                        # 这里；真实相机每 GOP 重发参数集 ⇒ 每 2s 左右就
                        # 被参数集清空，到不了这）。超限 ⇒ 清空**保持就绪**
                        # （ready=False 会让"无重发参数集"的流永久失活；
                        # 后续 IDR 起重新缓冲，自举概率仍优于死缓冲）。
                        self._es_gop = []
                        self._es_gop_bytes = 0
                        self._es_dropped += 1
                self._es_pending.append(nal)
                self._es_pending_bytes += len(nal)
                if self._es_pending_bytes > self.ES_PENDING_MAX_BYTES:
                    self._es_pending = []
                    self._es_pending_bytes = 0
                    self._es_dropped += 1
                self._online = True
        except Exception as exc:                      # noqa: BLE001
            try:
                self._log("ES NAL 处理异常：%s: %s"
                          % (type(exc).__name__, exc))
            except Exception:                         # noqa: BLE001
                pass

    def _es_gop_has_vcl(self, h265):
        return any(_nal_is_vcl(_nal_type(n, h265), h265)
                   for n in self._es_gop)

    def _es_gop_has_idr(self, h265):
        return any(_nal_is_idr(_nal_type(n, h265), h265)
                   for n in self._es_gop)

    def _pump(self, client):
        """把 RTSP 的 ES 喂进解码子进程，同时另起线程读回 JPEG 帧。"""
        cmd = self._build_cmd()
        try:
            proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
        except OSError as exc:
            raise CameraError("无法启动 gstreamer 采集进程：%s" % exc)
        self._proc = proc
        reader = threading.Thread(target=self._read_frames, args=(proc,),
                                  name="camera-frames")
        reader.daemon = True
        reader.start()

        # [Broken pipe 要自己吞] 停止流程会先关 gst 的 stdin ⇒ ES 写入立刻
        # 抛 BrokenPipeError；若当成采集失败上报，会在**正常停止**时刷一条
        # 假错误并触发一次无谓的退避重连。⇒ 写失败只置 dead 标志，由
        # stop_flag 收口退出（真失败仍由外层的重连循环兜住）。
        dead = []

        def sink(data):
            try:
                proc.stdin.write(data)
                # [必须 flush] gst 侧要尽快拿到数据；不 flush 会攒在
                # Python 的缓冲里（低码率时延迟可达秒级）。
                proc.stdin.flush()
            except (BrokenPipeError, OSError):
                dead.append(True)

        try:
            while not self._stop.is_set() and not dead:
                client.read_es(sink, lambda: self._stop.is_set() or bool(dead))
        finally:
            try:
                proc.stdin.close()
            except Exception:
                pass
            try:
                proc.terminate()
                proc.wait(timeout=2.0)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass
            self._proc = None

    def _read_frames(self, proc):
        """读 gst stdout 的连续 JPEG 流 → 切帧 → 存最新一帧。

        [必须用 read1 —— 2026-11-19「连续十几帧后卡顿再恢复」根修]
        `BufferedReader.read(n)` 的语义是**凑满 n 字节或读到 EOF 才返回**
        （它会循环底层 read 直到满足），而 `read1(n)` 是「有多少给多少」。
        本管道每帧只有 47~91KB、按配置 fps 慢速输出 ⇒ 单帧常常**不够
        64KB** ⇒ 用 `read(65536)` 会**等下一帧来凑够**才返回：
            · 交付被成批化（一次交 64KB ⇒ 里面跨两帧）；
            · 帧间隔抖动成 330/566/460/674/925ms 这种跳变，
              现场观感就是「连续十几帧后明显卡一阵再恢复」。
        实测对照（同样 0.5s/帧、47KB/帧）：
            read(65536)  ⇒ 0.52 / 1.02 / **2.02** / 2.52 / 3.02 / **4.02**
            read1(65536) ⇒ 0.02 / 0.52 / 1.02 / 1.52 / 2.02 / 2.52 / 3.02
        ⇒ 用 `read1`：一帧到就立刻切出来交付，节拍跟采集器一致。
        （`read1` 是 BufferedReader 的标准方法；万一拿到的是裸
        FileIO 之类没有它，退回 `read` 保证不崩——只是节拍回到旧行为。）
        """
        reader = getattr(proc.stdout, "read1", None)
        if reader is None:
            def reader(n):
                return proc.stdout.read(n)
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = reader(65536)
            except Exception:
                break
            if not chunk:
                break
            buf += chunk
            # 只在能切出完整帧时才消费，避免半帧残留
            while True:
                start = buf.find(_JPEG_SOI)
                if start < 0:
                    buf = b""
                    break
                end = buf.find(_JPEG_EOI, start + 2)
                if end < 0:
                    buf = buf[start:]
                    break
                frame = buf[start:end + 2]
                buf = buf[end + 2:]
                self._store_frame(frame)

    def _store_frame(self, data):
        width, height = jpeg_size(data)
        with self._lock:
            self._seq += 1
            self._frames += 1
            if len(data) > self._max_frame_bytes:
                # [峰值帧大小] 首帧（IDR）往往比稳态大一个量级（实测 704²
                # 稳态 ~12KB、首帧 ~81KB）⇒ **分片必须按峰值设计**，按均值
                # 设计会在首帧超 XLink 的 16KB 单帧上限。
                self._max_frame_bytes = len(data)
            self._latest = CameraFrame(data, width, height,
                                       int(time.time() * 1000), self._seq)
            self._online = True

    def _teardown_proc(self):
        proc = self._proc
        if proc is None:
            return
        self._proc = None
        try:
            proc.terminate()
            proc.wait(timeout=2.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    def _set_state(self, online, error):
        with self._lock:
            self._online = bool(online)
            if error:
                self._error = str(error)
            elif online:
                self._error = ""
