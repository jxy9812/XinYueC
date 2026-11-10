# -*- coding: utf-8 -*-
"""本机客户端设置：**只影响这一台机器**的界面偏好（QSettings 持久化）。

[为什么单开一个模块] 这些项以前散在各页里、而且大多**没接线**（改了没效果：
`compact_check` / `scale_combo` / `refresh_spin` 改了不生效也不落盘）。集中
一处后：① 键名单一事实源，不会两页各写一个；② 启动时统一还原（`__main__`
只调一次 apply_startup）；③ 测试能拿临时 INI 隔离验证，不碰真注册表。

[存哪里 —— 2026-11-10 用户定案：**程序目录的 INI，不用注册表**]
  `<程序目录>/cfg/hsf_client_py.ini`
  · 程序目录 = 本包 `hsfsrv_client/` 的上一层（板端就是
    `/root/Desktop/XinYueC/hsf_client_py/`；桌面就是 `HighSpeedFilter/client_py/`）；
  · 与 C 端 `bin/cfg/hsf_client.ini` 同思路：**文件随程序走**，拷目录即带走
    配置；服务端与客户端部署在同一目录时，它就与 `hsf_data.db` 并排在 `cfg/`。
  · 之前用平台缺省 QSettings（Windows 注册表 HKCU\\Software\\XinYueC），
    卸载/换用户管理麻烦、也查不到文件 —— 用户明确要求改掉。
  · 登录窗的 host/port/user/remember 也在这份 INI 里（原来在注册表）。

[测试/多实例隔离] 环境变量 `HSF_CLIENT_SETTINGS_INI` 指向某个 .ini 时，
全部读写都走那份文件。回归测试靠它把自己写的东西关在临时目录里。
"""
import os

from PyQt5.QtCore import QSettings

ORG = "XinYueC"
APP = "HsfClientPy"

#: 覆盖设置存储路径的环变量（空/未设 = 程序目录下 cfg/hsf_client_py.ini）。
ENV_SETTINGS_INI = "HSF_CLIENT_SETTINGS_INI"
#: 设置文件名（放 <程序目录>/cfg/ 下）。
SETTINGS_FILE_NAME = "hsf_client_py.ini"

#: 界面缩放百分比（100/110/125/150）。**重启后生效**：QSS 与各页字号在
#: 建窗时按比例算好（theme.font_px），运行期改只能重建整棵树，故只存不改。
K_UI_SCALE_PCT = "uiScalePct"
#: 强制紧凑模式（原「紧凑模式」勾选；缺省 false=按窗宽自动判 <860）。
K_FORCE_COMPACT = "forceCompact"
#: 启动即全屏（原「全屏显示」勾选）。
K_FULLSCREEN = "fullscreen"
#: 主界面刷新周期（毫秒；主窗 refresh 定时器）。
K_REFRESH_MS = "refreshMs"
#: 实时趋势图记录间隔（毫秒；趋势卡「刷新」下拉）。
K_TREND_MS = "trendSampleMs"
#: 窗口尺寸 "宽x高"（应用窗口设置后记住；缺省空=用 theme 缺省尺寸）。
K_WIN_SIZE = "winSize"
#: 悬浮窗设置：标题栏 / 状态栏 / 左侧导航显隐。
K_SHOW_TITLE = "showTitleBar"
K_SHOW_STATUS = "showStatusBar"
K_SHOW_NAV = "showNav"
#: 历史页 / 日志页每页条数。
K_HIST_PAGE_SIZE = "histPageSize"
K_LOG_PAGE_SIZE = "logPageSize"

_DEFAULTS = {
    K_UI_SCALE_PCT: 0,          # 0 = 不覆盖（用 theme.ui_scale() 的缺省/环境变量）
    K_FORCE_COMPACT: False,
    K_FULLSCREEN: False,
    K_REFRESH_MS: 200,
    K_TREND_MS: 200,
    K_WIN_SIZE: "",
    K_SHOW_TITLE: True,
    K_SHOW_STATUS: True,
    K_SHOW_NAV: True,
    K_HIST_PAGE_SIZE: 0,        # 0 = 各页自己的缺省
    K_LOG_PAGE_SIZE: 0,
}


