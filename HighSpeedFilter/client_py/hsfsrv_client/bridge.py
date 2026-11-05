# -*- coding: utf-8 -*-
"""Qt 桥接层：把网络线程的数据安全地送到 UI 线程。

[核心问题] asyncio 收包在**网络线程**，Qt 控件只能在 **UI 线程**碰。
跨线程直接改控件会崩或行为不确定（Qt 的硬性要求）。

[做法] 一个 QObject 持有 Qt 信号，信号从网络线程 emit —— Qt 的队列连接
会自动把投递排进 UI 线程的事件队列。**这是唯一安全的跨线程通道**，
不要用「标记变量 + 定时器轮询」替代（那样一定会漏事件或读到撕裂状态）。

[订阅策略] 只在「当前页需要」时订阅对应主题（C 端同款：切页才订阅、
隐藏即退订）。这样链路流量与页面数量解耦——17 页不会同时推 17 份数据。
"""

import json
import os
import sys
import threading
import time

from PyQt5.QtCore import QObject, Qt, QTimer, pyqtSignal

from . import bson, proto
from .model import AppModel


def _dbg(msg):
    """临时调试输出（HSF_CLIENT_DEBUG=1 启用；定位设置页回显问题用）。"""
    if os.environ.get("HSF_CLIENT_DEBUG") == "1":
        print("[dbg %s] %s" % (time.strftime("%H:%M:%S"), msg),
              file=sys.stderr, flush=True)


