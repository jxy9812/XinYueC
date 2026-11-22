# -*- coding: utf-8 -*-
"""WebHub：网页服务器的中枢 —— 一条上游链路，扇出给所有浏览器。

职责一览
========

    1. 持有唯一的 XLinkUpstream（**惰性启动**：首个浏览器登录成功后才连上游）
    2. 收推送 → 解 BSON → 落到 AppModel → 扇出事件给所有 SSE 订阅者
    3. **订阅收敛**：把所有浏览器的"想要的主题"并集起来，只把差集发给服务端
    4. 请求代理：浏览器的 POST 转成 XLink 请求，统一做闸门预检与超时兜底
    5. 点动手持：浏览器按住点动时，网页服务器**代为**每 200ms 重发（带给定）

为什么订阅要收敛
================

Qt 客户端是「切页才订阅、隐藏即退订」：18 个页面不会同时订 9 个主题。
网页版有多个浏览器，每个的当前页不同。若各自直接控制上游订阅，
A 离开数字量页会退订数字量 —— 而 B 正停在数字量页，于是 B 的界面
停在 `--` 不动（服务端不再推）。所以必须按**并集**收敛：

    desired = union(每个 SSE 连接的 topics)
    上游 set_topics(desired)      ← 只发差集，无变化的主题不动

[收敛延迟] 切页时先订新的再退旧的（差值都是 add），中间没有空档。
若某个浏览器断线（SSE 关闭），它的 topics 从并集里移除，下一次收敛
就把没人要的主题退掉 —— 由 200ms 巡检任务驱动，多推几帧无害。

[漏登记主题 = 整页静默] `TOPIC_KIND` 取不到就**静默丢弃**：该主题的
推送全部消失、页面显示 `--`，而服务端日志明明在发（客户端漏过流量计、
旧骨架漏过浊度/泵——同一条实锅）。主题映射九个全登记，见下。

为什么点动要在服务端代持
========================

点动是"按住才动、松手即停"的动作，客户端每 200ms 重发一帧（**捎带给定**
hz/rpm——服务端拒绝停机时的独立给定，每拍先写给定再发点动命令字），
服务端 600ms 收不到就自动停机（`proto.JOG_TIMEOUT_MS`）。
浏览器的按下/抬起事件走 HTTP，若由浏览器自己起定时器重发：

  · 页面被切到后台时浏览器会节流定时器（Chrome 后台定时器最快 1 次/分钟），
    按住不放的电机会在 600ms 后自己停 —— 用户以为按钮坏了
  · 网络抖动/代理超时会让重发断档，同样误停

故网页版把"按住"变成一个**会话级状态**：浏览器 POST jog/start，
网页服务器自己起 200ms 任务重发；POST jog/stop 才停。

[兜底] 即使浏览器崩溃没发 stop，网页服务器也在 `JOG_MAX_MS`（缺省 10s）
后自动停 —— 不能让一个崩掉的页面把电机一直点着。

[为什么不再自采历史] 历史真源是服务端 SQLite（19 列 HISTORY_COLUMNS），
web 自采会与服务端口径漂移（缺列/单位/格式各有一套）——历史/日志页全部
走 /api/history/page、/api/log/page 转发（见 api.py）。本模块只保留
RunLog（web 自身日志）与 AlarmLog（报警页边沿数据源）。
"""

import asyncio
import threading
import time
from collections import deque

from . import bson
from . import model as model_mod
from . import proto
from . import store as store_mod
from .camera_frames import CameraAssembler
from .xlink import AuthRejected, RequestTimeout, SessionState, XLinkUpstream


#: 主题 → 事件名（SSE 的 event: 字段 / 内部 kind 名）。
#:
#: [漏一个就会"页面永远没数据"] `_on_publish` 里取不到就**静默丢弃**，
#: 后果是该主题的推送全部消失，页面显示 `--`，而服务端日志明明在发。
#: 与 client bridge.TOPIC_KIND 逐项一致（九个遥测主题一个不漏）。
TOPIC_KIND = {
    proto.TOPIC_VFD_STATUS: "vfd",
    proto.TOPIC_TEMPHUM_STATUS: "temphum",
    proto.TOPIC_AI_STATUS: "analog",
    proto.TOPIC_FLOWMETER_STATUS: "flowmeter",
    proto.TOPIC_TURBIDITY_STATUS: "turbidity",
    proto.TOPIC_PUMP_STATUS: "pump",
    proto.TOPIC_DIGITAL_STATUS: "digital",
    proto.TOPIC_DEVICE_HEALTH: "health",
    proto.TOPIC_VALVE_STATUS: "valve",
    proto.TOPIC_AUTO_ALARM: "alarm",      # 自动运行告警 → alarm 事件
    # 相机画面（2026-11-19）：**RAW 二进制**载荷（不是 BSON）⇒ 走单独的
    # 分片组帧分支，不进下面的 BSON 解码路径。
    proto.TOPIC_CAMERA_FRAME: "camera",
    # TOPIC_VALVE_ALARM 是遗留常量（服务端已改合帧进 0x00010007，`ac` 键
    # 判型），不订阅、不登记；收到也不认识——这是有意的。
}

