# -*- coding: utf-8 -*-
"""REST 路由与 SSE 流：前端与网页服务器之间**唯一**的接口面。

接口一览
========

    POST  /api/login                登录（用户名/口令 → token）
    POST  /api/logout               登出
    GET   /api/me                   当前会话身份

    GET   /api/state                全量快照（首屏/断线恢复用）
    GET   /api/stream               **SSE 实时流**（推送）
    POST  /api/topics               本连接想订阅的主题（切页时调）

    GET   /api/status               诊断（链路/订阅者/历史/告警统计）
    GET   /api/log                  运行日志（?limit=&level=&contains=）

    GET   /api/history              历史采样（?limit=&since=）
    GET   /api/history/files        已落盘的历史文件
    GET   /api/alarms               告警记录（?limit=&level=&kind=）
    GET   /api/alarms/active        当前仍处于异常的条目

    POST  /api/vfd/control          变频器控制（启停/给定/点动…）
    POST  /api/vfd/snapshot         请求变频器快照（会现读设备）
    POST  /api/digital/control      DO 单点/整组控制
    POST  /api/digital/snapshot     请求数字量快照（?dev=）
    POST  /api/valve/control        阀开/关
    POST  /api/valve/snapshot       请求阀快照
    GET   /api/mode                 查询运行模式
    POST  /api/mode                 切换运行模式（需 root）

    POST  /api/jog/start            按住点动开始（网页服务器代持重发）
    POST  /api/jog/stop             松开点动

    GET   /api/pages                页面注册表（导航树/页名，供前端渲染）
    *                               其余路径按静态文件处理

设计约定
========

1) **所有控制命令都过同一道闸门**（`auth.check_control_gate`）：未登录 /
   自动模式 / 非 root 一律 403 并给出原因。服务端自己还有一道闸门，
   网页侧这道是**下限**（少走一次无谓的 485 往返，也给用户更快的反馈）。

2) **错误统一形状**：`{"ok": false, "err": "...", "status": 403}`。
   前端只认 `ok` 与 `err`，不用按状态码分支。

3) **控制应答原样透传服务端的 ok/err**。网页服务器**不**把服务端的
   "命令被拒"改写成成功 —— 现场排查时两边的判断必须一致。

4) `POST /api/topics` 是"本连接想要什么"，不是"现在订阅什么"：
   服务端按所有连接的并集收敛（见 hub.py 的模块文档）。
"""

import asyncio
import json
import urllib.parse

from . import auth as auth_mod
from . import httpd
from . import model as model_mod
from . import proto
from .httpd import HttpError, Response, error_response, json_response

#: 页面注册表（与 client_py/pages_registry.py 逐项一致）。
#:
#: [为什么后端也要有一份] 前端要渲染导航树与页名；放在后端的好处是
#: **改页面顺序只改一处**（与服务端/C 端的页序对齐是验收口径之一）。
#: 前端只是渲染，不做业务判断。
PAGE_HOME = 0
PAGE_MENU = 1
PAGE_MONITOR_MENU = 2
PAGE_MONITOR_DIGITAL = 3
PAGE_MONITOR_ANALOG = 4
PAGE_MONITOR_VFD = 5
PAGE_MONITOR_FLOWMETER = 6
PAGE_MONITOR_VALVE = 7
PAGE_DATA_MENU = 8
PAGE_DATA_HISTORY = 9
PAGE_DATA_LOG = 10
PAGE_DATA_ALARM = 11
PAGE_SETTINGS_MENU = 12
PAGE_SETTINGS_LOCAL = 13
PAGE_SETTINGS_REMOTE = 14
PAGE_SETTINGS_NETWORK = 15
PAGE_SETTINGS_OVERLAY = 16
PAGE_SETTINGS_WINDOW = 17
PAGE_COUNT = 18

PAGE_NAMES = [
    "主界面", "主菜单", "监视", "数字量", "模拟量", "变频器", "电磁流量计",
    "电磁阀", "数据", "历史数据", "日志", "报警", "设置", "本地设置",
    "远端设置", "网络设置", "悬浮窗设置", "窗口设置",
]

