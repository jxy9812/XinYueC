# -*- coding: utf-8 -*-
"""REST 路由与 SSE 流：前端与网页服务器之间**唯一**的接口面。

接口一览（与 PARITY_PLAN §2.6 对齐）
====================================

    GET   /api/ping                探活（无鉴权）
    GET   /api/version             版本/特性（无鉴权）
    GET   /api/pages               页面注册表（导航树/页名/每页主题）
    POST  /api/login               登录（用户名/口令 → token；真到服务端认证）
    POST  /api/logout              登出
    GET   /api/me                  当前会话身份

    GET   /api/state               全量快照（首屏/断线恢复用）
    GET   /api/stream              **SSE 实时流**（首帧 snapshot + 按事件推送）
    POST  /api/topics              本连接想订阅的主题（切页时调）
    GET   /api/status              诊断（链路/订阅者/点动/告警统计）
    GET   /api/serverlog           web 服务器自身日志（诊断）

    GET   /api/mode                查询运行模式 + 自动状态族
    POST  /api/mode                切换运行模式（PERM_MODE_SWITCH）
    POST  /api/auto/control        自动运行启停/复位（PERM_AUTO_CONTROL）
    POST  /api/vfd/control         变频器控制（PERM_DEVICE_CONTROL + 闸）
    POST  /api/vfd/snapshot        请求变频器快照（会现读设备）
    GET   /api/vfd/params          变频器参数读（0x011B 转发；登录即可）
    POST  /api/vfd/params          变频器参数写（0x011B 转发；PERM_ACCOUNT_ADMIN）
    POST  /api/jog/start           按住点动开始（web 代持 200ms 重发）
    POST  /api/jog/stop            松开点动（安全动作，不设闸）
    POST  /api/digital/control     DO 单点/整组控制
    POST  /api/digital/snapshot    请求数字量快照
    POST  /api/valve/control       阀开/关
    POST  /api/valve/snapshot      请求阀快照
    POST  /api/pump/control        进水泵开关
    POST  /api/pump/snapshot       进水泵快照
    POST  /api/turbidity/snapshot  浊度仪快照
    POST  /api/flowmeter/snapshot  流量计快照
    POST  /api/temphum/snapshot    温湿度快照

    GET   /api/history/page        服务端历史分页（0x0118 转发）
    GET   /api/history/export      历史导出（按筛选全量批取；csv/xlsx）
    GET   /api/log/page            服务端运行日志分页（0x0119 转发）
    GET   /api/log/export          日志导出（按筛选全量批取；csv/xlsx）
    GET   /api/config              读服务端配置（0x0116）
    POST  /api/config              写服务端配置（0x0117；PERM_ACCOUNT_ADMIN）

    GET   /api/alarms              告警记录（边沿）
    GET   /api/alarms/page         服务端报警历史分页（0x011A 转发）
    GET   /api/alarms/active       当前仍处于异常的条目
    GET   /api/alarm/export        报警导出（按筛选全量批取 0x011A；csv/xlsx）
    GET   /api/wifi                本机无线**只读**快照（适配器 + 当前连接）
    *                               其余路径按静态文件处理

设计约定
========

1) **权限闸逐端点查位表**（permissions.can_role）：未登录 401、权限不够 403
   并给 DENIED_TEXT 原文。**禁止** `role >= ROLE_ADMIN` 数值比较
   （角色值不单调，Operator=3 是追加值——旧骨架因此把操作者误放行成管理员）。

2) **设备控制类**（0x0101/0x0109/0x0111/0x0114 + 点动）先过
   `auth.check_control_gate`（未连接/自动模式两态文案/无设备控制位）。
   **只对"动输出"的命令做这道下限预检**——0x0105/0x0115/0x0117 走各自的
   权限位，不套本闸（否则自动模式下永远切不回手动）。服务端闸门仍然生效，
   web 这道是下限（少走一次无谓的 485 往返）。

3) **错误统一形状**：`{"ok": false, "err": "...", "status": N}`。
   **控制应答原样透传**服务端 ok/err（不洗白）——现场排查时两边的判断
   必须一致。

4) `POST /api/topics` 是"本连接想要什么"，不是"现在订阅什么"：
   hub 按所有连接的**并集**收敛（切页不互踢）。
"""

import asyncio
import json
import os
import re
import time

from . import auth as auth_mod
from . import export as export_mod
from . import httpd
from . import model as model_mod
from . import permissions as perms_mod
from . import proto
from .httpd import (HttpError, Response, json_response, stream_response)

#: XLSX 下载的 MIME（OOXML 工作簿；单点常量，三条导出路由共用）。
XLSX_CONTENT_TYPE = ("application/vnd.openxmlformats-officedocument."
                     "spreadsheetml.sheet")

#: 最近一次历史导出的进度（网页端状态行轮询；同一时刻只保留最近一次）。
#: [为什么用模块级] 导出在处理协程里跑，前端在**另一条 HTTP 请求**里问
#: 进度（同一个事件循环）⇒ 一个共享快照最省事，不必给 SSE 加新事件类型。
_EXPORT_PROGRESS = {
    "job": "", "active": False, "format": "", "rows": 0, "bytes": 0,
    "startedMs": 0, "phase": "", "error": "",
}

#: 用户取消的导出 job id（批取循环每批检查一次；XLSX 在写盘阶段就会中断，
#: CSV 已发出的部分由 `_guarded_export` 追加 #EXPORT_ABORTED 说明行）。
_EXPORT_CANCEL = set()


class _ExportCancelled(Exception):
    """用户取消导出（只用于内部流程，不对外暴露成 500）。"""

# ============================================================================
#                  页面注册表（2026-11-22 起页 6 已拆分）
# ============================================================================
# [为什么后端也要有一份] 前端渲染导航树与页名；改页面顺序只改一处
# （与 C 端页序对齐是验收口径之一）。**不是**旧骨架的「监视/变频器/
# 流量计/电磁阀」口径——2026-11-10 用户定案改名后以 pages_registry 为准。
# [2026-11-22 页 6 拆分] 网页端把「水质监测」拆成页 6「水质」+ 页 18
# 「流量」；**桌面客户端（client_py）与 C 端未拆** ⇒ 两端页数/页名在此
# 有意不同（网页端 19 页 / 客户端 18 页），其余页号仍逐号对齐。

PAGE_HOME = 0
PAGE_MENU = 1
PAGE_MONITOR_MENU = 2
PAGE_MONITOR_DIGITAL = 3
PAGE_MONITOR_ANALOG = 4
PAGE_MONITOR_VFD = 5
# [2026-11-22 页 6 拆分] 原「水质监测」= 电磁流量计 + 两台浊度仪 + 进水泵
# 一页看完；现按用户口径拆成两页：页 6「水质」只留浊度×2（含水温），
# 流量计/进水泵移到**追加末尾**的页 18「流量」。页号只追加、不重排
# （跨端页号是操作手册契约）。**C 端未拆**（HsfPages.c 仍是一页「流量计」）
# ⇒ 跨端页数/页名因此不同（网页端 19 页 / C 端 18 页），已知且可接受。
PAGE_MONITOR_WATER = 6
#: [兼容别名] 旧常量名仍指向页 6（外部脚本/文档按旧名引用；语义已收窄为
#: 「水质」，流量计部分见 PAGE_MONITOR_FLOW）。
PAGE_MONITOR_FLOWMETER = PAGE_MONITOR_WATER
PAGE_MONITOR_VALVE = 7
PAGE_DATA_MENU = 8
PAGE_DATA_HISTORY = 9
PAGE_DATA_LOG = 10
PAGE_DATA_ALARM = 11
PAGE_SETTINGS_MENU = 12
PAGE_SETTINGS_LOCAL = 13
PAGE_SETTINGS_REMOTE = 14
PAGE_SETTINGS_NETWORK = 15
# [2026-11-16 Web 设置（仅桌面 Python 客户端）] 页号 16 在**桌面客户端**是
# 「Web 设置」（网页服务/域名/远程端口/二维码）。网页端**不实现**该页，
# 但**页号槽位保留** ⇒ 网页端的页号与桌面客户端逐号对齐（同一份操作手册
# 的同一个页号，两端指的是同一页）。⇒ 网页端相机页 = 17（不是 16）。
PAGE_SETTINGS_WEB = 16        #: 保留槽位（网页端不实现；导航树里无此节点）
# [2026-11-19 相机页] 页号**只能追加末尾**（跨端页号是操作手册契约，不许
# 重排）⇒ 相机页 = 17。
PAGE_MONITOR_CAMERA = 17
# [2026-11-22 页 6 拆分] 「流量」页 = 18（页号追加末尾；16 是桌面端 Web 设置
# 保留槽位、17 是相机）。内容 = 电磁流量计 + 进水泵 + 流量趋势（见
# static/js/pages/page18_flow.js）。
PAGE_MONITOR_FLOW = 18
# [2026-11-13 三页合并] 原 16「悬浮窗设置」/ 17「窗口设置」两页**并入 13
# 「本地设置」**（三个分区：界面设置 / 悬浮窗 / 窗口与屏幕），两个页号从
# 注册表与导航树删除、**其余页号一律不变**（跨端页号是操作手册契约）。
# ⇒ 页数 18 → 16（PAGE_COUNT=16），页号 0..15 连续。
# [2026-11-19] 相机页追加 ⇒ 页数 16 → 18（PAGE_COUNT=18），页号 0..17
# （16 是保留槽位，不在导航树里）。
# [2026-11-22 页 6 拆分] 流量页追加末尾 ⇒ 页数 18 → 19（页号 0..18）。
PAGE_COUNT = 19

PAGE_NAMES = [
    "主界面", "主菜单", "设备", "数字量", "模拟量",
    # [2026-11-05 现场口径] 这路设备驱动的是**搅拌泵**。页名按工艺用途叫
    # 「搅拌泵」，设备**类别**仍是变频器（首页"类型"列显示变频器）。
    "搅拌泵", "水质", "电动球阀",
    "数据", "历史数据", "日志", "报警",
    "设置", "本地设置", "远端设置", "网络设置",
    # [2026-11-16] 保留槽位：桌面客户端的「Web 设置」页（网页端不实现）⇒
    # 两端页号逐号对齐（导航树里没有这一项，故网页端不会出现它）。
    "Web 设置",
    # [2026-11-19] 相机页（设备组下）。
    "相机",
    # [2026-11-22] 流量页（页 6 拆分后追加末尾）：电磁流量计 + 进水泵。
    "流量",
]

#: 导航树 (caption, page_index, child_of)；child_of=-1 为顶层。
#: child_of 是**父节点在本表里的下标**（与 C 端 kHsfNavNode.childOf 同义）。
#: [2026-11-22 页 6 拆分] 设备组下顺序：数字量、模拟量、搅拌泵、水质、流量、
#: 电动球阀、相机（「流量」插在「水质」之后）⇒ 其后各组的 child_of 随下标
#: 后移（数据组 9 / 设置组 13）。C 端未拆 ⇒ 导航项与网页端不同（已知差异）。
NAV_TREE = [
    ("主界面",     PAGE_HOME,               -1),
    ("设备",       PAGE_MONITOR_MENU,       -1),
    ("数字量",     PAGE_MONITOR_DIGITAL,     1),
    ("模拟量",     PAGE_MONITOR_ANALOG,      1),
    ("搅拌泵",     PAGE_MONITOR_VFD,         1),
    ("水质",       PAGE_MONITOR_WATER,       1),
    ("流量",       PAGE_MONITOR_FLOW,        1),
    ("电动球阀",   PAGE_MONITOR_VALVE,       1),
    ("相机",       PAGE_MONITOR_CAMERA,      1),
    ("数据",       PAGE_DATA_MENU,          -1),
    ("历史数据",   PAGE_DATA_HISTORY,        9),
    ("日志",       PAGE_DATA_LOG,            9),
    ("报警",       PAGE_DATA_ALARM,          9),
    ("设置",       PAGE_SETTINGS_MENU,      -1),
    ("本地设置",   PAGE_SETTINGS_LOCAL,     13),
    ("远端设置",   PAGE_SETTINGS_REMOTE,    13),
    ("网络设置",   PAGE_SETTINGS_NETWORK,   13),
]

