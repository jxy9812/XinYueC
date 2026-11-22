# -*- coding: utf-8 -*-
"""本机能力桥（WiFi + 屏幕电源）的 ctypes 封装。

[为什么走 C 动态库] 「WiFi 网络设置」与「本地设置·窗口与屏幕·屏幕休眠」
两块界面只在本机生效：WiFi 要调平台无线 API（Windows = Native Wifi / Linux =
nmcli），熄屏/唤醒要调平台电源 API（Win32 = SC_MONITORPOWER / Linux =
DPMS）。仓库里这两块能力都已在框架层落地——无线业务层
`Src/XCore/XCode/XNetwork/XNetworkWiFi/`、平台电源契约
`Src/XGui/Platform/XPlatformPower.h`——桥接层
`client_py/tools/hsf_bridge/hsf_bridge.c`（CMake 目标 `hsb_bridge`）把
它们折成**扁平 C ABI**，本模块用 ctypes 直接调。

[库缺失怎么办] `available()` 返回 False → 界面把对应区**置灰并说明怎么编**
（`.zcode/build_hsf_bridge.bat`），**不许崩、不许留死控件**。
`require()` 抛 `NativeUnavailable`（可捕获）。

[错误口径] C 侧失败时错误文本在 `hsb_last_error()`（线程局部）。
本模块的动作类方法返回 True/False，失败原因读 `last_error()`；
轮询类方法（扫描状态 / 连接快照）失败时返回「安全的失败值」（-1 / None），
**不抛异常**——轮询里抛异常会把界面打断在半路。

[行列/单位口径] 时间一律毫秒（ms）；能力位/唤醒来源是位掩码。
"""

import ctypes
import json
import os

#: 能力位（与 C 侧 XPlatformPowerCapability 同值）。
CAP_SCREEN_CONTROL = 1 << 0
CAP_SCREEN_TIMEOUT = 1 << 1
CAP_SYSTEM_SUSPEND = 1 << 2

#: 唤醒来源位（与 C 侧 XPlatformPowerInputMask 同值）。
INPUT_MOUSE = 1 << 0
INPUT_KEYBOARD = 1 << 1
INPUT_TOUCH = 1 << 2
INPUT_ALL = 0xFF

#: 扫描状态（与 C 侧 hsb_wifi_scan_state 同值）。
SCAN_IDLE = 0
SCAN_RUNNING = 1
SCAN_COMPLETED = 2
SCAN_FAILED = 3

#: 轮询失败哨兵（区别于「空闲」0）。
SCAN_ERROR = -1

_DLL = None
_DLL_TRIED = False


class NativeUnavailable(RuntimeError):
    """hsb_bridge 动态库未编译/未找到（本机能力不可用）。"""


def _bin_dir():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin")


def library_path():
    """动态库路径（不保证存在——用 available() 判断）。"""
    env = os.environ.get("HSF_BRIDGE_LIB")
    if env:
        return env
    name = "hsb_bridge.dll" if os.name == "nt" else "hsb_bridge.so"
    return os.path.join(_bin_dir(), name)


def available():
    """动态库是否就绪（界面据此启用/置灰本机能力区）。"""
    return os.path.isfile(library_path())


