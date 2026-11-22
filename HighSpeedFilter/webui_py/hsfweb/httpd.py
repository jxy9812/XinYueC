# -*- coding: utf-8 -*-
"""HTTP/1.1 服务：纯标准库 asyncio 实现（不依赖 aiohttp/框架）。

为什么自己写而不用 aiohttp
==========================

板端是 Debian + **没有 pip 环境**（`apt-get` 只能装系统包，python3-aiohttp
在新版 Debian 里存在、在 11 上版本很旧；而 client_py 已经证明"零第三方依赖"
是这套部署的硬约束，见 client_py/bson.py 的模块注释）。
本文件只用 `asyncio.start_server` + 手写请求行/头解析，功能面刻意收窄：

    支持：GET / POST / OPTIONS，HTTP/1.1，Content-Length 请求体，
          keep-alive（HTTP/1.1 缺省），SSE（分块/长连接）
    不支持：TLS、HTTP/2、multipart、chunked **请求**体、WebSocket

这些不支持项在本项目都用不到（控制命令都是几百字节的 JSON，前端不用
文件上传）。真需要时再上反代（nginx）——那也是现场既有做法。

[安全边界]
  · 只接受 GET/POST/OPTIONS，其余一律 405
  · 请求体上限 `MAX_BODY`（防止被一个大 POST 撑死）
  · 路径做白名单（`api.py` 里的路由表），不做静态文件目录穿越
  · 静态文件只从 `static/` 目录取，且拼接后**校验仍在目录内**
    （`..` 穿越是这类自写服务器的经典洞）

[为什么 SSE 不用 chunked]
  SSE 是 `Content-Type: text/event-stream` + 无 Content-Length 的长连接，
  按 HTTP/1.1 语义（无长度、无 chunked）在 `Connection: close` 下就是
  "读到连接关闭为止"。但我们要求 keep-alive 语义下客户端能边读边渲染，
  故显式用 `Transfer-Encoding: chunked` 写分块 —— 浏览器 EventSource
  对两种都支持，chunked 更规范（不需要 close 就能流式）。
"""

import asyncio
import gzip
import json
import mimetypes
import os
import socket
import time
import urllib.parse

#: 单个请求体上限（控制命令都是几百字节，1MB 已经非常宽松）。
MAX_BODY = 1024 * 1024
#: 请求头总长上限（防头部洪泛）。
MAX_HEADER = 64 * 1024
#: 读请求头的超时（秒）——防慢速攻击把连接占满。
HEADER_TIMEOUT = 15.0
#: SSE 心跳间隔（秒）。浏览器的 EventSource 在没有数据时不会报错，
#: 但中间代理/路由器会掐掉长时间静默的连接；定期发注释行保活。
SSE_HEARTBEAT_S = 15.0
#: SSE 连接最大空闲时长（秒）——**没有任何真事件写出**超过这个时长就收口。
#:
#: [为什么心跳不算"活动"] 心跳（": ping"）每 15s 写一次，写给一个已经
#: 拔网线的浏览器照样"成功"（数据进内核发送缓冲）⇒ 心跳证明不了对端还
#: 活着。故空闲判据只认**真事件**（遥测/告警/快照）。正常运行时每个
#: 连接每秒都有温湿度/健康事件（hub.GLOBAL_TOPICS）⇒ 90s 空闲必然是
#: 僵尸/半开连接。
SSE_MAX_IDLE_S = 90.0
#: SSE 连接最大寿命（秒；12 小时）——即使一切正常也强制收口。
#:
#: [为什么要有] 长连接活过整个班次是正常的，但连接对象（订阅者/队列/协程）
#: 不该无限期存活；定期收口让浏览器自动重连（前端 sse.js 有退避重连），
#: 顺带刷新状态。
SSE_MAX_LIFE_S = 12 * 3600.0
#: 单次写出（drain）的等待上限（秒）——对端不读时 drain 永远不返回。
#:
#: [为什么必须有闸] 拔网线/半开 TCP 的对端，内核发送缓冲填满后
#: `drain()` 会**一直等** ⇒ SSE 协程永久卡住 ⇒ 订阅者永不回收。
#: 超时即判定对端已死，收口重连。
SSE_DRAIN_TIMEOUT_S = 15.0
#: 对端探活周期（秒）——每 5s 查一次"对端还在不在"。
#:
#: [为什么要探] SSE 只写不读 ⇒ 对端关标签页（FIN）时 asyncio 的
#: transport **不会**关闭（`eof_received()` 返回 keep_open）⇒ 光看
#: `transport.is_closing()` 永远看不到断开。真正的信号在 reader 里
#: （`feed_eof()`）⇒ 由 PeerWatchWriter 读出来。
SSE_PROBE_S = 5.0