#: 子菜单页的条目（与 client pages_registry.SUBMENU_ENTRIES 同源）。
SUBMENU_ENTRIES = {
    PAGE_MONITOR_MENU: [
        ("数字量", PAGE_MONITOR_DIGITAL),
        ("模拟量", PAGE_MONITOR_ANALOG),
        ("搅拌泵", PAGE_MONITOR_VFD),
        ("水质", PAGE_MONITOR_WATER),
        ("流量", PAGE_MONITOR_FLOW),
        ("电动球阀", PAGE_MONITOR_VALVE),
        ("相机", PAGE_MONITOR_CAMERA),
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
    ],
}

#: 主菜单入口（client MAIN_MENU_ENTRIES 同源：主界面/设备/数据/设置）。
#: [2026-11-22 用户定案「菜单页档主界面打不开」] 菜单页模式隐藏导航树 ⇒
#: 主菜单必须自带「主界面」；三端（client/web）条目顺序逐项一致。
MAIN_MENU_ENTRIES = [
    ("主界面", PAGE_HOME, "⌂"),
    ("设备", PAGE_MONITOR_MENU, "▤"),
    ("数据", PAGE_DATA_MENU, "≣"),
    ("设置", PAGE_SETTINGS_MENU, "⚙"),
]

#: 每页需要的推送主题（切页时前端把它 POST 给 /api/topics）。
#:
#: [全局常驻] 温湿度/健康/**自动告警**由 hub.GLOBAL_TOPICS 无条件订阅，
#: 这里不必列（自动告警是事件流 ⇒ 不常驻就收不到，2026-11-14 修）。
#: [历史页不订遥测] 它是查询型（0x0118/0x0119），在线时不订遥测主题。
PAGE_TOPICS = {
    PAGE_HOME: [proto.TOPIC_VFD_STATUS, proto.TOPIC_TEMPHUM_STATUS,
                proto.TOPIC_AI_STATUS, proto.TOPIC_FLOWMETER_STATUS,
                proto.TOPIC_TURBIDITY_STATUS, proto.TOPIC_PUMP_STATUS,
                proto.TOPIC_DIGITAL_STATUS, proto.TOPIC_VALVE_STATUS],
    PAGE_MONITOR_DIGITAL: [proto.TOPIC_DIGITAL_STATUS],
    PAGE_MONITOR_ANALOG: [proto.TOPIC_AI_STATUS],
    PAGE_MONITOR_VFD: [proto.TOPIC_VFD_STATUS],
    # [2026-11-22 页 6 拆分] 水质页只订浊度（流量计/进水泵已移到页 18）。
    PAGE_MONITOR_WATER: [proto.TOPIC_TURBIDITY_STATUS],
    #: [2026-11-22] 流量页 = 流量计 + 进水泵两个主题；趋势只画流量。
    PAGE_MONITOR_FLOW: [proto.TOPIC_FLOWMETER_STATUS,
                        proto.TOPIC_PUMP_STATUS],
    PAGE_MONITOR_VALVE: [proto.TOPIC_VALVE_STATUS],
    # [相机页 2026-11-19] 只订相机主题 ⇒ **只有打开相机页才会让服务端推
    # 画面**（服务端按订阅门控：没人订阅时一帧都不推）。
    PAGE_MONITOR_CAMERA: [proto.TOPIC_CAMERA_FRAME],
}

#: 远端配置的合法节（0x0117 的 section；逐节 fail-closed）。
#: [protect 2026-11-20 补] 运行保护节（空管/压差/电机超温）一直漏在白名单
#: 外 ⇒ 网页端「应用运行保护」恒回 400「section 必须是…之一」（2026-11-15
#: 起就在，实锅：手机浏览器 E2E 实测抓到——webui_check 没测过这条 HTTP
#: 通路）。服务端对键仍逐节 fail-closed，放行节名不放松键校验。
CONFIG_SECTIONS = ("collect", "storage", "login", "auto", "camera", "protect")

#: 「现读快照」端点的上游等待上限（秒）。
#:
#: [为什么是 4s 而不是 10~12s] 上游正常应答在毫秒级；上游忙/卡时**早失败
#: 比晚失败有用**——调用方（界面「请求快照」按钮、验收脚本的轮询）都在自己
#: 的窗口里重试，一次现读把整个窗口吃掉的话，后面就没有重试机会了
#: （2026-11-13 第 3 轮验收：上游一拍的卡顿 ⇒ 阀/DO/泵的回读轮询直接超时
#: 判失败）。超时后走**推送态回落**（_cached_*），数据同源、值不编造。
SNAPSHOT_TIMEOUT_S = 4.0

#: 变频器参数（0x011B）的上游等待上限（秒）。
#:
#: [为什么比快照宽] 0x011B op=0 要把 0xF100 段**逐个**从驱动器读回来
#: （Modbus 一问一答，板端 CPU 忙时 4s 不够）；op=1 更重（写 + 回读重试
#: 确认）。⇒ 读给 8s、写给 20s（与桌面客户端「超时随条目数放大」同款
#: 口径：服务端最坏写时长 ≈ 6 参数 × 写 + 3 次回读 × 0.2s ≈ 17s）。
VFD_PARAMS_TIMEOUT_S = 8.0
VFD_PARAMS_WRITE_TIMEOUT_S = 20.0


def _accepts_gzip(request):
    """对端是否接受 gzip（RFC 7231 的 Accept-Encoding 判定）。

    [为什么要自己判] 静态资源是文本为主（首屏原始 691.1 KB）⇒ 真正命中
    gzip 后线上只传 236.2 KB（1/2.93，板端弱 WiFi 上这一趟差别很大）。
    [数字订正 2026-11-22] 这里曾写「688.7 KB → 约 1/5」，那是**错的**：
    旧 gzip 判定漏掉了带 `; charset=utf-8` 的 .js（32 个一个没压），
    实测只压到 623.6 KB（90%）；修好后才是 236.2 KB（34%）。
    但**不能无脑压**：不支持的对端拿到 gzip 体会解不开。

    [口径] `gzip` 或 `*` 且 **q > 0** 才算接受（`gzip;q=0` 是显式拒绝）。
    """
    raw = ""
    try:
        raw = request.header("accept-encoding") or ""
    except Exception:                                   # noqa: BLE001
        raw = ""
    if not raw:
        return False
    for part in raw.split(","):
        bits = part.strip().split(";")
        name = bits[0].strip().lower()
        if name not in ("gzip", "*"):
            continue
        quality = 1.0
        for extra in bits[1:]:
            extra = extra.strip()
            if extra.lower().startswith("q="):
                try:
                    quality = float(extra[2:])
                except ValueError:
                    quality = 0.0
        if quality > 0.0:
            return True
    return False


