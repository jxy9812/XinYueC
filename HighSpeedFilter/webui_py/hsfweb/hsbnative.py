# -*- coding: utf-8 -*-
"""本机能力只读代理（hsb_bridge 动态库）——网页端用。

[为什么网页端要这个] 桌面客户端的「网络设置 → WiFi 设置」卡读的是**本机**
无线状态（适配器枚举 / 当前连接），走 C 动态库 `hsb_bridge`（框架层
XNetworkWiFi，client_py/hsfsrv_client/hsf_native.py 是同一套 ctypes 绑定）。
网页版原先只能显示「--」占位 ⇒ 两端一眼能看出不是同一套 UI。

[只读，不操作] 本模块**只**暴露两个查询：
    · wifi_adapters()   —— 枚举无线适配器
    · wifi_connection() —— 当前连接快照
扫描/连接/断开**不做**：浏览器端不代持本机无线会话（会话句柄在服务端进程
里，浏览器一关就断），操作仍走桌面客户端。

[动态库找不到时的口径] 返回 (None, 原因) 之类的「不可用」信息，**不抛异常**
——网页服务器不能因为缺一个可选动态库而起不来（板端 .so 可能没交叉编译）。
前端据此把该区置灰并说明原因（与参考端「能力位不支持 ⇒ 整区置灰」同款）。
"""

import ctypes
import json
import os


class NativeUnavailable(RuntimeError):
    """hsb_bridge 动态库不可用（本机能力查询不可做）。"""


_DLL = None
_DLL_TRIED = False
_WIFI = None


def _candidate_paths():
    """动态库候选路径（按优先级）。

    ① 环境变量 HSF_BRIDGE_LIB（板端交叉编译的 .so 一般放这里）；
    ② webui_py/hsfweb/bin/（随包分发）；
    ③ 桌面客户端的 bin/（同一仓库里编出来的 hsb_bridge）；
    ④ 仓库根 bin/。
    """
    env = os.environ.get("HSF_BRIDGE_LIB", "").strip()
    if env:
        yield env
    here = os.path.dirname(os.path.abspath(__file__))
    name = "hsb_bridge.dll" if os.name == "nt" else "hsb_bridge.so"
    roots = [
        here,
        os.path.dirname(here),                       # webui_py/
        os.path.join(os.path.dirname(here),
                     "client_py", "hsfsrv_client"),
        os.path.dirname(os.path.dirname(os.path.dirname(here))),
    ]
    for root in roots:
        yield os.path.join(root, "bin", name)
        yield os.path.join(root, name)


def library_path():
    """找到的动态库路径；找不到返回 ""。"""
    for path in _candidate_paths():
        try:
            if path and os.path.isfile(path):
                return path
        except OSError:                                # pragma: no cover
            continue
    return ""


def available():
    """动态库是否就绪。"""
    return bool(library_path())


def _bind(dll):
    """绑定用到的导出符号（argtypes/restype 全部显式声明）。"""
    for func, args, res in (
            ("hsb_wifi_open", [ctypes.POINTER(ctypes.c_void_p)], ctypes.c_int),
            ("hsb_wifi_close", [ctypes.c_void_p], None),
            ("hsb_last_error", [], ctypes.c_char_p),
            ("hsb_free", [ctypes.c_void_p], None),
            ("hsb_wifi_adapters", [ctypes.c_void_p], ctypes.c_void_p),
            ("hsb_wifi_connection",
             [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p)):
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
    if not path:
        raise NativeUnavailable(
            "未找到本机能力动态库 hsb_bridge（板端需交叉编译出 .so，"
            "或用环境变量 HSF_BRIDGE_LIB 指定）")
    try:
        dll = ctypes.CDLL(path)
    except OSError as exc:
        raise NativeUnavailable("加载 %s 失败：%s" % (path, exc))
    _DLL = _bind(dll)
    return _DLL


def last_error():
    """最近一次错误文本（UTF-8；无错误返回空串）。"""
    try:
        dll = _load()
    except NativeUnavailable:
        return ""
    raw = dll.hsb_last_error()
    if not raw:
        return ""
    try:
        return raw.decode("utf-8", "replace")
    except Exception:                                # pragma: no cover
        return ""


def _take_string(ptr):
    """取走 C 侧返回的字符串并释放（C 侧是 malloc 出来的）。"""
    if not ptr:
        return None
    dll = _load()
    try:
        raw = ctypes.string_at(ptr)
    finally:
        dll.hsb_free(ctypes.c_void_p(ptr))
    try:
        return raw.decode("utf-8", "replace")
    except Exception:                                # pragma: no cover
        return raw.decode("utf-8", "replace")


def _enc(text):
    if text is None:
        return None
    return str(text).encode("utf-8")


def _wifi_open():
    """打开无线会话（进程内常驻；失败抛 NativeUnavailable 带原因）。"""
    global _WIFI
    if _WIFI is not None:
        return _WIFI
    dll = _load()
    handle = ctypes.c_void_p()
    rc = dll.hsb_wifi_open(ctypes.byref(handle))
    if rc != 1 or not handle.value:
        raise NativeUnavailable(last_error() or "无线会话打开失败")
    _WIFI = handle.value
    return _WIFI


def wifi_adapters():
    """枚举无线适配器。

    :return: (list, err) —— list 为 [{id,name,desc,index,mode}]；
              失败时 list=[] 且 err 为原因文本。
    """
    try:
        handle = _wifi_open()
    except NativeUnavailable as exc:
        return [], str(exc)
    dll = _load()
    text = _take_string(dll.hsb_wifi_adapters(ctypes.c_void_p(handle)))
    if not text:
        return [], last_error() or "枚举无线适配器失败"
    try:
        data = json.loads(text)
    except ValueError:
        return [], last_error() or "适配器列表解析失败"
    if not isinstance(data, list):
        return [], "适配器列表格式异常"
    return data, ""


def wifi_connection(adapter_id):
    """当前连接快照。

    :param adapter_id: 适配器 id（wifi_adapters 给的）；空串=用第一个。
    :return: (dict, err) —— dict 为 {ssid,mac,signal,secure,state,...}；
              失败时 dict={} 且 err 为原因文本。
    """
    try:
        handle = _wifi_open()
    except NativeUnavailable as exc:
        return {}, str(exc)
    dll = _load()
    if not adapter_id:
        adapters, _err = wifi_adapters()
        if not adapters:
            return {}, "没有可用适配器"
        adapter_id = adapters[0].get("id", "")
    text = _take_string(dll.hsb_wifi_connection(
        ctypes.c_void_p(handle), _enc(adapter_id)))
    if not text:
        return {}, last_error() or "连接状态查询失败"
    try:
        data = json.loads(text)
    except ValueError:
        return {}, last_error() or "连接状态解析失败"
    if not isinstance(data, dict):
        return {}, "连接状态格式异常"
    return data, ""


def snapshot():
    """本机无线只读快照（一次给全：适配器 + 当前连接）。

    :return: dict —— {available, adapters, connection, err}。
              available=False 表示本机能力不可用（前端据此置灰）。
    """
    adapters, err = wifi_adapters()
    out = {
        "available": not err or bool(adapters),
        "adapters": adapters,
        "connection": {},
        "err": err,
    }
    if adapters:
        conn, cerr = wifi_connection(adapters[0].get("id", ""))
        out["connection"] = conn
        if cerr and not err:
            out["err"] = cerr
    return out
