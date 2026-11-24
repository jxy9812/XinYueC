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
#: 实时趋势图时间范围（毫秒；趋势卡「范围」下拉；2026-11-24）。
K_TREND_RANGE_MS = "trendRangeMs"
#: 窗口尺寸 "宽x高"（应用窗口设置后记住；缺省空=用 theme 缺省尺寸）。
K_WIN_SIZE = "winSize"
#: 悬浮窗设置：标题栏 / 状态栏 / 左侧导航显隐。
K_SHOW_TITLE = "showTitleBar"
#: [页面预热 2026-11-18] 启动后后台逐页预建（不显示）⇒ 第一次点进
#: 某页不再卡（板端实测：首次进入某页 0.03~0.85s，二次 2~3ms）。
#: 代价＝预建期间 UI 线程被占（弱 CPU 一次 0.03~0.85s）。
#: [缺省订正 2026-11-21] 原注释写「默认关」，但 2026-11-19 已改成
#: **真实交互会话缺省开、离屏（offscreen/minimal）缺省关**（见
#: main_window._prewarm_enabled）：现场（触摸屏）反馈「有些界面触摸明显
#: 卡顿」⇒ 预热缺省开才有意义；而离屏预热会把页面提前塞进 pages/stack，
#: 污染回归断言与测量的确定性 ⇒ 离屏关。
K_PREWARM_PAGES = "prewarmPages"
K_SHOW_STATUS = "showStatusBar"
K_SHOW_NAV = "showNav"
#: 悬浮窗设置：标题栏内的**时钟**与**温湿度**显隐（2026-11-13 合并进
#: 本地设置页时由死控件改成真兑现：写设置 + 立刻作用到标题栏 + 启动还原）。
K_SHOW_CLOCK = "showClock"
K_SHOW_TEMPHUM = "showTemphum"
#: [2026-11-22 标题栏新元素同步进本地设置] 二维码徽章（2026-11-18 加）、
#: 运行模式钮（含自动运行徽标）、设备健康指示（2026-11-19 加）此前**没有
#: 显隐开关** ⇒ 本地设置「悬浮窗 → 标题栏内容」只有时钟/温湿度两项。
#: 三项缺省全显；与时钟/温湿度同一条 apply_overlay 通道持久化/还原。
K_SHOW_QR = "showQr"
K_SHOW_MODE = "showMode"
K_SHOW_HEALTH = "showHealth"
#: [2026-11-22 用户定案「顶部状态栏加相机位图 + 本地设置同步」] 标题栏
#: 相机徽章显隐（点徽章弹/收悬浮相机画面卡）。默认显示。
K_SHOW_CAMERA = "showCamera"
#: [2026-11-22 用户定案「点击左上角图标用导航树还是菜单页」] 标题栏 ☰ 的
#: 导航方式（本地设置页「悬浮窗 → 导航方式」）：
#:   · `tree`（缺省）= 保持既有行为：宽屏收起/展开左侧导航树；
#:   · `page` = 打开**当前页所属分类的菜单页**（如设备页 → 「设备 选择子项」），
#:     在菜单页再点 ☰ 回到主菜单；子菜单页有「← 返回主菜单」。
#: 窄屏（紧凑）放不下导航树，两档都落到菜单页（tree 档保持旧口径主菜单）。
K_MENU_BUTTON_STYLE = "menuButtonStyle"
MENU_STYLE_TREE = "tree"
MENU_STYLE_PAGE = "page"
MENU_STYLES = (MENU_STYLE_TREE, MENU_STYLE_PAGE)
#: 历史页 / 日志页每页条数。
K_HIST_PAGE_SIZE = "histPageSize"
K_LOG_PAGE_SIZE = "logPageSize"
#: 历史图表每线抽样点数（2026-11-24 用户「抽样 2000 还是太多了，可以设置
#: 抽样数吗」）。取值必须落在 `HIST_CHART_POINT_CHOICES` 内；非法回缺省。
K_HIST_CHART_POINTS = "histChartPoints"
HIST_CHART_POINT_CHOICES = (100, 200, 500, 1000, 2000)
HIST_CHART_POINTS_DEF = 100
#: 屏幕电源策略（本地设置页「窗口与屏幕」区的「屏幕休眠」区）：
#:   · 自动熄屏超时（毫秒；0 = 从不）
#:   · 系统挂起超时（毫秒；0 = 从不）
#:   · 唤醒来源位集（1=鼠标 2=键盘 4=触摸）
#: [为什么落盘] 平台侧的电源策略是**进程内影子值**（XPlatformPower_policy
#: 回读的是本应用设定值），进程重启即丢 ⇒ 必须落盘并在启动时重新下发，
#: 否则「设了 5 分钟自动熄屏，重启就没了」= 选了不兑现。
K_POWER_SCREEN_OFF_MS = "powerScreenOffMs"
K_POWER_SUSPEND_MS = "powerSuspendMs"
K_POWER_WAKE_INPUTS = "powerWakeInputs"
#: 自动休眠的「到期动作」（2026-11-26 用户「自动休眠那增加选择可以走屏保」）：
#: 与自动熄屏**共用同一个空闲时长**（K_POWER_SCREEN_OFF_MS），只多一个选择——
#: 到点后是真正关显示器（off，缺省＝既有行为），还是显示本机动画屏保
#: （screensaver，位图 logo + 呼吸浮动动画，任意输入退出）。
#: 选屏保时下发平台的熄屏超时置 0（互斥），改由本进程覆盖层接管。
K_POWER_BLANK_ACTION = "powerBlankAction"
BLANK_ACTION_OFF = "off"
BLANK_ACTION_SCREENSAVER = "screensaver"
#: 屏保动画开关（2026-11-26 用户「单独增加一个屏保卡片设置，流光/呼吸/
#: 移动/文字流光都是可配置选项，本地持久化」）：
#:   · K_SS_FLOW       流光动画（logo 色带流动）缺省开；
#:   · K_SS_BREATH     呼吸缩放缺省开；
#:   · K_SS_MOVE       移动漂浮缺省**关**（用户口径：图标放大居中不移动）；
#:   · K_SS_TEXT_FLOW  文字流光缺省开（勾选后文字也随色带明显变色）。
K_SS_FLOW = "screenSaverFlow"
K_SS_BREATH = "screenSaverBreath"
K_SS_MOVE = "screenSaverMove"
K_SS_TEXT_FLOW = "screenSaverTextFlow"
#: 网页服务（2026-11-14）：是否随客户端启动网页端 + 网页监听端口。
#:   · 缺省**开**（用户口径「启动客户端，网页也启动」）；
#:   · 端口缺省 **8085**（**不用 8080**：本机 8080 常被别的常驻程序占着）。
K_WEB_ENABLED = "webEnabled"
K_WEB_PORT = "webPort"
#: 网页端**远程访问**（2026-11-16 用户定案「Web 设置页能配域名和远程端口，
#: 并按它生成二维码」）：
#:   · `K_WEB_DOMAIN` = 对外域名（DDNS / 固定域名），空 = 未配置；
#:   · `K_WEB_REMOTE_PORT` = 路由器上映射到本机网页端口的**外部端口**，
#:     0 = 与本地监听端口相同（界面显示成"留空=同本地"）；
#:   · 两者都只是**展示用**（拼二维码/地址文本里的访问 URL）——把外网流量
#:     接进来靠现场路由器端口映射或内网穿透，客户端**不做穿透**、也不去
#:     改用户的路由器（不在背后动网络配置）。
K_WEB_DOMAIN = "webDomain"
K_WEB_REMOTE_PORT = "webRemotePort"
#: 客户端关闭时**是否同步关闭网页端**（2026-11-17 用户定案「Web 设置网页
#: 服务……并可以设置客户端关闭的时候是否要同步关闭 web 端」）。
#:   · **开**（缺省）= 同步关闭：网页端以**内嵌线程**形态随客户端跑 ⇒
#:     客户端退，网页端随之退，端口干净释放（不留孤儿进程）；
#:   · **关** = 不同步关闭：网页端以**独立子进程**形态跑 ⇒ 客户端退出后
#:     网页端**继续在后台服务**（别人仍能访问；下次客户端起来能探测到它）。
K_WEB_STOP_WITH_CLIENT = "webStopWithClient"
#: 鼠标（2026-11-27 用户「板端的客户端在本地设置中添加鼠标的一些常见设置
#: 比如鼠标大小，隐藏鼠标光标等等」+「还有鼠标样式」）：
#:   · K_MOUSE_CURSOR_STYLE：光标样式（arrow=箭头缺省 / cross=十字 /
#:     hand=手型 / pointing=指向手型 / sizeall=移动十字）；
#:   · K_MOUSE_CURSOR_SIZE：箭头样式下的自绘尺寸（px；0=系统默认）。
#:     非箭头样式由 Qt 标准光标提供，尺寸跟随系统（自绘其它形状收益低）。
#:   · K_MOUSE_CURSOR_HIDDEN：隐藏光标（触摸屏现场防挡视线/防误触）。
K_MOUSE_CURSOR_STYLE = "mouseCursorStyle"
K_MOUSE_CURSOR_SIZE = "mouseCursorSize"
K_MOUSE_CURSOR_HIDDEN = "mouseCursorHidden"
#: 样式取值 → 中文名（界面下拉与回归共用一份）。
MOUSE_CURSOR_STYLES = (
    ("arrow", "箭头"),
    ("cross", "十字"),
    ("hand", "手型"),
    ("pointing", "指向手型"),
    ("sizeall", "移动十字"),
)
MOUSE_CURSOR_STYLE_DEF = "arrow"
#: 箭头尺寸档（0=系统默认；其余为自绘像素）。
MOUSE_CURSOR_SIZES = (0, 24, 32, 48)
#: 界面主题（2026-11-14 用户定案「本地设置增加主题功能」）。
#:   · 取值 = theme.THEME_LIST 里的主题 id（light/dark/isa/green/contrast）；
#:   · 缺省 = theme.DEFAULT_THEME_ID（亮色·工业白）；
#:   · **本机设置**（客户端 INI）⇒ 只影响本机界面，与服务端无关。
K_THEME = "uiTheme"