def _etag_matches(if_none_match, etag):
    """请求的 `If-None-Match` 是否命中当前 ETag（RFC 7232）。

    [口径] `If-None-Match` 是**逗号分隔的列表**；`*` 表示「资源存在即命中」
    （用于 Range 之类）。逐项比时要忽略 `W/` 前缀的强弱差异（弱比较即可）。
    大小写敏感（ETag 是不透明串，按原文比）。
    """
    if not if_none_match or not etag:
        return False
    raw = if_none_match.strip()
    if raw == "*":
        return True
    want = etag.strip()
    for part in raw.split(","):
        item = part.strip()
        if not item:
            continue
        # 弱比较：去掉 W/ 前缀后逐字比。
        if item.startswith("W/"):
            item = item[2:].strip()
        base = want
        if base.startswith("W/"):
            base = base[2:].strip()
        if item == base:
            return True
    return False


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
        #: 登录限流（IP 令牌桶 + 账号失败退避；见 auth.LoginThrottle）。
        self.login_throttle = auth_mod.LoginThrottle()

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
        body, gz, etag = httpd.read_static_cached(self.static_dir, rel)
        if body is None:
            # 前端是单页应用：未知路径回 index.html，由前端路由接管。
            if "." not in rel.rsplit("/", 1)[-1]:
                body, gz, etag = httpd.read_static_cached(self.static_dir,
                                                          "index.html")
            if body is None:
                raise HttpError(404, "找不到 %s" % rel)
            rel = "index.html"
        mime = httpd.guess_mime(rel)
        extra = {}
        if rel.endswith((".html", ".js", ".css")):
            # [不缓存 HTML/JS/CSS] 升级后浏览器还拿旧页面是最常见的"改了没用"。
            # JS/CSS 原本没有 Cache-Control ⇒ 浏览器按启发式缓存，
            # 页面工匠改完 .js 验收时跑的还是旧字节码（2026-11-13 实锅，
            # 与「界面进程必须重启才加载新代码」同一族）。本部署是局域网
            # 单页应用，no-cache 的代价可忽略。
            extra["Cache-Control"] = "no-cache"
        if rel == "index.html":
            # [版本戳：改一次 JS 必刷新一次 2026-11-19] 光有 no-cache 还不够
            # —— 浏览器**启发式缓存**里已经躺着一份旧 index.html 时，它按旧
            # 引用去取旧 js（no-cache 只保证"下次重新问"，可旧 js 自己也带
            # 一份老缓存）⇒ 用户看到"改了没生效、刷新也不对"（本次真发过：
            # 用户看到的循环分钟数是 10/5/5，服务端真值是 30/10/10 ⇒ 页面
            # 跑的是**旧 JS**）。
            # ⇒ 给 index.html 里的每个 <script src>/<link href> 注入
            # **?v=<静态资源指纹>** ⇒ 文件一改，指纹变 ⇒ URL 变 ⇒ 浏览器
            # 必然重新取。指纹 = 静态目录里最新 mtime（进程内缓存 1s）。
            body = self._versioned_index(body)
        elif gz is not None and _accepts_gzip(request):
            # [gzip 2026-11-20 性能优化] 网页端是单页应用 ⇒ 首屏要下
            # index.html 引用的**全部** JS/CSS（实测 33 个文件共 691.1 KB）。
            # 未压缩走一趟在板端的弱 WiFi 上很慢 ⇒ 文本类资源压缩后发
            # （实测首屏字节 623.6 KB → 236.2 KB）。
            # · `Content-Encoding: gzip` + `Vary: Accept-Encoding`
            #   （Vary 必带：否则中间缓存会把 gzip 体发给不支持的客户端）。
            # · 不支持 gzip 的对端（老浏览器/脚本）拿到的仍是原字节。
            extra["Content-Encoding"] = "gzip"
            extra["Vary"] = "Accept-Encoding"
            body = gz
        # [条件请求 2026-11-21 性能优化] 浏览器带着 `If-None-Match` 回来
        # ⇒ 文件没改（ETag 同）⇒ 回 **304 零字节**。
        # [为什么不改成 immutable 长缓存] no-cache 的语义是「每次都问」，
        # 正是为了不重蹈 2026-11-13「改了 JS 页面没变」的覆辙 ⇒ 保留
        # no-cache，只把「问了但没变」的成本从「全量字节」降到「0 字节」。
        # （首屏 236 KB ⇒ 第二次打开只有 304 头，几 KB。）
        if etag:
            extra["ETag"] = etag
            if _etag_matches(request.header("if-none-match"), etag):
                return Response(304, b"", mime, extra)
        return Response(200, body, mime, extra)

    def _versioned_index(self, body):
        """给 index.html 的静态引用注入版本戳（?v=<指纹>）。"""
        try:
            text = body.decode("utf-8")
        except (AttributeError, UnicodeDecodeError):
            return body
        stamp = self._static_stamp()
        if not stamp:
            return body
        # [为什么用正则] index.html 里是 `<script src="js/x.js">` /
        # `<link href="css/theme.css">` ⇒ 按 (src|href)="…(.js|.css)" 抓，
        # 末尾追加 ?v=指纹（已带 ?v 的不动）。
        pat = re.compile(r'((?:src|href)=")([^"]+\.(?:js|css))(")')
        text = pat.sub(lambda m: m.group(1) + m.group(2) + "?v=" + stamp
                       + m.group(3), text)
        return text.encode("utf-8")

    def _static_stamp(self):
        """静态资源指纹（最新 mtime，秒级；进程内缓存 1s ⇒ 不扫盘太勤）。"""
        now = time.time()
        cached = getattr(self, "_stamp_cache", None)
        if cached and now - cached[0] < 1.0:
            return cached[1]
        stamp = ""
        try:
            newest = 0.0
            for root, _dirs, files in os.walk(self.static_dir or ""):
                for name in files:
                    try:
                        mt = os.path.getmtime(os.path.join(root, name))
                    except OSError:
                        continue
                    if mt > newest:
                        newest = mt
            if newest > 0:
                stamp = str(int(newest))
        except OSError:
            stamp = ""
        self._stamp_cache = (now, stamp)
        return stamp

    # ==================================================================
    #                          API 分派
    # ==================================================================
    async def _api(self, request):
        path = request.path
        method = request.method

        # ---- 无鉴权端点 ----
        if path == "/api/ping":
            return json_response({"ok": True, "server": "hsf-webui"})
        if path == "/api/version" and method in ("GET", "HEAD"):
            return json_response(self._version_json())
        if path == "/api/pages" and method in ("GET", "HEAD"):
            return json_response(self._pages_json())
        if path == "/api/login" and method == "POST":
            return await self._login(request)
        if path == "/api/logout" and method == "POST":
            return self._logout(request)

        # ---- 需要登录（含匿名角色登录后的只读面）----
        session = self._require_session(request)

        if path == "/api/me" and method in ("GET", "HEAD"):
            return json_response({"ok": True, "session": session.to_json(),
                                  "runMode": self.hub.model.mode_json()})
        if path == "/api/state" and method in ("GET", "HEAD"):
            return json_response(self._state_json(session))
        if path == "/api/stream" and method in ("GET", "HEAD"):
            return self._sse_response(request, session)
        if path == "/api/topics" and method == "POST":
            return self._set_topics(request, session)
        if path == "/api/status" and method in ("GET", "HEAD"):
            return json_response(self._status_json())
        if path == "/api/serverlog" and method in ("GET", "HEAD"):
            return json_response(self._serverlog_json(request))
        # ---- 相机画面（image/jpeg）----
        # [为什么走 HTTP 而不是 SSE 推字节] SSE 是**文本**通道 ⇒ JPEG 得
        # base64（+33% 体积、每帧转一次）⇒ 帧本体留在 web 进程内存，这里
        # 现取；SSE 只广播元信息（有新帧 + 序号）⇒ 前端换 URL 破缓存。
        if path == "/api/camera/frame" and method in ("GET", "HEAD"):
            return self._camera_frame_response(request, session)
        # ---- 相机 ES 增量（2026-11-19 直传；二进制 NAL 流）----
        # [与 /frame 的分工] transport=stream 时服务端推的是编码流 NAL
        # （不是 JPEG）⇒ 浏览器 WebCodecs 解码；本端点给**自 cursor 以来
        # 的 NAL**（二进制 length 前缀），画面本体照旧不过 SSE。
        if path == "/api/camera/es" and method in ("GET", "HEAD"):
            return self._camera_es_response(request, session)

        # ---- 告警（web 本地边沿记录）----
        if path == "/api/alarms" and method in ("GET", "HEAD"):
            return json_response(self._alarms_json(request))
        if path == "/api/alarms/active" and method in ("GET", "HEAD"):
            return json_response({"ok": True,
                                  "active": self.hub.alarms.active()})
        # ---- 告警**服务端历史**分页（0x011A 转发；报警页主数据源）----
        # [为什么与 /api/alarms 并存] /api/alarms = web 进程内存里的边沿
        # 存档（老服务端兜底）；/api/alarms/page = 服务端 alarms 表（单一
        # 事实源，与桌面客户端同一份）。上游没有 0x011A（C 端）⇒ 回**显式
        # 失败包络**（ok=false + err），前端据此回落本地存档（fail-open）。
        if path == "/api/alarms/page" and method in ("GET", "HEAD"):
            return await self._alarms_page(request, session)
        # ---- 告警导出（0x011A 批取；csv/xlsx；权限闸 PERM_DOWNLOAD）----
        # [为什么单开一条而不是复用 /api/alarms/page] 导出要的是**按筛选
        # 全量**（分页批取 + 同一锚点），分页接口只给一页 ⇒ 两条路形状
        # 不同（一个 JSON 一个文件流），各走各的更清楚。
        if path == "/api/alarm/export" and method in ("GET", "HEAD"):
            return await self._alarm_export(request, session)

        # ---- 本机无线（只读快照；能力不可用时给原因，不报错）----
        if path == "/api/wifi" and method in ("GET", "HEAD"):
            return json_response(self._wifi_json())

        # ---- 运行模式 / 自动控制 ----
        if path == "/api/mode":
            if method in ("GET", "HEAD"):
                return json_response({"ok": True,
                                      **self.hub.model.mode_json()})
            if method == "POST":
                return await self._set_mode(request, session)
        if path == "/api/auto/control" and method == "POST":
            return await self._auto_control(request, session)

        # ---- 控制类（设备控制位 + 自动模式闸）----
        if path == "/api/vfd/control" and method == "POST":
            return await self._vfd_control(request, session)
        if path == "/api/vfd/snapshot" and method == "POST":
            return await self._vfd_snapshot(session)
        # ---- 变频器参数（0x011B 转发；读=登录，写=管理员）----
        # [为什么读走 GET、写走 POST] 与桌面客户端同款：0x011B 的 op 字段
        # 在协议里存在，但把「读」也塞进 POST 会让浏览器的 GET 缓存/预取
        # 语义混乱，且读是幂等面、写是动作面 ⇒ 网关层分清更安全。
        if path == "/api/vfd/params":
            if method in ("GET", "HEAD"):
                return await self._vfd_params_get(session)
            if method == "POST":
                return await self._vfd_params_write(request, session)
        if path == "/api/jog/start" and method == "POST":
            return await self._jog_start(request, session)
        if path == "/api/jog/stop" and method == "POST":
            return await self._jog_stop(session)
        if path == "/api/digital/control" and method == "POST":
            return await self._digital_control(request, session)
        if path == "/api/digital/snapshot" and method == "POST":
            return await self._digital_snapshot(request, session)
        if path == "/api/valve/control" and method == "POST":
            return await self._valve_control(request, session)
        if path == "/api/valve/snapshot" and method == "POST":
            return await self._valve_snapshot(session)
        if path == "/api/pump/control" and method == "POST":
            return await self._pump_control(request, session)
        if path == "/api/pump/snapshot" and method == "POST":
            return await self._pump_snapshot(session)
        if path == "/api/turbidity/snapshot" and method == "POST":
            return await self._turbidity_snapshot(session)
        if path == "/api/flowmeter/snapshot" and method == "POST":
            return await self._flowmeter_snapshot(session)
        if path == "/api/temphum/snapshot" and method == "POST":
            return await self._temphum_snapshot(request, session)

        # ---- 数据三页（服务端历史库/日志库转发）----
        if path == "/api/history/page" and method in ("GET", "HEAD"):
            return await self._history_page(request, session)
        if path == "/api/history/export" and method in ("GET", "HEAD"):
            return await self._history_export(request, session)
        if path == "/api/history/export/progress" and method in ("GET", "HEAD"):
            self._require_perm(session, proto.PERM_DOWNLOAD)
            doc = dict(_EXPORT_PROGRESS)
            doc["ok"] = True
            if doc.get("startedMs"):
                doc["elapsedMs"] = max(
                    0, model_mod.wall_ms() - int(doc["startedMs"]))
            return json_response(doc)
        if path == "/api/history/export/cancel" and method == "POST":
            return await self._history_export_cancel(request, session)
        if path == "/api/log/page" and method in ("GET", "HEAD"):
            return await self._log_page(request, session)
        if path == "/api/log/export" and method in ("GET", "HEAD"):
            return await self._log_export(request, session)

        # ---- 远端配置 ----
        if path == "/api/config":
            if method in ("GET", "HEAD"):
                return await self._config_get(session)
            if method == "POST":
                return await self._config_set(request, session)

        raise HttpError(404, "未知接口 %s %s" % (method, path))

    # ==================================================================
    #                          鉴权与闸门
    # ==================================================================
    def _require_session(self, request):
        token = request.bearer_token()
        session = self.sessions.get(token)
        if session is None:
            raise HttpError(401, "未登录或会话已过期")
        return session

    def _require_ready(self):
        if not self.hub.upstream.is_ready():
            raise HttpError(503, "未连接服务端")
        return True

    def _require_perm(self, session, perm):
        """权限位闸（查表，拒绝给 DENIED_TEXT 原文；服务端仍终审）。"""
        if not session.can(perm):
            raise HttpError(403, perms_mod.denied_text(perm))
        return True

    def _require_view(self, session):
        return self._require_perm(session, proto.PERM_VIEW)

    def _control_gate(self, session):
        """设备控制闸（未连接/自动模式两态/设备控制位；见模块头注 2）。"""
        self._require_ready()
        reason = auth_mod.check_control_gate(session, self.hub.model)
        if reason:
            raise HttpError(403, reason)
        return True

    async def _login(self, request):
        doc = request.json(default={}) or {}
        user = str(doc.get("user", "") or "").strip()
        password = str(doc.get("password", "") or "")
        remote = request.client
        # [限流 2026-11-18] 登录洪泛 ⇒ 每次都要跑 10000 轮 HMAC ⇒ 网页端
        # 被打停（SSE 心跳停发 ⇒ 上游判静默断链 ⇒ 看着像"整个控制台死了"）。
        # ⇒ 洪泛挡在密码学之前（IP 令牌桶 + 账号失败退避）。
        # [只取 IP，不含端口] request.client 是 "ip:port"；端口每次都变 ⇒
        # 按端口分桶等于没分。rsplit 兼顾 IPv6（"::1:1234" → "::1"）。
        source_ip = remote.rsplit(":", 1)[0] if remote else "?"
        try:
            self.login_throttle.check(source_ip, user)
        except auth_mod.LoginError as exc:
            self.log.warn("Web 登录被限流（%s / %s）：%s"
                          % (user or "?", source_ip, exc))
            raise HttpError(429, str(exc))
        try:
            role = await auth_mod.verify_with_upstream(
                self.upstream_host, self.upstream_port, user, password,
                log=self.log.write, timeout=12.0)
        except auth_mod.LoginError as exc:
            self.login_throttle.on_failure(source_ip, user)
            self.log.warn("Web 登录失败（%s）：%s" % (user or "?", exc))
            raise HttpError(401, str(exc))
        self.login_throttle.on_success(source_ip, user)
        session = self.sessions.create(user, role, remote)
        self.log.info("Web 登录成功：%s（%s，来自 %s）"
                      % (user, auth_mod.role_text(role), remote))
        # [惰性连接上游] 首个浏览器登录成功后才连主链路——避免常驻空占
        # root 会话挡住桌面客户端与截图流程。稍等 1s 让服务端回收一次性
        # 探测连接的会话（DenyNew/同账并发关闭时立刻抢连会被瞬时拒绝，
        # 那也有 AuthTransient 退避兜底）。
        loop = asyncio.get_event_loop()
        loop.call_later(1.0, self.hub.ensure_upstream)
        return json_response({
            "ok": True,
            "token": session.token,
            "session": session.to_json(),
            "runMode": self.hub.model.mode_json(),
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
        # [上游不随登出停] 其他浏览器可能还在线；上游链路的启停只跟
        # "有没有任何浏览器会话"走（简化：进程生命周期内保持）。
        return json_response({"ok": True}, headers={
            "Set-Cookie": "hsf_token=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax",
        })

    # ==================================================================
    #                          状态类
    # ==================================================================
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
        stats["serverTimeMs"] = model_mod.wall_ms()
        return stats

    def _serverlog_json(self, request):
        limit = _int_arg(request, "limit", 200, 1, 800)
        level = request.query.get("level") or None
        contains = request.query.get("contains") or None
        return {"ok": True,
                "lines": self.hub.log.tail(limit=limit, level=level,
                                           contains=contains)}

    def _camera_frame_response(self, request, session):
        """相机最新一帧（**image/jpeg**）。

        [谁在存帧] web 进程内存（model.camera.frame）——画面经上游 XLink 分片
        到本机、组帧后存这里；浏览器现取 ⇒ 不必把二进制塞进 SSE。
        [404 而不是 503] 没有帧 = 相机还没出帧（未启用/离线/还在连）⇒ 页面
        据此显示「等待画面」，别弹错误。
        """
        cam = self.hub.model.camera
        if not cam.frame:
            return Response(404, b"", "text/plain; charset=utf-8",
                            {"X-Camera-Reason": "no-frame"})
        headers = {"Cache-Control": "no-store",
                   "X-Camera-Seq": str(cam.seq),
                   "X-Camera-Width": str(cam.width),
                   "X-Camera-Height": str(cam.height)}
        return Response(200, cam.frame, "image/jpeg", headers)

    def _camera_es_response(self, request, session):
        """相机 ES 增量（**二进制**：自 cursor 以来的 NAL）。

        [响应形状] 小端：
            u32 cursor     本响应里最后一条 NAL 的序号（下轮带上）
            u16 codec      1=h264 2=h265
            u16 resync     1=请求的 cursor 已被挤掉 ⇒ 全量并需重置解码器
            u32 count      NAL 条数
            count × { u32 len, len 字节 NAL（Annex-B，含起始码） }
        [404 而不是 503] ES 队列为空 = 相机还没出流 ⇒ 页面显示等待。
        [鉴权] 与 /api/camera/frame 同路（<img>/fetch 自动带 hsf_token
        Cookie；fetch 默认 same-origin 凭据）。
        """
        try:
            cursor = int(request.query.get("cursor") or 0)
        except (TypeError, ValueError):
            cursor = 0
        nals, codec, resync = self.hub.camera_es_since(cursor)
        if not nals and not resync:
            return Response(404, b"", "text/plain; charset=utf-8",
                            {"X-Camera-Reason": "no-es"})
        import struct as _s
        out = [_s.pack("<IHHI", nals[-1][0] if nals else cursor,
                       2 if codec == "h265" else 1,
                       1 if resync else 0, len(nals))]
        for _cur, nal in nals:
            out.append(_s.pack("<I", len(nal)))
            out.append(nal)
        return Response(200, b"".join(out), "application/octet-stream",
                        {"Cache-Control": "no-store",
                         "X-Camera-Codec": codec,
                         "X-Camera-Resync": "1" if resync else "0"})

    def _alarms_json(self, request):
        limit = _int_arg(request, "limit", 200, 1, 500)
        level = request.query.get("level") or None
        kind = request.query.get("kind") or None
        return {"ok": True,
                "alarms": self.hub.alarms.query(limit=limit, level=level,
                                                kind=kind),
                "active": self.hub.alarms.active()}

    # ==================================================================
    #              告警历史分页（0x011A；真源 = 服务端 alarms 表）
    # ==================================================================
    def _alarm_params(self, request):
        """解析报警分页参数（键名与 proto.ALARM_KEY_* 同字面）。

        [与 0x0118/0x0119 同形] offset/limit/beforeTs/loTs/hiTs/kw 六键
        与历史页、日志页完全一致 ⇒ 前端工具条（时间窗/关键字）能直接
        下推服务端，服务端没有数据时回**空页**（rows=[]，不是报错）。
        """
        doc = {}
        doc[proto.ALARM_KEY_OFFSET] = _int_arg(request, "offset", 0, 0,
                                                2 ** 31)
        doc[proto.ALARM_KEY_LIMIT] = _int_arg(
            request, "limit", proto.ALARM_PAGE_DEFAULT,
            1, proto.ALARM_PAGE_MAX)
        before_ts = _int_arg(request, "beforeTs", 0, 0, 2 ** 62)
        if before_ts > 0:
            doc[proto.ALARM_KEY_BEFORE_TS] = before_ts
        lo_ts = _int_arg(request, "loTs", 0, 0, 2 ** 62)
        if lo_ts > 0:
            doc[proto.ALARM_KEY_LO_TS] = lo_ts
        hi_ts = _int_arg(request, "hiTs", 0, 0, 2 ** 62)
        if hi_ts > 0:
            doc[proto.ALARM_KEY_HI_TS] = hi_ts
        kw = (request.query.get("kw") or "").strip()
        if kw:
            doc[proto.ALARM_KEY_KW] = kw
        return doc

    async def _alarms_page(self, request, session):
        """服务端报警历史分页（0x011A 转发；报警页主数据源）。

        [失败不洗白] 上游没有 0x011A（C 端）⇒ 服务端回 ok=false + err ⇒
        `_with_ok` 原样透传 ⇒ 前端回落本地边沿存档（fail-open）。**绝不能**
        把失败洗成「0 行历史」——那会让页面误显空表。
        """
        self._require_view(session)
        self._require_ready()
        doc = self._alarm_params(request)
        result = await self.hub.alarm_page(
            offset=doc[proto.ALARM_KEY_OFFSET],
            limit=doc[proto.ALARM_KEY_LIMIT],
            before_ts=doc.get(proto.ALARM_KEY_BEFORE_TS),
            lo_ts=doc.get(proto.ALARM_KEY_LO_TS),
            hi_ts=doc.get(proto.ALARM_KEY_HI_TS),
            kw=doc.get(proto.ALARM_KEY_KW),
            timeout=8.0)
        return json_response(_with_ok(result))

    def _wifi_json(self):
        """本机无线**只读**快照（hsb_bridge 动态库）。

        [为什么放服务端] 无线会话句柄在**本进程**里（桌面客户端也是这么
        做的，client_py/hsfsrv_client/hsf_native.py），浏览器拿不到句柄 ⇒
        由网页服务器代查，前端只读展示。扫描/连接/断开**不代理**（会话会
        随服务器/浏览器生命周期断），仍走桌面客户端。
        [能力不可用不报错] 动态库没编译（板端 .so 缺失）⇒ 返回
        available=False + err 原因，前端把该区置灰并说明——与参考端
        「能力位不支持 ⇒ 整区置灰」同款口径。
        """
        try:
            from . import hsbnative
            snap = hsbnative.snapshot()
        except Exception as exc:                       # noqa: BLE001
            snap = {"available": False, "adapters": [], "connection": {},
                    "err": "本机能力查询失败：%s" % exc}
        return {"ok": True, **snap}

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
            # 前端按位表做置灰（permissions.js 与本表逐位一致）。
            "roles": {"anonymous": proto.ROLE_ANONYMOUS,
                      "user": proto.ROLE_USER,
                      "admin": proto.ROLE_ADMIN,
                      "operator": proto.ROLE_OPERATOR},
            "perms": {"view": proto.PERM_VIEW,
                      "deviceControl": proto.PERM_DEVICE_CONTROL,
                      "modeSwitch": proto.PERM_MODE_SWITCH,
                      "autoControl": proto.PERM_AUTO_CONTROL,
                      "accountAdmin": proto.PERM_ACCOUNT_ADMIN,
                      "download": proto.PERM_DOWNLOAD},
            "autoModes": [{"id": m, "name": t} for m, t in proto.AUTO_MODES]
                        + [{"id": proto.AUTO_MODE_AUTO, "name": "自动（流量循环）"}],
            # [两份模式列表必须分开 2026-11-18] autoModes = **能跑**（首页
            # 下拉：四模式 + 组合「自动」）；autoConfigModes = **能配**
            # （远端设置配置块：四模式，不含组合「自动」——组合的子模式在
            # 「自动循环配置」块里单独配）。放水模式是普通的可选/可启动
            # 模式 ⇒ 两份列表里都有；它只是**不进组合循环**。
            "autoConfigModes": [{"id": m, "name": t}
                                for m, t in proto.AUTO_CONFIG_MODES],
            "autoDevices": [{"id": d, "name": t} for d, t in proto.AUTO_DEVICES],
        }

    def _version_json(self):
        return {"ok": True, "name": "hsf-webui", "protocol": "XLink v2 (0x0200)",
                "pages": PAGE_COUNT,
                # 前端据此判断"后端换了但浏览器还是旧页面"
                "features": ["sse", "history-proxy", "log-page",
                             "export-csv", "export-xlsx", "alarm-edge",
                             "jog-hold", "lazy-upstream", "auto-modes",
                             "config-rw"]}

    # ==================================================================
    #                          SSE
    # ==================================================================
    def _sse_response(self, request, session):
        hub = self.hub
        sub = hub.register_subscriber(session.user, session.role)
        # [收口幂等] 协程 finally 与 HTTP 层 cleanup 回调都调它（先到先算）。
        cleaned = []

        def _cleanup():
            if cleaned:
                return
            cleaned.append(True)
            hub.unregister_subscriber(sub)

        async def _stream(writer):
            try:
                await httpd.sse_producer(
                    writer,
                    lambda: sub.queue,
                    log=self.log,
                    on_close=_cleanup,
                    # [被淘汰即收口] hub 侧订阅者有硬上限，LRU 淘汰时
                    # 把这个连接一并收掉（否则它还在往上游要主题）。
                    alive_check=lambda: hub.is_subscriber_alive(sub))
            finally:
                _cleanup()

        return Response(200, _stream,
                        "text/event-stream; charset=utf-8",
                        headers={
                            # [首帧就把 connId 告诉前端] 前端要用它调
                            # /api/topics 与 /api/jog/*（一个浏览器会话可能
                            # 开多个标签页，各停在不同页，不能按 token 混算）。
                            "X-Hsf-Conn-Id": str(sub.conn_id),
                        },
                        is_sse=True,
                        # [首帧写失败 ⇒ 协程从未被 await ⇒ 它的 finally
                        #  不执行 ⇒ 订阅者永久残留] ⇒ 清理上提到 HTTP 层
                        # （httpd._handle_client 的 finally 统一收口）。
                        cleanup=_cleanup)

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
    #                  运行模式 / 自动控制（0x0105 / 0x0115）
    # ==================================================================
    async def _set_mode(self, request, session):
        self._require_perm(session, proto.PERM_MODE_SWITCH)
        self._require_ready()
        doc = request.json(default={}) or {}
        mode = _require_int(doc, "mode", default=None)
        if mode is None or mode not in (0, 1):
            raise HttpError(400, "mode 只能是 0（手动）或 1（自动）")
        # [切模式不套自动模式闸] 否则自动模式下永远切不回手动。
        # [同模式重复下发] 服务端幂等（不动设备），原样转发以服务端为准。
        self.log.info("切换运行模式：%s（%s）"
                      % ("自动" if mode else "手动", session.user))
        result = await self.hub.set_run_mode(mode)
        out = _with_ok(result)
        out.update(self.hub.model.mode_json())
        return json_response(out)

    async def _auto_control(self, request, session):
        """自动运行控制：{"op":1..4} 或 {"on":bool,"autoMode"?:id}。

        [为什么复位也在这] op=4（AUTO_OP_RESET）解除急停锁存 + 报警锁存
        但**不启动**；权限同属 PERM_AUTO_CONTROL（运行决策）。
        [为什么不套设备控制闸] 自动控制本身是 0x0115，模式闸对它无意义
        （启动自动运行的前提就是已处自动模式——那是服务端的事）。
        """
        self._require_perm(session, proto.PERM_AUTO_CONTROL)
        self._require_ready()
        doc = request.json(default={}) or {}
        payload = {}
        if "op" in doc:
            op = _require_int(doc, "op", default=None)
            if op not in (proto.AUTO_OP_START, proto.AUTO_OP_STOP,
                          proto.AUTO_OP_ESTOP, proto.AUTO_OP_RESET,
                          proto.AUTO_OP_SELECT):
                raise HttpError(400, "op 只能是 1（启动）/2（停止）/3（急停）"
                                     "/4（复位）/5（只选模式）")
            payload["op"] = op
            if op in (proto.AUTO_OP_START, proto.AUTO_OP_SELECT):
                # 启动带 op 时也可捎带模式（与 on 同口径）；op=5「只选模式」
                # **必须**带 autoMode（服务端靠它记粘性）。
                mode = self._auto_mode_of(doc)
                if mode:
                    payload[proto.KEY_AUTO_MODE] = mode
                if op == proto.AUTO_OP_SELECT and not mode:
                    raise HttpError(400, "op=5（只选模式）必须带 autoMode")
        elif "on" in doc:
            payload["on"] = _require_bool(doc.get("on"))
            if payload["on"]:
                mode = self._auto_mode_of(doc)
                if mode:
                    payload[proto.KEY_AUTO_MODE] = mode
        else:
            raise HttpError(400, "载荷需要 op（1..5）或 on（bool）")
        self.log.info("自动运行控制：%s（%s）"
                      % (json.dumps(payload, ensure_ascii=False), session.user))
        result = await self.hub.send_auto_control(payload)
        out = _with_ok(result)
        # 应答透传服务端全量状态键（拒绝也回全量状态键——两端口径）。
        out.update({k: v for k, v in self.hub.model.mode_json().items()
                    if k not in out})
        return json_response(out)

    def _auto_mode_of(self, doc):
        """载荷里的可选 autoMode（协议稳定 id；非法值 fail-closed 拒收）。"""
        mode = doc.get(proto.KEY_AUTO_MODE)
        if mode in (None, ""):
            return ""
        mode = str(mode)
        if mode not in proto.AUTO_MODE_IDS:
            raise HttpError(400, "autoMode 必须是 %s 之一"
                            % "/".join(proto.AUTO_MODE_IDS))
        return mode

    # ==================================================================
    #                    变频器控制 / 点动（0x0101）
    # ==================================================================
    async def _vfd_control(self, request, session):
        """变频器控制：op/hz/rpm/rev/val/idx。

        [点动走独立端点] op=JOG/JOG_RELEASE 由 `/api/jog/*` 代持，
        不接受从这里直发 —— 否则浏览器切后台被节流时点动会自己停。
        [给定捎带] 服务端拒绝停机时的独立给定：给定（hz/rpm）必须在
        **启动帧**里捎带（客户端 pages_monitor 同口径），这里不拦——
        服务端闸会拒并回原因。
        """
        self._control_gate(session)
        self._require_perm(session, proto.PERM_DEVICE_CONTROL)
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
                            ("rev", _as_bool), ("idx", int)):
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
        self._require_view(session)
        self._require_ready()
        try:
            result = await self.hub.request(proto.CMD_VFD_SNAPSHOT, None,
                                            timeout=SNAPSHOT_TIMEOUT_S)
        except TimeoutError:
            cached = self._cached_vfd_json()
            if cached is None:
                raise
            return json_response(cached)
        if result and result.get(proto.KEY_OK) is not False:
            self.hub.model.vfd.apply(result)
            self.hub.broadcast("vfd", self.hub.model.vfd.to_json())
        return json_response(_with_ok(result))

    # ==================================================================
    #              变频器参数（0x011B；读=登录，写=管理员）
    # ==================================================================
    async def _vfd_params_get(self, session):
        """读变频器参数整组（0x011B op=0）。

        [读=登录即可] 只读面（PERM_VIEW）——与桌面客户端同款；写才要
        管理员（PERM_ACCOUNT_ADMIN，照 0x0117 config_set 同款闸）。
        [失败不洗白] 上游没有 0x011B（C 端）⇒ 服务端回 ok=false + err ⇒
        `_with_ok` 原样透传 ⇒ 前端把原因显示在状态行（不假成功）。
        """
        self._require_view(session)
        self._require_ready()
        result = await self.hub.vfd_params(op=proto.VFD_PARAM_OP_READ,
                                           timeout=VFD_PARAMS_TIMEOUT_S)
        return json_response(_with_ok(result))

    async def _vfd_params_write(self, request, session):
        """写变频器参数（0x011B op=1；管理员）。

        [载荷形状] {"op":1, "values":{"<地址十进制串>": raw}}——地址是
        **十进制字符串**（BSON 文档键只能是字符串，与服务端 int(key)
        归一口径一致）；值是**原始寄存器码**（工程值→raw 的换算在前端
        按 PARAM_DEFS 的 scale 做，与桌面客户端同款）。

        [fail-closed 载荷校验] 空 values / 非法地址 / 非整数值一律**显式
        400**，绝不把拼错的键悄悄变成「没写」或半截写。

        [RunLocked 闸在服务端] 变频器运行中写参数由服务端终审拒绝（err =
        「变频器运行中禁止写参数（请先停止）」，三端同款文案）⇒ web 只
        透传，不改写。
        """
        self._require_perm(session, proto.PERM_ACCOUNT_ADMIN)
        self._require_ready()
        body = request.json(default={}) or {}
        try:
            op = int(body.get(proto.VFD_PARAMS_KEY_OP))
        except (TypeError, ValueError):
            raise HttpError(400, "缺少 op 字段（写参数用 op=1）")
        if op != proto.VFD_PARAM_OP_WRITE:
            raise HttpError(400, "op 非法：读参数请用 GET /api/vfd/params")
        raw_values = body.get(proto.VFD_PARAMS_KEY_VALUES)
        if not isinstance(raw_values, dict) or not raw_values:
            raise HttpError(400, "values 不能为空（没有要写的参数）")
        values = {}
        for key, val in raw_values.items():
            try:
                addr = int(str(key).strip())
                raw = int(val)
            except (TypeError, ValueError):
                raise HttpError(400, "values 键必须是十进制地址、值必须是整数")
            if addr < 0:
                raise HttpError(400, "values 地址不能为负")
            values[addr] = raw
        self.log.info("变频器参数写入（%s）：%d 项"
                      % (session.user, len(values)))
        result = await self.hub.vfd_params(op=proto.VFD_PARAM_OP_WRITE,
                                           values=values,
                                           timeout=VFD_PARAMS_WRITE_TIMEOUT_S)
        return json_response(_with_ok(result))

    async def _jog_start(self, request, session):
        """按住点动开始（网页服务器代持 200ms 重发，捎带按下那一刻的给定）。"""
        self._control_gate(session)
        self._require_perm(session, proto.PERM_DEVICE_CONTROL)
        doc = request.json(default={}) or {}
        reverse = _as_bool(doc.get("rev", False))
        given = {}
        if "hz" in doc:
            given["hz"] = float(doc["hz"])
        if "rpm" in doc:
            given["rpm"] = float(doc["rpm"])
        # 找到调用者的 SSE 连接（用于"页面断开就释放"）。
        conn_id = doc.get("connId")
        try:
            conn_id = int(conn_id) if conn_id is not None else -1
        except (TypeError, ValueError):
            conn_id = -1
        await self.hub.start_jog(reverse, conn_id, session.user, given)
        return json_response({"ok": True, "active": True,
                              "reverse": reverse,
                              "jogRepeatMs": proto.JOG_REPEAT_MS,
                              "jogMaxMs": _jog_max_ms()})

    async def _jog_stop(self, session):
        """松开点动 → 立刻发释放。

        [为什么不设闸] 这是**安全**动作（停止类永远放行）；自动模式下
        也不该拦"松手即停"。
        """
        await self.hub.stop_jog(reason="操作员松开")
        return json_response({"ok": True, "active": False})

    # ==================================================================
    #                    数字量（0x0108 / 0x0109）
    # ==================================================================
    async def _digital_control(self, request, session):
        """DO 控制：单点 {dev,do,on} 或整组 {dev,mask,values}。"""
        self._control_gate(session)
        self._require_perm(session, proto.PERM_DEVICE_CONTROL)
        doc = request.json(default={}) or {}
        dev = self._clamp_dev(_require_int(doc, "dev", default=0))
        payload = {"dev": dev}
        if "mask" in doc:
            payload["mask"] = _require_int(doc, "mask", default=0)
            if "values" in doc:
                payload["values"] = _require_int(doc, "values", default=0)
        elif "do" in doc:
            payload["do"] = _require_int(doc, "do", default=0)
            payload["on"] = _require_bool(doc.get("on", False))
        else:
            raise HttpError(400, "缺 do 或 mask 字段")
        self.log.info("DO 控制：模块%d %s（%s）"
                      % (dev, payload, session.user))
        result = await self.hub.request(proto.CMD_DIGITAL_CONTROL, payload,
                                        timeout=8.0)
        if result and result.get(proto.KEY_OK):
            # 用服务端回的 do 位图同步本地模型（不等下一拍推送）。
            dev_obj = self.hub.model.digital_state(dev)
            if dev_obj is not None:
                dev_obj.do = _int_or(result.get(proto.KEY_DO, dev_obj.do),
                                     dev_obj.do)
                self.hub.broadcast("digital", dev_obj.to_json())
        return json_response(_with_ok(result))

    def _clamp_dev(self, dev):
        """把请求里的数字量模块号夹到合法范围（0..N-1）。

        [为什么必须夹] 请求里的 dev 原本**原样透传上游**，而 web 侧的
        `model.digital_state(dev)` 会**为任意 dev 新建并永久保存**一个
        DigitalState ⇒ **最低权限账号（只读位）循环 POST 不同 dev 就能
        让服务端内存无界增长**，/api/state 首帧 JSON 一起膨胀。
        ⇒ 入口处夹住（控制类也绝不把野值发给上游）。

        :param dev: 请求里的模块号（可能越界/为负）
        :returns: 合法模块号（0..N-1；N = 实际模块数，至少 1）
        """
        try:
            dev = int(dev)
        except (TypeError, ValueError):
            dev = 0
        count = max(1, self.hub.model.digital_module_count())
        if dev < 0:
            clamped = 0
        elif dev >= count:
            clamped = count - 1
        else:
            clamped = dev
        if clamped != dev:
            # 越界请求如实留痕（不静默——否则现场查"我点了没反应"没线索）。
            self.log.warn("数字量模块号越界：请求 %s ⇒ 按 %d 处理（合法 0..%d）"
                          % (dev, clamped, count - 1))
        return clamped

    async def _digital_snapshot(self, request, session):
        self._require_view(session)
        self._require_ready()
        doc = request.json(default={}) or {}
        dev = self._clamp_dev(_require_int(doc, "dev", default=0))
        try:
            result = await self.hub.request(proto.CMD_DIGITAL_SNAPSHOT,
                                            {"dev": dev},
                                            timeout=SNAPSHOT_TIMEOUT_S)
        except TimeoutError:
            # 现读超时 ⇒ 回落**推送态**（同桌面客户端的数据源，见 _cached_*）。
            cached = self._cached_digital_json(dev)
            if cached is None:
                raise
            return json_response(cached)
        if result and result.get(proto.KEY_OK) is not False:
            # [不夹服务端回的 dev] 回包里带的模块号若超出本机模型的合法
            # 范围 ⇒ 只应答、**不落模**（夹到别的模块上会把数据写错地方）。
            got_dev = _int_or(result.get(proto.KEY_DEV, dev), dev)
            dev_obj = self.hub.model.digital_state(got_dev)
            if dev_obj is not None:
                dev_obj.apply(result)
                self.hub.broadcast("digital", dev_obj.to_json())
        return json_response(_with_ok(result))

    # ==================================================================
    #        现读失败时的回落（推送态）——与桌面客户端同源同值
    # ==================================================================
    #
    # [为什么回落] 快照端点是「现读设备」；上游（服务端）忙/卡时会在超时
    # 内不答复。此时**直接报错**会让调用方拿到空包（界面显示不出状态，
    # 自动化脚本也判失败）。桌面客户端的阀/泵/数字量页读的就是**推送态**
    # （TOPIC_* 每秒一拍），现读只是「立刻再要一次」⇒ 现读超时时回落到
    # 推送态：**同一个上游、同一个值**，不是编造。
    # [只在推送态还新鲜时回落] 推送态过期（超过 staleMs）就照旧报错，
    # 不拿陈旧值糊弄界面/调用方。回落包带 "cached": true 供界面标注。

    def _cached_digital_json(self, dev):
        """数字量模块的推送态（新鲜才给；否则 None）。"""
        state = self.hub.model.digital_state(dev)
        if state is None or not state.is_fresh():
            return None
        return {"ok": True, "cached": True, "count": state.count,
                "di": state.di, "do": state.do, "valid": state.valid,
                "name": state.name}

    def _cached_valve_json(self):
        """阀的推送态（新鲜才给；否则 None）。"""
        valve = self.hub.model.valve
        if not valve.is_fresh():
            return None
        doc = {"ok": True, "cached": True, "count": valve.count}
        for index, item in valve.items.items():
            for key, value in item.items():
                doc["v%d_%s" % (index, key)] = value
        return doc

    def _cached_pump_json(self):
        """进水泵的推送态（新鲜才给；否则 None）。"""
        pump = self.hub.model.pump
        if not pump.is_fresh():
            return None
        doc = {"ok": True, "cached": True, "count": pump.count}
        for index, item in pump.items.items():
            for key, value in item.items():
                doc["v%d_%s" % (index, key)] = value
        return doc

    def _cached_vfd_json(self):
        """变频器的推送态（新鲜才给；否则 None）。

        [键形] 变频器快照的键是**平铺**的（running/setFreqHz/outputFreqHz…），
        与模型 to_json 同款 ⇒ 直接摊开即可（多带 kind/fresh/ageMs 无害）。
        """
        vfd = self.hub.model.vfd
        if not vfd.is_fresh():
            return None
        doc = dict(vfd.to_json())
        doc["ok"] = True
        doc["cached"] = True
        return doc

    # ==================================================================
    #                    电动球阀（0x0110 / 0x0111）
    # ==================================================================
    async def _valve_control(self, request, session):
        self._control_gate(session)
        self._require_perm(session, proto.PERM_DEVICE_CONTROL)
        doc = request.json(default={}) or {}
        index = _require_int(doc, "index", default=None)
        if index is None or not 0 <= index < proto.HSF_VALVE_COUNT:
            raise HttpError(400, "index 必须是阀号 0~%d"
                            % (proto.HSF_VALVE_COUNT - 1))
        if "on" not in doc:
            raise HttpError(400, "缺 on 字段（true=开 / false=关）")
        payload = {"index": index, "on": _require_bool(doc.get("on"))}
        self.log.info("阀控制：%s（%s）"
                      % ("开阀" if payload["on"] else "关阀", session.user))
        result = await self.hub.request(proto.CMD_VALVE_CONTROL, payload,
                                        timeout=12.0)
        if result:
            ok = bool(result.get(proto.KEY_OK))
            # [处置结果给界面] 服务端在应答里带回 v{i}_cmdResult
            # （issued/repeated/reversed/ignored）——记录到模型，状态行
            # 能区分"动了"和"本来就在那儿"。
            cmd_result = result.get("v%d_%s" % (index,
                                                proto.KEY_VALVE_CMD_RESULT))
            if cmd_result:
                self.hub.model.valve.note_cmd(
                    index, payload["on"], cmd_result, ok,
                    result.get(proto.KEY_ERR, ""))
            if proto.KEY_VALVE_COUNT in result:
                # 服务端在应答里带回一整拍状态（不必再拉快照）。
                self.hub.model.valve.apply(result)
            self.hub.broadcast("valve", self.hub.model.valve.to_json())
        return json_response(_with_ok(result))

    async def _valve_snapshot(self, session):
        self._require_view(session)
        self._require_ready()
        try:
            result = await self.hub.request(proto.CMD_VALVE_SNAPSHOT, None,
                                            timeout=SNAPSHOT_TIMEOUT_S)
        except TimeoutError:
            cached = self._cached_valve_json()
            if cached is None:
                raise
            return json_response(cached)
        if result and proto.KEY_VALVE_COUNT in result:
            self.hub.model.valve.apply(result)
            self.hub.broadcast("valve", self.hub.model.valve.to_json())
        return json_response(_with_ok(result))

    # ==================================================================
    #                    进水泵（0x0113 / 0x0114）
    # ==================================================================
    async def _pump_control(self, request, session):
        self._control_gate(session)
        self._require_perm(session, proto.PERM_DEVICE_CONTROL)
        doc = request.json(default={}) or {}
        index = _require_int(doc, "index", default=0)
        if "on" not in doc:
            raise HttpError(400, "缺 on 字段（true=启动 / false=停止）")
        payload = {"index": index, "on": _require_bool(doc.get("on"))}
        self.log.info("泵控制：%s（%s）"
                      % ("启动" if payload["on"] else "停止", session.user))
        result = await self.hub.request(proto.CMD_PUMP_CONTROL, payload,
                                        timeout=8.0)
        if result and proto.KEY_COUNT in result:
            # 应答整帧回显直接落地（与客户端同口径）。
            self.hub.model.pump.apply(result)
            self.hub.broadcast("pump", self.hub.model.pump.to_json())
        return json_response(_with_ok(result))

    async def _pump_snapshot(self, session):
        self._require_view(session)
        self._require_ready()
        try:
            result = await self.hub.request(proto.CMD_PUMP_SNAPSHOT, None,
                                            timeout=SNAPSHOT_TIMEOUT_S)
        except TimeoutError:
            cached = self._cached_pump_json()
            if cached is None:
                raise
            return json_response(cached)
        if result and proto.KEY_COUNT in result:
            self.hub.model.pump.apply(result)
            self.hub.broadcast("pump", self.hub.model.pump.to_json())
        return json_response(_with_ok(result))

    # ==================================================================
    #          浊度 / 流量计 / 温湿度快照（0x0112 / 0x0106 / 0x0104）
    # ==================================================================
    async def _turbidity_snapshot(self, session):
        self._require_view(session)
        self._require_ready()
        result = await self.hub.request(proto.CMD_TURBIDITY_SNAPSHOT, None,
                                        timeout=8.0)
        if result and proto.KEY_COUNT in result:
            self.hub.model.turbidity.apply(result)
            self.hub.broadcast("turbidity", self.hub.model.turbidity.to_json())
        return json_response(_with_ok(result))

    async def _flowmeter_snapshot(self, session):
        self._require_view(session)
        self._require_ready()
        result = await self.hub.request(proto.CMD_FLOWMETER_SNAPSHOT, None,
                                        timeout=8.0)
        if result and result.get(proto.KEY_OK) is not False:
            self.hub.model.flowmeter.apply(result)
            self.hub.broadcast("flowmeter", self.hub.model.flowmeter.to_json())
        return json_response(_with_ok(result))

    async def _temphum_snapshot(self, request, session):
        self._require_view(session)
        self._require_ready()
        doc = request.json(default={}) or {}
        payload = None
        if "side" in doc:
            payload = {"side": _require_int(doc, "side", default=0)}
        result = await self.hub.request(proto.CMD_TEMPHUM_SNAPSHOT, payload,
                                        timeout=8.0)
        if result and result.get(proto.KEY_OK) is not False:
            self.hub.model.temphum.apply(result)
            self.hub.broadcast("temphum", self.hub.model.temphum.to_json())
        return json_response(_with_ok(result))

    # ==================================================================
    #              历史分页 / 导出（0x0118；真源 = 服务端 SQLite）
    # ==================================================================
    def _history_params(self, request, for_export):
        """解析历史查询参数（键名与 proto.HIST_KEY_* 同字面）。"""
        doc = {}
        limit = _int_arg(request, "limit", proto.HISTORY_PAGE_DEFAULT,
                         proto.HISTORY_PAGE_MIN, proto.HISTORY_PAGE_MAX)
        offset = _int_arg(request, "offset", 0, 0, 2 ** 31)
        doc[proto.HIST_KEY_OFFSET] = offset
        doc[proto.HIST_KEY_LIMIT] = limit
        before_ts = _int_arg(request, "beforeTs", 0, 0, 2 ** 62)
        if before_ts > 0:
            doc[proto.HIST_KEY_BEFORE_TS] = before_ts
        lo_ts = _int_arg(request, "loTs", 0, 0, 2 ** 62)
        if lo_ts > 0:
            doc[proto.HIST_KEY_LO_TS] = lo_ts
        hi_ts = _int_arg(request, "hiTs", 0, 0, 2 ** 62)
        if hi_ts > 0:
            doc[proto.HIST_KEY_HI_TS] = hi_ts
        kw = (request.query.get("kw") or "").strip()
        if kw:
            doc[proto.HIST_KEY_KW] = kw
        kw_cols = request.query.get("kwCols")
        if kw_cols:
            cols = []
            for part in kw_cols.split(","):
                try:
                    cols.append(int(part.strip()))
                except (TypeError, ValueError):
                    continue
            if cols:
                doc[proto.HIST_KEY_KW_COLS] = cols
        if for_export:
            doc[proto.HIST_KEY_EXPORT] = True
        return doc

    async def _history_page(self, request, session):
        export_flag = (request.query.get("export") or "").lower() in \
            ("1", "true", "yes")
        if export_flag:
            self._require_perm(session, proto.PERM_DOWNLOAD)
        else:
            self._require_view(session)
        self._require_ready()
        doc = self._history_params(request, for_export=export_flag)
        result = await self.hub.request(proto.CMD_HISTORY_PAGE, doc,
                                        timeout=8.0)
        return json_response(_with_ok(result))

    async def _history_export(self, request, session):
        """历史导出：按筛选全量批取（整轮钉同一 beforeTs 锚点）。

        [筛选口径] 与列表页同一条 WHERE：loTs/hiTs（时间）+ kw/kwCols
        （关键字只在勾选列里匹配）。[2026-11-22 列筛选导出] `outCols` =
        要写进文件的显示列下标（空 = 全部 16 列）：表头与每行文本都按它
        裁剪/排序 ⇒ 「列」多选既是关键字范围，也是导出列范围。
        [取消] `job` 参数 + POST /api/history/export/cancel 置位；批取循环
        每批检查一次（XLSX 在写盘阶段中断、CSV 已发部分追加说明行）。
        [两种格式都是流式] CSV 与 XLSX 都按 chunked 边生成边发（不限行数）。
        [探测批先行] 先取第一批确认链路/权限/总数，失败在响应头之前回
        JSON 错误（不让浏览器收到半个坏文件才发现失败）。
        """
        self._require_perm(session, proto.PERM_DOWNLOAD)
        self._require_ready()
        fmt = _export_format(request)
        if fmt is None:
            raise HttpError(400, "format 只支持 csv|xlsx")
        job = (request.query.get("job") or "").strip()[:64]
        out_cols = _parse_index_list(request.query.get("outCols"),
                                     len(export_mod.HISTORY_HEADERS))
        headers = (tuple(export_mod.HISTORY_HEADERS[i] for i in out_cols)
                   if out_cols else export_mod.HISTORY_HEADERS)
        _EXPORT_PROGRESS.update({
            "job": job, "active": True, "format": fmt, "rows": 0,
            "bytes": 0, "startedMs": model_mod.wall_ms(), "phase": "fetch",
            "error": "",
        })
        if job:
            _EXPORT_CANCEL.discard(job)
        # [锚点定格] 导出发起那一刻定一次；每批原样回传（<=0 时取"现在"）。
        anchor = _int_arg(request, "beforeTs", 0, 0, 2 ** 62) \
            or model_mod.wall_ms()

        # [keyset 分页 2026-11-17 与桌面客户端同口径] 原来每批 `offset`
        # 递增 + 固定 anchor ⇒ 服务端 `LIMIT ? OFFSET ?` 的 OFFSET 每批都要
        # 索引从头扫（20 万行实测末尾 753ms/批）⇒ 总成本 O(N²)。改成把
        # `beforeTs` 推进到"上一批最旧那行的 ts - 1"（历史 ts 是主键、唯一
        # ⇒ 等价严格 `<`），offset 恒 0。配合服务端「导出模式不数总数」
        # （total=-1）⇒ 20 万行关键字导出从 ~66 分钟降到 ~12 秒。
        keyset = {"before": anchor}

        def _batch_doc(limit):
            doc = self._history_params(request, for_export=True)
            doc[proto.HIST_KEY_OFFSET] = 0
            doc[proto.HIST_KEY_LIMIT] = limit
            doc[proto.HIST_KEY_BEFORE_TS] = keyset["before"]
            return doc

        async def fetch_batch(offset, limit):
            if job and job in _EXPORT_CANCEL:
                raise _ExportCancelled("用户取消")
            doc = await self.hub.request(proto.CMD_HISTORY_PAGE,
                                         _batch_doc(limit), timeout=10.0)
            try:
                rows = doc.get(proto.HIST_KEY_ROWS) or []
                if rows:
                    keyset["before"] = min(int(r[0]) for r in rows) - 1
                    if not job or _EXPORT_PROGRESS.get("job") == job:
                        _EXPORT_PROGRESS["rows"] += len(rows)
            except (TypeError, ValueError, IndexError):
                pass
            return doc

        # ---- 探测批（失败在发头之前回 JSON 错误）----
        first = await fetch_batch(0, export_mod.EXPORT_CHUNK)
        if not isinstance(first, dict) or not first.get(proto.KEY_OK):
            err = first.get("err") if isinstance(first, dict) else "服务端无应答"
            _EXPORT_PROGRESS.update({"active": False, "error": str(err or "")})
            raise HttpError(502, "导出失败：%s" % (err or "服务端拒绝"))

        def _rows_to_texts(row):
            texts = export_mod.history_row_texts(
                row, first.get("cols") or proto.HISTORY_COLUMNS)
            if not out_cols:
                return texts
            if isinstance(texts, tuple):
                return ([texts[0][i] for i in out_cols], texts[1])
            return [texts[i] for i in out_cols]

        gen = _guarded_export(
            export_mod.export_stream(
                fetch_batch, headers, _rows_to_texts,
                fmt, sheet_name="历史数据", first_doc=first),
            fmt)

        async def _tracked(inner):
            """进度收口：字节数、active/phase；取消标记一律清理。"""
            try:
                async for chunk in inner:
                    _EXPORT_PROGRESS["bytes"] += len(chunk)
                    yield chunk
            except _ExportCancelled:
                _EXPORT_PROGRESS["error"] = "已取消"
                raise
            except BaseException:
                _EXPORT_PROGRESS["error"] = "导出中断"
                raise
            finally:
                if not job or _EXPORT_PROGRESS.get("job") == job:
                    _EXPORT_PROGRESS["active"] = False
                    _EXPORT_PROGRESS["phase"] = "done"
                if job:
                    _EXPORT_CANCEL.discard(job)

        gen = _tracked(gen)
        filename = export_mod.export_filename("hsf_history", fmt)
        if fmt == "xlsx":
            # [流式发] 生成器按 256KB 分块 yield ⇒ 直接 chunked 发，
            # 不在内存里拼成一整块（峰值曾 ≈ 2× 文件大小）。
            return stream_response(
                gen, XLSX_CONTENT_TYPE,
                headers={
                    "Content-Disposition":
                        'attachment; filename="%s"' % filename,
                    "Cache-Control": "no-store",
                })
        return stream_response(
            gen, "text/csv; charset=utf-8",
            headers={
                "Content-Disposition": 'attachment; filename="%s"' % filename,
            })

    async def _history_export_cancel(self, request, session):
        """取消正在跑的历史导出（幂等：job 不在跑也回 ok）。

        [为什么需要服务端标记] 前端 abort fetch 只能断 HTTP；XLSX 的耗时在
        「写临时文件」阶段（还没开始发字节）⇒ 只靠断链停不住服务端的批取。
        批取循环每批查一次本表 ⇒ 下一批立刻收手。
        """
        self._require_perm(session, proto.PERM_DOWNLOAD)
        doc = request.json(default={}) or {}
        job = str(doc.get("job") or "").strip()[:64]
        if not job:
            raise HttpError(400, "缺少 job")
        _EXPORT_CANCEL.add(job)
        return json_response({"ok": True, "job": job})

    # ==================================================================
    #              运行日志分页 / 导出（0x0119；权限闸在 web）
    # ==================================================================
    def _log_params(self, request):
        doc = {}
        doc[proto.KEY_LOG_OFFSET] = _int_arg(request, "offset", 0, 0, 2 ** 31)
        doc[proto.KEY_LOG_LIMIT] = _int_arg(
            request, "limit", proto.LOG_PAGE_DEFAULT,
            proto.HISTORY_PAGE_MIN, proto.LOG_PAGE_MAX)
        before_ts = _int_arg(request, "beforeTs", 0, 0, 2 ** 62)
        if before_ts > 0:
            doc[proto.KEY_LOG_BEFORE_TS] = before_ts
        lo_ts = _int_arg(request, "loTs", 0, 0, 2 ** 62)
        if lo_ts > 0:
            doc[proto.KEY_LOG_LO_TS] = lo_ts
        hi_ts = _int_arg(request, "hiTs", 0, 0, 2 ** 62)
        if hi_ts > 0:
            doc[proto.KEY_LOG_HI_TS] = hi_ts
        category = (request.query.get("category") or "").strip()
        if category:
            doc[proto.KEY_LOG_CATEGORY] = category
        kw = (request.query.get("kw") or "").strip()
        if kw:
            doc[proto.KEY_LOG_KW] = kw
        return doc

    async def _log_page(self, request, session):
        self._require_view(session)
        self._require_ready()
        result = await self.hub.request(proto.CMD_LOG_PAGE,
                                        self._log_params(request),
                                        timeout=8.0)
        return json_response(_with_ok(result))

    async def _log_export(self, request, session):
        """日志导出：CSV（utf-8 BOM+CRLF）/ XLSX（流式工作簿）。

        [权限闸在 web] 0x0119 没有 export 键可验 ⇒ 下载权限只能由网页
        这道闸把守（与桌面客户端 LogPage._export_data 同款：普通用户及
        以上可导出，更低角色 403 + DENIED_TEXT 原文）。
        [两种形态同一套参数] format=csv|xlsx；其余参数（类别/关键字/时间
        窗/锚点）与 /api/log/page 完全一致 ⇒ 导出内容 = 界面筛选命中的
        **全量**（不是当前页那几十行）。
        [XLSX 也能流式] 与历史导出同款：export_stream 先写临时文件再分块
        回读 ⇒ 行数不限、内存恒定、事件循环不被卡住。
        """
        self._require_perm(session, proto.PERM_DOWNLOAD)
        self._require_ready()
        fmt = _export_format(request)
        if fmt is None:
            raise HttpError(400, "format 只支持 csv|xlsx")
        # [锚点] 缺省/0 = 服务端取"现在"，但那样翻页窗口会跟着走 ⇒
        # 发包前定格本地"现在"再发送（客户端 logRequest 同口径）。
        anchor = _int_arg(request, "beforeTs", 0, 0, 2 ** 62) \
            or model_mod.wall_ms()

        def _batch_doc(offset, limit):
            doc = self._log_params(request)
            doc[proto.KEY_LOG_OFFSET] = offset
            doc[proto.KEY_LOG_LIMIT] = limit
            doc[proto.KEY_LOG_BEFORE_TS] = anchor
            # [导出模式不数总数 2026-11-17 口径] 服务端 0x0119 认
            # export 键 ⇒ 回 total=-1（免得每批都 COUNT 一遍全表）。
            doc[proto.HIST_KEY_EXPORT] = True
            return doc

        async def fetch_batch(offset, limit):
            return await self.hub.request(proto.CMD_LOG_PAGE,
                                          _batch_doc(offset, limit),
                                          timeout=10.0)

        first = await fetch_batch(0, export_mod.EXPORT_CHUNK)
        if not isinstance(first, dict) or not first.get(proto.KEY_OK):
            err = first.get("err") if isinstance(first, dict) else "服务端无应答"
            raise HttpError(502, "导出失败：%s" % (err or "服务端拒绝"))

        gen = _guarded_export(
            export_mod.export_stream(fetch_batch, export_mod.LOG_HEADERS,
                                     export_mod.log_row_texts, fmt,
                                     sheet_name="日志", first_doc=first),
            fmt)
        filename = export_mod.export_filename("hsf_log", fmt)
        if fmt == "xlsx":
            return stream_response(
                gen, XLSX_CONTENT_TYPE,
                headers={
                    "Content-Disposition":
                        'attachment; filename="%s"' % filename,
                    "Cache-Control": "no-store",
                })
        return stream_response(
            gen, "text/csv; charset=utf-8",
            headers={
                "Content-Disposition": 'attachment; filename="%s"' % filename,
            })

    # ==================================================================
    #             报警历史导出（0x011A；csv/xlsx；权限闸 PERM_DOWNLOAD）
    # ==================================================================
    async def _alarm_export(self, request, session):
        """报警导出：按筛选全量（CSV / XLSX），数据源 = 服务端 alarms 表。

        [数据源单一] 0x011A（与报警页同一份事实源）⇒ 导出内容与桌面
        客户端报警页同源；老服务端（C 端）没有 0x011A ⇒ 回**显式失败
        包络**（前端据此回落本机边沿存档导出；绝不洗白成"0 行成功"）。

        [筛选口径 = 报警页界面同一套]
            · **类型**（kindText 参数）：类型列文案（设备类别中文 /
              「自动运行」；空 = 全部）；
            · **时间窗**（loTs/hiTs）：下推服务端（0x011A 原生支持）；
            · **关键字**（kw）：设备/类型/状态三列文本包含——与界面
              `rowVisible` 同口径。**不下推服务端**：0x011A 的 kw 是
              text/source/kind 的 LIKE，与界面口径不同（下推会漏掉
              "类型列命中"的行，例如关键字「变频器」）。

        [批取流] 批 40、整轮钉同一 beforeTs 锚点、服务端 offset 连续
        递增（服务端 ts DESC/rowid DESC 稳定 ⇒ 不重不漏）；类型与关键字
        在**映射成显示行之后**过滤 ⇒ 导出行 = 界面筛选命中的行。
        """
        self._require_perm(session, proto.PERM_DOWNLOAD)
        self._require_ready()
        fmt = _export_format(request)
        if fmt is None:
            raise HttpError(400, "format 只支持 csv|xlsx")
        # [锚点] 缺省/0 = 服务端取"现在"，那样翻页窗口会跟着走 ⇒ 发包前
        # 定格本地"现在"（与历史/日志导出同口径）。
        anchor = _int_arg(request, "beforeTs", 0, 0, 2 ** 62) \
            or model_mod.wall_ms()
        kind_text = (request.query.get("kindText") or "").strip()
        kw = (request.query.get("kw") or "").strip()
        lo_ms = _int_arg(request, "loTs", 0, 0, 2 ** 62)
        hi_ms = _int_arg(request, "hiTs", 0, 0, 2 ** 62)
        cols = _alarm_export_cols(request)
        headers = tuple(ALARM_HEADERS[i] for i in cols)

        def _batch_doc(offset, limit):
            doc = {
                proto.ALARM_KEY_OFFSET: int(offset),
                proto.ALARM_KEY_LIMIT: int(limit),
                proto.ALARM_KEY_BEFORE_TS: int(anchor),
                # 导出模式：服务端不数总数（免得每批 COUNT 一遍）。
                proto.HIST_KEY_EXPORT: True,
            }
            if lo_ms > 0:
                doc[proto.ALARM_KEY_LO_TS] = int(lo_ms)
            if hi_ms > 0:
                doc[proto.ALARM_KEY_HI_TS] = int(hi_ms)
            return doc

        state = {"offset": 0, "done": False, "pending": []}

        async def fetch_batch(offset, limit):
            """取一批**过滤后**的行（export_stream 按 limit 连续取）。

            [为什么自己维护服务端游标] export_stream 传进来的 offset 是
            "已产出的行数"（过滤后的口径），与服务端 offset 不是一回事
            ⇒ 服务端游标自己记（与历史导出的 keyset 同思路）。
            [短批 = 取完] 服务端返回的行数少于批大小 ⇒ 没有更多了 ⇒
            本批可以短（export_stream 以短批作为结束判据）。
            """
            while len(state["pending"]) < limit and not state["done"]:
                doc = await self.hub.request(
                    proto.CMD_ALARM_PAGE,
                    _batch_doc(state["offset"], export_mod.EXPORT_CHUNK),
                    timeout=10.0)
                if not isinstance(doc, dict) or not doc.get(proto.KEY_OK):
                    err = (doc.get("err") if isinstance(doc, dict)
                           else "服务端无应答")
                    raise HttpError(502, _alarm_export_error(err))
                rows = doc.get(proto.ALARM_KEY_ROWS) or []
                state["offset"] += len(rows)
                if len(rows) < export_mod.EXPORT_CHUNK:
                    state["done"] = True
                for row in rows:
                    if not isinstance(row, dict):
                        continue
                    texts = alarm_row_texts(row)
                    if alarm_row_visible(texts, alarm_row_ts(row), kind_text,
                                         kw, lo_ms, hi_ms):
                        state["pending"].append(row)
            out = state["pending"][:limit]
            del state["pending"][:limit]
            return {proto.KEY_OK: True,
                    proto.ALARM_KEY_ROWS: out,
                    proto.ALARM_KEY_TOTAL: -1}

        # ---- 探测批（失败在发头之前回 JSON 错误）----
        first = await fetch_batch(0, export_mod.EXPORT_CHUNK)
        if not isinstance(first, dict) or not first.get(proto.KEY_OK):
            err = (first.get("err") if isinstance(first, dict)
                   else "服务端无应答")
            raise HttpError(502, _alarm_export_error(err))

        gen = _guarded_export(
            export_mod.export_stream(
                fetch_batch, headers,
                lambda row: _alarm_row_selected(row, cols),
                fmt, sheet_name="报警", first_doc=first),
            fmt)
        filename = export_mod.export_filename("hsf_alarm", fmt)
        if fmt == "xlsx":
            return stream_response(
                gen, XLSX_CONTENT_TYPE,
                headers={
                    "Content-Disposition":
                        'attachment; filename="%s"' % filename,
                    "Cache-Control": "no-store",
                })
        return stream_response(
            gen, "text/csv; charset=utf-8",
            headers={
                "Content-Disposition": 'attachment; filename="%s"' % filename,
            })

    # ==================================================================
    #                    远端配置（0x0116 / 0x0117）
    # ==================================================================
    async def _config_get(self, session):
        self._require_view(session)
        self._require_ready()
        result = await self.hub.request(proto.CMD_CONFIG_GET, None,
                                        timeout=8.0)
        return json_response(_with_ok(result))

    async def _config_set(self, request, session):
        self._require_perm(session, proto.PERM_ACCOUNT_ADMIN)
        self._require_ready()
        body = request.json(default={}) or {}
        section = str(body.get("section", "") or "")
        if section not in CONFIG_SECTIONS:
            raise HttpError(400, "section 必须是 %s 之一"
                            % "/".join(CONFIG_SECTIONS))
        # 原样转发（含 section 键）；服务端按出现的键逐个应用、未出现的
        # 保持原值（部分更新）；非法键服务端 fail-closed 报错，透传其 err。
        self.log.info("配置写入（%s）：%s"
                      % (section, json.dumps(body, ensure_ascii=False)[:300]))
        result = await self.hub.request(proto.CMD_CONFIG_SET, body,
                                        timeout=8.0)
        return json_response(_with_ok(result))