class HttpError(Exception):
    """带状态码的业务异常（由路由抛出，本层转成 JSON 错误应答）。"""

    def __init__(self, status, message, extra=None):
        Exception.__init__(self, message)
        self.status = int(status)
        self.message = message
        self.extra = extra or {}


class Request(object):
    """解析后的请求。"""

    __slots__ = ("method", "path", "query", "headers", "body", "client",
                 "version", "raw_path")

    def __init__(self):
        self.method = ""
        self.path = ""
        self.raw_path = ""
        self.query = {}
        self.headers = {}
        self.body = b""
        self.client = ""
        self.version = "HTTP/1.1"

    def header(self, name, default=""):
        return self.headers.get(name.lower(), default)

    def json(self, default=None):
        """把请求体解成 dict（解不开抛 400）。"""
        if not self.body:
            return {} if default is None else default
        try:
            data = json.loads(self.body.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise HttpError(400, "请求体不是合法 JSON：%s" % exc)
        if default is not None and not isinstance(data, dict):
            return default
        return data

    def cookie(self, name, default=""):
        raw = self.header("cookie")
        if not raw:
            return default
        for part in raw.split(";"):
            part = part.strip()
            if "=" not in part:
                continue
            key, value = part.split("=", 1)
            if key.strip() == name:
                return value.strip()
        return default

    def bearer_token(self):
        """从 Authorization 头取 Bearer token（也接受 Cookie 兜底）。"""
        raw = self.header("authorization")
        if raw.lower().startswith("bearer "):
            return raw[7:].strip()
        return self.cookie("hsf_token", "")


class Response(object):
    """路由返回的应答。

    body 三种形态：
      · bytes                —— 普通应答（算 Content-Length）
      · 可等待（协程函数）    —— SSE 长连接（`is_sse=True`，内部自己写块）
      · async 生成器          —— 流式下载（`is_stream=True`，chunked 逐块发）

    :ivar cleanup: 可选的收尾回调（无参）。**由 HTTP 层在连接结束时统一
        调用**，用于"应答没能正常跑完"时也要收口资源（如 SSE 订阅者）。
        [为什么需要它] SSE 首帧写失败时，body 里的协程**从未被 await** ⇒
        它的 finally 不会执行 ⇒ 订阅者永久残留（还要打一条
        "coroutine was never awaited"）。⇒ 清理责任上提到 HTTP 层。
    """

    __slots__ = ("status", "body", "content_type", "headers", "is_sse",
                 "is_stream", "cleanup")

    def __init__(self, status=200, body=b"", content_type="application/json",
                 headers=None, is_sse=False, is_stream=False, cleanup=None):
        self.status = int(status)
        if isinstance(body, bytes):
            self.body = body
        elif is_sse or is_stream:
            # SSE（协程函数）/流式（async 生成器）：原样持有，不 stringify。
            self.body = body
        else:
            self.body = str(body).encode("utf-8")
        self.content_type = content_type
        self.headers = headers or {}
        self.is_sse = is_sse
        self.is_stream = is_stream
        self.cleanup = cleanup


def stream_response(gen, content_type, headers=None, status=200):
    """流式应答（导出下载用）：gen 逐块产出 bytes，按 chunked 发送。"""
    return Response(status, gen, content_type, headers, is_stream=True)


def json_response(data, status=200, headers=None):
    body = json.dumps(data, ensure_ascii=False,
                      separators=(",", ":")).encode("utf-8")
    return Response(status, body, "application/json; charset=utf-8", headers)


def error_response(status, message, extra=None):
    payload = {"ok": False, "err": message, "status": status}
    if extra:
        payload.update(extra)
    return json_response(payload, status)


def text_response(text, status=200, content_type="text/plain; charset=utf-8"):
    return Response(status, text, content_type)


#: 状态码 → 短语（自己拼应答行要用）。
STATUS_TEXT = {
    200: "OK", 204: "No Content", 301: "Moved Permanently",
    302: "Found", 304: "Not Modified", 400: "Bad Request",
    401: "Unauthorized", 403: "Forbidden", 404: "Not Found",
    405: "Method Not Allowed", 409: "Conflict", 413: "Payload Too Large",
    500: "Internal Server Error", 501: "Not Implemented",
    503: "Service Unavailable", 504: "Gateway Timeout",
}


class HttpServer(object):
    """极简 HTTP 服务。

    :param router: (request) -> Response 的可调用对象（可 await）
    :param static_dir: 静态文件根目录（`static/`）
    """

    def __init__(self, router, static_dir, log, host="0.0.0.0", port=8080):
        self.router = router
        self.static_dir = os.path.abspath(static_dir) if static_dir else ""
        self.log = log
        self.host = host
        self.port = int(port)
        self._server = None
        self._connections = 0
        self.requests_total = 0
        self.errors_total = 0

    async def start(self):
        self._server = await asyncio.start_server(
            self._handle_client, self.host, self.port)
        # 端口 0 时由系统分配，回读真实端口（测试用）。
        try:
            sock = self._server.sockets[0]
            self.port = sock.getsockname()[1]
        except (IndexError, OSError):
            pass
        return self.port

    async def close(self):
        if self._server is not None:
            self._server.close()
            try:
                await self._server.wait_closed()
            except Exception:                               # noqa: BLE001
                pass
            self._server = None

    # ==================================================================
    #                          连接处理
    # ==================================================================
    async def _handle_client(self, reader, writer):
        self._connections += 1
        peer = writer.get_extra_info("peername")
        client = "%s:%s" % (peer[0], peer[1]) if peer else "?"
        response = None
        try:
            while True:
                request = await self._read_request(reader, client)
                if request is None:
                    break
                self.requests_total += 1
                response = await self._dispatch(request)
                keep = self._should_keep_alive(request)
                if response.is_sse:
                    # SSE 是长连接，自己管到结束（内部含 keep-alive 语义）。
                    await self._serve_sse(request, response, writer, reader)
                    try:
                        writer.close()
                        await writer.wait_closed()
                    except (ConnectionError, OSError):
                        pass
                    break
                if response.is_stream:
                    # 流式下载同样自己管到结束（chunked 无长度语义）。
                    await self._serve_stream(request, response, writer)
                    try:
                        writer.close()
                        await writer.wait_closed()
                    except (ConnectionError, OSError):
                        pass
                    break
                await self._write_response(writer, request, response, keep)
                if not keep:
                    break
        except asyncio.IncompleteReadError:
            pass
        except (ConnectionError, OSError):
            pass
        except asyncio.CancelledError:
            raise
        except Exception as exc:                            # noqa: BLE001
            self.errors_total += 1
            self.log.warn("HTTP 连接异常（%s）：%s: %s"
                          % (client, type(exc).__name__, exc))
        finally:
            # [应答级收口] SSE 首帧写失败时 body 里的协程**从未被 await**
            # ⇒ 它的 finally 不执行 ⇒ 订阅者永久残留。⇒ 清理统一提到
            # HTTP 层（按应答的 cleanup 回调），并把没跑起来的协程
            # close() 掉（免得 GC 时打 "coroutine was never awaited"）。
            self._cleanup_response(response)
            self._connections -= 1
            try:
                writer.close()
                await writer.wait_closed()
            except (ConnectionError, OSError, RuntimeError):
                pass

    def _cleanup_response(self, response):
        """应答收口（幂等）：跑 cleanup 回调 + 关掉未启动的 body 协程。"""
        if response is None:
            return
        cleanup = getattr(response, "cleanup", None)
        if cleanup is not None:
            try:
                cleanup()
            except Exception:                               # noqa: BLE001
                pass
        body = getattr(response, "body", None)
        if asyncio.iscoroutine(body):
            try:
                body.close()
            except Exception:                               # noqa: BLE001
                pass

    async def _dispatch(self, request):
        """路由分派（异常统一转成 JSON 错误）。"""
        try:
            result = self.router(request)
            if asyncio.iscoroutine(result):
                result = await result
            if isinstance(result, Response):
                return result
            # 路由不小心返回了 dict/None：兜底成 JSON。
            if result is None:
                return json_response({"ok": True})
            return json_response(result)
        except HttpError as exc:
            self.errors_total += 1
            return error_response(exc.status, exc.message, exc.extra)
        except asyncio.TimeoutError:
            self.errors_total += 1
            return error_response(504, "上游服务端应答超时")
        except Exception as exc:                            # noqa: BLE001
            self.errors_total += 1
            self.log.error("路由异常 %s %s：%s: %s"
                           % (request.method, request.path,
                              type(exc).__name__, exc))
            return error_response(500, "服务器内部错误：%s" % type(exc).__name__)

    # ==================================================================
    #                          读请求
    # ==================================================================
    async def _read_request(self, reader, client):
        try:
            head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"),
                                          HEADER_TIMEOUT)
        except asyncio.LimitOverrunError:
            raise
        except (asyncio.IncompleteReadError, ConnectionError, OSError):
            return None
        except asyncio.TimeoutError:
            self.log.debug("读请求头超时（%s）" % client)
            return None
        if len(head) > MAX_HEADER:
            return None
        request = self._parse_head(head, client)
        if request is None:
            return None
        # ---- 请求体 ----
        try:
            length = int(request.header("content-length") or 0)
        except ValueError:
            length = 0
        if length < 0:
            length = 0
        if length > MAX_BODY:
            # 不读完（会撑爆内存）：直接回 413 并关连接。
            raise HttpError(413, "请求体过大（上限 %d 字节）" % MAX_BODY)
        if length:
            try:
                request.body = await asyncio.wait_for(
                    reader.readexactly(length), HEADER_TIMEOUT)
            except asyncio.TimeoutError:
                return None
            except (asyncio.IncompleteReadError, ConnectionError, OSError):
                return None
        return request

    def _parse_head(self, head, client):
        try:
            text = head.decode("iso-8859-1")
        except UnicodeDecodeError:
            return None
        lines = text.split("\r\n")
        if not lines:
            return None
        parts = lines[0].split(" ")
        if len(parts) < 3:
            return None
        request = Request()
        request.method = parts[0].upper()
        request.raw_path = parts[1]
        request.version = parts[2]
        request.client = client
        # ---- 路径与查询串 ----
        raw = parts[1]
        if "?" in raw:
            path, query = raw.split("?", 1)
        else:
            path, query = raw, ""
        # 百分号解码（中文路径/参数）；path 只解一次，避免二次编码绕过。
        try:
            request.path = urllib.parse.unquote(path)
        except Exception:                                   # noqa: BLE001
            request.path = path
        try:
            request.query = {k: v[0] for k, v in
                             urllib.parse.parse_qs(query).items()}
        except Exception:                                   # noqa: BLE001
            request.query = {}
        # ---- 头 ----
        for line in lines[1:]:
            if not line:
                continue
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            request.headers[key.strip().lower()] = value.strip()
        return request

    def _should_keep_alive(self, request):
        conn = request.header("connection").lower()
        if request.version == "HTTP/1.0":
            return conn == "keep-alive"
        return conn != "close"

    # ==================================================================
    #                          写应答
    # ==================================================================
    def _build_head(self, request, response, keep, extra=None,
                    no_length=False):
        """拼应答头。

        :param no_length: True 时不写 Content-Length（SSE/chunked 用）
        """
        status = response.status
        phrase = STATUS_TEXT.get(status, "OK")
        headers = [
            "%s %d %s" % (request.version or "HTTP/1.1", status, phrase),
            "Server: hsf-webui",
            "Date: %s" % _http_date(),
        ]
        ctype = response.content_type or "application/octet-stream"
        if ctype:
            headers.append("Content-Type: %s" % ctype)
        if not no_length and response.status not in (204, 304):
            # [304 不带 Content-Length] RFC 7232：304 的响应体恒为空，
            # 且**不得**带 Content-Length（若带，其值指「选中表示的长度」
            # 而不是本响应体的长度 ⇒ 浏览器按本响应体长度解析会出错）。
            # 条件请求（ETag/If-None-Match）命中时走这条路。
            headers.append("Content-Length: %d" % len(response.body))
        headers.append("Connection: %s" % ("keep-alive" if keep else "close"))
        # [安全头] 这是内网控制台：禁止被嵌进别的页面（防点击劫持），
        # 并禁止浏览器猜 MIME（防把 txt 当 html 执行）。
        headers.append("X-Content-Type-Options: nosniff")
        headers.append("X-Frame-Options: SAMEORIGIN")
        headers.append("Referrer-Policy: no-referrer")
        for key, value in response.headers.items():
            headers.append("%s: %s" % (key, value))
        if extra:
            headers.extend(extra)
        return ("\r\n".join(headers) + "\r\n\r\n").encode("iso-8859-1")

    async def _write_response(self, writer, request, response, keep):
        if request.method == "HEAD":
            body = b""
        else:
            body = response.body
        head = self._build_head(request, response, keep)
        writer.write(head)
        if body:
            writer.write(body)
        await writer.drain()

    # ==================================================================
    #                          SSE
    # ==================================================================
    async def _serve_sse(self, request, response, writer, reader=None):
        """长连接推送（调用方负责关闭 writer）。

        `response.body` 约定为**可等待对象**：它负责把 SSE 数据写出去。
        由路由层实现具体格式（见 api.py 的 _sse_stream），本层只做分块头。

        :param reader: 同一连接的读端——SSE 只写不读，对端断开（FIN/RST）
            的信号**只**出现在 reader 里（transport 因 `eof_received()`
            返回 keep_open 而不会关闭）⇒ 用它做探活（见 PeerWatchWriter）。
        """
        head = self._build_head(request, response, keep=True, no_length=True,
                                extra=[
                                    "Cache-Control: no-cache, no-transform",
                                    "X-Accel-Buffering: no",   # 反代不缓冲
                                    "Transfer-Encoding: chunked",
                                ])
        writer.write(head)
        try:
            # [首帧也要有闸] 对端已经没了时 drain() 可能永远不返回 ⇒
            # 连接永远收不掉。超时即放弃（订阅者的清理由 HTTP 层兜底）。
            await asyncio.wait_for(writer.drain(), SSE_DRAIN_TIMEOUT_S)
        except (asyncio.TimeoutError, ConnectionError, OSError, RuntimeError):
            return
        # [探活代理] 把 reader 的 EOF/异常接进写路径（对端一断，下一次写
        #  就抛错 ⇒ SSE 协程收口 ⇒ 订阅者被回收）。
        await response.body(PeerWatchWriter(writer, reader))

    async def _serve_stream(self, request, response, writer):
        """流式下载（chunked）：`response.body` 是 async 生成器，逐块产出 bytes。

        [为什么导出走流式] 历史导出是"筛选命中的全部行"，CSV 可能有十几万
        行——先攒成一个 bytes 会把内存吃爆；生成器边取（上游 485 分页）边发，
        内存占用恒为一小块。
        """
        head = self._build_head(request, response, keep=True, no_length=True,
                                extra=[
                                    "Cache-Control: no-store",
                                    "X-Accel-Buffering: no",
                                    "Transfer-Encoding: chunked",
                                ])
        writer.write(head)
        await writer.drain()
        try:
            async for data in response.body:
                if data:
                    writer.write(chunk(data))
                    await writer.drain()
        except (ConnectionError, OSError, RuntimeError):
            return
        finally:
            # chunked 终止块（长度 0）——不发浏览器会一直等。
            try:
                writer.write(b"0\r\n\r\n")
                await writer.drain()
            except (ConnectionError, OSError, RuntimeError):
                pass


