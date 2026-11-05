# -*- coding: utf-8 -*-
"""WebHub：网页服务器的中枢 —— 一条上游链路，扇出给所有浏览器。

职责一览
========

    1. 持有唯一的 XLinkUpstream（登录、重连都在里面）
    2. 收推送 → 解 BSON → 落到 AppModel → 扇出事件给所有 SSE 订阅者
    3. **订阅收敛**：把所有浏览器的"想要的主题"并集起来，只把差集发给服务端
    4. 请求代理：浏览器的 POST 转成 XLink 请求，统一做闸门预检与超时兜底
    5. 点动手持：浏览器按住点动时，网页服务器**代为**每 200ms 重发
    6. 采样落盘：按节拍采样历史 + 健康边沿记录

为什么订阅要收敛
================

Qt 客户端是「切页才订阅、隐藏即退订」：17 个页面不会同时订 6 个主题。
网页版有多个浏览器，每个的当前页不同。若各自直接控制上游订阅，
A 离开数字量页会退订数字量 —— 而 B 正停在数字量页，于是 B 的界面
停在 `--` 不动（服务端不再推）。所以必须按**并集**收敛：

    desired = union(每个 SSE 连接的 topics)
    上游 set_topics(desired)      ← 只发差集，无变化的主题不动

[收敛延迟] 切页时先订新的再退旧的（差值都是 add），中间没有空档。
若某个浏览器断线（SSE 关闭），它的 topics 从并集里移除，
下一个收敛拍就把没人要的主题退掉 —— 故收敛不是即时的，而是由一个
200ms 的巡检任务驱动。这带来一个可接受的副作用：断线后最迟 200ms
才退订（多推几帧无害），比"立刻退订导致别人断数据"安全得多。

为什么点动要在服务端代持
========================

点动是"按住才动、松手即停"的动作，客户端每 200ms 重发一帧，
服务端 600ms 收不到就自动停机（`proto.JOG_TIMEOUT_MS`）。
浏览器的按下/抬起事件走 HTTP，若由浏览器自己起定时器重发：

  · 页面被切到后台时浏览器会节流定时器（Chrome 后台定时器最快 1 次/分钟），
    按住不放的阀会在 600ms 后自己停 —— 用户以为按钮坏了
  · 网络抖动/代理超时会让重发断档，同样误停

故网页版把"按住"变成一个**会话级状态**：浏览器 POST jog/start，
网页服务器自己起 200ms 任务重发；POST jog/stop 才停。这样点动的
安全看门狗由网页服务器这条稳定链路维持，与浏览器是否在前台无关。

[兜底] 即使浏览器崩溃没发 stop，网页服务器也在 `JOG_MAX_MS`（缺省 10s）
后自动停 —— 不能让一个崩掉的页面把电机一直点着。
"""

import asyncio
import time

from . import bson
from . import model as model_mod
from . import proto
from . import store as store_mod
from .xlink import AuthRejected, RequestTimeout, SessionState, XLinkUpstream


#: 主题 → 事件名（SSE 的 event: 字段 / 内部 kind 名）。
#:
#: [漏一个就会"页面永远没数据"] `_on_publish` 里取不到就**静默丢弃**，
#: 后果是该主题的推送全部消失，页面显示 `--`，而服务端日志明明在发。
#: 客户端就漏过流量计这一条（见 client_py/bridge.py 的注释）。
TOPIC_KIND = {
    proto.TOPIC_VFD_STATUS: "vfd",
    proto.TOPIC_TEMPHUM_STATUS: "temphum",
    proto.TOPIC_AI_STATUS: "analog",
    proto.TOPIC_FLOWMETER_STATUS: "flowmeter",
    proto.TOPIC_DIGITAL_STATUS: "digital",
    proto.TOPIC_DEVICE_HEALTH: "health",
    proto.TOPIC_VALVE_STATUS: "valve",
    proto.TOPIC_VALVE_ALARM: "valve",     # 独立告警主题也归到 valve 事件
}