# ============================================================================
#              报警行的显示映射与筛选（导出用；与报警页同口径）
# ============================================================================
#
# [为什么后端也要一份] 报警页（page11_alarm.js）在**前端**把服务端行映射
# 成四列显示行；导出在后端做 ⇒ 这份映射必须与前端逐条同口径，否则
# "导出的表"和"看到的表"对不上（同一个告警，导出里类型列写「自动运行」
# 而界面写「流量计」⇒ 现场对账时没法信）。
#
# 事实源：page11_alarm.js 的 serverRowOf / rowVisible
#         （= 桌面客户端 pages_other.AlarmPage._server_row_to_record）。

#: 报警导出的表头（与报警页四列逐字一致：时间/设备/类型/状态）。
ALARM_HEADERS = ("时间", "设备", "类型", "状态")

#: 服务端报警行 kind → (类型列兜底文案, 状态列文案)。
#: [与 page11_alarm.js 的 SERVER_KIND_ROWS 逐项一致]
#: （桌面客户端那份少 analog/digital 两档——网页端更全；这里照网页端，
#:  导出的是网页端看到的那张表。）
ALARM_KIND_ROWS = {
    "auto": ("自动运行", "自动运行告警"),
    "valve": ("电动球阀组", "阀告警"),
    "flowmeter": ("流量计", "流量计告警"),
    "device": ("设备故障", "设备故障"),
    "analog": ("模拟量", "越限保护"),
    "digital": ("数字量", "数字量告警"),
}