#: 导航树 (caption, page_index, child_of)；child_of=-1 为顶层。
NAV_TREE = [
    ("主界面",     PAGE_HOME,               -1),
    ("监视",       PAGE_MONITOR_MENU,       -1),
    ("数字量",     PAGE_MONITOR_DIGITAL,     1),
    ("模拟量",     PAGE_MONITOR_ANALOG,      1),
    ("变频器",     PAGE_MONITOR_VFD,         1),
    ("流量计",     PAGE_MONITOR_FLOWMETER,   1),
    ("电磁阀",     PAGE_MONITOR_VALVE,       1),
    ("数据",       PAGE_DATA_MENU,          -1),
    ("历史数据",   PAGE_DATA_HISTORY,        6),
    ("日志",       PAGE_DATA_LOG,            6),
    ("报警",       PAGE_DATA_ALARM,          6),
    ("设置",       PAGE_SETTINGS_MENU,      -1),
    ("本地设置",   PAGE_SETTINGS_LOCAL,     10),
    ("远端设置",   PAGE_SETTINGS_REMOTE,    10),
    ("网络设置",   PAGE_SETTINGS_NETWORK,   10),
    ("悬浮窗设置", PAGE_SETTINGS_OVERLAY,   10),
    ("窗口设置",   PAGE_SETTINGS_WINDOW,    10),
]

#: 子菜单页的条目（与 client_py/pages_registry.SUBMENU_ENTRIES 同源）。
SUBMENU_ENTRIES = {
    PAGE_MONITOR_MENU: [
        ("数字量", PAGE_MONITOR_DIGITAL),
        ("模拟量", PAGE_MONITOR_ANALOG),
        ("变频器", PAGE_MONITOR_VFD),
        ("流量计", PAGE_MONITOR_FLOWMETER),
        ("电磁阀", PAGE_MONITOR_VALVE),
    ],
    PAGE_DATA_MENU: [
        ("历史数据", PAGE_DATA_HISTORY),
        ("日志", PAGE_DATA_LOG),
        ("报警", PAGE_DATA_ALARM),
    ],
    PAGE_SETTINGS_MENU: [
        ("本地设置", PAGE_SETTINGS_LOCAL),
        ("远端设置", PAGE_SETTINGS_REMOTE),
        ("网络设置", PAGE_SETTINGS_NETWORK),
        ("悬浮窗设置", PAGE_SETTINGS_OVERLAY),
        ("窗口设置", PAGE_SETTINGS_WINDOW),
    ],
}

#: 主菜单三大分类入口。
MAIN_MENU_ENTRIES = [
    ("监视", PAGE_MONITOR_MENU, "▤"),
    ("数据", PAGE_DATA_MENU, "≣"),
    ("设置", PAGE_SETTINGS_MENU, "⚙"),
]

#: 每页需要的推送主题（切页时前端把它 POST 给 /api/topics）。
#:
#: [全局常驻] 温湿度/健康由 hub.GLOBAL_TOPICS 无条件订阅，这里不必列。
PAGE_TOPICS = {
    PAGE_HOME: [proto.TOPIC_VFD_STATUS, proto.TOPIC_AI_STATUS],
    PAGE_MONITOR_DIGITAL: [proto.TOPIC_DIGITAL_STATUS],
    PAGE_MONITOR_ANALOG: [proto.TOPIC_AI_STATUS],
    PAGE_MONITOR_VFD: [proto.TOPIC_VFD_STATUS],
    PAGE_MONITOR_FLOWMETER: [proto.TOPIC_FLOWMETER_STATUS],
    PAGE_MONITOR_VALVE: [proto.TOPIC_VALVE_STATUS, proto.TOPIC_VALVE_ALARM],
    PAGE_DATA_HISTORY: [proto.TOPIC_VFD_STATUS, proto.TOPIC_AI_STATUS],
    PAGE_DATA_ALARM: [],
    PAGE_SETTINGS_REMOTE: [proto.TOPIC_VFD_STATUS],
}


