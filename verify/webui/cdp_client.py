# -*- coding: utf-8 -*-
"""极简 Chrome DevTools Protocol 客户端（纯标准库，够用即可）。

[为什么需要] verify/webui/web_sweep_edge.py 的「同一会话逐页翻页」口径
需要：在一个**浏览器实例、一个文档**里换 hash 翻页并逐页截图。Edge 的
``--screenshot`` 命令行开关做不到（每发一个进程 = 一个新文档 = 状态栏
消息被重置），只有 DevTools 协议能「导航 + 截图」分开做。

[实现范围] 只做本项目要用的三件事：
    · Page.captureScreenshot  截图（PNG base64）
    · Runtime.evaluate       换 hash / 读 DOM 就绪标志
    · Page.navigate          （备用）整页导航
WebSocket 只实现客户端最小集：文本帧 + 掩码 + 分片忽略 + ping/pong。
不用第三方库（verify 目录要求纯标准库）。

[事件收集 2026-11-20 新增，向后兼容]
原先 `call()` 收到「非本次应答 id 的消息」一律丢弃 ⇒ 控制台异常、
JS 未捕获异常（Runtime.exceptionThrown）、浏览器日志（Log.entryAdded）
根本收不到。现在这些**事件帧**会被收进 `Cdp.events` 缓冲
（`drain_events()` 取走并清空 / `poll_events()` 只看不拿）。
**老调用方不受影响**：事件只是「顺手收着」，行为与以前完全一致；
`call()`/`evaluate()`/`screenshot_png()` 的签名与语义都没动。
[收事件的时机] 事件是异步推来的，只有在**下一次 call() 等待应答**时
才会被读出来 ⇒ 想拿准事件，先做一次廉价调用（如 Runtime.evaluate）
把 socket 缓冲里的事件冲出来，再 drain_events()。
"""

import base64
import json
import os
import socket
import struct
import time
import urllib.request


class CdpError(RuntimeError):
    """CDP 调用失败（协议错误或超时）。"""


class _WebSocket(object):
    """最小 WebSocket 客户端（只够跟 DevTools 说话）。"""

    def __init__(self, url, timeout=30.0):
        if not url.startswith("ws://"):
            raise CdpError("只支持 ws://（收到 %s）" % url)
        rest = url[len("ws://"):]
        hostport, _, path = rest.partition("/")
        host, _, port = hostport.partition(":")
        self.sock = socket.create_connection((host, int(port or 80)),
                                             timeout=timeout)
        self.sock.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        req = ("GET /%s HTTP/1.1\r\n"
               "Host: %s\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % (path, hostport, key))
        self.sock.sendall(req.encode("ascii"))
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise CdpError("WebSocket 握手失败（连接被关闭）")
            buf += chunk
        header, _, rest = buf.partition(b"\r\n\r\n")
        if b" 101 " not in header.split(b"\r\n")[0]:
            raise CdpError("WebSocket 握手被拒：%r" % header.split(b"\r\n")[0])
        self._buf = rest

    # ---- 低层 ----
    def _recv_exact(self, n):
        while len(self._buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise CdpError("WebSocket 连接中断（要 %d 字节）" % n)
            self._buf += chunk
        out, self._buf = self._buf[:n], self._buf[n:]
        return out

    def _send_frame(self, opcode, payload):
        header = bytes([0x80 | opcode])
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            header += bytes([0x80 | n])
        elif n < (1 << 16):
            header += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            header += bytes([0x80 | 127]) + struct.pack(">Q", n)
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + masked)

    def _recv_frame(self):
        while True:
            b0, b1 = self._recv_exact(2)
            fin = b0 & 0x80
            opcode = b0 & 0x0F
            masked = b1 & 0x80
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._recv_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._recv_exact(8))[0]
            mask = self._recv_exact(4) if masked else None
            payload = self._recv_exact(length) if length else b""
            if mask:
                payload = bytes(b ^ mask[i % 4]
                                for i, b in enumerate(payload))
            if opcode == 0x8:
                raise CdpError("WebSocket 被对端关闭")
            if opcode == 0x9:                      # ping → 回 pong
                self._send_frame(0xA, payload)
                continue
            if not fin:                           # 分片：DevTools 不会用，忽略
                continue
            return opcode, payload

    # ---- 文本帧 ----
    def send_text(self, text):
        self._send_frame(0x1, text.encode("utf-8"))

    def recv_text(self):
        while True:
            opcode, payload = self._recv_frame()
            if opcode == 0x1:
                return payload.decode("utf-8", "replace")

    def close(self):
        try:
            self._send_frame(0x8, b"")
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