def _alarm_export_error(err):
    """上游失败的导出错误文案（区分"服务端没有 0x011A"）。

    [为什么要区分] 老服务端（C 端）没有 0x011A ⇒ 前端要能认出这个
    失败并回落本机边沿存档导出；文案里带「0x011A」就是给前端看的
    契约（前端按它判是否回落，别的错误不回落）。
    """
    text = str(err or "服务端拒绝")
    if "未知命令" in text:
        return "服务端不支持报警历史查询（0x011A）：%s" % text
    return "导出失败：%s" % text


def _alarm_export_cols(request):
    """导出列集合（cols=0,1,3；缺省/非法 = 全部四列）。

    [为什么支持] 报警页有"列多选"，导出跟随**可见列**（与桌面客户端
    AlarmPage._export_data 同款：界面看到哪些列就导哪些列）；缺省给
    全部四列（后端被直接调用时的口径）。
    """
    picked = []
    for part in str(request.query.get("cols") or "").split(","):
        part = part.strip()
        if not part:
            continue
        try:
            idx = int(part)
        except (TypeError, ValueError):
            continue
        if 0 <= idx < len(ALARM_HEADERS) and idx not in picked:
            picked.append(idx)
    return picked or list(range(len(ALARM_HEADERS)))


def _alarm_row_selected(row, cols):
    """服务端报警行 → 按导出列集合取出的文本列表。"""
    texts = alarm_row_texts(row)
    return [texts[i] if i < len(texts) else "" for i in cols]