def chunk(data):
    """把一个 SSE 数据块包成 HTTP chunked 帧。"""
    if not data:
        return b""
    payload = data.encode("utf-8") if isinstance(data, str) else bytes(data)
    return ("%X\r\n" % len(payload)).encode("ascii") + payload + b"\r\n"


def sse_event(event, data, event_id=None):
    """构造一条 SSE 消息（data 会被 JSON 序列化）。

    [为什么所有数据都走 JSON] SSE 的 `data:` 只能是文本行，且换行要拆多行。
    统一 JSON + 单行（`separators` 去掉多余空格、`ensure_ascii=False`
    保留中文）后不必处理多行拼接，前端 `JSON.parse` 一步到位。
    """
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    lines = []
    if event_id is not None:
        lines.append("id: %s" % event_id)
    if event:
        lines.append("event: %s" % event)
    lines.append("data: %s" % payload)
    lines.append("")
    lines.append("")
    return "\n".join(lines)


class PeerWatchWriter(object):
    """SSE 写的代理：把「对端已经断开」变成 drain() 的异常。

    [为什么必须有] SSE **只写不读** ⇒ 对端关闭（浏览器关标签页 ⇒ 发
    FIN）时，asyncio 的 transport **不会**关闭（`eof_received()` 返回
    keep_open ⇒ `transport.is_closing()` 永远是 False）⇒ 连接永远收不掉
    ⇒ 僵尸订阅者永远留在订阅表里，它的 topics 还污染并集。
    而 EOF 其实**已经被读到了**（`StreamReader.feed_eof()`）——只是没人
    看。⇒ 这里把 reader 的 EOF/异常状态接进写路径：对端一断，下一次
    写就抛错，SSE 协程随即收口（订阅者被回收）。

    [为什么不直接用 socket peek] Windows 的 Proactor 事件循环里
    `transport.get_extra_info("socket")` 返回的是 `TransportSocket`
    包装（**没有 recv**）⇒ peek 不可用；reader 的 EOF 状态是跨平台的。
    """

    def __init__(self, writer, reader):
        self._writer = writer
        self._reader = reader

    # ---- 探活 ----
    def peer_gone(self):
        """对端是否已断（EOF / 连接异常 / 传输层已关）。"""
        reader = self._reader
        if reader is not None:
            try:
                if reader.at_eof():
                    return True
                exc = reader.exception()
                if exc is not None:
                    return True
            except Exception:                               # noqa: BLE001
                pass
        return peer_gone(self._writer)

    # ---- 写 ----
    def write(self, data):
        self._writer.write(data)

    async def drain(self):
        if self.peer_gone():
            raise ConnectionResetError("SSE 对端已断开")
        await self._writer.drain()

    def __getattr__(self, name):
        return getattr(self._writer, name)