def _bind(dll):
    """绑定全部导出符号（argtypes/restype 全部显式声明）。"""
    for func, args, res in (
            ("hsb_wifi_open", [ctypes.POINTER(ctypes.c_void_p)], ctypes.c_int),
            ("hsb_wifi_close", [ctypes.c_void_p], None),
            ("hsb_last_error", [], ctypes.c_char_p),
            ("hsb_free", [ctypes.c_void_p], None),
            ("hsb_wifi_adapters", [ctypes.c_void_p], ctypes.c_void_p),
            ("hsb_wifi_refresh_adapters", [ctypes.c_void_p],
             ctypes.c_void_p),
            ("hsb_wifi_start_scan", [ctypes.c_void_p, ctypes.c_char_p],
             ctypes.c_int),
            ("hsb_wifi_scan_state", [ctypes.c_void_p, ctypes.c_char_p],
             ctypes.c_int),
            ("hsb_wifi_take_scan_results", [ctypes.c_void_p, ctypes.c_char_p],
             ctypes.c_void_p),
            ("hsb_wifi_connect",
             [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
              ctypes.c_char_p], ctypes.c_int),
            ("hsb_wifi_disconnect", [ctypes.c_void_p, ctypes.c_char_p],
             ctypes.c_int),
            ("hsb_wifi_connection", [ctypes.c_void_p, ctypes.c_char_p],
             ctypes.c_void_p),
            ("hsb_power_capabilities", [], ctypes.c_uint),
            ("hsb_power_is_screen_off", [], ctypes.c_int),
            ("hsb_power_set_screen_off", [ctypes.c_int], ctypes.c_int),
            ("hsb_power_get_policy", [ctypes.c_char_p, ctypes.c_int],
             ctypes.c_int),
            ("hsb_power_set_policy",
             [ctypes.c_longlong, ctypes.c_longlong, ctypes.c_uint],
             ctypes.c_int),
            ("hsb_power_request_permission", [], ctypes.c_int),
            ("hsb_power_tick", [], ctypes.c_int),
            ("hsb_power_note_activity", [ctypes.c_uint], ctypes.c_int),
            # [2026-11-16] 二维码（框架 XRcode 的扁平面；Web 设置页画访问
            # 地址用）。矩阵指针在下次 encode/destroy 前有效 ⇒ 本模块的
            # qr_matrix() 会立刻整块拷成 bytes 再返回。
            ("hsb_qr_encode", [ctypes.c_char_p, ctypes.c_int], ctypes.c_int),
            ("hsb_qr_size", [], ctypes.c_int),
            ("hsb_qr_matrix", [], ctypes.POINTER(ctypes.c_ubyte)),
            ("hsb_qr_destroy", [], None)):
        fn = getattr(dll, func)
        fn.argtypes = args
        fn.restype = res
    return dll


def _load():
    """懒加载动态库（进程内一次；失败后不再重试）。"""
    global _DLL, _DLL_TRIED
    if _DLL is not None:
        return _DLL
    if _DLL_TRIED:
        raise NativeUnavailable("hsb_bridge 动态库不可用")
    _DLL_TRIED = True
    path = library_path()
    if not os.path.isfile(path):
        raise NativeUnavailable(
            "未找到本机能力动态库 %s（用 .zcode/build_hsf_bridge.bat 编译，"
            "板端需交叉编译出 .so）" % path)
    try:
        dll = ctypes.CDLL(path)
    except OSError as exc:
        raise NativeUnavailable("加载 %s 失败：%s" % (path, exc))
    _DLL = _bind(dll)
    return _DLL


def require():
    """确保动态库可用；不可用时抛 NativeUnavailable（界面捕获后置灰）。"""
    _load()
    return _DLL