#: **全局常驻订阅**的主题。
#:
#: [为什么温湿度/健康必须常驻] 顶栏的温湿度/设备健康是 chrome，
#: 任何页面都要显示。早先"只订本页需要的主题"导致数字量页不订温湿度
#: ⇒ 顶栏立刻变成 `--`，用户一眼看出来不对劲（客户端实锅）。
GLOBAL_TOPICS = (
    proto.TOPIC_TEMPHUM_STATUS,
    proto.TOPIC_DEVICE_HEALTH,
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
                 "created_ms", "dropped", "sent_bytes")

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
        self.dropped = 0
        self.sent_bytes = 0

    def offer(self, event, data):
        """非阻塞投递一条事件（队列满则丢最旧）。"""
        item = (event, data)
        while True:
            try:
                self.queue.put_nowait(item)
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

    def __init__(self, host, port, user, password,
                 history_ms=1000, data_dir=None, log=None):
        self.log = log or store_mod.RunLog()
        self.model = model_mod.AppModel()
        self.history = store_mod.HistoryStore(data_dir, sample_ms=history_ms,
                                              max_channels=8)
        self.alarms = store_mod.AlarmLog(data_dir)
        self.upstream = XLinkUpstream(host, port, user, password,
                                      log=self.log.write,
                                      reconnect_interval=2.0)
        self.upstream.on_publish = self._on_publish
        self.upstream.on_state_changed = self._on_state_changed
        self.upstream.on_ready_changed = self._on_ready_changed

        self.subscribers = {}          # conn_id → Subscriber
        self._conn_seq = 0

        #: 最近一次解析出的 complete snapshot（GET /api/state 用）
        self._last_publish_ms = 0

        # ---- 点动状态 ----
        self._jog_task = None
        self._jog_reverse = False
        self._jog_started_ms = 0
        self._jog_owner = None         # (conn_id, user)
        self.jog_count = 0

        # ---- 后台任务 ----
        self._tasks = []

    # ==================================================================
    #                          生命周期
    # ==================================================================
    async def start(self):
        self.log.info("网页服务器中枢启动")
        self.upstream.start()
        loop = asyncio.get_event_loop()
        self._tasks = [
            loop.create_task(self._reconcile_loop()),
            loop.create_task(self._sample_loop()),
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
        self.broadcast("link", {
            "state": state,
            "ready": ready,
            "detail": detail,
            "error": self.upstream.auth_error,
            "role": int(self.upstream.auth_role),
            "user": self.upstream.user,
            "host": self.upstream.host,
            "port": self.upstream.port,
        })

    def _on_ready_changed(self, ready):
        """链路就绪翻转：重连后必须把订阅集合重新下发。

        [为什么在这里重订] 服务端不记跨会话订阅（见 xlink._teardown 的清空），
        断线重连后上游 subscriptions 是空的。若不补，重连后所有页面都不再
        收到推送，而链路状态显示"已连接"——最难查的那种故障。
        """
        if ready:
            self.log.info("上游就绪，重新下发订阅")
            self.model.run_mode_known = False
            self._schedule(self._reconcile_now())
            # 顺便拉一次运行模式与健康（这两个不是全程推送的）。
            self._schedule(self._refresh_mode())
            self._schedule(self._refresh_health())
        else:
            # [断线要清非周期数据] run_mode/health 没有新鲜度保护，
            # 不清就会一直显示断线前的值（界面说"手动可操作"但其实连不上）。
            self.model.reset_live()
            self.broadcast("mode", self.model.snapshot()["mode"])
            self.broadcast("health", self.model.health.to_json())
            self._schedule(self.stop_jog(reason="链路断开", force=True))

    def _on_publish(self, topic, payload, data_type):
        """收到推送（事件循环内）：解包 → 落模型 → 扇出。"""
        kind = TOPIC_KIND.get(topic)
        if kind is None:
            # 未知主题：记一次就够（不要每帧刷日志）。
            self.log.debug("忽略未知主题 0x%08X" % topic)
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
        if kind == "digital":
            index = int(doc.get(proto.KEY_DEV, 0) or 0)
            dev = self.model.digital_state(index)
            dev.apply(doc)
            self.broadcast("digital", dev.to_json())
            return
        if kind == "health":
            self.model.health.apply(doc)
            self.alarms.feed_health(self.model.health)
            self.broadcast("health", self.model.health.to_json())
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
        self._schedule(self._reconcile_now())
        return sub

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
        st["jogActive"] = bool(self._jog_task and not self._jog_task.done())
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

    async def _reconcile(self):
        """把上游订阅集合收敛到 `desired_topics()`。"""
        if not self.upstream.is_ready():
            return
        desired = self.desired_topics()
        if not desired:
            return
        try:
            await self.upstream.set_topics(desired)
        except Exception as exc:                            # noqa: BLE001
            self.log.warn("订阅收敛失败：%s：%s" % (type(exc).__name__, exc))

    # ==================================================================
    #                          采样落盘
    # ==================================================================
    async def _sample_loop(self):
        """按节拍采样历史（HistoryStore 内部再判节流）。"""
        try:
            while True:
                await asyncio.sleep(0.25)
                try:
                    self.history.maybe_sample(self.model)
                except Exception as exc:                    # noqa: BLE001
                    self.log.warn("历史采样异常：%s" % exc)
        except asyncio.CancelledError:
            raise

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
            self.model.set_run_mode(int(doc.get("mode", 0) or 0),
                                    auto=bool(doc.get("auto", False)))
            self.broadcast("mode", self.model.snapshot()["mode"])

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

    async def set_run_mode(self, mode, role):
        """切换运行模式（需 root）。返回服务端应答 dict。"""
        doc = await self.request(proto.CMD_RUN_MODE, {"mode": int(mode)},
                                 timeout=6.0)
        if doc.get("ok"):
            self.model.set_run_mode(int(doc.get("mode", mode) or mode),
                                    auto=bool(doc.get("auto", False)))
            self.broadcast("mode", self.model.snapshot()["mode"])
        return doc

    # ==================================================================
    #                          点动手持
    # ==================================================================
    async def start_jog(self, reverse, conn_id, user):
        """开始点动（浏览器按下）。已在点动则只更新方向。

        [为什么重复按下要容忍] 浏览器的 pressed 事件偶尔会重复触发
        （触屏长按、鼠标抖动）。重复 start 不该报错，只该刷新方向。
        """
        if self._jog_task is not None and not self._jog_task.done():
            self._jog_reverse = bool(reverse)
            return True
        self._jog_reverse = bool(reverse)
        self._jog_owner = (conn_id, user)
        self._jog_started_ms = _now_ms()
        # 先发一帧（不等 200ms），让电机立刻响应"按下"这个动作。
        await self._jog_send()
        self._jog_task = asyncio.ensure_future(self._jog_loop())
        self.broadcast("jog", {"active": True, "reverse": self._jog_reverse})
        self.log.info("点动开始（%s）" % ("反转" if reverse else "正转"))
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
        """发一帧 JOG。失败不打断循环（链路抖一下就断点动更危险）。"""
        try:
            await self.request(proto.CMD_VFD_CONTROL,
                               {"op": proto.OP_JOG,
                                "rev": bool(self._jog_reverse)},
                               timeout=2.0)
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
                "count": self.jog_count,
                "owner": list(self._jog_owner) if self._jog_owner else None,
            },
            "lastPublishMs": self._last_publish_ms,
            "history": self.history.stats(),
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