class Router(object):
    """请求分派表。

    :param hub: WebHub
    :param sessions: SessionStore
    :param static_dir: 静态文件目录
    :param log: RunLog
    """

    def __init__(self, hub, sessions, static_dir, log,
                 upstream_host="", upstream_port=0):
        self.hub = hub
        self.sessions = sessions
        self.static_dir = static_dir
        self.log = log
        self.upstream_host = upstream_host
        self.upstream_port = upstream_port
        #: conn_id → Subscriber 的反查由 hub 维护；SSE 连接在下面登记。
        self._sse_bodies = {}

    # ==================================================================
    #                          入口
    # ==================================================================
    async def __call__(self, request):
        path = request.path
        method = request.method

        if method == "OPTIONS":
            # 同源部署下用不到 CORS；给一个宽松的预检应答方便分开部署调试。
            return Response(204, b"", "", {
                "Access-Control-Allow-Origin": "*",
                "Access-Control-Allow-Headers": "Authorization, Content-Type",
                "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
            })

        if path.startswith("/api/"):
            return await self._api(request)

        if method not in ("GET", "HEAD"):
            raise HttpError(405, "静态资源只支持 GET")

        # ---- 静态文件 ----
        rel = path
        if rel in ("", "/"):
            rel = "index.html"
        body = httpd.read_static(self.static_dir, rel)
        if body is None:
            # 前端是单页应用：未知路径回 index.html，由前端路由接管。
            if "." not in rel.rsplit("/", 1)[-1]:
                body = httpd.read_static(self.static_dir, "index.html")
            if body is None:
                raise HttpError(404, "找不到 %s" % rel)
            rel = "index.html"
        mime = httpd.guess_mime(rel)
        extra = {}
        if rel.endswith(".html"):
            # [不缓存 HTML] 板端升级后浏览器还拿旧页面是最常见的"改了没用"。
            extra["Cache-Control"] = "no-cache"
        return Response(200, body, mime, extra)

    # ==================================================================
    #                          API 分派
    # ==================================================================
    async def _api(self, request):
        path = request.path
        method = request.method

        # ---- 无鉴权端点 ----
        if path == "/api/login" and method == "POST":
            return await self._login(request)
        if path == "/api/logout" and method == "POST":
            return self._logout(request)
        if path == "/api/pages" and method in ("GET", "HEAD"):
            return json_response(self._pages_json())
        if path == "/api/ping":
            return json_response({"ok": True, "server": "hsf-webui"})
        if path == "/api/version" and method in ("GET", "HEAD"):
            return json_response(self._version_json())

        # ---- 需要登录 ----
        session = self._require_session(request)

        if path == "/api/me" and method in ("GET", "HEAD"):
            return json_response({"ok": True, "session": session.to_json(),
                                  "runMode": self._mode_json()})
        if path == "/api/state" and method in ("GET", "HEAD"):
            return json_response(self._state_json(session))
        if path == "/api/stream" and method in ("GET", "HEAD"):
            return self._sse_response(request, session)
        if path == "/api/topics" and method == "POST":
            return self._set_topics(request, session)
        if path == "/api/status" and method in ("GET", "HEAD"):
            return json_response(self._status_json())
        if path == "/api/log" and method in ("GET", "HEAD"):
            return json_response(self._log_json(request))

        # ---- 历史 / 告警 ----
        if path == "/api/history" and method in ("GET", "HEAD"):
            return json_response(self._history_json(request))
        if path == "/api/history/files" and method in ("GET", "HEAD"):
            return json_response({"ok": True,
                                  "files": self.hub.history.files(),
                                  "stats": self.hub.history.stats()})
        if path == "/api/alarms" and method in ("GET", "HEAD"):
            return json_response(self._alarms_json(request))
        if path == "/api/alarms/active" and method in ("GET", "HEAD"):
            return json_response({"ok": True,
                                  "active": self.hub.alarms.active()})

        # ---- 运行模式 ----
        if path == "/api/mode":
            if method in ("GET", "HEAD"):
                return json_response({"ok": True, **self._mode_json()})
            if method == "POST":
                return await self._set_mode(request, session)

        # ---- 控制类 ----
        if path == "/api/vfd/control" and method == "POST":
            return await self._vfd_control(request, session)
        if path == "/api/vfd/snapshot" and method == "POST":
            return await self._vfd_snapshot(session)
        if path == "/api/digital/control" and method == "POST":
            return await self._digital_control(request, session)
        if path == "/api/digital/snapshot" and method == "POST":
            return await self._digital_snapshot(request, session)
        if path == "/api/valve/control" and method == "POST":
            return await self._valve_control(request, session)
        if path == "/api/valve/snapshot" and method == "POST":
            return await self._valve_snapshot(session)
        if path == "/api/jog/start" and method == "POST":
            return await self._jog_start(request, session)
        if path == "/api/jog/stop" and method == "POST":
            return await self._jog_stop(request, session)

        raise HttpError(404, "未知接口 %s %s" % (method, path))

    # ==================================================================
    #                          鉴权
    # ==================================================================
    def _require_session(self, request):
        token = request.bearer_token()
        session = self.sessions.get(token)
        if session is None:
            raise HttpError(401, "未登录或会话已过期")
        return session

    def _gate(self, session):
        """控制闸门（未通过则抛 403）。"""
        reason = auth_mod.check_control_gate(session, self.hub.model)
        if reason:
            raise HttpError(403, reason)
        return True

    async def _login(self, request):
        doc = request.json(default={}) or {}
        user = str(doc.get("user", "") or "").strip()
        password = str(doc.get("password", "") or "")
        remote = request.client
        try:
            role = await auth_mod.verify_with_upstream(
                self.upstream_host, self.upstream_port, user, password,
                log=self.log.write, timeout=12.0)
        except auth_mod.LoginError as exc:
            self.log.warn("Web 登录失败（%s）：%s" % (user or "?", exc))
            raise HttpError(401, str(exc))
        session = self.sessions.create(user, role, remote)
        self.log.info("Web 登录成功：%s（%s，来自 %s）"
                      % (user, auth_mod.role_text(role), remote))
        return json_response({
            "ok": True,
            "token": session.token,
            "session": session.to_json(),
            "runMode": self._mode_json(),
        }, headers={
            # Cookie 与 Bearer 都给：前端可用任一（Cookie 让刷新页面不掉线）。
            "Set-Cookie": "hsf_token=%s; Path=/; HttpOnly; SameSite=Lax"
                          % session.token,
        })

    def _logout(self, request):
        token = request.bearer_token()
        session = self.sessions.get(token)
        if session is not None:
            self.sessions.drop(token)
            self.log.info("Web 登出：%s" % session.user)
        return json_response({"ok": True}, headers={
            "Set-Cookie": "hsf_token=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax",
        })

    # ==================================================================
    #                          状态类
    # ==================================================================
    def _mode_json(self):
        m = self.hub.model
        return {"runMode": m.run_mode, "auto": m.is_auto(),
                "known": m.run_mode_known}

    def _state_json(self, session):
        snap = self.hub.model.snapshot()
        snap["ok"] = True
        snap["session"] = session.to_json()
        snap["link"] = self.hub._link_json()
        snap["serverTimeMs"] = model_mod.wall_ms()
        return snap

    def _status_json(self):
        stats = self.hub.stats()
        stats["ok"] = True
        stats["sessions"] = self.sessions.list()
        stats["http"] = {
            "requestsTotal": getattr(self._http, "requests_total", 0)
            if hasattr(self, "_http") else 0,
        }
        stats["serverTimeMs"] = model_mod.wall_ms()
        return stats

    def _log_json(self, request):
        limit = _int_arg(request, "limit", 200, 1, 800)
        level = request.query.get("level") or None
        contains = request.query.get("contains") or None
        return {"ok": True,
                "lines": self.hub.log.tail(limit=limit, level=level,
                                           contains=contains)}

    def _history_json(self, request):
        limit = _int_arg(request, "limit", 300, 1, 3600)
        since = _int_arg(request, "since", 0, 0, 2 ** 62)
        rows = self.hub.history.query(limit=limit,
                                      since_ms=since or None)
        return {"ok": True, "rows": rows, "stats": self.hub.history.stats()}

    def _alarms_json(self, request):
        limit = _int_arg(request, "limit", 200, 1, 500)
        level = request.query.get("level") or None
        kind = request.query.get("kind") or None
        return {"ok": True,
                "alarms": self.hub.alarms.query(limit=limit, level=level,
                                                kind=kind),
                "active": self.hub.alarms.active()}

    def _pages_json(self):
        navigable = []
        for caption, page, child_of in NAV_TREE:
            navigable.append({"caption": caption, "page": page,
                              "childOf": child_of})
        submenus = {}
        for page, entries in SUBMENU_ENTRIES.items():
            submenus[str(page)] = [{"name": n, "page": p} for n, p in entries]
        return {
            "ok": True,
            "count": PAGE_COUNT,
            "names": PAGE_NAMES,
            "nav": navigable,
            "mainMenu": [{"name": n, "page": p, "icon": i}
                         for n, p, i in MAIN_MENU_ENTRIES],
            "subMenus": submenus,
            "pageTopics": {str(k): [int(t) for t in v]
                           for k, v in PAGE_TOPICS.items()},
            "roles": {"anonymous": proto.ROLE_ANONYMOUS,
                      "user": proto.ROLE_USER,
                      "admin": proto.ROLE_ADMIN},
        }

    def _version_json(self):
        return {"ok": True, "name": "hsf-webui", "protocol": "XLink v2 (0x0200)",
                "pages": PAGE_COUNT,
                # 前端据此判断"后端换了但浏览器还是旧页面"
                "features": ["sse", "history-csv", "alarm-edge", "jog-hold"]}

    # ==================================================================
    #                          SSE
    # ==================================================================
    def _sse_response(self, request, session):
        hub = self.hub
        sub = hub.register_subscriber(session.user, session.role)

        async def _stream(writer):
            try:
                await httpd.sse_producer(
                    writer,
                    lambda: sub.queue,
                    log=self.log,
                    on_close=lambda: hub.unregister_subscriber(sub))
            finally:
                hub.unregister_subscriber(sub)

        return Response(200, b"", "text/event-stream; charset=utf-8",
                        is_sse=True).__class__(200, _CoroBody(_stream),
                                               "text/event-stream; charset=utf-8",
                                               is_sse=True)

    def _set_topics(self, request, session):
        """更新本连接的订阅意图。

        [为什么按 connId 而不是按 token] 一个浏览器会话可能开多个标签页
        （两个 SSE 连接），各自停在不同的页。按 token 收敛会把两个标签页
        的需求混成一份，切页时互相覆盖。故前端把 SSE 首帧里拿到的 connId
        回传上来。
        """
        doc = request.json(default={}) or {}
        conn_id = doc.get("connId")
        topics = doc.get("topics") or []
        if conn_id is None:
            # 没给 connId：退化成"该用户的全部连接都设成这批主题"。
            targets = [s for s in self.hub.subscribers.values()
                       if s.user == session.user]
        else:
            sub = self.hub.subscribers.get(int(conn_id))
            targets = [sub] if sub is not None else []
        topic_ids = []
        for t in topics:
            try:
                topic_ids.append(int(t))
            except (TypeError, ValueError):
                continue
        for sub in targets:
            self.hub.set_subscriber_topics(sub, topic_ids)
        return json_response({"ok": True, "connId": conn_id,
                              "applied": len(targets),
                              "topics": topic_ids})

    # ==================================================================
    #                          控制类
    # ==================================================================
    async def _vfd_control(self, request, session):
        """变频器控制：op/hz/rpm/rev/val/idx。

        [点动走独立端点] op=JOG/JOG_RELEASE 由 `/api/jog/*` 代持，
        不接受从这里直发 —— 否则浏览器切后台被节流时点动会自己停。
        """
        self._gate(session)
        doc = request.json(default={}) or {}
        try:
            op = int(doc.get("op"))
        except (TypeError, ValueError):
            raise HttpError(400, "缺少 op 字段")
        if op in (proto.OP_JOG, proto.OP_JOG_RELEASE):
            raise HttpError(400, "点动请用 /api/jog/start 与 /api/jog/stop")
        if op not in proto.VALID_CONTROL_OPS:
            raise HttpError(400, "不支持的 op=%d" % op)
        payload = {"op": op}
        for key, caster in (("hz", float), ("rpm", float), ("val", float),
                            ("rev", bool), ("idx", int)):
            if key in doc:
                try:
                    payload[key] = caster(doc[key])
                except (TypeError, ValueError):
                    raise HttpError(400, "字段 %s 类型不对" % key)
        self.log.info("VFD 控制：%s（%s）" % (describe_vfd_op(op), session.user))
        result = await self.hub.request(proto.CMD_VFD_CONTROL, payload,
                                        timeout=8.0)
        return json_response(_with_ok(result))

    async def _vfd_snapshot(self, session):
        self._gate_readonly(session)
        result = await self.hub.request(proto.CMD_VFD_SNAPSHOT, None,
                                        timeout=8.0)
        if result and result.get(proto.KEY_OK) is not False:
            self.hub.model.vfd.apply(result)
            self.hub.broadcast("vfd", self.hub.model.vfd.to_json())
        return json_response(_with_ok(result))

    async def _digital_control(self, request, session):
        """DO 控制：单点 {dev,do,on} 或整组 {dev,mask}。"""
        self._gate(session)
        doc = request.json(default={}) or {}
        dev = _require_int(doc, "dev", default=0)
        payload = {"dev": dev}
        if "mask" in doc:
            payload["mask"] = _require_int(doc, "mask", default=0)
        elif "values" in doc:
            payload["values"] = _require_int(doc, "values", default=0)
        elif "do" in doc:
            payload["do"] = _require_int(doc, "do", default=0)
            payload["on"] = bool(doc.get("on", False))
        else:
            raise HttpError(400, "缺 do 或 mask 字段")
        self.log.info("DO 控制：模块%d %s（%s）"
                      % (dev, payload, session.user))
        result = await self.hub.request(proto.CMD_DIGITAL_CONTROL, payload,
                                        timeout=8.0)
        if result and result.get(proto.KEY_OK):
            # 用服务端回的 do 位图同步本地模型（不等下一拍推送）。
            dev_obj = self.hub.model.digital_state(dev)
            dev_obj.do = int(result.get(proto.KEY_DO, dev_obj.do) or 0)
            self.hub.broadcast("digital", dev_obj.to_json())
        return json_response(_with_ok(result))

    async def _digital_snapshot(self, request, session):
        self._gate_readonly(session)
        doc = request.json(default={}) or {}
        dev = _require_int(doc, "dev", default=0)
        result = await self.hub.request(proto.CMD_DIGITAL_SNAPSHOT,
                                        {"dev": dev}, timeout=10.0)
        if result and result.get(proto.KEY_OK) is not False \
                and proto.KEY_NAME in result:
            dev_obj = self.hub.model.digital_state(
                int(result.get(proto.KEY_DEV, dev) or dev))
            dev_obj.apply(result)
            self.hub.broadcast("digital", dev_obj.to_json())
        return json_response(_with_ok(result))

    async def _valve_control(self, request, session):
        self._gate(session)
        doc = request.json(default={}) or {}
        index = _require_int(doc, "index", default=None)
        if index is None:
            raise HttpError(400, "缺 index 字段（阀号 0~5）")
        if "on" not in doc:
            raise HttpError(400, "缺 on 字段（true=开 / false=关）")
        payload = {"index": index, "on": bool(doc.get("on"))}
        self.log.info("阀控制：%s（%s）"
                      % ("开阀" if payload["on"] else "关阀", session.user))
        result = await self.hub.request(proto.CMD_VALVE_CONTROL, payload,
                                        timeout=12.0)
        if result:
            # 服务端在应答里带回一整拍状态（不必再拉快照）。
            if proto.KEY_VALVE_COUNT in result:
                self.hub.model.valve.apply(result)
                self.hub.broadcast("valve", self.hub.model.valve.to_json())
        return json_response(_with_ok(result))

    async def _valve_snapshot(self, session):
        self._gate_readonly(session)
        result = await self.hub.request(proto.CMD_VALVE_SNAPSHOT, None,
                                        timeout=12.0)
        if result and proto.KEY_VALVE_COUNT in result:
            self.hub.model.valve.apply(result)
            self.hub.broadcast("valve", self.hub.model.valve.to_json())
        return json_response(_with_ok(result))

    async def _set_mode(self, request, session):
        doc = request.json(default={}) or {}
        mode = _require_int(doc, "mode", default=None)
        if mode is None or mode not in (0, 1):
            raise HttpError(400, "mode 只能是 0（手动）或 1（自动）")
        # 切模式本身就要 root（服务端也这么判），故不套 control_gate
        # 的"自动模式拒绝"分支 —— 否则自动模式下永远切不回手动。
        if session.role < proto.ROLE_ADMIN:
            raise HttpError(403, "权限不足：切换运行模式需要 root 账户")
        if not self.hub.upstream.is_ready():
            raise HttpError(503, "未连接服务端")
        self.log.info("切换运行模式：%s（%s）"
                      % ("自动" if mode else "手动", session.user))
        result = await self.hub.set_run_mode(mode, session.role)
        out = _with_ok(result)
        out.update(self._mode_json())
        return json_response(out)

    async def _jog_start(self, request, session):
        """按住点动开始（网页服务器代持 200ms 重发）。"""
        self._gate(session)
        doc = request.json(default={}) or {}
        reverse = bool(doc.get("rev", False))
        # 找到调用者的 SSE 连接（用于"页面断开就释放"）。
        conn_id = doc.get("connId")
        try:
            conn_id = int(conn_id) if conn_id is not None else -1
        except (TypeError, ValueError):
            conn_id = -1
        await self.hub.start_jog(reverse, conn_id, session.user)
        return json_response({"ok": True, "active": True,
                              "reverse": reverse,
                              "jogRepeatMs": self.hub.__class__.__dict__
                              .get("JOG_REPEAT_MS", 200)})

    async def _jog_stop(self, request, session):
        await self.hub.stop_jog(reason="操作员松开")
        return json_response({"ok": True, "active": False})

    # ---- 只读类操作的"登录即可"闸门 ----
    def _gate_readonly(self, session):
        """快照类请求：只要求登录（不要求 root，不改设备输出）。

        [为什么快照要单独一道] 快照会**现读设备**（服务端 read_fresh），
        是真实 IO。未登录的匿名请求不该能触发 485 读。
        """
        if not self.hub.upstream.is_ready():
            raise HttpError(503, "未连接服务端")
        return True