class HsbBridge(object):
    """hsb_bridge 的薄封装（单实例即可；会话句柄由本类管理）。"""

    def __init__(self):
        self._dll = None
        self._wifi = None
        self._last_error = ""

    # ---------------- 底层 ----------------

    def _lib(self):
        if self._dll is None:
            self._dll = _load()
        return self._dll

    def last_error(self):
        """最近一次错误文本（UTF-8；无错误返回空串）。"""
        try:
            dll = self._lib()
        except NativeUnavailable:
            return self._last_error
        raw = dll.hsb_last_error()
        if not raw:
            return self._last_error
        try:
            return raw.decode("utf-8", "replace")
        except Exception:                                # pragma: no cover
            return self._last_error

    def _fail(self, fallback=""):
        text = self.last_error()
        self._last_error = text
        return text or fallback

    def _take_string(self, ptr):
        """取走 C 侧返回的字符串并释放（C 侧是 malloc 出来的）。"""
        if not ptr:
            return None
        dll = self._lib()
        try:
            raw = ctypes.string_at(ptr)
        finally:
            dll.hsb_free(ctypes.c_void_p(ptr))
        try:
            return raw.decode("utf-8", "replace")
        except Exception:                                # pragma: no cover
            return raw.decode("utf-8", "replace")

    @staticmethod
    def _enc(text):
        if text is None:
            return None
        return str(text).encode("utf-8")

    # ---------------- WiFi ----------------

    def wifi_available(self):
        """无线会话是否已打开（打开失败时给出错误文本）。"""
        return self._wifi is not None

    def wifi_open(self):
        """打开无线会话。

        :return: True=已打开；False=失败（原因见 last_error()）。
        """
        if self._wifi is not None:
            return True
        dll = self._lib()
        handle = ctypes.c_void_p()
        rc = dll.hsb_wifi_open(ctypes.byref(handle))
        if rc != 1 or not handle.value:
            self._fail("无线会话打开失败")
            return False
        self._wifi = handle.value
        return True

    def wifi_close(self):
        """关闭无线会话（幂等）。"""
        if self._wifi is None:
            return
        dll = self._lib()
        dll.hsb_wifi_close(ctypes.c_void_p(self._wifi))
        self._wifi = None

    def wifi_adapters(self):
        """枚举无线适配器。

        :return: [{id, name, desc, index, mode}]；失败返回 None。

        [注意] C 侧**缓存**了这份列表：平台的适配器枚举会复位平台的扫描
        状态（Windows 驱动重建适配器槽），若每次轮询都重新枚举，扫描
        永远收不到「完成」。要强制刷新用 `wifi_refresh_adapters()`。
        """
        if self._wifi is None:
            self._fail("无线会话未打开")
            return None
        dll = self._lib()
        text = self._take_string(dll.hsb_wifi_adapters(
            ctypes.c_void_p(self._wifi)))
        if not text:
            self._fail("枚举无线适配器失败")
            return None
        try:
            data = json.loads(text)
        except ValueError:
            self._fail("适配器列表解析失败")
            return None
        return data if isinstance(data, list) else []

    def wifi_refresh_adapters(self):
        """强制重新枚举适配器（丢弃 C 侧缓存）。

        :return: [{id, name, desc, index, mode}]；失败返回 None。

        [什么时候用] 用户想刷新适配器列表时。**不要**在扫描轮询里调用
        ——重新枚举会复位平台侧的扫描状态。
        """
        if self._wifi is None:
            self._fail("无线会话未打开")
            return None
        dll = self._lib()
        text = self._take_string(dll.hsb_wifi_refresh_adapters(
            ctypes.c_void_p(self._wifi)))
        if not text:
            self._fail("枚举无线适配器失败")
            return None
        try:
            data = json.loads(text)
        except ValueError:
            self._fail("适配器列表解析失败")
            return None
        return data if isinstance(data, list) else []

    def wifi_start_scan(self, adapter_id):
        """发起扫描（发起即返回；结果靠 wifi_scan_state/wifi_take_scan_results）。"""
        if self._wifi is None:
            self._fail("无线会话未打开")
            return False
        dll = self._lib()
        rc = dll.hsb_wifi_start_scan(ctypes.c_void_p(self._wifi),
                                     self._enc(adapter_id))
        if rc != 1:
            self._fail("扫描发起失败")
            return False
        return True

    def wifi_scan_state(self, adapter_id):
        """轮询扫描状态（0 空闲 / 1 扫描中 / 2 完成 / 3 失败；出错 -1）。"""
        if self._wifi is None:
            self._fail("无线会话未打开")
            return SCAN_ERROR
        dll = self._lib()
        state = dll.hsb_wifi_scan_state(ctypes.c_void_p(self._wifi),
                                        self._enc(adapter_id))
        if state < 0:
            self._fail("扫描状态查询失败")
            return SCAN_ERROR
        return int(state)

    def wifi_take_scan_results(self, adapter_id):
        """取走扫描结果。

        :return: [{ssid, mac, signal, channel, secure, needPass}]；
                 失败（未完成/无结果）返回 None。
        """
        if self._wifi is None:
            self._fail("无线会话未打开")
            return None
        dll = self._lib()
        text = self._take_string(dll.hsb_wifi_take_scan_results(
            ctypes.c_void_p(self._wifi), self._enc(adapter_id)))
        if not text:
            self._fail("扫描未完成或没有结果")
            return None
        try:
            data = json.loads(text)
        except ValueError:
            self._fail("扫描结果解析失败")
            return None
        return data if isinstance(data, list) else []

    def wifi_connect(self, adapter_id, ssid, password=""):
        """发起连接（发起即返回；结果靠 wifi_connection 的 state 收口）。"""
        if self._wifi is None:
            self._fail("无线会话未打开")
            return False
        dll = self._lib()
        rc = dll.hsb_wifi_connect(ctypes.c_void_p(self._wifi),
                                   self._enc(adapter_id), self._enc(ssid),
                                   self._enc(password or ""))
        if rc != 1:
            self._fail("连接发起失败")
            return False
        return True

    def wifi_disconnect(self, adapter_id):
        """断开当前连接。"""
        if self._wifi is None:
            self._fail("无线会话未打开")
            return False
        dll = self._lib()
        rc = dll.hsb_wifi_disconnect(ctypes.c_void_p(self._wifi),
                                     self._enc(adapter_id))
        if rc != 1:
            self._fail("断开失败")
            return False
        return True

    def wifi_connection(self, adapter_id):
        """当前连接快照。

        :return: {ssid, mac, signal, secure, state, stateCode}；
                 失败返回 None。
        """
        if self._wifi is None:
            self._fail("无线会话未打开")
            return None
        dll = self._lib()
        text = self._take_string(dll.hsb_wifi_connection(
            ctypes.c_void_p(self._wifi), self._enc(adapter_id)))
        if not text:
            self._fail("连接状态查询失败")
            return None
        try:
            data = json.loads(text)
        except ValueError:
            self._fail("连接状态解析失败")
            return None
        return data if isinstance(data, dict) else None

    # ---------------- 屏幕电源 ----------------

    def power_capabilities(self):
        """本平台电源能力位（0=无任何能力）。"""
        dll = self._lib()
        return int(dll.hsb_power_capabilities())

    def power_is_screen_off(self):
        """当前屏幕是否处于熄屏状态。"""
        dll = self._lib()
        return bool(dll.hsb_power_is_screen_off())

    def power_set_screen_off(self, off):
        """立即熄屏（off=True）/ 唤醒（off=False）。"""
        dll = self._lib()
        rc = dll.hsb_power_set_screen_off(1 if off else 0)
        if rc != 1:
            self._fail("屏幕开关失败")
            return False
        return True

    def power_get_policy(self):
        """回读电源策略。

        :return: {screenOffMs, systemSuspendMs, wakeInputs}；失败返回 None。
        """
        dll = self._lib()
        buf = ctypes.create_string_buffer(512)
        rc = dll.hsb_power_get_policy(buf, ctypes.sizeof(buf))
        if rc != 1:
            self._fail("电源策略读取失败")
            return None
        try:
            data = json.loads(buf.value.decode("utf-8", "replace"))
        except ValueError:
            self._fail("电源策略解析失败")
            return None
        return data if isinstance(data, dict) else None

    def power_set_policy(self, screen_off_ms, system_suspend_ms, wake_inputs):
        """设置电源策略（毫秒；0=从不；wake_inputs 是位掩码）。"""
        dll = self._lib()
        rc = dll.hsb_power_set_policy(ctypes.c_longlong(int(screen_off_ms)),
                                      ctypes.c_longlong(int(system_suspend_ms)),
                                      ctypes.c_uint(int(wake_inputs)))
        if rc != 1:
            self._fail("电源策略设置失败")
            return False
        return True

    def power_request_permission(self):
        """请求「设置自动熄屏时长」所需的平台授权（安卓引导到系统设置）。"""
        dll = self._lib()
        rc = dll.hsb_power_request_permission()
        if rc != 1:
            self._fail("本平台无需授权，或授权发起失败")
            return False
        return True

    def power_tick(self):
        """推进一次电源巡检（空闲超时 → 熄屏 / 挂起）。

        [为什么要它] 框架把「空闲多久做什么」的判定放在
        `XPlatformPower_tick()`，由**应用层的周期定时器**调用（C 端是
        XGuiApplication 的 1Hz 定时器）。桌面 Python 客户端不跑
        XGuiApplication ⇒ 没人调它，「自动熄屏时长」就选了不兑现。
        本方法把框架原语原样暴露出来，由客户端的 1Hz 定时器调用。

        :return: True=已推进；False=失败（原因读 last_error()）。
        """
        dll = self._lib()
        rc = dll.hsb_power_tick()
        if rc != 1:
            self._fail("电源巡检失败")
            return False
        return True

    def power_note_activity(self, input_mask):
        """上报一次用户活动（重置空闲计时；熄屏态按唤醒来源决定是否唤醒）。

        :param input_mask: 位掩码（INPUT_MOUSE / INPUT_KEYBOARD /
            INPUT_TOUCH 可按位或）。
        """
        dll = self._lib()
        rc = dll.hsb_power_note_activity(ctypes.c_uint(int(input_mask)))
        if rc != 1:
            self._fail("用户活动上报失败")
            return False
        return True