def alarm_row_ts(row):
    """服务端报警行的时刻（epoch 毫秒；坏值 → 0）。"""
    try:
        return int(row.get(proto.ALARM_KEY_TS) or 0)
    except (TypeError, ValueError):
        return 0


def _alarm_dev_text(dev):
    """设备类别键（dev）→ 中文类别名；缺/不认识 → 空串。

    [注意顺序] 必须先排除 None 再转数字：int(None) 会抛、而
    Number(null)===0 会把「无类别」错映射成「变频器」。
    """
    if dev is None:
        return ""
    try:
        key = int(dev)
    except (TypeError, ValueError):
        return ""
    return model_mod.KIND_TEXT.get(key, "")


def alarm_row_texts(row):
    """服务端报警行 {ts,kind,source,text,dev} → [时间, 设备, 类型, 状态]。

    与 page11_alarm.js 的 serverRowOf 逐条同口径：
      · 健康边沿行（kind=health 或无 kind 但有 source）：设备列=设备名、
        类型列=设备类别（缺 dev → 「设备异常」）、状态列=健康档文案；
      · 其余（auto/valve/flowmeter/device/analog/digital）：类型列=设备
        类别（缺 dev → 按 kind 的兜底类别）、状态列=按 kind 的告警文案；
      · 自动/模拟量/数字量告警的**设备列放告警原文**（"现场启动按钮被拒
        ——…"），阀/流量计/设备故障放来源名——与网页端同款。
    """
    ts = alarm_row_ts(row)
    kind = str(row.get(proto.ALARM_KEY_KIND) or "")
    source = str(row.get(proto.ALARM_KEY_SOURCE) or "")
    text = str(row.get(proto.ALARM_KEY_TEXT) or "")
    dev_text = _alarm_dev_text(row.get(proto.ALARM_KEY_DEV))
    if kind == "health" or (not kind and source):
        kind_text = dev_text or "设备异常"
        name, state = source, text
    else:
        base_kind, base_state = ALARM_KIND_ROWS.get(
            kind, ("自动运行", "自动运行告警"))
        kind_text = dev_text or base_kind
        state = base_state
        name = text if kind in ("auto", "analog", "digital") \
            else (source or text)
    stamp = _alarm_hms(ts)
    return [stamp, name or "--", kind_text or "--", state or "--"]