class _CoroBody(object):
    """把协程包成"像 bytes 一样"的对象，供 Response 持有 SSE 生产者。

    [为什么需要这个壳] `Response.body` 在普通应答里是 bytes（要算
    Content-Length）；SSE 分支不需要长度，只需要一个可等待的产出函数。
    用一个小壳把它区分开，比给 Response 加一个专用字段更省事 —— 且
    `httpd._serve_sse` 只对它调 `await`。
    """

    __slots__ = ("_fn",)

    def __init__(self, fn):
        self._fn = fn

    def __len__(self):
        return 0

    def __await__(self):
        return self._fn().__await__()


# ============================================================================
#                          小工具
# ============================================================================

def _with_ok(result):
    """把服务端应答规整成前端认的形状（ok/err 恒在）。"""
    if not isinstance(result, dict):
        return {"ok": False, "err": "服务端应答为空"}
    out = dict(result)
    out["ok"] = bool(result.get(proto.KEY_OK))
    if not out["ok"] and not out.get("err"):
        out["err"] = "服务端未说明原因"
    return out


def _int_arg(request, name, default, low, high):
    raw = request.query.get(name)
    if raw in (None, ""):
        return default
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return default
    return max(low, min(high, value))


def _require_int(doc, name, default=None):
    if name not in doc:
        return default
    value = doc[name]
    if isinstance(value, bool):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        raise HttpError(400, "字段 %s 必须是整数" % name)


#: op → 中文（日志/审计用）。
VFD_OP_TEXT = {
    proto.OP_READ_STATUS: "读状态",
    proto.OP_SET_FREQUENCY: "设定频率",
    proto.OP_SET_SPEED: "设定转速",
    proto.OP_START: "启动",
    proto.OP_STOP: "停机",
    proto.OP_FREE_STOP: "自由停车",
    proto.OP_QUICK_STOP: "快速停车",
    proto.OP_RESET_FAULT: "故障复位",
    proto.OP_JOG: "点动",
    proto.OP_SELECT_PRESET_SPEED: "选择预设速度",
    proto.OP_SET_PRESET_SPEED_HZ: "设预设速度",
    proto.OP_SET_ACCEL_DECEL: "设加减速",
    proto.OP_SET_PID_TARGET: "设 PID 目标",
    proto.OP_JOG_RELEASE: "点动释放",
}


def describe_vfd_op(op):
    try:
        return VFD_OP_TEXT.get(int(op), "op=%d" % int(op))
    except (TypeError, ValueError):
        return "op=?"