def program_dir():
    """程序目录 = 本包目录的上一层（板端 `/root/Desktop/XinYueC/hsf_client_py`）。"""
    here = os.path.dirname(os.path.abspath(__file__))   # .../hsfsrv_client
    return os.path.dirname(here)


def settings_path():
    """设置文件绝对路径（环境变量 > 程序目录/cfg/hsf_client_py.ini）。"""
    env = os.environ.get(ENV_SETTINGS_INI, "").strip()
    if env:
        return env
    return os.path.join(program_dir(), "cfg", SETTINGS_FILE_NAME)


def _store():
    """当前设置后端：程序目录下的 INI（**不用注册表**）。

    [写不进去怎么办] 只读安装目录时 `makedirs/setValue` 会失败：`set_value`
    已静默吞异常，读路径会拿到空值⇒回缺省。不 quietly 换到注册表——那正是
    用户要避免的东西。
    """
    path = settings_path()
    try:
        d = os.path.dirname(path)
        if d and not os.path.isdir(d):
            os.makedirs(d, exist_ok=True)
    except OSError:
        pass
    return QSettings(path, QSettings.IniFormat)


def migrate_from_legacy():
    """把旧版（注册表 QSettings("XinYueC","HsfClientPy")）的键搬到 INI 一次。

    [为什么要迁移] 登录记忆（host/port/user/remember）以前写在注册表；
    换成文件后不搬的话现场要重填服务器地址。只在 INI 还没有该键时搬，
    搬完删注册表键（用户明确要求"不要存注册表"）。

    :return: 搬了几个键（0 = 无需迁移）。
    """
    path = settings_path()
    if os.path.exists(path):
        return 0
    try:
        legacy = QSettings(ORG, APP)          # 平台缺省 = 旧位置（注册表/conf）
        keys = [k for k in ("host", "port", "user", "remember")
                if legacy.contains(k)]
    except Exception:                         # noqa: BLE001
        return 0
    if not keys:
        return 0
    store = _store()
    for k in keys:
        store.setValue(k, legacy.value(k))
    store.sync()
    for k in keys:
        legacy.remove(k)
    legacy.sync()
    return len(keys)


def get_int(key, default=0):
    try:
        return int(_store().value(key, default, int))
    except (TypeError, ValueError):
        return int(default)


def get_bool(key, default=False):
    return bool(_store().value(key, default, bool))


def get_str(key, default=""):
    v = _store().value(key, default, str)
    return "" if v is None else str(v)


def set_value(key, value):
    """写一个键（失败静默：设置写不进去不该让界面操作失败）。"""
    try:
        _store().setValue(key, value)
        _store().sync()
    except Exception:                                   # noqa: BLE001
        pass


def ui_scale():
    """界面缩放比例（float）；0/未设置 ⇒ 0.0 表示"不覆盖"。

    返回 0.0 时调用方应保留 `theme.ui_scale()`（环境变量 HSF_UI_SCALE /
    内置缺省 1.5），这样现场用环境变量调过的不被本机设置顶掉。
    """
    pct = get_int(K_UI_SCALE_PCT, 0)
    if pct <= 0:
        return 0.0
    return max(1.0, min(2.0, pct / 100.0))


def refresh_ms():
    """主界面刷新周期（毫秒）；夹到 100..2000（与本地设置页范围一致）。"""
    return max(100, min(2000, get_int(K_REFRESH_MS, 200)))


def trend_ms():
    """实时趋势记录间隔（毫秒）；非法/缺失 ⇒ 200。"""
    v = get_int(K_TREND_MS, 200)
    return v if v > 0 else 200


def hist_page_size():
    return get_int(K_HIST_PAGE_SIZE, 0)


def log_page_size():
    return get_int(K_LOG_PAGE_SIZE, 0)


def win_size():
    """上次应用的窗口尺寸 (w, h)；没有/非法 ⇒ (0, 0)。"""
    txt = get_str(K_WIN_SIZE, "")
    try:
        w, h = txt.lower().split("x")
        w, h = int(w), int(h)
    except (ValueError, AttributeError):
        return 0, 0
    if w < 400 or h < 300 or w > 10000 or h > 10000:
        return 0, 0
    return w, h