#: 进程内共享实例（界面各页共用一个会话）。
_BRIDGE = None


def bridge():
    """取共享桥实例（动态库缺失时抛 NativeUnavailable，界面捕获后置灰）。"""
    global _BRIDGE
    if _BRIDGE is None:
        _BRIDGE = HsbBridge()
    return _BRIDGE


# ============================================================================
#                    二维码（框架 XRcode；Web 设置页用）
# ============================================================================

#: 纠错级别（与 XRcode.h 的 level 同值）。**框架只内置 L 与 H**
#: （M/Q 的表项未内置，传入即编码失败——见 XRcode.c 的 m_qrcode_info 表）。
QR_LEVEL_L = 0
QR_LEVEL_M = 1      # 未内置（保留常量，便于将来框架补齐后直接用）
QR_LEVEL_Q = 2      # 未内置
QR_LEVEL_H = 3

#: 缺省级别：**H**（约 30% 冗余）。现场是手机对着屏幕/机身标签扫，反光、
#: 角度、打印褪色都会吃掉容错；内容只有 20~40 字节，H 级也只到版本 4，
#: 面板上依然清楚。
QR_LEVEL_DEFAULT = QR_LEVEL_H


def qr_encode(text, level=QR_LEVEL_DEFAULT):
    """把文本编成二维码，返回行优先的模块字节串（每字节 0/1）。

    :returns: ``(size, data)``：size = 边长（模块数），data = size*size 字节，
              行优先，1 = 黑。
    :raises NativeUnavailable: 动态库缺失（界面据此置灰并提示怎么编）。
    :raises ValueError: 内容为空/过长，或框架不支持的纠错级（原因取自
              ``hsb_last_error``）。
    """
    dll = require()
    raw = (str(text or "")).encode("utf-8")
    if not raw:
        raise ValueError("二维码内容为空")
    n = int(dll.hsb_qr_encode(raw, int(level)))
    if n <= 0:
        err = dll.hsb_last_error()
        raise ValueError((err or b"\xe7\xbc\x96\xe7\xa0\x81\xe5\xa4\xb1\xe8"
                          b"\xb4\xa5").decode("utf-8", "replace"))
    ptr = dll.hsb_qr_matrix()
    if not ptr:
        raise ValueError("二维码矩阵为空")
    # [立刻整块拷走] C 侧的矩阵缓冲在下次 encode/destroy 前有效；ctypes
    # 指针自己不会延长它的寿命 ⇒ string_at 拷成 bytes 再交给界面。
    return n, ctypes.string_at(ctypes.cast(ptr, ctypes.c_void_p), n * n)