class Cdp(object):
    """CDP 会话（一条 WebSocket 上按 id 收发）。"""

    #: 事件缓冲上限（超出丢最旧的）——防止长跑页面把内存吃满。
    EVENT_CAP = 5000

    def __init__(self, ws_url, timeout=30.0):
        self.ws = _WebSocket(ws_url, timeout=timeout)
        self._id = 0
        #: 已收到的事件帧（2026-11-20 新增；老调用方不用它）。
        self.events = []
        #: 累计收到的事件数（含已被上限丢掉的）。
        self.event_count = 0

    def _push_event(self, msg):
        """收一个事件帧进缓冲（满了丢最旧的）。"""
        self.event_count += 1
        self.events.append({"method": msg.get("method"),
                            "params": msg.get("params") or {}})
        if len(self.events) > self.EVENT_CAP:
            del self.events[0:len(self.events) - self.EVENT_CAP]

    def poll_events(self, methods=None):
        """看已收事件（**不清空**）。methods 给了就只挑这些方法名。"""
        if not methods:
            return list(self.events)
        wanted = set(methods)
        return [e for e in self.events if e["method"] in wanted]

    def drain_events(self, methods=None):
        """取走已收事件并清空（methods 给了就只取这些方法名）。"""
        if not self.events:
            return []
        if not methods:
            out = self.events
            self.events = []
            return out
        wanted = set(methods)
        out = [e for e in self.events if e["method"] in wanted]
        self.events = [e for e in self.events if e["method"] not in wanted]
        return out

    def call(self, method, params=None, timeout=30.0):
        """发一条命令并等它的应答（期间的事件消息收进 events 缓冲）。"""
        self._id += 1
        mid = self._id
        self.ws.send_text(json.dumps({"id": mid, "method": method,
                                      "params": params or {}}))
        deadline = time.time() + timeout
        while time.time() < deadline:
            msg = json.loads(self.ws.recv_text())
            if msg.get("id") != mid:
                if "method" in msg:                  # 事件帧：收下（老行为是丢弃）
                    self._push_event(msg)
                continue                             # 其它应答/事件
            if "error" in msg:
                raise CdpError("%s 失败：%s" % (method, msg["error"]))
            return msg.get("result", {})
        raise CdpError("%s 超时（%ds）" % (method, timeout))

    def evaluate(self, expression, timeout=30.0):
        """求值并返回 JSON 值（异常时返回 None）。"""
        res = self.call("Runtime.evaluate",
                        {"expression": expression, "returnByValue": True,
                         "awaitPromise": True},
                        timeout=timeout)
        result = res.get("result", {})
        if result.get("subtype") == "error":
            return None
        return result.get("value")

    def screenshot_png(self, timeout=30.0):
        """整页视口截图（PNG 字节）。"""
        res = self.call("Page.captureScreenshot", {"format": "png"},
                        timeout=timeout)
        data = res.get("data")
        if not data:
            raise CdpError("Page.captureScreenshot 没给数据")
        return base64.b64decode(data)

    def enable_console(self):
        """开 Runtime/Log/Page 三个域（收 JS 异常与控制台输出用）。

        [为什么这三个] Runtime.exceptionThrown = 未捕获异常；
        Runtime.consoleAPICalled(type=error) = console.error；
        Log.entryAdded(level=error) = 浏览器级日志（网络 404 等）。
        """
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.enable")

    def performance_metrics(self):
        """Performance.getMetrics → {指标名: 值}（JSHeapUsedSize 等）。"""
        self.call("Performance.enable")
        res = self.call("Performance.getMetrics")
        out = {}
        for item in (res.get("metrics") or []):
            name = item.get("name")
            if name:
                out[name] = item.get("value")
        return out

    def close(self):
        self.ws.close()


def wait_for_target(port, timeout_s=30.0, want_url=""):
    """等 DevTools 的页面 target 出现，返回其 webSocketDebuggerUrl。"""
    deadline = time.time() + timeout_s
    last = ""
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(
                    "http://127.0.0.1:%d/json/list" % port, timeout=3) as resp:
                targets = json.loads(resp.read().decode("utf-8", "replace"))
            for t in targets:
                if t.get("type") != "page":
                    continue
                if want_url and want_url not in (t.get("url") or ""):
                    continue
                if t.get("webSocketDebuggerUrl"):
                    return t["webSocketDebuggerUrl"]
            last = "无匹配 page target（%d 个 target）" % len(targets)
        except Exception as exc:                    # noqa: BLE001
            last = str(exc)
        time.sleep(0.4)
    raise CdpError("等 DevTools target 超时：%s" % last)