def peer_gone(writer):
    """对端是否已经断开（传输层探活，尽力而为）。

    [为什么还要这一层] `PeerWatchWriter` 看的是 reader 的 EOF/异常；
    这一层看的是传输层自己知不知道（本地已关闭 / 写失败过）。两者互为
    补充：Linux 上还能顺带 peek socket（对端 FIN 后内核缓冲里是 0 字节）。
    """
    transport = None
    try:
        transport = getattr(writer, "transport", None)
        if transport is not None and transport.is_closing():
            return True
    except Exception:                                       # noqa: BLE001
        pass
    try:
        if getattr(writer, "is_closing", None) and writer.is_closing():
            return True
    except Exception:                                       # noqa: BLE001
        pass
    if transport is None:
        return False
    try:
        sock = transport.get_extra_info("socket")
        if sock is None:
            return False
        import select as _select
        readable, _w, _x = _select.select([sock], [], [], 0)
        if not readable:
            return False
        data = sock.recv(1, socket.MSG_PEEK)
        return data == b""                                 # 0 字节 = 对端 FIN
    except Exception:                                       # noqa: BLE001
        # 探活失败**不等于**对端已断（Windows 的 TransportSocket 没有
        # recv / socket 已被换掉等）⇒ 按"还在"。
        return False


