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
import json
import mimetypes
import os
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
    """路由返回的应答。"""

    __slots__ = ("status", "body", "content_type", "headers", "is_sse")

    def __init__(self, status=200, body=b"", content_type="application/json",
                 headers=None, is_sse=False):
        self.status = int(status)
        self.body = body if isinstance(body, bytes) else str(body).encode("utf-8")
        self.content_type = content_type
        self.headers = headers or {}
        self.is_sse = is_sse


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
                    await self._serve_sse(request, response, writer)
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
            self._connections -= 1
            try:
                writer.close()
                await writer.wait_closed()
            except (ConnectionError, OSError, RuntimeError):
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
        if not no_length and response.status != 204:
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
    async def _serve_sse(self, request, response, writer):
        """长连接推送（调用方负责关闭 writer）。

        `response.body` 约定为**可等待对象**：它负责把 SSE 数据写出去。
        由路由层实现具体格式（见 api.py 的 _sse_stream），本层只做分块头。
        """
        head = self._build_head(request, response, keep=True, no_length=True,
                                extra=[
                                    "Cache-Control: no-cache, no-transform",
                                    "X-Accel-Buffering: no",   # 反代不缓冲
                                    "Transfer-Encoding: chunked",
                                ])
        writer.write(head)
        await writer.drain()
        await response.body(writer)


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


async def sse_producer(writer, queue_getter, log, on_close=None):
    """通用的 SSE 生产循环：从队列取事件写出去 + 定期心跳。

    :param queue_getter: 返回当前队列（或 None 表示结束）
    :param on_close: 退出时回调（清理订阅）
    """
    try:
        while True:
            queue = queue_getter()
            if queue is None:
                break
            try:
                event, data = await asyncio.wait_for(queue.get(),
                                                     SSE_HEARTBEAT_S)
            except asyncio.TimeoutError:
                # 心跳注释行（": " 开头），浏览器会忽略但代理不会掐连接。
                writer.write(chunk(": ping %d\n\n" % int(asyncio.get_event_loop().time())))
                await writer.drain()
                continue
            except asyncio.CancelledError:
                raise
            writer.write(chunk(sse_event(event, data)))
            await writer.drain()
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
    try:
        with open(target, "rb") as fh:
            return fh.read()
    except OSError:
        return None


def _http_date():
    """RFC 7231 日期字符串。"""
    import email.utils
    import time
    return email.utils.formatdate(time.time(), usegmt=True)