def _alarm_hms(ts_ms):
    """毫秒 → "HH:MM:SS"（报警页时间列口径；缺/坏 → "--"）。"""
    if not ts_ms or ts_ms <= 0:
        return "--"
    import time as _time
    try:
        return _time.strftime("%H:%M:%S",
                               _time.localtime(ts_ms / 1000.0))
    except (TypeError, ValueError, OSError, OverflowError):
        return "--"


def alarm_row_visible(texts, ts_ms, kind_text, kw, lo_ms, hi_ms):
    """行是否命中筛选（与 page11_alarm.js 的 rowVisible 逐条同口径）。

    · 类型：与**类型列文本**全等匹配（类型列 = 设备类别或「自动运行」）；
    · 时间窗：ts < lo ⇒ 隐（ts 缺失/为 0 时不按 lo 隐，与前端同）；
      ts > hi ⇒ 隐；
    · 关键字：设备/类型/状态三列**文本包含**（空 = 不筛）。
    """
    if kind_text:
        if len(texts) < 3 or texts[2] != kind_text:
            return False
    if lo_ms > 0 and ts_ms > 0 and ts_ms < lo_ms:
        return False
    if hi_ms > 0 and ts_ms > hi_ms:
        return False
    if kw:
        hit = False
        for idx in (1, 2, 3):
            if idx < len(texts) and kw in texts[idx]:
                hit = True
                break
        if not hit:
            return False
    return True