def _peer_gone_fast(writer):
    """优先用 writer 自带的探活（能看见 reader 的 EOF），否则用传输层探活。"""
    checker = getattr(writer, "peer_gone", None)
    if callable(checker):
        try:
            return bool(checker())
        except Exception:                                   # noqa: BLE001
            return False
    return peer_gone(writer)


async def sse_producer(writer, queue_getter, log, on_close=None,
                        max_idle_s=SSE_MAX_IDLE_S, max_life_s=SSE_MAX_LIFE_S,
                        alive_check=None):
    """通用的 SSE 生产循环：从队列取事件写出去 + 定期心跳。

    :param queue_getter: 返回当前队列（或 None 表示结束）
    :param on_close: 退出时回调（清理订阅）
    :param max_idle_s: 最大空闲（无**真事件**写出）时长，超时收口
    :param max_life_s: 连接最大寿命，超时收口
    :param alive_check: 可选回调，返回 False 表示"订阅已被淘汰"⇒ 收口

    [为什么要这几道闸] 只写不读的长连接会变成"僵尸"：对端拔网线/关标签
    页后，服务端既收不到 EOF（只写不读）也等不到写失败 ⇒ 订阅者永远
    留在订阅表里，它的 topics 还污染并集（上游继续为「没人看的浏览器」
    推主题）。

      ① 探活（每 5s 一轮）——对端 EOF/断开 ⇒ 立刻收口；
      ② 写出超时（drain 有闸）——对端不读 ⇒ 收口；
      ③ 空闲闸——长时间没有任何真事件（心跳不算）⇒ 收口；
      ④ 寿命闸——12h 强制收口，浏览器自动重连。
    """
    started = time.monotonic()
    last_event = started
    last_ping = started
    try:
        while True:
            now = time.monotonic()
            life_left = max_life_s - (now - started)
            idle_left = max_idle_s - (now - last_event)
            if life_left <= 0:
                log.info("SSE 连接超过最大寿命（%.0f 秒），主动收口"
                         % max_life_s)
                break
            if idle_left <= 0:
                log.info("SSE 连接空闲超过 %.0f 秒（无事件），主动收口"
                         % max_idle_s)
                break
            if alive_check is not None and not alive_check():
                log.info("SSE 订阅已被淘汰，主动收口")
                break
            if _peer_gone_fast(writer):
                log.info("SSE 对端已断开，主动收口")
                break
            queue = queue_getter()
            if queue is None:
                break
            # 等下一个事件；等不到就探活一次（到点收口由下一轮循环头判）。
            wait = min(SSE_PROBE_S, life_left, idle_left)
            try:
                event, data = await asyncio.wait_for(queue.get(), wait)
            except asyncio.TimeoutError:
                if _peer_gone_fast(writer):
                    log.info("SSE 对端已断开，主动收口")
                    break
                now = time.monotonic()
                if now - last_ping >= SSE_HEARTBEAT_S:
                    try:
                        writer.write(chunk(": ping %d\n\n" % int(now)))
                        await asyncio.wait_for(writer.drain(),
                                               SSE_DRAIN_TIMEOUT_S)
                    except (asyncio.TimeoutError, ConnectionError, OSError,
                            RuntimeError):
                        break
                    last_ping = now
                continue
            except asyncio.CancelledError:
                raise
            writer.write(chunk(sse_event(event, data)))
            try:
                await asyncio.wait_for(writer.drain(), SSE_DRAIN_TIMEOUT_S)
            except asyncio.TimeoutError:
                # [对端不读 ⇒ drain 永远不返回] 这是"拔网线"最直接的
                # 症状：写超时即判定对端已死，收口（否则协程与订阅者永久
                # 卡死，僵尸订阅者的 topics 还会污染上游订阅并集）。
                log.info("SSE 写出超时（对端不读），主动收口")
                break
            except (ConnectionError, OSError, RuntimeError):
                break
            last_event = time.monotonic()
    except (ConnectionError, OSError, RuntimeError):
        pass
    except asyncio.CancelledError:
        raise
    except Exception as exc:                                # noqa: BLE001
        log.debug("SSE 生产循环结束：%s: %s" % (type(exc).__name__, exc))
    finally:
        if on_close:
            try:
                on_close()
            except Exception:                               # noqa: BLE001
                pass


