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

from . import bson, permissions, proto
from . import model as model_mod
from .camera_frames import CameraAssembler
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
        proto.TOPIC_AUTO_ALARM: "autoalarm",
        # 相机画面（2026-11-19）：**RAW 二进制**载荷（分片），不是 BSON
        # ⇒ 走单独分支，不进 BSON 解码路径。
        proto.TOPIC_CAMERA_FRAME: "camera",
    }

    #: **常驻订阅**的主题（连接期订、重连补订；C 端 HsfLinkClient
    #: hsfLcSubscribeTopics 的 TOPIC_AUTO_ALARM 同款）。
    #:
    #: [为什么不进 `_subscribed`] `_subscribed` 是页面订阅表——切页
    #: `set_topics` 会把它收敛成「当前页需要的主题」。自动运行告警是
    #: **事件流**（报警发生在哪个页面不可预知，漏采不可补），必须全天候
    #: 采集 ⇒ 只直接对 client 发订阅帧，**不进页面集合**，切页收敛、
    #: 退订永不碰它（`set_topics` 只会退订 `_subscribed` 里有的主题）。
    #: [重连补订] 服务端订阅登记在**每条会话**上（xlink `_teardown` 清
    #: `subscriptions`），重连后旧订阅已不存在 ⇒ 每次 READY 迁移都经
    #: `_subscribe_persistent` 重发（`_subscribed` 的清空+上层重订只覆盖
    #: 页面主题，不覆盖这里）。
    #: [2026-11-17] 常驻订阅**清空**：报警数据一律向服务端要（0x011A）⇒
    #: 客户端不再本地记自动告警，也就不必常驻订阅那条事件流。
    PERSISTENT_TOPICS = ()

    def __init__(self, parent=None):
        QObject.__init__(self, parent)
        self.model = AppModel()
        self.client = None
        #: 相机分片组帧器（2026-11-19）：画面经服务端切片推来（XLink 单帧
        #: 上限 16KB）⇒ 客户端在网络线程里组帧，UI 只取整帧字节贴图。
        self._camera_asm = CameraAssembler()
        #: [2026-11-19 ES 直传] NAL 解码线程（懒建：第一个 ES 分片组好时）。
        #: `_camera_hw` 来自远端设置 camera 节的 hwDecode（auto/on/off），
        #: 页面回包时经 `set_camera_hw` 更新。
        self._es_decoder = None
        self._camera_hw = proto.CAMERA_HW_DEF
        self.user = ""
        self.role = 0
        self.connected_flag = False
        #: [退出竞态闸 2026-11-09] True = disconnect_from 已启动：网络线程
        #  的一切 Qt 信号发射立即静默（Qt 对象可能正在被主线程析构）。
        self._shutting_down = False
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
        # [退出竞态闸必须重新开闸] disconnect_from() 会把 _shutting_down
        # 置 True（那是"真退出"的语义），但这里只是**换一条连接**：
        # 若不重新开闸，网络线程发的 connected 信号会被 _emit_net 静默
        # 吞掉 ⇒ MainWindow._on_connected 永不执行 ⇒ 首屏页面的
        # set_topics 永远不调用 ⇒ 首屏无主题订阅（卡片全是 "--"），
        # 切页才恢复（切页走 goto_page 里的 set_topics 直连，不经信号）。
        # 实锤：2026-11-09 首屏温湿度/搅拌泵/压差/流量计/电磁阀全 "--"，
        # 唯独设备健康有数（那是一次性快照请求，不靠订阅）。
        self._shutting_down = False
        self.user = user
        # [log 走 _emit_net 2026-11-16 审计] 网络线程的内部日志（"帧头
        # 非法"/"连接失败" 等）原来直接 `statusMessage.emit`——绕过
        # `_emit_net` 的退出静默闸：close() join 超时后线程成僵尸时，
        # 解释器收尾阶段仍可能 emit ⇒ pyqtBoundSignal UAF 家族。⇒ 统一
        # 走 `_emit_net`（退出期静默）。
        self.client = XLinkClient(host, port, user, password,
                                  log=lambda msg:
                                  self._emit_net(self.statusMessage, msg))
        # [旧会话的迟到回调必须忽略 2026-11-09]
        # 连接重建时，旧 client 的拆链沿还会回调状态（DISCONNECTED）⇒ 把
        # connected_flag 翻成 False ⇒ 界面假"未连接"（实测：重连刚成功就被
        # 旧会话的迟到沿打掉；connect_to() 会重新开 _shutting_down，旧会话
        # 的 teardown 信号因此不再被静默）。
        # 故把三个回调都按 client 身份绑：非当前 client 的事件一律丢弃
        # （旧会话的迟到推送/认证结果同理，否则会写进新会话的 model）。
        _client = self.client
        _client.on_state_changed = (
            lambda st, c=_client: self._on_state(st, c))
        _client.on_publish = (
            lambda topic, payload, data_type, c=_client:
            self._on_publish(topic, payload, data_type, c))
        _client.on_auth_result = (
            lambda ok, role, err, c=_client:
            self._on_auth_result(ok, role, err, c))
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
            self._emit_net(self.authFailed, err)
        return ok, err

    def disconnect_from(self):
        # [退出竞态闸] 必须在关 client 之前置位：close 触发的断线/重连
        # 状态迁移信号全部静默，杜绝「主线程拆卸中、网络线程还在 emit」
        # 的段错误（pyqtBoundSignal_emit UAF，2026-11-09 根修，台账 §六）。
        self._shutting_down = True
        self.connected_flag = False
        with self._lock:
            self._subscribed.clear()
        if self.client is not None:
            self.client.auto_reconnect = False
            self.client.close()
            self.client = None
        self._refresh_timer.stop()

    def logout(self):
        """退出登录（**不退出进程**）：断开 + 清账户态 + 通知界面回「未登录」。

        [为什么不能直接调 disconnect_from()] 那是「真退出/拆卸」语义：
          · 置 `_shutting_down=True` ⇒ 后续一切网络线程信号被 `_emit_net`
            静默，退出后到重登之间界面收不到任何状态（退出登录不是拆卸，
            进程还在跑、还要接着弹登录窗）；
          · 且它**不发 `disconnected`** ⇒ 状态栏会永远停在
            「已连接 · root · 管理员」（现场看着像没退出去）。
        本方法复用同一套拆链动作，但保持 `_shutting_down=False`，并在 UI
        线程直接发 `disconnected("已退出登录")` 让 MainWindow 把账户刷成
        「未登录」。`connect_to()` 重开闸的逻辑（首行 disconnect_from →
        重开 False）在「退出 → 切换账户 → 重登」链路上仍成立。

        [先断网再动 UI] 顺序与 `_teardown` 一致：先 close client，再 emit
        信号，避免断网线程的迟到回调打到正在改控件的 UI（Py 退出三闸）。
        """
        self.connected_flag = False
        with self._lock:
            self._subscribed.clear()
        client = self.client
        self.client = None
        if client is not None:
            client.auto_reconnect = False
            client.close()
        try:
            self._refresh_timer.stop()
        except Exception:                                   # noqa: BLE001
            pass
        self.user = ""
        self.role = 0
        # [自动态复位 2026-11-16 审计] 同 _on_state(DISCONNECTED)：换账户/
        # 换服务端后，上一台的运行态/累计时长/告警缓冲不该带过来。
        m = self.model
        m.run_mode_known = False
        m.auto_running = None
        m.auto_mode = ""
        m.auto_phase = ""
        m.auto_fault = None
        m.auto_run_ms_known = False
        m.auto_stage_known = False
        # [最近报警 2026-11-19] 换账户后不该带上一台服务端的报警 ⇒ 清。
        m.last_alarm = None
        m.last_alarm_known = False
        # [不发 _shutting_down] 退出登录 ≠ 退出进程；保持 False 让随后
        # connect_to() 的 connected 信号与界面刷新正常到达。
        self.disconnected.emit("已退出登录")

    def _emit_net(self, signal, *args):
        """网络线程发 Qt 信号的唯一入口：关闭流程中静默。

        [为什么需要] request/push 的回调都在网络线程执行，`signal.emit`
        是从非 UI 线程触碰桥对象——正常期 Qt 自动排队没问题，但退出
        拆卸期主线程的 QObject 已析构，emit 就是 UAF（实测段错误，
        core 栈 pyqtBoundSignal_emit ← asyncio 网络线程）。
        """
        if self._shutting_down:
            return
        signal.emit(*args)

    def is_connected(self):
        return self.connected_flag

    def can(self, perm):
        """界面侧权限判定（与服务端 permissions.py 同源缺省表的镜像）。

        只决定按钮/入口能不能用；权威判定始终在服务端。角色值不单调
        （Operator=3 是追加值），故此函数按位查表，禁止在外面写
        ``role >= ROLE_*`` 比较。
        """
        return permissions.can_role(int(self.role or 0), int(perm))

    def is_auto_mode(self):
        return self.model.is_auto()

    # ==================================================================
    #                      网络线程 → UI 线程
    # ==================================================================
    def _on_state(self, state, client=None):
        """状态迁移 → UI 信号。

        [为什么 READY 要在这里也处理] 自动重连成功后状态会重新变成 READY，
        但那条路径不经过 connect_to()（它只在首次登录时调用），所以必须
        在状态回调里把 connected_flag 置回 True 并补发 connected 信号，
        否则界面会永远停在"未连接"（实测：断线 6 秒后重连成功，界面不变）。
        """
        from .xlink import SessionState
        # [旧会话的迟到事件] 非当前 client 的状态迁移一律忽略。
        if client is not None and client is not self.client:
            return
        if state == SessionState.READY:
            if not self.connected_flag:
                self.connected_flag = True
                self.role = self.client.auth_role if self.client else 0
                # 重连后订阅表已随旧会话作废，清空以便上层重新 set_topics。
                with self._lock:
                    self._subscribed.clear()
                # 常驻主题不进 _subscribed（页面收敛管不到它）⇒ 重订不能
                # 等上层 set_topics，必须在这里自己补（首连同路径覆盖）。
                self._subscribe_persistent()
                self._emit_net(self.connected, self.role)
            return
        if state == SessionState.DISCONNECTED and self.connected_flag:
            self.connected_flag = False
            # [自动态/known 标志复位 2026-11-16 审计] 这些标志"只置位从
            # 不清"⇒ 断链期间首页/顶栏继续断言旧状态（"自动 · 运行中 ·
            # 产水模式…"——服务端那边早停了；板端 WiFi 弱链反复掉线的
            # 窗口里全是这种假状态）。⇒ 断线即回"未知"，重连后由快照
            # 重新置位；同时清自动告警缓冲（新会话不该带上一会话的）。
            m = self.model
            m.run_mode_known = False
            m.auto_running = None
            m.auto_mode = ""
            m.auto_phase = ""
            m.auto_fault = None
            m.auto_run_ms_known = False
            m.auto_stage_known = False
            # [最近报警 2026-11-19] 断链 ⇒ 报警摘要也回"未知"（留着上一台
            # 服务端的报警会误导：那是**别的机器**上发生的事）。
            m.last_alarm = None
            m.last_alarm_known = False
            self._emit_net(self.disconnected, "连接已断开")

    def _on_auth_result(self, ok, role, err, client=None):
        if client is not None and client is not self.client:
            return
        if not ok:
            self.connected_flag = False
            self._emit_net(self.authFailed, err)

    def _subscribe_persistent(self):
        """订阅常驻主题（`PERSISTENT_TOPICS`；READY 迁移时调用）。

        [线程] `_on_state` 在**网络线程**回调；`subscribe_async` 内部经
        `run_coroutine_threadsafe` 投递，任意线程可调（与页面路径经
        `set_topics`→`subscribe_async` 同一通道）。刻意**不碰
        `_subscribed`**：那是页面订阅表，进了它切页收敛就会把常驻主题
        退订掉（见 PERSISTENT_TOPICS 注释）。幂等性由 xlink 端
        `subscriptions` 去重集合保证（同会话重复订阅不发重复帧）。
        """
        client = self.client
        if client is None:
            return
        for topic in self.PERSISTENT_TOPICS:
            client.subscribe_async(topic)

    def _on_publish(self, topic, payload, data_type, client=None):
        if client is not None and client is not self.client:
            return
        """推送到达（**网络线程**）：解包成 model，再信号通知 UI。

        [为什么在这里就解包] 解 BSON 是纯 CPU 活，放网络线程不影响 UI；
        若把原始字节丢给 UI 再解，17 个页面各解一次就浪费了。
        """
        kind = self.TOPIC_KIND.get(topic)
        if kind is None:
            return
        if kind == "camera":
            # [相机画面走单独分支] 载荷是 **RAW 二进制**（分片）⇒ 不能进
            # BSON 解码。组帧在**网络线程**里做（纯 CPU，不碰 UI）。
            frame = self._camera_asm.feed(payload)
            if frame is None:
                return
            flags = self._camera_asm.last_flags
            if flags & proto.CAM_FRAME_FLAG_ES:
                # [ES 直传 2026-11-19] 组出的是**编码流 NAL**（不是 JPEG）
                # ⇒ 喂解码线程（PyAV 独立线程，见 esplayer.py），解出
                # RGB 后在回调里 emit。服务端不解码 ⇒ 帧率 20fps、
                # 服务端 CPU 归零（架构转向）。
                if self._es_decoder is None:
                    from .esplayer import EsDecoder
                    self._es_decoder = EsDecoder(self._on_es_frame,
                                                 self._on_es_note)
                self._es_decoder.configure(
                    bool(flags & proto.CAM_FRAME_FLAG_H265), self._camera_hw)
                self._es_decoder.feed(frame)
                return
            self.model.camera.set_frame(frame, self._camera_asm.width,
                                        self._camera_asm.height)
            self.model.camera.dropped = self._camera_asm.dropped
            self._emit_net(self.dataUpdated, "camera")
            return
        try:
            doc = bson.decode(payload) if data_type == proto.DATATYPE_BSON else {}
        except ValueError:
            return
        self._apply_doc(kind, doc)
        self._emit_net(self.dataUpdated, kind)

    # -- ES 直传解码回调（解码线程直调；只发信号不碰控件） ----------------

    def _on_es_frame(self, rgb, width, height):
        """解出一帧 RGB（解码线程）：进 model + 通知 UI。"""
        if self._shutting_down:
            return
        self.model.camera.set_es_frame(rgb, width, height)
        self._emit_net(self.dataUpdated, "camera")

    def _on_es_note(self, text):
        """解码方式切换/失败原因（解码线程）→ 状态栏。"""
        if self._shutting_down:
            return
        self._emit_net(self.statusMessage, "相机：%s" % text)

    def set_camera_hw(self, mode):
        """远端设置回包里的 hwDecode（auto/on/off）→ 解码器按需重建。"""
        mode = str(mode or "").strip()
        if mode not in proto.CAMERA_HW_CHOICES:
            return
        self._camera_hw = mode
        if self._es_decoder is not None:
            self._es_decoder.configure(None, mode)

    def _apply_doc(self, kind, doc):
        """把解出的 dict 合并进 model（按 kind 分派）。"""
        # [诊断 2026-11-06] 现场出现「设备健康显示正常，但卡片一直是 --」。
        # 那种现象只有两种成因，一条日志就能分开：
        #   · 卡片收不到帧      → 这里根本不打（或很稀疏）
        #   · 收到帧但值/有效位空 → 打出的 doc 里 vm 无位 / 没有 temp，
        #                         / tempValid=false，一眼可见
        # 只对最容易出问题、体积又小的两个主题打，避免刷屏。
        if kind in ("vfd", "temphum"):
            _dbg("pub %s keys=%s vm=%s temp=%s valid=%s"
                 % (kind, ",".join(sorted(doc.keys()))[:90],
                    doc.get("vm"), doc.get("temp"),
                    doc.get("tempValid")))
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
            # [2026-11-17] 这里**不再**做本地边沿检测：报警历史一律向
            # 服务端要（0x011A）⇒ 本地不留副本（原先本地边沿 + 服务端
            # 历史两套并行，正是两端条数对不上的根源）。
        elif kind == "valve":
            self.model.valve.apply(doc)
        elif kind == "autoalarm":
            # [2026-11-17] 自动运行告警**不再**在客户端本地落表、也不再
            # 发信号：服务端已把这条告警写进 alarms 表 ⇒ 报警页按 0x011A
            # 取历史就能拿到（单一数据源，客户端重启不丢、两端口径一致）。
            # 键缺省兜 0/""：坏帧不给界面造一条空告警之外的惊喜。
            _kind = int(doc.get(proto.AUTOALARM_KEY_KIND, 0) or 0)
            _at = int(doc.get(proto.AUTOALARM_KEY_AT_MS, 0) or 0)
            _text = str(doc.get(proto.AUTOALARM_KEY_TEXT) or "")
            # [设备类别 2026-11-15] 缺键（老服务端）⇒ -1 =
            # "按自动运行处理"（报警页落表时用）。
            _dev = int(doc.get(proto.AUTOALARM_KEY_DEV, -1)
                       if doc.get(proto.AUTOALARM_KEY_DEV) is not None
                       else -1)

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
        # [先判活再建协程 2026-11-16 审计] `self.client.request(...)`
        # 是协程——实参求值时**协程对象已创建**；loop 已死时 call_soon
        # 把它原样丢弃 ⇒ "coroutine ... was never awaited"（与
        # request_async/subscribe_async 上一轮修的同一坑，这里漏改）。
        if not getattr(self.client, "_alive", lambda: True)():
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
            self._emit_net(self.dataUpdated, "health")

    def _apply_mode_doc(self, doc):
        """把模式帧落到 model（**模式位与运行位分开读**）。

        [运行位缺省不改] 老服务端不发 `autoRunning`：此时保持原值（首次为
        None=未知），**不要**折成 False —— 那会在自动模式下显示"自动已停止"，
        是编造出来的故障。
        [自动模式三键同口径 2026-11-11] autoMode/autoPhase/autoFault 缺键
        保持原值（老服务端兼容）。
        """
        self.model.run_mode = int(doc.get("mode", 0) or 0)
        self.model.run_mode_known = True
        if "autoRunning" in doc:
            self.model.auto_running = bool(doc.get("autoRunning"))
        if proto.KEY_AUTO_MODE in doc:
            self.model.auto_mode = str(doc.get(proto.KEY_AUTO_MODE) or "")
        if proto.KEY_AUTO_PHASE in doc:
            self.model.auto_phase = str(doc.get(proto.KEY_AUTO_PHASE) or "")
        if proto.KEY_AUTO_FAULT in doc:
            self.model.auto_fault = bool(doc.get(proto.KEY_AUTO_FAULT))
        if proto.KEY_AUTO_RUN_MS in doc:
            self.model.auto_run_ms = int(doc.get(proto.KEY_AUTO_RUN_MS) or 0)
            self.model.auto_run_ms_known = True
        # [子阶段计时 2026-11-12] 两键同进同退：老服务端一个都不发 ⇒
        # auto_stage_known 保持 False ⇒ 界面显示「--」，不倒推假倒计时。
        if (proto.KEY_AUTO_STAGE_ELAPSED_MS in doc
                or proto.KEY_AUTO_STAGE_TOTAL_MS in doc):
            self.model.auto_stage_elapsed_ms = int(
                doc.get(proto.KEY_AUTO_STAGE_ELAPSED_MS, -1))
            self.model.auto_stage_total_ms = int(
                doc.get(proto.KEY_AUTO_STAGE_TOTAL_MS, 0) or 0)
            self.model.auto_stage_known = True
        # [最近报警 2026-11-19] 主页「最近报警」卡的一行摘要。**缺键不改
        # 原值**（老服务端不发 ⇒ last_alarm 保持 None ⇒ 界面显示
        # 「无报警」）；带了键但值是 null ⇒ 也是 None（服务端说"没有报警"
        # 就是没有，不编一条出来）。
        if proto.KEY_LAST_ALARM in doc:
            raw = doc.get(proto.KEY_LAST_ALARM)
            self.model.last_alarm = self._parse_last_alarm(raw)
            self.model.last_alarm_known = True

    @staticmethod
    def _parse_last_alarm(raw):
        """服务端 lastAlarm 文档 → model 的 dict（None = 无报警/坏数据）。

        [为什么客户端再解析一遍] 网络层只该收**结构化**数据；字段取舍
        与缺省由客户端定（与服务端 app._last_alarm_doc 同口径：dev 缺
        键 = 无设备类别，不补 0 —— 0 是"变频器"，补 0 会撒谎）。

        [typeText 在 model 层算好] 「类型」文案由 model.alarm_type_text
        算好放进 typeText ⇒ 界面直接显示，不必在每个界面里各抄一份映射表
        （抄一份 = 迟早漂移）。网页端同款（后端算好下发）。
        """
        if not isinstance(raw, dict):
            return None
        text = str(raw.get(proto.LAST_ALARM_KEY_TEXT) or "")
        at_ms = int(raw.get(proto.LAST_ALARM_KEY_AT_MS) or 0)
        if not text and not at_ms:
            return None          # 空文档 = 无报警
        kind = str(raw.get(proto.LAST_ALARM_KEY_KIND) or "")
        dev = raw.get(proto.LAST_ALARM_KEY_DEV)
        if dev is not None:
            try:
                dev = int(dev)
            except (TypeError, ValueError):
                dev = None
        out = {
            "kind": kind,
            "text": text,
            "atMs": at_ms,
            "typeText": model_mod.alarm_type_text(kind, dev),
        }
        if dev is not None:
            out["dev"] = dev
        return out

    def refresh_run_mode(self, on_done=None):
        """查一次运行模式（顶栏模式钮要显示当前状态）。"""
        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            self._apply_mode_doc(doc)
            self._emit_net(self.dataUpdated, "mode")
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
                self._emit_net(self.dataUpdated, "mode")
            if on_done:
                on_done(doc)
        self.request(proto.CMD_RUN_MODE, {"mode": int(mode)}, on_done=_done,
                     on_error=on_error)

    def send_vfd_control(self, doc, on_done=None, on_error=None):
        """下发变频器控制（op/rev/hz/...）。"""
        self.request(proto.CMD_VFD_CONTROL, doc, on_done=on_done,
                     on_error=on_error)

    def send_auto_control(self, on, on_done=None, on_error=None, mode=None):
        """启停自动运行（需 root + 自动模式）。

        :param mode: 启动时携带的自动模式 id（produce/backwash/rinse/auto）；
            停止不用带。服务端校验模式配置（未配置设备/**组合循环配置不完整**
            都回明确错因），客户端只发意图。

        [为什么单独一条] 切模式 ≠ 开始自动运行。前者只是授权，后者才真正
        驱动设备；合成一个动作会让"切个模式"变成"设备动起来"。
        """
        doc = {"op": proto.AUTO_OP_SELECT} if on is None else {"on": bool(on)}
        if mode:
            doc[proto.KEY_AUTO_MODE] = str(mode)

        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if doc.get("ok"):
                self._apply_mode_doc(doc)
                self._emit_net(self.dataUpdated, "mode")
            if on_done:
                on_done(doc)
        self.request(proto.CMD_AUTO_CONTROL, doc,
                     on_done=_done, on_error=on_error)

    def send_auto_select(self, mode, on_done=None, on_error=None):
        """只**选择**自动模式（op=5；记粘性+落盘，不启动、不动设备）。

        [2026-11-13 板端实锅] 现场启动按钮用「上次选中的」（粘性）；此前
        客户端下拉切换不与服务端同步 ⇒ 服务端粘性为空/过期，物理启动键
        被拒「未选择自动模式」。主界面模式下拉在 activated 时调本方法，
        使服务端粘性与界面一致（服务端持久化，重启也恢复）。
        """
        doc = {"op": proto.AUTO_OP_SELECT,
               proto.KEY_AUTO_MODE: str(mode)}

        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if doc.get("ok"):
                self._apply_mode_doc(doc)
                self._emit_net(self.dataUpdated, "mode")
            if on_done:
                on_done(doc)
        self.request(proto.CMD_AUTO_CONTROL, doc,
                     on_done=_done, on_error=on_error)

    def send_auto_reset(self, on_done=None, on_error=None):
        """复位自动运行锁存（清报警锁存 + 急停锁存；**不启动**）。

        [为什么需要按钮] 2026-11-12 用户口径：「有故障报警的时候可以按
        复位」——此前客户端只能发 {"on":bool}（启动/停止），表达不了复位，
        复位只能靠现场按钮组的物理复位按钮 ⇒ 故障锁死时操作员远程无解。
        服务端 op=4 的语义：解两把锁（急停锁存 + 模式序列报警锁存），
        回停止态。

        [注意] 服务端把"复位"也归在 PERM_AUTO_CONTROL 权限闸下
        （自动运行状态属运行决策）⇒ 非操作者/管理员按钮是灰的。
        """
        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if doc.get("ok"):
                self._apply_mode_doc(doc)
                self._emit_net(self.dataUpdated, "mode")
            if on_done:
                on_done(doc)

        self.request(proto.CMD_AUTO_CONTROL, {proto.KEY_OP: proto.AUTO_OP_RESET},
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
            on_error = lambda e: self._emit_net(self.statusMessage,   # noqa: E731
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
            on_error = lambda e: self._emit_net(self.statusMessage,   # noqa: E731
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

    def history_page(self, offset=0, limit=None, before_ts=None, lo_ts=None,
                     hi_ts=None, kw=None, kw_cols=None, export_mode=False,
                     on_done=None, on_error=None, timeout=5.0):
        """分页取服务端历史数据（**只读，无需 root**）。

        :param offset: 跳过多少行（0 = 最新）
        :param limit:  本页行数（客户端说了算；服务端夹到 1..50）
        :param before_ts: 分页窗口锚点。**首屏发一次、翻页原样回传**，
            否则新入库的行会把 offset 顶偏，相邻两页重复/漏行。
        :param lo_ts/hi_ts: 时间范围（毫秒时间戳；推到 SQL 里筛，
            保证 total 与页内容同口径）。
        :param kw: 关键字（下推服务端，在显示文本上匹配）。
        :param kw_cols: 关键字要比对的显示列下标（None/空 = 全部列）。
        on_done 收已解码的 doc（约定同 config_get）：{"ok","total",
        "rows":[[…]],"cols":[…],"dbRows","latestTs",…}。

        [为什么关键字也下推] 分页在服务端而筛选在客户端时，客户端只能在
        拿到的那一页里搜；命中行绝大多数在别的页上，界面表现就是"关键字
        筛选无效"（用户实测反馈）。总数与页内容必须共用同一条 WHERE。

        [为什么超时给 5s] 这比普通快照重（SQL 排序+计数+LIKE、几 KB 载荷），
        板端 CPU 忙时 3s 不够；但也不能无限等——界面有"正在加载"态。
        """
        if limit is None:
            limit = proto.HISTORY_PAGE_DEFAULT
        doc = {proto.HIST_KEY_OFFSET: max(0, int(offset)),
               proto.HIST_KEY_LIMIT: int(limit)}
        if before_ts is not None:
            doc[proto.HIST_KEY_BEFORE_TS] = int(before_ts)
        if lo_ts is not None:
            doc[proto.HIST_KEY_LO_TS] = int(lo_ts)
        if hi_ts is not None:
            doc[proto.HIST_KEY_HI_TS] = int(hi_ts)
        if kw:
            doc[proto.HIST_KEY_KW] = str(kw)
        if kw_cols:
            doc[proto.HIST_KEY_KW_COLS] = [int(c) for c in kw_cols]
        if export_mode:
            # [下载权限] 导出意图上服务端验权（界面灰按钮拦不住直接发帧）。
            doc[proto.HIST_KEY_EXPORT] = True
        if on_error is None:
            on_error = lambda e: self._emit_net(self.statusMessage,   # noqa: E731
                "历史数据请求失败：%s" % e)

        def _done(code, dtype, data):
            try:
                got = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                got = {}
            if on_done:
                on_done(got)

        self.request(proto.CMD_HISTORY_PAGE, doc, on_done=_done,
                     on_error=on_error, timeout=timeout)

    def log_page(self, offset=0, limit=None, before_ts=None, lo_ts=None,
                 hi_ts=None, category=None, kw=None,
                 on_done=None, on_error=None, timeout=5.0):
        """分页查询服务端运行日志（CMD_LOG_PAGE=0x0119；**C 端扩展**）。

        :param offset: 跳过多少行（0 = 最新一段；时间 DESC 取页、页内正序）。
        :param limit:  本页行数（客户端说了算；服务端夹到 1..50）。
        :param before_ts: 分页窗口锚点。**新查询发一次、翻页原样回传**——
            缺省/0 = 服务端取"现在"，但那样翻页时窗口会跟着走 ⇒ 客户端
            在发包前把锚定格成自己的"现在"再发送（C 端 logRequest 同口径）。
        :param lo_ts/hi_ts: 时间窗（epoch 毫秒；推到 SQL 里筛，total 与
            页内容共用同一条 WHERE，与 0x0118 同机制）。
        :param category: 类别过滤（conn/ctrl/alarm/auto/auth；None/空=全部）。
        :param kw: 关键字（下推服务端，text+category 列 LIKE 匹配）。
        on_done 收已解码的 doc：{"ok","rows":[{ts,category,text}…],
        "total","count","offset","limit"}；offset/limit 由服务端原样回显，
        客户端做在途应答归属判定（防串页，与 history_page 同机制）。

        [为什么 server_py 连不上时本方法没有用] server_py 不实现 0x0119，
        老服务端会回 {"ok":false,"err":"未知命令"}——页面按否定应答提示，
        不会崩。
        """
        if limit is None:
            limit = proto.LOG_PAGE_DEFAULT
        doc = {proto.KEY_LOG_OFFSET: max(0, int(offset)),
               proto.KEY_LOG_LIMIT: int(limit)}
        if before_ts is not None and int(before_ts) > 0:
            doc[proto.KEY_LOG_BEFORE_TS] = int(before_ts)
        if lo_ts is not None and int(lo_ts) > 0:
            doc[proto.KEY_LOG_LO_TS] = int(lo_ts)
        if hi_ts is not None and int(hi_ts) > 0:
            doc[proto.KEY_LOG_HI_TS] = int(hi_ts)
        if category:
            doc[proto.KEY_LOG_CATEGORY] = str(category)
        if kw:
            doc[proto.KEY_LOG_KW] = str(kw)
        if on_error is None:
            on_error = lambda e: self._emit_net(self.statusMessage,   # noqa: E731
                "运行日志请求失败：%s" % e)

        def _done(code, dtype, data):
            try:
                got = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                got = {}
            if on_done:
                on_done(got)

        self.request(proto.CMD_LOG_PAGE, doc, on_done=_done,
                     on_error=on_error, timeout=timeout)

    def alarm_page(self, offset=0, limit=None, before_ts=None, lo_ts=None,
                   hi_ts=None, kw=None, on_done=None, on_error=None,
                   timeout=5.0):
        """分页查询服务端**报警历史**（CMD_ALARM_PAGE=0x011A，2026-11-17 新增）。

        :param offset: 跳过多少行（0 = 最新一段；时间 DESC 取页、页内正序）。
        :param limit:  本页行数（服务端夹到 1..50）。
        :param before_ts: 分页窗口锚点（新查询发一次、翻页原样回传）。
        :param lo_ts/hi_ts: 时间窗（epoch 毫秒）。
        :param kw: 关键字（text/source/kind 三列 LIKE）。
        on_done 收已解码的 doc：{"ok","rows":[{ts,kind,source,text,dev}…],
        "total","count","offset","limit"}。

        [为什么报警页要向服务端取] 报警记录原先只存在**各客户端进程内存里**
        （客户端自己算健康边沿）⇒ 客户端一重启历史全丢，而网页端（常驻
        服务）看得到开机以来的全部 ⇒ 用户实报「web 端报警有数据，客户端
        报警视图没数据」。⇒ 边沿改由服务端记进 alarms 表，两端都按本命令
        取**同一份**（单一数据源）。
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
        if on_error is None:
            on_error = lambda e: self._emit_net(   # noqa: E731
                self.statusMessage, "报警记录请求失败：%s" % e)

        def _done(code, dtype, data):
            try:
                got = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                got = {}
            if on_done:
                on_done(got)

        self.request(proto.CMD_ALARM_PAGE, doc, on_done=_done,
                     on_error=on_error, timeout=timeout)

    def vfd_params(self, op=proto.VFD_PARAMS_OP_READ, values=None,
                   on_done=None, on_error=None, timeout=5.0):
        """读/写变频器功能码参数（CMD_VFD_PARAMS=0x011B，2026-11-18 新增）。

        :param op: proto.VFD_PARAMS_OP_READ(0)=读整组 / WRITE(1)=写若干项。
        :param values: op=1 时要写的 {addr(int): raw(int)}——**原始寄存器
            值**，工程值→raw 的标度换算按 proto.PARAM_DEFS 的 scale 在
            调用方完成；线上键是 addr 的**十进制字符串**（BSON 文档键只能
            是字符串，与服务端解析口径一致）。
        :param on_done: 收**已解码的应答 doc**（约定同 config_get）：
            {"ok","err"?,"params":[{code,label,addr,raw,value,unit,
            writable,runLocked,applied?}…],"vfdFaulted","runState"}。
            **params[].value 是服务端从驱动器回读的工程值**（写后服务端
            重新读驱动器再返回）⇒ 页面只认应答里的 value，绝不显示本地
            提交值。
        :param on_error: 网络层失败（超时/断链）。服务端**业务拒绝**
            （RunLocked/离线/权限）走 ok=false + err，仍进 on_done——
            页面把 err 原样显示到状态行，不改写文案。

        [为什么超时给 5s] 与 history_page/alarm_page 同口径：op=0 要把
        0xF100 段逐个从驱动器读回来（Modbus 一问一答，板端 CPU 忙时
        3s 不够）；op=1 更重（写 + 回读确认）。
        """
        doc = {proto.KEY_OP: int(op)}
        if values:
            doc[proto.KEY_VFDP_VALUES] = {
                str(int(addr)): int(raw) for addr, raw in values.items()}
        if on_error is None:
            on_error = lambda e: self._emit_net(   # noqa: E731
                self.statusMessage, "变频器参数请求失败：%s" % e)

        def _done(code, dtype, data):
            try:
                got = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                got = {}
            if on_done:
                on_done(got)

        self.request(proto.CMD_VFD_PARAMS, doc, on_done=_done,
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
            self._emit_net(self.statusMessage, "界面回调异常：%s" % exc)

    def _on_refresh_tick(self):
        for fn in list(self._refresh_listeners):
            try:
                fn()
            except Exception as exc:                    # noqa: BLE001
                # 单个页面的刷新异常不得拖垮整个界面。
                self._emit_net(self.statusMessage, "刷新异常：%s" % exc)