_DEFAULTS = {
    K_UI_SCALE_PCT: 0,          # 0 = 不覆盖（用 theme.ui_scale() 的缺省/环境变量）
    K_FORCE_COMPACT: False,
    K_FULLSCREEN: False,
    K_REFRESH_MS: 200,
    K_TREND_MS: 200,
    K_TREND_RANGE_MS: 60000,
    K_WIN_SIZE: "",
    K_SHOW_TITLE: True,
    K_PREWARM_PAGES: False,
    K_SHOW_STATUS: True,
    K_SHOW_NAV: True,
    K_SHOW_CLOCK: True,
    K_SHOW_TEMPHUM: True,
    K_SHOW_QR: True,
    K_SHOW_MODE: True,
    K_SHOW_HEALTH: True,
    K_SHOW_CAMERA: True,
    K_MENU_BUTTON_STYLE: MENU_STYLE_TREE,
    K_HIST_PAGE_SIZE: 0,        # 0 = 各页自己的缺省
    K_LOG_PAGE_SIZE: 0,
    K_HIST_CHART_POINTS: HIST_CHART_POINTS_DEF,
    K_WEB_ENABLED: True,        # 网页端随客户端启动（缺省开）
    K_WEB_PORT: 8085,           # 网页端口（不占 8080）
    K_WEB_STOP_WITH_CLIENT: True,  # 客户端关闭时同步关闭网页端（缺省开）
    K_THEME: "",                 # 空 = 用 theme.DEFAULT_THEME_ID
    K_POWER_BLANK_ACTION: BLANK_ACTION_OFF,  # 自动休眠缺省仍真熄屏
    K_MOUSE_CURSOR_STYLE: MOUSE_CURSOR_STYLE_DEF,  # 箭头
    K_MOUSE_CURSOR_SIZE: 0,      # 0 = 系统默认尺寸
    K_MOUSE_CURSOR_HIDDEN: False,  # 缺省显示光标
    K_SS_FLOW: True,       # 流光动画缺省开
    K_SS_BREATH: True,     # 呼吸缩放缺省开
    K_SS_MOVE: False,      # 移动缺省关（图标放大居中）
    K_SS_TEXT_FLOW: True,  # 文字流光缺省开
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


def menu_button_style():
    """左上角 ☰ 的导航方式：`tree` 或 `page`（非法值回 `tree`，fail-safe）。"""
    v = get_str(K_MENU_BUTTON_STYLE, MENU_STYLE_TREE)
    return v if v in MENU_STYLES else MENU_STYLE_TREE


def trend_ms():
    """实时趋势记录间隔（毫秒）；非法/缺失 ⇒ 200。"""
    v = get_int(K_TREND_MS, 200)
    return v if v > 0 else 200


def web_enabled():
    """是否随客户端启动网页端（缺省**开**：用户口径「启动客户端，网页也启动」）。"""
    return get_bool(K_WEB_ENABLED, True)


#: 数据三页（历史/日志/报警）工具条的**折叠状态**（2026-11-19 用户口径
#: 「表格的上面与这些下面中间是不是可以增加一个分割条，可以折叠红框中的功能
#:  区，这样浏览数据是不是更方便」）。
#:
#: [为什么落盘] 折叠是操作员的**长期偏好**（小屏上表格越大越好）⇒ 每次
#:  开机都要重新点一遍是折磨 ⇒ 记进本机设置（INI），按页分别记
#:  （"dataToolbarCollapsed.history" / ".log" / ".alarm"）。
K_DATA_TOOLBAR_COLLAPSED = "dataToolbarCollapsed"


def data_toolbar_collapsed(page_key=""):
    """该页的数据工具条是否折叠（缺省**不折叠** ⇒ 筛选区默认可见）。"""
    key = K_DATA_TOOLBAR_COLLAPSED
    if page_key:
        key = "%s.%s" % (key, page_key)
    return get_bool(key, False)


def set_data_toolbar_collapsed(page_key, collapsed):
    """记下该页工具条的折叠状态（写不进去静默：不该因设置失败而打断界面）。"""
    key = K_DATA_TOOLBAR_COLLAPSED
    if page_key:
        key = "%s.%s" % (key, page_key)
    set_value(key, bool(collapsed))


def web_port():
    """网页监听端口（缺省 8085——**不用 8080**；夹到 [1024,65535]）。"""
    return max(1024, min(65535, get_int(K_WEB_PORT, 8085)))


def normalize_domain(raw):
    """把用户输入的域名收敛成**纯主机名**（见 :func:`web_domain`）。

    现场常见几种写法都收：``https://hsf.example.com/``、``hsf.example.com:8085``、
    带空格的复制粘贴——统一去 scheme / 路径 / 尾部斜杠 / 端口 / 空白。
    端口由 :func:`web_remote_port` 单独管，**不从域名里猜**。
    """
    text = (raw or "").strip()
    if not text:
        return ""
    low = text.lower()
    for scheme in ("http://", "https://"):
        if low.startswith(scheme):
            text = text[len(scheme):]
            break
    for sep in ("/", "?", "#"):
        cut = text.find(sep)
        if cut >= 0:
            text = text[:cut]
    # 去端口（host:port；IPv6 字面量 [::1]:port 不在本功能场景内）
    if text.count(":") == 1:
        text = text.split(":", 1)[0]
    return text.strip().rstrip(".")


def web_domain():
    """对外域名（规范化后返回；空串 = 未配置）。"""
    return normalize_domain(get_str(K_WEB_DOMAIN, ""))


def web_stop_with_client():
    """客户端关闭时是否**同步关闭网页端**（缺省 **开** = 同步关闭）。

    [两种形态的差别] 开 ⇒ 网页端内嵌在客户端进程里（线程）⇒ 客户端退它
    就退；关 ⇒ 网页端由客户端拉起**独立子进程** ⇒ 客户端退它继续跑。
    """
    return get_bool(K_WEB_STOP_WITH_CLIENT, True)


def web_remote_port():
    """远程访问端口（0 = 未配置 ⇒ 与本地监听端口相同；否则 1..65535）。"""
    v = get_int(K_WEB_REMOTE_PORT, 0)
    if v <= 0 or v > 65535:
        return 0
    return v


def hist_page_size():
    return get_int(K_HIST_PAGE_SIZE, 0)


def hist_chart_points():
    """历史图表每线抽样点数（取值须在 HIST_CHART_POINT_CHOICES 内）。"""
    v = get_int(K_HIST_CHART_POINTS, HIST_CHART_POINTS_DEF)
    return v if v in HIST_CHART_POINT_CHOICES else HIST_CHART_POINTS_DEF


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