#: **全局常驻订阅**的主题。
#:
#: [为什么温湿度/健康必须常驻] 顶栏的温湿度/设备健康是 chrome，
#: 任何页面都要显示。早先"只订本页需要的主题"导致数字量页不订温湿度
#: ⇒ 顶栏立刻变成 `--`（客户端实锅）。
#:
#: [为什么自动告警必须常驻 2026-11-14] TOPIC_AUTO_ALARM 是**事件流**
#: （急停/阀超时/动作失败…），**不是**周期遥测：只在「报警页开着」时
#: 订它 ⇒ 用户在别的页面时上游根本不推 ⇒ 那些告警永远收不到、也不落
#: 本地报警档（行数据源与订阅集合分离的同一个坑，Python 客户端的
#: bridge.PERSISTENT_TOPICS / C 客户端的连接期常驻订阅都是这么修的）。
#: 现在**无条件订阅**：hub._on_publish 收到即喂 AlarmLog（落档）+ 广播
#: alarm 事件给所有 SSE 连接（广播不分主题 ⇒ 报警页打开与否都能看到）。
GLOBAL_TOPICS = (
    proto.TOPIC_TEMPHUM_STATUS,
    proto.TOPIC_DEVICE_HEALTH,
    proto.TOPIC_AUTO_ALARM,
)

#: 点动看门狗：网页服务器在这个时长后强制停（浏览器崩了也不会一直点）。
JOG_MAX_MS = 10000
#: 点动重发间隔（与服务端 JOG_TIMEOUT_MS=600 配合：容忍连丢 2 帧）。
JOG_REPEAT_MS = proto.JOG_REPEAT_MS


class Subscriber(object):
    """一个 SSE 订阅者（= 一个浏览器标签页的连接）。

    [为什么要单独一个对象而不是裸队列] 需要记录"这个连接想看哪些主题"
    （用于订阅收敛）与"什么时候断的"（用于清理并重算并集）。
    """

    __slots__ = ("queue", "topics", "conn_id", "user", "role",
                 "created_ms", "dropped", "sent_bytes", "last_ms")

    def __init__(self, conn_id, user, role, topics=()):
        self.conn_id = conn_id
        self.user = user
        self.role = int(role)
        self.topics = set(topics)
        #: 有界的推送队列。
        #:
        #: [必须有界] 浏览器卡住/网络堵住时，队列会无限增长把板子内存吃光。
        #: 满了就丢**最旧**的（推的是最新状态，旧的本来也没用）。
        self.queue = asyncio.Queue(maxsize=256)
        self.created_ms = _now_ms()
        #: 最近一次「有事件投给它」的时刻（LRU 淘汰用）。
        self.last_ms = self.created_ms
        self.dropped = 0
        self.sent_bytes = 0

    def offer(self, event, data):
        """非阻塞投递一条事件（队列满则丢最旧）。"""
        item = (event, data)
        while True:
            try:
                self.queue.put_nowait(item)
                self.last_ms = _now_ms()
                return True
            except asyncio.QueueFull:
                try:
                    self.queue.get_nowait()
                    self.dropped += 1
                except asyncio.QueueEmpty:
                    pass


def _now_ms():
    return int(time.monotonic() * 1000)