class LinkBridge(QObject):
    """连接 + 数据桥。

    对外暴露的 Qt 信号（全部在 UI 线程触发）：
        connected(int)          登录成功（携带角色）
        disconnected(str)       断开（携带原因）
        authFailed(str)         登录失败原因
        dataUpdated(str)        某类数据更新（"vfd"/"temphum"/"analog"/
                                "digital"/"health"/"mode"）
        statusMessage(str)      状态栏提示
    """

    connected = pyqtSignal(int)
    disconnected = pyqtSignal(str)
    authFailed = pyqtSignal(str)
    dataUpdated = pyqtSignal(str)
    statusMessage = pyqtSignal(str)
    #: 通用「把可调用对象投递到 UI 线程」信号。
    #: [为什么需要] `bridge.request` 的 on_done/on_error 跑在**网络线程**；
    #: 页面里的回调常常要读控件文字、改按钮状态甚至重建卡片——从非 UI
    #: 线程碰 QWidget 会让 Qt **直接崩进程**（无 Python traceback）。
    #: 用信号 + 队列连接投递，Qt 会把它排到 UI 线程的事件循环里执行。
    #: 用法：`self.bridge.run_in_ui(lambda: self.xxx())`
    _uiCall = pyqtSignal(object)

    #: 主题 → 数据类型名（dataUpdated 的参数）
    #:
    #: [漏一个就会"页面永远没数据"] `_on_publish` 里 `kind = TOPIC_KIND.get()`
    #: 取不到就**静默 return**，所以忘登记的后果是：该主题的推送全部被丢弃，
    #: 页面显示 `--`，而服务端日志明明在发。流量计就漏过这一条。
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
    }

    def __init__(self, parent=None):
        QObject.__init__(self, parent)
        self.model = AppModel()
        self.client = None
        self.user = ""
        self.role = 0
        self.connected_flag = False
        #: 当前已订阅的主题集合（UI 线程读写，网络线程只读快照）。
        self._subscribed = set()
        self._lock = threading.Lock()
        # [跨线程执行] 队列连接：emit 时若在别的线程，Qt 会把调用排进
        # UI 线程的事件循环（不阻塞调用方）。这是页面回调能安全改控件的
        # 唯一正道 —— 直接调 QWidget 会让 Qt 崩进程。
        self._uiCall.connect(self._run_ui_call,
                             Qt.QueuedConnection)

        # 定时器：兜底刷新（推送间隔 100ms，UI 侧 200ms 轮一次足够）。
        # [为什么要定时器] 数字量/健康是「变化才推」，页面若只靠信号触发，
        # 静止时首帧可能久等不到。定时器保证页面至少每 200ms 有一拍视图。
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setInterval(200)
        self._refresh_timer.timeout.connect(self._on_refresh_tick)
        self._refresh_listeners = []

    # ==================================================================
    #                          连接管理
    # ==================================================================
    def connect_to(self, host, port, user, password, timeout=8.0):
        """建链 + 登录（**阻塞**，须在非 UI 线程或接受短暂卡顿）。

        本项目在登录窗里同步调用：登录本来就要等，卡住是预期行为。
        """
        from .xlink import XLinkClient
        self.disconnect_from()
        self.user = user
        self.client = XLinkClient(host, port, user, password,
                                  log=self.statusMessage.emit)
        self.client.on_state_changed = self._on_state
        self.client.on_publish = self._on_publish
        self.client.on_auth_result = self._on_auth_result
        ok, err = self.client.start(timeout)
        if ok:
            # 注意：connected 信号由 _on_state(READY) 统一发出，
            # 这里不重复 emit（重复会让上层收到两次连接事件）。
            self.role = self.client.auth_role
            self.connected_flag = True
            # 登录成功后立刻拉全量健康快照（事件驱动主题，不拉会空着）。
            self._refresh_health()
        else:
            self.connected_flag = False
            self.authFailed.emit(err)
        return ok, err

    def disconnect_from(self):
        self.connected_flag = False
        with self._lock:
            self._subscribed.clear()
        if self.client is not None:
            self.client.close()
            self.client = None
        self._refresh_timer.stop()

    def is_connected(self):
        return self.connected_flag

    def is_auto_mode(self):
        return self.model.is_auto()

    # ==================================================================
    #                      网络线程 → UI 线程
    # ==================================================================
    def _on_state(self, state):
        """状态迁移 → UI 信号。

        [为什么 READY 要在这里也处理] 自动重连成功后状态会重新变成 READY，
        但那条路径不经过 connect_to()（它只在首次登录时调用），所以必须
        在状态回调里把 connected_flag 置回 True 并补发 connected 信号，
        否则界面会永远停在"未连接"（实测：断线 6 秒后重连成功，界面不变）。
        """
        from .xlink import SessionState
        if state == SessionState.READY:
            if not self.connected_flag:
                self.connected_flag = True
                self.role = self.client.auth_role if self.client else 0
                # 重连后订阅表已随旧会话作废，清空以便上层重新 set_topics。
                with self._lock:
                    self._subscribed.clear()
                self.connected.emit(self.role)
            return
        if state == SessionState.DISCONNECTED and self.connected_flag:
            self.connected_flag = False
            self.disconnected.emit("连接已断开")

    def _on_auth_result(self, ok, role, err):
        if not ok:
            self.connected_flag = False
            self.authFailed.emit(err)

    def _on_publish(self, topic, payload, data_type):
        """推送到达（**网络线程**）：解包成 model，再信号通知 UI。

        [为什么在这里就解包] 解 BSON 是纯 CPU 活，放网络线程不影响 UI；
        若把原始字节丢给 UI 再解，17 个页面各解一次就浪费了。
        """
        kind = self.TOPIC_KIND.get(topic)
        if kind is None:
            return
        try:
            doc = bson.decode(payload) if data_type == proto.DATATYPE_BSON else {}
        except ValueError:
            return
        self._apply_doc(kind, doc)
        self.dataUpdated.emit(kind)

    def _apply_doc(self, kind, doc):
        """把解出的 dict 合并进 model（按 kind 分派）。"""
        if kind == "vfd":
            self.model.vfd.apply(doc)
        elif kind == "temphum":
            self.model.temphum.apply(doc)
        elif kind == "analog":
            self.model.analog.apply(doc)
        elif kind == "flowmeter":
            self.model.flowmeter.apply(doc)
        elif kind == "turbidity":
            self.model.turbidity.apply(doc)
        elif kind == "pump":
            self.model.pump.apply(doc)
        elif kind == "digital":
            index = int(doc.get("dev", 0) or 0)
            self.model.digital_state(index).apply(doc)
        elif kind == "health":
            self.model.health.apply(doc)
        elif kind == "valve":
            self.model.valve.apply(doc)

    # ==================================================================
    #                          订阅控制
    # ==================================================================
    def subscribe(self, *topics):
        """订阅指定主题（幂等）。从 UI 线程调用。"""
        if self.client is None:
            return
        with self._lock:
            wanted = set(topics)
            added = wanted - self._subscribed
            self._subscribed |= added
        for topic in added:
            self.client.subscribe_async(topic)

    def unsubscribe(self, *topics):
        if self.client is None:
            return
        with self._lock:
            removed = set(topics) & self._subscribed
            self._subscribed -= removed
        for topic in removed:
            self.client.unsubscribe_async(topic)

    def set_topics(self, topics):
        """把订阅集合收敛到恰好 `topics`（切页时用：一次调用完成增删）。"""
        topics = set(topics)
        if self.client is None:
            return
        with self._lock:
            add = topics - self._subscribed
            drop = self._subscribed - topics
            self._subscribed = topics
        for topic in drop:
            self.client.unsubscribe_async(topic)
        for topic in add:
            self.client.subscribe_async(topic)

    def current_topics(self):
        with self._lock:
            return set(self._subscribed)

    # ==================================================================
    #                          请求（UI 线程发起）
    # ==================================================================
    def request(self, cmd, doc=None, on_done=None, on_error=None, timeout=3.0):
        """发业务请求；回调在**网络线程**，调用方须自行投递回 UI。
        """
        if self.client is None:
            _dbg(">> req cmd=0x%04X 被拒：未连接" % cmd)
            if on_error:
                on_error("未连接")
            return
        payload = b""
        if doc is not None:
            try:
                payload = bson.encode(doc)
            except ValueError as exc:
                if on_error:
                    on_error("载荷编码失败：%s" % exc)
                return
        _dbg(">> req cmd=0x%04X payload=%dB" % (cmd, len(payload)))
        wrapped_done = on_done
        if on_done is not None:
            def wrapped_done(*args):
                _dbg("<< on_done cmd=0x%04X" % cmd)
                on_done(*args)
        wrapped_error = on_error
        if on_error is not None:
            def wrapped_error(*args):
                _dbg("<< on_error cmd=0x%04X: %r" % (cmd, args))
                on_error(*args)
        self.client.request_async(cmd, payload, proto.DATATYPE_BSON, timeout,
                                  wrapped_done, wrapped_error)

    def request_sync(self, cmd, doc=None, timeout=3.0):
        """同步请求（阻塞 UI！只用于登录后拉首帧这类一次性动作）。"""
        if self.client is None:
            return None
        import concurrent.futures
        payload = b""
        if doc is not None:
            payload = bson.encode(doc)
        fut = self.client.call_soon(
            self.client.request(cmd, payload, proto.DATATYPE_BSON, timeout))
        if fut is None:
            return None
        try:
            code, dtype, data = fut.result(timeout=timeout + 1.0)
        except Exception:                               # noqa: BLE001
            return None
        try:
            return bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
        except ValueError:
            return None

    def _refresh_health(self):
        doc = self.request_sync(proto.CMD_DEVICE_HEALTH, timeout=3.0)
        if doc:
            self.model.health.apply(doc)
            self.dataUpdated.emit("health")

    def _apply_mode_doc(self, doc):
        """把模式帧落到 model（**模式位与运行位分开读**）。

        [运行位缺省不改] 老服务端不发 `autoRunning`：此时保持原值（首次为
        None=未知），**不要**折成 False —— 那会在自动模式下显示"自动已停止"，
        是编造出来的故障。
        """
        self.model.run_mode = int(doc.get("mode", 0) or 0)
        self.model.run_mode_known = True
        if "autoRunning" in doc:
            self.model.auto_running = bool(doc.get("autoRunning"))

    def refresh_run_mode(self, on_done=None):
        """查一次运行模式（顶栏模式钮要显示当前状态）。"""
        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            self._apply_mode_doc(doc)
            self.dataUpdated.emit("mode")
            if on_done:
                on_done(doc)
        self.request(proto.CMD_RUN_MODE, None, on_done=_done)

    def set_run_mode(self, mode, on_done=None, on_error=None):
        """切换运行模式（需 root）。"""
        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            if doc.get("ok"):
                self._apply_mode_doc(doc)
                self.dataUpdated.emit("mode")
            if on_done:
                on_done(doc)
        self.request(proto.CMD_RUN_MODE, {"mode": int(mode)}, on_done=_done,
                     on_error=on_error)

    def send_vfd_control(self, doc, on_done=None, on_error=None):
        """下发变频器控制（op/rev/hz/...）。"""
        self.request(proto.CMD_VFD_CONTROL, doc, on_done=on_done,
                     on_error=on_error)

    def send_auto_control(self, on, on_done=None, on_error=None):
        """启停自动运行（需 root + 自动模式）。

        [为什么单独一条] 切模式 ≠ 开始自动运行。前者只是授权，后者才真正
        驱动设备；合成一个动作会让"切个模式"变成"设备动起来"。
        """
        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if doc.get("ok"):
                self._apply_mode_doc(doc)
                self.dataUpdated.emit("mode")
            if on_done:
                on_done(doc)
        self.request(proto.CMD_AUTO_CONTROL, {"on": bool(on)},
                     on_done=_done, on_error=on_error)

    def send_pump_control(self, index, on, on_done=None, on_error=None):
        """下发进水泵开关。

        [逻辑在服务端] 幂等/门禁/回显确认全由服务端判定；客户端只发意图、
        并按应答更新界面。"""
        self.request(proto.CMD_PUMP_CONTROL,
                     {"index": int(index), "on": bool(on)},
                     on_done=on_done, on_error=on_error)

    def config_get(self, on_done=None, on_error=None, timeout=3.0):
        """读服务端配置快照（采集 + 落盘）。

        应答 doc：{"ok","collect":{…},"storage":{…},"storageDbPath":str}。
        [回调约定] on_done 收**已解码的 doc**（本包装负责 bson.decode，
        与 set_run_mode 同口径）——曾经裸传底层 (code,dtype,payload)
        三元组，页面按 doc 收包直接 TypeError，且异常被网络线程吞掉、
        界面毫无反应（用户反馈"点了保存没生效"的根因）。
        """
        if on_error is None:
            on_error = lambda e: self.statusMessage.emit(  # noqa: E731
                "配置请求失败：%s" % e)

        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if on_done:
                on_done(doc)

        self.request(proto.CMD_CONFIG_GET, None, on_done=_done,
                     on_error=on_error, timeout=timeout)

    def config_set(self, section, values, on_done=None, on_error=None,
                   timeout=3.0):
        """提交一节配置（"collect" / "storage"；**需 root**）。

        values 是**只含被改键**的 dict —— 服务端按出现的键逐个应用，
        没出现的键保持原值（部分更新，不是整节覆盖）。on_done 收已解码
        应答 doc（约定同 config_get）。
        """
        if on_error is None:
            on_error = lambda e: self.statusMessage.emit(  # noqa: E731
                "配置请求失败：%s" % e)
        payload = dict(values or {})
        payload["section"] = str(section)

        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if on_done:
                on_done(doc)

        self.request(proto.CMD_CONFIG_SET, payload, on_done=_done,
                     on_error=on_error, timeout=timeout)

    def send_digital_control(self, dev, on_done=None, on_error=None,
                             channel=None, on=False, mask=None):
        """下发 DO 控制：channel+on 单点，或 mask 整组。"""
        payload = {"dev": int(dev)}
        if mask is not None:
            payload["mask"] = int(mask)
        else:
            payload["do"] = int(channel)
            payload["on"] = bool(on)
        self.request(proto.CMD_DIGITAL_CONTROL, payload, on_done=on_done,
                     on_error=on_error)

    # ==================================================================
    #                        兜底刷新
    # ==================================================================
    def add_refresh_listener(self, fn):
        """注册「周期刷新」回调（UI 线程，每 200ms 一次）。

        页面用它做「数据没变也要重画的量」——比如表格里的 "--" 占位
        在数据到达后必须重绘，纯信号驱动会漏。
        """
        if fn not in self._refresh_listeners:
            self._refresh_listeners.append(fn)

    def remove_refresh_listener(self, fn):
        if fn in self._refresh_listeners:
            self._refresh_listeners.remove(fn)

    def start_refresh(self):
        self._refresh_timer.start()

    def stop_refresh(self):
        self._refresh_timer.stop()

    # ==================================================================
    #                    跨线程投递（页面回调必用）
    # ==================================================================
    def run_in_ui(self, fn):
        """把一个可调用对象**排到 UI 线程**执行（不阻塞调用方）。

        [为什么必须用它] `request`/`send_*` 的 on_done/on_error 都在**网络
        线程**执行。回调里若读控件文字（`label.text()`）、改按钮态
        （`setEnabled`）或重建卡片，就是从非 UI 线程碰 QWidget ——
        **Qt 直接崩进程，没有 Python traceback**。
        实测症状：点"自由停车"后界面立刻消失、服务端会话一起断，
        日志里查不到任何异常（因为进程是被 signal/SIGSEGV 干掉的）。

        :param fn: 无参可调用对象；异常会被吞掉并打到状态栏，
                   不能让一个页面的回调炸掉整个客户端。
        """
        self._uiCall.emit(fn)

    def _run_ui_call(self, fn):
        """在 UI 线程真正执行 run_in_ui 投递过来的函数。"""
        try:
            fn()
        except Exception as exc:                        # noqa: BLE001
            self.statusMessage.emit("界面回调异常：%s" % exc)

    def _on_refresh_tick(self):
        for fn in list(self._refresh_listeners):
            try:
                fn()
            except Exception as exc:                    # noqa: BLE001
                # 单个页面的刷新异常不得拖垮整个界面。
                self.statusMessage.emit("刷新异常：%s" % exc)