# ============================================================================
#                          导出流的兜底包装
# ============================================================================

async def _guarded_export(gen, fmt):
    """给导出生成器加"中途失败可见"兜底。

    探测批已经把链路/权限问题挡在响应头之前；取数中途链路断掉时，
    CSV 追加一行显式的错误注释行（# 开头，Excel 里可见、可 grep），
    不让文件静默截短。

    [XLSX 不追加注释行] XLSX 是 zip 二进制，追加一行 CSV 会**破坏文件
    结构**（Excel 会报"文件已损坏"）。且 XLSX 的取数与写盘都在**第一个
    字节发出之前**完成（export_stream 先写临时文件、再分块回读）⇒ 失败
    时一个字节都没发出去 ⇒ 浏览器拿到 0 字节文件 ⇒ 前端据空文件判失败。
    这里让异常照常抛出（服务端日志留痕），不产出坏文件。
    """
    try:
        async for data in gen:
            yield data
    except Exception as exc:                                # noqa: BLE001
        if str(fmt or "").lower() == "csv":
            yield export_mod.csv_line(
                ["#EXPORT_ABORTED", "导出中止：%s: %s"
                 % (type(exc).__name__, exc)])
        else:
            raise


def _jog_max_ms():
    from . import hub as hub_mod
    return hub_mod.JOG_MAX_MS


def _as_bool(value):
    """宽松转 bool（"true"/"1"/非零 → True；用于 JSON 载荷清洗）。"""
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    text = str(value).strip().lower()
    if text in ("1", "true", "yes", "on"):
        return True
    if text in ("0", "false", "no", "off", ""):
        return False
    raise ValueError("不是布尔值: %r" % (value,))


def _require_bool(value):
    if isinstance(value, bool):
        return value
    raise HttpError(400, "字段必须是布尔值（true/false）")


def _with_ok(result):
    """把服务端应答规整成前端认的形状（ok/err 恒在；不洗白否定应答）。

    [快照应答没有 ok 键] C 服务端的**纯数据快照**（0x0102/0x0103/0x0104/
    0x0106/0x0107/0x0108/0x0110/0x0112/0x0113）不盖章 ok——载荷本身就是
    数据（Qt 客户端直接当数据用，从不看 ok）。只有**包络类**应答（控制/
    配置/分页/模式）才带 ok:true，拒绝时必带 ok:false + err（HsfServerApp.c
    的 hsfAppMakeEnvelope）。故规则：**显式 ok 尊重之；没盖 ok 但载荷
    非空且无 err = 成功**（数据就在）；空 dict（传输层没拿到）= 失败。
    """
    if not isinstance(result, dict) or not result:
        return {"ok": False, "err": "服务端应答为空"}
    out = dict(result)
    if proto.KEY_OK in result:
        out["ok"] = bool(result.get(proto.KEY_OK))
    elif result.get(proto.KEY_ERR):
        out["ok"] = False
    else:
        out["ok"] = True
    if not out["ok"] and not out.get("err"):
        out["err"] = "服务端未说明原因"
    return out


def _int_or(value, default=0):
    if isinstance(value, bool):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _int_arg(request, name, default, low, high):
    raw = request.query.get(name)
    if raw in (None, ""):
        return default
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return default
    return max(low, min(high, value))


def _parse_index_list(raw, high):
    """「0,2,5」→ 去重后的合法下标列表（顺序保留；越界/坏值丢弃）。

    用于导出列 `outCols`：勾选顺序即导出列顺序；空/全非法 = 空列表，
    调用方按「全部列」处理（fail-open，老前端不带这个参数行为不变）。
    """
    out = []
    for part in str(raw or "").split(","):
        try:
            idx = int(part.strip())
        except (TypeError, ValueError):
            continue
        if 0 <= idx < high and idx not in out:
            out.append(idx)
    return out


def _export_format(request):
    """导出格式参数（**format** 为主，**fmt** 为兼容别名）。

    [为什么收两个名字] 三条导出路由（历史/日志/报警）历来用 ``format=``；
    但外部调用方（脚本、别的端）也可能按 ``?fmt=xlsx`` 写 ⇒ 若只认
    ``format``，``fmt=xlsx`` 会**静默回落成 csv**（拿到一个 CSV 文件却
    以为是 Excel）——那是最难查的一类错。⇒ 两个都收，``format`` 优先。

    :returns: "csv" | "xlsx"（都小写）；非法值 → None（由调用方 400）。
    """
    raw = request.query.get("format")
    if raw in (None, ""):
        raw = request.query.get("fmt")
    text = str(raw or "csv").strip().lower()
    return text if text in ("csv", "xlsx") else None


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