class WebHub(object):
    """网页服务器中枢。"""

    #: 收敛巡检周期（毫秒）。
    RECONCILE_MS = 200
    #: 运行模式/自动状态族周期轮询间隔。桌面客户端按 1s 轮询 CMD_RUN_MODE，
    #: 网页版同口径——没有这条环，相位文字/累计时长/有界倒计时只在两次
    #: 命令应答之间冻结（2026-11-13 现场「web 倒计时不走、时间不走」实锅）。
    MODE_POLL_MS = 1000
    #: 同时在线 SSE 连接数**硬上限**（僵尸订阅者的兜底闸）。
    #:
    #: [为什么要有] SSE 是"只写不读"的长连接：拔网线/半开 TCP 的订阅者
    #: 若不被回收，会永远留在订阅表里，它的 topics 还污染并集 ⇒ 上游
    #: 继续为「没人看的浏览器」推主题。httpd 侧已有空闲/寿命/写出超时
    #: 三道闸回收僵尸；这里再加一道**数量**闸（LRU 淘汰）⇒ 即便有
    #: 漏网之鱼，订阅表也不会无界增长。
    MAX_SUBSCRIBERS = 32

    def __init__(self, host, port, user, password, data_dir=None, log=None):
        self.log = log or store_mod.RunLog()
        self.model = model_mod.AppModel()
        self.alarms = store_mod.AlarmLog(data_dir)
        #: 相机分片组帧器（2026-11-19）：web 端**不从浏览器直连相机**，
        #: 画面经上游 XLink 分片到本机再组帧。
        self._camera_asm = CameraAssembler()
        #: [2026-11-19 ES 直传] NAL 环形缓冲（见 camera_es_since /
        #: _on_camera_frame）。
        self._es_lock = threading.Lock()
        self._es_queue = deque()
        self._es_bytes = 0
        self._es_cursor = 0
        self._es_base = 0
        self._es_codec = "h264"
        self._es_last_broadcast = 0
        self.upstream = XLinkUpstream(host, port, user, password,
                                      log=self.log.write,
                                      reconnect_interval=2.0)
        self.upstream.on_publish = self._on_publish
        self.upstream.on_state_changed = self._on_state_changed
        self.upstream.on_ready_changed = self._on_ready_changed

        self.subscribers = {}          # conn_id → Subscriber
        self._conn_seq = 0
        self._upstream_started = False

        #: 最近一次解析出的推送时刻（诊断）。
        self._last_publish_ms = 0

        # ---- 点动状态 ----
        self._jog_task = None
        self._jog_reverse = False
        self._jog_given = {}           # 按下那一刻的给定（hz / rpm），每拍捎带
        self._jog_started_ms = 0
        self._jog_owner = None         # (conn_id, user)
        self.jog_count = 0

        # ---- 后台任务 ----
        self._tasks = []

    # ==================================================================
    #                          生命周期
    # ==================================================================
    def ensure_upstream(self):
        """惰性启动上游链路（首个浏览器登录成功后调）。

        [为什么不随 web 启动] 上游主链路账号固定（缺省 root/root），
        root 单会话策略下**常驻空占**会把桌面客户端/截图流程挡在外面。
        没人用网页就不占上游会话；第一个浏览器登录成功（凭据已由一次性
        探测连接到服务端真认证过）才起主链路。
        """
        if self._upstream_started and self.upstream.task_running():
            return
        self._upstream_started = True
        self.log.info("首个浏览器登录成功，连接上游 %s:%d（%s）"
                      % (self.upstream.host, self.upstream.port,
                         self.upstream.user))
        self.upstream.start()

    async def start(self):
        """起 web 侧后台任务。**不**连上游（惰性，见 ensure_upstream）。"""
        self.log.info("网页服务器中枢启动（上游 %s:%d 惰性连接）"
                      % (self.upstream.host, self.upstream.port))
        loop = asyncio.get_event_loop()
        self._tasks = [
            loop.create_task(self._reconcile_loop()),
            loop.create_task(self._mode_poll_loop()),
        ]

    async def close(self):
        self.log.info("网页服务器中枢关闭")
        await self.stop_jog(reason="服务关闭", force=True)
        for task in self._tasks:
            task.cancel()
        for task in self._tasks:
            try:
                await task
            except (asyncio.CancelledError, Exception):     # noqa: BLE001
                pass
        self._tasks = []
        await self.upstream.close()

    # ==================================================================
    #                       上游事件 → 模型 → 扇出
    # ==================================================================
    def _on_state_changed(self, state, detail):
        """链路状态变化 → 广播给所有浏览器（顶栏的连接指示）。"""
        ready = state == SessionState.READY
        self.broadcast("link", self._link_event(ready, detail))

    def _link_event(self, ready, detail=""):
        st = self.upstream.status()
        return {
            "state": st["state"],
            "ready": bool(ready),
            "detail": detail,
            "error": st["error"],
            "authBlocked": st["authBlocked"],
            "role": st["role"],
            "user": st["user"],
            "host": st["host"],
            "port": st["port"],
        }

    def _on_ready_changed(self, ready):
        """链路就绪翻转：重连后必须把订阅集合重新下发。

        [为什么在这里重订] 服务端不记跨会话订阅（见 xlink._teardown 的清空），
        断线重连后上游 subscriptions 是空的。若不补，重连后所有页面都不再
        收到推送，而链路状态显示"已连接"——最难查的那种故障。
        """
        if ready:
            self.log.info("上游就绪，重新下发订阅")
            self._schedule(self._reconcile_now())
            # 顺便拉一次运行模式与健康（这两个不是全程推送的）。
            self._schedule(self._refresh_mode())
            self._schedule(self._refresh_health())
        else:
            # [断线要清非周期数据] run_mode/health/自动状态族没有新鲜度
            # 保护，不清就会一直显示断线前的值（界面说"手动可操作"但
            # 其实连不上）。
            self.model.reset_live()
            self.broadcast("mode", self.model.mode_json())
            self.broadcast("health", self.model.health.to_json())
            self._schedule(self.stop_jog(reason="链路断开", force=True))

    #: [ES 直传 2026-11-19] NAL 环形缓冲（cursor → bytes）：
    #: 浏览器用 `/api/camera/es?cursor=N` 增量取；丢头后 cursor 落后 ⇒
    #: 响应带 resync（浏览器重置解码器等下一 GOP）。512Kbps×2s GOP ≈
    #: 128KB ⇒ 1MB 上限绰绰有余（>2 个整 GOP 才会覆盖到游标）。
    ES_BUFFER_MAX_BYTES = 1024 * 1024

    def _on_camera_frame(self, body):
        """相机分片 → 组帧 → 存模型 → 广播**元信息**。

        [为什么只广播元信息] SSE 是文本通道 ⇒ JPEG 走 base64 要 +33% 体积。
        ⇒ 帧本体留在 web 进程内存（model.camera.frame），浏览器用
        `<img src="/api/camera/frame?ts=…">` 现取；SSE 只告诉页面
        「有新帧了 + 尺寸 + 序号」⇒ 页面据此换 img 的 URL（带时间戳破缓存）。
        [ES 直传分支] 组出的是**编码流 NAL**（flags 带 ES 位）⇒ 不进
        `model.camera.frame`（那是 JPEG 语义），改入本方法上方 ES 环形
        缓冲；浏览器照旧轮询（`/api/camera/es?cursor=`），画面本体依旧
        不过 SSE。广播节流：NAL 20fps 逐条广播会把 SSE 打满 ⇒ 每
        200ms 至多一条（页面收到就拉一次增量）。
        """
        data = self._camera_asm.feed(body)
        if data is None:
            return
        if self._camera_asm.last_flags & proto.CAM_FRAME_FLAG_ES:
            h265 = bool(self._camera_asm.last_flags
                        & proto.CAM_FRAME_FLAG_H265)
            with self._es_lock:
                self._es_cursor += 1
                self._es_queue.append((self._es_cursor, data))
                self._es_bytes += len(data)
                self._es_codec = "h265" if h265 else "h264"
                while self._es_bytes > self.ES_BUFFER_MAX_BYTES \
                        and self._es_queue:
                    _cur, old = self._es_queue.popleft()
                    self._es_bytes -= len(old)
                    self._es_base = max(self._es_base, _cur)
            now = _now_ms()
            if now - self._es_last_broadcast >= 200:
                self._es_last_broadcast = now
                self.model.camera.mode = "es"
                self.model.camera.codec = self._es_codec
                self.model.camera.seq += 1
                self.model.camera.ts_ms = now
                self.broadcast("camera", self.model.camera.to_json())
            return
        self.model.camera.set_frame(data, self._camera_asm.width,
                                    self._camera_asm.height)
        self.model.camera.dropped = self._camera_asm.dropped
        self.broadcast("camera", self.model.camera.to_json())

    def camera_es_since(self, cursor):
        """给 `/api/camera/es`：返回 (nal_list, codec, resync)。

        :param cursor: 浏览器上次拿到的最大 NAL 序号（0 = 从头要）。
        :return: ([(cursor, nal_bytes), …], codec_str, resync_bool)
            resync=True 表示请求的 cursor 已被环形缓冲挤掉 ⇒ 浏览器
            应重置解码器、按"全新订阅"处理（先收参数集+IDR 再解）。
        [队列为空 ⇒ 返回 (空, codec, False)]（HTTP 层转 404）——**不能
        回 resync=1**：页面 resync 分支会把 cursor 置 0 ⇒ 空队列 +
        cursor=0 恒 resync ⇒ 120ms 一次的空转循环（2026-11-19 IAB
        实锤：cursor 恒 0、resync 每 120ms 一条打点）。resync 只在
        「cursor 落在已淘汰区间（>0 且 < base）」时为真。
        """
        with self._es_lock:
            if not self._es_queue:
                return [], self._es_codec, False
            if cursor and cursor < self._es_base:
                return list(self._es_queue), self._es_codec, True
            return [(c, n) for (c, n) in self._es_queue if c > cursor], \
                self._es_codec, False

    def _on_publish(self, topic, payload, data_type):
        """收到推送（事件循环内）：解包 → 落模型 → 扇出。"""
        kind = TOPIC_KIND.get(topic)
        if kind is None:
            # 未知主题：记一次就够（不要每帧刷日志）。
            self.log.debug("忽略未知主题 0x%08X" % topic)
            return
        if kind == "camera":
            # [相机画面走单独分支] 载荷是 **RAW 二进制**（分片），不是 BSON
            # ⇒ 不能进下面的 bson.decode。
            self._on_camera_frame(payload)
            return
        try:
            doc = bson.decode(payload) if data_type == proto.DATATYPE_BSON else {}
        except ValueError as exc:
            self.log.warn("推送载荷解析失败（主题 0x%08X）：%s" % (topic, exc))
            return
        self._last_publish_ms = _now_ms()

        # ---- 落模型 ----
        if kind == "vfd":
            self.model.vfd.apply(doc)
            self.broadcast("vfd", self.model.vfd.to_json())
            return
        if kind == "temphum":
            self.model.temphum.apply(doc)
            self.broadcast("temphum", self.model.temphum.to_json())
            return
        if kind == "analog":
            self.model.analog.apply(doc)
            self.broadcast("analog", self.model.analog.to_json())
            return
        if kind == "flowmeter":
            self.model.flowmeter.apply(doc)
            self.broadcast("flowmeter", self.model.flowmeter.to_json())
            return
        if kind == "turbidity":
            self.model.turbidity.apply(doc)
            self.broadcast("turbidity", self.model.turbidity.to_json())
            return
        if kind == "pump":
            self.model.pump.apply(doc)
            self.broadcast("pump", self.model.pump.to_json())
            return
        if kind == "digital":
            try:
                index = int(doc.get(proto.KEY_DEV, 0) or 0)
            except (TypeError, ValueError):
                return
            dev = self.model.digital_state(index)
            if dev is None:
                # [越界索引 ⇒ 丢弃] 模型层对非法模块号不建对象（防无界
                # 增长），这里也不能往下走（None 没有 apply）。
                return
            dev.apply(doc)
            self.broadcast("digital", dev.to_json())
            return
        if kind == "health":
            self.model.health.apply(doc)
            self.alarms.feed_health(self.model.health)
            self.broadcast("health", self.model.health.to_json())
            return
        if kind == "alarm":
            # [两条告警源] TOPIC_AUTO_ALARM（{"kind","atMs","text"}）与
            # 阀合帧告警（TOPIC_VALVE_STATUS 里 "ac" 键）都汇到 alarm 事件。
            self._feed_auto_alarm(doc)
            return
        if kind == "valve":
            # 告警帧（"ac" 键）与状态帧混在同一主题上，ValveState.apply 内部分派。
            is_alarm = proto.KEY_ALARM_COUNT in doc
            if is_alarm:
                before = len(self.model.valve.alarms)
                self.model.valve.apply(doc)
                # [必须在这里存档] WebHub 是告警的唯一"总出口"：页面消费
                # 走的是 drain_alarms，若只让页面取走，没打开阀页的浏览器
                # 就永远看不到这条告警，而历史里也该留一条。
                for alarm in self.model.valve.alarms[before:]:
                    self.alarms.feed_valve_alarm(alarm)
                    self.broadcast("alarm", {
                        "kind": "valve",
                        "source": alarm.get(proto.KEY_ALARM_VALVE_NAME, ""),
                        "level": _alarm_level(alarm.get(proto.KEY_ALARM_KIND)),
                        "text": alarm.get(proto.KEY_ALARM_TEXT, ""),
                        "at": alarm.get(proto.KEY_ALARM_AT, 0),
                    })
                # 告警帧不改变状态，但让界面刷新一下"最近告警"区域。
                self.broadcast("valve", self.model.valve.to_json())
                return
            self.model.valve.apply(doc)
            self.broadcast("valve", self.model.valve.to_json())
            return

    def _feed_auto_alarm(self, doc):
        """TOPIC_AUTO_ALARM → AlarmLog 存档 + alarm 事件。"""
        text = str(doc.get("text", "") or "")
        at = _int_or(doc.get("atMs", 0), 0)
        kind_code = _int_or(doc.get("kind", 0), 0)
        # [流量计告警 2026-11-15] 第 6 类（上限/下限/空管/系统 + 空管保护
        # 动作）——不给它单列文案的话，网页端会一律显示"自动运行告警"，
        # 现场看报警页分不出是哪台设备的事。
        label = {proto.AUTO_ALARM_VALVE_TIMEOUT: "阀动作超时",
                 proto.AUTO_ALARM_FLOWMETER: "流量计报警",
                 proto.AUTO_ALARM_DEVICE_FAULT: "设备故障",
                 proto.AUTO_ALARM_PRESS_HIGH: "压差过高"}.get(
            kind_code, "自动运行告警")
        self.alarms.feed_auto_alarm(label, text, at)
        self.broadcast("alarm", {
            "kind": "auto",
            "source": "自动运行",
            "level": "warn",
            "text": text or label,
            "at": at,
        })

    def broadcast(self, event, data):
        """把一条事件投给所有订阅者（非阻塞）。

        :param event: SSE 的 event 名（前端按它分派）
        :param data: 任意可 JSON 序列化的结构
        """
        if not self.subscribers:
            return 0
        count = 0
        for sub in list(self.subscribers.values()):
            if sub.offer(event, data):
                count += 1
        return count

    # ==================================================================
    #                          订阅收敛
    # ==================================================================
    def register_subscriber(self, user, role):
        """新建一个 SSE 订阅者。"""
        self._conn_seq += 1
        conn_id = self._conn_seq
        sub = Subscriber(conn_id, user, role,
                         topics=set(GLOBAL_TOPICS))
        self.subscribers[conn_id] = sub
        self.log.info("SSE 连接 #%d 接入（%s）" % (conn_id, user))
        # 立刻补发一份全量快照：否则新页面要等下一拍推送才有数据，
        # 数字量/健康这类"变化才推"的主题可能等很久（首帧体验）。
        self._prime_subscriber(sub)
        # [数量闸] 超上限 ⇒ 淘汰最久没活动的（LRU），保证订阅表有界。
        self._evict_subscribers()
        self._schedule(self._reconcile_now())
        return sub

    def _evict_subscribers(self):
        """订阅者超上限 ⇒ 淘汰最久没活动的（LRU）。

        [为什么按 last_ms 排] `last_ms` 在「有事件投给它」时刷新 ⇒
        活跃的浏览器（每秒都有温湿度/健康事件）不会被淘汰；卡死/僵尸
        连接会被优先清掉。被淘汰者的 SSE 协程下一拍发现
        `is_subscriber_alive()` 为假 ⇒ 自行收口关闭连接。
        """
        while len(self.subscribers) > self.MAX_SUBSCRIBERS:
            oldest = min(self.subscribers.values(), key=lambda s: s.last_ms)
            self.log.info("SSE 连接 #%d 被淘汰（订阅者超上限 %d）"
                          % (oldest.conn_id, self.MAX_SUBSCRIBERS))
            self.unregister_subscriber(oldest)

    def is_subscriber_alive(self, sub):
        """该订阅者是否仍在册（被淘汰 ⇒ False ⇒ SSE 协程收口）。"""
        return self.subscribers.get(sub.conn_id) is sub

    def unregister_subscriber(self, sub):
        """断开一个 SSE 订阅者。"""
        if self.subscribers.pop(sub.conn_id, None) is not None:
            self.log.info("SSE 连接 #%d 断开（丢 %d 条积压）"
                          % (sub.conn_id, sub.dropped))
        if self._jog_owner and self._jog_owner[0] == sub.conn_id:
            self._schedule(self.stop_jog(reason="页面断开", force=True))
        self._schedule(self._reconcile_now())

    def _prime_subscriber(self, sub):
        """给新连接补发当前已知的全部状态（一次 JSON）。"""
        snap = self.model.snapshot()
        snap["link"] = self._link_json()
        snap["initial"] = True
        sub.offer("snapshot", snap)

    def _link_json(self):
        st = self.upstream.status()
        st["jogActive"] = self.jog_active()
        st["jogCount"] = self.jog_count
        return st

    def set_subscriber_topics(self, sub, topics):
        """更新某个连接想看的主题（页面切页时调）。"""
        sub.topics = set(topics) | set(GLOBAL_TOPICS)
        self._schedule(self._reconcile_now())

    def desired_topics(self):
        """所有连接想看的主题并集 + 全局常驻。"""
        wanted = set(GLOBAL_TOPICS)
        for sub in self.subscribers.values():
            wanted |= sub.topics
        return wanted

    async def _reconcile_loop(self):
        """周期收敛（兜底：断线/超时没触发到 _reconcile_now 时也能收敛）。"""
        try:
            while True:
                await asyncio.sleep(self.RECONCILE_MS / 1000.0)
                await self._reconcile()
        except asyncio.CancelledError:
            raise

    async def _reconcile_now(self):
        """尽快收敛一次（切页/连接变化时调）。"""
        await self._reconcile()

    async def _mode_poll_loop(self):
        """周期查一次运行模式/自动状态族（对齐桌面客户端 1s 轮询 CMD_RUN_MODE）。

        [为什么必须轮询] /api/mode 只回 model 缓存，而 model 的 mode 族仅
        在「链路就绪」与「0x0105/0x0115 命令应答」两个时机刷新 ⇒ 组合循环
        的相位推进、累计时长、有界倒计时在两次命令应答之间全部不走
        （服务端在推进、界面却冻结）。每秒查一次并广播，前端表盘/状态行
        才有秒级节拍。
        """
        try:
            while True:
                await asyncio.sleep(self.MODE_POLL_MS / 1000.0)
                if not self.upstream.is_ready():
                    continue
                await self._refresh_mode()
        except asyncio.CancelledError:
            raise

    async def _reconcile(self):
        """把上游订阅集合收敛到 `desired_topics()`。"""
        if not self.upstream.is_ready():
            return
        desired = self.desired_topics()
        if not desired:
            return
        try:
            await self.upstream.set_topics(desired)
        except (RequestTimeout, AuthRejected) as exc:
            self.log.warn("订阅收敛失败：%s" % exc)
        except Exception as exc:                            # noqa: BLE001
            self.log.warn("订阅收敛异常：%s：%s" % (type(exc).__name__, exc))

    # ==================================================================
    #                          请求代理
    # ==================================================================
    async def request(self, msg_code, doc=None, timeout=5.0):
        """转成 XLink 请求，返回解出的 dict。

        :raises TimeoutError: 未连接/超时（调用方转 HTTP 503/504）
        """
        payload = b""
        if doc is not None:
            payload = bson.encode(doc)
        try:
            _code, dtype, data = await self.upstream.request(
                msg_code, payload, proto.DATATYPE_BSON, timeout)
        except RequestTimeout as exc:
            raise TimeoutError(str(exc))
        if dtype != proto.DATATYPE_BSON:
            return {}
        try:
            return bson.decode(data)
        except ValueError as exc:
            self.log.warn("应答解析失败（cmd=0x%04X）：%s" % (msg_code, exc))
            return {}

    async def _refresh_mode(self):
        """查一次运行模式（模式钮/控制闸门都要它）。"""
        try:
            doc = await self.request(proto.CMD_RUN_MODE, None, timeout=4.0)
        except TimeoutError as exc:
            self.log.warn("查询运行模式失败：%s" % exc)
            return
        if doc:
            self.model.apply_mode_doc(doc)
            self.broadcast("mode", self.model.mode_json())

    async def _refresh_health(self):
        """拉一次全量健康（首连/重连后补）。"""
        try:
            doc = await self.request(proto.CMD_DEVICE_HEALTH, None, timeout=4.0)
        except TimeoutError as exc:
            self.log.warn("查询设备健康失败：%s" % exc)
            return
        if doc:
            self.model.health.apply(doc)
            self.alarms.feed_health(self.model.health)
            self.broadcast("health", self.model.health.to_json())

    async def set_run_mode(self, mode):
        """切换运行模式（闸门在 api 层）。返回服务端应答 dict。"""
        doc = await self.request(proto.CMD_RUN_MODE, {"mode": int(mode)},
                                 timeout=6.0)
        self._consume_mode_response(doc)
        return doc

    async def send_auto_control(self, payload):
        """自动运行控制（0x0115；op 或 on）。返回服务端应答 dict。"""
        doc = await self.request(proto.CMD_AUTO_CONTROL, payload, timeout=8.0)
        self._consume_mode_response(doc)
        return doc

    # ==================================================================
    #        报警历史分页（0x011A）/ 变频器参数（0x011B）
    # ==================================================================
    async def alarm_page(self, offset=0, limit=None, before_ts=None,
                         lo_ts=None, hi_ts=None, kw=None, timeout=8.0):
        """分页读**服务端**报警历史（0x011A；2026-11-17 对齐桌面客户端）。

        [为什么要它] 报警页此前是数据三页里唯一不向服务端取数的一页：
        web 端在 hub 进程内存里算健康边沿（AlarmLog）⇒ 记录只在该进程
        活着期间存在；桌面客户端各算各的 ⇒ 两端条数天然对不上（用户实报
        「web 端报警有数据，客户端报警视图没数据」）。⇒ 边沿改由**服务端**
        记进 alarms 表，两端都按本命令取同一份（单一数据源）。

        :param offset: 跳过多少行（0 = 最新一段；时间 DESC 取页、页内正序）。
        :param limit:  本页行数（服务端夹到 1..50）。
        :param before_ts: 分页窗口锚点（新查询发一次、翻页原样回传）。
        :param lo_ts/hi_ts: 时间窗（epoch 毫秒）。
        :param kw: 关键字（text/source/kind 三列 LIKE）。
        返回服务端应答 dict：{"ok","err"?,"total","count","offset","limit",
        "rows":[{ts,kind,source,text,dev}…]}。

        [老服务端兜底] 上游没有 0x011A（C 端）⇒ 服务端回**显式失败包络**
        （ok=false + err）⇒ api 层原样透传，前端据此回落本地边沿存档
        （fail-open，别让报警页变空白）。
        """
        if limit is None:
            limit = proto.ALARM_PAGE_DEFAULT
        doc = {proto.ALARM_KEY_OFFSET: max(0, int(offset)),
               proto.ALARM_KEY_LIMIT: int(limit)}
        if before_ts is not None and int(before_ts) > 0:
            doc[proto.ALARM_KEY_BEFORE_TS] = int(before_ts)
        if lo_ts is not None and int(lo_ts) > 0:
            doc[proto.ALARM_KEY_LO_TS] = int(lo_ts)
        if hi_ts is not None and int(hi_ts) > 0:
            doc[proto.ALARM_KEY_HI_TS] = int(hi_ts)
        if kw:
            doc[proto.ALARM_KEY_KW] = str(kw)
        return await self.request(proto.CMD_ALARM_PAGE, doc, timeout=timeout)

    async def vfd_params(self, op=None, values=None, timeout=8.0):
        """读/写变频器功能码参数（0x011B；2026-11-18 对齐桌面客户端）。

        :param op: 0=读整组（PARAM_DEFS 全部条目）/ 1=写若干项。
        :param values: op=1 时要写的 {addr(int): raw(int)}——**原始寄存器
            值**（工程值→raw 的标度换算按 proto.PARAM_DEFS 的 scale 在
            调用方完成）；线上键是 addr 的**十进制字符串**（BSON 文档键只能
            是字符串，与服务端 int(key) 归一口径一致）。
        返回服务端应答 dict：{"ok","err"?,"params":[{code,label,addr,raw,
        value,unit,writable,runLocked,applied?}…],"vfdFaulted","runState"}。
        **params[].value 是服务端从驱动器回读的工程值**（写后服务端重新
        读驱动器再返回）⇒ 页面只认应答里的 value，绝不显示本地提交值。

        [超时口径] 与桌面客户端同款：op=0 要把 0xF100 段逐个从驱动器读
        回来（Modbus 一问一答，板端 CPU 忙时 5s 不够）⇒ 默认给 8s 兜底。
        """
        if op is None:
            op = proto.VFD_PARAM_OP_READ
        doc = {proto.VFD_PARAMS_KEY_OP: int(op)}
        if values:
            # [BSON 键必须是字符串] int 键上不了线 ⇒ 地址转十进制串。
            doc[proto.VFD_PARAMS_KEY_VALUES] = {
                str(int(addr)): int(raw) for addr, raw in values.items()}
        return await self.request(proto.CMD_VFD_PARAMS, doc, timeout=timeout)

    def _consume_mode_response(self, doc):
        """0x0105/0x0115 应答落 model + 广播。

        [拒绝也回全量状态键] 服务端对这两条命令的否定应答同样带
        mode/auto/autoRunning/… 全键（C 端 hsfAppSendRunMode 应答注释口径），
        落模能让"为什么被拒"与界面状态一致。
        """
        if not doc:
            return
        self.model.apply_mode_doc(doc)
        self.broadcast("mode", self.model.mode_json())

    # ==================================================================
    #                          点动手持
    # ==================================================================
    async def start_jog(self, reverse, conn_id, user, given=None):
        """开始点动（浏览器按下）。已在点动则只更新方向。

        :param given: {"hz":…}|{"rpm":…,"hz":…} 按下那一刻的给定；
            每拍重发都捎带（服务端每拍先写给定再发点动命令字，与客户端
            pages_monitor._jog_repeat 同口径）。

        [为什么重复按下要容忍] 浏览器的 pressed 事件偶尔会重复触发
        （触屏长按、鼠标抖动）。重复 start 不该报错，只该刷新方向。
        """
        if self._jog_task is not None and not self._jog_task.done():
            self._jog_reverse = bool(reverse)
            if given:
                self._jog_given = dict(given)
            return True
        self._jog_reverse = bool(reverse)
        self._jog_given = dict(given or {})
        self._jog_owner = (conn_id, user)
        self._jog_started_ms = _now_ms()
        # 先发一帧（不等 200ms），让电机立刻响应"按下"这个动作。
        await self._jog_send()
        self._jog_task = asyncio.ensure_future(self._jog_loop())
        self.broadcast("jog", {"active": True, "reverse": self._jog_reverse})
        self.log.info("点动开始（%s%s）"
                      % ("反转" if reverse else "正转",
                         ("，给定 %s" % self._jog_given) if self._jog_given else ""))
        return True

    async def _jog_loop(self):
        """按住期间每 JOG_REPEAT_MS 重发一帧，超过 JOG_MAX_MS 强制停。"""
        try:
            while True:
                await asyncio.sleep(JOG_REPEAT_MS / 1000.0)
                if _now_ms() - self._jog_started_ms > JOG_MAX_MS:
                    self.log.warn("点动超过 %dms 未释放，强制停止" % JOG_MAX_MS)
                    await self.stop_jog(reason="超过最长点动时间", force=True)
                    return
                await self._jog_send()
        except asyncio.CancelledError:
            raise

    async def _jog_send(self):
        """发一帧 JOG（捎带给定）。失败不打断循环（链路抖一下就断点动更危险）。"""
        frame = {"op": proto.OP_JOG, "rev": bool(self._jog_reverse)}
        frame.update(self._jog_given or {})
        try:
            await self.request(proto.CMD_VFD_CONTROL, frame, timeout=2.0)
            self.jog_count += 1
        except TimeoutError:
            pass
        except Exception as exc:                            # noqa: BLE001
            self.log.warn("点动帧发送异常：%s" % exc)

    async def stop_jog(self, reason="", force=False):
        """松开点动 → 立刻发释放（不等看门狗），这是安全动作。"""
        task, self._jog_task = self._jog_task, None
        active = task is not None and not task.done()
        if task is not None and not task.done():
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):     # noqa: BLE001
                pass
        if not active and not force:
            return False
        try:
            await self.request(proto.CMD_VFD_CONTROL,
                               {"op": proto.OP_JOG_RELEASE}, timeout=2.0)
        except (TimeoutError, Exception):                   # noqa: BLE001
            pass
        self._jog_owner = None
        self._jog_given = {}
        self.broadcast("jog", {"active": False, "reason": reason})
        if reason:
            self.log.info("点动释放（%s）" % reason)
        return True

    def jog_active(self):
        return self._jog_task is not None and not self._jog_task.done()

    # ==================================================================
    #                          工具
    # ==================================================================
    def _schedule(self, coro):
        """把协程排进事件循环（回调里不能 await，用这个）。"""
        try:
            loop = asyncio.get_event_loop()
            return loop.create_task(coro)
        except RuntimeError:
            return None

    def stats(self):
        """给 /api/status 用的汇总。"""
        subs = []
        for sub in self.subscribers.values():
            subs.append({
                "connId": sub.conn_id,
                "user": sub.user,
                "role": sub.role,
                "topics": sorted(int(t) for t in sub.topics),
                "queued": sub.queue.qsize(),
                "dropped": sub.dropped,
                "ageMs": _now_ms() - sub.created_ms,
            })
        return {
            "upstream": self.upstream.status(),
            "subscribers": subs,
            "subscriberCount": len(self.subscribers),
            "desiredTopics": sorted(int(t) for t in self.desired_topics()),
            "jog": {
                "active": self.jog_active(),
                "reverse": bool(self._jog_reverse),
                "given": dict(self._jog_given or {}),
                "count": self.jog_count,
                "owner": list(self._jog_owner) if self._jog_owner else None,
            },
            "lastPublishMs": self._last_publish_ms,
            "alarms": {"total": self.alarms.total,
                       "active": len(self.alarms.active())},
        }


def _alarm_level(kind):
    """阀告警类型 → 界面级别（与 AlarmLog 的口径一致）。"""
    try:
        kind = int(kind)
    except (TypeError, ValueError):
        return "warn"
    if kind == proto.VALVE_ALARM_CLEARED:
        return "clear"
    if kind == proto.VALVE_ALARM_CONFLICT:
        return "fault"
    return "warn"


def _int_or(value, default=0):
    """宽松转 int（BSON 里 bool 也是 int 的子类，这里显式排除）。

    [2026-11-13 实锅] _feed_auto_alarm 一经开张就调本函数，而本模块
    此前**没有定义也没有导入**它 ⇒ 每收到一条 TOPIC_AUTO_ALARM 帧
    （急停/阀超时等自动运行告警）_on_publish 都 NameError 崩一拍，
    AlarmLog 与报警页从此永远看不到自动告警（webui_check.py 定向实验
    实锤：webui 日志 `on_publish 异常：name '_int_or' is not defined`）。
    实现与 model._int_or 同口径。
    """
    if isinstance(value, bool):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return default