# ============================================================================
#                          静态文件
# ============================================================================

#: 扩展名 → MIME（mimetypes 在部分精简系统上缺 .js/.css 映射）。
EXTRA_MIME = {
    ".js": "application/javascript; charset=utf-8",
    ".mjs": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".html": "text/html; charset=utf-8",
    ".htm": "text/html; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".ico": "image/x-icon",
    ".woff2": "font/woff2",
    ".map": "application/json; charset=utf-8",
}


def guess_mime(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in EXTRA_MIME:
        return EXTRA_MIME[ext]
    mime, _enc = mimetypes.guess_type(path)
    return mime or "application/octet-stream"


def read_static(static_dir, rel_path):
    """安全地读一个静态文件，返回 bytes（越界/不存在返回 None）。

    [目录穿越防护] `rel_path` 来自 URL，必须保证拼接后仍落在 static_dir
    内。仅靠"过滤 .."不够（`%2e%2e`、Windows 的 `..\\`、符号链接都可能
    绕过），故用 realpath 比较前缀。
    """
    target = _resolve_static(static_dir, rel_path)
    if target is None:
        return None
    try:
        with open(target, "rb") as fh:
            return fh.read()
    except OSError:
        return None


def _resolve_static(static_dir, rel_path):
    """URL 相对路径 → 盘上绝对路径（越界/不存在返回 None）。"""
    if not static_dir:
        return None
    rel_path = rel_path.lstrip("/\\")
    if not rel_path:
        rel_path = "index.html"
    # [Windows 上反斜杠也是分隔符] 统一成正斜杠再拼，避免绕过。
    rel_path = rel_path.replace("\\", "/")
    root = os.path.realpath(static_dir)
    target = os.path.realpath(os.path.join(root, rel_path))
    if not (target == root or target.startswith(root + os.sep)):
        return None
    if not os.path.isfile(target):
        return None
    return target


# ==================================================================
#                       静态资源缓存 + gzip
# ==================================================================
#: [为什么要有缓存] 静态资源是给**浏览器**取的，响应头带
#: `Cache-Control: no-cache`（改了 JS 必刷新，见 api.py 的说明）⇒
#: 浏览器**每次都重新问** ⇒ 服务端每次都要**重新读盘**。
#: 板端是 flash + 弱 WiFi，一个 111KB 的 JS 每次都真读一遍不划算。
#: ⇒ 按 (路径, mtime, 大小) 缓存文件内容；文件一改，mtime 变 ⇒ 自动失效。
#:
#: [为什么连 gzip 一起做] 网页端是**单页应用**：index.html 里一次引了
#: 33 个 JS/CSS（共 **691.1 KB**），浏览器**首屏全下**。实测板端到现场
#: 是弱 WiFi（-72dBm、3~5% 丢包），这 691.1 KB 未压缩文本走一趟很慢。
#: gzip 后同样的资源实测 **236.2 KB**（约 **1/2.93**）⇒ 首屏字节降到三成
#: 不到。压缩体**只算一次**（按 mtime 缓存），不占每请求的 CPU。
#: [数字订正 2026-11-21] 这里原先写「688.7 KB → 约 130 KB（1/5）」——
#: 那是**根修前**的记录，而当时 gzip **几乎没生效**（MIME 带 charset ⇒
#: 白名单精确匹配失配 ⇒ 32 个 .js 一个没压；实测线上只发出 623.6 KB）。
#: 根修（`_is_gzip_mime` 先剥 charset 参数）后实测 691.1 → 236.2 KB。
#:
#: [不压缩哪些] 图片/字体等已是压缩格式 ⇒ 不动；`index.html` 每次请求
#: 都要注入 `?v=<指纹>`（版本戳）⇒ 内容随指纹变 ⇒ 本实现**不压它**
#: （才 3 KB，压了也不值当）。
_STATIC_CACHE = {}
#: 缓存条目上限（文件数）。静态目录约 37 个文件 ⇒ 64 足够全量常驻；
#: 超了按插入顺序淘汰最旧的（dict 保序）。
_STATIC_CACHE_MAX = 64

#: 会被 gzip 的 MIME 前缀/类型（文本类）。
_GZIP_MIME_PREFIX = ("text/",)
_GZIP_MIME_EXACT = ("application/javascript", "application/json",
                    "application/xml", "image/svg+xml")


def _is_gzip_mime(mime):
    """该 MIME 是否值得 gzip（文本类才值得）。

    [2026-11-21 根修：先剥 charset 参数再判]
        `guess_mime` 返回的是**带参数**的 MIME（如
        `application/javascript; charset=utf-8`、`text/css; charset=utf-8`）
        —— 而白名单里写的是**裸类型** ⇒ 老的 `mime in _GZIP_MIME_EXACT`
        精确匹配对 `application/*` **全部失配** ⇒ **所有 .js 都没被压缩**
        （只有 .css 侥幸命中：它在 `;` 之前就以 `text/` 开头，走的是前缀
        匹配而不是精确匹配）。
        现象（实测线上字节）：首屏 33 个资源共 691.1 KB，压缩后只发出
        **623.6 KB（压到 90%）**——32 个 JS 一个没压，只有 theme.css 压了。
        板端弱 WiFi 上这一趟白走 593 KB。
        ⇒ 判 gzip 前先剥掉 `;` 之后的参数再比（**不能**改白名单去迁就
        参数——参数是 MIME 的合法组成部分，剥掉才是「按类型判」的正解）。
    """
    if not mime:
        return False
    base = mime.split(";", 1)[0].strip().lower()
    if base.startswith(_GZIP_MIME_PREFIX):
        return True
    return base in _GZIP_MIME_EXACT


def read_static_cached(static_dir, rel_path):
    """读静态文件（带缓存）并**顺带备好 gzip 体**。

    :returns: (raw, gz, etag) —— raw 是原始字节；gz 是 gzip 压缩后的字节
              （不可压缩时为 None）；etag 是**内容指纹**（mtime_ns+size 拼成
              的弱 ETag 串，未取到文件时为 None）。
              文件不存在/越界 ⇒ (None, None, None)。

    [缓存键] (绝对路径, mtime_ns, 大小) ⇒ 文件一改（mtime/大小变）自动
    失效，不会拿到旧内容（与 `?v=` 版本戳两套机制互不干扰）。

    [2026-11-21 新增 etag] 配合条件请求（`If-None-Match`）：浏览器每次
    都问（保持「改了 JS 立刻生效」的既有契约），但**没变就回 304 零字节**
    ⇒ 首屏 236 KB 从「每次重下」变成「只有改动的那几个文件重下」。
    指纹用 mtime_ns+size 而不是内容哈希：文件一改必然变，且不用每次
    重算哈希（那才是真开销）。
    """
    target = _resolve_static(static_dir, rel_path)
    if target is None:
        return None, None, None
    try:
        st = os.stat(target)
    except OSError:
        return None, None, None
    key = (target, st.st_mtime_ns, st.st_size)
    hit = _STATIC_CACHE.get(key)
    if hit is not None:
        return hit
    try:
        with open(target, "rb") as fh:
            raw = fh.read()
    except OSError:
        return None, None, None
    gz = None
    mime = guess_mime(target)
    if _is_gzip_mime(mime) and not target.endswith("index.html"):
        try:
            gz = gzip.compress(raw, 6)
        except OSError:
            gz = None
    # [弱 ETag] 用引号包住（W/ 前缀表示弱校验器：mtime_ns 变了就变，
    # 但语义上等价 ⇒ 弱 ETag 合法）。
    etag = 'W/"%x-%x"' % (st.st_mtime_ns, st.st_size)
    if len(_STATIC_CACHE) >= _STATIC_CACHE_MAX:
        # 淘汰最旧的若干条（dict 保序 ⇒ 第一批即最旧）。
        for old in list(_STATIC_CACHE.keys())[:8]:
            _STATIC_CACHE.pop(old, None)
    _STATIC_CACHE[key] = (raw, gz, etag)
    return raw, gz, etag


def _http_date():
    """RFC 7231 日期字符串。"""
    import email.utils
    import time
    return email.utils.formatdate(time.time(), usegmt=True)
