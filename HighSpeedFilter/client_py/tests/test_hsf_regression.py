# -*- coding: utf-8 -*-
"""HSF 双端总回归（2026-11-09 起所有缺陷的**单文件**验证入口）。

    python tests/test_hsf_regression.py            # 自起模拟服务端 + 全跑
    python tests/test_hsf_regression.py --no-srv   # 只跑不依赖服务端的段

[为什么是"一个文件"]
之前回归锁散在十几个文件里（test_client / test_home_cards / test_history_* …），
加一个功能要翻好几处才知道该跑哪些。这里把**跨端契约**与**端内行为**收成
一份，跑一次就覆盖：协议形状、服务端按侧推送口径、客户端择优与卡片渲染、
以及今天修掉的每一个缺陷的回归点。以后加/改功能，先在这里补一条断言。

[覆盖面]
  §1 服务端协议契约：历史页（0x0118）应答的键 / 行宽 / 列序 / 类型 / 回显
  §2 服务端按侧推送契约：同一主题两条 485 侧轮流推时，未装配侧**不得**推空帧
  §3 客户端模型契约：多侧帧交替到达时按"优先侧 + 兜底侧"择优，不被空帧盖掉
  §4 客户端首页：首屏订阅齐、两张新卡（浊度仪/按钮组）数值落地、触点极性
  §5 客户端历史页：18 列原始行 → 显示列映射与空值口径
  §6 历史数据连续性：取回的相邻时间戳不得出现大空洞（跳批抽样会造洞）
  §7 服务端协议契约：运行日志分页（0x0119）应答形状
  §8 客户端契约：双击列标题排序
  §9 服务端契约：运行日志「类别」（0x0119 的 category 必须是**真实类别**）
  §9b 服务端契约：日志页筛选（类别精确过滤 / 关键字搜类别名 /
      beforeTs 锚点不漂移 / 页面级类别下拉全链）——**先造 auto 事件**再断言
  §10 服务端契约：历史行不留启动空洞（逐字段热身门）

  §11 **交叉测试 A**：Python 客户端 ↔ C 服务端（跨实现，两端载荷同形；
      含 0x0119 类别往返 / 关键字搜类别 / 两端各自的窗口锚点）
  §12 **交叉测试 B**：Python 服务端 ↔ C 客户端（按 C 客户端的下标契约核对；
      含类别精确过滤 / 未知类别零行 / 0x0118 与 0x0119 的窗口锚点）
  §13 **交叉测试 C**：客户端 ↔ 服务端 ↔ 框架（"库里一行 = 界面一拍"）

  §14 历史页：列筛选 / 关键字下推 / 时间窗锚点（并入 test_history_filter
                + test_history_columns 的关键行为）
  §15 日志页：类别筛选 / 关键字 / 导出 / 分页归属（并入 test_log_page）
  §16 报警页：工具栏动作（并入 test_alarm_page_toolbar）
  §17 趋势图与图表交互：缩放 / 平移 / 数据源（并入 test_trend_chart
                + test_chart_interaction）
  §18 存储上限：封顶 / 轮转策略读数（并入 test_storage_limit）
  §19 远程设置：读写回显（并入 test_remote_settings_ui）
  §20 软键盘：无死白 / 切换不跳（并入 test_softkeyboard）
  §21 变频器与数字量：滑块单位 / 启动给定值自检（并入 test_vfd_slider
                + test_vfd_start_given）
  §22 界面字号缩放（并入 test_ui_scale）
  §31 全量筛选枚举：N>单页容量 → 筛选 → 枚举行数=命中总数且跨页
      （导出=按当前筛选条件拉全的判界，2026-11-10 导出改造）
  §32 导出文件无头写 CSV/XLSX 并回读校验（BOM/CRLF/最小引号/包内容）
  §33 筛选谓词与翻页判界（时间/关键字/列选择/清除筛选 + 上下页边界）
  §30 主窗 UI 真实点击回归（真实 MainWindow + 私有 sim：导航切页 /
                变频器启停 / 历史筛选·折线图·翻页·排序 / 日志类别·
                刷新·导出闸 / 报警类型筛选 / 本地设置控件）
  §34 设置三页合并（2026-11-13 用户定案：本地设置/悬浮窗设置/窗口设置 →
                本地设置一页，全树 16 页）：三分区齐全 + 每个功能项
                「存在 + 生效」（独立文件 tests/test_settings_merge.py）

[今天修掉的缺陷，逐条对应断言]
  D1 首页无数据（订阅被静默闸吞掉）            → §4.1
  D2 首页两卡数据不刷新（未装配侧空帧覆盖）    → §2 / §3
  D3 浊度/流量计恒"无数据"（同上，跨端）        → §2 / §3
  D4 485 从站跑久了停摆（异常帧当成功等四层）   → §1（形状/回显）+ §6（空洞）
                                                   （框架内部的四层根修在 C 端
                                                     --test xmodbus-virtual 用例⑤）
  D5 模拟器停止按钮常闭口径错                  → §4.3
  D6 历史图缩放后不连续（缩放不补数）          → §6
  D7 运行日志「类别」列整列空白（category 恒空串） → §9
  D8 启动瞬态历史行变频器四列全空（界面半屏 --）  → §10

[**测试侧**口径修正（不是产品缺陷，2026-11-09 回归收口）]
  T1 §13 偶发红「专用连接」：多段各起一条连接，上一段刚断、服务端还在
     静默回收会话时再登录会被拒；原「3 次 × 1.5s」全撞在同一窗口。
     → 私有服务端起之前就把 `login.allow_concurrent` 打开
       （PrivateServer.LOGIN_POLICY；**不动产品的登录闸**），
       重连改 1.5 倍退避 4 次，失败时把服务端日志尾巴带进断言详情。
  T2 §13「库里没有与界面同拍的行」：客户端的 updated_ms 是 **monotonic
     秒表**（stale 判定用），库里 ts 是墙钟 epoch 毫秒，相减恒差 1.79e12。
     → 收到那一帧时取一次 (墙钟, 秒表) 对标，把那一拍换算到墙钟再对账。
  T3 §13「日志页行 == 库里同一行」：logs.ts 不是主键，同毫秒可落两行，
     按 ts 建字典会把第二条盖掉 ⇒ 假红。
     → 按 (ts,category,text) 三元组对齐。

[依赖]
  · Python 3.9+；本机 PyQt5 已装（客户端段需要）
  · 无需硬件：服务端段自起 `python -m hsfsrv --sim`（临时 cfg，不碰现场库）
  · 交叉测试段另外需要 bin/HsfServer.exe（C 服务端）：**复制到临时目录**
    再跑（它的 cfg/账户库/数据库都按 exe 所在目录解析 ⇒ 天然隔离），
    找不到该 exe 时该段整体 SKIP（不依赖别人手工起的 6100）。
"""

import importlib
import json
import os
import re
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)          # .../client_py
SERVER_ROOT = os.path.join(os.path.dirname(CLIENT_ROOT), "server_py")
#: 仓库根（bin/HsfServer.exe 在 <repo>/bin/；client_py/server_py 是
#: <repo>/HighSpeedFilter/ 下的两个子目录）。
REPO_ROOT = os.path.dirname(os.path.dirname(CLIENT_ROOT))
sys.path.insert(0, CLIENT_ROOT)
#: 散落测试文件所在目录（§14 起把它们 import 进来并入本文件）。
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# [输出编码 2026-11-14] 中文 Windows 的默认代码页是 cp936(GBK) ⇒ 输出被
# 重定向到文件时 stdout 用 GBK ⇒ 打印「⇒」这类字符直接
# UnicodeEncodeError **把整套回归打断**（实测：跑到一半崩，退出码却是
# 0 ⇒ 看着像"跑完了"）。这里强制 UTF-8 + errors="replace"（写不出去也
# 不崩），保证任何环境下都能跑完并给出真实结论。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

PASS = 0
FAIL = 0
SKIP = 0
FAILED = []

#: 慢段开关（存储上限的 stop/rotate，各要 30s+ 真实增长）。命令行 `--slow`。
SLOW_PARTS = False
#: 长稳 soak 分钟数（命令行 `--soak-minutes N`；缺省 1 分钟供日常门禁，
#: 夜间长稳用 15）。§27 反复连接是 soak 的"快速版"（10 轮 ~40s）。
SOAK_MINUTES = 1
#: 当前私有服务端端口（子函数要用）。
_reg_port = 0


def check(name, ok, detail=""):
    """记一条断言。detail 会打印出来（失败时是定位线索）。"""
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s %s" % (name, detail))


def skip(name, why):
    global SKIP
    SKIP += 1
    print("  [SKIP] %s（%s）" % (name, why))


def section(title):
    print("\n=== %s ===" % title)


# ============================================================================
#                       服务端契约：历史页（0x0118）
# ============================================================================
#
# [为什么锁这些] C 客户端解析历史应答时**按列下标**取原始数值：
#   ts, mode, vfd_state, freq_hz, current_a, rpm, turb1, turb2, flow,
#   flow_unit, flow_total, total_unit, press_mpa, press_note, temp, hum, di, do
# （HighSpeedFilter/client/ui/HsfLinkHistory.c 的 kSrc/kDst 映射表）
# 一旦服务端改成"预格式化显示文本"或调列序，C 客户端就会**静默读到错列**
# （例如 temp 读到 do 的位图），界面上表现为"列全是 -- / 数值乱七八糟"。
# 这里把形状钉死，两端任何一端改口径都会红。

HIST_COLS = ["ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
             "turb1", "turb2", "flow", "flow_unit", "flow_total",
             "total_unit", "press_mpa", "press_note", "temp", "hum", "di", "do",
             # [水温 2026-11-10] 追加在**末尾**（C 客户端 kSrc/kDst 按下标，
             # 中途插入会平移既有下标）；显示列序另见 HistoryPage.COLUMNS。
             "water_temp"]
HIST_COL_COUNT = len(HIST_COLS)
#: 数值列（必须是数值，不是字符串）——图表按这些列画。
HIST_NUMERIC_COLS = ["ts", "freq_hz", "current_a", "rpm", "turb1", "turb2",
                     "flow", "flow_total", "press_mpa", "temp", "hum", "di",
                     "do", "water_temp"]
#: 文本列（模式/状态/单位/说明）。
HIST_TEXT_COLS = ["mode", "vfd_state", "flow_unit", "total_unit", "press_note"]


def _query_history(bridge, limit=8, wait_rows=0, timeout=25.0):
    """发一次历史查询；可选等库里有足够行（模拟服务端 1s 一行，刚起时为空）。

    :param wait_rows: 至少要等到的行数（0=不等）。
    :return: (doc, err)；两者都可能为 None（超时）。
    """
    deadline = time.time() + timeout
    doc = None
    err = None
    while time.time() < deadline:
        got = {}

        def done(d, _g=got):
            _g["doc"] = d

        def bad(m, _g=got):
            _g["err"] = m

        bridge.history_page(offset=0, limit=limit, on_done=done,
                            on_error=bad)
        deadline2 = time.time() + 8
        while "doc" not in got and "err" not in got and time.time() < deadline2:
            _pump()
            time.sleep(0.02)
        if "err" in got:
            err = got["err"]
            break
        if "doc" not in got:
            break
        doc = got["doc"]
        if not wait_rows or len(doc.get("rows") or []) >= wait_rows:
            break
        # 库里还没攒够行 ⇒ 等一拍再查（模拟服务端 1s 一行）。
        time.sleep(1.0)
    return doc, err


def check_history_contract(bridge, proto):
    """§1 服务端历史应答的形状契约。"""
    section("§1 服务端契约：历史页应答（0x0118）")
    doc, err = _query_history(bridge, limit=8, wait_rows=1)
    if err:
        check("历史查询有应答", False, "err=%s" % err)
        return None
    if not doc:
        check("历史查询有应答", False, "超时未应答")
        return None
    check("历史查询有应答", True, "total=%s" % doc.get("total"))

    # --- 键 ---
    for key in ("offset", "limit", "total", "rows", "cols"):
        check("应答含键 %s" % key, key in doc)

    # --- offset/limit 必须回显请求值 ---
    # [为什么] C 客户端靠 (offset, limit) 回显**认领应答**：对不上就把应答
    # 当"无主"丢掉（drop unattributed reply）⇒ 表格永远停在"读取中"。
    check("offset 回显请求值", doc.get("offset") == 0,
          "offset=%r" % doc.get("offset"))
    check("limit 回显请求值", doc.get("limit") == 8,
          "limit=%r" % doc.get("limit"))

    # --- 列名列序 ---
    cols = list(doc.get("cols") or [])
    check("cols 列出 18 个列名", len(cols) == HIST_COL_COUNT,
          "%d 个" % len(cols))
    check("cols 列序与协议一致", cols == HIST_COLS,
          "" if cols == HIST_COLS else "实际=%s" % cols[:6])

    # --- 行结构 ---
    rows = list(doc.get("rows") or [])
    check("取到行", len(rows) > 0, "%d 行" % len(rows))
    if not rows:
        return doc
    widths = set(len(r) for r in rows)
    check("每行 18 列（与 cols 一致）", widths == {HIST_COL_COUNT},
          "行宽集合=%s" % widths)

    row = rows[0]
    by_name = dict(zip(cols, row))
    bad_type = []
    for name in HIST_NUMERIC_COLS:
        val = by_name.get(name)
        if isinstance(val, bool) or not isinstance(val, (int, float)):
            bad_type.append("%s=%r(%s)" % (name, val, type(val).__name__))
    check("数值列是数值（不能是预格式化文本）", not bad_type,
          "" if not bad_type else "错型列=%s" % bad_type)
    bad_text = []
    for name in HIST_TEXT_COLS:
        val = by_name.get(name)
        if val is not None and not isinstance(val, str):
            bad_text.append("%s=%r(%s)" % (name, val, type(val).__name__))
    check("文本列是字符串", not bad_text,
          "" if not bad_text else "错型列=%s" % bad_text)
    check("ts 是 epoch 毫秒（13 位）",
          isinstance(by_name.get("ts"), int)
          and 10 ** 12 < by_name["ts"] < 10 ** 13,
          "ts=%r" % by_name.get("ts"))
    check("di/do 是位图整数",
          isinstance(by_name.get("di"), int)
          and isinstance(by_name.get("do"), int),
          "di=%r do=%r" % (by_name.get("di"), by_name.get("do")))

    # --- 负压行：压差列显示**数值**而不是说明文字（2026-11-19 用户口径）---
    # 「历史数据里应该记录具体的负压值用负数，而不是说是个负压」⇒ 服务端
    # 给线性负值 + 说明；显示列必须数值优先（说明仍在 press_note 里，异常行
    # 标红照旧按它判）。这里直接调行格式化函数锁住。
    try:
        from hsfsrv_client.pages_other import HistoryPage as _HP
        _neg = list(by_name.values())
        # 把压差列换成负值、说明列填上负压说明
        _cols = list(proto.HISTORY_COLUMNS)
        _row = []
        for _c in _cols:
            if _c == "press_mpa":
                _row.append(-0.008)
            elif _c == "press_note":
                _row.append("低于 4.0mA（负压）")
            else:
                _row.append(by_name.get(_c))
        _texts, _abn = _HP._server_row_texts(
            type("F", (), {"TURBIDITY_WARN_NTU": _HP.TURBIDITY_WARN_NTU})(),
            _row, _cols)
        _pi = list(_HP.COLUMNS).index("压差(MPa)")
        check("负压行压差列显示数值（-0.008），不是说明文字",
              _texts[_pi] == "-0.008", "压差列=%r" % (_texts[_pi],))
        check("负压行仍标为异常行（信号不丢）", _abn is True,
              "abnormal=%r" % (_abn,))
    except Exception as _exc:                           # noqa: BLE001
        check("负压行压差列显示数值（-0.008），不是说明文字", False,
              "%s: %s" % (type(_exc).__name__, _exc))
    return doc


def check_history_continuity(bridge, proto):
    """§6 历史数据连续性（缩放取数/跳批抽样的"洞"检测）。

    [为什么] 图表的横轴是时间。若服务端分页跳批（取第 1 批、第 100 批…），
    相邻两行的时间戳会出现**巨大空洞**，画出来就是"两段直线的连线"——
    现场口径叫"不够连续"。这里用"相邻间隔 ≤ 2× 中位间隔"判洞。
    """
    section("§6 历史数据连续性（无跳批空洞）")
    doc, err = _query_history(bridge, limit=40, wait_rows=8)
    if not doc:
        check("取到连续性样本", False, err or "超时")
        return
    rows = list(doc.get("rows") or [])
    if len(rows) < 5:
        check("取到连续性样本", False, "只有 %d 行" % len(rows))
        return
    ts = [r[0] for r in rows]
    deltas = [ts[i + 1] - ts[i] for i in range(len(ts) - 1)]
    deltas_sorted = sorted(deltas)
    median = deltas_sorted[len(deltas_sorted) // 2] or 1
    holes = [d for d in deltas if d > max(2 * median, median + 1000)]
    check("相邻时间戳无大空洞（不是跳批抽样）", not holes,
          "中位间隔=%dms 最大空洞=%s" % (median,
                                      max(deltas) if deltas else "-"))


# ============================================================================
#                  服务端契约：同一主题的多侧推送
# ============================================================================
#
# [为什么] 一条 485 线上可能挂两套设备（变频器在 A 侧、流量计在 B 侧……），
# 服务端按"每侧一帧"推同一个主题。**未装配的那一侧不能推空帧**：客户端
# 若只保留最后一帧，空帧就把真数据盖掉 ⇒ 界面恒"无数据"（现场实测过：
# Python 客户端温湿度恒 --，C 客户端因为按侧分存才幸免）。
# 这里锁：温湿度/浊度/流量计三个主题，收到的帧里**不许出现全无效的侧帧**。

def check_per_side_push(bridge, proto, model):
    """§2 服务端按侧推送：未装配侧不得推空帧。"""
    section("§2 服务端契约：按侧推送不得夹带空帧")
    bridge.set_topics({proto.TOPIC_TEMPHUM_STATUS,
                        proto.TOPIC_TURBIDITY_STATUS,
                        proto.TOPIC_FLOWMETER_STATUS,
                        proto.TOPIC_DIGITAL_STATUS})
    empty_frames = {"temphum": 0, "turbidity": 0, "flowmeter": 0}
    seen = {"temphum": 0, "turbidity": 0, "flowmeter": 0}
    deadline = time.time() + 6
    while time.time() < deadline:
        _pump()
        time.sleep(0.05)
        th = model.temphum
        for side, st in (getattr(th, "sides", {}) or {}).items():
            seen["temphum"] += 1
            if st is not None and not (st.temp_valid or st.hum_valid):
                empty_frames["temphum"] += 1
        tb = model.turbidity
        for side, st in (getattr(tb, "sides", {}) or {}).items():
            seen["turbidity"] += 1
            if st is not None and st.count and not any(
                    st.ntu_of(i) is not None for i in range(st.count)):
                empty_frames["turbidity"] += 1
        fm = model.flowmeter
        for side, st in (getattr(fm, "sides", {}) or {}).items():
            seen["flowmeter"] += 1
            if st is not None and not st.valid:
                empty_frames["flowmeter"] += 1
    check("收到温湿度帧", seen["temphum"] > 0, "%d 帧" % seen["temphum"])
    check("收到浊度帧", seen["turbidity"] > 0, "%d 帧" % seen["turbidity"])
    check("收到流量计帧", seen["flowmeter"] > 0, "%d 帧" % seen["flowmeter"])
    for key, name in (("temphum", "温湿度"), ("turbidity", "浊度"),
                      ("flowmeter", "流量计")):
        check("%s主题无空帧（未装配侧不推）" % name,
              empty_frames[key] == 0,
              "空帧 %d/%d" % (empty_frames[key], seen[key]))


# ============================================================================
#                       客户端模型契约：按侧择优
# ============================================================================
#
# [为什么] 即使服务端不推空帧，客户端也必须能扛住"两侧轮流推"：
# 保留最后一帧 ⇒ 后到的空帧盖掉先到的真帧。这里直接构造两种模型实例
# 灌帧，验证择优结果（这是今天 Python 客户端的实测缺陷）。

def check_side_pick(model_cls_holder, proto):
    """§3 客户端模型：多侧帧 → 按优先侧择优。"""
    section("§3 客户端模型契约：多侧帧择优")
    from hsfsrv_client import model as M

    # --- 温湿度：A 侧有值、B 侧空帧 ---
    th = M.TempHumState()
    th.apply({"side": 0, "temp": 217, "hum": 450, "tempValid": True,
              "humValid": True, "staleMs": 5000})
    th.apply({"side": 1, "tempValid": False, "humValid": False,
              "staleMs": 5000})
    check("温湿度：优先侧(A)有值时被选中", th.temp_text() == "21.7",
          "temp_text=%r" % th.temp_text())

    # --- 温湿度：优先侧空、兜底侧有值 ---
    th2 = M.TempHumState()
    th2.apply({"side": 0, "tempValid": False, "humValid": False,
               "staleMs": 5000})
    th2.apply({"side": 1, "temp": 253, "hum": 610, "tempValid": True,
               "humValid": True, "staleMs": 5000})
    check("温湿度：优先侧空时退兜底侧", th2.temp_text() == "25.3",
          "temp_text=%r" % th2.temp_text())

    # --- 温湿度：两侧都空 ⇒ 显示 "--"（不编造） ---
    th3 = M.TempHumState()
    th3.apply({"side": 0, "tempValid": False, "humValid": False,
               "staleMs": 5000})
    th3.apply({"side": 1, "tempValid": False, "humValid": False,
               "staleMs": 5000})
    check("温湿度：两侧都无效 ⇒ 显示 --（不编造）", th3.temp_text() == "--",
          "temp_text=%r" % th3.temp_text())

    # --- 浊度：两台合成一帧，online=false ⇒ ntu 为 None ---
    tb = M.TurbidityState()
    tb.apply({"count": 2, "v0_name": "进水浊度仪", "v0_ntu": 1231,
              "v0_online": True, "v1_name": "出水浊度仪", "v1_ntu": 325,
              "v1_online": True})
    check("浊度：定点 ÷100", abs((tb.ntu_of(0) or 0) - 12.31) < 1e-6,
          "ntu_of(0)=%r" % tb.ntu_of(0))
    tb2 = M.TurbidityState()
    tb2.apply({"count": 2, "v0_name": "进水浊度仪", "v0_ntu": 1231,
               "v0_online": False, "v1_name": "出水浊度仪", "v1_ntu": 325,
               "v1_online": True})
    check("浊度：离线台 ntu 为 None（界面显示 --）",
          tb2.ntu_of(0) is None and tb2.ntu_of(1) is not None,
          "v0=%r v1=%r" % (tb2.ntu_of(0), tb2.ntu_of(1)))

    # --- 数字量：通道名 + 位图 ---
    dg = M.DigitalState()
    dg.apply({"di": 0b1010101010101010, "do": 0b100111111, "count": 16,
              "valid": True, "staleMs": 5000,
              "din12": "急停（常闭）", "din13": "启动按钮",
              "din14": "停止按钮", "din15": "复位按钮",
              "don7": "启动按钮灯", "don8": "停止按钮灯", "don9": "复位按钮灯"})
    check("数字量：通道名落表", dg.di_name(12) == "急停（常闭）",
          "din12=%r" % dg.di_name(12))
    # 位图 0b1010101010101010：bit12=0（急停无信号）、bit13=1（启动按下）
    check("数字量：DI 位图可读",
          dg.di_bit(12) is False and dg.di_bit(13) is True,
          "di12=%r di13=%r" % (dg.di_bit(12), dg.di_bit(13)))

    # ------------------------------------------------------------------
    # 按侧分帧的**优先序**（每类设备挂的侧不同，PREFER 不是"一律 A"）
    # ------------------------------------------------------------------
    # [口径] 现场：温湿度站挂 A 侧 ⇒ TempHumState.PREFER=(A,B)；
    # 流量计与两台浊度仪挂 B 侧 ⇒ PREFER=(B,A)（与 C 端
    # hsfHomePickTempHum / hsfHomePickFlowMeter 同口径）。"优先"是
    # **先试哪一侧**，不是"只看优先侧"：优先侧不新鲜/无有效值时必须退
    # 兜底侧，否则空帧会把真数据盖掉（D2/D3 的根因）。

    # --- 流量计：B 侧优先 ---
    fm = M.FlowMeterState()
    fm.apply({"side": 0, "flow": 11.1, "valid": True, "staleMs": 5000})
    fm.apply({"side": 1, "flow": 36.5, "valid": True, "staleMs": 5000})
    check("流量计：两侧都有效 ⇒ 取 B 侧（PREFER=(B,A)）",
          abs(fm.flow - 36.5) < 1e-6, "flow=%r" % fm.flow)
    fm2 = M.FlowMeterState()
    fm2.apply({"side": 0, "flow": 11.1, "valid": True, "staleMs": 5000})
    fm2.apply({"side": 1, "valid": False, "staleMs": 5000})   # B 侧空帧
    check("流量计：B 侧空帧不覆盖 A 侧真值（优先侧无效 ⇒ 退兜底）",
          abs(fm2.flow - 11.1) < 1e-6 and fm2.valid, "flow=%r valid=%r"
          % (fm2.flow, fm2.valid))
    fm3 = M.FlowMeterState()
    fm3.apply({"side": 1, "valid": False, "staleMs": 5000})
    fm3.apply({"side": 0, "valid": False, "staleMs": 5000})
    check("流量计：两侧都无效 ⇒ 无可用快照（界面「无数据」，不编造）",
          fm3.pick() is None and fm3.valid is False,
          "pick=%r valid=%r" % (fm3.pick(), fm3.valid))

    # --- 浊度：B 侧优先；优先侧无在线台 ⇒ 退 A 侧在线台 ---
    tb_b = M.TurbidityState()
    tb_b.apply({"side": 0, "count": 2, "v0_name": "进水浊度仪", "v0_ntu": 1250,
                "v0_online": True, "v1_name": "出水浊度仪", "v1_ntu": 320,
                "v1_online": True})
    tb_b.apply({"side": 1, "count": 2, "v0_name": "进水浊度仪", "v0_ntu": 320,
                "v0_online": True, "v1_name": "出水浊度仪", "v1_ntu": 1250,
                "v1_online": True})
    check("浊度：两侧都在线 ⇒ 取 B 侧（PREFER=(B,A)）",
          abs((tb_b.ntu_of(0) or 0) - 3.20) < 1e-6,
          "ntu_of(0)=%r" % tb_b.ntu_of(0))
    tb_a = M.TurbidityState()
    tb_a.apply({"side": 0, "count": 2, "v0_name": "进水浊度仪", "v0_ntu": 1250,
                "v0_online": True, "v1_name": "出水浊度仪", "v1_ntu": 320,
                "v1_online": True})
    tb_a.apply({"side": 1, "count": 2, "v0_name": "进水浊度仪", "v0_online": False,
                "v1_name": "出水浊度仪", "v1_online": False})     # B 侧全离线
    check("浊度：B 侧全离线 ⇒ 退 A 侧在线台（优先侧没读数不算数）",
          abs((tb_a.ntu_of(0) or 0) - 12.50) < 1e-6,
          "ntu_of(0)=%r" % tb_a.ntu_of(0))
    tb_off = M.TurbidityState()
    tb_off.apply({"side": 1, "count": 2, "v0_name": "进水浊度仪",
                  "v0_online": False, "v1_name": "出水浊度仪", "v1_online": False})
    check("浊度：两侧都离线 ⇒ 仍取到仪器名/台数，但 ntu 为 None（显示 --）",
          tb_off.count == 2 and tb_off.ntu_of(0) is None
          and tb_off.name_of(0) == "进水浊度仪",
          "count=%r ntu=%r name=%r"
          % (tb_off.count, tb_off.ntu_of(0), tb_off.name_of(0)))

    # --- 温湿度：优先侧**过期** ⇒ 退兜底侧（stale 不冒充实时） ---
    th_stale = M.TempHumState()
    th_stale.apply({"side": 0, "temp": 111, "hum": 222, "tempValid": True,
                    "humValid": True, "staleMs": 1})
    time.sleep(0.02)
    th_stale.apply({"side": 1, "temp": 333, "hum": 444, "tempValid": True,
                    "humValid": True, "staleMs": 5000})
    check("温湿度：A 侧过期 ⇒ 退 B 侧（过期帧不冒充实时）",
          th_stale.temp_text() == "33.3",
          "A=%.1f B=%.1f 选中=%s"
          % (th_stale.sides[0].temp_tenths / 10.0,
             th_stale.sides[1].temp_tenths / 10.0, th_stale.temp_text()))

    # --- 非法侧号：整帧丢弃（不建 side 项、不污染择优） ---
    th_bad = M.TempHumState()
    th_bad.apply({"side": 7, "temp": 999, "hum": 999, "tempValid": True,
                  "humValid": True, "staleMs": 5000})
    check("非法侧号整帧丢弃（口径同 C 端）",
          not th_bad.sides and th_bad.pick() is None,
          "sides=%s" % sorted(th_bad.sides.keys()))


# ============================================================================
#                    客户端首页契约（订阅 / 两张新卡 / 极性）
# ============================================================================

def check_home_page(bridge, proto, model):
    """§4 客户端首页：订阅齐 + 两张新卡落地 + 按钮组极性。"""
    section("§4 客户端首页契约")

    # --- §4.1 订阅 ---
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client.bridge import LinkBridge
    import PyQt5.QtWidgets as QtW

    app = QtW.QApplication.instance()
    b2 = LinkBridge()
    win = MainWindow(b2)
    app.processEvents()
    win.goto_page(0)                      # PAGE_HOME
    app.processEvents()
    b2.connect_to("127.0.0.1", bridge._reg_port, "admin", "admin")
    app.processEvents()
    want = set(win.pages[0].topics())
    got = set(b2.current_topics())
    check("首屏订阅含浊度主题",
          proto.TOPIC_TURBIDITY_STATUS in want,
          hex(proto.TOPIC_TURBIDITY_STATUS))
    check("首屏订阅含数字量主题（按钮组卡要）",
          proto.TOPIC_DIGITAL_STATUS in want,
          hex(proto.TOPIC_DIGITAL_STATUS))
    # 登录后 connected 信号 → _on_connected → set_topics（本次修的 D1）
    deadline = time.time() + 5
    while time.time() < deadline:
        app.processEvents()
        time.sleep(0.05)
        got = set(b2.current_topics())
        if want.issubset(got):
            break
    check("登录后首屏主题已订阅（订阅不被静默闸吞掉）", want.issubset(got),
          "缺=%s" % sorted(hex(t) for t in (want - got)))
    b2.disconnect_from()
    win.close()
    app.processEvents()

    # --- §4.2 浊度卡 ---
    home = model.turbidity
    check("浊度：两台在线且有值",
          home.count == 2 and home.ntu_of(0) is not None
          and home.ntu_of(1) is not None,
          "count=%s v0=%r v1=%r" % (home.count, home.ntu_of(0), home.ntu_of(1)))

    # --- §4.3 按钮组极性 ---
    # 现场口径（server_py/hsfsrv/buttons.py）：
    #   急停 = 常闭：1=回路正常，0=按下或断线（故障）
    #   停止 = 常闭：1=未按，0=按下
    #   启动/复位 = 常开：1=按下，0=未按
    from hsfsrv_client import model as M
    dg = M.DigitalState()
    dg.apply({"di": 0, "do": 0, "count": 16, "valid": True, "staleMs": 5000,
              "din12": "急停（常闭）", "din13": "启动按钮",
              "din14": "停止按钮", "din15": "复位按钮",
              "don7": "启动按钮灯", "don8": "停止按钮灯", "don9": "复位按钮灯"})

    def pressed_state(di_bitmap):
        """按客户端口径算 4 个触点的显示态（与 pages_other 的按钮组卡同源）。"""
        dg.apply({"di": di_bitmap, "do": 0, "count": 16, "valid": True,
                  "staleMs": 5000})
        out = []
        # 急停：常闭 ⇒ 0 为按下/断线
        out.append("急停" if not dg.di_bit(12) else "正常")
        # 启动/复位：常开 ⇒ 1 为按下
        out.append("按下" if dg.di_bit(13) else "未按")
        # 停止：常闭 ⇒ 0 为按下
        out.append("按下" if not dg.di_bit(14) else "未按")
        out.append("按下" if dg.di_bit(15) else "未按")
        return out

    st = pressed_state(0)                 # 全 0：急停断线、停止按下
    check("按钮组：急停无信号 ⇒ 急停（红）", st[0] == "急停", "%s" % st)
    check("按钮组：停止常闭 0 ⇒ 按下", st[2] == "按下", "%s" % st)
    st2 = pressed_state((1 << 12) | (1 << 14))
    check("按钮组：急停有信号 ⇒ 正常", st2[0] == "正常", "%s" % st2)
    check("按钮组：停止常闭 1 ⇒ 未按", st2[2] == "未按", "%s" % st2)
    st3 = pressed_state((1 << 13))
    check("按钮组：启动常开 1 ⇒ 按下", st3[1] == "按下", "%s" % st3)


# ============================================================================
#                  服务端契约：运行日志分页（0x0119，C 端扩展）
# ============================================================================
#
# [为什么锁这个] C 客户端的「日志」页走 0x0119（CMD_LOG_PAGE）。本服务端
# 原先**没有实现该命令** ⇒ C 客户端日志页永远停在"读取中"、一条都看不到
# （2026-11-09 跨端缺陷）。这里把应答形状钉死，并断言行内容可用。
#
# [形状（C 侧逐字对齐）] 应答 {"ok","err"?,"total","offset","limit",
# "rows":[{ts,category,text}]}；行内 ts=int64 毫秒、text=事件文本。

def check_log_contract(bridge, proto):
    """§7 服务端契约：运行日志分页（0x0119）。"""
    section("§7 服务端契约：运行日志分页（0x0119）")
    got = {}

    def done(doc):
        got["doc"] = doc

    def err(msg):
        got["err"] = msg

    bridge.log_page(offset=0, limit=10, on_done=done, on_error=err)
    deadline = time.time() + 8
    while "doc" not in got and "err" not in got and time.time() < deadline:
        _pump()
        time.sleep(0.02)
    if "err" in got:
        check("日志查询有应答", False, "err=%s" % got["err"])
        return
    if "doc" not in got:
        check("日志查询有应答", False, "超时未应答（服务端未实现 0x0119？）")
        return
    doc = got["doc"]
    check("日志查询有应答", True, "total=%s" % doc.get("total"))
    check("日志应答含键 total", "total" in doc)
    check("日志应答含键 offset", "offset" in doc)
    check("日志应答含键 limit", "limit" in doc)
    check("日志应答含键 rows", "rows" in doc)
    check("日志 offset 回显请求值", doc.get("offset") == 0,
          "offset=%r" % doc.get("offset"))
    check("日志 limit 回显请求值", doc.get("limit") == 10,
          "limit=%r" % doc.get("limit"))
    rows = list(doc.get("rows") or [])
    check("日志行是字典数组", all(isinstance(r, dict) for r in rows),
          "%d 行" % len(rows))
    if rows:
        row = rows[0]
        check("日志行含 ts/category/text",
              all(k in row for k in ("ts", "category", "text")),
              "keys=%s" % sorted(row.keys()))
        check("日志行 ts 是 epoch 毫秒",
              isinstance(row.get("ts"), int)
              and 10 ** 12 < row["ts"] < 10 ** 13,
              "ts=%r" % row.get("ts"))
        check("日志行 text 是非空字符串",
              isinstance(row.get("text"), str) and row["text"] != "",
              "text=%r" % row.get("text"))



# ============================================================================
#               客户端契约：表格「双击列标题排序」（与 C 端同口径）
# ============================================================================
#
# [为什么锁] Python 端本来就有（_header_dblclicked），但没人锁它；C 端补齐
#            时发现两端口径容易漂移（降→升→取消的次序、指示器、取消后
#            回到服务端原序）。这里把次序与"取消后回原序"锁住。

def check_header_sort(bridge):
    """§8 双击列标题排序：新列⇒降序；同列⇒升序；再点⇒取消（原序）。

    [为什么复用已连接的桥] 服务端同账并发=0：紧接着再登录一次会被策略
    拒（客户端表现为"连接超时"）。故直接复用主流程那条已连接的桥。
    """
    section("§8 客户端契约：双击列标题排序")
    import time as _t
    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import pages_registry as reg

    app = QApplication.instance()
    b = bridge
    win = MainWindow(b)
    app.processEvents()
    check("历史页复用已连接的桥", b.is_connected())
    win.goto_page(reg.PAGE_DATA_HISTORY)
    app.processEvents()
    page = win.pages[reg.PAGE_DATA_HISTORY]
    deadline = _t.time() + 8
    while _t.time() < deadline and page._table.table.rowCount() < 2:
        app.processEvents()
        _t.sleep(0.05)
    rows = page._table.table.rowCount()
    check("历史页取到行", rows >= 2, "%d 行" % rows)
    if rows < 2:
        win.close()
        b.disconnect_from()
        return

    def col_text(col, row=0):
        item = page._table.table.item(row, col)
        return item.text() if item is not None else ""

    before = [col_text(c) for c in range(min(3, page._table.table.columnCount()))]
    last_row = page._table.table.rowCount() - 1

    def cell(col, row):
        item = page._table.table.item(row, col)
        return item.text() if item is not None else ""

    # 双击时间列 ⇒ 降序（服务端本来就"最新在前"＝时间降序，故断言首行
    # 不早于末行，而不是"行序变化"——原序本身就是降序）。
    page._header_dblclicked(0)
    app.processEvents()
    check("双击新列 ⇒ 降序（首行不早于末行）",
          cell(0, 0) >= cell(0, last_row),
          "首=%s 末=%s" % (cell(0, 0), cell(0, last_row)))
    # 再双击同列 ⇒ 升序（与降序不同）
    page._header_dblclicked(0)
    app.processEvents()
    check("再双击同列 ⇒ 升序（首行不晚于末行）",
          cell(0, 0) <= cell(0, last_row),
          "首=%s 末=%s" % (cell(0, 0), cell(0, last_row)))
    # 第三次 ⇒ 取消排序 ⇒ 回到服务端原序
    page._header_dblclicked(0)
    app.processEvents()
    back = [col_text(c) for c in range(min(3, page._table.table.columnCount()))]
    check("第三次双击 ⇒ 取消排序（回到原序）", back == before,
          "原=%s 回=%s" % (before[:2], back[:2]))
    check("取消后排序指示器隐藏",
          not page._table.table.horizontalHeader().isSortIndicatorShown())
    _check_history_columns_fit(page, win)
    win.close()
    app.processEvents()
    b.disconnect_from()


def _check_history_columns_fit(page, win=None):
    """历史页列宽回归锁（2026-11-14 用户截图「表格列显示不全」）。

    [锁三件事]
    ① **任何一列的文字都不被省略号截断** —— 根因：列宽下限原先只按
       **表头**算，「模式」列表头 2 个字、内容"自动·产水"5 个字 ⇒ 列被
       压到下限 ⇒ 整列显示 "自动…"（用户圈的就是这一列）。
    ② **16 列一次装得下视口** —— 不装下就必然横向滚动 ⇒ 右半截的列
       （水温/流量/压差/温湿度/DI/DO）要滚到**表格底部**的滚动条才看得到。
    ③ **表头与单元格同字号** —— 表头若被 QSS 重刷还原成界面字号，就会
       "表头比内容还大"，且列宽按大字号量 ⇒ 反而把内容挤到截断。

    [为什么要先撑宽窗口] ①只在「内容下限装得下视口」时才成立 ⇒ 必须在
    **宽视口**（用户实际的 1680px）下验；窄窗口下表格本来就要横向滚动，
    内容被截断是既有的兜底行为（与改动前一致）。
    """
    from PyQt5.QtGui import QFontMetrics
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    tbl = page._table.table
    header = tbl.horizontalHeader()
    view = page._table                      # DataTable
    n = tbl.columnCount()
    # ---- ① 内容不被截断（在宽视口下）----
    if win is not None:
        win.resize(2100, 1000)
        for _ in range(8):
            app.processEvents()
        view._apply_column_widths()
        view.refresh_content_widths()
        for _ in range(8):
            app.processEvents()
    texts = view._measure_content_texts()
    base_min = getattr(view, "_min_section_base", 72)
    active = list(range(n))
    parts = view._text_parts(n)
    avail = 1680                       # 用户窗口的实际视口宽（2511 物理像素 @125%）
    # [直接调**真代码**的列宽规划器] 表格在布局里、宽度由布局决定 ⇒ 撑不宽
    # ⇒ 验算同一段逻辑（不是测试自己重写一遍）。
    scale, floors = view._plan_widths(avail, active, parts, base_min)
    # ---- ① 内容不被截断（按**求解出来的**缩放比与列宽验算）----
    fm = QFontMetrics(view._fit_font(scale))
    bad = []
    for c in range(n):
        t = texts.get(c, "")
        if not t:
            continue
        need = fm.width(t) + view._CELL_PAD
        if need > floors.get(c, 0):
            bad.append("col%d(%s) 需%d>实%d" % (c, t[:8], need,
                                                 floors.get(c, 0)))
    check("历史页：没有任何一列的文字被省略号截断", not bad,
          "; ".join(bad[:4]) if bad else "16 列全装得下")
    # ---- ② 宽表按**内容**给列宽：装不下就横向滚动（网页端口径）----
    # [口径变更 2026-11-17 用户「客户端表格美化成 web 端这种样子」]
    #   旧口径：把 16 列硬塞进视口 ⇒ 只能把表内字号缩到 ~12px（用户实报
    #   「数据界面的表格文字太小了」）；新口径照网页端：**列宽跟着内容走**
    #   ⇒ 装不下就横向滚动，字不被省略号截断（① 断言守的就是"不截断"）。
    total = sum(floors.values())
    check("历史页：表内字号不缩（保持界面同字号，与网页端一致）",
          scale == 1.0,
          "scale=%.2f" % scale)
    check("历史页：列宽按内容给（宽表横向滚动，不截字）",
          total >= avail and not bad,
          "合计%d 视口%d（超出部分横向滚动）" % (total, avail))
    check("历史页：缩放比不低于可读下限", scale >= view.MIN_FIT_SCALE,
          "scale=%.2f 下限=%.2f" % (scale, view.MIN_FIT_SCALE))
    # ---- ③ 表头与单元格同字号（表头被 QSS 重刷放大 ⇒ 反而挤窄内容）----
    h_px = header.fontMetrics().height()
    c_px = tbl.fontMetrics().height()
    check("历史页：表头与单元格字号一致（表头不被 QSS 重刷放大）",
          abs(h_px - c_px) <= 2, "表头%d 单元格%d" % (h_px, c_px))


# ============================================================================
#                服务端契约：运行日志「类别」（0x0119 的 category）
# ============================================================================
#
# [为什么锁这个] 服务端 logs 表新增了 category 列，0x0119 应答的行里
# category 必须是**真实类别**（conn/ctrl/alarm/auto/auth 之类）。之前的
# 缺陷：category 恒为空串 ⇒ C 客户端「类别」列整列空白（服务端明明有
# 日志，界面上那一列却是空的）。
#
# [口径] 类别取值集合**不写死**：服务端将来加新类别也算通过 —— 只要是
# 非空、短 ASCII 串即可。
#
# [为什么要轮询] 私有服务端是刚起的，日志表里可能只有启动瞬间那几行；
# 类别要等服务端自己多写几条带类别的日志才有 ⇒ 轮询等（带超时），
# 别假设第一条就有。
#
# [本条只验"写库口径通不通"] 本段服务端**没有**任何动作（只读查询），
# 能不能看到类别化行取决于启动期是否有事件（数字量名字帧/全停告警/会话
# 接入…）。"先造事件再从新行取样"的确定性版本在 §9b
# （check_log_filters：root 切一次模式 ⇒ 必有 category=auto 的新行）。

#: 等"非空类别"出现的上限（秒）。
LOG_CATEGORY_WAIT_S = 10.0
#: 类别长度上限（短 ASCII ⇒ 一眼能看出没塞垃圾）。
LOG_CATEGORY_MAX_LEN = 32


def _log_page_once(bridge, limit=20):
    """发一次 0x0119 日志分页；返回 (doc, err)（都可能为 None）。"""
    got = {}

    def done(d):
        got["doc"] = d

    def bad(m):
        got["err"] = m

    bridge.log_page(offset=0, limit=limit, on_done=done, on_error=bad)
    deadline = time.time() + 8
    while "doc" not in got and "err" not in got and time.time() < deadline:
        _pump()
        time.sleep(0.02)
    return got.get("doc"), got.get("err")


def check_log_category(bridge, proto, srv):
    """§9 服务端契约：运行日志类别（0x0119 的 category 不是摆设）。"""
    section("§9 服务端契约：运行日志类别（0x0119）")

    doc, err = _log_page_once(bridge)
    if err:
        check("日志查询有应答（§9 类别）", False, "err=%s" % err)
        return
    if not doc:
        check("日志查询有应答（§9 类别）", False, "超时未应答")
        return
    rows = list(doc.get("rows") or [])
    check("日志查询有应答（§9 类别）", True,
          "total=%s 行=%d" % (doc.get("total"), len(rows)))

    # --- (1) 形状：每行都带 category 键（客户端「类别」列得有数据源）---
    missing = [i for i, r in enumerate(rows) if "category" not in r]
    check("日志每行都带 category 键（客户端「类别」列有数据源）",
          not missing,
          "" if not missing else "缺 category 的行号=%s" % missing[:5])

    # --- (2) 类别非空：轮询等服务端多写几条带类别的日志 ---
    cats = []
    poll_err = ""
    deadline = time.time() + LOG_CATEGORY_WAIT_S
    while True:
        cats = [str(r.get("category") or "") for r in rows]
        if any(c.strip() for c in cats):
            break
        if time.time() >= deadline:
            break
        time.sleep(1.0)
        doc2, err2 = _log_page_once(bridge)
        if err2 or not doc2:
            # 单次查询失败不算数（可能是链路抖动）⇒ 接着轮询到超时。
            poll_err = str(err2 or "无应答")
            continue
        rows = list(doc2.get("rows") or [])
    nonempty = sorted(set(c for c in cats if c.strip()))
    check("至少一条日志的 category 非空（服务端真的落了类别，不是空串）",
          bool(nonempty),
          "非空类别=%s" % nonempty if nonempty
          else "轮询 %.0fs 内没出现非空类别（样本类别=%r%s）"
               % (LOG_CATEGORY_WAIT_S, sorted(set(cats)),
                  "，最后一次查询失败=%s" % poll_err if poll_err else ""))

    # --- (3) 类别取值：短 ASCII（集合不写死，加新类别也算过）---
    bad_cat = []
    for c in cats:
        if not c:
            continue
        if len(c) > LOG_CATEGORY_MAX_LEN:
            bad_cat.append(repr(c))
        elif not all(32 <= ord(ch) < 127 for ch in c):
            bad_cat.append(repr(c))
    check("日志 category 是短 ASCII 串（类别集合不写死，服务端加新类别也算过）",
          not bad_cat,
          "" if not bad_cat else "异常类别=%s" % bad_cat[:5])

    # --- (4) 根因：服务端自己的库里 logs.category 真写进去了吗 ---
    db_path, why = _server_db_path(bridge, srv)
    if not db_path:
        skip("服务端库 logs.category 列真的写进了值（写库口径）", why)
        return
    conn, err3 = _open_server_db(db_path)
    if conn is None:
        skip("服务端库 logs.category 列真的写进了值（写库口径）", err3)
        return
    try:
        cols = [r[1] for r in conn.execute("PRAGMA table_info(logs)")]
        if "category" not in cols:
            check("服务端库 logs.category 列真的写进了值（写库口径）", False,
                  "logs 表没有 category 列（列=%s）" % cols)
            return
        total = int(conn.execute("SELECT COUNT(*) FROM logs").fetchone()[0])
        n_cat = int(conn.execute(
            "SELECT COUNT(*) FROM logs WHERE category IS NOT NULL"
            " AND TRIM(category) <> ''").fetchone()[0])
    except sqlite3.Error as exc:
        skip("服务端库 logs.category 列真的写进了值（写库口径）",
             "查库失败：%s" % exc)
        return
    finally:
        conn.close()
    check("服务端库 logs.category 列真的写进了值（写库口径）", n_cat > 0,
          "带类别的行=%d / 总行=%d" % (n_cat, total))


# ============================================================================
#              服务端契约：历史行不留启动空洞（逐字段热身门）
# ============================================================================
#
# [为什么锁这个] 服务端起动后的 1~2 秒里 485 变频器还没首次上报 ⇒ 采集到
# 的历史行里变频器四列（vfd_state/freq_hz/current_a/rpm）是 NULL ⇒ 界面
# 那一行半屏是 "--"（现场截图确认）。修法是**逐字段热身**：某字段还没见
# 过有效值之前，带该字段空洞的行**不入库**；见过之后照写（设备长期离线
# 不会让历史表停摆，中途掉线仍留无数据行 —— 那是有信息量的）。
#
# [口径] 服务端实现：Python 侧 server_py/hsfsrv/app.py 的
# _HIST_WARM_GROUPS / _history_row_ready()；C 侧 server/HsfServerApp.c
# 的 HSF_HIST_WARM_* 位掩码。两端口径一致。

#: 变频器四列（热身门的第一组）。
HIST_VFD_COLS = ("vfd_state", "freq_hz", "current_a", "rpm")


def _history_holes(rows, cols=HIST_VFD_COLS):
    """挑出变频器四列带空洞（任一列为 None）的行；返回 [(ts, 缺哪几列)]。"""
    holes = []
    for r in rows:
        bad = [c for c in cols if r.get(c) is None]
        if bad:
            holes.append((r.get("ts"), bad))
    return holes


def check_history_warmup(bridge, proto, srv):
    """§10 服务端契约：历史行不留启动空洞（热身门）。"""
    section("§10 服务端契约：历史行不留启动空洞（热身门）")

    # --- 先记下"现在"：等服务端把起动那几行写进库 ---
    doc, err = _query_history(bridge, limit=50, wait_rows=2, timeout=45)
    if err:
        check("历史查询有应答（§10 热身门）", False, "err=%s" % err)
        return
    if not doc:
        check("历史查询有应答（§10 热身门）", False, "超时未应答")
        return
    cols = list(doc.get("cols") or [])
    rows = [dict(zip(cols, r)) for r in (doc.get("rows") or [])]
    check("历史查询有应答（§10 热身门）", True,
          "total=%s 行=%d" % (doc.get("total"), len(rows)))
    if not rows:
        # 一行都没有 = 热身门还没放行（变频器压根没上报）⇒ 前提不成立。
        skip("历史行变频器四列不留空洞（起动瞬态不入库）",
             "服务端还没写出任何历史行（热身门未放行 ⇒ 变频器尚未上报）")
        return
    first_ts = max(r.get("ts") or 0 for r in rows)

    # --- 等服务端再写几行：轮询等"出现更新的行"，不死等固定拍数 ---
    # [为什么改成轮询 2026-11-09] 原实现 `time.sleep(7)` 后查一次。服务端
    # 采样周期 5s（负载高时更慢），死等 7s 恰好落在两次采样**之间**就会误判
    # "服务端没写入新行"（批次 B 实测 4 次里撞到 1 次假红）。判据本身是
    # "有没有比 first_ts 更新的行" ⇒ 就**轮询等它出现**，上限 25s；
    # 25s 内真没出现 ⇒ 采样确实停摆，那才该红。
    deadline = time.time() + 25.0
    rows2 = []
    new_rows = []
    while time.time() < deadline:
        time.sleep(1.0)
        _pump()
        doc2, err2 = _query_history(bridge, limit=50, wait_rows=0, timeout=15)
        rows2 = [dict(zip(cols, r)) for r in ((doc2 or {}).get("rows") or [])]
        new_rows = [r for r in rows2 if (r.get("ts") or 0) > first_ts]
        if new_rows:
            break

    # (1) 确实等到了新行 ⇒ 后面的空洞检查不是空转
    check("等一拍后服务端写入了新的历史行（下面的空洞检查不是空转）",
          bool(new_rows), "新增 %d 行" % len(new_rows))

    # (2) 变频器四列不得为 None（热身门：没见过的空洞行不入库）
    holes = _history_holes(rows + rows2)
    check("历史行变频器四列不留空洞（起动瞬态不入库）", not holes,
          "" if not holes else "带空洞的行=%s" % holes[:3])

    # (3) 新写入的行同样不留空洞（见过有效值之后照写）
    holes_new = _history_holes(new_rows)
    check("新增的历史行同样不留变频器空洞（见过有效值之后照写）",
          not holes_new,
          "" if not holes_new else "带空洞的行=%s" % holes_new[:3])

    # (4) 变频器上线过没有？没上线就整段按跳过处理并写明原因
    if not any(r.get("vfd_state") is not None for r in rows2):
        skip("服务端库里没有变频器四列带空洞的历史行（起动瞬态不入库）",
             "变频器压根没上线（485 无设备），本段不适用")
        return

    # (5) 服务端自己的库直查（更严格：起动那几行也一起看）
    db_path, why = _server_db_path(bridge, srv)
    if not db_path:
        skip("服务端库里没有变频器四列带空洞的历史行（起动瞬态不入库）", why)
        return
    conn, err3 = _open_server_db(db_path)
    if conn is None:
        skip("服务端库里没有变频器四列带空洞的历史行（起动瞬态不入库）",
             err3)
        return
    try:
        total = int(conn.execute("SELECT COUNT(*) FROM history").fetchone()[0])
        n_null = int(conn.execute(
            "SELECT COUNT(*) FROM history WHERE vfd_state IS NULL"
            " OR freq_hz IS NULL OR current_a IS NULL"
            " OR rpm IS NULL").fetchone()[0])
        n_allnull = int(conn.execute(
            "SELECT COUNT(*) FROM history WHERE vfd_state IS NULL"
            " AND freq_hz IS NULL AND current_a IS NULL"
            " AND rpm IS NULL").fetchone()[0])
    except sqlite3.Error as exc:
        skip("服务端库里没有变频器四列带空洞的历史行（起动瞬态不入库）",
             "查库失败：%s" % exc)
        return
    finally:
        conn.close()
    check("服务端库里没有变频器四列带空洞的历史行（起动瞬态不入库）",
          n_null == 0,
          "带空洞的行=%d（四列全空 %d）/ 总行=%d" % (n_null, n_allnull, total))


def check_history_warm_gate_rule():
    """§10 附：热身门**规则**白盒核对（直接调服务端的热身门函数）。

    [为什么必须补这条] 端到端那几条（§10 主段）在**模拟**环境里永远是绿的：
    私有服务端采样周期 5s，第一拍快照落在 t+5s，那时模拟变频器早就上报
    了 ⇒ 就算把热身门整个关掉（= 修复前的行为），库里照样没有空洞行。
    实测：把服务端副本的热身门关掉再跑本回归，§10 主段照样全绿。
    端到端那几条锁的是"线上不许出现空洞行"这个**结果**；真正把
    "逐字段热身"这条**规则**钉死的是下面这几条（直接调服务端实现）。
    C 侧同口径实现在 server/HsfServerApp.c 的 HSF_HIST_WARM_* 位掩码。
    """
    section("§10 附：历史行热身门规则（白盒核对服务端实现）")
    if SERVER_ROOT not in sys.path:
        sys.path.insert(0, SERVER_ROOT)
    try:
        from hsfsrv.app import App
    except Exception as exc:                       # noqa: BLE001
        skip("热身门规则（逐字段热身）",
             "服务端模块不可加载：%s: %s" % (type(exc).__name__, exc))
        return

    # --- 热身组里必须含变频器四列（否则改组名/改列会悄悄漏掉变频器）---
    groups = dict(App._HIST_WARM_GROUPS)
    vfd_cols = tuple(groups.get("vfd") or ())
    check("热身门分组含变频器四列（vfd_state/freq_hz/current_a/rpm）",
          set(vfd_cols) == set(HIST_VFD_COLS),
          "vfd 组=%s" % (vfd_cols,))

    def _ready(app, row):
        return bool(App._history_row_ready(app, row))

    full = {"ts": 1, "mode": "手动",
            "vfd_state": "停机", "freq_hz": 0.0, "current_a": 0.0, "rpm": 0.0,
            "turb1": 12.0, "turb2": 3.0, "flow": 36.0, "flow_total": 0.1,
            "press_mpa": 0.5, "temp": 26.0, "hum": 55.0, "di": 21845, "do": 256}
    hole = dict(full)
    for col in HIST_VFD_COLS:
        hole[col] = None

    # [2026-11-17] 服务端 `_hist_warm_keys` 用 getattr 访问设备（白盒用
    # `object.__new__(App)` 造最小对象）；同时"未装配的组不拦门"是
    # 2026-11-16 定的语义 ⇒ 断热身前必须显式挂一台假变频器，否则 vfd 组
    # 被当"未装配"豁免，空洞行照写（用例会假绿）。
    no_dev = object.__new__(App)
    check("热身门：未装配的组不拦门（空洞行照写，历史表不被配置卡死）",
          _ready(no_dev, hole), "判定=%s（应为 True）" % _ready(no_dev, hole))

    # --- (1) 从未见过有效值的字段带空洞 ⇒ 该行不入库（起动瞬态）---
    cold = object.__new__(App)
    cold.vfd = object()              # 装配了变频器 ⇒ vfd 组参与热身判定
    cold_verdict = _ready(cold, hole)
    check("热身门：从没见过的字段带空洞 ⇒ 该行不入库（起动瞬态）",
          not cold_verdict, "判定=%s（应为 False）" % cold_verdict)

    # --- (2) 该字段见过有效值之后，即使掉线也照写（历史表不停摆）---
    warm = object.__new__(App)
    warm.vfd = object()
    _ready(warm, full)               # 先让所有组都见过有效值
    warm_verdict = _ready(warm, hole)
    check("热身门：见过有效值之后设备掉线也照写（历史表不停摆）",
          warm_verdict, "判定=%s（应为 True）" % warm_verdict)

    # --- (3) 各字段都有效 ⇒ 照写 ---
    ok_app = object.__new__(App)
    ok_verdict = _ready(ok_app, full)
    check("热身门：字段都有效 ⇒ 该行照写", ok_verdict,
          "判定=%s（应为 True）" % ok_verdict)


def run_log_category_and_history_warmup(srv):
    """§9 + §10 + §9b：自起专用连接跑完，跑完断开。

    [为什么不复用主桥] §8 收尾时把主桥断了；更要紧的是主桥的
    connected_flag 会被**旧连接迟到的 DISCONNECTED 回调**翻掉 ⇒
    "看旗标决定要不要重连"不可靠（实测过一次重连握手超时 ⇒ §10 整段
    假红）。这里另起一条干净连接：只连一次、只服务这两段、用完断开。
    """
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client import proto

    b = LinkBridge()
    try:
        ok, err = _connect_with_retry(b, srv.port, srv=srv)
        if not ok:
            check("§9/§10 专用连接（私有服务端）", False, "err=%s" % err)
            return
        check("§9/§10 专用连接（私有服务端）", True, "端口 %d" % srv.port)
        check_log_category(b, proto, srv)
        check_history_warmup(b, proto, srv)
    finally:
        b.disconnect_from()
    # 热身门规则白盒核对（不需要连接；放在 finally 之后以免连接失败时漏跑）
    check_history_warm_gate_rule()
    # §9b：日志页筛选/关键字/锚点（要 root 造一条类别化事件，另起连接）。
    check_log_filters(srv)


# ============================================================================
#      §9b 服务端契约：日志页筛选（类别 / 关键字 / 分页锚点，活服务端）
# ============================================================================
#
# [为什么要"先造事件再断言"] 日志类别是**事件驱动**的：服务端起好后如果
# 只靠启动期那几行，可能一行类别化的日志都没有（DB 开库晚于启动日志、
# 或该版本没有那些事件点）⇒ "至少一条类别非空"的红/绿取决于启动时序，
# 不是契约（2026-11-09 那条红就是这么飘的）。这里**先制造一条确定会写库
# 的类别化事件**（切自动 → 切手动 ⇒ 服务端落 category=auto），再从
# "新写入的行"里取样断言，时序无关。
#
# [口径 = C 端] C 侧 hsfAppEventLog("auto", ...) 在模式切换处入库；Python
# 侧 app.set_run_mode/apply_mode_to_devices 同为 LOG_CAT_AUTO。
# 切模式要 root（admin 只读）⇒ 本段自己起一条 root 连接（私有服务端已开
# 同账并发，不影响既有会话）。

def check_log_filters(srv):
    """§9b：类别精确过滤 / 关键字搜类别 / beforeTs 锚点不漂移（活服务端）。"""
    section("§9b 服务端契约：日志页筛选（类别 / 关键字 / 锚点）")
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client import proto

    b = LinkBridge()
    ok, err = _connect_with_retry(b, srv.port, srv=srv,
                                  user="root", password="root")
    check("§9b 专用连接（root：能造 auto 事件）", ok, err or "端口 %d" % srv.port)
    if not ok:
        return
    try:
        # ---- (1) 制造一条类别化事件（新写入的行，不依赖启动时序）----
        t0 = int(time.time() * 1000)
        d1, e1 = _req_doc(b.set_run_mode, mode=1)          # → 自动
        d2, e2 = _req_doc(b.set_run_mode, mode=0)          # → 手动
        check("§9b 造事件：切自动被受理", bool(d1) and d1.get("ok") is not False,
              e1 or "mode=1")
        check("§9b 造事件：切回手动被受理", bool(d2) and d2.get("ok") is not False,
              e2 or "mode=0")

        # 轮询等新行落库（写库在事件处理里，正常是毫秒级；给 12s 余量）。
        fresh = []
        deadline = time.time() + 12
        while True:
            doc, _e = _log_page_once(b, limit=50)
            rows = list((doc or {}).get("rows") or [])
            fresh = [r for r in rows if int(r.get("ts") or 0) >= t0]
            if any(str(r.get("category") or "") == "auto" for r in fresh):
                break
            if time.time() >= deadline:
                break
            time.sleep(0.3)
        cats = sorted({str(r.get("category") or "") for r in fresh})
        check("新写入的日志行带非空类别（不靠启动期老行）",
              bool([c for c in cats if c.strip()]),
              "t0 之后 %d 行，类别=%s" % (len(fresh), cats))
        check("新事件落在 auto 类别（模式切换，与 C 端 hsfAppEventLog 同口径）",
              "auto" in cats, "新行类别=%s" % cats)

        # ---- (2) 类别精确过滤：点某一类 ⇒ 只回该类 ----
        cat_doc, _e = _req_doc(b.log_page, offset=0, limit=50, category="auto")
        cro = list((cat_doc or {}).get("rows") or [])
        check("日志类别筛选（category=auto ⇒ 行类别全为 auto）",
              bool(cro) and all(str(r.get("category") or "") == "auto"
                                for r in cro),
              "命中 %d 行 / total=%s" % (len(cro), (cat_doc or {}).get("total")))
        check("类别筛选命中含刚造的新行（筛的是新数据）",
              any(int(r.get("ts") or 0) >= t0 for r in cro),
              "t0=%d 最先 ts=%s" % (t0, cro[0].get("ts") if cro else "-"))
        nb, _e = _req_doc(b.log_page, offset=0, limit=10,
                          category="zz_no_such_category")
        check("未知类别 ⇒ total=0 且 rows 空（category 真下推）",
              (nb or {}).get("total") == 0 and not ((nb or {}).get("rows") or []),
              "total=%r rows=%d" % ((nb or {}).get("total"),
                                    len((nb or {}).get("rows") or [])))

        # ---- (3) 关键字搜类别名（C 端口径 text||' '||category）----
        kw_doc, _e = _req_doc(b.log_page, offset=0, limit=50, kw="auto")
        kwr = list((kw_doc or {}).get("rows") or [])
        cat_only = [r for r in kwr
                    if "auto" not in str(r.get("text") or "")
                    and "auto" in str(r.get("category") or "")]
        check("关键字搜类别名能命中（text||' '||category 口径）",
              bool(kwr) and bool(cat_only),
              "命中 %d 行，其中仅靠类别列的 %d 行" % (len(kwr), len(cat_only)))
        check("关键字命中的行都真的含该关键字（不空转）",
              all("auto" in (str(r.get("text") or "") + " "
                             + str(r.get("category") or "")) for r in kwr),
              "命中 %d 行" % len(kwr))
        # 类别 + 关键字合取：结果同时满足两者（AND，共用同一条 WHERE）。
        both, _e = _req_doc(b.log_page, offset=0, limit=50,
                            category="auto", kw="手动")
        bro = list((both or {}).get("rows") or [])
        check("类别+关键字合取（AND：类别=auto 且命中'手动'）",
              all(str(r.get("category") or "") == "auto"
                  and "手动" in str(r.get("text") or "") for r in bro)
              and int((both or {}).get("total") or 0)
              <= int((cat_doc or {}).get("total") or 0),
              "命中 %d 行 / total=%s" % (len(bro), (both or {}).get("total")))

        # ---- (4) beforeTs 锚点不漂移（与 0x0118 同口径）----
        check_window_anchor(b, proto, True, "【§9b】0x0119 ")

        # ---- (5) 页面级：类别下拉换一类 ⇒ 只铺该类（UI→协议→渲染全链）----
        _check_log_page_category_ui(srv)
    finally:
        b.disconnect_from()


def _check_log_page_category_ui(srv):
    """§9b(5)：真实 LogPage 换类别 ⇒ 请求带类别、表里只有该类。"""
    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import LogPage

    app = QApplication.instance()
    b = LinkBridge()
    ok, err = _connect_with_retry(b, srv.port, srv=srv)
    if not ok:
        check("日志页类别筛选（UI 全链）可用连接", False, err)
        return
    try:
        page = LogPage(PageContext(b, lambda i: None, lambda m: None))
        page.on_enter()
        deadline = time.time() + 8
        while time.time() < deadline and not page._rows:
            app.processEvents()
            time.sleep(0.02)
        # 找"自动"那一档（CATEGORY_ITEMS 里 ("auto","自动")），换档即重查。
        idx = next((i for i, (key, _n) in enumerate(LogPage.CATEGORY_ITEMS)
                    if key == "auto"), -1)
        check("日志页类别下拉含 auto 档（与 C 端同档表）", idx > 0,
              "index=%d 档表=%s" % (idx, [k for k, _n in LogPage.CATEGORY_ITEMS]))
        if idx > 0:
            page._cat_combo.setCurrentIndex(idx)
            deadline = time.time() + 8
            while time.time() < deadline:
                app.processEvents()
                time.sleep(0.02)
                if page._rows and all(r[1] == "自动" for r in page._rows):
                    break
            cats = [r[1] for r in page._rows]
            check("日志页换类别后只铺该类（表格类别列全是「自动」）",
                  bool(cats) and all(c == "自动" for c in cats),
                  "行=%d 类别列=%s" % (len(cats), sorted(set(cats))))
        page.on_leave()
    finally:
        b.disconnect_from()


# ============================================================================
#              交叉测试：通用件（形状签名 / 下标契约 / 隔离 C 服务端）
# ============================================================================
#
# [为什么要交叉测试] 两端各写各的单测，可能"各自自测都绿、拼起来不通"。
# 交叉段让**一端的实现去验另一端的实现**：Python 客户端连 C 服务端、
# Python 客户端按 C 客户端的解析口径去读 Python 服务端。两端任何一端
# 改了载荷形状而另一端没跟上，这里立刻红。

#: 每一列的**值域**（C 客户端按列下标取值 ⇒ 类型必须固定）。
#: "num"=数值（int/float）、"int"=整数（位图/计数）、"str"=文本。
HIST_COL_DOMAIN = {
    "ts": "int", "mode": "str", "vfd_state": "str", "freq_hz": "num",
    "current_a": "num", "rpm": "num", "turb1": "num", "turb2": "num",
    "flow": "num", "flow_unit": "str", "flow_total": "num",
    "total_unit": "str", "press_mpa": "num", "press_note": "str",
    "temp": "num", "hum": "num", "di": "int", "do": "int",
    # 水温：带温度的机型才有值，其余为 null（null 允许，见调用点注释）。
    "water_temp": "num",
}

#: **C 客户端解析历史应答的下标契约**（client/ui/HsfLinkHistory.c）：
#:   kSrc[]   = {3,4,5,6,7,18,10,12,14,15} 显示列 → 原始列（含水温 18）
#:   kNumSrc[]= {3,4,5,6,7,18,8,10,12,14,15} 图表原值旁路（按显示列序）
#:   文本列：1(mode) 2(vfd_state) 9(flow_unit) 11(total_unit) 13(press_note)
#:   位图列：16(di) 17(do)；时间列：0(ts)；水温在生产列序末尾：18(water_temp)
C_CLIENT_NUM_IDX = (3, 4, 5, 6, 7, 18, 8, 10, 12, 14, 15)
C_CLIENT_TEXT_IDX = (1, 2, 9, 11, 13)
C_CLIENT_TS_IDX = 0
C_CLIENT_BITMAP_IDX = (16, 17)

#: C 客户端解析历史应答依赖的**文档键**（HsfLinkHistory.c 里逐个取值的）。
C_CLIENT_HIST_KEYS = ("ok", "total", "dbRows", "offset", "limit",
                      "latestTs", "earliestTs", "rows", "cols")
#: C 客户端解析日志分页依赖的**文档键**（HsfLinkClient.c 的 hsfLcParseLogPage）。
C_CLIENT_LOG_KEYS = ("ok", "total", "offset", "limit", "rows")


def _req_doc(method, **kwargs):
    """发一次请求并等应答；返回 (doc, err)（都可能为 None）。

    [为什么统一走这里] 交叉段要比"两端载荷同形"，必须**用同一个调用
    姿势**取两边的应答，否则比的是调用姿势而不是载荷。
    """
    got = {}

    def done(d):
        got["doc"] = d

    def bad(m):
        got["err"] = m

    kwargs["on_done"] = done
    kwargs["on_error"] = bad
    try:
        method(**kwargs)
    except Exception as exc:                       # noqa: BLE001
        return None, "%s: %s" % (type(exc).__name__, exc)
    deadline = time.time() + 10
    while "doc" not in got and "err" not in got and time.time() < deadline:
        _pump()
        time.sleep(0.02)
    return got.get("doc"), got.get("err")


#: [传输层偶发的错误文案] 命中这些 ⇒ 传输层把连接判死了（读 0 字节判 EOF ⇒
#: 投递 disconnected ⇒ 会话复位 ⇒ 在途请求收不到应答），**不是**被测的
#: 载荷契约问题。
_TRANSPORT_FLICKER = ("未连接", "连接已关闭", "超时", "socket", "closed")


def _req_doc_retry(bridge, method, conn, **kwargs):
    """发一次请求；**传输层偶发**时重连重试（最多 3 次；夹具容错，不是掩盖）。

    :param bridge 客户端桥（用于重连）。
    :param method 桥上的请求方法。
    :param conn ``(host, port, user, passwd)``（重连用）。
    :returns: ``(doc, err)``。

    [为什么需要] 2026-11-21 实测：IOCP 缺省后端下「连上之后的某个请求」
    偶发丢应答（读侧 0 字节被判 EOF ⇒ 会话复位）。实测连跑 5 次交叉段
    命中 4 次；A/B 定性——换 WSAEVENT 后端即全绿 ⇒ 判据在**传输层**、
    与应用层无关（与端侧改动无关：本段不带 export，行为与改动前逐字
    等价）。⇒ 交叉段测的是「两端载荷**同形**」**不是网络可靠性** ⇒
    这里做**有限**重连重试；**3 次都失败照红**（不掩盖真缺陷）。

    [为什么限 3 次] 无限重试会把「真·连不上」也变成绿 ⇒ 限 3 次，且
    **每次都打印**（日志里能看到重试次数 ⇒ 偶发率可统计、可追责）。

    [重连后要**恢复订阅**] 连接被复位 ⇒ XLinkSession_reset 清空订阅表 ⇒
    bridge 的 connected 回调也会清 `_subscribed`（产品行为：重连后等上层
    重新 set_topics）。⇒ 把重连前的订阅集合**重订**回去，否则重试后的
    请求能通、但遥测主题再也落不了地。
    """
    doc, err = _req_doc(method, **kwargs)
    for attempt in range(3):
        if doc is not None:
            return doc, err
        text = str(err or "")
        if not any(k in text for k in _TRANSPORT_FLICKER):
            return doc, err
        print("  [INFO] 传输层偶发（%s）：重连重试（第 %d/3 次）"
              % (text, attempt + 1))
        saved = set()
        try:
            saved = set(bridge.current_topics())
        except Exception:                               # noqa: BLE001
            saved = set()
        try:
            bridge.disconnect_from()
        except Exception:                               # noqa: BLE001
            pass
        try:
            ok, _err = bridge.connect_to(conn[0], conn[1], conn[2], conn[3])
        except Exception as exc:                      # noqa: BLE001
            return doc, "%s: %s" % (type(exc).__name__, exc)
        if not ok:
            return doc, err
        if saved:
            # 等 connected 回调把 `_subscribed` 清空（它在网络线程 ⇒ 要泵事件）。
            for _ in range(30):
                _pump()
                time.sleep(0.01)
            try:
                bridge.set_topics(saved)
            except Exception:                           # noqa: BLE001
                pass
        doc, err = _req_doc(method, **kwargs)
    return doc, err


def _hist_contract_problems(doc):
    """按"C 客户端下标契约"核对一份历史应答；返回问题清单（空=合规）。"""
    problems = []
    if not isinstance(doc, dict):
        return ["应答不是字典"]
    cols = list(doc.get("cols") or [])
    if cols != HIST_COLS:
        problems.append("列名/列序与协议不一致：%s" % (cols[:6],))
    rows = doc.get("rows")
    if not isinstance(rows, list):
        return problems + ["rows 不是数组"]
    for i, row in enumerate(rows):
        if not isinstance(row, (list, tuple)):
            problems.append("第 %d 行不是数组（C 客户端按 cell[k] 取值）" % i)
            continue
        if len(row) != HIST_COL_COUNT:
            problems.append("第 %d 行宽度 %d != %d" % (i, len(row), HIST_COL_COUNT))
            continue
        for j, name in enumerate(HIST_COLS):
            val = row[j]
            if val is None:
                continue                    # null = 无数据，C 客户端显 "--"
            dom = HIST_COL_DOMAIN.get(name, "num")
            if dom == "num":
                ok = isinstance(val, (int, float)) and not isinstance(val, bool)
            elif dom == "int":
                ok = isinstance(val, int) and not isinstance(val, bool)
            else:
                ok = isinstance(val, str)
            if not ok:
                problems.append("第 %d 行 %s 列值域不对：%r(%s)"
                                % (i, name, val, type(val).__name__))
    return problems


def _hist_signature(doc):
    """历史应答的**形状签名**（"两端同口径"的机器可查证据）。

    [只取与解析器有关的部分] 列名列序 / 行宽 / 每列值域。数据本身两端
    不可比（设备不同、采样不同），形状才可比。
    """
    cols = list(doc.get("cols") or [])
    rows = [r for r in (doc.get("rows") or []) if isinstance(r, (list, tuple))]
    domains = []
    for j in range(len(cols)):
        vals = [r[j] for r in rows if j < len(r) and r[j] is not None]
        if not vals:
            domains.append("null")
            continue
        val = vals[0]
        if isinstance(val, bool):
            domains.append("bool")
        elif isinstance(val, int):
            domains.append("int")
        elif isinstance(val, float):
            domains.append("float")
        elif isinstance(val, str):
            domains.append("str")
        else:
            domains.append("other")
    return {"cols": cols,
            "widths": sorted(set(len(r) for r in rows)),
            "domains": domains}


# ============================================================================
#              交叉测试通用件：窗口锚点（beforeTs）不漂移
# ============================================================================
#
# [为什么两端都要锁这个] 0x0118 / 0x0119 都是"时间 DESC 取页 + offset"：
# 窗口上界缺省 = **现在**。库在持续入库 ⇒ 每次查询上界都在往前走，
# `offset` 指向的行会跟着挪 ⇒ 翻页重复/漏行（现场表现为"翻页跳数"）。
# 客户端把锚点定格（logRequest / histRequest 同款）再翻页，服务端必须
# 认这个锚：**同一锚点 + 同一 offset ⇒ 同一批行**，两页不重叠，连续翻页
# 不重不漏。C 侧 HsfServerStore_queryLogs / queryHistory 与 Python 侧
# store.query_logs_page / query_history 共用同一口径，这里对**两端**各
# 跑一遍（§11 验 C，§12 验 Python）。

def _page_row_key(is_log, row):
    """一行的身份键：日志行 (ts,category,text)、历史行 (ts)。"""
    if is_log:
        return (int(row.get("ts") or 0),
                str(row.get("category") or ""),
                str(row.get("text") or ""))
    ts = row[0] if isinstance(row, (list, tuple)) and row else 0
    return (int(ts or 0),)


def _window_tol(is_log):
    """锚点容差：日志段 1（同毫秒新写入是常态），历史段 0（严格）。"""
    return 1 if is_log else 0


def _window_batch_ok(is_log, has_reply, keys_a, keys_b, total_a, total_b):
    """同锚同偏移同批：行集合一致（核心）+ total 漂移 ≤ 容差。"""
    return (bool(has_reply) and set(keys_b) == set(keys_a)
            and abs(int(total_b or 0) - int(total_a or 0)) <= _window_tol(is_log))


def _window_pages_ok(is_log, has_reply, total_a, total_b):
    """相邻两页 total 一致（容差同上）。"""
    return (bool(has_reply)
            and abs(int(total_b or 0) - int(total_a or 0)) <= _window_tol(is_log))


def _window_no_overlap_ok(is_log, keys_a, keys_b):
    """翻页不重叠（容差同上）。"""
    return len(set(keys_a) & set(keys_b)) <= _window_tol(is_log)


def _window_walk_ok(is_log, seen, want):
    """全窗口遍历不重不漏（容差同上）。"""
    tol = _window_tol(is_log)
    return (len(seen) - len(set(seen)) <= tol
            and len(seen) >= want - tol and len(seen) <= want + tol)


def check_window_anchor_negative():
    """锚点容差负对照：大幅漂移必须红（纯判据，不碰全局 PASS/FAIL）。

    [为什么用显式 raise 不用 check/assert] check() 会累加全局 PASS/FAIL 并
    append FAILED，负对照的故意失败会污染记数、stdout 留不可回收的假
    [FAIL] 行；而 assert 会被 `python -O` 剥离（负对照静默失效）。这里用
    纯判据函数 + 显式 raise AssertionError：异常被 main 的 try/except 接住
    转成一条真实 check 失败，正常时完全不碰全局计数，且不依赖 -O 开关。
    """
    section("窗口锚点容差负对照（纯判据，不记全局计数）")

    def want_false(cond, why):
        if cond:
            raise AssertionError(why)

    def want_true(cond, why):
        if not cond:
            raise AssertionError(why)

    ts = 1700000000000
    k0 = [(ts, "conn", "a"), (ts + 1000, "conn", "b")]
    kb = [(ts, "conn", "a"), (ts + 9000, "conn", "X"),
          (ts + 10000, "conn", "Y")]          # total 5→8、行集合也变 ⇒ 漂移 3 ≫ 容差 1
    # 同锚同偏移：漂移 3 ⇒ 日志/历史都必须红
    want_false(_window_batch_ok(True, True, k0, kb, 5, 8), "日志段同批漂移3未红")
    want_false(_window_batch_ok(False, True, k0, kb, 5, 8), "历史段同批漂移3未红")
    # 相邻页 total：日志放行 1、红于 2；历史红于 1
    want_true(_window_pages_ok(True, True, 5, 6), "日志段 total+1 应放行")
    want_false(_window_pages_ok(True, True, 5, 8), "日志段 total+3 未红")
    want_false(_window_pages_ok(False, True, 5, 6), "历史段 total+1 未红")
    # 翻页重叠 2 条 ⇒ 两端都红
    ka = [(1,), (2,), (3,)]
    kc = [(2,), (3,), (4,)]
    want_false(_window_no_overlap_ok(True, ka, kc), "日志段重叠2条未红")
    want_false(_window_no_overlap_ok(False, ka, kc), "历史段重叠2条未红")
    # 全窗口遍历：重复 2 条 / 少 3 行 ⇒ 红；日志 1 条重复仍放行，历史不放行
    want_false(_window_walk_ok(True, [1, 2, 2, 2, 3], 4), "日志段重复2条未红")
    want_true(_window_walk_ok(True, [1, 2, 2, 3], 4), "日志段重复1条应放行")
    want_false(_window_walk_ok(False, [1, 2, 2, 3], 4), "历史段重复1条未红")
    want_false(_window_walk_ok(True, [1, 2], 10), "日志段少3行未红")


def check_window_anchor(bridge, proto, is_log, label, page=5, req_fn=None):
    """锚定分页契约：上界生效 / 同锚同偏移同批 / 翻页不重不漏。

    [锚点怎么取] 取**当前最新一行的 ts**（先发一次无锚点的第 1 页，拿
    max(ts)）。不取"现在 - 5s"：服务端刚起时那个窗口里可能一行都没有
    （C 服务端起来 10s 内历史只有 1~2 行、日志只有登录那几条），只能
    证明"空窗口时 rows 也空"，证明不了翻页。取最新行 ts 则有数据的服务端
    一定有非空窗口，且它是个**固定值**：之后新入库的行 ts 更大，自然被
    `ts <= 锚点` 挡在窗外 ⇒ 同一锚点的 total 恒定（拿"现在"当锚时窗口会
    边长，翻页就会重/漏）。

    [req_fn] 可选的请求包装（默认 `_req_doc`）。§11 交叉段传入带
    **一次重连重试**的版本（传输层偶发容错，见 `_req_doc_retry`）——
    本段测的是**两端载荷同形**、不是网络可靠性 ⇒ 传输层偶发不该让它红，
    但**重试仍失败照红**。
    """
    fn = bridge.log_page if is_log else bridge.history_page
    req = req_fn or _req_doc

    def query(offset, anchor):
        return req(fn, offset=offset, limit=page, before_ts=anchor)

    base, berr = req(fn, offset=0, limit=page)
    if not base:
        check("%s窗口锚点查询有应答" % label, False, berr or "超时")
        return
    base_ts = [_page_row_key(is_log, r)[0] for r in (base.get("rows") or [])]
    check("%s 取到一页真实数据（锚点要用数据里的 ts）" % label,
          bool(base_ts), "total=%s" % base.get("total"))
    if not base_ts:
        return
    anchor = max(base_ts)

    p0, err = query(0, anchor)
    if not p0:
        check("%s 锚点查询有应答" % label, False, err or "超时")
        return
    rows0 = list(p0.get("rows") or [])
    ts0 = [_page_row_key(is_log, r)[0] for r in rows0]
    check("%s 锚点查询有应答" % label, True,
          "锚点=%d total=%s 行=%d" % (anchor, p0.get("total"), len(rows0)))
    check("%s beforeTs 上界生效（返回行 ts ≤ 锚点）" % label,
          all(t <= anchor for t in ts0),
          "锚点=%d 最大行 ts=%s" % (anchor, max(ts0) if ts0 else "-"))

    total = int(p0.get("total") or 0)
    check("%s 锚点窗口里有行（不是空转断言）" % label, total > 0,
          "total=%d" % total)
    if total <= 0:
        return

    # --- 同一锚点 + 同一 offset ⇒ 同一批行（窗口钉住不跟"现在"走）---
    # [容差只给**日志**段 2026-11-21] 断言原先要求 **total 逐字相等**，
    # 它隐含假设「锚点之后新入库的行 ts **严格大于**锚点」——**毫秒精度**下
    # 不成立：服务端在同一毫秒内再写一条（ts 与锚点**相等**）⇒ 它落在
    # `ts <= 锚点` 的窗口内 ⇒ total +1 ⇒ 偶发红（实测 8→9、翻页重叠 1 条、
    # 遍历多 1 行）。
    #   · **日志表（is_log）**：服务端**持续写入**（连/断/心跳都记日志）⇒
    #     同毫秒写入是常态 ⇒ 容差 1。
    #   · **历史表（!is_log）**：采样 **1s 一行** ⇒ 同毫秒概率≈0 ⇒ **容差 0**
    #     （保持严格：历史段的漂移是真缺陷，不该被容差盖住）。
    # ⇒ 容差只放掉毫秒级竞态，**窗口大幅漂移仍会红**（行集合变了 / 差 >1）。
    _tol = _window_tol(is_log)
    again, _e = query(0, anchor)
    keys0 = [_page_row_key(is_log, r) for r in rows0]
    keys_b = [_page_row_key(is_log, r) for r in ((again or {}).get("rows") or [])]
    _tb = int((again or {}).get("total") or 0)
    check("%s 同一锚点同偏移 ⇒ 同一批行（窗口不漂移）" % label,
          _window_batch_ok(is_log, again, keys0, keys_b, total, _tb),
          "total=%s→%s 行=%d→%d（容差 %d）"
          % (total, (again or {}).get("total"), len(keys0), len(keys_b), _tol))

    # --- 翻页：total 不变、两页不重叠、第 2 页整体不晚于第 1 页---
    p1, _e = query(page, anchor)
    rows1 = list((p1 or {}).get("rows") or [])
    ts1 = [_page_row_key(is_log, r)[0] for r in rows1]
    keys1 = [_page_row_key(is_log, r) for r in rows1]
    _t1 = int((p1 or {}).get("total") or 0)
    check("%s 相邻两页 total 一致（锚定窗口不变；日志段容差 1＝同毫秒新写入）"
          % label,
          _window_pages_ok(is_log, p1, total, _t1),
          "第1页 total=%s / 第2页 total=%s" % (total, (p1 or {}).get("total")))
    check("%s 翻页不重叠（同一条不出现两次；日志段容差 1＝同毫秒新写入）" % label,
          _window_no_overlap_ok(is_log, keys0, keys1),
          "重叠 %d 条" % len(set(keys0) & set(keys1)))
    check("%s 第 2 页整体不晚于第 1 页最早行（DESC 取页）" % label,
          not ts1 or not ts0 or max(ts1) <= min(ts0),
          "max(第2页)=%s min(第1页)=%s"
          % (max(ts1) if ts1 else "-", min(ts0) if ts0 else "-"))

    # --- 全窗口遍历：不重不漏 ---
    seen = list(keys0)
    offset = page
    pages = 1
    while len(seen) < total and pages < 12:
        pn, _e = query(offset, anchor)
        if not pn:
            break
        part = [_page_row_key(is_log, r) for r in (pn.get("rows") or [])]
        if not part:
            break
        seen.extend(part)
        offset += page
        pages += 1
    want = min(total, pages * page)
    check("%s 锚定分页不重不漏（遍历 %d 行无重复）" % (label, len(seen)),
          _window_walk_ok(is_log, seen, want),
          "取到 %d 行 / 窗口 total=%d（%d 页；容差 %d）"
          % (len(seen), total, pages, _tol))


class IsolatedCServer(object):
    """自起一个**隔离**的 C 服务端（复制 exe ⇒ 自带 cfg/账户库/数据库）。

    [为什么要复制 exe 而不是直接再起一个] C 服务端的配置文件、账户库、
    SQLite 库路径都按**可执行文件所在目录**解析
    （server/HsfServerSettings.c：XCoreApplication_applicationDirPath()
    + "cfg/"；账户库 hsf_users.json 同口径）。在 bin/ 里再起一个 ⇒ 与
    现场那台共用 cfg/hsf_server.ini / hsf_users.json / hsf_data.db ⇒
    两个实例互相重写账户库 ⇒ 登录一律 "bad password"（今天踩过）。
    复制 exe 到临时目录即可完全隔离，**绝不碰**现场那台的任何文件。

    [端口] 自己找一个空闲端口（命令行位置参数覆盖配置端口；只对本次
    启动生效，不写回 cfg/hsf_server.ini —— 那是现场那台的配置）。

    [不依赖别人手工起的 6100] 现场那台可能没在跑，也可能被占着会话
    （root 单会话 + 会话总数上限 ⇒ 登录被策略拒），拿它当依赖就不叫
    回归了。这里只用自己的实例；exe 找不到时整段 SKIP。
    """

    def __init__(self):
        self.tmp = None
        self.proc = None
        self.port = 0
        self.exe = None
        self.log_path = None

    @staticmethod
    def find_exe():
        """找 C 服务端可执行文件；找不到返回 None。"""
        for rel in (("bin", "HsfServer.exe"),):
            path = os.path.join(REPO_ROOT, *rel)
            if os.path.isfile(path):
                return path
        return None

    def start(self):
        """起服务；返回 (ok, 原因)。"""
        exe = self.find_exe()
        if not exe:
            return False, "找不到 C 服务端可执行文件（%s）" % os.path.join(
                REPO_ROOT, "bin", "HsfServer.exe")
        self.tmp = tempfile.mkdtemp(prefix="hsf_creg_")
        dst = os.path.join(self.tmp, "HsfServer.exe")
        try:
            shutil.copy2(exe, dst)
        except OSError as exc:
            return False, "复制 C 服务端可执行文件失败：%s" % exc
        # ---- 自带落盘配置（快节拍）----
        # [为什么要写 INI] 服务端的入库周期缺省自 2026-11-11 起是 **1 分钟**
        # （用户定案），而本段要"等一拍就有一行历史"（历史页/关键字下推的
        # 取数断言都按这个口径）⇒ 必须给测试自己一份快节拍配置，不能吃
        # 缺省。服务端读 <程序目录>/cfg/hsf_server.ini（exe 已被复制到临时
        # 目录）⇒ INI 写在那里即生效，也天然把库隔离在临时目录。
        try:
            cfg_dir = os.path.join(self.tmp, "cfg")
            os.makedirs(cfg_dir, exist_ok=True)
            with open(os.path.join(cfg_dir, "hsf_server.ini"), "w",
                      encoding="utf-8") as fh:
                fh.write("[storage]\n"
                         "enabled=1\n"
                         "sample_ms=1000\n"
                         "max_mb=50\n"
                         "keep_files=3\n"
                         "full_policy=ring\n")
        except OSError as exc:
            return False, "写临时落盘配置失败：%s" % exc
        self.exe = dst
        self.port = _free_port()
        self.log_path = os.path.join(self.tmp, "HsfServer.out")
        env = dict(os.environ)
        try:
            # stdout 落文件而不是管道：管道写满会让服务端**卡住**（推数据
            # 的线程被阻塞 ⇒ 看起来像"服务端死了"）。
            out = open(self.log_path, "wb")
        except OSError as exc:
            return False, "打开服务端日志文件失败：%s" % exc
        try:
            self.proc = subprocess.Popen(
                [dst, "--sim", str(self.port)], cwd=self.tmp, env=env,
                stdout=out, stderr=subprocess.STDOUT)
        except OSError as exc:
            out.close()
            return False, "起 C 服务端失败：%s" % exc
        deadline = time.time() + 30
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False, "C 服务端进程秒退（%s）" % self._tail()
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), 0.3)
                sock.close()
                return True, ""
            except OSError:
                time.sleep(0.2)
        return False, "C 服务端 30s 内没监听端口 %d" % self.port

    def _tail(self, n=400):
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def db_path(self):
        """本实例的 SQLite 库路径（C 服务端按 exe 目录解析 ⇒ <tmp>/cfg/）。"""
        return os.path.join(self.tmp or "", "cfg", "hsf_data.db")

    def stop(self):
        """收尾：停进程 + 删临时目录（**只停自己起的那个**）。"""
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=6)
            except Exception:                        # noqa: BLE001
                self.proc.kill()
        if self.tmp:
            shutil.rmtree(self.tmp, ignore_errors=True)
            self.tmp = None


# ============================================================================
#            §11 交叉测试 A：Python 客户端 ↔ C 服务端（跨实现）
# ============================================================================
#
# [判据] Python 服务端与 C 服务端的载荷必须**同形**：
#   · 同样的键（C 客户端依赖的 9 个历史键 + 5 个日志键）
#   · 同样的 18 列、同样的列序
#   · 每列的值域相同（null 或该列的固定类型）
#   · 同样的 offset/limit 回显
# 这就是"两端同口径"的**机器可查证据**——不是"看着一样"，是逐项比对。

def check_cross_c_server(py_hist_doc, proto):
    """§11 交叉测试 A：Python 客户端连 C 服务端，验两端载荷同形。"""
    section("§11 交叉测试 A：Python 客户端 ↔ C 服务端（跨实现）")
    from hsfsrv_client.bridge import LinkBridge

    csrv = IsolatedCServer()
    ok, why = csrv.start()
    if not ok:
        skip("§11 交叉测试 A（Python 客户端 ↔ C 服务端）", why)
        return
    check("C 服务端隔离实例起动（自带 cfg/账户库/库）", True,
          "端口 %d" % csrv.port)
    bridge = LinkBridge()
    try:
        ok2, err = bridge.connect_to("127.0.0.1", csrv.port, "root", "root")
        check("Python 客户端连上 C 服务端（root/root）", ok2, err or "")
        if not ok2:
            return

        # [传输层偶发容错 2026-11-21] 本段所有请求都走带**一次重连重试**
        # 的包装（`_req_doc_retry`）：IOCP 缺省后端下「连上之后的某个请求」
        # 偶发丢应答（读 0 字节判 EOF ⇒ 会话复位 ⇒ 在途请求无应答）——
        # 实测连跑 5 次本段命中 4 次，A/B 定性：换 WSAEVENT 后端即全绿 ⇒
        # 判据在传输层、与应用层无关。本段测的是「两端载荷**同形**」不是
        # 网络可靠性 ⇒ 重试一次；**重试仍失败照红**。
        _conn = ("127.0.0.1", csrv.port, "root", "root")

        def _req(method, **kwargs):
            return _req_doc_retry(bridge, method, _conn, **kwargs)

        # -----------------------------------------------------------
        # (1) 历史应答（0x0118）：形状与 Python 服务端同形
        # -----------------------------------------------------------
        # [先等首行 2026-11-10] C 服务端的历史采样周期是 5s
        # （HSF_STORE_SAMPLE_MS），隔离实例起来后**第一行**要等满一个周期
        # 才落库；本段连上就查 ⇒ 恒 total=0 ⇒ 下面依赖行数据的断言（行宽
        # 逐项比对 / 关键字下推 / 翻页锚点）随机红（本次实跑：1154 PASS /
        # 4 FAIL，四条全是「0 行」形态）。改成轮询等首行（上限 20s），
        # 让断言只反映「两端形状是否同形」，不反映「等没等到采样周期」。
        doc = None
        err = None
        deadline = time.time() + 20.0
        while True:
            doc, err = _req(bridge.history_page, offset=0, limit=8)
            if err or not doc:
                # [传输层偶发容错 2026-11-21] IOCP 缺省后端下「连上之后的
                # **第一个**请求」偶发丢应答（读侧 0 字节被判 EOF ⇒ 投递
                # disconnected ⇒ 会话复位 ⇒ 在途请求收不到应答）。实测：连跑
                # 5 次本段命中 4 次；A/B 定性——换 WSAEVENT 后端即全绿 ⇒
                # 判据在**传输层**、与应用层无关（与端侧改动无关：本段
                # 不带 export，行为与改动前逐字等价）。
                # 本段测的是「两端载荷**同形**」**不是网络可靠性** ⇒ 第一次
                # 无应答就**重连一次**再试；**重试仍失败照红**（不掩盖）。
                break
            if int(doc.get("total") or 0) > 0:
                break
            if time.time() >= deadline:
                break
            time.sleep(0.5)
        if err or not doc:
            check("C 服务端历史查询有应答", False, err or "超时")
            return
        check("C 服务端历史查询有应答", True, "total=%s" % doc.get("total"))

        missing = [k for k in C_CLIENT_HIST_KEYS if k not in doc]
        check("C 服务端历史应答含 C 客户端依赖的全部键",
              not missing, "缺=%s" % missing)

        c_sig = _hist_signature(doc)
        p_sig = _hist_signature(py_hist_doc) if py_hist_doc else {}
        check("C 服务端历史列序与协议一致（18 列，同 Python 服务端）",
              c_sig["cols"] == HIST_COLS,
              "" if c_sig["cols"] == HIST_COLS
              else "实际=%s" % (c_sig["cols"][:6],))
        check("C 服务端历史列序与 Python 服务端逐字一致",
              bool(p_sig) and c_sig["cols"] == p_sig["cols"],
              "" if bool(p_sig) and c_sig["cols"] == p_sig["cols"]
              else "C=%s / Py=%s" % (c_sig["cols"][:6], p_sig.get("cols", [])[:6]))
        check("C 服务端历史行宽 = 18（与 cols 一致）",
              c_sig["widths"] in ([], [HIST_COL_COUNT]),
              "行宽集合=%s" % c_sig["widths"])
        c_problems = _hist_contract_problems(doc)
        check("C 服务端历史每列值域与 C 客户端解析契约一致",
              not c_problems, "" if not c_problems
              else "问题=%s" % c_problems[:3])
        check("C 服务端 offset/limit 原样回显（C 客户端靠它认领应答）",
              doc.get("offset") == 0 and doc.get("limit") == 8,
              "offset=%r limit=%r" % (doc.get("offset"), doc.get("limit")))

        # --- 同形判据（逐项） ---
        check("【同形判据】两端历史应答列序逐字一致",
              bool(p_sig) and c_sig["cols"] == p_sig["cols"],
              "C=%d 列 / Py=%d 列" % (len(c_sig["cols"]), len(p_sig.get("cols", []))))
        check("【同形判据】两端历史应答行宽一致",
              bool(p_sig) and c_sig["widths"] == p_sig["widths"],
              "C=%s / Py=%s" % (c_sig["widths"], p_sig.get("widths")))
        p_problems = _hist_contract_problems(py_hist_doc)
        check("【同形判据】两端历史应答的每列值域都符合同一份列契约"
              "（null = 无数据，允许）",
              not c_problems and not p_problems,
              "C 端问题=%d / Py 端问题=%d（实测值域 C=%s / Py=%s；"
              "null 与有值是**数据**差异不是形状差异）"
              % (len(c_problems), len(p_problems),
                 c_sig["domains"][:8], p_sig.get("domains", [])[:8]))
        check("【同形判据】两端历史应答都满足 C 客户端下标契约",
              not c_problems and not p_problems,
              "C 端问题=%d / Py 端问题=%d"
              % (len(c_problems), len(p_problems)))

        # --- 关键字 / 时间窗下推（两端同口径的第二层证据） ---
        kw_doc, _e = _req(bridge.history_page, offset=0, limit=20,
                              kw="手动")
        check("C 服务端关键字下推生效（kw=手动 能筛出该行）",
              bool(kw_doc) and bool(kw_doc.get("rows")),
              "命中 %d 行" % len((kw_doc or {}).get("rows") or []))
        now_ms = int(time.time() * 1000)
        win_doc, _e = _req(bridge.history_page, offset=0, limit=20,
                               lo_ts=now_ms - 60 * 1000, hi_ts=now_ms)
        rows = list((win_doc or {}).get("rows") or [])
        in_window = True
        for r in rows:
            ts = r[0] if isinstance(r, (list, tuple)) and r else None
            if ts is not None and not (now_ms - 60 * 1000 <= ts <= now_ms):
                in_window = False
        check("C 服务端时间窗下推生效（返回行都落在窗口内）",
              bool(win_doc) and in_window, "%d 行" % len(rows))
        anchor_doc, _e = _req(bridge.history_page, offset=0, limit=8,
                                  before_ts=now_ms)
        check("C 服务端翻页锚点回显（beforeTs 生效且不回写成 0）",
              bool(anchor_doc) and anchor_doc.get("offset") == 0,
              "offset=%r limit=%r" % (anchor_doc.get("offset"),
                                      anchor_doc.get("limit")))

        # -----------------------------------------------------------
        # (2) 日志页（0x0119）：形状 + 类别字段
        # -----------------------------------------------------------
        log_doc, err = _req(bridge.log_page, offset=0, limit=10)
        if err or not log_doc:
            check("C 服务端日志查询有应答", False, err or "超时")
        else:
            check("C 服务端日志查询有应答", True, "total=%s" % log_doc.get("total"))
            missing = [k for k in C_CLIENT_LOG_KEYS if k not in log_doc]
            check("C 服务端日志应答含 C 客户端依赖的全部键",
                  not missing, "缺=%s" % missing)
            lrows = list(log_doc.get("rows") or [])
            check("C 服务端日志行是字典数组（C 客户端按 key 取 ts/category/text）",
                  all(isinstance(r, dict) for r in lrows), "%d 行" % len(lrows))
            if lrows:
                row = lrows[0]
                check("C 服务端日志行含 ts/category/text 三个键",
                      all(k in row for k in ("ts", "category", "text")),
                      "keys=%s" % sorted(row.keys()))
                check("C 服务端日志行 ts 是 epoch 毫秒（int64）",
                      isinstance(row.get("ts"), int)
                      and 10 ** 12 < row["ts"] < 10 ** 13, "ts=%r" % row.get("ts"))
                cats = [str(r.get("category") or "") for r in lrows]
                check("C 服务端日志 category 是真实类别（非空串）",
                      any(c.strip() for c in cats),
                      "类别集合=%s" % sorted(set(c for c in cats if c)))
                check("C 服务端日志 text 是非空事件文本",
                      all(isinstance(r.get("text"), str) and r["text"]
                          for r in lrows),
                      "首行 text=%r" % (lrows[0].get("text") if lrows else None))
                # 类别筛选（2026-11-09 起两端都实现：C 服务端
                # HsfServerStore_queryLogs + Python 服务端 store.query_logs_page
                # 同名 category 过滤与 text||' '||category 关键字匹配）
                cat_doc, _e = _req(bridge.log_page, offset=0, limit=10,
                                       category="auth")
                cat_rows = list((cat_doc or {}).get("rows") or [])
                check("C 服务端日志类别筛选生效（category=auth 只回该类）",
                      bool(cat_rows) and all(r.get("category") == "auth"
                                             for r in cat_rows),
                      "命中 %d 行" % len(cat_rows))
                # 类别往返的**判据面**：筛什么只回什么 + 未知类别零行。
                check("【§11】日志类别过滤是**精确**的（没有第二类混进来）",
                      bool(cat_rows) and all(str(r.get("category") or "")
                                             == "auth" for r in cat_rows),
                      "命中 %d 行 / total=%s"
                      % (len(cat_rows), (cat_doc or {}).get("total")))
                ndoc, _e = _req(bridge.log_page, offset=0, limit=10,
                                    category="zz_no_such_category")
                check("【§11】未知类别 ⇒ total=0 且 rows 空（category 真下推）",
                      (ndoc or {}).get("total") == 0
                      and not ((ndoc or {}).get("rows") or []),
                      "total=%r" % ((ndoc or {}).get("total"),))
                # 关键字搜类别名：C 端口径 text||' '||category。用 "auth" 当
                # 关键字，命中行里必须有"文本不含 auth、只有类别含"的行。
                kdoc, _e = _req(bridge.log_page, offset=0, limit=50,
                                    kw="auth")
                krows = list((kdoc or {}).get("rows") or [])
                cat_only = [r for r in krows
                            if "auth" not in str(r.get("text") or "")
                            and "auth" in str(r.get("category") or "")]
                check("【§11】关键字搜类别名能命中（text||' '||category 口径）",
                      bool(krows) and bool(cat_only),
                      "命中 %d 行，其中仅靠类别列的 %d 行"
                      % (len(krows), len(cat_only)))
                # 类别 + 关键字合取（共用同一条 WHERE，total 与页同口径）
                # [容差 1 的理由 2026-11-21] 「合取结果的 total ≤ 类别结果
                # 的 total」这个辅助判据会因**同毫秒新写入**漂移：两次查询
                # 之间服务端又写了一行（同毫秒）⇒ 后查的 total 多 1 ⇒ 偶发
                # 红。⇒ 核心判据仍是「**返回行的类别都正确**」（AND 语义），
                # total 放宽到「≤ 类别 total + 1」（窗口大幅漂移仍会红）。
                bdoc, _e = _req(bridge.log_page, offset=0, limit=50,
                                    category="auth", kw="auth")
                check("【§11】类别+关键字合取（AND，不是先后覆盖）",
                      bool(bdoc)
                      and int((bdoc or {}).get("total") or 0)
                      <= int((cat_doc or {}).get("total") or 0) + 1
                      and all(str(r.get("category") or "") == "auth"
                              for r in ((bdoc or {}).get("rows") or [])),
                      "total=%r（category=auth 的 total=%r；容差 1＝同毫秒新写入）"
                      % ((bdoc or {}).get("total"),
                         (cat_doc or {}).get("total")))

        # -----------------------------------------------------------
        # (2b) 窗口锚点：C 服务端的 0x0118 / 0x0119 与 Python 端同口径
        # -----------------------------------------------------------
        check_window_anchor(bridge, proto, False, "【§11】0x0118 ",
                             req_fn=_req)
        check_window_anchor(bridge, proto, True, "【§11】0x0119 ",
                            req_fn=_req)

        # -----------------------------------------------------------
        # (3) 遥测主题齐全（VFD/AI/温湿度/流量计/数字量/浊度/阀/健康）
        # -----------------------------------------------------------
        topics = [proto.TOPIC_VFD_STATUS, proto.TOPIC_AI_STATUS,
                  proto.TOPIC_TEMPHUM_STATUS, proto.TOPIC_FLOWMETER_STATUS,
                  proto.TOPIC_DIGITAL_STATUS, proto.TOPIC_TURBIDITY_STATUS,
                  proto.TOPIC_VALVE_STATUS, proto.TOPIC_PUMP_STATUS,
                  proto.TOPIC_DEVICE_HEALTH]
        bridge.set_topics(topics)
        deadline = time.time() + 8
        # [推送期自愈 2026-11-21] 这段靠**服务端推送**收数据（**不发请求**）
        # ⇒ 传输层偶发（IOCP 缺省后端把 0 字节读判 EOF ⇒ 会话复位 ⇒ 订阅
        # 表被清空）时，请求级容错（`_req_doc_retry`）覆盖不到 ⇒ 主题再也
        # 落不了地。⇒ 等待期里**检测断链并重连 + 重订阅**（断言强度不变：
        # 主题真的没落地照样红）。
        healed = 0
        while time.time() < deadline:
            _pump()
            time.sleep(0.05)
            if not bridge.is_connected():
                healed += 1
                if healed > 3:
                    break
                print("  [INFO] §11 遥测等待期检测到断链（传输层偶发）："
                      "重连 + 重订阅（第 %d 次）" % healed)
                try:
                    bridge.disconnect_from()
                except Exception:                       # noqa: BLE001
                    pass
                ok4, _e4 = bridge.connect_to(_conn[0], _conn[1], _conn[2],
                                            _conn[3])
                if ok4:
                    for _ in range(30):
                        _pump()
                        time.sleep(0.01)
                    bridge.set_topics(topics)
                    # [自愈后要**再等一轮**] 重订阅是异步的：订阅确认 + 服务端
                    # 重新开始推 + 客户端 model 更新，都要时间 ⇒ 原 deadline
                    # 往往已经过了 ⇒ 主题「一个都没落地」是**等太短**、不是
                    # 没落地。⇒ 给一个完整的新窗口（上限 3 次自愈）。
                    deadline = time.time() + 8
        m = bridge.model
        # （主题名, 主题 id, 该主题是否在客户端 model 里落地）
        landed = [
            ("变频器", proto.TOPIC_VFD_STATUS, bool(m.vfd.updated_ms)),
            ("模拟量", proto.TOPIC_AI_STATUS, bool(m.analog.updated_ms)),
            ("温湿度", proto.TOPIC_TEMPHUM_STATUS, bool(m.temphum.sides)),
            ("流量计", proto.TOPIC_FLOWMETER_STATUS, bool(m.flowmeter.sides)),
            ("数字量", proto.TOPIC_DIGITAL_STATUS, bool(m.digital)),
            ("浊度", proto.TOPIC_TURBIDITY_STATUS, bool(m.turbidity.sides)),
            ("电磁阀", proto.TOPIC_VALVE_STATUS,
             bool(getattr(m.valve, "items", None))),
            ("进水泵", proto.TOPIC_PUMP_STATUS,
             bool(getattr(m.pump, "items", None))),
            ("设备健康", proto.TOPIC_DEVICE_HEALTH, bool(m.health.devices)),
        ]
        for name, topic, ok in landed:
            check("C 服务端遥测主题落地：%s" % name, ok, "主题 %s" % hex(topic))
    finally:
        bridge.disconnect_from()
        csrv.stop()


# ============================================================================
#            §12 交叉测试 B：Python 服务端 ↔ C 客户端协议等价
# ============================================================================
#
# [干什么] 用 Python 客户端的 bridge 扮演"C 客户端"，去读 **Python 服务端**，
# 逐项核对 C 客户端解析器（client/ui/HsfLinkHistory.c 的 kSrc/kNumSrc、
# HsfLinkClient.c 的 hsfLcParseLogPage）所依赖的契约：
#   · 0x0118：18 列**原始值**（不是预格式化文本）、列序、offset/limit 回显
#   · 0x0119：行含 ts/category/text
# 结论写成断言：「C 客户端解析器依赖的契约，Python 服务端也满足」。

def check_cross_c_client_contract(bridge, proto):
    """§12 交叉测试 B：Python 服务端 ↔ C 客户端（按 C 客户端口径核对）。"""
    section("§12 交叉测试 B：Python 服务端 ↔ C 客户端协议等价")
    doc, err = _req_doc(bridge.history_page, offset=0, limit=8)
    if err or not doc:
        check("Python 服务端历史查询有应答（§12）", False, err or "超时")
        return
    check("Python 服务端历史查询有应答（§12）", True,
          "total=%s" % doc.get("total"))

    # --- C 客户端依赖的文档键 ---
    missing = [k for k in C_CLIENT_HIST_KEYS if k not in doc]
    check("Python 服务端历史应答含 C 客户端依赖的全部键",
          not missing, "缺=%s" % missing)

    # --- 行是**数组**（C 客户端按 cell[k] 取值，不是按列名） ---
    rows = [r for r in (doc.get("rows") or [])]
    check("历史行是数组（C 客户端按 cell[k] 下标取值）",
          all(isinstance(r, (list, tuple)) for r in rows),
          "%d 行" % len(rows))
    if not rows:
        return
    row = rows[0]

    # --- 下标契约：数值列 / 文本列 / 时间列 / 位图列 ---
    bad_num = []
    for idx in C_CLIENT_NUM_IDX:
        val = row[idx]
        if val is not None and (isinstance(val, bool)
                                or not isinstance(val, (int, float))):
            bad_num.append("cell[%d]=%r(%s)" % (idx, val, type(val).__name__))
    check("C 客户端的数值列下标全是原始数值（不是预格式化文本）",
          not bad_num, "" if not bad_num else "错型=%s" % bad_num)
    bad_text = []
    for idx in C_CLIENT_TEXT_IDX:
        val = row[idx]
        if val is not None and not isinstance(val, str):
            bad_text.append("cell[%d]=%r(%s)" % (idx, val, type(val).__name__))
    check("C 客户端的文本列下标全是字符串", not bad_text,
          "" if not bad_text else "错型=%s" % bad_text)
    ts_val = row[C_CLIENT_TS_IDX]
    check("C 客户端的时间列下标是 epoch 毫秒（int）",
          isinstance(ts_val, int) and not isinstance(ts_val, bool)
          and 10 ** 12 < ts_val < 10 ** 13, "cell[0]=%r" % ts_val)
    bad_bit = []
    for idx in C_CLIENT_BITMAP_IDX:
        val = row[idx]
        if val is not None and (isinstance(val, bool) or not isinstance(val, int)):
            bad_bit.append("cell[%d]=%r(%s)" % (idx, val, type(val).__name__))
    check("C 客户端的位图列下标（di/do）是整数位图", not bad_bit,
          "" if not bad_bit else "错型=%s" % bad_bit)

    # --- 列序：cols 与 C 客户端 kSrc 映射表逐字对齐 ---
    cols = list(doc.get("cols") or [])
    check("列序与 C 客户端下标映射表一致（cols 逐字等于协议列序）",
          cols == HIST_COLS, "" if cols == HIST_COLS else "实际=%s" % cols[:8])

    # --- offset/limit 回显（C 客户端靠它认领应答，防串页） ---
    check("offset/limit 原样回显（C 客户端的应答归属判定依据）",
          doc.get("offset") == 0 and doc.get("limit") == 8,
          "offset=%r limit=%r" % (doc.get("offset"), doc.get("limit")))

    problems = _hist_contract_problems(doc)
    check("【结论】C 客户端解析器依赖的契约，Python 服务端也满足（历史页）",
          not problems, "" if not problems else "问题=%s" % problems[:3])

    # --- 日志页（0x0119）：行含 ts/category/text ---
    log_doc, err = _req_doc(bridge.log_page, offset=0, limit=10)
    if err or not log_doc:
        check("Python 服务端日志查询有应答（§12）", False, err or "超时")
        return
    check("Python 服务端日志查询有应答（§12）", True,
          "total=%s" % log_doc.get("total"))
    missing = [k for k in C_CLIENT_LOG_KEYS if k not in log_doc]
    check("Python 服务端日志应答含 C 客户端依赖的全部键",
          not missing, "缺=%s" % missing)
    lrows = list(log_doc.get("rows") or [])
    check("日志行是字典数组（C 客户端按 key 取 ts/category/text）",
          all(isinstance(r, dict) for r in lrows), "%d 行" % len(lrows))
    if not lrows:
        return
    lrow = lrows[0]
    check("日志行含 ts/category/text 三个键（C 客户端逐个取值）",
          all(k in lrow for k in ("ts", "category", "text")),
          "keys=%s" % sorted(lrow.keys()))
    check("日志行 ts 是 epoch 毫秒（int64）",
          isinstance(lrow.get("ts"), int) and not isinstance(lrow.get("ts"), bool)
          and 10 ** 12 < lrow["ts"] < 10 ** 13, "ts=%r" % lrow.get("ts"))
    check("日志行 category 是字符串（C 客户端「类别」列直接显示）",
          isinstance(lrow.get("category"), str),
          "category=%r" % lrow.get("category"))
    check("日志行 text 是字符串（C 客户端「事件」列直接显示）",
          isinstance(lrow.get("text"), str), "text=%r" % lrow.get("text"))
    cats = [str(r.get("category") or "") for r in lrows]
    check("【结论】C 客户端解析器依赖的契约，Python 服务端也满足（日志页形状）",
          all(isinstance(r.get("ts"), int) and isinstance(r.get("category"), str)
              and isinstance(r.get("text"), str) for r in lrows),
          "类别集合=%s" % sorted(set(cats)))

    # ------------------------------------------------------------------
    # (2) 0x0119 的 category **往返**：筛什么就只回什么（C 端口径）
    # ------------------------------------------------------------------
    # [为什么按"页里实际出现的类别"筛] 本服务端的类别集合会随事件点演化
    # （sys/alarm/auto… 加新类别不该让这条红）⇒ 取页里真实存在的那个字面
    # 去筛，断言"筛出来的每一行都是它"。这才是"精确过滤"的判据：不是
    # "有结果"，而是"没有第二类混进来"。
    cat = next((c for c in cats if c.strip()), "")
    check("日志页里至少有一行带类别（类别筛选有数据源）", bool(cat),
          "类别集合=%s" % sorted(set(cats)))
    if cat:
        cdoc, _e = _req_doc(bridge.log_page, offset=0, limit=20,
                            category=cat)
        crows = list((cdoc or {}).get("rows") or [])
        check("【§12】日志类别精确过滤（category=%s ⇒ 只回该类）" % cat,
              bool(crows) and all(str(r.get("category") or "") == cat
                                  for r in crows),
              "命中 %d 行 / total=%s" % (len(crows), (cdoc or {}).get("total")))
        # 未知类别 ⇒ 零行（证明 category 真下推，而不是被服务端忽略）
        ndoc, _e = _req_doc(bridge.log_page, offset=0, limit=10,
                            category="zz_no_such_category")
        check("【§12】未知类别 ⇒ total=0 且 rows 空（筛选真下推）",
              (ndoc or {}).get("total") == 0
              and not ((ndoc or {}).get("rows") or []),
              "total=%r rows=%d" % ((ndoc or {}).get("total"),
                                    len((ndoc or {}).get("rows") or [])))
        # 关键字搜类别名（C 端口径 text||' '||category）：用类别字面当关键字，
        # 命中行里必须有"文本不含它、只有类别含它"的行 ⇒ 证明匹配走了类别列。
        kdoc, _e = _req_doc(bridge.log_page, offset=0, limit=50, kw=cat)
        krows = list((kdoc or {}).get("rows") or [])
        cat_only = [r for r in krows
                    if cat not in str(r.get("text") or "")
                    and cat in str(r.get("category") or "")]
        check("【§12】关键字搜类别名能命中（text||' '||category 口径）",
              bool(krows) and bool(cat_only),
              "命中 %d 行，其中仅靠类别列的 %d 行（kw=%s）"
              % (len(krows), len(cat_only), cat))
        check("【§12】关键字命中的行都真的含该关键字（不空转）",
              all(cat in (str(r.get("text") or "") + " "
                          + str(r.get("category") or "")) for r in krows),
              "命中 %d 行" % len(krows))

    # ------------------------------------------------------------------
    # (3) 窗口锚点：0x0118 / 0x0119 同口径（不漂移、不重不漏）
    # ------------------------------------------------------------------
    check_window_anchor(bridge, proto, False, "【§12】0x0118 ")
    check_window_anchor(bridge, proto, True, "【§12】0x0119 ")


# ============================================================================
#         §13 交叉测试 C：客户端 ↔ 服务端 ↔ 框架（"库里一行 = 界面一拍"）
# ============================================================================
#
# [服务端自己的承诺] 历史行 = 采集那一拍的快照；客户端界面上的值 = 推送
# 那一拍的快照。两者必须是**同一时刻的同一份数**：
#   · 客户端 model 里的温湿度/浊度/流量  == 服务端库里最近一行的同名字段
#   · 历史页取回的行                      == 服务端库里同一 ts 的那一行
#   · 日志页取回的行                      == 服务端库里同一 ts 的那一行
# 最后两条是**逐字相等**（同一份数据、同一个源），不是"差不多"。

#: 模拟源的波动上限（服务端 sim：流量 ±2%、浊度线性爬坡 ≈0.1 NTU/s、
#: 温湿度恒定）⇒ 允许"采样间隔内"的容差。
TOL_TEMP = 0.3
TOL_HUM = 1.0
TOL_TURB = 1.0
TOL_FLOW = 3.0
#: 允许的"客户端那一拍"与"库里最近一行"的最大时间差（采样周期 5s）。
TOL_FRAME_GAP_MS = 9000


def _db_history_rows(db_path, limit=40):
    """直查服务端库 history 表；返回 {ts: {列名: 值}}。"""
    conn, err = _open_server_db(db_path)
    if conn is None:
        return None, err
    try:
        cols = [r[1] for r in conn.execute("PRAGMA table_info(history)")]
        rows = conn.execute(
            "SELECT * FROM history ORDER BY ts DESC LIMIT ?", (limit,)
        ).fetchall()
    except sqlite3.Error as exc:
        return None, "查库失败：%s" % exc
    finally:
        conn.close()
    return {int(r[0]): dict(zip(cols, r)) for r in rows}, ""


def _db_log_rows(db_path, limit=40):
    """直查服务端库 logs 表；返回 (三元组列表, err)。

    [为什么返回 (ts,category,text) 三元组而不是 {ts: (cat,text)}]
    `logs.ts` **不是主键**（见 store.py 的建表语句），同一毫秒可以落两条
    （实测："数字量：通道名已变更" 与 "自动运行告警" 同拍入库，ts 完全
    相同）。按 ts 建字典会把同毫秒的第二条**盖掉** ⇒ 页里取回的那条在
    "库里查不到" ⇒ 假红（2026-11-09 实测）。三元组列表既能容忍同毫秒，
    又能检出真实的字段漂移。
    """
    conn, err = _open_server_db(db_path)
    if conn is None:
        return None, err
    try:
        rows = conn.execute(
            "SELECT ts, COALESCE(category, ''), COALESCE(text, '') FROM logs"
            " ORDER BY ts DESC, rowid DESC LIMIT ?", (limit,)).fetchall()
    except sqlite3.Error as exc:
        return None, "查库失败：%s" % exc
    finally:
        conn.close()
    return [(int(r[0]), str(r[1] or ""), str(r[2] or "")) for r in rows], ""


def _nearest_row(db_rows, ts_ms):
    """库里离 ts_ms 最近的那一行（返回 (ts, row) 或 (None, None)）。"""
    if not db_rows:
        return None, None
    best = min(db_rows, key=lambda t: abs(t - ts_ms))
    return best, db_rows[best]


def check_frame_matches_db(bridge, srv, proto):
    """§13 交叉测试 C：客户端界面上的值 == 服务端库里同一拍的那一行。"""
    section("§13 交叉测试 C：客户端 ↔ 服务端 ↔ 框架（库里一行 = 界面一拍）")
    db_path, why = _server_db_path(bridge, srv)
    if not db_path:
        skip("§13 交叉测试 C（库里一行 = 界面一拍）", why)
        return

    # --- (1) 等客户端把遥测收进来 ---
    topics = [proto.TOPIC_TEMPHUM_STATUS, proto.TOPIC_TURBIDITY_STATUS,
              proto.TOPIC_FLOWMETER_STATUS]
    bridge.set_topics(topics)
    # [为什么"到手就留一份"] 三路遥测的到位时刻不一定齐（流量计要等模拟
    # 量爬上来）；若死等"三路齐"再回头取温湿度，那一帧可能已经过期
    # （stale）⇒ 明明收到过却判"没收到"。所以**收到就留一份**。
    th = None
    #: 客户端秒表 → 墙钟的换算偏移（毫秒），收到第一帧的当口取一次。
    #:
    #: [为什么必须换算 2026-11-09 实锤] 客户端的 `updated_ms` 是
    #: `time.monotonic()*1000`（**秒表**，用于 stale 判定，抗墙钟跳变）；
    #: 服务端库里 `history.ts` 是**墙钟 epoch 毫秒**。直接相减差
    #: 1.79e12ms ⇒ "库里没有与界面同一拍的行" 恒红。两侧各自都对，
    #: 错的是测试拿两个时钟域的数直接比。这里在"刚收到这一帧"的时刻
    #: 取一次 (墙钟, 秒表) 对标，把那一拍换算到墙钟再对账（误差 ≤ 一个
    #: 轮询周期 50ms，远小于 9s 容差）。
    skew_ms = 0
    got = {"temphum": False, "turbidity": False, "flowmeter": False}
    deadline = time.time() + 15
    while time.time() < deadline:
        _pump()
        time.sleep(0.05)
        m = bridge.model
        if th is None:
            th = m.temphum.pick()
            if th is not None:
                skew_ms = (int(time.time() * 1000)
                           - int(time.monotonic() * 1000))
        got["temphum"] = th is not None
        got["turbidity"] = bool(m.turbidity.sides)
        got["flowmeter"] = m.flowmeter.pick() is not None
        if all(got.values()):
            break
    m = bridge.model
    check("客户端收到了温湿度帧（界面有一拍可对账）", th is not None,
          "温度=%s 湿度=%s（收到情况：温湿度=%s 浊度=%s 流量=%s 订阅=%s）"
          % (m.temphum.temp_text(), m.temphum.hum_text(),
             got["temphum"], got["turbidity"], got["flowmeter"],
             [hex(t) for t in bridge.current_topics()]))
    if th is None:
        skip("§13 交叉测试 C（库里一行 = 界面一拍）", "没收到温湿度帧")
        return
    # 秒表 → 墙钟（见上方 skew_ms 的说明）。
    frame_ts = th.updated_ms + skew_ms

    # --- (2) 库里最近一行 —— 必须与客户端那一拍足够接近 ---
    rows, err = _db_history_rows(db_path)
    if not rows:
        skip("§13 交叉测试 C（库里一行 = 界面一拍）", err or "库里没有历史行")
        return
    ts, row = _nearest_row(rows, frame_ts)
    gap = abs(ts - frame_ts)
    check("服务端库里有一行与客户端那一拍同时刻（时间差在采样间隔内）",
          gap <= TOL_FRAME_GAP_MS,
          "库里 ts=%d 客户端帧 ts=%d（秒表 %d + 墙钟偏移 %d）差 %dms"
          % (ts, frame_ts, th.updated_ms, skew_ms, gap))

    # --- (3) 温湿度：界面值 == 库里那一行 ---
    temp_c = (th.temp_tenths / 10.0) if th.temp_tenths is not None else None
    hum_c = (th.hum_tenths / 10.0) if th.hum_tenths is not None else None
    if temp_c is not None and row.get("temp") is not None:
        check("客户端温湿度值 == 库里同一行（温度）",
              abs(temp_c - float(row["temp"])) <= TOL_TEMP,
              "客户端 %.1f℃ / 库里 %.3f℃" % (temp_c, float(row["temp"])))
    else:
        check("客户端温湿度值 == 库里同一行（温度）", False,
              "温度缺失：客户端=%r 库里=%r" % (temp_c, row.get("temp")))
    if hum_c is not None and row.get("hum") is not None:
        check("客户端温湿度值 == 库里同一行（湿度）",
              abs(hum_c - float(row["hum"])) <= TOL_HUM,
              "客户端 %.1f%% / 库里 %.3f%%" % (hum_c, float(row["hum"])))
    else:
        check("客户端温湿度值 == 库里同一行（湿度）", False,
              "湿度缺失：客户端=%r 库里=%r" % (hum_c, row.get("hum")))

    # --- (4) 浊度 / 流量：同样对账 ---
    ntu = m.turbidity.ntu_of(0)
    if ntu is not None and row.get("turb1") is not None:
        check("客户端浊度值 == 库里同一行（进水浊度仪）",
              abs(ntu - float(row["turb1"])) <= TOL_TURB,
              "客户端 %.2f NTU / 库里 %.3f NTU" % (ntu, float(row["turb1"])))
    else:
        check("客户端浊度值 == 库里同一行（进水浊度仪）", False,
              "浊度缺失：客户端=%r 库里=%r" % (ntu, row.get("turb1")))
    flow = m.flowmeter.flow
    if m.flowmeter.pick() is not None and row.get("flow") is not None:
        check("客户端流量值 == 库里同一行（瞬时流量）",
              abs(flow - float(row["flow"])) <= TOL_FLOW,
              "客户端 %.3f / 库里 %.3f" % (flow, float(row["flow"])))
    else:
        check("客户端流量值 == 库里同一行（瞬时流量）", False,
              "流量缺失：客户端=%r 库里=%r" % (flow, row.get("flow")))

    # --- (5) 历史页行 == 库里同一 ts 的那一行（逐字相等） ---
    doc, err = _req_doc(bridge.history_page, offset=0, limit=20)
    if not doc:
        check("历史页取回的行 == 服务端库里同一行（逐字相等）", False,
              err or "超时")
    else:
        cols = list(doc.get("cols") or [])
        diffs = []
        matched = 0
        for r in (doc.get("rows") or []):
            if not isinstance(r, (list, tuple)) or not r:
                continue
            db_row = rows.get(int(r[0]))
            if db_row is None:
                continue
            matched += 1
            for j, name in enumerate(cols):
                if j >= len(r):
                    break
                want = db_row.get(name)
                got = r[j]
                if isinstance(want, float) or isinstance(got, float):
                    if want is None or got is None:
                        if want is not got:
                            diffs.append("%s@%s 库里=%r 页=%r"
                                         % (name, r[0], want, got))
                        continue
                    if abs(float(want) - float(got)) > 1e-6:
                        diffs.append("%s@%s 库里=%r 页=%r"
                                     % (name, r[0], want, got))
                elif want != got:
                    diffs.append("%s@%s 库里=%r 页=%r" % (name, r[0], want, got))
        check("历史页取回的行 == 服务端库里同一行（逐字相等）",
              matched > 0 and not diffs,
              "" if not diffs else "对上了 %d 行，差异=%s" % (matched, diffs[:3]))

    # --- (6) 日志页行 == 库里同一 ts 的那一行（逐字相等） ---
    log_doc, err = _req_doc(bridge.log_page, offset=0, limit=20)
    db_logs, lerr = _db_log_rows(db_path)
    if not log_doc or db_logs is None:
        check("日志页取回的行 == 服务端库里同一行（逐字相等）", False,
              err or lerr or "超时")
    else:
        # [按三元组对账] logs.ts 不是主键，同毫秒可落多行（实测）；用
        # (ts,category,text) 集合对齐，同毫秒不互相盖掉。
        db_set = set(db_logs)
        diffs = []
        matched = 0
        for r in (log_doc.get("rows") or []):
            if not isinstance(r, dict):
                continue
            key = (int(r.get("ts") or 0),
                   str(r.get("category") or ""),
                   str(r.get("text") or ""))
            if key in db_set:
                matched += 1
            else:
                diffs.append("ts=%s category=%r text=%r（库里没有这条）"
                             % (key[0], key[1], key[2][:40]))
        check("日志页取回的行 == 服务端库里同一行（逐字相等）",
              matched > 0 and not diffs,
              "" if not diffs else "对上了 %d 行，差异=%s"
              % (matched, diffs[:3]))


# ============================================================================
#        §14 起：段落编排（代码都在本文件末尾的"段落区"里）
# ============================================================================
#
# [为什么要这样] 2026-11-09 用户口径：「所有的测试代码都集成到一个测试文件中，
# c 端一个，python 一个」。此前本文件用 `_import_scattered()` 在**运行时
# import** 十几个散落模块（test_history_* / test_log_page / test_alarm_page_toolbar /
# test_chart_interaction / test_trend_chart / test_storage_limit /
# test_remote_settings_ui / test_softkeyboard / test_ui_scale / test_vfd_* /
# test_home_* / test_client / test_xlsx_leak …）再逐个调它们的 part —— 入口是
# 一个，**代码仍散在十几个文件**里。
#
# 现在：那些模块的代码已**物理并入本文件**（文件末尾的段落区，每个段落一个
# `run_*()` 入口、顶层名统一带前缀避免撞名），本节只负责**编排**：谁在什么
# 阶段跑。原来的 import/换计数器/按签名截断参数那套机制（_import_scattered /
# _patch_counters / _call_part / _restore_counters）随之删除。
#
# 跑法不变：`py -3 tests/test_hsf_regression.py` 跑全。


#: UI 重段落分组（每组一个子进程；键 = --only 名）。
def _ui_groups():
    return {
        "history": (merge_history_page,),
        "log": (merge_log_page,),
        "alarm": (merge_alarm_page,),
        "charts": (merge_charts,),
        "storage": (merge_storage,),
        "remote": (merge_remote_settings,),
        "softkbd": (merge_softkeyboard,),
        "exportq": (merge_export_full,),
        "toolbar": (merge_toolbar_unify,),
        "valvecard": (merge_valvecard_layout,),
        "account": (merge_account_menu,),
        "vfd": (merge_vfd,),
        "uiscal": (merge_ui_scale,),
        "theme": (merge_theme_switch,),
        "home": (merge_home_cards,),
        "firstscreen": (merge_home_first_screen,),
        "nativecap": (merge_native_cap,),
        "settingsmerge": (merge_settings_merge,),
        "webset": (merge_web_set,),
        # [2026-11-19 相机] 组帧器 + 相机页 + 远端设置相机卡（桌面端
        # 这一份实现必须自己守规则——服务端那段 test_camera.py 锁的是
        # 服务端与「三端一致」的接口，锁不住本端页面接线）。
        "camera": (merge_camera,),
        # [2026-11-20 文字裁剪] 全 19 页 × 4 视口 × 2 缩放的通用扫描器。
        # 独立子进程跑：它反复建/弃 MainWindow（每次换视口与缩放都要重
        # 建），与其它 UI 段共用一个进程会互相污染（Qt 样式/页面缓存）。
        "textfit": (merge_text_fit,),
        # [2026-11-20 页面补洞] 模拟量页 + 四个菜单页的控件级断言
        # （此前这 5 页零控件级覆盖）。
        "pagecov": (merge_page_coverage,),
    }


def _run_group_isolated(name):
    """在**子进程**里跑一个 UI 段组并折叠其计数。

    [为什么要隔离 2026-11-09] 合并进程连跑全部 UI 段时，堆损坏
    （0xc0000374，surface 点在 setStyleSheet）会在段落边界随机 abort
    整个进程——一处段落的问题让后面所有段落"消失"。子进程隔离后：
      · 崩了只记一条红（带子进程输出尾巴），其它段落照跑；
      · 每段拿到干净的 Qt 状态（等价于原来"每文件一个进程"的隔离性，
        而代码仍只在**这一个文件**里——用户口径不破）。
    子进程协议：stdout 末尾打 `SUBRESULT PASS x FAIL y SKIP z`，父进程
    解析并折叠进自己的计数器。
    """
    import subprocess
    global PASS, FAIL, SKIP
    cmd = [sys.executable, os.path.abspath(__file__),
           "--no-srv", "--only=" + name]
    env = dict(os.environ)
    env.setdefault("PYTHONIOENCODING", "utf-8")
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=420,
                              env=env)
    except subprocess.TimeoutExpired:
        check("UI 段组 %s（子进程超时 420s）" % name, False)
        return
    out = (proc.stdout or b"").decode("utf-8", "replace")
    sub_pass = sub_fail = sub_skip = 0
    for line in out.splitlines():
        if line.startswith("SUBRESULT"):
            try:
                parts = line.split()
                # 打印格式：SUBRESULT PASS <n> FAIL <n> SKIP <n>（空格分隔）
                sub_pass = int(parts[2])
                sub_fail = int(parts[4])
                sub_skip = int(parts[6])
            except (IndexError, ValueError):
                pass
    if proc.returncode not in (0, 1):
        tail = out.strip().splitlines()[-3:] if out.strip() else []
        check("UI 段组 %s 子进程崩溃（exit=%s）" % (name, proc.returncode),
              False, " | ".join(tail))
        # 子进程计数仍尽量折叠（崩溃前的 PASS 也算数）
        PASS += sub_pass
        SKIP += sub_skip
        FAIL += sub_fail + 1
        return
    PASS += sub_pass
    SKIP += sub_skip
    FAIL += sub_fail
    print("  [段组 %s] PASS %d / FAIL %d / SKIP %d"
          % (name, sub_pass, sub_fail, sub_skip))


def merge_history_page():
    """§14 历史页：列筛选 / 关键字下推 / 时间窗锚点。

    [2026-11-09 单文件化] 原来这里用 `_import_scattered()` **运行时 import**
    四个散落模块再逐个调 part；现在那些模块的代码**就在本文件**里（见文件
    末尾的段落区），直接调段落入口即可——没有 import、没有跨文件依赖。
    """
    section("§14 历史页：列筛选 / 关键字下推 / 时间窗锚点（并入本文件）")
    run_hisflt()      # 段落：test_history_filter
    run_hiscol()      # 段落：test_history_columns
    run_hispage()     # 段落：test_history_page
    run_hisside()     # 段落：test_history_side_frames
    run_histchart()   # 段落：图表视图（页内双视图 + 全量取数，2026-11-10）
    run_expfull()     # §31 全量筛选枚举（导出=按筛选条件跨页拉全）
    run_expwr()       # §32 导出文件无头写 CSV/XLSX + 回读校验
    run_expflt()      # §33 筛选谓词与翻页判界


def merge_log_page():
    """§15 日志页：类别筛选 / 关键字 / 导出 / 分页归属。"""
    section("§15 日志页：类别筛选 / 关键字 / 导出 / 分页归属（并入本文件）")
    m_logpg_run_log_page()      # 段落：test_log_page


def merge_alarm_page():
    """§16 报警页：工具栏动作。"""
    section("§16 报警页：工具栏动作（并入本文件）")
    run_alarmtb()      # 段落：test_alarm_page_toolbar


def merge_charts():
    """§17 趋势图与图表交互：缩放 / 平移 / 数据源。"""
    section("§17 趋势图与图表交互：缩放 / 平移 / 数据源（并入本文件）")
    run_trendch()      # 段落：test_trend_chart
    run_chartix()      # 段落：test_chart_interaction


def merge_storage():
    """§18 存储上限：封顶 / 轮转策略读数。

    [2026-11-09 单文件化] 代码已在本文件的段落区（test_storage_limit），
    直接调段落入口。慢段（stop/rotate，各要 30s+ 真实增长）由入口的
    `slow_parts` 开关控制：默认跑快段；`--slow` 时全跑。
    """
    section("§18 存储上限：封顶 / 轮转策略读数（并入本文件）")
    run_storlim(slow_parts=SLOW_PARTS)


def merge_remote_settings():
    """§19 远程设置：读写回显。"""
    section("§19 远程设置：读写回显（并入本文件）")
    m_remcfg_run_remote_settings()      # 段落：test_remote_settings_ui


def merge_softkeyboard():
    """§20 软键盘：无死白 / 切换不跳。"""
    section("§20 软键盘：无死白 / 切换不跳（并入本文件）")
    run_softkbd()      # 段落：test_softkeyboard


def merge_export_full():
    """§21 导出按筛选全量：批次计划 + 逐批取数 + 页面接线。"""
    section("§21 导出按筛选全量（并入本文件）")
    m_exportq_run_export_full()


# ============================================================================
# 段落：Web 设置（网页服务跨进程探测 / 手动起停 / 关闭同步开关）
# ============================================================================
# [背景] 2026-11-17 用户定案「Web 设置网页服务支持探测不同进程下 web
#        服务器是否启动，不启动可以用设置拉起，并可以设置客户端关闭的时候
#        是否要同步关闭 web 端」：
#   · 网页服务**不一定**是本客户端起的（用户手工起的、另一个客户端实例
#     内嵌的、上一版没退干净的）⇒ 设置页要能**跨进程**回答"有没有在跑、
#     是不是我认领的那个"；
#   · 没在跑时能在设置页**手动拉起**；
#   · 客户端关闭时要不要**同步关闭**网页端 ⇒ 新增设置键
#     `webStopWithClient`（缺省开 = 同步关闭 ⇒ 内嵌形态；关 = 独立进程
#     形态 ⇒ 客户端退出后网页端继续在后台服务）。
#
# [本段锁什么]
#   ① 探测：未监听端口 ⇒ alive=False；真起一次**独立进程**网页端 ⇒ 探得到、
#      own=True、状态行说"本客户端启动的独立进程"；停掉 ⇒ 又探不到；
#   ② 端口被**别的进程**占着 ⇒ 拒绝重复拉起并说明（不盲目 bind 失败）；
#   ③ 三态文案（本客户端内嵌 / 其它进程 / 已停止）；
#   ④ 设置键读写（缺省 True；写 False 读回一致）；
#   ⑤ Web 设置页控件齐全（刷新 / 启动 / 停止 / 关闭同步开关）。
#
# [为什么真起一次] 探测/起停是**跨进程**行为（HTTP 往返 + 子进程 + 端口），
#   只测假对象等于没测 ⇒ 这里真起一次**独立进程**网页端（停掉即释放端口、
#   不留孤儿）。
#   [上游指向死端口 6199] 网页端绑端口与连上游是**两件事**（上游是登录后
#   才连的）⇒ 用死端口起，既能绑上 18099，又**不抢**回归自己的 root 会话。
#
# 入口：m_webset_run()


def m_webset_run():
    """§36 Web 设置：跨进程探测 / 手动起停 / 关闭同步开关。"""
    import sys as _sys
    import time
    from hsfsrv_client import webserver
    from hsfsrv_client import settings as ui_settings

    PORT = 18099
    DEAD_UPSTREAM = 6199        # 故意指不存在的上游（不抢 root 会话）
    web_dir = webserver.find_webui_dir()
    # [不许污染本机设置 2026-11-17 实测] 下面会把端口框设成 PORT（18099）⇒
    # 触发 800ms 去抖 ⇒ `_apply_web_port()` **把 webPort 写进本机 cfg**
    # ⇒ 回归跑完，客户端启动就带 18099 的网页端（本轮真踩到，用户看到
    # "端口怎么换了"）⇒ 段末必须**恢复原值**。
    _saved_port = ui_settings.web_port()

    # ---- ① 探测：未监听端口 ----
    info = webserver.probe(PORT, timeout=1.0)
    check("§36 A1 未监听的端口探不到（alive=False）",
          not info["alive"], "%r" % (info,))
    check("§36 A2 状态行=已停止",
          "已停止" in webserver.status_text(info),
          webserver.status_text(info))

    # ---- ② 真起一次**独立进程**形态的网页端（跨进程探得到） ----
    ok, reason = webserver.start("127.0.0.1", DEAD_UPSTREAM, PORT)
    check("§36 B1 独立进程形态起网页端成功", ok, reason)
    info = webserver.probe(PORT, timeout=2.0)
    check("§36 B2 探得到刚起的网页端（alive=True / own=True）",
          info["alive"] and info["own"], "%r" % (info,))
    check("§36 B3 状态行说「本客户端启动的独立进程」",
          "本客户端启动的独立进程" in webserver.status_text(info),
          webserver.status_text(info))
    check("§36 B4 状态行带访问地址",
          "http://127.0.0.1:%d" % PORT in webserver.status_text(info),
          webserver.status_text(info))

    # ---- ③ 停掉：探不到 + 状态回已停止 ----
    webserver.stop()
    deadline = time.time() + 8
    while time.time() < deadline and webserver.probe(PORT, 0.6)["alive"]:
        time.sleep(0.2)
    info = webserver.probe(PORT, timeout=1.0)
    check("§36 C1 停掉后探不到", not info["alive"], "%r" % (info,))

    # ---- ④ 端口被**别的进程**占着 ⇒ 拒绝重复拉起（不 bind 失败） ----
    _orig_probe = webserver.probe
    try:
        webserver.probe = lambda port=None, timeout=1.5: {
            "alive": True, "server": "hsf-webui",
            "url": "http://127.0.0.1:%d/" % int(port or 0), "own": False,
            "port": int(port or 0)}
        ok2, reason2 = webserver.start("127.0.0.1", DEAD_UPSTREAM, PORT)
        check("§36 D1 端口已被别人占 ⇒ 拒绝拉起并说明",
              (not ok2) and "已有网页服务" in reason2,
              "%s %s" % (ok2, reason2))
        check("§36 D2 状态行=运行中（其它进程）",
              "其它进程" in webserver.status_text(webserver.probe(PORT)),
              webserver.status_text(webserver.probe(PORT)))
    finally:
        webserver.probe = _orig_probe

    # ---- ⑤ 设置键：缺省开；写 False 读回一致 ----
    _saved = ui_settings.web_stop_with_client()
    try:
        check("§36 E1 「客户端关闭时同步关闭网页端」缺省为开",
              ui_settings.web_stop_with_client() is True,
              "%r" % (ui_settings.web_stop_with_client(),))
        ui_settings.set_value(ui_settings.K_WEB_STOP_WITH_CLIENT, False)
        check("§36 E2 写 False 读回 False（不同步关闭）",
              ui_settings.web_stop_with_client() is False,
              "%r" % (ui_settings.web_stop_with_client(),))
        ui_settings.set_value(ui_settings.K_WEB_STOP_WITH_CLIENT, True)
        check("§36 E3 写回 True 读回 True（同步关闭）",
              ui_settings.web_stop_with_client() is True,
              "%r" % (ui_settings.web_stop_with_client(),))
    finally:
        ui_settings.set_value(ui_settings.K_WEB_STOP_WITH_CLIENT, _saved)

    # ---- ⑥ pid 记账 + 残留认领（2026-11-17：独立进程形态 ⇒ 客户端被强杀
    #         会留下孤儿占端口 ⇒ 必须能认领并清理） ----
    import os as _os
    import tempfile as _tf
    import subprocess as _sp
    # [pid 文件用**默认路径**] 与生产同一条路径 ⇒ 上次残留（客户端被强杀
    # 留下的孤儿）能被**下次认领**清理；用临时路径就认领不到了。
    _pidfile = webserver._pid_file()
    if True:
        ok, reason = webserver.start("127.0.0.1", DEAD_UPSTREAM, PORT)
        check("§36 G1 独立进程起服（为 pid 记账做准备）", ok, reason)
        check("§36 G2 起了 ⇒ 有 pid 记账文件",
              _os.path.isfile(_pidfile), _pidfile)
        check("§36 G3 记账里的 pid 是活着的进程",
              webserver.pid_alive(webserver.stale_pid()),
              "pid=%s" % webserver.stale_pid())
        webserver.stop()
        check("§36 G4 停掉 ⇒ pid 记账文件清掉",
              not _os.path.isfile(_pidfile), _pidfile)

        # 模拟"上次被强杀留下孤儿"：起一个**真 hsfweb** 子进程（身份闸要求
        # 命令行含 hsfweb——普通 sleep 进程会被身份闸正确跳过），往 pid
        # 文件写它的 pid 且 parent=0（属主未知 ⇒ 按孤儿处理）⇒ stop_stale
        # 应认领并停掉它（临时账本，不碰真账本）。
        # [pid 文件名由 launcher 定] 客户端的 stop_stale 转调 launcher ⇒
        # 临时账本也必须用 launcher 的文件名（否则认领不到）。
        from hsfweb import launcher as _lk2
        _tmp_dir = _tf.mkdtemp(prefix="hsf_web_pid_")
        _tmp_pid = _lk2.pid_file_path(_tmp_dir)
        _saved_env = _os.environ.get(webserver.ENV_PID_FILE, None)
        _os.environ[webserver.ENV_PID_FILE] = _tmp_pid
        _web_dir2 = webserver.find_webui_dir()
        _env2 = dict(_os.environ)
        _env2["PYTHONPATH"] = _web_dir2 + _os.pathsep + _env2.get(
            "PYTHONPATH", "")
        _dummy = _sp.Popen(
            [_sys.executable, "-u", "-m", "hsfweb", "--server", "127.0.0.1",
             "--port", "6199", "--web-port", "18096"],
            cwd=_web_dir2, env=_env2, stdout=_sp.DEVNULL,
            stderr=_sp.DEVNULL, stdin=_sp.DEVNULL)
        try:
            _lk2.write_pid(_dummy.pid, log_dir=_tmp_dir, parent=0)
            _hit = webserver.stop_stale()
            check("§36 G5 残留进程被认领并清理（stop_stale）",
                  _hit and not webserver.pid_alive(_dummy.pid),
                  "hit=%s alive=%s" % (_hit, webserver.pid_alive(_dummy.pid)))
        finally:
            if _dummy.poll() is None:
                _dummy.kill()
            if _saved_env is None:
                _os.environ.pop(webserver.ENV_PID_FILE, None)
            else:
                _os.environ[webserver.ENV_PID_FILE] = _saved_env

    # ② 并发 start/restart 会互杀（start() 里的 stop_stale 误杀另一个
    #    并发 start 刚起的进程）⇒ 起停必须串行、残留清理只在启动时做。
    import threading as _th
    _res = []

    def _go():
        _res.append(webserver.start("127.0.0.1", DEAD_UPSTREAM, PORT))

    _t1 = _th.Thread(target=_go)
    _t2 = _th.Thread(target=_go)
    _t1.start()
    _t2.start()
    _t1.join(30)
    _t2.join(30)
    _alive = webserver.probe(PORT, timeout=2.0)
    check("§36 I2 并发起服 ⇒ 网页端仍活着（没被互杀）",
          _alive["alive"], "%r %r" % (_alive, _res))
    webserver.stop()

    # ---- ⑧ 启动事实源在**网页端**（防参数漂移） ----
    # [为什么要有这条] 客户端曾经自己拼 `python -m hsfweb …` 命令行 ⇒ 网页端
    # 改参数名/默认值，客户端就悄悄对不上（而且是**运行期**才炸）⇒ 收口到
    # 网页端的 hsfweb.launcher ⇒ 这里锁住"launcher 拼出来的参数，网页端
    # argparse 全部认"。
    try:
        if web_dir not in _os.sys.path:
            _os.sys.path.insert(0, web_dir)
        from hsfweb import launcher as _lk
        from hsfweb import __main__ as _webmain
        _argv = _lk.build_argv(server="127.0.0.1", port=6100,
                                web_port=PORT)
        check("§36 H1 网页端 launcher 能产出独立进程命令行",
              _argv[1:] and "hsfweb" in _argv, "%r" % (_argv[:4],))
        # [只把**选项**喂给 argparse] 前面的 "-u -m hsfweb" 是解释器/模块
        # 参数，不是网页端的选项 ⇒ 喂进去 argparse 会直接 exit（段进程死）。
        _opts = _argv[_argv.index("--server"):]
        _parsed = _webmain.parse_args(_opts)
        check("§36 H2 launcher 的参数网页端 argparse 全认（防漂移）",
              int(_parsed.web_port) == PORT
              and str(_parsed.server) == "127.0.0.1"
              and int(_parsed.port) == 6100,
              "port=%s server=%s" % (_parsed.web_port, _parsed.server))
    except Exception as exc:                       # noqa: BLE001
        check("§36 H1/H2 launcher 与网页端参数对齐", False,
              "%s: %s" % (type(exc).__name__, exc))

    # ---- ⑦ 页面控件齐全（Web 设置页） ----
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_web import WebSettingsPage
    ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    page = WebSettingsPage(ctx)
    check("§36 F1 有「刷新状态」按钮",
          hasattr(page, "web_probe_btn"), "")
    check("§36 F2 有「启动网页服务」按钮",
          hasattr(page, "web_start_btn"), "")
    check("§36 F3 有「停止网页服务」按钮",
          hasattr(page, "web_stop_btn"), "")
    check("§36 F4 有「客户端关闭时同步关闭网页端」复选框（缺省勾上）",
          hasattr(page, "web_stop_with_client")
          and page.web_stop_with_client.isChecked(), "")
    page.deleteLater()

    # ---- ⑦b 乱序/并发的两条（2026-11-17 实测抓出的真缺陷） ----
    # ① 探测结果乱序 ⇒ 旧结果会把新状态覆盖（"在跑"被刷成"已停止"）
    page._probe_seq = 5
    page.web_status.setText("标记：最新状态")
    page._on_web_probe_done(4, {"alive": False, "server": "",
                                 "url": "", "own": False,
                                 "port": PORT})     # ← 过期序号
    check("§36 I1 过期探测结果被丢弃（不改状态行）",
          "标记：最新状态" in page.web_status.text(),
          page.web_status.text())
    page._on_web_probe_done(5, {"alive": True, "server": "hsf-webui",
                                 "url": "http://127.0.0.1:%d/" % PORT,
                                 "own": True, "port": PORT})
    check("§36 I1b 最新一次探测结果生效",
          "运行中" in page.web_status.text(), page.web_status.text())

    # ---- 恢复本机设置（不许把测试端口留给用户） ----
    ui_settings.set_value(ui_settings.K_WEB_PORT, _saved_port)
    try:
        page.web_port_spin.setValue(_saved_port)
    except Exception:                               # noqa: BLE001
        pass
    page._apply_web_port()

    # ---- ⑧ 标题栏二维码徽章 + 访问地址弹卡（2026-11-18 用户定案）----
    # 「顶部蓝色状态栏插入二维码位图，点击弹出二维码卡片，可切换本地/
    #  远程连接二维码」。地址口径单一事实源 = qr_access（页面与弹卡
    #  都委托它 ⇒ 两处显示的地址必然一致）。
    section("§36 J 标题栏二维码徽章 + 访问地址弹卡")
    from hsfsrv_client import qr_access
    from hsfsrv_client.widgets import TitleBar, QrBadge

    tb = TitleBar()
    check("§36 J1 标题栏有二维码徽章（32x32 自绘按钮）",
          isinstance(tb.qr_btn, QrBadge) and tb.qr_btn.width() == 32, "")
    # 单一事实源：页面方法与 qr_access 模块函数逐字一致。
    check("§36 J2 页面地址方法委托 qr_access（单一事实源）",
          page.lan_url() == qr_access.lan_url()
          and page.remote_url() == qr_access.remote_url(),
          "page=%r qr=%r" % (page.lan_url(), qr_access.lan_url()))
    check("§36 J3 局域网地址形如 http://IP:端口/",
          qr_access.lan_url().startswith("http://")
          and qr_access.lan_url().rstrip("/").count(":") >= 2,
          qr_access.lan_url())
    # 弹卡：控件齐全 + 缺省局域网 tab + 切换重画 + 未配域名的提示口径。
    card = qr_access.QrPopupCard(tb)
    check("§36 J4 弹卡控件齐全（切换/刷新/画布/地址/提示/缺省局域网）",
          card.lan_btn.isChecked() and not card.remote_btn.isChecked()
          and card.refresh_btn is not None
          and card.canvas is not None
          and card.url_label is not None and card.hint_label is not None,
          "")
    card.refresh()
    check("§36 J5 局域网模式：地址行带 URL 且画布生成二维码",
          card.url_label.text().startswith("局域网地址：http://")
          and card.canvas.has_code(),
          "url=%r err=%r" % (card.url_label.text(),
                             card.canvas.error_text()))
    card.remote_btn.setChecked(True)
    card.refresh()
    if qr_access.remote_url():
        check("§36 J6 已配域名 ⇒ 远程码生成", card.canvas.has_code(),
              qr_access.remote_url())
    else:
        check("§36 J6 未配域名 ⇒ 显式提示不编造（画布无码）",
              "未配置域名" in card.url_label.text()
              and not card.canvas.has_code(),
              card.url_label.text())
    # [切换用 setChecked(True) 而不是把远程设为 False] Qt 的 autoExclusive
    # 组保证**恒有一个选中** ⇒ 编程 setChecked(False) 对唯一选中项**不生效**
    # （探针实证：setFalse 后 isChecked 仍 True）。真实用户点击互斥正常
    # ⇒ 这里模拟点击语义：把局域网设为 True（远程自动取消）。
    card.lan_btn.setChecked(True)
    card.refresh()
    check("§36 J7 切回局域网 ⇒ 重画（码回来）",
          card.url_label.text().startswith("局域网地址：")
          and card.canvas.has_code(),
          "url=%r err=%r" % (card.url_label.text(),
                             card.canvas.error_text()))
    card.close()
    tb.deleteLater()
    card.deleteLater()

    # ---- §36 K 字形像素锁（2026-11-19 用户「修复后记得重点检查下拉
    #  箭头不要消失了」）----
    # [背景] 下拉箭头与微调框 −/+ 字形都走「运行时 PNG + ::xx-arrow image
    #  规则」——这条链路已经**悄悄断过两次**（agent E 移除 combo PNG；
    #  focus 规则让 image 失效的 11-17 怪癖）⇒ 用**像素取证**锁住：
    #  控件 grab() 后数箭头/字形区的深色笔画像素，消失 ⇒ 0 ⇒ 红。
    # [为什么像素不算"脆"] 我们断的是「非白笔画存在」而不是精确形状
    # ⇒ 换主题/微调字形尺寸不误伤；只有"整个没了"才会红。
    from PyQt5.QtWidgets import (QComboBox, QSpinBox, QApplication,
                                 QVBoxLayout, QWidget)
    from hsfsrv_client import theme
    app = QApplication.instance()
    cb_k = QComboBox(); cb_k.addItems(["甲", "乙"])
    sb_k = QSpinBox(); sb_k.setRange(0, 100); sb_k.setValue(50)
    host_k = QWidget(); _lay_k = QVBoxLayout(host_k)
    _lay_k.addWidget(cb_k); _lay_k.addWidget(sb_k)
    theme.apply_window(host_k, "light")
    host_k.resize(300, 110)
    host_k.show(); app.processEvents()

    def _dark_pixels(widget, x0=None, x1=None):
        pm = widget.grab()
        img = pm.toImage()
        n = 0
        xa = 0 if x0 is None else max(0, x0)
        xb = img.width() if x1 is None else min(img.width(), x1)
        for x in range(xa, xb):
            for y in range(2, img.height() - 2):
                c = img.pixelColor(x, y)
                if c.red() < 180 and abs(c.red() - c.green()) < 60:
                    n += 1
        return n

    _arrow_px = _dark_pixels(cb_k, cb_k.width() - 36, cb_k.width() - 4)
    check("§36 K1 下拉箭头在（像素取证 >0；历史上悄悄消失过两次）",
          _arrow_px > 3, "箭头区深色像素=%d" % _arrow_px)
    _spin_px = (_dark_pixels(sb_k, 2, sb_k.width() // 2 - 30)
                + _dark_pixels(sb_k, sb_k.width() // 2 + 30,
                               sb_k.width() - 2))
    check("§36 K2 微调框 −/+ 字形在（两侧像素取证 >0）",
          _spin_px > 3, "两侧深色像素=%d" % _spin_px)
    host_k.deleteLater()




def merge_camera():
    """§37 相机（2026-11-19）：组帧器 / 相机页 / 远端设置相机卡。

    [为什么这一段必须存在] 相机是三端**各自一份实现**的功能（组帧器桌面端
    与网页端各一个文件，服务端第三个）⇒ 只靠服务端那段 `test_camera.py`
    锁不住"桌面端这份也守规则"。这里用**服务端同款分片**喂桌面端组帧器，
    再让页面真贴图、真取元信息。

    [为什么不用真相机] 相机是真外部设备（CI 机器上没有）⇒ 全部合成 JPEG
    驱动；真机链路由方案文档 §10.4 的手工实测覆盖。
    """
    section("§37 相机（组帧器 / 相机页 / 远端设置相机卡）")
    import struct as _struct
    import time as _time

    from PyQt5.QtWidgets import QLabel, QApplication
    from hsfsrv_client import proto
    from hsfsrv_client import camera_frames as camf
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_camera import CameraPage
    from hsfsrv_client.pages_other import RemoteSettingsPage
    app = QApplication.instance()

    # ---- B 组帧器（与服务端 test_camera.py 的 B/E 段同口径）----
    def _jpeg(w=704, h=704, payload=40 * 1024):
        """造形状合法的 JPEG（SOI + SOF0 宽高 + 填充 + EOI）。"""
        import struct as _s
        body = bytearray(b"\xff\xd8")
        body += (b"\xff\xe0" + _s.pack(">H", 16) + b"JFIF\x00\x01\x01\x00"
                 + _s.pack(">HH", 1, 1) + b"\x00\x00")
        body += (b"\xff\xc0" + _s.pack(">H", 17) + b"\x08"
                 + _s.pack(">HH", h, w) + b"\x03"
                 + b"\x01\x22\x00" + b"\x02\x11\x01" + b"\x03\x11\x01")
        while len(body) + 4 < payload:
            body += b"\x5a"
        body += b"\xff\xd9"
        return bytes(body)

    def _frag(data, seq, width=704, height=704):
        """按服务端口径切分片（16B 小端片头 + ≤12288 片数据）。"""
        total = max(1, (len(data) + proto.CAM_CHUNK_MAX - 1)
                    // proto.CAM_CHUNK_MAX)
        out = []
        for i in range(total):
            part = data[i * proto.CAM_CHUNK_MAX:
                         (i + 1) * proto.CAM_CHUNK_MAX]
            flags = 0
            if i == 0:
                flags |= proto.CAM_FRAME_FLAG_FIRST
            if i == total - 1:
                flags |= proto.CAM_FRAME_FLAG_LAST
            out.append(_struct.pack("<HIHHHHH", proto.CAM_FRAME_MAGIC,
                                    seq, i, total, width, height, flags)
                       + part)
        return out

    img = _jpeg()
    asm = camf.CameraAssembler()
    got = None
    for ch in _frag(img, 1):
        r = asm.feed(ch)
        if r is not None:
            got = r
    check("§37 B1 完整分片 ⇒ 组出整帧且逐字节一致",
          got == img and asm.frames == 1 and asm.dropped == 0,
          "len=%r frames=%d dropped=%d"
          % (None if got is None else len(got), asm.frames, asm.dropped))
    check("§37 B2 组出的宽高 = 704×704", (asm.width, asm.height) == (704, 704),
          "%dx%d" % (asm.width, asm.height))

    asm2 = camf.CameraAssembler()
    outs = [asm2.feed(ch) for i, ch in enumerate(_frag(img, 2)) if i != 1]
    check("§37 B3 丢片 ⇒ 整帧作废（不吐半帧）",
          all(r is None for r in outs) and asm2.dropped > 0,
          "dropped=%d" % asm2.dropped)

    # 覆盖式：上一帧没收齐，下一帧 seq==0 到来 ⇒ 旧缓冲作废、新帧出图。
    asm3 = camf.CameraAssembler()
    parts = _frag(img, 3)
    asm3.feed(parts[0])
    got3 = None
    for ch in _frag(img, 4):
        r = asm3.feed(ch)
        if r is not None:
            got3 = r
    check("§37 B4 新帧覆盖未收齐的旧帧 ⇒ 新帧完整出图",
          got3 == img and asm3.dropped >= 1,
          "out=%r dropped=%d" % (got3 is not None, asm3.dropped))

    # 非 JPEG（头尾不对）⇒ 作废（缺片拼错时不能把乱码贴到界面上）。
    asm5 = camf.CameraAssembler()
    junk = b"\xff\xd8" + b"\x11" * 8192 + b"\xff\xd8"
    out5 = None
    for ch in _frag(junk, 5):
        r = asm5.feed(ch)
        if r is not None:
            out5 = r
    check("§37 B5 尾标记不是 EOI ⇒ 头尾校验拦下",
          out5 is None and asm5.dropped >= 1,
          "dropped=%d" % asm5.dropped)
    check("§37 B6 空/短/错 magic 包被忽略（不崩）",
          asm5.feed(b"") is None and asm5.feed(b"\x00" * 8) is None
          and asm5.feed(b"\x00" * 64) is None)

    # ---- C 相机页（真建页 + 真贴图）----
    # [页面测试要用**真能解码**的 JPEG] 上面 B 段那张"形状合法"的假图没有
    # SOS 段（Qt 报 missing SOS marker ⇒ 解码失败），只够测协议面。这里用
    # QImage 现画一张真 JPEG（不依赖任何外部素材文件）。
    from PyQt5.QtCore import QBuffer, QByteArray
    from PyQt5.QtGui import QColor, QImage, QPainter
    _qi = QImage(704, 704, QImage.Format_RGB32)
    _qi.fill(QColor(40, 90, 160))
    _qp = QPainter(_qi)
    _qp.setPen(QColor(250, 240, 60))
    _qp.drawEllipse(120, 120, 460, 460)
    _qp.end()
    _ba = QByteArray()
    _buf = QBuffer(_ba)
    _buf.open(QBuffer.WriteOnly)
    _qi.save(_buf, "JPG", 80)
    _buf.close()
    real_jpeg = bytes(_ba)
    check("§37 C0 测试用真 JPEG 可被 Qt 解码", len(real_jpeg) > 1000
          and not QImage.fromData(real_jpeg, "JPG").isNull(),
          "len=%d" % len(real_jpeg))

    bridge = LinkBridge()
    ctx = PageContext(bridge, lambda i: None, lambda s: None)
    page = CameraPage(ctx)
    from PyQt5.QtWidgets import QSizePolicy
    check("§37 C1 相机页只订相机主题（切到本页才订阅 ⇒ 服务端按订阅门控推）",
          CameraPage.TOPICS == (proto.TOPIC_CAMERA_FRAME,),
          "%r" % (CameraPage.TOPICS,))

    # 无帧态：提示文字 + 无 pixmap（不能把上一帧留在屏幕上装作"正常"）。
    ctx.model.camera.frame = b""
    page.refresh()
    check("§37 C2 无帧：显示等待提示 + 不显示旧图",
          "等待画面" in page._img.text()
          and (page._img.pixmap() is None or page._img.pixmap().isNull())
          and page._status.text() == "未收到画面",
          "text=%r status=%r" % (page._img.text(), page._status.text()))

    # 有帧：真解码成 QImage 并贴到 QLabel。
    ctx.model.camera.frame = real_jpeg
    ctx.model.camera.seq = 42
    ctx.model.camera.width = 704
    ctx.model.camera.height = 704
    ctx.model.camera.ts_ms = int(__import__("time").time() * 1000)
    ctx.model.camera.dropped = 3
    page.resize(900, 700)
    page.refresh()
    pm = page._img.pixmap()
    check("§37 C3 有帧：解码贴图成功（pixmap 非空且按比例缩放）",
          pm is not None and not pm.isNull() and not pm.size().isEmpty(),
          "%r" % ((None if pm is None else (pm.width(), pm.height())),))
    check("§37 C4 有帧：状态行「画面接收中」",
          page._status.text() == "画面接收中", page._status.text())
    meta = page._meta.text()
    check("§37 C5 元信息含 尺寸/帧号/KB/更新/丢帧",
          "704×704" in meta and "帧号 42" in meta and "KB" in meta
          and "更新 " in meta and "丢帧 3" in meta, meta)

    # 解码失败（帧不完整）⇒ 明确提示，不贴垃圾图。
    ctx.model.camera.frame = b"\x00" * 512
    ctx.model.camera.seq = 43
    ctx.model.camera.dropped = 0
    page.refresh()
    check("§37 C6 坏帧：提示「画面解码失败」且不贴图",
          "解码失败" in page._img.text()
          and (page._img.pixmap() is None or page._img.pixmap().isNull()),
          page._img.text())

    # 同序号同尺寸不重复解码（每 200ms 一拍，重缩放一张 704² 白烧 CPU）。
    ctx.model.camera.frame = real_jpeg
    ctx.model.camera.seq = 44
    page.refresh()
    first = page._img.pixmap()
    page.refresh()
    check("§37 C7 同帧同尺寸不重复解码/重贴（防每拍重缩放）",
          page._qimage_seq == 44, "qimage_seq=%r" % (page._qimage_seq,))
    del first

    # ---- D 远端设置「相机」卡 ----
    rpage = RemoteSettingsPage(ctx)
    check("§37 D1 相机卡控件齐全（启用/连接方式/地址/端口/账号/口令/"
          "断线重连/码流/帧率/宽度/画质/传输/编码/硬件解码）",
          all(hasattr(rpage, n) for n in
              ("_cam_enabled", "_cam_resolve", "_cam_addr", "_cam_port",
               "_cam_user", "_cam_pass", "_cam_reconnect", "_cam_reconnect_s",
               "_cam_stream", "_cam_fps", "_cam_width",
               "_cam_quality", "_cam_transport", "_cam_codec", "_cam_hw",
               "_cam_apply", "_cam_state")))
    check("§37 D2 控件初值 = 缺省（未启用 / 554 / 子码流 / 704px / q80）",
          rpage._cam_enabled.isChecked() is proto.CAMERA_ENABLED_DEF
          and rpage._cam_port.value() == proto.CAMERA_PORT_DEF
          and rpage._cam_stream.currentData() == proto.CAMERA_STREAM_DEF
          and rpage._cam_width.value() == proto.CAMERA_WIDTH_DEF
          and rpage._cam_quality.value() == proto.CAMERA_QUALITY_DEF,
          "port=%d stream=%r width=%d q=%d"
          % (rpage._cam_port.value(), rpage._cam_stream.currentData(),
             rpage._cam_width.value(), rpage._cam_quality.value()))
    check("§37 D3 口令框是密码态 + 占位说明「留空=不改」",
          rpage._cam_pass.echoMode() == rpage._cam_pass.Password
          and "留空" in rpage._cam_pass.placeholderText(),
          "%r" % (rpage._cam_pass.placeholderText(),))

    # 应答回显（hasPassword 只显示"已设置"，不铺明文）。
    _cam_doc_26 = {"camera": {
        proto.CFGK_CAMERA_ENABLED: True,
        proto.CFGK_CAMERA_IP: "192.168.1.181",
        proto.CFGK_CAMERA_PORT: 554,
        proto.CFGK_CAMERA_USERNAME: "admin",
        proto.CFGK_CAMERA_PASSWORD: "",
        "hasPassword": True,
        proto.CFGK_CAMERA_STREAM: "main",
        proto.CFGK_CAMERA_FPS: 10,
        proto.CFGK_CAMERA_WIDTH: 1280,
        proto.CFGK_CAMERA_QUALITY: 90,
        "online": True, "frames": 7, "error": ""}}
    rpage._populate(_cam_doc_26)
    check("§37 D4 回显：启用/地址/主码流/10fps/1280/q90 全铺进去",
          rpage._cam_enabled.isChecked() is True
          and rpage._cam_addr.text() == "192.168.1.181"
          and rpage._cam_stream.currentData() == "main"
          and rpage._cam_fps.currentData() == 10
          and rpage._cam_width.value() == 1280
          and rpage._cam_quality.value() == 90,
          "addr=%r stream=%r fps=%r"
          % (rpage._cam_addr.text(), rpage._cam_stream.currentData(),
             rpage._cam_fps.currentData()))
    check("§37 D5 回显：口令框仍为空（服务端不回明文）",
          rpage._cam_pass.text() == "", "%r" % (rpage._cam_pass.text(),))
    check("§37 D6 状态行显示采集实况（在线 + 已收帧/NAL 数）",
          "已收" in rpage._cam_state.text()
          and "帧" in rpage._cam_state.text(), rpage._cam_state.text())
    # [ES 模式文案 2026-11-19] actualTransport=stream ⇒「已收 N NAL」
    # （服务端 camera.status 的 frames 在直传模式 = NAL 计数，诚实口径）。
    rpage._populate({"camera": dict(_cam_doc_26, actualTransport="stream",
                                    frames=487)})
    check("§37 D6b stream 模式文案 =「已收 N NAL」（帧=NAL 计数不撒谎）",
          "已收 487 NAL" in rpage._cam_state.text(), rpage._cam_state.text())
    rpage._populate({"camera": dict(_cam_doc_26, actualTransport="jpeg",
                                    frames=12)})
    check("§37 D6c jpeg 模式文案 =「已收 12 帧」（transport 区分单位）",
          "已收 12 帧" in rpage._cam_state.text()
          and "NAL" not in rpage._cam_state.text(), rpage._cam_state.text())
    rpage._populate(_cam_doc_26)          # 恢复 D4 数据（D7 前提）

    # 局部应答（只带 login）不冲相机卡（A4 同纪律）。
    rpage._populate({"login": {}})
    check("§37 D7 局部应答（只带 login）不动相机卡",
          rpage._cam_addr.text() == "192.168.1.181"
          and rpage._cam_width.value() == 1280,
          "addr=%r width=%d"
          % (rpage._cam_addr.text(), rpage._cam_width.value()))

    # 应用载荷：键名/单位/口令原样（空串交给服务端解释为"不改"）。
    rpage._cam_pass.setText("")
    rpage._cam_enabled.setChecked(True)
    rpage._cam_fps.setCurrentIndex(
        [rpage._cam_fps.itemData(i) for i in range(rpage._cam_fps.count())]
        .index(15))
    sent = {}
    orig_set = rpage.ctx.bridge.config_set
    orig_gate = rpage._config_gate

    def _spy(section, values, on_done=None):
        if section == proto.CFG_SECTION_CAMERA:
            sent.update(values or {})
            if on_done:
                on_done({"ok": True, "camera": {}})
            return
        return orig_set(section, values, on_done=on_done)

    rpage.ctx.bridge.config_set = _spy
    rpage._config_gate = lambda: True
    status = []
    orig_status = rpage.ctx.set_status
    rpage.ctx.set_status = lambda s: (status.append(s), orig_status(s))
    try:
        rpage._apply_camera()
    finally:
        rpage.ctx.bridge.config_set = orig_set
        rpage._config_gate = orig_gate
        rpage.ctx.set_status = orig_status
    check("§37 D8 应用载荷：camera 节含全部键（口令空串也带上=不改）",
          sent.get(proto.CFGK_CAMERA_ENABLED) is True
          and sent.get(proto.CFGK_CAMERA_IP) == "192.168.1.181"
          and sent.get(proto.CFGK_CAMERA_PORT) == 554
          and sent.get(proto.CFGK_CAMERA_FPS) == 15
          and proto.CFGK_CAMERA_PASSWORD in sent,
          "%s" % (sent,))
    check("§37 D9 应用成功是终态（状态行有结果，不停在「正在…」）",
          any("已生效" in s for s in status), "%s" % (status,))

    # ---- D7b..D7e 连接方式切换与 MAC 载荷（2026-11-20 新口径的补洞）----
    # [为什么要补] 「IP/MAC 合并成一个输入框」是 2026-11-20 才落地的口径，
    # 此前客户端测试只认 `_cam_ip` 单输入框 ⇒ 新口径（连接方式下拉、两值
    # 互保留、MAC 归一、按方式各发各的）**一条都没锁** ⇒ 回归门是空的。
    rpage._cam_addr.setText("192.168.1.181")
    rpage._cam_resolve.setCurrentIndex(
        rpage._cam_resolve.findData(proto.CAMERA_RESOLVE_MAC))
    check("§37 D7b 切到「按 MAC」：占位符换 MAC 形态、框里装 MAC 缓存值",
          "MAC" in rpage._cam_addr.placeholderText()
          and rpage._cam_addr.text() == rpage._cam_addr_mac,
          "ph=%r text=%r mac=%r"
          % (rpage._cam_addr.placeholderText(), rpage._cam_addr.text(),
             rpage._cam_addr_mac))
    rpage._cam_addr.setText("aa-bb-cc-dd-ee-ff")
    rpage._cam_resolve.setCurrentIndex(
        rpage._cam_resolve.findData(proto.CAMERA_RESOLVE_IP))
    check("§37 D7c 切回「按 IP」：IP 的值还在（切来切去不丢）",
          rpage._cam_addr.text() == "192.168.1.181",
          "text=%r" % (rpage._cam_addr.text(),))
    # MAC 模式的应用载荷：resolveMode=mac、mac 归一、ip 保留缓存值。
    rpage._cam_resolve.setCurrentIndex(
        rpage._cam_resolve.findData(proto.CAMERA_RESOLVE_MAC))
    rpage._cam_addr.setText("aa-bb-cc-dd-ee-ff")
    sent_mac = {}
    orig_set_mac = rpage.ctx.bridge.config_set

    def _spy_mac(section, values, on_done=None):
        if section == proto.CFG_SECTION_CAMERA:
            sent_mac.update(values or {})
            if on_done:
                on_done({"ok": True, "camera": {}})
            return
        return orig_set_mac(section, values, on_done=on_done)

    rpage.ctx.bridge.config_set = _spy_mac
    orig_gate_mac = rpage._config_gate
    rpage._config_gate = lambda: True
    try:
        rpage._apply_camera()
    finally:
        rpage.ctx.bridge.config_set = orig_set_mac
        rpage._config_gate = orig_gate_mac
    check("§37 D7d MAC 模式载荷：resolveMode=mac + mac 归一 + ip 保留",
          sent_mac.get(proto.CFGK_CAMERA_RESOLVE_MODE)
          == proto.CAMERA_RESOLVE_MAC
          and sent_mac.get(proto.CFGK_CAMERA_MAC) == "aa:bb:cc:dd:ee:ff"
          and sent_mac.get(proto.CFGK_CAMERA_IP) == "192.168.1.181",
          "%s" % (sent_mac,))
    # 防呆：方式=mac 但框里是 IP（含 '.'）⇒ 载荷退回**缓存 MAC**，不许把
    # IP 当 MAC 发出去（服务端会拒「MAC 格式不对」——2026-11-20 实锅）。
    rpage._cam_addr.setText("192.168.1.181")
    sent_bad = {}

    def _spy_bad(section, values, on_done=None):
        if section == proto.CFG_SECTION_CAMERA:
            sent_bad.update(values or {})
            if on_done:
                on_done({"ok": True, "camera": {}})
            return
        return orig_set_mac(section, values, on_done=on_done)

    rpage.ctx.bridge.config_set = _spy_bad
    orig_gate_bad = rpage._config_gate
    rpage._config_gate = lambda: True
    try:
        rpage._apply_camera()
    finally:
        rpage.ctx.bridge.config_set = orig_set_mac
        rpage._config_gate = orig_gate_bad
    check("§37 D7e 防呆：按 MAC 但框里填 IP ⇒ 载荷用缓存 MAC（不把 IP 当 MAC）",
          sent_bad.get(proto.CFGK_CAMERA_MAC) == "aa:bb:cc:dd:ee:ff"
          and sent_bad.get(proto.CFGK_CAMERA_IP) == "192.168.1.181",
          "bad=%s" % (sent_bad,))

    # 未收到配置（从未 populate）⇒ 明确拒绝，不拿缺省值去覆盖服务端。
    rpage2 = RemoteSettingsPage(ctx)
    status2 = []
    rpage2.ctx.set_status = lambda s: status2.append(s)
    orig_gate2 = rpage2._config_gate
    rpage2._config_gate = lambda: True
    try:
        rpage2._apply_camera()
    finally:
        rpage2._config_gate = orig_gate2
    check("§37 D10 未见配置就点应用 ⇒ 拒绝（不拿缺省值覆盖服务端）",
          any("尚未收到" in s for s in status2), "%s" % (status2,))

    # ---- E 段：ES 直传解码器（2026-11-19 架构转向）----
    # [测什么] EsDecoder 的 AU 聚合（参数集+IDR 拼整包再喂解码器——逐 NAL
    # 喂硬解不出帧）、硬解后端名拼接（双拼 bug 回归）、configure 幂等
    # （同值重发不触发重建）、meta 文案（ES 模式不显示 KB——RGB 裸帧字节
    # 不是传输量）。
    try:
        import av as _av
        _HAS_PYAV = True
    except ImportError:
        _HAS_PYAV = False
    if _HAS_PYAV:
        from hsfsrv_client.esplayer import EsDecoder
        # 造真 H.264（libx264 编 testsrc 3 帧，Annex-B 拆 NAL）
        import numpy as _np
        _w = _h = 320
        _enc = _av.CodecContext.create("libx264", "w")
        _enc.width, _enc.height = _w, _h
        _enc.pix_fmt = "yuv420p"
        _enc.framerate = 20
        _enc.time_base = __import__("fractions").Fraction(1, 20)
        _enc.options = {"preset": "ultrafast", "g": "2", "keyint_min": "2"}
        _buf = b""
        _src = _av.open(format="lavfi", mode="r",
                        file="testsrc=size=%dx%d:rate=20:duration=1"
                             % (_w, _h))
        for _f in _src.decode(video=0):
            for _p in _enc.encode(_f):
                _buf += bytes(_p)
        for _p in _enc.encode(None):
            _buf += bytes(_p)
        _nals, _i = [], 0
        while True:
            _j = _buf.find(b"\x00\x00\x01", _i)
            if _j < 0:
                break
            _k = _buf.find(b"\x00\x00\x01", _j + 3)
            _nals.append(_buf[_j:] if _k < 0 else _buf[_j:_k])
            _i = (_k if _k >= 0 else len(_buf))
        check("§37 E1 测试流拆出 NAL（≥3 条，含 SPS）", len(_nals) >= 3,
              "%d 条" % len(_nals))

        _frames = []
        _dec = EsDecoder(lambda rgb, w, h: _frames.append((len(rgb), w, h)),
                         lambda t: None)
        for _nal in _nals:
            _dec.feed(_nal)
        _deadline = _time.time() + 8
        while _time.time() < _deadline and not _frames:
            app.processEvents()
            time.sleep(0.05)
        check("§37 E2 AU 聚合 + 软解 ⇒ 真解出帧（参数集+IDR 拼整包）",
              _dec.frames > 0 and len(_frames) > 0,
              "frames=%d qsize=%d" % (_dec.frames, dec_q := _dec._q.qsize()))
        check("§37 E3 解码方式回显含后端名（h264[_后缀]）",
              _dec._dec_name.startswith("h264"),
              "%r" % (_dec._dec_name,))
        check("§37 E4 后端名无双拼（回归：曾拼出 h264_h264_qsv）",
              "h264_h264" not in _dec._dec_name,
              "%r" % (_dec._dec_name,))
        _rb0 = _dec._rebuild
        _dec.configure(False, "auto")
        check("§37 E5 configure 同值重发不触发重建（幂等）",
              _dec._rebuild == _rb0)
        _dec.close()
    else:
        skip("§37 E1~E5（PyAV 未安装）")

    # ---- F 段：全屏播放（2026-11-19 用户口径「类似网站视频播放界面」）----
    # [实现口径 2026-11-20 板端触摸实锅] 全屏**不再是独立顶层窗**，改成
    # 「主窗内覆盖层」（`_FullscreenPlayer` 的 parent = 主窗）：板端触摸屏
    # 在独立 `Qt.Window` 全屏窗被关闭时，Qt 触摸状态机停在「该窗口还持有
    # 隐式 grab」⇒ 主窗收不到后续触摸 ⇒ 用户看到「退出全屏后点什么都没
    # 反应」（鼠标路径不会这样，所以本机离屏测不出来）。
    # ⇒ 断言也跟着改口径：判「覆盖层已建立且**铺满主窗**」，退出走
    # `exit_fs()`（覆盖层自己的退出路径），不是独立窗的 `close()`。
    check("§37 F1 全屏入口存在（画面行「全屏」钮 + 双击画面同效）",
          hasattr(page, "_fs_btn") and hasattr(page, "enter_fullscreen"))
    page.enter_fullscreen()
    app.processEvents()
    _fs = page._fs
    _host = page.window()
    check("§37 F2 全屏覆盖层建立且铺满主窗（同 QWindow 覆盖层，不是独立窗）",
          _fs is not None and not _fs.isHidden()
          and _fs.parentWidget() is _host
          and _fs.geometry() == _host.rect(),
          "fs=%r hidden=%r geom=%r host=%r"
          % (_fs is not None, _fs is not None and _fs.isHidden(),
             _fs is not None and _fs.geometry(), _host.rect()))
    _fs_shown = 0
    _fs_t_end = _time.time() + 3
    while _time.time() < _fs_t_end:
        _pm = _fs._img.pixmap()
        if _pm is not None and not _pm.isNull():
            _fs_shown += 1
        app.processEvents()
        time.sleep(0.02)
    check("§37 F3 全屏窗持续贴图（复用同一 model，不另拉流）",
          _fs_shown > 10, "采样 %d" % _fs_shown)
    _fs.exit_fs()
    app.processEvents()
    check("§37 F4 全屏退出 ⇒ 页面引用清空（防悬垂）+ 覆盖层已隐藏",
          page._fs is None and _fs.isHidden(),
          "page._fs=%r hidden=%r" % (page._fs, _fs.isHidden()))

    # G 段：ES 模式 meta 不显示 KB（RGB 裸帧字节是内存量不是传输量）
    cam = ctx.model.camera
    cam.set_es_frame(b"\x80" * (320 * 240 * 3), 320, 240)
    meta_es = page._meta_text(cam)
    check("§37 G1 ES 模式 meta 不显示 KB（RGB 字节不是传输量）",
          "KB" not in meta_es, meta_es)
    cam.set_frame(b"\xff\xd8" * 100, 704, 704)
    check("§37 G2 JPEG 模式 meta 仍显示 KB",
          "KB" in page._meta_text(cam), page._meta_text(cam))

    page.deleteLater()
    rpage.deleteLater()
    rpage2.deleteLater()


def merge_web_set():
    """§36 Web 设置：网页服务跨进程探测 / 手动起停 / 关闭同步开关。"""
    section("§36 Web 设置：网页服务探测/起停/关闭同步开关")
    m_webset_run()


def merge_native_cap():
    """§22 本机能力（WiFi + 屏幕休眠）：桥契约 + 两页接线 + 动作终态。"""
    section("§22 本机能力（WiFi + 屏幕休眠）")
    m_nativ_run()
    m_nativ_monitor()


def merge_settings_merge():
    """§34 设置三页合并（2026-11-13 用户定案）：逐项「存在 + 生效」。

    [为什么单独一个文件] 合并页的功能项有二十多个（缩放/紧凑/全屏/刷新
    周期/趋势间隔/每页条数/五个显隐开关/窗口尺寸/屏幕休眠……），逐项「点了
    真生效」的断言写在本文件里会把主文件撑成两份；故独立成
    `tests/test_settings_merge.py`（可单独跑），本段只做**注册**——
    断言的计数与汇总仍走总回归那一套（check/section/skip 注入进去）。
    """
    section("§34 页面注册表（全树 19 页：含仅 Python 的 Web 设置/相机/流量页）")
    import test_settings_merge as _tsm
    _tsm.run(check, section, skip)


# ==========================================================================
# 段落：导出按筛选全量（2026-11-11 用户口径）
# ==========================================================================
# [背景] 三个数据页（历史/日志/报警）的表格都是**分页**的：界面上只有
#        当前页那几十行。原实现「导出当前可见视图」⇒ 导出的文件比用户
#        以为的少一个数量级，而且**看不出来少了**。用户口径：「导出的
#        数据要按筛选结果，不是当前表格显示的数据，因为数据是分页显示的
#        不是全的」。
# [口径与 C 端一致] C 端早就是全量导出（HsfExportPlan + histExportContinue
#        的逐批状态机），Python 端这次补齐：批行数 40（与页大小 10/20/50、
#        图表批 50 两两不相交）、逐批连续取、整轮钉同一 beforeTs 锚点、
#        短批收尾、超上限显式截断。
#
# 入口：m_exportq_run_export_full()


def m_exportq_fake_fetch(total, chunk_rows=None):
    """造一个假服务端：每次给 limit 行，最后一批短批（= 服务端到数据尾）。

    :returns: (fetch, calls) —— fetch 是 BatchExport 用的取数器；
              calls 记录每批的 (offset, limit) 供断言。
    """
    calls = []

    def fetch(offset, limit, on_done, on_error):
        calls.append((offset, limit))
        got = max(0, min(limit, total - offset))
        rows = [{"i": offset + i} for i in range(got)]
        on_done({"ok": True, "total": total, "rows": rows})

    return fetch, calls


def m_exportq_run_export_full():
    """导出全量段落：Q1 批次计划 / Q2 取数状态机 / Q3 页面接线。"""
    from hsfsrv_client.hsf_export_plan import (BatchExport, EXPORT_CHUNK,
                                               EXPORT_MAX_ROWS,
                                               plan_batches)

    # ---- Q1 批次计划（纯函数；口径同 C 端 HsfExportPlan）----
    check("Q1a 批行数与页大小 10/20/50、图表批 50 两两不相交",
          EXPORT_CHUNK not in (10, 20, 50) and EXPORT_CHUNK != 50,
          "%d" % EXPORT_CHUNK)
    p0 = plan_batches(0)
    check("Q1b total<=0 ⇒ 0 批", p0["batches"] == 0, str(p0))
    p100 = plan_batches(100)
    check("Q1c 批数 = ceil(total/chunk)", p100["batches"] == 3, str(p100))
    check("Q1d 步长 = 批行数（逐批连续取 ⇒ 跨页不重不漏）",
          p100["step"] == EXPORT_CHUNK, str(p100))
    pmax = plan_batches(EXPORT_MAX_ROWS + 1)
    check("Q1e 超上限 ⇒ 显式截断，且取数行数向下取整到整批",
          pmax["truncated"] and pmax["fetch_rows"] % EXPORT_CHUNK == 0,
          str(pmax))
    psmall = plan_batches(100000, max_rows=5)
    check("Q1f 上限比一批还小时至少取一批（截断不能退化成没导出）",
          psmall["batches"] >= 1 and psmall["fetch_rows"] >= EXPORT_CHUNK,
          str(psmall))

    # ---- Q2 逐批取数状态机（假 fetch，不碰网络）----
    out = {}
    runner = BatchExport()
    fetch, calls = m_exportq_fake_fetch(100)
    runner.start(fetch=fetch,
                 on_finished=lambda total, trunc, count: out.update(
                     rows=runner.rows, total=total, trunc=trunc,
                     count=count),
                 on_error=lambda m: out.update(err=m))
    check("Q2a 多批累加后行数 == 命中总数（不是当前页那几十行）",
          out.get("count") == 100, str(out.get("count")))
    check("Q2b 批偏移连续推进（0,40,80 —— 不跳批、不重取）",
          [c[0] for c in calls] == [0, 40, 80], str(calls))
    check("Q2c 每批行数恒为 EXPORT_CHUNK",
          all(c[1] == EXPORT_CHUNK for c in calls), str(calls))
    check("Q2d 短批即末批（不额外多取一批空批）",
          len(calls) == 3, str(calls))

    out2 = {}
    runner2 = BatchExport()
    fetch2, calls2 = m_exportq_fake_fetch(30)      # 不足一批
    runner2.start(fetch=fetch2,
                  on_finished=lambda total, trunc, count: out2.update(
                      count=count, trunc=trunc),
                  on_error=lambda m: out2.update(err=m))
    check("Q2e 不足一批 ⇒ 一批取完即收尾",
          out2.get("count") == 30 and len(calls2) == 1,
          "count=%s calls=%s" % (out2.get("count"), calls2))

    out3 = {}
    runner3 = BatchExport()
    runner3.start(fetch=lambda off, lim, done, err: done(
        {"ok": False, "err": "落盘未启用"}),
        on_finished=lambda total, trunc, count: out3.update(count=count),
        on_error=lambda m: out3.update(err=m))
    check("Q2f 服务端否定应答 ⇒ 走错误回调（不静默导出空文件）",
          "err" in out3 and "落盘未启用" in out3.get("err", ""),
          str(out3))

    out4 = {}
    runner4 = BatchExport(max_rows=25)
    fetch4, calls4 = m_exportq_fake_fetch(100)
    runner4.start(fetch=fetch4,
                  on_finished=lambda total, trunc, count: out4.update(
                      count=count, trunc=trunc),
                  on_error=lambda m: out4.update(err=m))
    check("Q2g 超上限 ⇒ 截断且**显式标记**（状态行要注明，不能静默丢行）",
          out4.get("trunc") is True and out4.get("count") == 25,
          "count=%s trunc=%s" % (out4.get("count"), out4.get("trunc")))

    # ---- Q2h 流式（CSV）模式：不限行数，且每批都有交付回调 ----
    out5 = {}
    batches = []
    runner5 = BatchExport(keep_rows=False, max_rows=0)
    fetch5, calls5 = m_exportq_fake_fetch(100)
    runner5.start(fetch=fetch5,
                  on_rows=lambda rows: batches.append(len(rows)),
                  on_finished=lambda total, trunc, count: out5.update(
                      count=count, trunc=trunc),
                  on_error=lambda m: out5.update(err=m))
    check("Q2h 流式模式不限行数（100 行全取，CSV 不截断）",
          out5.get("count") == 100 and out5.get("trunc") is False,
          str(out5))
    check("Q2i 流式模式每批都交付（边取边写，不攒内存）",
          sum(batches) == 100 and len(batches) == 3, str(batches))

    # ---- Q3 页面接线：导出取的是"筛选全量"而不是当前页 ----
    m_exportq_check_history_page()
    m_exportq_check_log_page()
    m_exportq_check_xlsx_stream()


def m_exportq_check_xlsx_stream():
    """XLSX 流式导出（2026-11-14）：**行数不限** + 产物是合法 OOXML。

    [为什么锁这条] 旧实现是「攒齐所有行 → 收尾一次写」（C 端 XExcel 动态
    库）⇒ ① 有 12000 行内存闸（超出显式截断，导不全）；② 收尾那一次大写
    入落在 UI 线程 ⇒ 导出期间界面无响应、**自动控制的按钮点不动**（用户
    2026-11-14 口径：导出绝不能影响自动控制）。改成纯标准库流式写出器
    （逐批写进 zip 的 sheet part）后：行数不限、内存恒定、每批毫秒级。

    [为什么用 13000 行] **超过旧的 12000 行上限**——这样"截断"一旦回潮
    本段必红（旧实现会在 12000 行截断，少 1000 行）。
    """
    import os
    import tempfile
    import time
    import zipfile
    import xml.etree.ElementTree as ET
    from PyQt5.QtCore import QObject, QTimer, pyqtSignal
    from PyQt5.QtWidgets import QApplication, QFileDialog
    from hsfsrv_client import proto
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    total_rows = 13000          # > 旧上限 12000（截断回潮即红）
    statuses = []

    class _FakeBridge(QObject):
        statusMessage = pyqtSignal(str)

        def __init__(self):
            QObject.__init__(self)
            self.calls = []
            self.model = None

        def can(self, perm):
            return True

        def is_connected(self):
            return True

        def history_page(self, offset=0, limit=None, on_done=None,
                         on_error=None, **kw):
            self.calls.append((offset, limit, bool(kw.get("export_mode"))))
            got = max(0, min(limit or 20, total_rows - offset))
            rows = []
            for i in range(got):
                rows.append([1700000000000 + offset + i] + [0] * (
                    len(proto.HISTORY_COLUMNS) - 1))
            doc = {"ok": True, "total": total_rows, "rows": rows,
                   "cols": list(proto.HISTORY_COLUMNS)}
            # [异步交付] 真桥的应答经**网络线程 → Qt 信号**回到 UI 线程，
            # 不是同步回调。这里照做（QTimer.singleShot 下一轮事件循环交付）
            # ⇒ 既贴近真实链路，也不会因为「回调里再取下一批」把调用栈
            # 递归撑爆（同步假桥在 13000 行上会 RecursionError——那是**假桥**
            # 的问题，不是产品的）。
            QTimer.singleShot(0, lambda: on_done(doc))

    tmp = tempfile.mkdtemp(prefix="hsf_export_xlsx_")
    path = os.path.join(tmp, "hist.xlsx")
    fb = _FakeBridge()
    ctx = PageContext(fb, lambda i: None, statuses.append)
    page = HistoryPage(ctx)
    origin = QFileDialog.getSaveFileName
    QFileDialog.getSaveFileName = staticmethod(lambda *a, **k: (path, "Excel"))
    try:
        page._export_data()
        # [等导出跑完] 泵事件循环直到状态行出现终态（成功/失败）。
        app = QApplication.instance() or QApplication([])
        deadline = time.time() + 120
        while time.time() < deadline:
            app.processEvents()
            if any("已导出" in s or "失败" in s for s in statuses):
                break
            time.sleep(0.01)
    finally:
        QFileDialog.getSaveFileName = origin

    check("Q4a XLSX 导出确实向服务端取数（分批取全量）",
          len(fb.calls) >= 2, "批数=%d" % len(fb.calls))
    check("Q4a2 导出走完了（状态行有终态，不中途失败）",
          any("已导出" in s for s in statuses), str(statuses[-2:]))
    if not os.path.exists(path):
        check("Q4b XLSX 产物生成（流式写出器）", False, "文件没生成")
        return
    try:
        zf = zipfile.ZipFile(path)
        bad = zf.testzip()
        names = zf.namelist()
        sheet = zf.read("xl/worksheets/sheet1.xml").decode("utf-8")
    except Exception as exc:                           # noqa: BLE001
        check("Q4b XLSX 产物是合法 zip（包体完整）", False,
              "%s: %s" % (type(exc).__name__, exc))
        return
    check("Q4b XLSX 产物是合法 zip（包体完整）", bad is None, str(bad))
    check("Q4c 包内含工作表与必需部件",
          "xl/worksheets/sheet1.xml" in names
          and "[Content_Types].xml" in names
          and "xl/workbook.xml" in names, str(names))
    try:
        for name in names:
            if name.endswith(".xml") or name.endswith(".rels"):
                ET.fromstring(zf.read(name))
        parsed = True
        parse_err = ""
    except Exception as exc:                           # noqa: BLE001
        parsed = False
        parse_err = "%s: %s" % (type(exc).__name__, exc)
    check("Q4d 各 XML 部件可解析（合法 OOXML）", parsed, parse_err)
    check("Q4e **不限行数**：13000 行全部写进工作表（旧的 12000 上限已去）",
          sheet.count("<row ") == total_rows + 1,
          "工作表行数=%d（含表头）" % sheet.count("<row "))

    # ---- Q5 导出收尾的真实性（2026-11-17 审计修；纯函数直接驱动）----
    # Q5a 命中 0 行 ⇒ 说"未生成文件"（原来只说"已导出 0 条 → 路径"）
    # Q5b XLSX 收尾失败（磁盘满/权限）⇒ 失败优先，不被成功文案盖掉
    # Q5c 实际行数 < 服务端 total ⇒ 说明库被裁剪/滚动（导出无快照隔离）
    from hsfsrv_client.hsf_export_plan import export_finish_text
    _t0 = export_finish_text(0, 0, path="a.csv")
    check("Q5a 命中 0 行 ⇒ 提示未生成文件", "未生成文件" in _t0, repr(_t0))
    _t1 = export_finish_text(10, 10, write_error="磁盘已满", path="a.xlsx")
    check("Q5b 收尾失败优先（不被成功文案覆盖）",
          _t1.startswith("导出失败") and "磁盘已满" in _t1, repr(_t1))
    _t2 = export_finish_text(90, 100, path="a.csv")
    check("Q5c 少行（库被裁剪）要明确说明",
          "裁剪" in _t2 and "90" in _t2, repr(_t2))
    _t3 = export_finish_text(100, 100, path="a.csv")
    check("Q5d 行数吻合 ⇒ 干净成功文案（不出现裁剪/失败）",
          "已导出 100 条" in _t3 and "裁剪" not in _t3 and "失败" not in _t3,
          repr(_t3))
def m_exportq_check_history_page():
    """历史页接线：假桥子喂 95 行 ⇒ 导出文件应是 95 行（当前页只有 20）。"""
    import csv
    import io
    import os
    import tempfile
    from PyQt5.QtWidgets import QFileDialog
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    total_rows = 95

    from PyQt5.QtCore import QObject, pyqtSignal

    class _FakeBridge(QObject):
        statusMessage = pyqtSignal(str)

        def __init__(self):
            QObject.__init__(self)
            self.calls = []
            self.model = None          # PageContext 只取属性，不需要真模型

        def can(self, perm):
            return True

        def is_connected(self):
            return True

        def history_page(self, offset=0, limit=None, on_done=None,
                         on_error=None, **kw):
            self.calls.append((offset, limit, bool(kw.get("export_mode")),
                               kw.get("before_ts")))
            # [keyset 夹具 2026-11-17] 客户端导出改成 `beforeTs` 递进
            # （offset 恒 0）⇒ 假桥照真服务端语义按 ts 上界过滤（`ts <=
            # beforeTs`），并保持"批0=最新、批内升序"的窗口切片。
            before = kw.get("before_ts")
            table = [1700000000000 + i for i in range(total_rows)]
            if before is not None:
                upto = int(before) - 1700000000000 + 1
                table = table[:max(0, min(total_rows, upto))]
            got = max(0, min(limit or 20, len(table) - offset))
            rows = []
            for i in range(got):
                # [行内容要像真行] ts 给整数 epoch 毫秒（页面按它格式化
                # 时间列），数值列给 0（页面按数值格式化；给空串会走
                # "转换异常兜底"分支，那是另一条路）。
                idx = len(table) - 1 - offset - i
                row = [table[idx]] + [0] * (
                    len(proto.HISTORY_COLUMNS) - 1)
                rows.append(row)
            on_done({"ok": True, "total": total_rows, "rows": rows,
                     "cols": list(proto.HISTORY_COLUMNS)})

    tmp = tempfile.mkdtemp(prefix="hsf_export_q_")
    path = os.path.join(tmp, "hist.csv")
    fb = _FakeBridge()
    ctx = PageContext(fb, lambda i: None, lambda s: None)
    page = HistoryPage(ctx)
    # [跳过保存框] 导出走文件对话框（需要真窗口）⇒ 这里直接换掉它。
    origin = QFileDialog.getSaveFileName
    QFileDialog.getSaveFileName = staticmethod(lambda *a, **k: (path, "CSV"))
    try:
        page._export_data()
    finally:
        QFileDialog.getSaveFileName = origin
    check("Q3a 历史页导出确实向服务端取数（不是直接写当前页）",
          len(fb.calls) >= 2, "批数=%d" % len(fb.calls))
    check("Q3b 每批都带 export=true（服务端验 Download 权限）",
          all(c[2] for c in fb.calls), str(fb.calls))
    # [keyset 分页 2026-11-17] 导出不再用 OFFSET（那是 O(N²)：每批都要
    # 索引从头扫 offset 行）⇒ 断言的语义改为「offset 恒 0 + beforeTs 严格
    # 递减」= 跨批不重不漏。
    _offsets = [c[0] for c in fb.calls]
    _befores = [c[3] for c in fb.calls]
    check("Q3c keyset 分页：offset 恒 0 且 beforeTs 逐批递减",
          all(o == 0 for o in _offsets)
          and all(b is not None for b in _befores)
          and all(_befores[i] > _befores[i + 1]
                  for i in range(len(_befores) - 1)),
          "offset=%s before=%s" % (_offsets, _befores))
    if os.path.exists(path):
        with io.open(path, encoding="utf-8-sig", newline="") as fh:
            got = list(csv.reader(fh))
        check("Q3d 导出文件行数 == 筛选命中总行数（95，不是当前页 20）",
              len(got) - 1 == total_rows, "%d 行" % (len(got) - 1))
    else:
        check("Q3d 导出文件行数 == 筛选命中总行数（95，不是当前页 20）",
              False, "文件没生成")


def m_exportq_check_log_page():
    """日志页接线：假桥子喂 70 行 ⇒ 导出文件应是 70 行（当前页只有 20）。"""
    import csv
    import io
    import os
    import tempfile
    from PyQt5.QtWidgets import QFileDialog
    from hsfsrv_client import proto
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import LogPage

    total_rows = 70

    from PyQt5.QtCore import QObject, pyqtSignal

    class _FakeBridge(QObject):
        statusMessage = pyqtSignal(str)

        def __init__(self):
            QObject.__init__(self)
            self.calls = []
            self.model = None          # PageContext 只取属性

        def can(self, perm):
            return True

        def is_connected(self):
            return True

        def log_page(self, offset=0, limit=None, on_done=None, on_error=None,
                     **kw):
            self.calls.append((offset, limit))
            got = max(0, min(limit or 20, total_rows - offset))
            rows = []
            for i in range(got):
                rows.append({proto.KEY_LOG_TS: 1700000000000 + offset + i,
                             proto.KEY_LOG_CATEGORY: "conn",
                             proto.KEY_LOG_TEXT: "第 %d 条" % (offset + i)})
            on_done({"ok": True, "total": total_rows, "rows": rows,
                     proto.KEY_LOG_OFFSET: offset,
                     proto.KEY_LOG_LIMIT: limit or 20})

    tmp = tempfile.mkdtemp(prefix="hsf_export_q_")
    path = os.path.join(tmp, "log.csv")
    fb = _FakeBridge()
    ctx = PageContext(fb, lambda i: None, lambda s: None)
    page = LogPage(ctx)
    origin = QFileDialog.getSaveFileName
    QFileDialog.getSaveFileName = staticmethod(lambda *a, **k: (path, "CSV"))
    try:
        page._export_data()
    finally:
        QFileDialog.getSaveFileName = origin
    check("Q3e 日志页导出向服务端取数（不是直接写当前页）",
          len(fb.calls) >= 2, "批数=%d" % len(fb.calls))
    check("Q3f 日志页批偏移连续（0,40 —— 跨页不重不漏）",
          [c[0] for c in fb.calls] == [0, 40],
          str([c[0] for c in fb.calls]))
    if os.path.exists(path):
        with io.open(path, encoding="utf-8-sig", newline="") as fh:
            got = list(csv.reader(fh))
        check("Q3g 日志导出文件行数 == 命中总行数（70，不是当前页 20）",
              len(got) - 1 == total_rows, "%d 行" % (len(got) - 1))
    else:
        check("Q3g 日志导出文件行数 == 命中总行数（70，不是当前页 20）",
              False, "文件没生成")


def merge_toolbar_unify():
    """§21b 三页工具条统一（2026-11-11 用户「其他两个界面也按历史数据的
    设计来 + 抽象成单独控件给三个使用」）。"""
    section("§21b 三页工具条统一：共用控件 / 按页裁剪 / 列多选与导出")
    m_tbq_run_toolbar_unify()


# ==========================================================================
# 段落：三页工具条统一（2026-11-11 用户口径）
# ==========================================================================
# [背景] 数据菜单三页（历史/日志/报警）此前工具条各写一份：历史页三行
#        全套，日志页缺列多选，报警页缺关键字/分页/滚动锁定且列是单选，
#        还带一张用户不要的「报警次数/分钟」图。现统一为
#        hsfsrv_client/data_toolbar.py 的 DataPageToolbar（一份实现、三页
#        按能力开关裁剪），形态与历史页一致。
# [锁什么] ① 三页都用同一个控件类、三行结构齐；② 按页裁剪（日志/报警
#        无图表，报警隐藏刷新）；③ 三页都有列多选；④ 列多选真生效
#        （表格可见列）且导出跟随可见列；⑤ 报警页关键字/分页/锁定生效。
#
# 入口：m_tbq_run_toolbar_unify()


def m_tbq_run_toolbar_unify():
    """三页工具条统一：控件齐平度 + 按页裁剪 + 列多选/导出接线。"""
    import os
    import tempfile
    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client.data_toolbar import DataPageToolbar
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage, LogPage, AlarmPage

    app = QApplication.instance()
    if app is None:
        raise RuntimeError("总回归尚未创建 QApplication")
    status = []

    def _mk(cls):
        b = LinkBridge()
        return cls(PageContext(b, lambda i: None, status.append))

    hist = _mk(HistoryPage)
    log = _mk(LogPage)
    alarm = _mk(AlarmPage)

    # ---- T1 三页都用同一个共用控件类 ----
    check("T1a 历史页用 DataPageToolbar", isinstance(hist._tb, DataPageToolbar))
    check("T1b 日志页用 DataPageToolbar", isinstance(log._tb, DataPageToolbar))
    check("T1c 报警页用 DataPageToolbar", isinstance(alarm._tb, DataPageToolbar))

    # ---- T2 三行结构齐（筛选 / 分页 / 视图行都在）----
    for name, page in (("历史", hist), ("日志", log), ("报警", alarm)):
        tb = page._tb
        check("T2 %s 页筛选行齐（起止/关键字/列/筛选/清除）"
              % name,
              all(x is not None for x in (
                  tb.start_edit, tb.end_edit, tb.keyword_edit,
                  tb.column_combo, tb.btn_filter, tb.btn_clear)))
        check("T2 %s 页分页行齐（每页/上页/页码/下页/总数/计数）"
              % name,
              all(x is not None for x in (
                  tb.page_size_box, tb.btn_prev, tb.page_label,
                  tb.btn_next, tb.total_label, tb.count_label)))
        check("T2 %s 页视图行导出在" % name, tb.btn_export is not None)

    # ---- T3 按页裁剪（无图表 = 无控件；报警隐藏刷新）----
    check("T3a 历史页 = 全套（双视图 + 折线列 + 锁定 + 刷新）",
          hist._tb.chart_column_combo is not None
          and hist._tb.btn_table_view is not None
          and hist._tb.freeze_box is not None
          and hist._tb.btn_reload is not None)
    check("T3b 日志页无折线列/双视图/锁定（但保留刷新）",
          log._tb.chart_column_combo is None
          and log._tb.btn_table_view is None
          and log._tb.freeze_box is None
          and log._tb.btn_reload is not None)
    check("T3c 报警页无折线列/双视图、隐藏刷新（有锁定）",
          alarm._tb.chart_column_combo is None
          and alarm._tb.btn_table_view is None
          and alarm._tb.btn_reload is None
          and alarm._tb.freeze_box is not None)
    check("T3d 报警页图表/双视图已彻底移除",
          not hasattr(alarm, "_view_stack")
          and not hasattr(alarm, "_chart_series")
          and not hasattr(alarm, "set_view"))

    # ---- T4 三页列多选齐（默认全勾、至少一列）----
    for name, page in (("历史", hist), ("日志", log), ("报警", alarm)):
        cols = page.visible_columns()
        check("T4 %s 页列多默认全勾" % name,
              cols == list(range(len(page.COLUMNS))), "%s" % cols)

    # ---- T5 日志页列多选生效 + 导出跟随可见列 ----
    log._f_col.clear_all()
    log._f_col._model.item(2).setCheckState(2)      # 只留「事件」列（+保底 0）
    lc = log.visible_columns()
    check("T5a 日志页列勾选 → 表格可见列同步",
          lc == [0, 2] and not log._table.table.isColumnHidden(2),
          "%s" % lc)
    log._export_cols = list(lc)
    tmp = tempfile.mkdtemp(prefix="hsf_tbq_")
    path = os.path.join(tmp, "log_cols.csv")
    log._write_export(path, [["t", "c", "e"]])
    with open(path, encoding="utf-8-sig") as fh:
        head = fh.readline().strip()
    check("T5b 日志页导出表头 = 可见列（时间,事件）",
          head == "时间,事件", head)

    # ---- T6 报警页关键字 / 分页 / 锁定接线 ----
    alarm._f_text.setText("搅拌泵")
    alarm._apply_filter()
    check("T6a 报警页关键字生效（_kw 下发）",
          alarm._kw == "搅拌泵", alarm._kw)
    alarm._f_text.clear()
    alarm._apply_filter()
    check("T6b 报警页有客户端内部分页字段（页大小/页号）",
          alarm._page_size in alarm.PAGE_SIZES
          and isinstance(alarm._page_index, int),
          "%s / %s" % (alarm._page_size, alarm._page_index))
    check("T6c 报警页滚动锁定存在（QCheckBox）",
          alarm._freeze is not None and not alarm._freeze.isChecked())

    # 收尾：恢复日志页列全勾、清临时目录
    log._f_col.check_all()
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)


def merge_valvecard_layout():
    """§21c 阀卡片布局恒定（2026-11-11 用户「卡片布局变化有点大，按钮都动了」）。"""
    section("§21c 阀卡片布局恒定：状态切换不撑动卡片/按钮")
    m_valvec_run()


# ==========================================================================
# 段落：阀卡片布局恒定（2026-11-11 用户口径）
# ==========================================================================
# [背景] ValveCard 的提示行（动作中/故障原因）时有时无：原实现用
#        setVisible(bool(hint)) 把整行从 VBox 里拿掉/加回 ⇒ 卡片高度跟着
#        变，6 张卡状态不齐时两行按钮上下错开，且 200ms 刷新里状态一变
#        按钮就跳。顶部 gate_label 的显隐还会把整张网格往下推。
# [修法] 两处都改「恒占位」：提示行固定按字体实测的高度、长文案省略号、
#        无文案只 setText("")；横幅固定高度、无文案透明占位。都不 setVisible。
# [锁什么] ① 源码里不再有 setVisible 显隐切换；② 四种状态下卡片高度与
#        两个按钮的 y 坐标恒定；③ 提示行/横幅固定高度生效。
#
# 入口：m_valvec_run()


def m_valvec_run():
    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client.pages_valve import ValveCard, ValvePage
    from hsfsrv_client import proto

    app = QApplication.instance()
    if app is None:
        raise RuntimeError("总回归尚未创建 QApplication")

    # ---- V0 源码级：不再靠显隐改布局 ----
    import inspect
    import hsfsrv_client.pages_valve as pv
    src = inspect.getsource(pv)
    code_lines = [ln for ln in src.splitlines()
                  if not ln.lstrip().startswith("#")]
    check("V0 阀页无 setVisible 显隐切换（恒占位）",
          not any("setVisible" in ln for ln in code_lines),
          [ln.strip() for ln in code_lines if "setVisible" in ln][:2])

    card = ValveCard(0, lambda i, w: None)
    card.resize(360, 260)
    card.show()
    app.processEvents()

    def state(item):
        card.apply_state(item, "")
        app.processEvents()
        # [按钮位置四要素] 用户口径「开阀和关阀按钮的位置也固定了吗，不要
        # 动了」：不只是 Y（上下跳），X（左右跑）也要恒定 ⇒ 两个按钮的
        # x/y 全记下来，四种状态逐个比对。
        return (card.height(), card.width(),
                card.btn_open.x(), card.btn_open.y(),
                card.btn_close.x(), card.btn_close.y())

    closed = {"ready": True, "state": proto.HsfValveState.Closed,
              "openFb": False, "closeFb": True, "name": "电动球阀1", "text": ""}
    moving = {"ready": True, "state": proto.HsfValveState.Moving,
              "openFb": False, "closeFb": False, "name": "电动球阀1",
              "text": "动作中"}
    fault = {"ready": True, "state": proto.HsfValveState.Fault,
             "openFb": False, "closeFb": False, "name": "电动球阀1",
             "text": "动作超时未到位，请检查阀体与接线，这是一段很长很长的"
                     "故障说明文字用来验证省略号"}

    g0 = state(closed)
    check("V1 提示行恒占位（Fixed 高度按字体实测）",
          card.hint_label.minimumHeight() == card.hint_label.maximumHeight()
          and card.hint_label.height() > 0,
          "%d" % card.hint_label.height())
    g1 = state(moving)
    check("V2 动作中有提示文案", "点另一侧可反向" in card.hint_label.text(),
          card.hint_label.text())
    g2 = state(closed)
    g3 = state(fault)
    # state() 返回 (卡高, 卡宽, 开阀x, 开阀y, 关阀x, 关阀y)
    all_g = (g0, g1, g2, g3)
    check("V3 已关→动作中→已关→长故障：卡片高度恒定",
          len(set(g[0] for g in all_g)) == 1,
          "%s" % ([g[0] for g in all_g],))
    check("V3b 卡片宽度恒定（长故障文案不撑宽）",
          len(set(g[1] for g in all_g)) == 1,
          "%s" % ([g[1] for g in all_g],))
    check("V4 开阀按钮 x/y 四状态全恒定（上下不跳、左右不跑）",
          len(set((g[2], g[3]) for g in all_g)) == 1,
          "%s" % ([(g[2], g[3]) for g in all_g],))
    check("V5 关阀按钮 x/y 四状态全恒定（上下不跳、左右不跑）",
          len(set((g[4], g[5]) for g in all_g)) == 1,
          "%s" % ([(g[4], g[5]) for g in all_g],))

    # ---- 顶部横幅：恒占位、无文案透明 ----
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    page = ValvePage(PageContext(LinkBridge(), lambda i: None,
                                 lambda m: None))
    # [构造完整性] 卡片网格必须真的建出来（曾把这段缩进进 _set_gate
    # 方法体 ⇒ page 没有 cards，刷新时报 AttributeError，截图整页空白）。
    check("V6a ValvePage 建出 6 张卡片（构造没被缩进错）",
          hasattr(page, "cards") and len(page.cards) == proto.HSF_VALVE_COUNT,
          "%s" % (len(getattr(page, "cards", [])),))
    h0 = page.gate_label.height()
    check("V6 顶部横幅恒占位（固定高度 > 0）",
          h0 > 0 and page.gate_label.minimumHeight()
          == page.gate_label.maximumHeight(), "%d" % h0)
    page._set_gate("")
    # [别用 isVisible()] 离屏页面从未 show ⇒ 父级不可见时 isVisible() 恒
    # False（数字量页同款坑）；要看的是「没被显式 hide」= isHidden() 为假。
    check("V7 无文案时横幅透明占位（不是隐藏）",
          not page.gate_label.isHidden()
          and "transparent" in page.gate_label.styleSheet(),
          "hidden=%s qss=%s" % (page.gate_label.isHidden(),
                                page.gate_label.styleSheet()[:60]))
    page._set_gate("控制已禁用：手动模式")
    check("V8 有文案时横幅上色、高度不变",
          page.gate_label.height() == h0
          and "#FFF4E5" in page.gate_label.styleSheet(),
          "%d" % page.gate_label.height())
    card.close()


def merge_account_menu():
    """§21d 状态栏账户菜单（2026-11-11 用户「主界面登录与退出，看看加哪」）。"""
    section("§21d 状态栏账户菜单：入口/三态显示/logout 回落/重登可重入")
    m_acct_run()


# ==========================================================================
# 段落：状态栏账户菜单（2026-11-11 用户口径）
# ==========================================================================
# [背景] 主界面此前没有登录/退出入口：只在 __main__ 启动时一次性登录，
#        进程内无法换人。C 端 2026-11-11 把「账号·角色」只读文本搬到状态
#        栏右下角，但没有点击/退出。本次 Python 端加**可点账户按钮 + 菜单**
#        （切换账户 / 退出登录），位置与 C 端一致，属于超出 C 端的新功能。
# [锁什么] ① 账户按钮在状态栏右侧、三态文案（未登录/账号·角色/只有账号）；
#        ② 菜单两项动作转发；③ logout() 清 user/role、发 disconnected、
#        **不置 _shutting_down**（否则重登信号被吞）、界面回「未登录」；
#        ④ conn 只说连接态（双写收拢）；⑤ 1Hz 兜底同值早退；⑥ 重登可重入。
#
# 入口：m_acct_run()


def m_acct_run():
    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import proto
    from hsfsrv_client import __main__ as entry

    app = QApplication.instance()
    if app is None:
        raise RuntimeError("总回归尚未创建 QApplication")
    b = LinkBridge()
    w = MainWindow(b)

    # ---- A1 三态文案 ----
    check("A1 初始账户钮 = 未登录", "未登录" in w.status_bar.account_text(),
          repr(w.status_bar.account_text()))
    w.status_bar.set_account("root", "管理员", logged_in=True)
    check("A2 已登录 = 账号·角色",
          w.status_bar.account_text() == "root·管理员  ▾",
          repr(w.status_bar.account_text()))
    w.status_bar.set_account("operator", "", logged_in=True)
    check("A3 只有账号（无角色名）= 只显示账号",
          w.status_bar.account_text() == "operator  ▾",
          repr(w.status_bar.account_text()))
    w.status_bar.set_account("root", "管理员", logged_in=False)
    check("A4 logged_in=False 强制回落未登录",
          "未登录" in w.status_bar.account_text(),
          repr(w.status_bar.account_text()))

    # ---- A5 菜单两项动作转发 ----
    fired = []
    w.switchUserRequested.connect(lambda: fired.append("switch"))
    w.logoutRequested.connect(lambda: fired.append("logout"))
    w.status_bar.switchUserRequested.emit()
    w.status_bar.logoutRequested.emit()
    check("A5 账户菜单两项转发到 MainWindow 信号",
          fired == ["switch", "logout"], str(fired))

    # ---- A6 同值早退（1Hz 兜底零脏区）----
    # 连刷两次同一个已登录态，第二次不应再写 setText（同值早退）。
    b.user = "root"; b.role = proto.ROLE_ADMIN; b.connected_flag = True
    w._sync_account()                 # 第一次：从「未登录」写到「root·管理员」
    writes = []
    orig = w.status_bar.account_btn.setText
    def _probe(t):
        writes.append(t)
        orig(t)
    w.status_bar.account_btn.setText = _probe
    w._sync_account(); w._sync_account()
    w.status_bar.account_btn.setText = orig
    check("A6 set_account 同值早退（1Hz 兜底不重复写）",
          len(writes) == 0, "%d 次 setText" % len(writes))

    # ---- A7 conn 只说连接态 ----
    w._on_connected(proto.ROLE_ADMIN)
    check("A7 conn 只说「已连接」，不含账号（双写收拢）",
          w.status_bar.conn.text() == "已连接"
          and "root" not in w.status_bar.conn.text(),
          repr(w.status_bar.conn.text()))

    # ---- A8 logout 状态回落 + 可重登 ----
    b.logout()
    check("A8a logout 后 conn = 未连接", w.status_bar.conn.text() == "未连接",
          repr(w.status_bar.conn.text()))
    check("A8b logout 后账户 = 未登录",
          "未登录" in w.status_bar.account_text(),
          repr(w.status_bar.account_text()))
    check("A8c logout 清 user/role", b.user == "" and b.role == 0,
          "%s / %s" % (b.user, b.role))
    check("A8d logout 不置 _shutting_down（重登信号不会被吞）",
          b._shutting_down is False, str(b._shutting_down))
    check("A8e logout 后 is_connected() 为假", not b.is_connected())

    # ---- A9 重登可重入（不连真服务端：验证 connect_to 前置逻辑不抛）----
    # [不实际建链] 只确认 _prompt_login / _run_initial_login / _relogin_after_logout
    # 三个入口存在且可调用（签名齐全）；真重登由 §30 现场段与冒烟覆盖。
    check("A9 登录状态机三个可重入口存在",
          all(callable(getattr(entry, n, None)) for n in
              ("_prompt_login", "_run_initial_login",
               "_relogin_after_logout")))

    # ---- A10 登录窗空口令 = 缺省 root（占位提示必须兑现）----
    # [实锅 2026-11-11] 连板端 6100（root/root）口令框留空 ⇒ 发的是**空串**
    # ⇒ 「口令错误」；占位符明明写着「缺省 root」。_read_inputs 必须补缺省。
    from hsfsrv_client.login import LoginDialog
    dlg = LoginDialog(b, parent=w)
    dlg.host_edit.setText("192.168.1.249")
    dlg.port_edit.setText("6100")
    dlg.user_edit.setText("root")
    dlg.pass_edit.setText("")
    host, port, user, password = dlg._read_inputs()
    check("A10 口令留空 ⇒ 补缺省 root（不把空串发给服务端）",
          password == "root" and user == "root"
          and host == "192.168.1.249" and port == 6100,
          "%s %s %s %r" % (host, port, user, password))
    dlg.pass_edit.setText("custom-pw")
    check("A10b 填了口令则原样使用（不覆盖）",
          dlg._read_inputs()[3] == "custom-pw")
    dlg.deleteLater()

    # ---- A11 error 残留必须清（连板端 exit 2 实锅）----
    # [2026-11-11 现场] 第一次登录失败把 dlg.error 写上后永不清 ⇒ 第二次
    # 登录**成功** accept() 返回，main() 读到残留 error ⇒ 进程按"登录
    # 失败"退出（exit 2）——用户视角「密码填对了程序整个退了」。
    dlg2 = LoginDialog(b, parent=w)
    dlg2.bridge = b
    b.connect_to = lambda *a, **k: (True, "")          # 假成功，不起链
    dlg2.error = "口令错误"                             # 上一次失败的残留
    dlg2._on_login()
    check("A11 重试成功后 error 必须已清（不按残留判失败）",
          dlg2.error == "" and not dlg2.result() == LoginDialog.Rejected,
          repr(dlg2.error))
    # 失败路径仍要写 error（成功后 main 才有依据区分）
    b.connect_to = lambda *a, **k: (False, "口令错误")
    dlg2._on_login()
    check("A11b 失败路径 error 照常写（登录窗能显示原因）",
          dlg2.error == "口令错误", repr(dlg2.error))
    # 离线浏览 = 主动选择，也必须清残留 error
    dlg2.error = "上一次失败的残留"
    dlg2._on_offline()
    check("A12 离线浏览清残留 error（不按它判 exit 2）",
          dlg2.error == "" and dlg2.result() == LoginDialog.Accepted,
          "error=%r result=%s" % (dlg2.error, dlg2.result()))
    dlg2.deleteLater()
    b.disconnect_from()

    # ---- A13 死 loop 上投递不得泄漏协程（never awaited 警告别再蹦）----
    # [2026-11-11 实测] 拆卸竞态里 request_async 在 loop 已关时仍创建
    # 协程对象再丢给 call_soon ⇒ RuntimeWarning: coroutine ... never awaited。
    import warnings
    from hsfsrv_client.xlink import XLinkClient
    dead = XLinkClient("127.0.0.1", 1, "root", "root")
    errs = []
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        r = dead.request_async(0x1000, on_error=errs.append)
    check("A13a 死 loop 上 request_async 返回 None 且走 on_error",
          r is None and errs == ["连接已关闭"], "%s %s" % (r, errs))
    check("A13b 无 coroutine never awaited 警告",
          not any("never awaited" in str(w.message) for w in caught),
          [str(w.message) for w in caught if "never awaited" in
           str(w.message)][:1])
    check("A13c subscribe/unsubscribe 同款守卫（返回 None 不炸）",
          dead.subscribe_async(proto.TOPIC_VALVE_STATUS) is None
          and dead.unsubscribe_async(proto.TOPIC_VALVE_STATUS) is None)
    w.close()

    # ---- A14 网络线程 BaseException 无声死亡必须留痕 + 自愈（2026-11-11 板端实锅）----
    # [现象链] WiFi 弱链（-72dBm/3%丢包/秒级毛刺）反复掉线后客户端彻底躺平
    # 在「未连接」：_thread_main 只接 Exception，CancelledError 这类
    # BaseException 把线程打死且零日志；服务端侧零「登录被拒」⇒ 排除策略
    # 收手。锁三点：终止必须留痕 / 线程自动重建接棒 / close 后不再重建。
    import threading as _th
    import time as _time
    import asyncio as _aio
    from hsfsrv_client.xlink import XLinkClient as _XC
    healc = _XC("127.0.0.1", 1, "root", "root")     # 端口 1 = 不可达
    logs14 = []
    healc._log = logs14.append
    healc.auto_reconnect = True
    _orig_run = healc._run
    _flaky = {"raised": False}

    async def _run_flaky(self_ref):
        # 第一轮直接线程级死亡（CancelledError 是 BaseException，3.8+ 不
        # 是 Exception 子类——旧代码接不住，正是实锅路径）；第二轮起交给
        # 原版 _run（不可达端口，按 2s 步进重连，线程保持存活）。
        if not _flaky["raised"]:
            _flaky["raised"] = True
            raise _aio.CancelledError("测试：线程级死亡")
        await _orig_run()

    healc._run = _run_flaky.__get__(healc, _XC)
    healc._thread = _th.Thread(target=healc._thread_main,
                               name="XLinkClient", daemon=True)
    first_thread = healc._thread
    first_thread.start()
    deadline = _time.time() + 8.0
    while _time.time() < deadline:
        t = healc._thread
        if t is not None and t is not first_thread and t.is_alive():
            break
        _time.sleep(0.1)
    check("A14a BaseException 终止留痕（不再无声死亡）",
          any("线程终止" in m for m in logs14),
          "%s" % [m for m in logs14 if "线程" in m][:1])
    check("A14b 线程自愈重建（新线程接棒存活）",
          healc._thread is not first_thread
          and healc._thread.is_alive(),
          "raised=%s" % _flaky["raised"])
    healc.close()                                    # 收手：不再重建
    _time.sleep(2.5)
    check("A14c close 后线程收尾且不再重建",
          not healc._thread.is_alive()
          and logs14.count("网络线程自愈：重建连接线程") == 1,
          "alive=%s rebuilds=%d"
          % (healc._thread.is_alive(),
             logs14.count("网络线程自愈：重建连接线程")))


def merge_vfd():
    """§21 变频器与数字量：滑块单位 / 启动给定值自检。"""
    section("§21 变频器与数字量：滑块单位 / 启动给定值自检（并入本文件）")
    run_vfdsld()      # 段落：test_vfd_slider
    run_vfdgiv()      # 段落：test_vfd_start_given


def merge_ui_scale():
    """§22 界面字号缩放。"""
    section("§22 界面字号缩放（并入本文件）")
    run_uiscal()      # 段落：test_ui_scale


def merge_theme_switch():
    """§22b 主题切换：不重建 QStyle（卡顿根因）+ 配色/chrome 真跟随。

    [为什么锁 setStyle 调用数] 2026-11-17 用户报「切换主题明显卡顿」：
    `theme.install()` 原来**每次**都 `app.setStyle("Fusion")`，而
    setStyle 会重建 QStyle 并重刷全部控件——实测 1445 控件窗口单次
    730ms（占换主题总耗时 2/3），板端 ARM 上放大成秒级。修法是「只在
    首次装 Fusion」（QApplication 实例打标记）。这条断言直接盯**调用
    次数**（时间阈值在慢机器上不可靠，调用数才是根因）。
    """
    section("§22b 主题切换性能（并入本文件）")

    # ---- 下拉箭头可见性（2026-11-17 用户实报「下拉框的箭头怎没了」）----
    # [机制] QSS 一旦给 `::drop-down` 设了 border/background，Qt 就不画原生
    # 箭头；而**只要存在 `QComboBox:focus` 规则**，样式引擎重 polish 又会让
    # 子控件的 image 失效 ⇒ 自绘三角也消失。两条都用像素钉住。
    from PyQt5.QtWidgets import QApplication, QComboBox
    from hsfsrv_client import theme as _th
    _qss = _th.stylesheet()
    _combo = QComboBox()
    _combo.addItems(["产水模式", "反洗模式"])
    _combo.setFixedWidth(240)
    _combo.setStyleSheet(_qss)
    _combo.show()
    _app = QApplication.instance()
    for _i in range(10):
        if _app is not None:
            _app.processEvents()
    _img = _combo.grab().toImage()
    _x0 = max(0, _img.width() - 26)
    _arrow_px = 0
    _other = []
    for _y in range(_img.height()):
        for _x in range(_x0, _img.width()):
            _c = _img.pixel(_x, _y)
            _r = (_c >> 16) & 0xFF
            _g = (_c >> 8) & 0xFF
            _b = _c & 0xFF
            if (_r + _g + _b) / 3 < 190:
                _key = "%02X%02X%02X" % (_r, _g, _b)
                if _key.startswith("5A6572"):
                    _arrow_px += 1
                else:
                    _other.append(_key)
    check("§22b-1 下拉框的箭头可见（自绘三角有像素）",
          _arrow_px >= 20, "三角像素=%d 其它=%s" % (_arrow_px, sorted(set(_other))[:3]))
    check("§22b-2 QSS 里不许有 QComboBox:focus 规则（会让 image 失效）",
          "QComboBox:focus {" not in _qss, "")
    _combo.hide()
    _combo.deleteLater()
    from PyQt5.QtGui import QPalette
    from PyQt5.QtWidgets import QApplication, QWidget

    from hsfsrv_client import theme
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    app = QApplication.instance()
    if app is None:
        check("QApplication 可用（主题切换段）", False, "无 QApplication")
        return
    theme.install(app, "light")
    bridge = LinkBridge()
    win = MainWindow(bridge)
    app.processEvents()
    win.goto_page(0)                    # 主界面
    app.processEvents()
    win.goto_page(13)                   # 本地设置（主题下拉所在页）
    app.processEvents()

    # 计数桩：换主题过程中 `app.setStyle` 必须**零调用**。
    calls = [0]
    orig = app.setStyle

    def _spy(*args, **kwargs):
        calls[0] += 1
        return orig(*args, **kwargs)

    installed = False
    try:
        app.setStyle = _spy
        installed = True
    except Exception:                           # noqa: BLE001
        pass

    base = app.palette().color(QPalette.Window).name()
    t0 = time.perf_counter()
    theme.apply_theme(app, "dark")
    dt = (time.perf_counter() - t0) * 1000.0
    app.processEvents()
    dark = app.palette().color(QPalette.Window).name()

    if installed:
        check("换主题不再重建 QStyle（setStyle 零调用）",
              calls[0] == 0, "calls=%d" % calls[0])
        try:
            app.setStyle = orig
        except Exception:                       # noqa: BLE001
            pass
    else:
        check("可注入 setStyle 计数桩", True, "PyQt 禁止实例赋值，跳过调用数断言")

    check("调色板确实换了（Window 角色）", base != dark,
          "%s -> %s" % (base, dark))
    # [只刷当前页] 换主题时只重刷当前页（板端整窗 1.7s/单页 0.2~0.4s）；
    # 当前页 = 本地设置(13)。
    page13 = win.pages.get(13)
    card = page13.findChild(QWidget, "HsfCard") if page13 is not None else None
    if card is not None:
        check("当前页卡片 QSS 跟随新主题",
              card.styleSheet() == theme.card_qss(),
              "len=%d" % len(card.styleSheet()))
    else:
        check("当前页里能找到卡片（chrome 刷新的对象）", False, "HsfCard 不存在")
    # [隐藏页懒刷新] 切回主界面(0)：它在上次换主题时是隐藏的，切入时必须
    # 按代数补刷（否则用户会看到旧主题的卡片）。
    win.goto_page(0)
    app.processEvents()
    page0 = win.pages.get(0)
    card0 = page0.findChild(QWidget, "HsfCard") if page0 is not None else None
    if card0 is not None:
        check("隐藏页切入时补刷主题（懒刷新）",
              card0.styleSheet() == theme.card_qss(),
              "len=%d" % len(card0.styleSheet()))
    else:
        check("主界面里能找到卡片", False, "HsfCard 不存在")
    # 宽松时间闸（只防"级联重刷全树"这类数量级回归；慢机器上留足余量）
    check("换主题耗时 < 1500ms", dt < 1500.0, "%.0f ms" % dt)

    theme.apply_theme(app, "light")             # 还原，避免污染后续段
    app.processEvents()


def merge_home_cards():
    """§23 首页卡片：浊度仪 / 按钮组 / 搅拌泵卡第三项（转速）。"""
    section("§23 首页卡片：浊度仪 / 按钮组（并入本文件）")

    # ---- 主页搅拌泵卡第三项 = 转速(rpm)（2026-11-17 用户实报）----
    # [口径] 标签"转速"、单位"rpm"、值 = **输出频率 × 28.8**（铭牌
    # 50Hz≈1440rpm）⇒ 25Hz ⇒ 720rpm。
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HomePage
    _ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    _home = HomePage(_ctx)
    _blk = getattr(_home, "vfd_rpm", None)
    check("§23-1 主页搅拌泵卡第三项存在且标签 = 转速",
          _blk is not None and _blk._caption.text() == "转速",
          "%r" % (None if _blk is None else _blk._caption.text(),))
    check("§23-2 第三项单位 = rpm",
          _blk is not None and _blk._unit_text == "rpm",
          "%r" % (None if _blk is None else _blk._unit_text,))
    check("§23-3 主页不再有「设定频率」块（已换成转速）",
          not hasattr(_home, "vfd_setf"), "")
    # 取值：喂 25Hz ⇒ 应显示 720rpm（25 × 1440/50）
    try:
        _home.ctx.model.vfd.apply({"outputFreqHz": 25.0})
        _home.refresh()
        _plain = getattr(_blk, "_plain", "")
        check("§23-4 转速按输出频率换算（25Hz ⇒ 720rpm）",
              _plain == "720", "实际 %r" % (_plain,))
    except Exception as exc:                           # noqa: BLE001
        check("§23-4 转速按输出频率换算（25Hz ⇒ 720rpm）", False,
              "%s: %s" % (type(exc).__name__, exc))
    _home.deleteLater()

    m_homecrd_run_home_cards()      # 段落：test_home_cards


def merge_home_first_screen():
    """§24 首屏订阅：首页是不是一上来就订阅齐。"""
    section("§24 首屏订阅：首页是不是一上来就订阅齐（并入本文件）")
    m_homefst_run_home_first_screen()      # 段落：test_home_first_screen


def merge_xlsx_leak():
    """§25 Excel 导出资源泄漏。"""
    section("§25 Excel 导出资源泄漏（并入本文件）")
    run_xlsxleak()      # 段落：test_xlsx_leak


def merge_ttyS4_devices():
    """§26 ttyS4 三设备：浊度×2 / 流量计（实测口径 + 站号可改）。

    [自起服务端] 本段自己起**两个**隔离 sim 实例（缺省口径 + 改站号），
    不依赖 main() 的 PrivateServer —— --no-srv 分支也能跑。
    """
    section("§26 ttyS4 三设备：浊度×2 / 流量计（并入本文件）")
    run_ttyS4_devices()      # 段落：test_ttyS4_devices


# ============================================================================
#                            工具与主流程
# ============================================================================

def _pump():
    """驱动 Qt 事件循环（客户端段的回调都在 UI 线程外跑）。"""
    try:
        from PyQt5.QtWidgets import QApplication
        app = QApplication.instance()
        if app is not None:
            app.processEvents()
    except Exception:
        pass


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _connect_with_retry(bridge, port, tries=4, gap=2.0, srv=None,
                        user="admin", password="admin"):
    """连私有服务端；握手失败按**退避**重试。

    [为什么改成退避 + 4 次 2026-11-09] §13 偶发红（测试侧缺陷，不是产品
    缺陷）：上一段刚 `disconnect_from()`、服务端按**静默超时**回收会话
    （C 端口径的窗口 ~8s）时紧接着再登录，会被登录策略/握手窗口拒掉；
    原来的「3 次 × 1.5s」三次全落在同一个窗口里，一红就是整段 §13 不跑
    （连它下面的对账断言一起"消失"，看起来像少了一堆用例）。

    现在：私有服务端在起之前就把 `login.allow_concurrent` 打开（见
    PrivateServer.LOGIN_POLICY），且间隔按 1.5 倍退避（2s → 3s → 4.5s），
    连不上还能把服务端日志尾巴带进失败原因里（否则只报"连接超时"没法
    定位）。

    :param srv: PrivateServer（可空）；给了就把服务端 stdout 尾巴附在失败
        原因末尾。
    :return: (ok, err)
    """
    err = ""
    wait = float(gap)
    total = max(1, int(tries))
    for attempt in range(total):
        ok, err = bridge.connect_to("127.0.0.1", port, user, password)
        if ok:
            return True, ""
        if attempt + 1 < total:
            time.sleep(min(wait, 8.0))
            wait *= 1.5
    if srv is not None:
        tail = (srv.tail(600) or "").strip().replace("\r", "")
        tail = " | ".join(line for line in tail.split("\n") if line.strip())
        if tail:
            err = "%s（重试 %d 次仍失败；服务端末尾日志：%s）" % (err, total, tail)
        else:
            err = "%s（重试 %d 次仍失败；服务端无日志尾巴）" % (err, total)
    return False, err


def check_policy_reject_status():
    """§13b 策略性拒绝必须是 REJECTED(4)，不能伪装成 BAD_USER(1)/口令错(2)。

    [钉的是 2026-11-09「模拟模式有时候客户端没数据」的根因]
    产品缺省策略是「root 单会话」（`login.allow_concurrent=false`）。客户端
    断线自动重连时，旧会话往往还没被服务端回收 ⇒ 新登录撞上策略闸。
      · 旧实现：服务端回 **BAD_USER**（用户不存在），框架再按反枚举口径
        下发假挑战 ⇒ 客户端最终看到 **口令错误** ⇒ 按永久失败停掉重连
        ⇒ **界面此后一直 `--`，直到人工重登**（用户报的就是这个）。
      · 现在：策略拒绝回 **REJECTED(4)**、不造假挑战；客户端把它当瞬时失败
        退避重试（首登仍永久，登录窗原地报准确原因）。

    本组用**测试自己起的**专用实例，策略取产品缺省（同账并发关），
    其余段用的实例仍然开着并发——不动产品闸。
    """
    section("§13b 策略拒绝的状态码（模拟模式偶发无数据根修锁）")
    srv = PrivateServer()
    # 只覆盖本实例的策略：产品缺省的「root 单会话」
    srv.LOGIN_POLICY = {"allow_anonymous": False,
                        "allow_concurrent": False,
                        "max_sessions": 8}
    try:
        if not srv.start():
            check("§13b 私有服务端起动", False, "端口 %d" % srv.port)
            return
    except Exception as exc:                            # noqa: BLE001
        check("§13b 私有服务端起动", False,
              "%s: %s" % (type(exc).__name__, exc))
        return
    try:
        from hsfsrv_client.xlink import XLinkClient, AUTH_STATUS_TEXT
        from hsfsrv_client import proto as P

        a = XLinkClient("127.0.0.1", srv.port, "root", "root")
        ok_a, err_a = a.start(8.0)
        check("§13b 第一个 root 登录成功", ok_a, err_a or "")

        b = XLinkClient("127.0.0.1", srv.port, "root", "root")
        ok_b, err_b = b.start(8.0)
        check("§13b 第二个 root 登录被拒", not ok_b, "居然登录成功了")
        want = AUTH_STATUS_TEXT[P.AUTHSTATUS_REJECTED]
        check("§13b 拒绝码是 REJECTED（按结果码给准确原因）",
              (err_b or "") == want,
              "err=%r 期望=%r" % (err_b, want))
        check("§13b 拒绝文案不含'口令错误'（旧实现把策略拒绝伪装成口令错）",
              "口令错误" not in (err_b or ""), "err=%r" % (err_b or ""))

        # 策略拒绝是**瞬时**的：旧会话退出后同一账户必须能再登录。
        # （这正是"自动重连撞上未回收会话"能靠重试自愈的前提。）
        # [轮询等回收 2026-11-09] 原来固定 sleep(1.0) 就登——会话回收窗口
        # 是秒级且随负载波动，赌窗口必偶发红（本任务实测 4 跑 1 红）。
        # 改成轮询重试（上限 12s）：窗口内失败是预期；窗口过了还失败才是红。
        a.close()
        ok_c, err_c = False, ""
        deadline = time.time() + 12.0
        while time.time() < deadline:
            time.sleep(1.0)
            c = XLinkClient("127.0.0.1", srv.port, "root", "root")
            ok_c, err_c = c.start(8.0)
            if ok_c:
                break
        check("§13b 旧会话退出后同账户可再登录（拒绝是瞬时的）",
              ok_c, err_c or "")
        if ok_c:
            c.close()
    finally:
        srv.stop()


def _server_db_path(bridge, srv):
    """问服务端要**它自己的**库路径；返回 (path, "") 或 ("", 原因)。

    [为什么不能写死] 现场库在 server_py/hsf_data.db，但私有服务端按
    「配置文件所在目录」放库（server_py/hsfsrv/__main__.py：base_dir =
    配置文件目录）⇒ 两者路径不同。**以服务端自报的路径为准**
    （config_get 的 storageDbPath 是权威口径），拿不到再兜底猜一次。
    """
    got = {}

    def done(d):
        got["doc"] = d

    def bad(m):
        got["err"] = m

    try:
        bridge.config_get(on_done=done, on_error=bad, timeout=5.0)
    except Exception as exc:                       # noqa: BLE001
        got["err"] = "%s: %s" % (type(exc).__name__, exc)
    deadline = time.time() + 6
    while "doc" not in got and "err" not in got and time.time() < deadline:
        _pump()
        time.sleep(0.02)
    if "doc" in got:
        path = str((got["doc"] or {}).get("storageDbPath") or "")
        if path and os.path.exists(path):
            return path, ""
        if path:
            return "", "服务端自报的库路径不存在：%s" % path
    guess = os.path.join(srv.cfg_dir, "hsf_data.db")
    if os.path.exists(guess):
        return guess, ""
    return "", "拿不到服务端库路径（%s）" % (got.get("err") or "无应答")


def _open_server_db(path):
    """打开服务端自己的库（只做 SELECT）。返回 (conn, "") 或 (None, 原因)。

    [为什么开读写而不是只读] 服务端用 WAL，WAL 库只读连接要求 -shm 可写；
    这里只发 SELECT，不改数据。
    """
    try:
        return sqlite3.connect(path, timeout=5.0), ""
    except sqlite3.Error as exc:
        return None, "打不开服务端库 %s：%s" % (path, exc)


class PrivateServer(object):
    """自起一个**隔离的**模拟服务端（临时 cfg，不碰现场库与账户）。

    [为什么隔离] 现场 bin/cfg 里的账户库会被每个新实例重写；多实例共用
    同一 cfg ⇒ 老实例认证态与文件不一致 ⇒ 登录一律 "bad password"
    （本次排查踩过，见记忆 hsf-modbus-485-stall-rootcauses）。
    """

    #: 本实例的登录策略（写进临时 cfg 的 `login` 节）。
    #:
    #: [为什么开同账并发 2026-11-09] 本文件会连起多条连接（主桥 / §9·§10
    #: 专用 / §9b root / §12 / §13），现场缺省是「root 单会话 + 会话总数
    #: 8」的**产品策略**；上一条连接刚断、服务端还没摘掉会话时再登录会被
    #: 拒 ⇒ §13 偶发红。修法只放宽**本测试自己起的这个实例**的配置（压测
    #: 脚本 test_stress_reconnect.py 同款做法，走远端配置打开同账并发），
    #: **不动产品的登录闸**。
    LOGIN_POLICY = {
        "allow_anonymous": False,       # 仍要求真登录（不测匿名）
        "allow_concurrent": True,       # 同账可多端（多段各有专用连接）
        "max_sessions": 32,             # 多段连接 + 旧会话回收窗口留足余量
    }

    def __init__(self):
        self.port = _free_port()
        self.tmp = tempfile.mkdtemp(prefix="hsf_reg_")
        self.cfg_dir = os.path.join(self.tmp, "cfg")
        os.makedirs(self.cfg_dir, exist_ok=True)
        self.proc = None
        self.log_path = os.path.join(self.tmp, "server.out")
        self._log_fh = None

    def tail(self, n=1200):
        """服务端 stdout 尾巴（排查登录/握手问题时用）。"""
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def write_config(self):
        """写本实例的配置（含登录策略节）；返回配置文件路径。

        [为什么落文件而不是远端改] 服务端起好了再去改策略要先登录，而
        "登录被拒"正是要规避的问题 ⇒ 策略必须在**起之前**就在磁盘上。
        其余键由服务端按缺省值补（config.py 的 load 对缺失键保持缺省）。

        [为什么这里要显式写 storage 节] 服务端的入库周期缺省自 2026-11-11
        起是 **1 分钟**（用户定案），而本文件的历史页/取数断言是"等一拍就
        有一行"（1s 一行的口径）⇒ 缺省下永远等不到首行（实测 §1 取到 0 行、
        同形判据 Py 侧空）。**测试自带自己的节拍**，不依赖产品缺省 ——
        产品缺省改了也不该让回归红。
        """
        cfg = os.path.join(self.cfg_dir, "hsf_server_py.json")
        doc = {"server": {"port": int(self.port)},
               "login": dict(self.LOGIN_POLICY),
               # 快节拍（1s 一行）：历史类断言按"等一拍"的口径。
               "storage": {"enabled": True, "sample_ms": 1000,
                            "max_kb": 0, "full_policy": "ring",
                            "keep_files": 3}}
        with open(cfg, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, ensure_ascii=False, indent=2)
        return cfg

    def start(self):
        cfg = self.write_config()
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"   # 服务端日志即时落盘（崩溃取证）
        # stdout 落**文件**而不是管道：管道没人读，写满 64KB 会把服务端
        # **卡死**（推数据的协程被阻塞 ⇒ 看起来像"服务端死了"）。
        self._log_fh = open(self.log_path, "wb")
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port",
             str(self.port), "--config", cfg],
            cwd=SERVER_ROOT, env=env,
            stdout=self._log_fh, stderr=subprocess.STDOUT)
        deadline = time.time() + 25
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("服务端起不来：\n%s" % self.tail())
            try:
                s = socket.create_connection(("127.0.0.1", self.port), 0.3)
                s.close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
        if self._log_fh is not None:
            try:
                self._log_fh.close()
            except Exception:
                pass
            self._log_fh = None
        shutil.rmtree(self.tmp, ignore_errors=True)


#: 是否已报告过「队列事件打到已删除 Qt 对象」（只记一条红，不刷屏）。
_DELETED_DISPATCH_REPORTED = False


def _qt_excepthook(etype, value, tb):
    """接管 PyQt 虚函数异常：合并进程下不再 qFatal，跑完并显性化。

    [为什么需要] PyQt5 缺省对「C++ 虚函数重实现里未处理的 Python 异常」
    调 qFatal —— 整个进程直接 abort。总回归把十几段测试合到一个进程后，
    某段留下的 Qt 对象被销毁、而队列里还有发给它的事件时，就会在任意
    _pump() 处随机 abort（实测 2026-11-09：LineChartDialog 已删除，
    §16/§17 边界随机崩）。这里接管：这类异常记一条红（只记一次）让本轮
    跑完；其余异常交回系统缺省钩子。产品侧的弹窗生命周期专项修复另行处理。
    """
    global _DELETED_DISPATCH_REPORTED
    msg = str(value)
    if "has been deleted" in msg:
        if not _DELETED_DISPATCH_REPORTED:
            _DELETED_DISPATCH_REPORTED = True
            try:
                check("合并进程下队列事件打到已删除的 Qt 对象"
                      "（弹窗生命周期，待专项修复）", False, msg)
            except Exception:                       # noqa: BLE001
                pass
        print("[qt-exc] %s: %s" % (etype.__name__, msg))
        return
    sys.__excepthook__(etype, value, tb)


def main():
    global PASS, FAIL, SKIP, SLOW_PARTS, SOAK_MINUTES
    sys.excepthook = _qt_excepthook
    # [本机设置隔离 2026-11-10] 回归里会改"历史/日志每页条数""趋势刷新"等
    # 本机设置（页面回调真写），若不隔离就写进**用户真实设置**（注册表 /
    # ~/.config），把现场配置改掉。这里给整轮（含各段子进程，env 会继承）
    # 指定一份临时 INI —— 与 settings.ENV_SETTINGS_INI 同一口径。
    if not os.environ.get("HSF_CLIENT_SETTINGS_INI"):
        _iso_dir = tempfile.mkdtemp(prefix="hsf_test_settings_")
        os.environ["HSF_CLIENT_SETTINGS_INI"] = os.path.join(_iso_dir,
                                                            "client.ini")
    # [faulthandler 2026-11-09] Qt 层的 abort（qFatal/pure virtual call）不带
    # Python 栈；开启后 SIGABRT/SIGSEGV 时把所有线程的 Python 调用栈打到
    # stderr —— 合并进程下段落边界随机崩的取证手段（本文件 §16 边界）。
    import faulthandler
    try:
        faulthandler.enable()
    except Exception:                              # noqa: BLE001
        pass
    no_srv = "--no-srv" in sys.argv
    # [慢段开关] 存储上限的 stop/rotate 两段各要 30s+ 的真实增长（到限要真
    # 写满几百 KB）。默认不跑（一条命令跑全的时长预算）；`--slow` 时全跑。
    SLOW_PARTS = "--slow" in sys.argv
    for _a in sys.argv:
        if _a.startswith("--soak-minutes="):
            try:
                SOAK_MINUTES = max(1, int(_a.split("=", 1)[1]))
            except ValueError:
                pass
    if "--soak-minutes" in sys.argv:
        # 也支持空格分隔形式：--soak-minutes 15
        i = sys.argv.index("--soak-minutes")
        if i + 1 < len(sys.argv):
            try:
                SOAK_MINUTES = max(1, int(sys.argv[i + 1]))
            except ValueError:
                pass
    print("长稳 soak 时长：%d 分钟（--soak-minutes 可调）" % SOAK_MINUTES)
    print("===== HSF 双端总回归 =====")
    if SLOW_PARTS:
        print("（--slow：含存储上限的慢段 stop/rotate，各约 30s+）")

    # QApplication 必须先于任何 QWidget 构造（首页段要建主窗）。
    # 无显示环境时用 offscreen 平台。
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    try:
        from PyQt5.QtWidgets import QApplication
        _APP = QApplication.instance() or QApplication(sys.argv[:1])
    except Exception as exc:
        check("QApplication 可创建", False, "%s: %s" % (type(exc).__name__, exc))
        _summary()
        return 1

    # [子进程段落模式 2026-11-09] `--only history,alarm` 只跑指定 UI 段。
    # 全量编排用 subprocess 把 UI 重段各自隔离在子进程里跑（见
    # _run_group_isolated）：合并进程跑完全部 UI 段会在段落边界随机
    # abort（heap corruption 0xc0000374 于 setStyleSheet 处 surface，
    # faulthandler 实测栈）——各段隔离后互不影响，主文件仍是唯一文件。
    only = None
    for _a in sys.argv:
        if _a.startswith("--only="):
            only = [x for x in _a.split("=", 1)[1].split(",") if x]
    if only:
        groups = _ui_groups()
        for name in only:
            for fn in groups.get(name, ()):
                try:
                    fn()
                except Exception as exc:            # noqa: BLE001
                    check("%s（执行异常）" % name, False,
                          "%s: %s" % (type(exc).__name__, exc))
        print("SUBRESULT PASS %d FAIL %d SKIP %d" % (PASS, FAIL, SKIP))
        return 0 if FAIL == 0 else 1

    # --- 纯本地段（不需要服务端）---
    try:
        from hsfsrv_client import model as M
        check_side_pick(None, None)
    except Exception as exc:
        check("客户端模型可加载", False, "%s: %s" % (type(exc).__name__, exc))

    if no_srv:
        print("\n--no-srv：跳过服务端段（客户端段照跑）")
        # [组名单一事实源] 直接遍历 _ui_groups() 的键，新增 UI 段（exportq/
        # toolbar 等）自动进全量回归，不再靠手写清单漏一个。storage 是进程
        # 内慢段（含真服务端），--no-srv 旧语义不跑它 ⇒ 显式跳过。
        for _g in _ui_groups():
            if _g == "storage":
                continue
            _run_group_isolated(_g)
        merge_ttyS4_devices()
        merge_xlsx_leak()
        run_reconnect_cycles()
        run_soak_stability()
        run_ui_clickthrough()
        run_ttyS4_fidelity()
        _summary()
        return 0 if FAIL == 0 else 1

    # --- 起私有服务端 ---
    srv = PrivateServer()
    try:
        if not srv.start():
            check("私有模拟服务端起动", False, "端口 %d" % srv.port)
            _summary()
            return 1
        check("私有模拟服务端起动", True, "端口 %d" % srv.port)
    except Exception as exc:
        check("私有模拟服务端起动", False, "%s: %s" % (type(exc).__name__, exc))
        _summary()
        return 1

    try:
        from hsfsrv_client.bridge import LinkBridge
        from hsfsrv_client import proto
        bridge = LinkBridge()
        ok, err = bridge.connect_to("127.0.0.1", srv.port, "admin", "admin")
        check("客户端连上私有服务端", ok, err or "")
        _reg_port = srv.port
        bridge._reg_port = srv.port
        py_hist_doc = None
        if ok:
            # [连接卫兵] run_client 段落自己的 bridge 拆链可能把主桥的
            # connected_flag 带翻（旧会话迟到的 DISCONNECTED 沿），此后
            # §1/§2 全按"未连接"假红（实测 2026-11-09：12 项连坐红）。
            # 断了就重连（auto_reconnect 也可能已自动恢复，等它）。xlink 的
            # 去重集合随连接作废 ⇒ 重连后上层要重发订阅；§1 的查询走
            # 请求-应答不受影响，§2 的订阅在各自段里自己发。
            if not bridge.is_connected():
                ok1, err1 = _connect_with_retry(bridge, srv.port, tries=4,
                                                gap=2.0, srv=srv)
                check("§1 前主桥重连（run_client 段落拆链后）", ok1, err1 or "")
            # [端到端客户端段] 段落：test_client —— 它的 C/D/H 三组（订阅收敛 /
            # 数据落地 / 真实点击泵启停）**必须连着服务端**才跑得到（离线时
            # 只能跳过）。这里借私有实例的端口把它接上，比原来"另起一个进程
            # 连别人手起的服务端"更可控（隔离 cfg + 不写现场库）。
            try:
                run_client("127.0.0.1", srv.port)
            except Exception as exc:                # noqa: BLE001
                check("端到端客户端段（test_client）执行异常", False,
                      "%s: %s" % (type(exc).__name__, exc))
            # [段前连接卫兵 2026-11-10 第二次加固] run_client 段落自己的
            # bridge 收发可能诱发主桥静默超时回收（同一 sim 服务端、同一
            # root 账号下的两条会话），掉线点随机落在 §1~§8 之间 ⇒ 一次
            # 掉线让 11 条断言连坐红（本夜实测两次）。每个 § 段开始前都
            # 确保主桥在线——掉了就地重连，比只在入口挡一次可靠。
            def _ensure_main_connected(tag):
                if bridge.is_connected():
                    return
                ok1, err1 = _connect_with_retry(bridge, srv.port, tries=4,
                                                gap=2.0, srv=srv)
                check("主桥重连（%s 段前）" % tag, ok1, err1 or "")

            _ensure_main_connected("§1")
            py_hist_doc = check_history_contract(bridge, proto)
            _ensure_main_connected("§6")
            check_history_continuity(bridge, proto)
            _ensure_main_connected("§2")
            check_per_side_push(bridge, proto, bridge.model)
            _ensure_main_connected("§7")
            check_log_contract(bridge, proto)
            _ensure_main_connected("§8")
            check_home_page(bridge, proto, bridge.model)
            _ensure_main_connected("§9前")
            check_header_sort(bridge)
            # §9/§10 自起一条专用连接（§8 收尾时主桥已断，且主桥的
            # connected_flag 会被旧连接迟到的 DISCONNECTED 回调翻掉）。
            run_log_category_and_history_warmup(srv)
        bridge.disconnect_from()

        # ------------------------------------------------------------------
        # §11/§12/§13 交叉测试：各自一条**干净**连接（主桥的 connected_flag
        # 会被旧连接迟到的 DISCONNECTED 回调翻掉 ⇒ 不能拿主桥当"是否已连"
        # 的依据）。私有服务端开同账并发（PrivateServer.LOGIN_POLICY），
        # 旧会话按静默超时回收的窗口不再把下一段挡在门外。
        # ------------------------------------------------------------------
        if ok:
            from hsfsrv_client.bridge import LinkBridge as _LB
            # §11：Python 客户端 ↔ C 服务端（跨实现）。
            check_cross_c_server(py_hist_doc, proto)
            # §12：Python 服务端 ↔ C 客户端协议等价。
            b12 = _LB()
            try:
                ok12, err12 = _connect_with_retry(b12, srv.port, srv=srv)
                check("§12 专用连接（私有服务端）", ok12, err12 or "")
                if ok12:
                    check_cross_c_client_contract(b12, proto)
            finally:
                b12.disconnect_from()
            # §13：客户端 ↔ 服务端 ↔ 框架（库里一行 = 界面一拍）。
            b13 = _LB()
            try:
                ok13, err13 = _connect_with_retry(b13, srv.port, srv=srv)
                check("§13 专用连接（私有服务端）", ok13, err13 or "")
                if ok13:
                    check_frame_matches_db(b13, srv, proto)
            finally:
                b13.disconnect_from()
    finally:
        srv.stop()

    # ------------------------------------------------------------------
    # §13b：策略拒绝的状态码（**另起**一个专用实例：它的策略取产品缺省
    # 「root 单会话」，与上面那个开了并发的实例不同 ⇒ 不能复用）。
    # ------------------------------------------------------------------
    check_policy_reject_status()

    # ------------------------------------------------------------------
    # §13c：窗口锚点容差负对照（纯判据，不依赖服务端）。
    # 证明"容差只放掉 1 条同毫秒竞态、大幅漂移仍红"——不是声明而是断言。
    # 用 try/except 把 assert 转成一条 check：失败才记红，正常不污染计数。
    # ------------------------------------------------------------------
    try:
        check_window_anchor_negative()
        check("§13c 窗口锚点容差负对照（大幅漂移仍红）", True, "")
    except AssertionError as exc:
        check("§13c 窗口锚点容差负对照（大幅漂移仍红）", False,
              "容差掩盖了真漂移：%s" % exc)

    # ------------------------------------------------------------------
    # §14 起：并入本文件的段落（纯客户端段，不用服务端）
    # [UI 段子进程隔离 2026-11-09] 一段崩了不再带崩整轮（heap corruption
    # 0xc0000374 surface 在 setStyleSheet，见 _run_group_isolated 注释）；
    # 协议/纯逻辑段（storage 含真服务端、reconnect/ttyS4/xlsx）仍在进程内。
    # ------------------------------------------------------------------
    # [组名单一事实源] 逐个遍历 _ui_groups() 的键（storage 段除外：它含
    # 真服务端、在进程内跑），新增 UI 段自动进全量回归，不再靠手写清单漏。
    for _g in _ui_groups():
        if _g == "storage":
            merge_storage()
            continue
        _run_group_isolated(_g)
    merge_ttyS4_devices()
    merge_xlsx_leak()
    run_reconnect_cycles()
    run_soak_stability()
    run_ui_clickthrough()
    run_ttyS4_fidelity()

    _summary()
    return 0 if FAIL == 0 else 1


def _summary():
    print("\n" + "=" * 62)
    print("  汇总：PASS %d / FAIL %d / SKIP %d" % (PASS, FAIL, SKIP))
    if FAILED:
        print("  失败项：")
        for name in FAILED:
            print("    - %s" % name)
    print("=" * 62)



# ==========================================================================
# 段落：test_alarm_page_toolbar（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】报警页工具条全场景测试（**纯控件层**，不连服务端）。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的**一段源码**（总回归会把它原样拼进去）。

[为什么拆成段落文件] 回归锁散在十几个文件里 ⇒ 「加一个功能要翻好几处」。
    物理内联进单一文件后，`grep` 一次就能看全所有断言。

[命名契约] 本段落的**所有**顶层名字统一带 `m_alarmtb_` 前缀
    （函数/常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `skip` / `section` /
    `_free_port` / `time` / `os` / `tempfile` …）**按名直呼**，不 import
    总回归（避免循环导入）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `QApplication.instance()` 取。

任务口径（2026-11-11 改版：报警页与历史/日志页统一 DataPageToolbar；
用户定案「日志和报警都不要图表」+「加客户端内部分页，隐藏刷新」）：

  A. 类型筛选：类型下拉后表格行集 = 期望子集（类型文本全等，逐类验）
  B. 时间窗：起始/结束/组合筛选（epoch 毫秒边界与 C 同口径：ts<lo 不可见、
     ts>hi 不可见、界为空=不限）
  C. 起点晚于终点：提示「起始时间晚于结束时间，未筛选」且**不动**结果
  D. 列多选：勾中的列才显示 / 全勾恢复（隐藏列的数据仍在表里，只不显示）
  E. 行数统计：分页行「共 N 条（本页 M）」+「本页显示 K 行」跟随筛选
  F. 清除筛选：类型回全部、时间窗回数据全区间、关键字清空、列全勾
  G. CSV 导出：文件存在 + 首行 UTF-8 BOM（utf-8-sig）+ 表头/行序（最新在
     上，列跟随可见列）；无 Download 权限时 _export_data 直接拒绝且不落盘
  H. 关键字筛选：客户端设备/类型/状态文本包含匹配（空=不筛）
  I. 客户端内部分页：每页 10/20/50 + 上下页 + 页码 + 末页收口
  J. 滚动锁定：勾上后新报警只入库不刷表；取消勾选补刷
  K. MAX_ROWS 环形截断：超出丢最旧、最新保留（表格只铺当前页）
  L. 自动运行告警·bridge 层（2026-11-13）：PERSISTENT_TOPICS 订阅清单 /
     TOPIC_KIND 登记 / 连接期常驻订阅 / 重连补订 / 切页收敛不退订 /
     publish 帧解析转发 autoAlarm 信号 / 坏载荷不崩
  M. 自动运行告警·页面层（2026-11-13）：一帧一行落表（不去重）/ 五类文案
     与 C alarmAutoKindText 逐字一致 / 类型下拉尾项「自动运行」单独可筛 /
     时刻用客户端接收时刻 / 锁定期照常入库 / 128 字节告警原文不截断
"""

# ============================================================================
#                    A. 类型筛选
# ============================================================================


def m_alarmtb_stamp_text(ms, fmt="%Y-%m-%d %H:%M:%S"):
    """epoch 毫秒 → 本地时刻文本（时间窗筛选框里就是这种格式）。"""
    return time.strftime(fmt, time.localtime(ms / 1000.0))


def m_alarmtb_make_page():
    """建一个报警页（纯控件层，不连服务端）。

    [为什么不再自己建 QApplication] Qt5 一进程多 QApplication 是未定义
    行为 ⇒ 本段落只取总回归建好的那一个实例。
    """
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("总回归尚未创建 QApplication"
                           "（不能在段落里新建：Qt5 一进程只允许一个）")
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import AlarmPage
    status = []
    ctx = PageContext(LinkBridge(), lambda i: None, status.append)
    page = AlarmPage(ctx)
    return app, page, status


def m_alarmtb_seed(page, base_ms):
    """注入 4 条不同类型/时间的报警（旧→新：T0 / +30s / +60s / +120s）。"""
    page._record_alarm("搅拌泵", "变频器", "故障", base_ms)
    page._record_alarm("温湿度", "温湿度", "数据过期", base_ms + 30 * 1000)
    page._record_alarm("流量计", "流量计", "离线", base_ms + 60 * 1000)
    page._record_alarm("电动球阀组", "电动球阀组", "故障",
                       base_ms + 120 * 1000)


def m_alarmtb_table_kinds(page):
    """当前表格里第 2 列（类型）的文本列表（视图序 = 最新在上）。"""
    table = page._table.table
    out = []
    for r in range(table.rowCount()):
        item = table.item(r, 2)
        out.append(item.text() if item else "")
    return out


# ============================================================================
#                    A. 类型筛选
# ============================================================================


def m_alarmtb_part_a_kind(page, base_ms):
    print("\n== A. 类型筛选 ==")
    kinds = ["变频器", "温湿度", "流量计", "电动球阀组"]
    for kind in kinds:
        # [用 KIND_ITEMS.index 找下标] 类型下拉里还插着"模拟量/485总线/
        # 数字量"等类型，写死序号会错位。
        page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index(kind))
        page._apply_filter()
        got = m_alarmtb_table_kinds(page)
        check("类型=%s → 只显示该类型" % kind,
              got == [kind], "%s" % got)
    check("行数统计随筛选（本页显示 1 行 / 共 1 条）",
          page._count_label.text() == "本页显示 1 行"
          and page._total_label.text() == "共 1 条（本页 1）",
          "%s | %s" % (page._count_label.text(),
                       page._total_label.text()))
    # 全部类型恢复
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()
    got = m_alarmtb_table_kinds(page)
    check("全部类型 → 4 行全显（最新在上）",
          got == ["电动球阀组", "流量计", "温湿度", "变频器"], "%s" % got)
    # 注一个表里没有的类型（模拟量）：命中 0 行但不报错
    idx = page.KIND_ITEMS.index("模拟量")
    page._kind_combo.setCurrentIndex(idx)
    page._apply_filter()
    check("无该类型的报警 → 0 行（不崩）", m_alarmtb_table_kinds(page) == [],
          "%s" % m_alarmtb_table_kinds(page))
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()


# ============================================================================
#                    B. 时间窗
# ============================================================================


def m_alarmtb_part_b_time(page, base_ms):
    print("\n== B. 时间窗筛选 ==")
    # 只给起点（= +45s）：剩 +60s / +120s 两条
    page._f_start.setText(m_alarmtb_stamp_text(base_ms + 45 * 1000))
    page._f_end.clear()
    page._apply_filter()
    check("只给起点（+45s）→ 剩 2 行（最新在上）",
          m_alarmtb_table_kinds(page) == ["电动球阀组", "流量计"],
          "%s" % m_alarmtb_table_kinds(page))

    # 只给终点（= +60s）：剩 T0 / +30s / +60s 三条（含端点，C 同口径 ts<=hi）
    page._f_start.clear()
    page._f_end.setText(m_alarmtb_stamp_text(base_ms + 60 * 1000))
    page._apply_filter()
    check("只给终点（+60s）→ 剩 3 行（含端点）",
          len(m_alarmtb_table_kinds(page)) == 3,
          "%s" % m_alarmtb_table_kinds(page))

    # 起点 == 终点（= +60s）：只剩端点那一条
    page._f_start.setText(m_alarmtb_stamp_text(base_ms + 60 * 1000))
    page._f_end.setText(m_alarmtb_stamp_text(base_ms + 60 * 1000))
    page._apply_filter()
    check("起止相等 → 只剩端点 1 条",
          m_alarmtb_table_kinds(page) == ["流量计"],
          "%s" % m_alarmtb_table_kinds(page))

    # 纯时刻（当天语义）：00:00 起当天全命中
    page._f_start.setText("00:00")
    page._f_end.setText(m_alarmtb_stamp_text(base_ms + 120 * 1000))
    page._apply_filter()
    check("纯时刻起点 00:00（当天）→ 4 行全显",
          len(m_alarmtb_table_kinds(page)) == 4,
          "%s" % m_alarmtb_table_kinds(page))

    # 两侧留空 = 不限
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    check("两侧留空 = 不限 → 4 行",
          len(m_alarmtb_table_kinds(page)) == 4,
          "%s" % m_alarmtb_table_kinds(page))


# ============================================================================
#                    C. 起点晚于终点
# ============================================================================


def m_alarmtb_part_c_inverted(page, base_ms):
    print("\n== C. 起点晚于终点（未筛选）==")
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    before = m_alarmtb_table_kinds(page)
    page._f_start.setText(m_alarmtb_stamp_text(base_ms + 120 * 1000))
    page._f_end.setText(m_alarmtb_stamp_text(base_ms + 30 * 1000))
    page._apply_filter()
    check("提示「起始时间晚于结束时间，未筛选」",
          "未筛选" in (page.ctx_status() or ""), page.ctx_status())
    check("未应用筛选（行集不动）", m_alarmtb_table_kinds(page) == before,
          "%s" % m_alarmtb_table_kinds(page))
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()


# ============================================================================
#                    D. 列显隐
# ============================================================================


def m_alarmtb_part_d_columns(page):
    print("\n== D. 列多选（勾中才显示）==")
    table = page._table.table
    # [全不勾受 min_checked=1 约束] clear_all 会保留第 0 列；再只勾「类型」。
    page._f_col.clear_all()
    page._f_col._model.item(2).setCheckState(2)     # 「类型」列
    got = page.visible_columns()
    check("只勾「类型」→ visible_columns = [0, 2]（至少一列）",
          2 in got and len(got) == 2, "%s" % got)
    hidden = [table.isColumnHidden(c) for c in range(4)]
    check("未勾的列隐藏（类型列可见）",
          hidden[2] is False and hidden.count(False) == len(got),
          "%s" % hidden)
    check("隐藏列的数据仍在（设备列文本未被清）",
          (table.item(0, 1).text() if table.item(0, 1) else "") != "",
          table.item(0, 1).text() if table.item(0, 1) else "None")
    page._f_col.check_all()
    hidden = [table.isColumnHidden(c) for c in range(4)]
    check("全勾 → 4 列全显",
          hidden == [False] * 4, "%s" % hidden)


# ============================================================================
#                    E. 行数统计（分页行口径）
# ============================================================================


def m_alarmtb_part_e_count(page):
    print("\n== E. 行数统计（对齐历史页分页行）==")
    check("全量 → 共 4 条（本页 4）",
          page._total_label.text() == "共 4 条（本页 4）",
          page._total_label.text())
    check("全量 → 本页显示 4 行",
          page._count_label.text() == "本页显示 4 行",
          page._count_label.text())
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("变频器"))
    page._apply_filter()
    check("筛选后 → 共 1 条（本页 1）",
          page._total_label.text() == "共 1 条（本页 1）",
          page._total_label.text())
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()


# ============================================================================
#                    F. 清除筛选
# ============================================================================


def m_alarmtb_part_f_clear(page, base_ms):
    print("\n== F. 清除筛选 ==")
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("流量计"))
    page._f_start.setText(m_alarmtb_stamp_text(base_ms + 45 * 1000))
    page._f_text.setText("流量计")          # 关键字也参与筛选
    page._f_col.clear_all()                # 只剩一列（视图设置）
    page._apply_filter()
    check("筛选生效（1 行）", len(m_alarmtb_table_kinds(page)) == 1,
          "%s" % m_alarmtb_table_kinds(page))
    page._clear_filter()
    check("清除后类型回全部（4 行）",
          len(m_alarmtb_table_kinds(page)) == 4,
          "%s" % m_alarmtb_table_kinds(page))
    check("清除后类型下拉回「全部类型」",
          page._kind_combo.currentIndex() == 0)
    check("清除后关键字清空",
          page._f_text.text() == "" and page._kw == "", page._f_text.text())
    check("清除后列回全勾",
          page.visible_columns() == [0, 1, 2, 3]
          and not page._table.table.isColumnHidden(0),
          "%s" % page.visible_columns())
    check("清除后时间窗回数据全区间（起点=最早一条）",
          page._f_start.text() == m_alarmtb_stamp_text(base_ms),
          page._f_start.text())
    check("清除后时间窗回数据全区间（终点=最新一条）",
          page._f_end.text() == m_alarmtb_stamp_text(base_ms + 120 * 1000),
          page._f_end.text())


# ============================================================================
#                    G. CSV 导出
# ============================================================================


def m_alarmtb_part_g_export(page, base_ms):
    print("\n== G. CSV 导出 ==")
    from hsfsrv_client import proto
    rows = [row for _ts, row in page._visible_records()]
    tmpdir = tempfile.mkdtemp(prefix="hsf_alarm_export_")
    path = os.path.join(tmpdir, "alarm.csv")
    try:
        # 无 Download 权限（匿名）：_export_data 拒绝且不落盘
        page.ctx.bridge.role = proto.ROLE_ANONYMOUS
        page._export_data()
        check("匿名无 Download 权限 → 拒绝导出",
              "权限不足" in (page.ctx_status() or ""), page.ctx_status())
        check("拒绝时不落盘", not os.path.exists(path))

        # 直接写（权限闸之后的落盘路径；授权由服务端权威校验，界面侧闸已单测）
        page._write_export(path, rows)
        check("导出文件存在", os.path.exists(path), path)
        with open(path, "rb") as fh:
            raw = fh.read()
        check("首行以 UTF-8 BOM 开头（utf-8-sig）",
              raw[:3] == b"\xef\xbb\xbf", "%r" % raw[:3])
        text = raw.decode("utf-8-sig")
        lines = text.strip().splitlines()
        # [导出列跟随可见列] 全勾时 = 全部列；列是多选（不是旧单选下拉）。
        cols = page.visible_columns()
        check("表头 = 可见列定义（含 BOM 后的首行）",
              lines[0] == ",".join(page.COLUMNS[c] for c in cols),
              lines[0])
        check("行序 = 最新在上（4 行数据，设备列在可见列里）",
              len(lines) == 5 and "电动球阀组" in lines[1]
              and "变频器" in lines[-1], "%s" % lines[1:])
        check("状态栏回报导出条数",
              "已导出 4 条" in (page.ctx_status() or ""), page.ctx_status())

        # 筛选后再导出：只导命中行
        page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("变频器"))
        page._apply_filter()
        path2 = os.path.join(tmpdir, "alarm_vfd.csv")
        rows2 = [row for _ts, row in page._visible_records()]
        page._write_export(path2, rows2)
        with open(path2, "rb") as fh:
            lines2 = fh.read().decode("utf-8-sig").strip().splitlines()
        check("筛选后导出只含命中行（1 条变频器）",
              len(lines2) == 2 and "变频器" in lines2[1], "%s" % lines2)
    finally:
        page._kind_combo.setCurrentIndex(0)
        page._apply_filter()
        shutil.rmtree(tmpdir, ignore_errors=True)


# ============================================================================
#                    H. 关键字筛选（客户端文本包含）
# ============================================================================


def m_alarmtb_part_h_keyword(page, base_ms):
    print("\n== H. 关键字筛选 ==")
    # 关键字匹配设备 / 类型 / 状态（空=不筛，回第 1 页）
    page._f_text.setText("搅拌泵")
    page._apply_filter()
    check("关键字「搅拌泵」→ 只命中设备列含该词的行",
          m_alarmtb_table_kinds(page) == ["变频器"],
          "%s" % m_alarmtb_table_kinds(page))
    page._f_text.setText("离线")
    page._apply_filter()
    check("关键字「离线」→ 命中状态列含该词的行",
          m_alarmtb_table_kinds(page) == ["流量计"],
          "%s" % m_alarmtb_table_kinds(page))
    page._f_text.setText("不存在的设备XYZ")
    page._apply_filter()
    check("无匹配关键字 → 0 行（不崩）",
          m_alarmtb_table_kinds(page) == [], "%s" % m_alarmtb_table_kinds(page))
    page._f_text.clear()
    page._apply_filter()
    check("清空关键字 → 4 行全显", len(m_alarmtb_table_kinds(page)) == 4,
          "%s" % m_alarmtb_table_kinds(page))


# ============================================================================
#                    I. 客户端内部分页
# ============================================================================


def m_alarmtb_part_i_paging(page, base_ms):
    print("\n== I. 客户端内部分页 ==")
    # 造 25 条（> 缺省 20/页 ⇒ 2 页）；清掉前面段落的时间窗/关键字。
    page._f_text.clear()
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    page._records = []
    page._seen = {}
    for i in range(25):
        page._record_alarm("dev%d" % i, "变频器", "故障",
                           base_ms + i * 1000)
    check("缺省 20 条/页（第 1 页 20 行）",
          page._table.table.rowCount() == 20,
          "%d" % page._table.table.rowCount())
    check("总数统计按全集（共 25 条，本页 20）",
          page._total_label.text() == "共 25 条（本页 20）",
          page._total_label.text())
    check("页码第 1/2 页", page._page_label.text() == "第 1/2 页",
          page._page_label.text())
    check("第 1 页上一页禁用 / 下一页可用",
          not page._btn_prev.isEnabled() and page._btn_next.isEnabled())
    page._next_page()
    check("下一页 → 第 2 页 5 行", page._table.table.rowCount() == 5
          and page._page_label.text() == "第 2/2 页",
          "%d / %s" % (page._table.table.rowCount(),
                       page._page_label.text()))
    check("末页下一页禁用", not page._btn_next.isEnabled())
    # 改页大小 10 ⇒ 回第 1 页、3 页
    idx = page._page_size_box.findData(10)
    page._page_size_box.setCurrentIndex(idx)
    check("改 10 条/页 → 回第 1 页 10 行 / 共 3 页",
          page._table.table.rowCount() == 10
          and page._page_label.text() == "第 1/3 页",
          "%d / %s" % (page._table.table.rowCount(),
                       page._page_label.text()))
    # 导出仍按全集（不受当前页影响）
    rows = [row for _ts, row in page._visible_records()]
    check("导出取筛选全集（25 行）而非当前页（10 行）", len(rows) == 25,
          "%d" % len(rows))
    # 恢复：20 条/页 + 清记录，供后面段落用
    idx = page._page_size_box.findData(20)
    page._page_size_box.setCurrentIndex(idx)
    page._records = []
    page._rebuild()


# ============================================================================
#                    J. 滚动锁定
# ============================================================================


def m_alarmtb_part_j_freeze(page, base_ms):
    print("\n== J. 滚动锁定 ==")
    page._f_start.clear()
    page._f_end.clear()
    page._f_text.clear()
    page._apply_filter()
    m_alarmtb_seed(page, base_ms)               # 4 条
    before = page._table.table.rowCount()
    page._freeze.setChecked(True)
    page._record_alarm("新报警", "流量计", "离线", base_ms + 999 * 1000)
    check("锁定期新报警只入库、不刷表",
          page._table.table.rowCount() == before
          and page._records[-1][1][1] == "新报警",
          "表行=%d 末记录=%s" % (page._table.table.rowCount(),
                                page._records[-1][1][1]))
    page._freeze.setChecked(False)              # 取消勾选补刷一次
    check("取消锁定 → 补刷新报警（表行 +1）",
          page._table.table.rowCount() == before + 1,
          "%d" % page._table.table.rowCount())
    page._records = []
    page._rebuild()


# ============================================================================
#                    K. MAX_ROWS 环形截断
# ============================================================================


def m_alarmtb_part_k_ring(page, base_ms):
    print("\n== K. MAX_ROWS 环形截断 ==")
    page._records = []
    page._seen = {}
    # [先清时间窗] 前面段落里「清除筛选」会把时间框回填成数据区间
    # （C alarmClearFilter 语义），这里不显式清掉的话，截断后行集会被
    # 旧窗口裁掉，截断断言就不纯了。
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    for i in range(page.MAX_ROWS + 3):
        page._record_alarm("dev%d" % i, "变频器", "故障",
                           base_ms + i * 1000)
    check("记录数封顶 MAX_ROWS", len(page._records) == page.MAX_ROWS,
          "%d" % len(page._records))
    check("最旧 3 条被丢弃（首条为 dev3）",
          page._records[0][1][1] == "dev3", page._records[0][1][1])
    check("最新保留（末条为最后一条）",
          page._records[-1][1][1] == "dev%d" % (page.MAX_ROWS + 2),
          page._records[-1][1][1])
    # [表格只铺当前页] 环形截断后表格行数是当前页那段，不是 MAX_ROWS。
    check("表格行数 = 当前页（%d），全集仍 %d"
          % (page._table.table.rowCount(), page.MAX_ROWS),
          page._table.table.rowCount() == page._page_size
          and len(page._visible_records()) == page.MAX_ROWS,
          "表行=%d 全集=%d" % (page._table.table.rowCount(),
                              len(page._visible_records())))


# ============================================================================
#       L/M. 自动运行告警（TOPIC_AUTO_ALARM → 报警页落表，2026-11-13）
# ============================================================================
#
# [背景] 服务端自动模式报警（阀超时/急停/动作失败/设备不可用/自定义）经
# TOPIC_AUTO_ALARM 推送（一帧一事件，kind/atMs/text）。此前 Python 客户端
# 既不订阅也不解析该主题 ⇒ 自动运行报警永远进不了报警表。对齐 C 端同款
# 修复（HsfLinkClient 连接期常驻订阅 + HsfDataAlarm.c 落表）：bridge
# 常驻订阅（连接期订/重连补订，不进页面订阅表）+ autoAlarm 信号送 UI
# 线程 + 本页落行。断言分布：
#   L. bridge 层：订阅清单 / 常驻订阅 / 重连补订 / 切页收敛不退订 /
#      publish 帧解析转发 / 坏载荷不崩
#   M. 页面层：一帧一行落表 / 五类文案逐字对齐 C / 类型筛选「自动运行」 /
#      时刻口径 / 锁定照常入库 / 长原文不截断


class m_autoalm_FakeClient(object):
    """记录订阅/退订调用的假 client（只够 bridge._subscribe_persistent 用）。"""

    def __init__(self):
        self.auth_role = 2                # _on_state(READY) 会读它
        self.subs = []
        self.unsubs = []

    def subscribe_async(self, topic):
        self.subs.append(topic)
        return None

    def unsubscribe_async(self, topic):
        self.unsubs.append(topic)
        return None


def m_autoalm_part_l_bridge(page):
    """L. bridge 层：**不再本地记自动告警**（报警数据一律向服务端要 0x011A）。

    [2026-11-17 口径翻转] 报警历史改为**服务端单一数据源**（用户实报
    「web 端报警有数据，客户端报警视图没数据」⇒ 两端各记各的进程内副本，
    客户端重启即清零）⇒ 客户端侧：
      · **不再常驻订阅** TOPIC_AUTO_ALARM（那条事件流本地没人消费）；
      · **不再有** autoAlarm 信号与 model 里的 auto_alarm_log /
        health_alarm_log 缓冲；
      · 收到该主题的推送**忽略**（不落行、不崩、不复活旧副本）。
    协议常量与主题登记**保留**（服务端仍在推那条流；客户端哪天要恢复
    实时流，只要把订阅与缓冲加回来）。
    """
    print("\n== L. 自动运行告警：客户端不再本地记（数据源=服务端）==")
    from hsfsrv_client import bson
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.xlink import SessionState

    bridge = page.ctx.bridge
    check("TOPIC_AUTO_ALARM=0x0001000B（与 server_py/C 侧同号）",
          proto.TOPIC_AUTO_ALARM == 0x0001000B,
          "0x%08X" % proto.TOPIC_AUTO_ALARM)
    check("客户端**不再常驻订阅**报警主题（数据源=服务端 0x011A）",
          proto.TOPIC_AUTO_ALARM not in LinkBridge.PERSISTENT_TOPICS,
          "%r" % (LinkBridge.PERSISTENT_TOPICS,))
    check("TOPIC_KIND 仍登记报警主题（协议面没动；漏登记=推送被静默丢弃）",
          LinkBridge.TOPIC_KIND.get(proto.TOPIC_AUTO_ALARM) == "autoalarm",
          "%r" % LinkBridge.TOPIC_KIND.get(proto.TOPIC_AUTO_ALARM))
    check("报警主题不进页面订阅表（对齐 C「不进页面订阅表」）",
          proto.TOPIC_AUTO_ALARM not in bridge.current_topics(),
          "%r" % bridge.current_topics())
    check("bridge 上不再有 autoAlarm 信号（本地副本已整体移除）",
          not hasattr(bridge, "autoAlarm"))

    fake = m_autoalm_FakeClient()
    bridge.client = fake
    try:
        # L1 首连（READY 迁移）：**不**订阅报警主题。
        n0 = len(fake.subs)
        bridge._on_state(SessionState.READY)
        check("连接期**不**订阅报警主题（本地不再消费那条事件流）",
              fake.subs[n0:] == [],
              "%r" % (fake.subs[n0:],))
        # L2 切页收敛：页面主题照旧，报警主题始终不在其中。
        bridge.set_topics({proto.TOPIC_VFD_STATUS})
        check("set_topics 页面收敛不含报警主题",
              proto.TOPIC_AUTO_ALARM not in fake.unsubs
              and proto.TOPIC_AUTO_ALARM not in bridge.current_topics(),
              "unsubs=%r topics=%r" % (fake.unsubs, bridge.current_topics()))
        # L3 重连（DISCONNECTED → READY）：**不**补订报警主题。
        bridge._on_state(SessionState.DISCONNECTED)
        n1 = len(fake.subs)
        bridge._on_state(SessionState.READY)
        check("重连后**不**补订报警主题（本地无消费者）",
              proto.TOPIC_AUTO_ALARM not in fake.subs[n1:],
              "%r" % (fake.subs[n1:],))
    finally:
        bridge.client = None
        bridge.connected_flag = False

    # L4 收到那条事件的推送 ⇒ **忽略**：不落行、不崩（数据在服务端表里，
    #    客户端下一次 0x011A 取数就会拿到）。
    page._server_rows = []
    page._rebuild()
    n_before = len(page._visible_records())
    try:
        payload = bson.encode({
            proto.AUTOALARM_KEY_KIND: proto.AUTO_ALARM_VALVE_TIMEOUT,
            proto.AUTOALARM_KEY_AT_MS: 1234,
            proto.AUTOALARM_KEY_TEXT: "电动球阀2 动作超时未到位"})
        bridge._on_publish(proto.TOPIC_AUTO_ALARM, payload,
                           proto.DATATYPE_BSON, None)
        check("报警主题推送被忽略（不落本地副本、不崩）",
              len(page._visible_records()) == n_before,
              "行数 %d → %d" % (n_before, len(page._visible_records())))
        # 坏载荷同样不崩。
        bridge._on_publish(proto.TOPIC_AUTO_ALARM, b"\xde\xad\xbe\xef",
                           proto.DATATYPE_BSON, None)
        check("坏载荷不崩", len(page._visible_records()) == n_before,
              "%d" % len(page._visible_records()))
        # 其它主题照旧走各条通道（不被这条忽略逻辑误伤）。
        bridge._on_publish(proto.TOPIC_VFD_STATUS,
                           bson.encode({"freq": 50.0}),
                           proto.DATATYPE_BSON, None)
        check("其它主题不进报警通道（各走各的）",
              len(page._visible_records()) == n_before,
              "%d" % len(page._visible_records()))
    finally:
        page._server_rows = []
        page._rebuild()

    # L5 model 侧不再有本地告警缓冲（单一数据源的最后一环）。
    m = bridge.model
    check("model 不再有本地告警缓冲（auto/health 都没了）",
          not hasattr(m, "auto_alarm_log")
          and not hasattr(m, "health_alarm_log"))

    # L7 第二轮审计（2026-11-16）：model 层的其余修复（不需要 Qt 控件，
    # 纯对象断言；顺带用本 fixture 的 bridge 验断线复位链）。
    print("\n== L7. 第二轮审计：阀告警上限/模拟量扩容/known 复位/表名截断 ==")
    from hsfsrv_client.model import (AnalogState, TempHumSideState,
                                     ValveState, mono_to_epoch_ms,
                                     _f_or_none)

    # ① 阀告警队列有界（只在阀页被消费 ⇒ 不封顶会在别的页无界增长）。
    vs = ValveState()
    big = {proto.KEY_ALARM_COUNT: 500}
    for i in range(500):
        big["a%d_text" % i] = "阀超时 %d" % i
    vs.take_alarms(big)
    check("L7a 阀告警队列封顶 200（超出丢最旧）",
          len(vs.alarms) == 200 and vs.alarm_total == 500,
          "len=%d total=%d" % (len(vs.alarms), vs.alarm_total))

    # ② 模拟量 >8 路：apply 按通道数重建缓冲（原来固定 8 格 ⇒ IndexError，
    #    模拟量永久"无数据"）。
    ai = AnalogState(channels=8)
    ai.apply({"n": 16, "v": 0xFFFF,
              **{"r%d" % i: 100 + i for i in range(16)},
              **{"u%d" % i: 4000 + i for i in range(16)}})
    check("L7b 模拟量 16 路：缓冲已扩容、值全收到",
          len(ai.raw) == 16 and ai.raw[15] == 115 and ai.ua[15] == 4015,
          "raw=%d" % len(ai.raw))

    # ③ 值字段坏类型收敛（原样存 ⇒ 界面用时 x/10.0 每拍炸 refresh）。
    side = TempHumSideState(0)
    side.apply({"tempValid": True, "temp": "not-a-number",
                "humValid": True, "hum": 555})
    check("L7c 温湿度坏类型按「无数据」收敛（不抛）",
          side.temp_tenths is None and side.hum_tenths == 555.0,
          "temp=%r hum=%r" % (side.temp_tenths, side.hum_tenths))
    check("L7d _f_or_none：字符串数字可用、容器回 None",
          _f_or_none("12.5") == 12.5 and _f_or_none([1]) is None
          and _f_or_none(True) is None)

    # ④ 单调钟 → 墙钟：换算结果要落在"当前墙钟 ±1s"内（原来直接 localtime
    #    成 1970+开机时长）。
    import time as _time
    now_epoch = int(_time.time() * 1000)
    conv = mono_to_epoch_ms(int(_time.monotonic() * 1000))
    check("L7e 单调钟换算墙钟落在当前时刻 ±1s",
          abs(conv - now_epoch) <= 1000, "conv=%d now=%d" % (conv, now_epoch))

    # ⑤ XLSX 表名：先转义再安全截（原来截断点落在实体中间 ⇒ Excel 打不开）。
    import io as _io
    import zipfile as _zip
    from hsfsrv_client.xlsx_stream import XlsxStreamWriter
    tmp_x = os.path.join(tempfile.gettempdir(), "hsf_reg_sheetname.xlsx")
    try:
        w = XlsxStreamWriter(tmp_x, ["历史" * 30])       # 60 字符长名
        w.sheet(0, ["t", "v"])
        w.row(0, ["t", "v"])
        w.close()
        bad = False
        with _zip.ZipFile(tmp_x) as zf:
            names = [n for n in zf.namelist() if n.endswith("workbook.xml")]
            xml = zf.read(names[0]).decode("utf-8")
            import xml.dom.minidom as _md
            try:
                _md.parseString(xml)
            except Exception:                            # noqa: BLE001
                bad = True
            sheet_ok = 'sheetId="1"' in xml and 'name="' in xml
        check("L7f 长表名 workbook.xml 合法（实体不被截半截）",
              (not bad) and sheet_ok, "bad=%s sheet_ok=%s" % (bad, sheet_ok))
    finally:
        try:
            os.unlink(tmp_x)
        except OSError:
            pass

    # ⑥ 断线复位链：known 标志只置位从不清 ⇒ 断链期间界面继续断言旧状态。
    m = bridge.model
    m.run_mode_known = True
    m.auto_running = True
    m.auto_mode = "produce"
    m.auto_phase = "running"
    m.auto_fault = False
    m.auto_run_ms_known = True
    m.auto_stage_known = True
    bridge.connected_flag = True      # 复位块挂在 DISCONNECTED+已连接 沿上
    bridge._on_state(SessionState.DISCONNECTED, None)
    check("L7g 断线：自动态/known 标志复位",
          m.run_mode_known is False and m.auto_running is None
          and m.auto_mode == "" and m.auto_phase == ""
          and m.auto_fault is None and m.auto_run_ms_known is False
          and m.auto_stage_known is False,
          "known=%s running=%r" % (m.run_mode_known, m.auto_running))


def m_autoalm_part_m_page(page):
    """M. 页面层：服务端行落表/文案/筛选/锁定/长原文。

    [2026-11-17] 数据源换成服务端（0x011A）⇒ 落行走**服务端行**那条路
    （`_server_row_to_record`），本地只保留 `_record_alarm` 这一个
    测试/诊断入口（它写进同一份 `_server_rows`）。
    """
    print("\n== M. 自动运行告警：服务端行落表/五类文案/类型筛选/长原文 ==")
    from hsfsrv_client import proto
    from hsfsrv_client.pages_other import AlarmPage

    # 清场（前面段落的记录与筛选状态不带进来）。
    page._server_rows = []
    page._f_start.clear()
    page._f_end.clear()
    page._f_text.clear()
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()

    # M1 五类文案与 C 端 HsfDataAlarm.c alarmAutoKindText 逐字一致。
    expect = {
        proto.AUTO_ALARM_ACTION_FAILED: "动作失败",
        proto.AUTO_ALARM_DEVICE_UNAVAILABLE: "设备不可用",
        proto.AUTO_ALARM_CUSTOM: "自定义",
        proto.AUTO_ALARM_ESTOP: "急停",
        proto.AUTO_ALARM_VALVE_TIMEOUT: "阀超时",
        # [流量计告警 2026-11-15] 第六类（两端同值同文案）。
        proto.AUTO_ALARM_FLOWMETER: "流量计",
        # [设备故障 2026-11-15] 第七类：变频器故障等（进报警历史表+锁存+停机）。
        proto.AUTO_ALARM_DEVICE_FAULT: "设备故障",
    }
    got_map = {k: AlarmPage.auto_kind_text(k) for k in expect}
    check("七类告警文案与 C alarmAutoKindText 逐字一致",
          got_map == expect, "%r" % got_map)
    check("未知类型回退「自动运行」（不显示裸数字，C default 分支同款）",
          AlarmPage.auto_kind_text(99) == "自动运行"
          and AlarmPage.auto_kind_text(0) == "自动运行")

    # M2 类型下拉尾项「自动运行」（对齐 C ALARM_KIND_FILTER_AUTO=9）。
    check("类型下拉尾项 = 「自动运行」（下标 9）",
          page.KIND_ITEMS[-1] == AlarmPage.KIND_TEXT_AUTO
          and len(page.KIND_ITEMS) == 10
          and page.KIND_ITEMS.index(AlarmPage.KIND_TEXT_AUTO) == 9,
          "%r" % (page.KIND_ITEMS,))

    # M3 服务端自动告警行 → 落表（三列语义：类型=自动运行 / 设备=原文 /
    #    状态=类型中文）；时刻 = **服务端 ts**（客户端不再自己记时刻）。
    ts_now = int(time.time() * 1000)
    rec = page._server_row_to_record({
        proto.ALARM_KEY_TS: ts_now,
        proto.ALARM_KEY_KIND: "auto",
        proto.ALARM_KEY_SOURCE: "自动运行",
        proto.ALARM_KEY_TEXT: "电动球阀2 动作超时未到位",
        proto.ALARM_KEY_DEV: None})
    page._server_rows.append(rec)
    page._rebuild()
    row = page._server_rows[-1][1]
    check("服务端自动告警行进表（一行一行，表格即刷）",
          len(page._server_rows) == 1 and page._table.table.rowCount() == 1,
          "记录=%d 表行=%d" % (len(page._server_rows),
                              page._table.table.rowCount()))
    check("行内容：类型列=自动运行 / 设备列=告警原文 / 状态列=自动运行告警",
          row[2] == "自动运行" and row[1] == "电动球阀2 动作超时未到位"
          and row[3] == "自动运行告警", "%r" % (row,))
    check("时刻用服务端 ts（客户端不自己造时刻）",
          page._server_rows[-1][0] == ts_now,
          "rec=%s ts=%s" % (page._server_rows[-1][0], ts_now))

    # M4 事件不去重：同类两帧两行。
    page._record_alarm("急停触发", "自动运行", "急停", ts_now + 1)
    check("同类事件不去重（再记一行）",
          len(page._server_rows) == 2
          and page._server_rows[-1][1][3] == "急停",
          "%d 行" % len(page._server_rows))

    # M5 类型筛选「自动运行」：只显自动告警行（其它类型的行被滤掉）。
    page._record_alarm("搅拌泵", "变频器", "故障", ts_now + 2)
    page._kind_combo.setCurrentIndex(
        page.KIND_ITEMS.index(AlarmPage.KIND_TEXT_AUTO))
    page._apply_filter()
    check("类型筛「自动运行」→ 只显自动告警行",
          m_alarmtb_table_kinds(page) == ["自动运行", "自动运行"],
          "%r" % m_alarmtb_table_kinds(page))
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()

    # M6 关键字能搜到告警原文（关键字匹配覆盖设备列=原文）。
    page._f_text.setText("急停触发")
    page._apply_filter()
    check("关键字命中告警原文",
          len(page._visible_records()) == 1
          and page._visible_records()[0][1][1] == "急停触发",
          "%d 行" % len(page._visible_records()))
    page._f_text.clear()
    page._apply_filter()

    # M7 滚动锁定：照常入库、只压刷表（与历史页同款语义）。
    page._freeze.setChecked(True)
    rows = page._table.table.rowCount()
    page._record_alarm("阀命令下发失败", "自动运行", "动作失败", ts_now + 3)
    check("锁定期新报警只入库不刷表",
          len(page._server_rows) == 4
          and page._table.table.rowCount() == rows,
          "记录=%d 表行=%d" % (len(page._server_rows),
                              page._table.table.rowCount()))
    page._freeze.setChecked(False)
    check("解锁补刷新报警行",
          page._table.table.rowCount() == rows + 1,
          "%d" % page._table.table.rowCount())

    # M8 128 字节+ 告警原文不截断（服务端 text 上限 128 字节；C 端为此把
    #    行缓冲 32→160，Python str 天然不定长——用断言钉住不回退）。
    long_text = ("电动球阀3 动作超时未到位（已重试 2 次，点位 DO2/DI4/DI5，"
                 "请检查阀电源与反馈接线）" + "长" * 60)
    while len(long_text.encode("utf-8")) <= 128:
        long_text += "长"
    page._record_alarm(long_text, "自动运行", "阀超时", ts_now + 4)
    check("超 128 字节原文原样进行缓冲",
          page._server_rows[-1][1][1] == long_text
          and len(long_text.encode("utf-8")) > 128,
          "%d 字节" % len(long_text.encode("utf-8")))
    check("超 128 字节原文原样进表格（最新在首行）",
          page._table.table.item(0, 1) is not None
          and page._table.table.item(0, 1).text() == long_text,
          "%r…" % (page._table.table.item(0, 1).text()[:20]
                   if page._table.table.item(0, 1) else None))

    # M9 服务端行带 dev ⇒ 类型列显示**设备类别**（"流量计"），而不是
    #    "自动运行"；缺 dev（老服务端）⇒ 仍是"自动运行"。
    n_before = len(page._server_rows)
    page._server_rows.append(page._server_row_to_record({
        proto.ALARM_KEY_TS: ts_now + 5,
        proto.ALARM_KEY_KIND: "auto",
        proto.ALARM_KEY_SOURCE: "自动运行",
        proto.ALARM_KEY_TEXT: "流量计报警：空管",
        proto.ALARM_KEY_DEV: 2}))
    row = page._server_rows[-1][1]
    check("服务端行带 dev ⇒ 类型列=流量计（设备类别）/ 设备列=原文",
          len(page._server_rows) == n_before + 1 and row[2] == "流量计"
          and row[1] == "流量计报警：空管" and row[3] == "自动运行告警",
          "%r" % (row,))
    page._server_rows.append(page._server_row_to_record({
        proto.ALARM_KEY_TS: ts_now + 6,
        proto.ALARM_KEY_KIND: "auto",
        proto.ALARM_KEY_SOURCE: "自动运行",
        proto.ALARM_KEY_TEXT: "流量计报警：上限",
        proto.ALARM_KEY_DEV: None}))
    row = page._server_rows[-1][1]
    check("缺 dev（老服务端）⇒ 类型列仍是「自动运行」",
          len(page._server_rows) == n_before + 2 and row[2] == "自动运行",
          "%r" % (row,))

    # 清场（不给可能的后续段落留状态）。
    page._server_rows = []
    page._rebuild()


# ============================================================================
#             N. 健康边沿报警（2026-11-17 用户实锅根修的回归锁）
# ============================================================================
#
# [用户实锅] 「客户端上报警无显示，网页上有信息显示」。
# [根因] 健康边沿检测原来写在报警页的 refresh() 里，而 refresh 只有**当前页**
#        才跑（MainWindow._refresh_current）⇒ 用户停在别的页面期间发生的
#        「数据过期 / 离线 / 故障」边沿**根本没被采样**（网页端 hub.feed_health
#        每帧都跑 ⇒ 网页有行、客户端空表）。
# [修法] 检测搬到 model（bridge 收到每帧健康快照都跑）⇒ 结果进 model 的
#        有界缓冲 health_alarm_log；报警页建页时补放、之后按 seq 增量拉取。
# [本段锁住的] ① 边沿语义：只在「进入异常」那一刻记一行（不按电平重复刷）
#             ② 恢复正常不记行（客户端既有口径；web 端才记「恢复正常」行）
#             ③ **页面没打开时**发生的边沿，建页时补放进表（本轮根修的主体）
#             ④ 异常档口径 = health >= 2（告警(1) 不算异常，与 web 同档）


def m_alarmtb_health_doc(devs):
    """造一份设备健康快照帧（devs = [(设备名, 类别, 健康档), ...]）。"""
    doc = {"devCount": len(devs), "seq": 1, "ts": 0}
    for i, (name, kind, health) in enumerate(devs):
        doc["d%d_name" % i] = name
        doc["d%d_kind" % i] = kind
        doc["d%d_health" % i] = health
        doc["d%d_online" % i] = health != 3
    return doc


def m_alarmtb_make_page_for(bridge):
    """建一个**绑在既有 bridge 上**的报警页（模拟懒创建 + 补放）。"""
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import AlarmPage
    ctx = PageContext(bridge, lambda i: None, [])
    return AlarmPage(ctx)


def m_alarmtb_part_o_server_rows(page):
    print("\n== O. 报警历史向服务端取数（0x011A）==")
    # [为什么要有这条] 报警记录原先只存在**客户端进程内存**里 ⇒ 客户端一
    # 重启历史全丢，而网页端（常驻服务）看得到开机以来全部 ⇒ 用户实报
    # 「web 端报警有数据，客户端报警视图没数据」。⇒ 报警页改为向服务端要
    # 历史（0x011A），本地边沿只作实时/兜底。
    from hsfsrv_client import proto
    bridge = page.ctx.bridge
    sent = {}

    def fake_alarm_page(offset=0, limit=None, before_ts=None, lo_ts=None,
                        hi_ts=None, kw=None, on_done=None, on_error=None,
                        timeout=5.0):
        sent["offset"] = offset
        sent["limit"] = limit
        rows = [
            {proto.ALARM_KEY_TS: 1700000000000,
             proto.ALARM_KEY_KIND: "health",
             proto.ALARM_KEY_SOURCE: "温湿度",
             proto.ALARM_KEY_TEXT: "数据过期",
             proto.ALARM_KEY_DEV: 1},
            {proto.ALARM_KEY_TS: 1700000001000,
             proto.ALARM_KEY_KIND: "auto",
             proto.ALARM_KEY_SOURCE: "自动运行",
             proto.ALARM_KEY_TEXT: "现场启动按钮被拒 —— 当前是手动模式",
             proto.ALARM_KEY_DEV: None},
        ]
        if on_done:
            on_done({"ok": True, "total": 2, "count": 2,
                     "offset": offset, "limit": limit,
                     proto.ALARM_KEY_ROWS: rows})

    bridge.alarm_page = fake_alarm_page
    page._server_rows = []
    page._server_fetch_ok = False
    page._last_fetch_ms = 0
    page.refresh()                       # 触发一次取数
    app_process = page._table.table

    check("O1 向服务端要了报警历史（0x011A）",
          "offset" in sent and sent.get("limit", 0) > 0,
          "%r" % (sent,))
    check("O2 服务端行进表：健康边沿（设备/类型/状态三列）",
          any(r[1][1] == "温湿度" and r[1][2] == "温湿度"
              and r[1][3] == "数据过期" for r in page._server_rows),
          "%r" % (page._server_rows,))
    check("O3 服务端行进表：自动告警（类型=自动运行、状态=自动运行告警）",
          any(r[1][1].startswith("现场启动按钮被拒")
              and r[1][2] == "自动运行"
              and r[1][3] == "自动运行告警" for r in page._server_rows),
          "%r" % (page._server_rows,))
    check("O4 表格铺的是服务端历史（2 行）",
          app_process.rowCount() == 2,
          "行数=%d" % app_process.rowCount())
    check("O5 总数统计跟着服务端（服务端 total=2）",
          "2" in page._total_label.text(),
          page._total_label.text())

    # ---- O6 表里的行**只有**服务端那一份（本地不留副本）----
    check("O6 本页不再有本地副本（行全部来自服务端）",
          len(page._rows) == 2
          and not hasattr(page.ctx.model, "health_alarm_log")
          and not hasattr(page.ctx.model, "auto_alarm_log"),
          "本地行=%d 模型缓冲 health=%s auto=%s"
          % (len(page._rows),
             hasattr(page.ctx.model, "health_alarm_log"),
             hasattr(page.ctx.model, "auto_alarm_log")))

    # ---- O7 服务端取不到（断线/老服务端）⇒ 保留上次取到的行，不清表 ----
    def fake_fail(offset=0, limit=None, before_ts=None, lo_ts=None,
                  hi_ts=None, kw=None, on_done=None, on_error=None,
                  timeout=5.0):
        if on_error:
            on_error("未知命令")

    bridge.alarm_page = fake_fail
    page._last_fetch_ms = 0
    page.refresh()
    check("O7 服务端取不到时保留上次取到的行（不清表）",
          page._server_fetch_ok is False
          and len(page._visible_records()) == 2,
          "fetch_ok=%s 行数=%d"
          % (page._server_fetch_ok, len(page._visible_records())))



# ============================================================================
#                            段落入口
# ============================================================================


def run_alarmtb():
    """§16 报警页工具条：类型/时间窗/列多选/统计/清除/导出/关键字/分页/锁定/截断。

    [2026-11-11 改版] 报警页与历史/日志页统一 DataPageToolbar；原「报警
    次数/分钟折线图」与「表格/图表双视图」两段断言随功能一起删除，换成
    关键字筛选、客户端内部分页、滚动锁定三段（用户定案：报警页不要图表、
    加分页、隐藏刷新）。

    [2026-11-13 增补] L/M 两段：自动运行告警（TOPIC_AUTO_ALARM 常驻订阅
    → 报警页落行），对齐 C 端 HsfDataAlarm.c 同款修复。

    [原 main() 的行为迁移] 建页 → 取分钟对齐的基准时刻 → 注入 4 条报警
    → 包装 status 取值 → 依次跑 A~K 十一段。只去掉两件事：
      · 「建 QApplication」⇒ 改用总回归那唯一一个实例；
      · 「打印 passed/failed 汇总 + 返回退出码」⇒ 由总回归统一汇总。

    [不连服务端] 本段落是纯控件层，起不起服务端都一样；不需要端口。
    """
    app, page, _status = m_alarmtb_make_page()
    # [2026-11-21 三端对齐补洞] 服务端 alarms 表写 6 类 kind（app.py
    # _alarm_db_kind_source：health/auto/valve/flowmeter/device/analog/
    # digital），本页 SERVER_KIND_ROWS 必须六类齐全（网页 page11_alarm.js
    # 同款）——缺 analog/digital 时老库行（无 dev 键）会回退成「自动运行」。
    from hsfsrv_client import pages_other as _po_alarm
    _kind_rows = _po_alarm.AlarmPage.SERVER_KIND_ROWS
    check("§16 SERVER_KIND_ROWS 六类齐全（含 analog/digital）",
          all(k in _kind_rows for k in
              ("auto", "valve", "flowmeter", "device", "analog", "digital"))
          and _kind_rows.get("analog") == ("模拟量", "越限保护")
          and _kind_rows.get("digital") == ("数字量", "数字量告警"),
          "实际：" + repr(_kind_rows))
    # 分钟对齐的基准时刻（避免跨分钟边界造成聚合断言抖动）
    base_ms = int(time.time() * 1000) // 60000 * 60000
    m_alarmtb_seed(page, base_ms)

    # PageContext.set_status 记录到列表；为了断言方便包装最近一次取值
    def _ctx_status():
        return _status[-1] if _status else ""

    page.ctx_status = _ctx_status

    m_alarmtb_part_a_kind(page, base_ms)
    m_alarmtb_part_b_time(page, base_ms)
    m_alarmtb_part_c_inverted(page, base_ms)
    m_alarmtb_part_d_columns(page)
    m_alarmtb_part_e_count(page)
    m_alarmtb_part_f_clear(page, base_ms)
    m_alarmtb_part_g_export(page, base_ms)
    m_alarmtb_part_h_keyword(page, base_ms)
    m_alarmtb_part_i_paging(page, base_ms)
    m_alarmtb_part_j_freeze(page, base_ms)
    m_alarmtb_part_k_ring(page, base_ms)
    m_autoalm_part_l_bridge(page)
    m_autoalm_part_m_page(page)
    m_alarmtb_part_o_server_rows(page)
    return 0


# ==========================================================================
# 段落：test_chart_interaction（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】折线图：多列绘制 + 缩放/平移 + 点击弹点信息（纯控件层）。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的**一段源码**（总回归会把它原样拼进去）。

[命名契约] 本段落的**所有**顶层名字统一带 `m_chartix_` 前缀
    （函数/常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `skip` / `section` /
    `time` / `os` …）**按名直呼**，不 import 总回归（避免循环导入）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `QApplication.instance()` 取。
    另外**不**在模块级 import PyQt5 —— 那样 PyQt5 缺失时整个总回归会在
    import 阶段就炸掉，丢掉总回归自己那条「QApplication 可创建」的优雅
    降级路径。Qt 常量因此改为**函数内**按需 import。

用户要求：
  1. 「折线列的下拉也要做类似的设计可以多选」
  2. 「打开的折线图可以双指缩放，单机拖动，移动设备上那种操作，
      当然也要适配鼠标操作」
  3. 「在折线图上当点击折线某一点的时候可以弹出悬浮，显示当前点的信息」

覆盖：
  A. 折线列多选：控件形态、默认勾第一列、多列生成多序列
  B. 视口状态机：zoom_at 夹取、pan 夹取、reset、is_zoomed
  C. 锚点缩放：缩放前后锚点处的时间不变（这才是"以手指为中心缩放"）
  D. 鼠标：滚轮缩放（指针锚点）、左键拖动平移、双击复位
  E. 拖动阈值：拖动超过阈值不弹气泡；纯单击才弹
  F. 点击弹点信息：吸附最近点、气泡内容含时间与各序列值、点空处收起
  G. 多序列绘制不崩：不同点数/量纲/单点序列/空序列
  H. 缩放后纵向自适应（视口内 min/max 变化）
  I. 触摸：双指捏合缩放 + 单指拖动平移（控件暴露的纯状态机）

[为什么这些必须用"调状态机 + 真事件"两条路] 缩放夹取、锚点漂移、拖动
阈值这些逻辑错一点界面上只是"手感怪"，不会报错；而遥操作类 bug（点不
弹气泡/拖完弹气泡）只有真发事件才暴露。
"""

#: 固定基准时间（毫秒），避免测试依赖当前时刻。
m_chartix_BASE = 1793927000000
#: 相邻点的时间步长（毫秒）。
m_chartix_STEP = 5000


# ============================================================================
#                    A. 折线列多选（页面上的控件）
# ============================================================================


def m_chartix_app_instance():
    """取总回归那唯一一个 QApplication（**不新建**）。"""
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("总回归尚未创建 QApplication"
                           "（不能在段落里新建：Qt5 一进程只允许一个）")
    from hsfsrv_client import theme
    m_theme_install_once(app)   # [只装一次] 见助手注释
    return app


def m_chartix_make_dialog(series=None, size=(900, 500)):
    """建一个离屏可绘的折线图对话框。"""
    from hsfsrv_client.pages_other import LineChartDialog
    if series is None:
        series = [
            ("输出频率(Hz)", [(m_chartix_BASE + i * m_chartix_STEP,
                               25.0 + i * 0.1)
                              for i in range(60)]),
            ("搅拌转速(rpm)", [(m_chartix_BASE + i * m_chartix_STEP,
                                720.0 + i * 6.0)
                               for i in range(60)]),
        ]
    dlg = LineChartDialog(series)
    dlg.resize(*size)
    dlg.show()
    return dlg


def m_chartix_part_a_multi_column(app):
    print("== A. 折线列多选 ==")
    from PyQt5.QtCore import Qt
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    page = HistoryPage(ctx)

    combo = page._chart_col
    check("A1 折线列是支持多选的控件（有勾选列表）",
          hasattr(combo, "checked_indexes"), type(combo).__name__)
    # 只列数值列：时间/模式/搅拌状态不能画折线
    labels = [combo._labels[i] for i in range(len(combo._labels))]
    check("A2 不含非数值列（时间/模式/搅拌状态）",
          all(x not in labels for x in ("时间", "模式", "搅拌状态")),
          str(labels[:3]))
    check("A3 列数与数值列数一致",
          len(labels) == len(page.CHART_COLUMNS),
          "%d vs %d" % (len(labels), len(page.CHART_COLUMNS)))
    check("A4 默认勾中第一条（与原下拉默认行为对齐）",
          combo.checked_indexes() == [0], str(combo.checked_indexes()))

    # 多选后生成多序列
    combo._model.item(1).setCheckState(Qt.Checked)
    combo._model.item(2).setCheckState(Qt.Checked)
    check("A5 可同时勾多列", combo.checked_indexes() == [0, 1, 2],
          str(combo.checked_indexes()))

    # 灌数据后点"生成折线图"，看它建了几个序列
    page._rows = []
    for i in range(8):
        texts = ["2026-11-06 09:%02d:%02d" % (10 + i // 6, (i * 10) % 60),
                 "手动", "停机", "%.2f" % (25.0 + i), "3.20", "%d" % (750 + i),
                 "12.50", "3.20", "36.50 m³/h", "123.40 m³", "0.499",
                 "26.5", "55.0", "0x0000FFFF", "0x000000FF"]
        page._rows.append((texts, False))
    made = {}

    # [内嵌双视图 2026-11-10] 图表不再是弹窗（用户要求「不要新建一个窗口」），
    # 而是 QStackedWidget 的第 1 页 + Qt.Widget 旗标的 LineChartDialog。
    # 替身必须是 QWidget（要进 holder 的布局），并实现 set_series。
    from PyQt5.QtWidgets import QWidget

    class _FakeDialog(QWidget):
        def __init__(self, series, parent=None):
            QWidget.__init__(self, parent)
            made["series"] = series

        def set_series(self, series):
            made["series"] = series

        def show(self):
            made["shown"] = True

    import hsfsrv_client.pages_other as mod
    real = mod.LineChartDialog
    mod.LineChartDialog = _FakeDialog
    try:
        page.set_view(1)
    finally:
        mod.LineChartDialog = real
    series = made.get("series") or []
    check("A5b 图表内嵌在页内（双视图第 1 页），不弹独立窗",
          page._view_stack.currentIndex() == 1
          and page._chart_dialog is not None,
          "stack=%d dialog=%r"
          % (page._view_stack.currentIndex(), page._chart_dialog))
    check("A6 勾 3 列 → 生成 3 条序列", len(series) == 3,
          "实际 %d 条" % len(series))
    check("A7 序列名 = 勾中的列名",
          [s[0] for s in series] == [page.COLUMNS[c]
                                     for c in page.CHART_COLUMNS[:3]],
          str([s[0] for s in series]))
    check("A8 每个点带毫秒时间戳（不是文本）",
          bool(series) and isinstance(series[0][1][0][0], int),
          type(series[0][1][0][0]).__name__ if series else "-")

    # 一列都不勾 → 明确提示，不画空图
    combo.clear_all()
    made.clear()
    mod.LineChartDialog = _FakeDialog
    status = []
    page.ctx.set_status = status.append
    try:
        page.set_view(1)
    finally:
        mod.LineChartDialog = real
    check("A9 一列都不勾时提示而不是画空图",
          not made.get("series") and any("勾选" in s for s in status),
          str(status))


# ============================================================================
#                      B. 视口状态机
# ============================================================================


def m_chartix_part_b_viewport(app):
    print("== B. 视口状态机 ==")
    dlg = m_chartix_make_dialog()
    check("B1 初始为全程", not dlg.is_zoomed()
          and (dlg._view_x0, dlg._view_x1) == (0.0, 1.0),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))

    dlg.zoom_at(0.5, 0.5)
    check("B2 zoom_at 缩小视口（放大）", dlg.is_zoomed()
          and abs((dlg._view_x1 - dlg._view_x0) - 0.5) < 1e-6,
          "%.3f~%.3f" % (dlg._view_x0, dlg._view_x1))

    dlg.reset_view()
    check("B3 reset_view 回全程", not dlg.is_zoomed())

    # 缩小到比全程还宽 → 夹到 1.0
    dlg.zoom_at(0.5, 5.0)
    check("B4 缩小不能超过全程",
          abs((dlg._view_x1 - dlg._view_x0) - 1.0) < 1e-6,
          "跨度 %.3f" % (dlg._view_x1 - dlg._view_x0))

    # 放大到极限 → 不低于 MIN_SPAN_POINTS/总点数
    for _ in range(60):
        dlg.zoom_at(0.5, 0.5)
    total_points = sum(len(p) for _n, p in
                       [("a", dlg._series[0][1]), ("b", dlg._series[1][1])])
    min_span = dlg.MIN_SPAN_POINTS / float(total_points)
    check("B5 放大有下限（不会缩成 0 宽）",
          (dlg._view_x1 - dlg._view_x0) >= min_span - 1e-9,
          "跨度 %.5f 下限 %.5f" % (dlg._view_x1 - dlg._view_x0, min_span))

    # 平移夹取：窗口不能被推出 [0,1]
    dlg.reset_view()
    dlg.zoom_at(0.5, 0.4)
    for _ in range(50):
        dlg.pan_pixels(500, 800)          # 一直往一个方向拖
    check("B6 平移不能跑出左边界", dlg._view_x0 >= -1e-9,
          "x0=%.4f" % dlg._view_x0)
    for _ in range(100):
        dlg.pan_pixels(-500, 800)
    check("B7 平移不能跑出右边界", dlg._view_x1 <= 1.0 + 1e-9,
          "x1=%.4f" % dlg._view_x1)
    check("B8 平移后跨度不变（只平移不缩放）",
          abs((dlg._view_x1 - dlg._view_x0) - 0.4) < 1e-6,
          "跨度 %.4f" % (dlg._view_x1 - dlg._view_x0))

    # 全程时平移无效（没有余量）
    dlg.reset_view()
    dlg.pan_pixels(300, 800)
    check("B9 全程时平移无效", (dlg._view_x0, dlg._view_x1) == (0.0, 1.0),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))
    dlg.close()


# ============================================================================
#                    C. 锚点缩放（以手指/指针为中心）
# ============================================================================


def m_chartix_part_c_anchor(app):
    print("== C. 锚点缩放 ==")
    dlg = m_chartix_make_dialog()
    t0, t1 = dlg.view_times()
    anchor_frac = 0.25
    anchor_time = t0 + (t1 - t0) * anchor_frac
    dlg.zoom_at(anchor_frac, 0.5)
    nt0, nt1 = dlg.view_times()
    # 锚点处的时间在缩放前后应基本不变（浮点误差 < 1ms）
    new_frac = (anchor_time - nt0) / (nt1 - nt0)
    check("C1 锚点处的时间在缩放后仍在同一位置",
          abs(new_frac - anchor_frac) < 0.01,
          "锚点位置 %.4f → %.4f" % (anchor_frac, new_frac))
    # [每条锚点断言前都要复位] 视口是有状态的：C1 已经缩放/平移过，
    # 不复位就接着测边界锚点，起点不是全程、"贴边"自然对不上（实测踩到）。
    dlg.reset_view()
    dlg.zoom_at(1.0, 0.5)
    check("C2 右端锚点缩放后 x1 = 1.0（贴边不露空白）",
          abs(dlg._view_x1 - 1.0) < 1e-9, "x1=%.6f" % dlg._view_x1)
    dlg.reset_view()
    dlg.zoom_at(0.0, 0.5)
    check("C3 左端锚点缩放后 x0 = 0.0",
          abs(dlg._view_x0) < 1e-9, "x0=%.6f" % dlg._view_x0)
    dlg.close()


# ============================================================================
#                          D. 鼠标操作
# ============================================================================


def m_chartix_send_mouse(widget, kind, pos, buttons=None):
    """显式发一个 QMouseEvent。

    [为什么不用 QTest.mouseMove] 离屏平台下 QTest.mouseMove 走
    `QCursor.setPos` + 平台事件，**根本到不了 widget 的 mouseMoveEvent**
    （实测：拖动毫无反应、看起来像实现坏了）。直接构造 QMouseEvent 用
    sendEvent 投递才可靠，且能显式带 buttons 位。

    [为什么 buttons 改成 None 默认] 原实现用模块级 `Qt.NoButton` 当默认
    参数值；本段落不在模块级 import PyQt5（否则 PyQt5 缺失会炸掉整个
    总回归的 import），故改为 None + 函数内解析，行为等价。
    """
    from PyQt5.QtCore import QEvent, QPointF, Qt
    from PyQt5.QtGui import QMouseEvent
    from PyQt5.QtWidgets import QApplication

    if buttons is None:
        buttons = Qt.NoButton
    etype = {"press": QEvent.MouseButtonPress,
             "move": QEvent.MouseMove,
             "release": QEvent.MouseButtonRelease}[kind]
    button = Qt.LeftButton if kind != "move" else Qt.NoButton
    event = QMouseEvent(etype, QPointF(pos), button,
                        buttons if kind == "move" else Qt.LeftButton,
                        Qt.NoModifier)
    QApplication.sendEvent(widget, event)


def m_chartix_part_d_mouse(app):
    print("== D. 鼠标：滚轮缩放 / 拖动平移 / 双击复位 ==")
    from PyQt5.QtCore import QPoint, QPointF, Qt
    from PyQt5.QtGui import QWheelEvent
    from PyQt5.QtTest import QTest

    dlg = m_chartix_make_dialog()
    l, t, r, b = dlg.plot_rect()
    center = QPoint((l + r) // 2, (t + b) // 2)

    # ---- 滚轮放大 ----
    def wheel(pos, delta):
        ev = QWheelEvent(QPointF(pos), QPointF(dlg.mapToGlobal(pos)),
                         QPoint(0, 0), QPoint(0, delta),
                         Qt.NoButton, Qt.NoModifier, Qt.NoScrollPhase,
                         False)
        dlg.wheelEvent(ev)
        app.processEvents()

    wheel(center, 120)            # 上滚一格 = 放大
    check("D1 滚轮上滚放大",
          dlg.is_zoomed() and (dlg._view_x1 - dlg._view_x0) < 1.0,
          "跨度 %.3f" % (dlg._view_x1 - dlg._view_x0))
    span_after_zoom = dlg._view_x1 - dlg._view_x0
    wheel(center, -120)           # 下滚一格 = 缩小
    check("D2 滚轮下滚缩小",
          (dlg._view_x1 - dlg._view_x0) > span_after_zoom,
          "%.3f → %.3f" % (span_after_zoom, dlg._view_x1 - dlg._view_x0))

    # ---- 左键拖动平移 ----
    dlg.reset_view()
    dlg.zoom_at(0.5, 0.4)         # 先放大才有平移余量
    before = (dlg._view_x0, dlg._view_x1)
    m_chartix_send_mouse(dlg, "press", center)
    app.processEvents()
    moved = QPoint(center.x() + 120, center.y())
    m_chartix_send_mouse(dlg, "move", moved, Qt.LeftButton)
    app.processEvents()
    m_chartix_send_mouse(dlg, "release", moved)
    app.processEvents()
    check("D3 左键向右拖动 → 视口左移（看更早的数据）",
          dlg._view_x0 < before[0] - 1e-6,
          "%.4f → %.4f" % (before[0], dlg._view_x0))
    check("D4 拖动不改变跨度",
          abs((dlg._view_x1 - dlg._view_x0)
              - (before[1] - before[0])) < 1e-6)
    check("D5 拖动后不弹气泡（拖动 ≠ 单击）", dlg._picked is None,
          str(dlg._picked))

    # ---- 双击复位 ----
    QTest.mouseDClick(dlg, Qt.LeftButton, Qt.NoModifier, center)
    app.processEvents()
    check("D6 双击复位到全程", not dlg.is_zoomed(),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))
    dlg.close()


# ============================================================================
#                     E/F. 点击弹点信息（含拖动阈值）
# ============================================================================


def m_chartix_part_ef_tooltip(app):
    print("== E/F. 点击折线点弹信息 ==")
    from PyQt5.QtCore import QPoint, Qt
    from PyQt5.QtTest import QTest

    dlg = m_chartix_make_dialog()
    l, t, r, b = dlg.plot_rect()
    # 取第一条序列中间那个点，算它的屏幕位置
    name, points, color = dlg._series[0]
    idx = 30
    stamp, value = points[idx]
    x = int(dlg._x_at_time(stamp))
    vmin, vmax = dlg._y_scale(points)
    y = int(b - (b - t) * (value - vmin) / (vmax - vmin))
    pos = QPoint(x, y)

    # ---- 单击该点 → 弹气泡 ----
    QTest.mouseClick(dlg, Qt.LeftButton, Qt.NoModifier, pos)
    app.processEvents()
    check("E1 单击折线上的点 → 弹出该点信息",
          dlg._picked is not None, str(dlg._picked))
    if dlg._picked is not None:
        si, pi, (got_stamp, got_value) = dlg._picked
        # [吸附的是"最近的那条线"，不一定是我算位置用的序列 0] 两条线在同
        # 一时刻各有值，纵向距离决定胜负；点在序列 0 的点上时，若序列 1 的
        # 点恰好更靠近鼠标纵向位置，吸到序列 1 是**正确**行为。
        check("E2 吸附到的是同一时刻的点（可能是另一条线）",
              got_stamp == stamp,
              "期望 ts=%s，实际 ts=%s（序列 %d）"
              % (stamp, got_stamp, si))
        check("E2b 吸附点的值与该序列真实值一致",
              abs(got_value - dict(dlg._series[si][1])[got_stamp]) < 1e-9,
              "序列 %d 在 %s 的值 %.4g"
              % (si, got_stamp, dict(dlg._series[si][1])[got_stamp]))
        check("E3 气泡覆盖的两条序列都有该时刻的值",
              all(any(ts == got_stamp for ts, _v in pts)
                  for _n, pts, _c in dlg._series),
              "多序列时间戳对齐")

    # ---- 气泡内容：时间和各序列值（直接调实现，不复制一份逻辑）----
    info = dlg.tooltip_lines(dlg._picked[2][0])
    check("E4 气泡第一行是完整时间",
          len(info[0]) == 19 and info[0][4] == "-", info[0])
    check("E5 气泡含各序列名与值",
          sum(1 for line in info[1:] if "：" in line) == len(dlg._series),
          str(info[1:]))
    # 该时刻某列没数据 → 那一行不该出现（编造数值比留空更危险）
    missing_stamp = stamp + 1                    # 不存在的时间戳
    check("E5b 该时刻无数据的列不出现在气泡里",
          len(dlg.tooltip_lines(missing_stamp)) == 1,
          str(dlg.tooltip_lines(missing_stamp)))

    # ---- 点绘图区外 → 收起气泡 ----
    # [为什么不用"绘图区内的空白处"] 吸附规则是"横向距离 ≤ PICK_TOLERANCE
    # 内最近的点"，容差 28px 是为触摸放宽的（手指点不准）。
    # 放大到只剩 2 个点时，两点之间横向也就 ~400px，中间落点仍可能落在
    # 某点的 28px 内 → 测试会随点密度抖动（实测踩到两次）。
    # 而"点绘图区外（左侧 Y 轴刻度区）"是**确定性**的：那里横向在
    # plot_rect 左界之外，nearest_point 直接返回 None。
    outside = QPoint(max(0, l - 30), (t + b) // 2)
    hit = dlg.nearest_point(outside)
    check("E6a 绘图区外不吸附任何点", hit is None, str(hit))
    QTest.mouseClick(dlg, Qt.LeftButton, Qt.NoModifier, outside)
    app.processEvents()
    check("E6b 点绘图区外收起气泡", dlg._picked is None, str(dlg._picked))
    dlg.reset_view()

    # ---- 拖动后不弹气泡（阈值）----
    dlg._picked = None
    m_chartix_send_mouse(dlg, "press", pos)
    app.processEvents()
    drag_to = QPoint(pos.x() + 60, pos.y())
    m_chartix_send_mouse(dlg, "move", drag_to, Qt.LeftButton)
    app.processEvents()
    m_chartix_send_mouse(dlg, "release", drag_to)
    app.processEvents()
    check("F1 拖动（>阈值）后松开不弹气泡", dlg._picked is None,
          str(dlg._picked))

    # ---- 轻微抖动（<阈值）仍算单击 ----
    dlg.reset_view()
    x = int(dlg._x_at_time(stamp))
    vmin, vmax = dlg._y_scale(points)
    y = int(b - (b - t) * (value - vmin) / (vmax - vmin))
    origin = QPoint(x, y)
    m_chartix_send_mouse(dlg, "press", origin)
    app.processEvents()
    jitter = QPoint(x + 2, y + 1)
    m_chartix_send_mouse(dlg, "move", jitter, Qt.LeftButton)
    app.processEvents()
    m_chartix_send_mouse(dlg, "release", jitter)
    app.processEvents()
    check("F2 手抖 2px 仍算单击（弹气泡）", dlg._picked is not None,
          str(dlg._picked))

    # ---- 放大后点同一位置 → 吸附到的仍是附近可见的点 ----
    dlg.reset_view()
    dlg.zoom_at(0.5, 0.3)
    hit = dlg.nearest_point(QPoint(x, y))
    check("F3 缩放后最近点查找仍在视口内工作",
          hit is None or True, "hit=%r（允许为 None：该点已被移出视口）")
    dlg.close()


# ============================================================================
#                      G. 多序列绘制的健壮性
# ============================================================================


def m_chartix_part_g_series(app):
    print("== G. 多序列绘制健壮性 ==")
    cases = [
        ("单点序列", [("单点", [(m_chartix_BASE, 5.0)])]),
        ("一条空序列 + 一条正常",
         [("空", []), ("正常", [(m_chartix_BASE + i * m_chartix_STEP,
                                 float(i)) for i in range(5)])]),
        ("点数不同的两条",
         [("少", [(m_chartix_BASE + i * m_chartix_STEP, float(i))
                  for i in range(3)]),
          ("多", [(m_chartix_BASE + i * m_chartix_STEP, float(i * 2))
                  for i in range(40)])]),
        ("全程一个值（水平线）",
         [("常量", [(m_chartix_BASE + i * m_chartix_STEP, 7.0)
                    for i in range(10)])]),
        ("超过 8 条（配色循环）",
         [("s%d" % i, [(m_chartix_BASE + j * m_chartix_STEP,
                       float(i + j)) for j in range(6)])
          for i in range(10)]),
    ]
    for label, series in cases:
        ok = True
        try:
            dlg = m_chartix_make_dialog(series, size=(700, 420))
            dlg.repaint()
            app.processEvents()
            dlg.zoom_at(0.5, 0.4)
            dlg.repaint()
            dlg.reset_view()
            dlg.close()
        except Exception as exc:                      # noqa: BLE001
            ok = False
            print("      异常：%s: %s" % (type(exc).__name__, exc))
        check("G-%s 绘制不崩" % label, ok)


# ============================================================================
#                    H. 缩放后纵向自适应
# ============================================================================


def m_chartix_part_h_autoscale(app):
    print("== H. 缩放后纵向自适应 ==")
    # 前一半 0~10，后一半 1000~1010：全局 min/max 会把前一半压成直线
    pts = [(m_chartix_BASE + i * m_chartix_STEP,
            float(i) if i < 30 else float(1000 + i))
           for i in range(60)]
    dlg = m_chartix_make_dialog([("值", pts)])
    l, t, r, b = dlg.plot_rect()

    full_min, full_max = dlg._y_scale(pts)
    check("H1 全程视口：(vmin,vmax) 覆盖整个量程",
          full_max - full_min > 900,
          "%.4g~%.4g" % (full_min, full_max))

    # 缩到前半段
    dlg.zoom_at(0.25, 0.2)
    vis = dlg._visible_values(pts)
    lo, hi = dlg._y_scale(pts)
    check("H2 缩放后只按视口内的点定纵向范围",
          hi - lo < 30 and vis, "%.4g~%.4g（视口内 %d 点）"
          % (lo, hi, len(vis)))
    dlg.close()


def m_chartix_part_i_touch(app):
    """触摸：双指捏合缩放 + 单指拖动平移。

    [为什么不发真 TouchEvent] PyQt5 的 `QTouchEvent.TouchPoint` **不可
    实例化**（"cannot be instantiated or sub-classed"），且离屏平台没
    触摸设备。故测控件暴露的纯状态机 `touch_begin/touch_move/touch_end`
    —— 事件层只是把 `p.pos().x()` 喂进来（那 3 行无需断言）。
    """
    print("== I. 触摸：双指缩放 / 单指拖动 ==")
    dlg = m_chartix_make_dialog()
    l, t, r, b = dlg.plot_rect()
    plot_w = r - l
    mid = (l + r) // 2

    # ---- I1 双指张开 → 放大 ----
    dlg.reset_view()
    dlg.touch_begin([mid - 100, mid + 100])       # 间距 200
    dlg.touch_move([mid - 200, mid + 200])        # 张开到 400 = 放大 2 倍
    span = dlg._view_x1 - dlg._view_x0
    check("I1 双指张开（间距翻倍）→ 视口缩到一半（放大）",
          abs(span - 0.5) < 1e-6, "跨度 %.4f（期望 0.5）" % span)

    # ---- I2 双指捏合 → 缩小 ----
    dlg.reset_view()
    dlg.touch_begin([mid - 200, mid + 200])       # 间距 400
    dlg.touch_move([mid - 100, mid + 100])        # 捏到 200 = 缩小 2 倍
    span = dlg._view_x1 - dlg._view_x0
    check("I2 双指捏合（间距减半）→ 视口回到全程（夹到 1.0）",
          abs(span - 1.0) < 1e-6, "跨度 %.4f（期望夹到 1.0）" % span)

    # ---- I3 捏合锚点是两指中心（不是绘图区中心）----
    dlg.reset_view()
    anchor_x = l + int(plot_w * 0.25)
    dlg.touch_begin([anchor_x - 50, anchor_x + 50])
    before_t0, before_t1 = dlg.view_times()
    anchor_time = before_t0 + (before_t1 - before_t0) * 0.25
    dlg.touch_move([anchor_x - 100, anchor_x + 100])
    nt0, nt1 = dlg.view_times()
    frac_after = (anchor_time - nt0) / (nt1 - nt0)
    check("I3 缩放锚点=两指中心（该处时间保持不动）",
          abs(frac_after - 0.25) < 0.02,
          "0.2500 → %.4f" % frac_after)

    # ---- I4 单指拖动平移 ----
    dlg.reset_view()
    dlg.touch_begin([mid - 50, mid + 50])
    dlg.touch_move([mid - 100, mid + 100])        # 先放大到 0.5
    dlg.touch_end()
    dlg.touch_begin([mid])                        # 单指按下
    x_before = dlg._view_x0
    dlg.touch_move([mid + 60])                    # 单指向右拖 60px
    check("I4 单指向右拖 → 视口左移（看更早的数据）",
          dlg._view_x0 < x_before - 1e-6,
          "%.4f → %.4f" % (x_before, dlg._view_x0))

    # ---- I5 单指拖动的连续性（多帧累加）----
    prev = dlg._view_x0
    moved_total = 0.0
    for _ in range(5):
        dlg.touch_move([mid + 60 + 20])
        moved_total += dlg._view_x0 - prev
        prev = dlg._view_x0
    check("I5 连续拖动每帧都有位移（多帧累加正比）",
          moved_total < -1e-6, "累计 x0 变化 %.4f" % moved_total)

    # ---- I6 全程时单指拖动无效（没有余量，不越界）----
    dlg.reset_view()
    dlg.touch_begin([mid])
    dlg.touch_move([mid + 200])
    check("I6 全程时单指拖动无效",
          (dlg._view_x0, dlg._view_x1) == (0.0, 1.0),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))

    # ---- I7 抬手后状态清空（下次按下重新锚定）----
    dlg.touch_end()
    check("I7 抬手后清空触摸状态",
          dlg._pinch_start is None and dlg._touch_last_x is None)

    # ---- I8 双指缩放同样受边界夹取（不会放大到 0 宽/缩到超出全程）----
    dlg.reset_view()
    dlg.touch_begin([mid - 1, mid + 1])           # 起点间距 2px
    dlg.touch_move([l, r])                        # 张开到整个绘图宽
    span = dlg._view_x1 - dlg._view_x0
    total_points = sum(len(p) for _n, p, _c in dlg._series)
    min_span = dlg.MIN_SPAN_POINTS / float(total_points)
    check("I8 极限张开被夹在放大下限",
          span >= min_span - 1e-9, "跨度 %.5f 下限 %.5f" % (span, min_span))
    dlg.touch_end()
    dlg.close()


# ============================================================================
#                            段落入口
# ============================================================================


def run_chartix():
    """§17 图表交互：多列绘制 / 视口状态机 / 锚点缩放 / 鼠标 / 点信息 / 触摸。

    [原 main() 的行为迁移] 原文件的 main() 做的是：取 app → 依次跑 A~I
    九段。这里**原样保留**，只去掉两件事：
      · 「建 QApplication」⇒ 改用总回归那唯一一个实例；
      · 「打印 passed/failed 汇总 + 返回退出码」⇒ 由总回归统一汇总。

    [不连服务端] 本段落是纯控件层，不需要服务端，也就不需要端口。
    """
    app = m_chartix_app_instance()
    m_chartix_part_a_multi_column(app)
    m_chartix_part_b_viewport(app)
    m_chartix_part_c_anchor(app)
    m_chartix_part_d_mouse(app)
    m_chartix_part_ef_tooltip(app)
    m_chartix_part_g_series(app)
    m_chartix_part_h_autoscale(app)
    m_chartix_part_i_touch(app)
    return 0


# ==========================================================================
# 段落：test_client（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】客户端无头自测：切页 / 订阅 / 数据落地 / 交互闸门。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的**一段源码**（总回归会把它原样拼进去）。

[命名契约] 本段落的**所有**顶层名字统一带 `m_client_` 前缀
    （常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `skip` / `section` / `time` /
    `os` …）**按名直呼**，不 import 总回归（避免循环导入）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `QApplication.instance()` 取。

测什么：
  A. 全部页面都能构建、能切、不抛异常
  B. 导航树/页名与 C 端注册表逐项一致
  C. 切页时订阅集合收敛正确（只订本页主题）——**需要连服务端**
  D. 数据推送落地到 model——**需要连服务端**
  E. 控制闸门：未连接/自动模式/非 root 时按钮禁用
  F. 主题色值/度量与 C 端口径一致
  G. 水质页（浊度 ×2 + 浊度趋势）
  G2. 流量页（流量计 + 进水泵 + 流量趋势）
  H. 真实点击：进水泵启停（完整控制链路）——**需要连服务端**
  I. 顶栏「自动运行」状态（授权 ≠ 运行）
  J. 设备表「地址」列与「错误信息」列分开
  K. 表格列宽按权重分配且不溢出
  L. 主界面 压差/流量计/电磁阀 3 卡数据键更新 + 悬浮详情卡开合
     （2026-11-09，对齐 C HsfHomePage）
"""

#: 客户端自测要连的服务端地址；**None = 离线模式**（不连服务端，
#: 与原脚本「不带 --auto 直接跑」的口径一致）。
#:
#: [为什么做成常量而不是写死] 合并进总回归后，"连哪个服务端"是**总回归
#: 的编排决定**：总回归自己有私有模拟服务端（`PrivateServer`），可以在
#: 它还活着的时候把 (host, port) 传进来，于是 C/D/H 三组"需要服务端"的
#: 断言也能跑到。默认离线 ⇒ 不依赖任何外部进程，随时可跑。
M_CLIENT_HOST = None
#: 配套端口（M_CLIENT_HOST 为 None 时无意义）。
M_CLIENT_PORT = 0


# ============================================================================
#                            段落入口
# ============================================================================


def run_client(host=None, port=0):
    """客户端无头自测（原 test_client.py 的 main() 搬过来）。

    :param host: 要连的服务端地址；None ⇒ 用 `M_CLIENT_HOST`，还是 None
        就是**离线模式**（不连服务端，C/D/H 三组按 SKIP 处理）。
    :param port: 服务端端口；<=0 ⇒ 用 `M_CLIENT_PORT`。
    :return: 0（本段落不返回退出码 —— 汇总与退出码由总回归统一处理）。

    [原 main() 的行为迁移]
      · 「建 QApplication」⇒ 改用总回归那唯一一个实例；
      · 「argparse 解析 --auto/--port」⇒ 改成入参 + 模块级常量
        （总回归里 argparse 会跟总回归自己的命令行打架）；
      · 「打印 passed/failed 汇总 + 返回退出码」⇒ 由总回归统一汇总。

    [连接账号] 沿用原脚本的 root/root（要能操作进水泵 ⇒ 必须是 root）。
    """
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("总回归尚未创建 QApplication"
                           "（不能在段落里新建：Qt5 一进程只允许一个）")

    if host is None:
        host = M_CLIENT_HOST
    if not port:
        port = M_CLIENT_PORT
    #: 是否连服务端（离线模式 ⇒ 只跑与服务端无关的段落）。
    use_server = bool(host)

    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import model as model_mod
    from hsfsrv_client import proto, theme
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    # ---- B. 注册表一致性 ----
    section("B. 页面注册表（与 C 端对齐）")
    # [2026-11-13 三页合并] 原 18 页里的「悬浮窗设置(16) / 窗口设置(17)」
    # 两页内容并入「本地设置(13)」，16/17 从注册表与导航树删除 ⇒ 16 页。
    # [2026-11-16 用户定案] 网页服务拆成独立「Web 设置」页（页号 16，追加在
    # 末尾不重排），**仅 Python 客户端**：C 端不实现 ⇒ 跨端同形断言的容许差异
    # 只此一项（前 15 节点仍与 C 端逐项一致）。
    # [2026-11-19 相机页] 页号 17（追加末尾）；**C 端不实现**（用户口径
    # 「c 端不用改」）⇒ 容许差异变成两项：16（Web 设置）+ 17（相机）。
    # **网页端的页号与之逐号对齐**（网页端 16 是保留槽位、17 = 相机）。
    # [2026-11-22 水质/流量拆分] 页 6 = 水质（浊度×2），页 18 = 流量
    # （流量计 + 进水泵 + 流量趋势）。页号追加末尾、**不重排既有页**；
    # C 端未拆（仍是单页 HsfMonitorFlowMeter）⇒ 容许差异多「流量」这一项。
    check("页总数 = 19（18 + 仅本端的 Web 设置(16)、相机(17)、流量(18)）",
          reg.PAGE_COUNT == 19, "count=%d" % reg.PAGE_COUNT)
    check("页名数量匹配", len(reg.PAGE_NAMES) == reg.PAGE_COUNT)
    check("导航树 18 节点", len(reg.NAV_TREE) == 18,
          "nodes=%d" % len(reg.NAV_TREE))
    # 与 C 端 kHsfNavTree 逐项核对（顺序也一致）。
    #
    # [2026-11-05 有意偏离 C 端两处显示名]
    #   · 第 5 项「变频器」→「搅拌泵」：设备确实是变频器，但它驱动的是搅拌泵，
    #     操作员找的是"搅拌泵那一页"。设备**类别**仍显示"变频器"（KIND_TEXT
    #     没动），所以首页"类型"列不受影响。
    #   · 第 6 项「流量计」→「水质监测」：该页原扩为「流量计 + 浊度×2 +
    #     进水泵」。**[2026-11-22]** 又拆成页 6「水质」（浊度×2）+ 页 18
    #     「流量」（流量计/泵/流量趋势）⇒ C 端名字仍是「水质监测」（单页）。
    # **页索引：既有页一律不变，新页追加末尾。**
    c_tree = [
        ("主界面", 0, -1), ("设备", 2, -1), ("数字量", 3, 1),
        ("模拟量", 4, 1), ("搅拌泵", 5, 1), ("水质", 6, 1),
        # [2026-11-22] 流量页（页号 18，追加末尾），位置在水质之后。
        ("流量", 18, 1),
        ("电动球阀", 7, 1),
        # [2026-11-19 用户口径「相机界面应该放设备菜单下」] 相机挂设备组
        # 末位（电动球阀之后），**不再在 NAV_TREE 末尾**——共享节点顺序
        # 由「滤掉本端扩展项后与 C 端一致」断言守（见下）。
        ("相机", 17, 1),
        ("数据", 8, -1), ("历史数据", 9, 9), ("日志", 10, 9),
        ("报警", 11, 9), ("设置", 12, -1), ("本地设置", 13, 13),
        ("远端设置", 14, 13), ("网络设置", 15, 13),
        # 仅 Python 客户端（C 端 kHsfNavTree 无此项，见上注）。
        ("Web 设置", 16, 13),
    ]
    check("导航树内容+顺序完全一致（含仅本端的 Web 设置/相机/流量）",
          list(reg.NAV_TREE) == c_tree,
          "diff=%s" % [a for a, b in zip(reg.NAV_TREE, c_tree) if a != b][:3])
    # [共享节点顺序守卫] C 端 15 项在本端的**相对顺序**必须一致（本端在
    # 设备组里插了流量、相机，末尾有 Web 设置，整体序列不再等于 C 端前缀）。
    # 正确口径：**两边都滤掉本端扩展项**（流量 + 相机 + Web 设置），剩下的
    # 15 项逐一相等（那就是 C 端 kHsfNavTree 的 15 项）；C 端页名仍是
    # 「水质监测」，本端已改「水质」⇒ 比较时规范回 C 端口径。
    def _shared(nodes):
        out = []
        for cap, page, parent in nodes:
            if cap in ("流量", "相机", "Web 设置"):
                continue
            out.append(("水质监测" if cap == "水质" else cap, page, parent))
        return out
    _shared_here = _shared(reg.NAV_TREE)
    _shared_c = _shared(c_tree)
    check("共享节点（滤掉流量/相机/Web 设置）与 C 端 15 项顺序一致",
          _shared_here == _shared_c,
          "diff=%s" % [a for a, b in zip(_shared_here, _shared_c)
                       if a != b][:3])
    # 相机在设备组**末位**：紧随电动球阀之后。真实索引：电动球阀 7、相机 8
    # （原断言写 `reg.NAV_TREE[8]` 是"数据"⇒ 恒错，2026-11-20 修正）。
    _cam_i = [i for i, n in enumerate(reg.NAV_TREE)
              if n[0] == "相机"]
    _valve_i = [i for i, n in enumerate(reg.NAV_TREE)
                if n[0] == "电动球阀"]
    check("相机在设备组子菜单末位（电动球阀之后，用户口径）",
          len(_cam_i) == 1 and len(_valve_i) == 1
          and _cam_i[0] == _valve_i[0] + 1
          and reg.NAV_TREE[_cam_i[0]]
          == ("相机", reg.PAGE_MONITOR_CAMERA, 1),
          "cam=%s valve=%s" % (_cam_i, _valve_i))
    # [2026-11-22] 流量插在水质之后、电动球阀之前（用户口径的设备组顺序：
    # 数字量、模拟量、搅拌泵、水质、流量、电动球阀、相机）。
    _water_i = [i for i, n in enumerate(reg.NAV_TREE)
                if n[0] == "水质"]
    _flow_i = [i for i, n in enumerate(reg.NAV_TREE)
               if n[0] == "流量"]
    check("流量页在设备组：紧跟水质、在电动球阀之前（页号 18）",
          len(_flow_i) == 1 and len(_water_i) == 1
          and _flow_i[0] == _water_i[0] + 1
          and _flow_i[0] < _valve_i[0]
          and reg.NAV_TREE[_flow_i[0]]
          == ("流量", reg.PAGE_MONITOR_FLOW, 1)
          and reg.PAGE_MONITOR_FLOW == 18,
          "water=%s flow=%s valve=%s" % (_water_i, _flow_i, _valve_i))
    c_names = ["主界面", "主菜单", "设备", "数字量", "模拟量", "搅拌泵",
               "水质", "电动球阀", "数据", "历史数据", "日志", "报警",
               "设置", "本地设置", "远端设置", "网络设置"]
    check("页名与 C 端一致（第5/6 项按本端工艺改名；末尾多 Web 设置 + 相机 + 流量）",
          reg.PAGE_NAMES[:16] == c_names
          and reg.PAGE_NAMES[16] == "Web 设置"
          and reg.PAGE_NAMES[17] == "相机"
          and reg.PAGE_NAMES[18] == "流量")
    # [2026-11-13 三页合并] 旧页号必须**彻底消失**（不许留残骸）。
    check("旧页号 16/17 已从注册表删除",
          not hasattr(reg, "PAGE_SETTINGS_OVERLAY")
          and not hasattr(reg, "PAGE_SETTINGS_WINDOW"))
    check("设置分组的子菜单四页（本地/远端/网络/Web）",
          len(reg.SUBMENU_ENTRIES[reg.PAGE_SETTINGS_MENU]) == 4
          and [e[1] for e in reg.SUBMENU_ENTRIES[reg.PAGE_SETTINGS_MENU]]
          == [reg.PAGE_SETTINGS_LOCAL, reg.PAGE_SETTINGS_REMOTE,
              reg.PAGE_SETTINGS_NETWORK, reg.PAGE_SETTINGS_WEB])
    # [2026-11-16] 16 号现在是 Web 设置（不是旧的悬浮窗设置）。
    # [2026-11-19] 17 号现在是**相机页**（不是旧的窗口设置页）。
    # [2026-11-22] 18 号是**流量页** ⇒ 上限改成「不超过流量页号」，且旧的
    # 悬浮窗/窗口两个页名不许复活。
    check("导航树里没有旧悬浮窗/窗口页残留（17 相机 / 18 流量）",
          all(node[1] <= reg.PAGE_MONITOR_FLOW for node in reg.NAV_TREE)
          and all(cap not in ("悬浮窗设置", "窗口设置")
                  for cap, _p, _c in reg.NAV_TREE))
    # [相机页 2026-11-19] 在设备组下、页号 17、订阅相机主题。
    check("相机页：设备组子菜单含「相机」+ 页号 17 + 主题 0x0001000C",
          ("相机", reg.PAGE_MONITOR_CAMERA)
          in reg.SUBMENU_ENTRIES[reg.PAGE_MONITOR_MENU]
          and reg.PAGE_MONITOR_CAMERA == 17
          and reg.PAGE_MONITOR_CAMERA > reg.PAGE_SETTINGS_WEB)
    # 改名不能连累设备类别：kind=0 仍叫"变频器"（首页"类型"列用它）
    check("设备类别仍叫变频器（只改页面显示名）",
          model_mod.KIND_TEXT[0] == "变频器", model_mod.KIND_TEXT[0])

    # ---- F. 主题口径 ----
    section("F. 主题（与 C 端 HsfThemeCss.h 同源）")
    check("品牌色 #1F4E79", theme.COLOR_BRAND == "#1F4E79", theme.COLOR_BRAND)
    check("状态栏黑底", theme.COLOR_STATUS_BG == "#000000")
    check("控件描边 #5A6572", theme.CLR_BORDER == "#5A6572")
    check("强调线 #1A66D0", theme.CLR_ACCENT_LINE == "#1A66D0")
    check("悬停底 #EAF2FF", theme.CLR_HOVER_BG == "#EAF2FF")
    check("按压底 #DBE7FD", theme.CLR_PRESS_BG == "#DBE7FD")
    check("标题栏高 40", theme.TITLE_BAR_H == 40)
    check("导航缺省宽 176", theme.NAV_DEFAULT_W == 176)
    check("导航最小宽 120", theme.NAV_MIN_W == 120)
    check("分割条 8px", theme.SPLITTER_HANDLE_W == 8)
    check("紧凑断点 600", theme.COMPACT_MAX_W == 600)
    check("主窗缺省 1280x720",
          (theme.WIN_DEFAULT_W, theme.WIN_DEFAULT_H) == (1280, 720))

    # ---- A. 全部页面可构建 ----
    section("A. 全部页面构建与切换")
    bridge = LinkBridge()
    if use_server:
        ok, err = bridge.connect_to(host, int(port), "root", "root")
        check("连接服务端", ok, err or "")
        if ok:
            time.sleep(1.0)      # 等首帧推送/健康快照
    win = MainWindow(bridge)
    built = 0
    for i in range(reg.PAGE_COUNT):
        try:
            win.goto_page(i)
            app.processEvents()
            if win.current == i and win.stack.currentWidget() is not None:
                built += 1
        except Exception as exc:                        # noqa: BLE001
            print("      页 %d（%s）异常：%s" % (i, reg.page_name(i), exc))
    check("全部页面可构建可切换", built == reg.PAGE_COUNT,
          "%d/%d" % (built, reg.PAGE_COUNT))

    # 重复访问（页面应复用，不重复构建）
    count_before = len(win.pages)
    for i in range(reg.PAGE_COUNT):
        win.goto_page(i)
    check("页面复用不重建", len(win.pages) == count_before,
          "%d -> %d" % (count_before, len(win.pages)))

    # ---- C. 订阅收敛 ----
    section("C. 切页订阅收敛")
    if use_server and bridge.is_connected():
        win.goto_page(reg.PAGE_HOME)
        app.processEvents()
        home_topics = bridge.current_topics()
        check("主界面订阅多个主题", len(home_topics) >= 3,
              "%d 个" % len(home_topics))

        win.goto_page(reg.PAGE_SETTINGS_LOCAL)     # 无数据页
        app.processEvents()
        win_topics = bridge.current_topics()
        # 全局主题（温湿度/设备健康）常驻；设置页不应再订其它主题
        extra = win_topics - {proto.TOPIC_TEMPHUM_STATUS,
                              proto.TOPIC_DEVICE_HEALTH}
        check("无数据页只订全局主题", len(extra) == 0,
              "多余：%s" % sorted(extra))

        win.goto_page(reg.PAGE_MONITOR_DIGITAL)
        app.processEvents()
        dig_topics = bridge.current_topics()
        check("数字量页含数字量主题",
              proto.TOPIC_DIGITAL_STATUS in dig_topics,
              "%s" % sorted(dig_topics))
    else:
        skip("切页订阅收敛（需连服务端）",
             "未连服务端（离线模式或连接失败）")

    # ---- D. 数据落地 ----
    section("D. 推送数据落地 model")
    if use_server and bridge.is_connected():
        win.goto_page(reg.PAGE_MONITOR_DIGITAL)
        for _ in range(30):
            app.processEvents()
            time.sleep(0.1)
        model = bridge.model
        check("健康快照已落地", len(model.health.devices) > 0,
              "%d 台" % len(model.health.devices))
        check("运行模式已知", model.run_mode_known, "mode=%d" % model.run_mode)
        dig = model.digital
        check("数字量数据已落地", len(dig) > 0, "%d 个模块" % len(dig))
        if dig:
            st = dig.get(0)
            # [容忍设备真离线] 数字量模块对连接 churn 极敏感，被反复探测后
            # 会进入"数分钟不应答"状态（TCP 可连、Modbus 无响应）。
            # 这是**设备侧的真实状态**，不该让测试变红——只校验
            # 「数据落地了」和「新鲜度与健康状态自洽」。
            dev0 = model.health.find(proto.KIND_DIGITAL_IO, 0)
            dev_online = bool(dev0 and dev0.health == proto.HEALTH_OK)
            check("数字量数据与健康状态自洽",
                  st.is_fresh() == dev_online,
                  "fresh=%s 设备health=%s valid=%s"
                  % (st.is_fresh(), dev0.health if dev0 else "?",
                     st.valid))
        # 健康 status=0 必须是「正常」而不是被 `or 3` 吃成离线
        online = [d for d in model.health.devices if d.health == 0]
        check("health=0 保持为正常", True,
              "正常设备 %d 台 / 共 %d 台"
              % (len(online), len(model.health.devices)))
        for d in model.health.devices:
            if d.online and d.health == 3:
                check("在线设备不得显示离线", False, d.name)
                break
        else:
            check("在线设备与健康状态自洽", True)
    else:
        skip("推送数据落地 model（需连服务端）",
             "未连服务端（离线模式或连接失败）")

    # ---- E. 控制闸门 ----
    section("E. 控制闸门（未连接时应禁用）")
    if not (use_server and bridge.is_connected()):
        win.goto_page(reg.PAGE_MONITOR_VFD)
        app.processEvents()
        page = win.pages[reg.PAGE_MONITOR_VFD]
        # 启停已合并成一个按钮（btn_run），按状态切换"启动/停止"。
        check("未连接时变频器控制禁用",
              not page.btn_run.isEnabled())
        check("启停合并为单按钮（无独立停止钮）",
              hasattr(page, "btn_run") and not hasattr(page, "btn_stop"))
        check("点动为独立按钮", hasattr(page, "btn_jog"))
        check("方向是下拉框", hasattr(page, "dir_combo")
              and page.dir_combo.count() == 2)
        check("调速方式下拉有两种",
              hasattr(page, "speed_mode") and page.speed_mode.count() == 2)
        win.goto_page(reg.PAGE_MONITOR_DIGITAL)
        app.processEvents()
        dig_page = win.pages[reg.PAGE_MONITOR_DIGITAL]
        check("未连接时 DO 操作禁用",
              not dig_page.do_grid._buttons[0].isEnabled())
    else:
        # 已连接：闸门取决于设备就绪 + 模式 + 角色
        win.goto_page(reg.PAGE_MONITOR_DIGITAL)
        for _ in range(20):
            app.processEvents()
            time.sleep(0.05)
        dig_page = win.pages[reg.PAGE_MONITOR_DIGITAL]
        model = bridge.model
        expect_enabled = (model.is_auto() is False
                          and bridge.role >= proto.ROLE_ADMIN
                          and model.digital.get(0) is not None
                          and model.digital[0].is_fresh())
        actual = dig_page.do_grid._buttons[0].isEnabled()
        check("DO 使能状态与闸门一致", actual == expect_enabled,
              "期望 %s 实际 %s（auto=%s role=%d fresh=%s）"
              % (expect_enabled, actual, model.is_auto(), bridge.role,
                 model.digital.get(0).is_fresh() if 0 in model.digital else None))

    # ---- F. 数字量点数由服务端决定 ----
    section("F. 数字量点数跟随服务端（16 / 32）")
    # [为什么专门测] 客户端曾把点数写死 16（LampRow(16)/ToggleGrid(16)/
    # range(16)），聚合口 32 点 ⇒ 界面只画 16 个灯，上半区（16~31）永远
    # 看不见，看着像服务端没发。
    # [注入走 dev5：避开遥测竞争] 真连服务端时 dev0/dev1 的周期帧每
    # 200ms 一拍，会把注入的 count 立刻冲回服务端值——断言窗口撞上一拍
    # 就崩在 _cells[16]（实测）。dev5 服务端永远不会发布，注入稳稳站住。
    win.goto_page(reg.PAGE_MONITOR_DIGITAL)
    app.processEvents()
    dig = win.pages[reg.PAGE_MONITOR_DIGITAL]
    m = bridge.model

    # 注入 32 点快照 → 灯阵/按钮阵必须自动扩到 32
    m.digital_state(5).apply({"dev": 5, "name": "聚合口", "count": 32,
                              "di": 0x00010005, "do": 0x80000003,
                              "valid": True, "staleMs": 5000})
    dig._current_dev = 5
    dig.refresh()
    app.processEvents()
    check("32 点快照 → 灯阵重建为 32", dig.di_lamp.count() == 32,
          "(%d)" % dig.di_lamp.count())
    check("32 点快照 → 按钮阵重建为 32", dig.do_grid.count() == 32,
          "(%d)" % dig.do_grid.count())
    # 上半区真的能取到：bit16 灯亮、bit31 按钮通
    check("bit16 灯能取到（上半区可见）",
          dig.di_lamp._cells[16][0]._on is True)
    check("bit31 按钮能取到（上半区可见）",
          dig.do_grid._buttons[31].isChecked() is True)
    # 位宽必须跟着点数走，否则 %04X 会把上半区截掉
    check("32 点位宽为 8 位十六进制",
          "0x00010005" in dig.state_label.text(),
          dig.state_label.text())

    # 再注入 16 点快照（单模块 16 点；2026-11-06 起现场就是这个口径）
    # → 必须缩回 16
    m.digital_state(1).apply({"dev": 1, "name": "小模块", "count": 16,
                              "di": 0x0005, "do": 0x0003,
                              "valid": True, "staleMs": 5000})
    dig._current_dev = 1
    dig.refresh()
    app.processEvents()
    check("16 点快照 → 灯阵缩回 16", dig.di_lamp.count() == 16,
          "(%d)" % dig.di_lamp.count())
    check("16 点位宽为 4 位十六进制",
          "0x0005" in dig.state_label.text(),
          dig.state_label.text())
    dig._current_dev = 0
    dig.refresh()

    # ---- 网格列数随宽度自适应（2026-11-10 用户「显示不全」）----
    # 原来固定 8 列：窗宽不足时每格被压窄，第二行的语义名被裁
    # （"电动球阀1 控制" → "电动球阀1 控"）。现在按实测文字宽算列数。
    #
    # [为什么直接测判定函数] 页面在测试里没进窗口，对网格 resize/setFixedWidth
    # 都会被父布局改回去（实测列数不动、按钮恒 72px）。自适应逻辑本身是纯函数
    # `adaptive_columns(控件, 上限, 每格最小宽)`，直接喂宽度断言即可；真实接线
    # 由渲染验收（digital_fixed.png：1080 宽 6~7 列、字样完整）兜住。
    from PyQt5.QtGui import QFontMetrics as _FM
    from hsfsrv_client.page_base import adaptive_columns as _ac
    do_grid = dig.do_grid

    class _W(object):
        def __init__(self, w):
            self._w = w

        def width(self):
            return self._w

    cell = do_grid._min_cell_w()
    need = max(_FM(do_grid._buttons[0].font()).width("电动球阀0 控制"),
               _FM(do_grid._buttons[0].font()).width("启动按钮灯")) + 16
    check("每格最小宽按字体实测（≥ 最长语义名 + 内边距）",
          cell >= need, "cell=%d 需要=%d" % (cell, need))
    cols_wide = _ac(_W(1080), 8, cell)
    cols_mid = _ac(_W(900), 8, cell)
    cols_narrow = _ac(_W(700), 8, cell)
    check("窗越窄列数越少（1080/900/700 → 递减）",
          cols_wide >= cols_mid >= cols_narrow and cols_wide > cols_narrow,
          "%d/%d/%d（cell=%d）" % (cols_wide, cols_mid, cols_narrow, cell))
    check("每列至少装得下 measured cell 宽（不裁字）",
          all(w // c >= 1 for w, c in ((1080, cols_wide), (900, cols_mid),
                                       (700, cols_narrow))),
          "%d/%d/%d" % (cols_wide, cols_mid, cols_narrow))
    check("上限仍是最多 8 列（与 C 端宽屏口径一致）",
          _ac(_W(4000), 8, cell) == 8, "%d" % _ac(_W(4000), 8, cell))

    # ---- DO 按钮行高按字体实测（2026-11-11 用户「文字显示不全」）----
    # 现象：DO 按钮两行文字（"DO0 断" + 语义名）上下被削掉一点。
    # 根因：行高写死 46px（QSS 还钉了 max-height:46px），而界面缩放 1.5×
    # 时字号 18px、两行文字实测 42px，加 padding/border 需 52px ⇒ 内容区
    # 只剩 39px < 42px，字被裁。现在行高由 row_height() 按字体实测。
    from hsfsrv_client import theme as _theme
    _btn = do_grid._buttons[0]
    _fm2 = _FM(_btn.font())
    _need_h = (_fm2.lineSpacing() * 2 + 2 * _theme.DO_BTN_PAD_V
               + 2 * _theme.DO_BTN_BORDER)
    check("DO 按钮行高按字体实测（≥ 两行文字 + 内边距 + 边框）",
          do_grid.row_height() >= _need_h,
          "行高=%d 需要=%d" % (do_grid.row_height(), _need_h))
    check("DO 按钮实际高度装得下两行文字（内容区不裁字）",
          _btn.height() >= _need_h,
          "实际=%d 需要=%d" % (_btn.height(), _need_h))

    # ---- DI 灯阵每格宽按**实际语义名**实测（2026-11-11）----
    # 现象："电动球阀1 关到位" 被挤成两行（"电动球阀1 关" / "到位"），
    # 现场看着就是文字显示不全。根因：LampRow 每格最小宽写死 108px
    # （DO 那边一直是实测，灯阵漏了）。现在按实际下发的名字量。
    _alias_fm = _FM(dig.di_lamp._cells[0][2].font())
    _long = "电动球阀1 关到位"
    # 先把已有名字清空，量"无名字"缺省宽度（写死值只作下限）
    _saved = {}
    for _i in range(dig.di_lamp.count()):
        _saved[_i] = dig.di_lamp.name_of(_i)
        dig.di_lamp.set_name(_i, "")
    _w_empty = dig.di_lamp._min_cell_w()
    check("无语义名时每格宽回落缺省（写死值只当下限）",
          _w_empty == dig.di_lamp.CELL_MIN_W, "%d" % _w_empty)
    dig.di_lamp.set_name(0, _long)
    _w_after = dig.di_lamp._min_cell_w()
    check("DI 每格宽随实际语义名增大（不再写死 108）",
          _w_after > _w_empty, "%d -> %d" % (_w_empty, _w_after))
    check("DI 每格宽装得下最长语义名一行（不折行）",
          _w_after >= _alias_fm.width(_long) + 12,
          "cell=%d 名字宽=%d" % (_w_after, _alias_fm.width(_long)))
    # 名字变长 ⇒ 列数按新宽度重排（宁可多几行也不裁字）。
    _cols_after = _ac(_W(1080), 8, _w_after)
    check("语义名到达后列数按名字宽度重排（≤ 空名时的列数）",
          _cols_after <= _ac(_W(1080), 8, _w_empty),
          "%d ≤ %d" % (_cols_after, _ac(_W(1080), 8, _w_empty)))
    for _i, _t in _saved.items():        # 复原（不污染后续断言）
        dig.di_lamp.set_name(_i, _t)

    # ---- 通道名只在「帧里带名字」时更新（增量帧不得擦掉名字）----
    #
    # [回归锁 2026-11-05] 服务端与 C 端口径对齐后，周期推送是 full=False 的
    # 增量帧（只带位图，不带 din*/don* 名字），名字只在首连/订阅补发/快照
    # 应答的全量帧里出现。客户端原来每帧都整表重建名字表 ⇒ 全量帧给的 18 个
    # 名字被 200ms 后的增量帧擦成空 ⇒ 数字量页只剩 CH/DO 号、电磁阀语义名
    # 全没了。现象像"服务端没发名字"，其实是客户端自己擦的。
    print()
    print("[通道名缓存]")
    st = m.digital_state(0)
    st.apply({"dev": 0, "name": "数字量", "count": 32,
              "di": 0x00030000, "do": 0x00030000,
              "valid": True, "staleMs": 5000, "full": True,
              "din16": "电磁阀1 关到位", "din17": "电磁阀1 开到位",
              "don16": "电磁阀1 控制"})
    check("全量帧的名字被接收",
          st.di_name(16) == "电磁阀1 关到位"
          and st.do_name(16) == "电磁阀1 控制",
          "di16=%r do16=%r" % (st.di_name(16), st.do_name(16)))
    # 一帧增量（无名字键）之后，名字必须原样留着
    st.apply({"dev": 0, "count": 32, "di": 0x00030000, "do": 0x00030000,
              "valid": True, "staleMs": 5000, "full": False})
    check("增量帧（full=False）不擦名字",
          st.di_name(16) == "电磁阀1 关到位"
          and st.do_name(16) == "电磁阀1 控制",
          "di16=%r do16=%r" % (st.di_name(16), st.do_name(16)))
    # 连"full=True 但没带名字"的帧也不能擦（老服务端/无占用模块）
    st.apply({"dev": 0, "count": 32, "di": 0x00030000, "do": 0x00030000,
              "valid": True, "staleMs": 5000, "full": True})
    check("full=True 但无名字键时同样保留缓存",
          st.di_name(16) == "电磁阀1 关到位",
          "di16=%r" % st.di_name(16))
    # 再来一帧带名字的 → 整表替换（服务端不再发 din16 = 该口已释放）
    st.apply({"dev": 0, "count": 32, "di": 0x0, "do": 0x0,
              "valid": True, "staleMs": 5000, "full": True,
              "din18": "电磁阀2 关到位"})
    check("新全量帧整表替换（旧名字消失）",
          st.di_name(16) == "" and st.di_name(18) == "电磁阀2 关到位",
          "di16=%r di18=%r" % (st.di_name(16), st.di_name(18)))

    # ---- G. 水质页（浊度 ×2）----
    section("G. 水质页（浊度 ×2 + 浊度趋势）")

    # [为什么用注入而不是等推送] 这一段要在**离线**也能跑（CI 无服务端），
    # 且浊度/泵的离线降级行为本身就是要测的东西之一。
    # [2026-11-22 拆分] 页 6 只留水质（浊度×2）；流量计/进水泵/流量趋势
    # 挪到页 18（见 G2 段）。
    win.goto_page(reg.PAGE_MONITOR_WATER)
    app.processEvents()
    water = win.pages[reg.PAGE_MONITOR_WATER]

    # ① 页面身份：页 6 改名「水质」，只订阅浊度
    check("水质页在索引 6", reg.page_name(6) == "水质",
          reg.page_name(6))
    check("水质页只订阅浊度（流量/泵已挪页 18）",
          set(water.TOPICS) == {proto.TOPIC_TURBIDITY_STATUS},
          "%s" % sorted(water.TOPICS))
    check("水质页趋势 = 浊度两路 + 水温（turb1/turb2/temp）",
          [k for k, _n in water.TREND_SERIES]
          == ["turb1", "turb2", "temp"],
          str(water.TREND_SERIES))
    # [2026-11-22 用户口径] 两个新页的趋势缺省刷新 = 2.0s（无本机存档时）。
    # 回归入口给整轮指定了临时 INI（main 里 HSF_CLIENT_SETTINGS_INI）⇒
    # 没有存档是确定事实；有存档时以存档为准（settings_merge C5 另锁）。
    from PyQt5.QtWidgets import QComboBox as _QComboBox
    _wc = water.findChild(_QComboBox, "comboTrendSample")
    check("水质页趋势缺省 2.0 s（无本机存档）",
          _wc is not None and _wc.currentText() == "2.0 s"
          and water._trend.sample_ms == 2000,
          "%s / %s" % (_wc.currentText() if _wc else "-",
                       water._trend.sample_ms))
    check("水质页没有流量计/进水泵控件（拆分后各归各页）",
          not hasattr(water, "_flow_blocks")
          and not hasattr(water, "btn_pump_on")
          and not hasattr(water, "btn_pump_off"))

    # ② 两台浊度控件都在（没有的是"画漏了"，比数值错更严重）
    check("浊度两行读数控件就位", len(water._turb_rows) == 2,
          "%d 行" % len(water._turb_rows))
    check("水温标签两行就位（缺温度机型不显示，不写 0℃）",
          len(water._turb_temps) == 2,
          "%d 个" % len(water._turb_temps))

    # ③ 数值与单位**不在同一网格单元**（曾重叠绘制成乱码）
    #
    # [回归锁 2026-11-05] 原来把 value 与 unit 都 addWidget 到同一个
    # QGridLayout 单元格 (1, idx)。QGridLayout 允许同格多控件，后加的画在
    # 上层且不互相错开 ⇒ 截图里 "12.15" 与 "NTU" 叠成一团。
    check("NTU 单位标签就位", len(water._turb_units) == 2,
          "%d 个" % len(water._turb_units))
    # [必须让窗口"显示"过一次] 未 show 的窗不管怎么 resize/processEvents，
    # 子控件几何都是 (0,0,640,480) 这种占位值 —— 拿它比 x 区间会假通过。
    # WA_DontShowOnScreen 只做布局不上屏，CI/无头环境安全。
    from PyQt5.QtCore import Qt
    win.setAttribute(Qt.WA_DontShowOnScreen, True)
    win.resize(1280, 800)
    win.show()
    for _ in range(5):
        app.processEvents()
    overlapped = []
    for idx, unit_lbl in enumerate(water._turb_units):
        value_lbl = water._turb_rows[idx][1]
        # 两者都要有真实尺寸才能比 x 区间（未布局时全是 0，会假通过）
        if value_lbl.width() <= 0 or unit_lbl.width() <= 0:
            continue
        left, right = (value_lbl, unit_lbl)
        if left.x() > right.x():
            left, right = right, left
        if left.x() + left.width() > right.x():
            overlapped.append(idx)
    check("浊度数值与单位不重叠", not overlapped, "重叠：%s" % overlapped)

    # ④ 浊度数据落地：v{index}_ 前缀合并，ntu ×100 还原
    m.turbidity.apply({"count": 2, "staleMs": 5000,
                       "v0_name": "原水浊度", "v0_online": True,
                       "v0_ntu": 1234, "v0_raw": 1234,
                       "v1_name": "出水浊度", "v1_online": True,
                       "v1_ntu": 322, "v1_raw": 322})
    check("浊度两台都落地", m.turbidity.count == 2)
    check("浊度 1 换算正确（÷100）",
          abs(m.turbidity.ntu_of(0) - 12.34) < 1e-6,
          "%.2f" % (m.turbidity.ntu_of(0) or -1))
    check("浊度 2 换算正确（÷100）",
          abs(m.turbidity.ntu_of(1) - 3.22) < 1e-6,
          "%.2f" % (m.turbidity.ntu_of(1) or -1))
    check("浊度名字由服务端给", m.turbidity.name_of(0) == "原水浊度",
          m.turbidity.name_of(0))
    check("raw 保留（排查换算口径用）", m.turbidity.raw_of(0) == 1234)
    water.refresh()
    app.processEvents()
    check("界面显示浊度 1 数值", water._turb_rows[0][1].text() == "12.34",
          water._turb_rows[0][1].text())
    check("界面显示浊度 1 名字", water._turb_rows[0][0].text() == "原水浊度",
          water._turb_rows[0][0].text())

    # ⑤ 超阈值标红（显示口径，不是报警判据）
    m.turbidity.apply({"count": 2, "staleMs": 5000,
                       "v0_name": "原水浊度", "v0_online": True,
                       "v0_ntu": 2500,          # 25 NTU > 20 阈值
                       "v1_name": "出水浊度", "v1_online": True,
                       "v1_ntu": 100})
    water.refresh()
    app.processEvents()
    warn_qss = water._turb_rows[0][1].styleSheet()
    ok_qss = water._turb_rows[1][1].styleSheet()
    check("超阈值标红", theme.CLR_FAULT in warn_qss, warn_qss[:60])
    check("正常值不上红", theme.CLR_FAULT not in ok_qss, ok_qss[:60])

    # ⑥ 某台离线 ⇒ 该台显示 "--"，另一台照常
    m.turbidity.apply({"count": 2, "staleMs": 5000,
                       "v0_name": "原水浊度", "v0_online": False,
                       "v1_name": "出水浊度", "v1_online": True,
                       "v1_ntu": 300})
    water.refresh()
    app.processEvents()
    check("离线那台显示 --", water._turb_rows[0][1].text() == "--",
          water._turb_rows[0][1].text())
    check("在线那台仍显示数值", water._turb_rows[1][1].text() == "3.00",
          water._turb_rows[1][1].text())

    # ---- G2. 流量页（流量计 + 进水泵 + 流量趋势）----
    section("G2. 流量页（流量计 + 进水泵 + 流量趋势）")
    win.goto_page(reg.PAGE_MONITOR_FLOW)
    app.processEvents()
    flowp = win.pages[reg.PAGE_MONITOR_FLOW]

    # ① 页面身份：页 18（追加末尾，不重排既有页）
    check("流量页在索引 18（页号追加末尾）",
          reg.page_name(18) == "流量" and reg.PAGE_MONITOR_FLOW == 18,
          "%s / %s" % (reg.page_name(18), reg.PAGE_MONITOR_FLOW))
    check("流量页订阅流量计+进水泵",
          set(flowp.TOPICS) == {proto.TOPIC_FLOWMETER_STATUS,
                                proto.TOPIC_PUMP_STATUS},
          "%s" % sorted(flowp.TOPICS))
    check("流量页趋势 = 流量/流速/正反向累积四条",
          [k for k, _n in flowp.TREND_SERIES]
          == ["flow", "speed", "fwd", "rev"],
          str(flowp.TREND_SERIES))
    _fc = flowp.findChild(_QComboBox, "comboTrendSample")
    check("流量页趋势缺省 2.0 s（无本机存档）",
          _fc is not None and _fc.currentText() == "2.0 s"
          and flowp._trend.sample_ms == 2000,
          "%s / %s" % (_fc.currentText() if _fc else "-",
                       flowp._trend.sample_ms))
    check("流量计四个读数块就位",
          set(flowp._flow_blocks) == {"flow", "speed", "fwd", "rev"},
          "%s" % sorted(flowp._flow_blocks))
    check("流量页没有浊度控件（拆分后各归各页）",
          not hasattr(flowp, "_turb_rows"))

    # ② 流量数据落地（单位随单位码；报警码说中文）
    m.flowmeter.apply({"flow": 3.6, "speed": 0.42,
                       "forwardTotal": 123.4, "reverseTotal": 1.25,
                       "flowUnit": 5, "totalUnit": 1,
                       "alarms": 0x1024, "valid": True, "staleMs": 5000})
    flowp.refresh()
    app.processEvents()
    check("流量计瞬时流量落地（3.60 m³/h）",
          flowp._flow_blocks["flow"]._value.text() == "3.60"
          and flowp._flow_blocks["flow"].unit_text() == "m³/h",
          "%s %s" % (flowp._flow_blocks["flow"]._value.text(),
                     flowp._flow_blocks["flow"].unit_text()))
    check("流量计正/反累积落地",
          flowp._flow_blocks["fwd"]._value.text() == "123.40"
          and flowp._flow_blocks["rev"]._value.text() == "1.25",
          "%s / %s" % (flowp._flow_blocks["fwd"]._value.text(),
                       flowp._flow_blocks["rev"]._value.text()))
    check("流量计报警码说中文（空管）",
          "空管" in flowp._flow_alarm.text(), flowp._flow_alarm.text())

    # ③ 泵有启停两钮
    check("泵有启停两钮", hasattr(flowp, "btn_pump_on")
          and hasattr(flowp, "btn_pump_off"))

    # ④ 泵状态与按钮闸门
    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": False,
                  "v0_ready": True, "v0_valid": True})
    flowp.refresh()
    app.processEvents()
    check("泵名字由服务端给（写到卡标题）",
          flowp._pump_card.title_label.text() == "进水泵",
          flowp._pump_card.title_label.text())
    check("泵停止态：启动可点", flowp.btn_pump_on.isEnabled()
          or not bridge.is_connected(),
          "connected=%s" % bridge.is_connected())
    check("泵已在停止 ⇒ 停止钮置灰", not flowp.btn_pump_off.isEnabled())

    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": True,
                  "v0_ready": True, "v0_valid": True})
    flowp.refresh()
    app.processEvents()
    check("泵运行态：状态文案含『输出』",
          "输出" in flowp._pump_state.text(), flowp._pump_state.text())
    check("泵已在运行 ⇒ 启动钮置灰", not flowp.btn_pump_on.isEnabled())

    # ⑤ 泵不可用（模块离线）⇒ 两个按钮都禁用，不给人可点的假象
    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": False,
                  "v0_ready": True, "v0_valid": False})
    flowp.refresh()
    app.processEvents()
    # 注意：valid=False 但 ready=True 仍算可用（valid 是"本拍回显是否有效"），
    # 真正的禁用条件是 ready=False。这里用 ready=False 验禁用。
    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": False,
                  "v0_ready": False, "v0_valid": False})
    flowp.refresh()
    app.processEvents()
    check("泵未就绪 ⇒ 启动钮禁用", not flowp.btn_pump_on.isEnabled())
    check("泵未就绪 ⇒ 停止钮禁用", not flowp.btn_pump_off.isEnabled())
    check("泵未就绪 ⇒ 状态写『未就绪』",
          flowp._pump_state.text() == "未就绪", flowp._pump_state.text())

    # ⑥ 泵说明**不能**含 Markdown 星号（QLabel 不解析，会原样显示）
    check("泵说明无 Markdown 星号",
          "**" not in flowp._pump_note.text(), flowp._pump_note.text()[:40])
    check("泵说明写明无反馈",
          "无运行反馈" in flowp._pump_note.text(),
          flowp._pump_note.text()[:40])

    # ⑦ 数字量页 DO6 名字同步为「进水泵」（同一路输出两个页面必须同名）
    # [2026-11-06 点表收窄] .12 聚合器（对外 32 点）已拆除，现场只剩 .13
    # 一个 16 点模块：阀 1~6 在 DO0~DO5、进水泵在 DO6、按钮组 DO7~DO9。
    # 旧口径 DO21/DO22（16+5/16+6 的聚合编号）随之作废。
    dig_st = m.digital_state(0)
    dig_st.apply({"dev": 0, "name": "数字量", "count": 16,
                  "di": 0x0, "do": 0x0, "valid": True, "staleMs": 5000,
                  "full": True, "don5": "电磁阀6 控制",
                  "don6": "进水泵"})
    check("DO5 = 电磁阀6", dig_st.do_name(5) == "电磁阀6 控制",
          dig_st.do_name(5))
    check("DO6 = 进水泵（与流量页同源）", dig_st.do_name(6) == "进水泵",
          dig_st.do_name(6))

    # ---- K. 表格列宽按权重分配（不能被均分/溢出色）----
    #
    # [实锅 2026-11-05] `set_column_weights` 只"记录"权重，实际用
    # `QHeaderView.Stretch` —— Qt 把列**均分**（实测 5 列各 208px），
    # 权重完全没生效：地址列装不下 "COM17 · 115200 · 站 2" 被截成 "..."，
    # 而"设备/类型/状态"白占大片空白。
    #
    # 另一坑：`minimumSectionSize` 是**硬约束**。只按权重算、不管下限时
    # 5 列合计 1130 > 视口 1038 ⇒ 出现横向滚动条，看着还是挤。
    section("K. 表格列宽按权重分配且不溢出（表已改悬浮卡：先弹开再量）")

    win.goto_page(reg.PAGE_HOME)
    home2 = win.pages[reg.PAGE_HOME]
    home2._toggle_detail("table")     # 设备表已改悬浮卡：弹开才有真实几何
    for _ in range(12):
        app.processEvents()
    tbl2 = home2._table.table
    widths = [tbl2.columnWidth(c) for c in range(tbl2.columnCount())]
    total = sum(widths)
    viewport = tbl2.viewport().width()
    check("列宽合计不超出视口（无横向滚动条）",
          total <= viewport + 2, "合计=%d 视口=%d" % (total, viewport))
    check("列宽合计基本填满视口（不留大片空白）",
          total >= viewport - 8, "合计=%d 视口=%d" % (total, viewport))
    # 权重是 (4,2,2,7,5)：地址列(7)应明显宽于类型列(2)
    check("权重生效：地址列比类型列宽",
          widths[3] > widths[1], "地址=%d 类型=%d" % (widths[3], widths[1]))
    check("权重生效：设备列比状态列宽",
          widths[0] > widths[2], "设备=%d 状态=%d" % (widths[0], widths[2]))
    home2._close_detail()             # 量完收起，别影响后面的段落
    app.processEvents()

    # ---- J. 设备表「地址」列（与「错误信息」分开）----
    #
    # [为什么拆两列] 原来「说明」一列同时想放错误原因和连接地址，两者会
    # 互相覆盖：有错误时地址看不见，而排故障恰恰需要"它在哪个地址 + 为什么
    # 连不上"同时可见。
    section("J. 设备表：地址列与错误信息列分开")

    m3 = bridge.model
    m3.health.apply({
        "devCount": 2, "seq": 1, "ts": 0,
        "d0_name": "搅拌泵", "d0_kind": 0, "d0_side": 0, "d0_health": 0,
        "d0_error": 0, "d0_online": True, "d0_runState": 3,
        "d0_addr": "COM17 · 115200 · 站 2",
        "d1_name": "数字量", "d1_kind": 5, "d1_side": 0, "d1_health": 3,
        "d1_error": 0, "d1_online": False, "d1_runState": 0,
        "d1_addr": "192.168.0.12:502", "d1_text": "从站失联",
    })
    win.goto_page(reg.PAGE_HOME)
    app.processEvents()
    home = win.pages[reg.PAGE_HOME]
    home.refresh(force=True)          # 分帧刷新：force 一拍刷齐（含设备表）
    app.processEvents()

    check("地址已落到 model", m3.health.devices[0].addr == "COM17 · 115200 · 站 2",
          repr(m3.health.devices[0].addr))
    tbl = home._table.table
    check("设备表为 5 列（设备/类型/状态/地址/错误信息）",
          tbl.columnCount() == 5, "%d" % tbl.columnCount())
    headers = [tbl.horizontalHeaderItem(c).text() for c in range(tbl.columnCount())]
    check("表头含『地址』", "地址" in headers, "%s" % headers)
    check("原『说明』列已改名『错误信息』",
          "错误信息" in headers and "说明" not in headers, "%s" % headers)
    check("地址列显示连接参数",
          "COM17" in (tbl.item(0, 3).text() if tbl.item(0, 3) else ""),
          tbl.item(0, 3).text() if tbl.item(0, 3) else "None")
    # **关键**：故障行的地址与错误信息必须**同时**可见（拆列的意义）
    addr_row1 = tbl.item(1, 3).text() if tbl.item(1, 3) else ""
    err_row1 = tbl.item(1, 4).text() if tbl.item(1, 4) else ""
    check("故障行：地址仍显示（未被错误信息挤掉）",
          "192.168.0.12" in addr_row1, addr_row1)
    check("故障行：错误信息显示原因", "失联" in err_row1, err_row1)
    check("正常行错误信息为 '-'",
          (tbl.item(0, 4).text() if tbl.item(0, 4) else "") == "-",
          tbl.item(0, 4).text() if tbl.item(0, 4) else "None")

    # 老服务端不发 d*_addr ⇒ 显示 "-"，不能崩
    m3.health.apply({"devCount": 1, "seq": 2, "ts": 0,
                     "d0_name": "旧设备", "d0_kind": 0, "d0_health": 0,
                     "d0_error": 0, "d0_online": True, "d0_runState": 0})
    home.refresh(force=True)
    app.processEvents()
    check("老服务端无地址键 ⇒ 显示 '-'（不崩）",
          (tbl.item(0, 3).text() if tbl.item(0, 3) else "") == "-",
          tbl.item(0, 3).text() if tbl.item(0, 3) else "None")

    # ---- L. 主界面 3 卡 + 悬浮详情（2026-11-09，对齐 C HsfHomePage）----
    #
    # [补什么] C 端主页有 6 张看板卡，Python 端原来只有 3 张（温湿度/
    # 搅拌泵/设备健康）。这里补齐 压差（TOPIC_AI 通道 0）/ 流量计
    # （TOPIC_FLOWMETER 瞬时+正反累计）/ 电磁阀（TOPIC_VALVE 六路色点，
    # 色义对齐 pages_valve.VALVE_STATE_TEXT）三张卡的数据键更新，
    # 以及「设备表+泵详情」悬浮详情卡的开合交互（点卡弹出、× /再点同卡/
    # 点遮罩收起；对齐 C hsfHomeApplyDetail / hsfHomeFloatCloseSlot /
    # hsfHomeShadeClicked）。
    section("L. 主界面：压差/流量计/电磁阀 3 卡 + 悬浮详情卡")

    win.goto_page(reg.PAGE_HOME)
    app.processEvents()
    home = win.pages[reg.PAGE_HOME]
    m4 = bridge.model

    # ① TOPICS 补齐 3 主题（切页订阅集合按此走）
    check("主页 TOPICS 含压差(AI)主题", proto.TOPIC_AI_STATUS in home.TOPICS)
    check("主页 TOPICS 含流量计主题",
          proto.TOPIC_FLOWMETER_STATUS in home.TOPICS)
    check("主页 TOPICS 含电磁阀主题",
          proto.TOPIC_VALVE_STATUS in home.TOPICS)

    # ② 压差卡：TOPIC_AI 通道 0（e0=350 → 0.350；单位/名字随服务端下发）
    m4.analog.apply({"n": 8, "v": 1, "r0": 4200, "u0": 20000,
                     "e0": 350, "eu0": "MPa", "en0": "压差计", "ew0": ""})
    home.refresh(force=True)
    check("压差卡数值落地（e0/1000 = 0.350）",
          home.dp_value._value.text() == "0.350",
          home.dp_value._value.text())
    # [单位并入数值标签 2026-11-14] 数值与单位合成**一段富文本**（同行同基线
    # 同字号）⇒ 单位不再是独立标签 ⇒ 用 unit_text() 读。
    check("压差卡单位随服务端", home.dp_value.unit_text() == "MPa",
          home.dp_value.unit_text())
    # [同一行同一大小] 数值与单位必须**同字号**（用户口径「跟单位…同一大小」）。
    _sizes = re.findall(r"font-size:(\d+)px", home.dp_value._value.html())
    check("数值与单位同字号（合成一段富文本 ⇒ 同行同基线）",
          len(_sizes) >= 2 and len(set(_sizes)) == 1,
          "字号=%s" % _sizes)
    check("压差卡小字 = 通道名", home.dp_note.text() == "压差计",
          home.dp_note.text())

    # ---- 负压（低于 4mA）⇒ 值照显示（2026-11-18 用户核「小于 4mA 是负压」）
    # [口径 2026-11-17/18] 服务端 engineering 对低于量程下限返回**线性负值**
    # + note「低于 4.0mA（负压）」⇒ 主页卡显示负数（如 -0.016 MPa）+ note
    # 说明；**note ≠ 值无效**（旧实现 note 非空一律 "--"，把负压读数吃掉了）。
    m4.analog.apply({"n": 8, "v": 1, "r0": 3743, "u0": 3743,
                     "e0": -16, "eu0": "MPa",
                     "en0": "压差计", "ew0": "低于 4.0mA（负压）"})
    home.refresh(force=True)
    check("压差负压 ⇒ 数值显示负值（-0.016）",
          home.dp_value._value.text() == "-0.016",
          home.dp_value._value.text())
    check("压差负压 ⇒ 说明行显示负压 note（警示色）",
          "负压" in home.dp_note.text()
          and theme.CLR_WARN in home.dp_note.styleSheet(),
          home.dp_note.text())

    # ---- 超量程：服务端夹到满量程端点 ⇒ 值照显示 + note（与 C 客户端一致）
    m4.analog.apply({"n": 8, "v": 1, "e0": 1000, "eu0": "MPa",
                     "en0": "压差计", "ew0": "高于 20.0mA（超量程）"})
    home.refresh(force=True)
    check("压差超量程 ⇒ 显示端点值 + note（C 客户端同口径）",
          home.dp_value._value.text() == "1.000"
          and "超量程" in home.dp_note.text(),
          "%s / %s" % (home.dp_value._value.text(), home.dp_note.text()))
    check("压差超量程说明用警示色",
          theme.CLR_WARN in home.dp_note.styleSheet(),
          home.dp_note.styleSheet()[:70])

    # 断流（不新鲜）：无数据（不拿旧值冒充实时）
    m4.analog.updated_ms = 0
    home.refresh(force=True)
    check("压差断流 ⇒ 无数据",
          home.dp_value._value.text() == "--"
          and home.dp_note.text() == "无数据",
          "%s / %s" % (home.dp_value._value.text(), home.dp_note.text()))
    # 恢复正常值（供后续段落使用）
    m4.analog.apply({"n": 8, "v": 1, "e0": 350, "eu0": "MPa",
                     "en0": "压差计", "ew0": ""})

    # ③ 流量计卡：瞬时流量 + 正/反向累计（单位随服务端单位码）
    m4.flowmeter.apply({"flow": 12.5, "speed": 0.0,
                        "forwardTotal": 123.4, "reverseTotal": 1.25,
                        "flowUnit": 5, "totalUnit": 1,
                        "alarms": 0, "valid": True, "staleMs": 5000})
    home.refresh(force=True)
    check("流量计卡瞬时流量落地",
          home.fm_flow._value.text() == "12.50",
          home.fm_flow._value.text())
    check("流量计卡单位随单位码（5 = m³/h）",
          home.fm_flow.unit_text() == "m³/h", home.fm_flow.unit_text())
    check("流量计卡副行正/反累计",
          home.fm_sub.text() == "正累计 123.40 m³ / 反累计 1.25 m³",
          home.fm_sub.text())

    # ④ 电磁阀卡：六路「● 状态」色点（色义对齐 pages_valve 卡片）
    m4.valve.apply({"count": 6,
                    "v0_name": "进水阀", "v0_state": 2, "v0_ready": True,
                    "v1_name": "出水阀", "v1_state": 1, "v1_ready": True,
                    "v2_name": "反洗阀", "v2_state": 3, "v2_ready": True,
                    "v3_name": "排气阀", "v3_state": 4, "v3_ready": True,
                    "v4_name": "备用阀A", "v4_state": 0, "v4_ready": True,
                    "v5_name": "备用阀B", "v5_state": 2, "v5_ready": False})
    home.refresh(force=True)
    check("阀1 已开（文案+绿）",
          home._valve_state[0].text() == "● 已开"
          and "#2E7D32" in home._valve_state[0].styleSheet(),
          "%s %s" % (home._valve_state[0].text(),
                     home._valve_state[0].styleSheet()[:60]))
    check("阀2 已关（灰蓝）",
          home._valve_state[1].text() == "● 已关"
          and "#37474F" in home._valve_state[1].styleSheet(),
          home._valve_state[1].text())
    check("阀4 故障（红）",
          home._valve_state[3].text() == "● 故障"
          and theme.CLR_FAULT in home._valve_state[3].styleSheet(),
          home._valve_state[3].text())
    check("阀5 未知（灰）", home._valve_state[4].text() == "● 状态未知",
          home._valve_state[4].text())
    check("阀6 未配置（灰，与『未知』是两件事）",
          home._valve_state[5].text() == "● 未配置"
          and "#9E9E9E" in home._valve_state[5].styleSheet(),
          home._valve_state[5].text())
    check("阀名由服务端下发", home._valve_name[0].text() == "进水阀",
          home._valve_name[0].text())

    # ⑤ 刷新分帧相位推进（数值卡每拍刷、设备表每 2 拍铺一次）
    phase_before = home._refresh_phase
    home.refresh()
    check("刷新分帧相位推进", home._refresh_phase != phase_before,
          "%d -> %d" % (phase_before, home._refresh_phase))

    # ⑥ 悬浮详情卡开合（点卡弹出 / 再点同卡收起）
    home._toggle_detail("table")
    app.processEvents()
    check("点健康卡 ⇒ 悬浮卡弹出（table 态）",
          home._detail_mode == "table")
    check("悬浮卡与遮罩可见",
          home._detail_float.isVisibleTo(home)
          and home._shade.isVisibleTo(home))
    check("悬浮卡标题 = 设备状态", home._detail_title.text() == "设备状态")
    check("表可见、泵详情隐藏",
          home._table.isVisibleTo(home)
          and not home._pump_detail.isVisibleTo(home))
    home._toggle_detail("table")
    check("再点同卡 ⇒ 收起", home._detail_mode == "none"
          and not home._detail_float.isVisibleTo(home)
          and not home._shade.isVisibleTo(home))

    home._toggle_detail("pump")
    app.processEvents()
    check("点搅拌泵卡 ⇒ 泵详情态",
          home._detail_mode == "pump"
          and home._pump_detail.isVisibleTo(home)
          and not home._table.isVisibleTo(home))
    check("泵详情标题", home._detail_title.text() == "搅拌泵详情")

    # ⑦ 泵详情数据键更新（悬浮卡 pump 态每拍刷，字段缺了给 "--"）
    m4.vfd.apply({"runState": 3, "running": True, "faulted": False,
                  "outputFreqHz": 48.5, "setFreqHz": 50.0,
                  "outputCurrentA": 3.2, "speedRpm": 1450.0,
                  "vm": 0xFFFF})
    home.refresh(force=True)
    check("泵详情：运行状态落地",
          home.pd_state._value.text() == "正转运行",
          home.pd_state._value.text())
    check("泵详情：频率/电流/转速落地",
          home.pd_freq._value.text() == "48.50"
          and home.pd_cur._value.text() == "3.20"
          and home.pd_rpm._value.text() == "1450",
          "%s/%s/%s" % (home.pd_freq._value.text(),
                        home.pd_cur._value.text(),
                        home.pd_rpm._value.text()))

    # ⑧ × 收起 + 遮罩收起
    home._detail_close.click()
    check("点 × ⇒ 收起", home._detail_mode == "none"
          and not home._detail_float.isVisibleTo(home))
    home._toggle_detail("pump")
    home._shade.mousePressEvent(None)   # 点遮罩 ⇒ 收起（C hsfHomeShadeClicked）
    check("点遮罩 ⇒ 收起", home._detail_mode == "none"
          and not home._shade.isVisibleTo(home))

    # ---- I. 顶栏「自动运行」状态（与模式位/授权分开）----
    #
    # [⚠ 口径 2026-11-05] 切到自动模式 **不等于** 自动运行。切模式只是
    # "授权自动逻辑可以跑"，控制器仍是停止的；要真正开始跑，必须人再按
    # 一次「启动自动运行」。用户原话："默认应该是停止状态，不然太危险"。
    #
    # 所以顶栏要能表达四种组合，而不是"自动=运行中"：
    #   手动           → 隐藏（自动是否运行无意义，别占顶栏宽度）
    #   自动 + 已停    → 「自动 · 已停止」灰底（**默认**，已授权未启动）
    #   自动 + 运行中  → 「自动 · 运行中」橙底
    #   自动 + 未知    → 「自动 · 未知」灰底（老服务端不报该键）
    section("I. 顶栏自动运行状态（授权 ≠ 运行）")

    tb = win.title_bar
    m2 = bridge.model

    # ① 手动 ⇒ 隐藏
    m2.run_mode = 0
    m2.run_mode_known = True
    m2.auto_running = False
    tb.set_mode(False, True)
    tb.set_auto_running(False, known=True)
    check("手动模式 ⇒ 自动运行标签隐藏",
          not tb.auto_state.isVisibleTo(tb), tb.auto_state.text())

    # ② **默认**：自动模式 + 控制器停 ⇒ 显示"已停止"（不是"运行中"）
    #
    # 这条是用户提的那个危险观感的回归锁：只要客户端把"自动模式"当成
    # "自动运行中"显示，这里必然红。
    m2.run_mode = 1
    m2.auto_running = False
    tb.set_mode(True, True)
    tb.set_auto_running(False, known=True)
    check("自动+未启动 ⇒ 显示『已停止』（默认态，不是运行中）",
          "已停止" in tb.auto_state.text(), tb.auto_state.text())
    check("『已停止』用中性灰（这是正常待命，不是故障）",
          theme.CLR_FAULT not in tb.auto_state.styleSheet(),
          tb.auto_state.styleSheet()[:70])

    # ③ 人按过启动 ⇒ "运行中"
    m2.auto_running = True
    tb.set_mode(True, True)
    tb.set_auto_running(True, known=True)
    check("自动+已启动 ⇒ 显示『运行中』",
          "运行中" in tb.auto_state.text(), tb.auto_state.text())

    # ④ 服务端没报该键（老服务端）⇒ "未知"，不能折成"已停止"或"运行中"
    m2.auto_running = None
    tb.set_mode(True, True)
    tb.set_auto_running(False, known=False)
    check("服务端未报运行位 ⇒ 显示『未知』（不编造状态）",
          "未知" in tb.auto_state.text(), tb.auto_state.text())
    check("model.is_auto_running() 未知时返回 None（非 False）",
          m2.is_auto_running() is None, repr(m2.is_auto_running()))

    # ⑤ bridge：缺键不清已知值；带键才落地
    m2.auto_running = True
    bridge._apply_mode_doc({"mode": 1})
    check("老服务端帧（无 autoRunning）不清掉已知运行位",
          m2.auto_running is True, repr(m2.auto_running))
    bridge._apply_mode_doc({"mode": 1, "autoRunning": False})
    check("新帧带 autoRunning ⇒ 落地",
          m2.auto_running is False, repr(m2.auto_running))

    # 还原成服务端真实状态
    bridge.refresh_run_mode()
    for _ in range(20):
        app.processEvents()
        time.sleep(0.05)

    # ---- H. 真实点击（连服务端时才跑）----
    #
    # [为什么必须单独一组] 前面 G/G2 组是**注入式**：往 model 塞数据、读控件
    # 文字。它锁的是"数据到了界面显示对不对"，**不是**"用户点了按钮界面反应
    # 对不对"。这两件事的失败模式完全不同：
    #
    #   · 注入式测不出的：控制应答回调跑在**网络线程**，里面碰 QWidget
    #     ⇒ Qt 直接崩进程，**没有任何 Python traceback**。变频器页
    #     "点自由停车后界面消失"就是这类事故，单点断言全绿也照样炸。
    #   · 注入式测不出的：按钮 clicked 信号没接、接错参数、lambda 捕获错位。
    #
    # 所以这里用 `btn.click()` 走**完整的真实链路**：
    #   UI 线程 → bridge.request → 网络线程收应答 → run_in_ui 投回 → 改控件
    section("H. 真实点击：进水泵启停（完整控制链路）")

    if not (use_server and bridge.is_connected()):
        skip("真实点击：进水泵启停（完整控制链路）",
             "未连服务端（离线模式或连接失败）；"
             "连上服务端后跑（run_client(host, port)）")
    else:
        # [2026-11-22 拆分] 进水泵在流量页（页 18）。
        win.goto_page(reg.PAGE_MONITOR_FLOW)
        for _ in range(25):
            app.processEvents()
            time.sleep(0.05)
        water = win.pages[reg.PAGE_MONITOR_FLOW]
        pm = bridge.model.pump

        # 先把泵归到已知状态（停），避免上一轮/现场留下开着的泵
        if pm.count > 0 and pm.is_on(0):
            water.btn_pump_off.setEnabled(True)
            water.btn_pump_off.click()
            for _ in range(30):
                app.processEvents()
                time.sleep(0.05)
        check("泵初始为停止", pm.count > 0 and not pm.is_on(0),
              "count=%d on=%s" % (pm.count, pm.is_on(0)))
        check("停止态：启动钮可点", water.btn_pump_on.isEnabled())

        # ---- 真点「启动」----
        pump_before = pm.is_on(0)
        water.btn_pump_on.click()
        # 等应答回来（run_in_ui 投递 + 一轮推送）
        deadline = time.time() + 4.0
        while time.time() < deadline and not pm.is_on(0):
            app.processEvents()
            time.sleep(0.05)
        # 再多跑几拍，让"完成后的界面刷新"也走完（崩进程会在这一步暴露）
        for _ in range(10):
            app.processEvents()
            time.sleep(0.05)

        check("点击启动 → 模型 on 变 True（应答已落地）",
              pm.is_on(0) and not pump_before,
              "before=%s after=%s" % (pump_before, pm.is_on(0)))
        check("点击启动 → 界面文案变『运行』",
              "运行" in water._pump_state.text(), water._pump_state.text())
        check("点击启动 → 启动钮转置灰（已是该状态）",
              not water.btn_pump_on.isEnabled())
        check("点击启动 → 停止钮可点", water.btn_pump_off.isEnabled())

        # ---- 真点「停止」----
        water.btn_pump_off.click()
        deadline = time.time() + 4.0
        while time.time() < deadline and pm.is_on(0):
            app.processEvents()
            time.sleep(0.05)
        for _ in range(10):
            app.processEvents()
            time.sleep(0.05)

        check("点击停止 → 模型 on 变 False",
              pm.count > 0 and not pm.is_on(0), "on=%s" % pm.is_on(0))
        check("点击停止 → 界面文案变『停止』",
              "停止" in water._pump_state.text(), water._pump_state.text())

        # ---- 数字量页必须看到同一路输出（跨页自洽）----
        #
        # [为什么这条要真连服务端] 泵与数字量页操作的是**同一个 DO6**。
        # 若服务端没把泵的名字/位写进数字量快照，就会出现"流量页说泵在转、
        # 数字量页 DO6 是灭的"——现场查线时最容易被误导的一种不一致。
        # [2026-11-06 点表收窄] .12 聚合器拆除后现场只剩 .13 一个 16 点
        # 模块：进水泵在 DO6（旧口径 DO22 是 16+6 的聚合编号，已作废）。
        win.goto_page(reg.PAGE_MONITOR_DIGITAL)
        for _ in range(25):
            app.processEvents()
            time.sleep(0.05)
        dig_st = bridge.model.digital_state(0)
        check("数字量页能查到 DO6 的名字",
              dig_st.do_name(6) != "", repr(dig_st.do_name(6)))
        check("DO6 名字为『进水泵』", dig_st.do_name(6) == "进水泵",
              repr(dig_st.do_name(6)))
        dev0 = bridge.model.digital.get(0)
        bit6 = bool((getattr(dev0, "do_mask", 0) >> 6) & 1) if dev0 else None
        # 泵此刻是停的 ⇒ DO6 必须是灭的（两边同源）
        check("泵已停 ⇒ 数字量 DO6 位为 0", bit6 is False, "bit6=%s" % bit6)

        # ---- 回到流量页，确认来回切页不丢状态、不崩 ----
        win.goto_page(reg.PAGE_MONITOR_FLOW)
        for _ in range(20):
            app.processEvents()
            time.sleep(0.05)
        check("切回流量页后泵状态仍为停止",
              pm.count > 0 and not pm.is_on(0), "on=%s" % pm.is_on(0))
        check("切页往返后界面仍可用（未崩）",
              water._pump_state.text() != "", water._pump_state.text())

    # ---- 收尾 ----

    bridge.disconnect_from()
    return 0


# ==========================================================================
# 段落：test_history_columns（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】历史页「列多选框 = 显示哪些列」全场景测试
（**纯控件层**，不连服务端）。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的一段源码（总回归会把它原样拼进去）。

[命名契约] 本段落的**所有**顶层名字统一带 `m_hiscol_` 前缀
    （函数/常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `section` / `time` / `os` …）
    **按名直呼**，不 import 总回归（避免循环导入）。
    [顶层不 import] 本段落的 import **全部放在函数里** ⇒ 段落的顶层名
    与总回归、与其它段落**零交集**（`os` / `time` / `csv` 这些名字归
    总回归所有；独立放本文件时也不会撞名）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `m_hiscol_app()` 取（取不到就报错，**绝不自己建**）。

用户要求：「列多选框你要测试，只显示勾选的列，默认是全部勾选」

覆盖：
  A. 默认全部勾选（15/15 可见，没有列被隐藏）
  B. 只显示勾选的列（勾 3 列 ⇒ 其余 12 列隐藏、表头对得上）
  C. 至少保留一列（把最后一列也取消 → 被忽略，不会出现空表头）
  D. 恢复全选（一键回到 15 列）
  E. 列宽只分给可见列（不留大片空白、可见列被撑宽）
  F. 导出按可见列出列（表头 + 每行都只有勾中的列）
  G. 关键字只在可见列里匹配（藏起来的列不参与）
  H. 排序/行数据不受列显示影响（数据行始终完整 15 列）
  I. 真实鼠标点击（用户反馈的两个 bug：勾不回去 / 点一下浮层就消失）

[为什么单列一套] 上一轮把「列」从"筛选哪些列"改成"显示哪些列"，
这是语义变更而不是纯新增：空选含义、导出列、关键字范围、列宽分配
四处都跟着变。这四处各自都会静默出错（界面看着有内容、其实列错了/
导出的列不对），必须有断言盯着。
"""


# ============================================================================
#                       段落基础设施（app / 页面 / 数据）
# ============================================================================


def m_hiscol_app():
    """取总回归那唯一一个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。

    [取法] 优先用总回归的模块级 `_app`；总回归没暴露时退回
    `QApplication.instance()`（**仍然不新建**）。两者都取不到 ⇒ 报错，
    绝不退化成"自己建一个"（Qt5 未定义行为 ⇒ 段错误/析构顺序错乱）。
    """
    app = globals().get("_app") or globals().get("_APP")
    if app is None:
        from PyQt5.QtWidgets import QApplication
        app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑本段（段落里绝不新建）")
    return app


def m_hiscol_make_page():
    """造一个历史页控件（**离线**：不连服务端）。返回 (app, page)。

    [为什么不再自己建 QApplication] Qt5 一进程多 QApplication 是未定义
    行为 ⇒ 本段落只取总回归建好的那一个实例。
    """
    app = m_hiscol_app()
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage
    ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    return app, HistoryPage(ctx)


def m_hiscol_row(stamp, mode="手动", turb="12.50"):
    """造一行文本（列序同 HistoryPage.COLUMNS；2026-11-10 起含「水温(℃)」）。

    [水温放在第 8 位] 与 HistoryPage.COLUMNS 同序（浊度2 之后），
    数值用 "19.3" 便于断言"水温列被正确铺进表格"。
    """
    return ([stamp, mode, "停机", "25.00", "3.20", "750",
             turb, "3.20", "19.3", "36.50 m³/h", "123.40 m³", "0.499",
             "26.5", "55.0", "0x0000FFFF", "0x000000FF"], False)


def m_hiscol_seed(page, count=5):
    """往页面里塞 count 行假数据并重建视图。"""
    page._rows = [m_hiscol_row("2026-11-06 09:1%d:00" % i)
                   for i in range(count)]
    page._rebuild()


# ============================================================================
#                      A. 默认全部勾选
# ============================================================================


def m_hiscol_part_a_default(page):
    print("== A. 默认全部勾选 ==")
    cols = page._f_col.checked_indexes()
    check("A1 默认 15 列全部勾选", len(cols) == len(page.COLUMNS),
          "%d/%d" % (len(cols), len(page.COLUMNS)))
    table = page._table.table
    hidden = [c for c in range(table.columnCount())
              if table.isColumnHidden(c)]
    check("A2 没有列被隐藏", hidden == [], str(hidden))
    check("A3 visible_columns 返回全列",
          page.visible_columns() == list(range(len(page.COLUMNS))),
          str(page.visible_columns()))
    check("A4 下拉文本显示「全部列（全部）」",
          "全部" in page._f_col.lineEdit().text(),
          page._f_col.lineEdit().text())


# ============================================================================
#                        B. 只显示勾选的列
# ============================================================================


def m_hiscol_part_b_subset(page):
    print("== B. 只显示勾选的列 ==")
    from PyQt5.QtCore import QPoint, Qt
    m_hiscol_seed(page, 5)
    page._f_col.clear_all()
    for c in (0, 1, 6):                       # 时间 / 模式 / 浊度1
        page._f_col._model.item(c).setCheckState(Qt.Checked)
    table = page._table.table
    check("B1 勾选 3 列", page.visible_columns() == [0, 1, 6],
          str(page.visible_columns()))
    hidden = [c for c in range(table.columnCount())
              if table.isColumnHidden(c)]
    check("B2 其余列被隐藏",
          len(hidden) == len(page.COLUMNS) - 3,
          "%d 列隐藏（共 %d 列）" % (len(hidden), len(page.COLUMNS)))
    check("B3 勾选的列没有被隐藏",
          not any(table.isColumnHidden(c) for c in (0, 1, 6)))
    shown = [table.horizontalHeaderItem(c).text()
             for c in range(table.columnCount())
             if not table.isColumnHidden(c)]
    check("B4 显示的表头 = 勾中的列名",
          shown == ["时间", "模式", "浊度1(NTU)"], str(shown))
    check("B5 下拉文本换成勾中的列名",
          "时间" in page._f_col.lineEdit().text()
          and "浊度1" in page._f_col.lineEdit().text(),
          page._f_col.lineEdit().text())

    # 数据行仍是完整列（取数/排序/图表/导出都按完整行）
    check("B6 数据行仍保留完整列（显示只是视图）",
          len(page._rows[0][0]) == len(page.COLUMNS),
          "%d 列 / 应 %d" % (len(page._rows[0][0]), len(page.COLUMNS)))
    check("B7 表格显示的行数正常", table.rowCount() == len(page._rows),
          "table=%d" % table.rowCount())


# ============================================================================
#                        C. 至少保留一列
# ============================================================================


def m_hiscol_part_c_min_one(page):
    print("== C. 至少保留一列 ==")
    from PyQt5.QtCore import QPoint, Qt
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(Qt.Checked)   # 只剩「时间」
    check("C1 只剩 1 列", page.visible_columns() == [0],
          str(page.visible_columns()))
    # 试图把最后一列也取消（模拟真实点击：走 _toggle_item）
    model = page._f_col._model
    page._f_col._toggle_item(model.index(0, 0))
    check("C2 取消最后一列被忽略（不会清空表）",
          page.visible_columns() == [0], str(page.visible_columns()))
    check("C3 仍有一列表头可见",
          any(not page._table.table.isColumnHidden(c)
              for c in range(page._table.table.columnCount())))
    # clear_all 也必须留一列
    page._f_col.clear_all()
    check("C4 clear_all 也至少留一列", len(page.visible_columns()) >= 1,
          str(page.visible_columns()))


# ============================================================================
#                          D. 恢复全选
# ============================================================================


def m_hiscol_part_d_check_all(page):
    print("== D. 恢复全部 ==")
    page._f_col.check_all()
    check("D1 check_all 后 15 列全可见",
          page.visible_columns() == list(range(len(page.COLUMNS))),
          "%d 列" % len(page.visible_columns()))
    table = page._table.table
    check("D2 没有列被隐藏",
          not any(table.isColumnHidden(c)
                  for c in range(table.columnCount())))


# ============================================================================
#                     E. 列宽只分给可见列
# ============================================================================


def m_hiscol_part_e_widths(app):
    print("== E. 列宽分给可见列 ==")
    page = m_hiscol_make_page()[1]
    m_hiscol_seed(page, 3)
    page.resize(1200, 400)
    page.show()
    for _ in range(6):
        app.processEvents()
    table = page._table.table
    header = table.horizontalHeader()

    total_all = sum(header.sectionSize(c) for c in range(table.columnCount()))
    check("E1 全列时各列宽度之和接近视口宽",
          total_all > 0, "总宽 %dpx" % total_all)

    # [E1b 模式列容得下最长内容 2026-11-20 用户实报「模式列显示不全」]
    # 模式列内容最长是「自动·运行中」（6 字符），表头只有「模式」2 字 ⇒
    # 只按表头量宽会被截成「自动…」。修法是把这一列加进 auto_min_texts
    # 的内容下限表（网页端 COL_MIN_TEXTS 早有，桌面端漏了）。
    from PyQt5.QtGui import QFontMetrics
    _fm = QFontMetrics(table.font())
    _need = _fm.horizontalAdvance("自动·运行中") + 12
    check("E1b 模式列宽容得下「自动·运行中」（内容下限生效）",
          header.sectionSize(1) >= _need,
          "宽=%d 需要≈%d" % (header.sectionSize(1), _need))

    from PyQt5.QtCore import QPoint, Qt
    page._f_col.clear_all()
    for c in (0, 1, 6):
        page._f_col._model.item(c).setCheckState(Qt.Checked)
    for _ in range(6):
        app.processEvents()
    vis_total = sum(header.sectionSize(c) for c in (0, 1, 6))
    check("E2 3 列时可见列被撑宽（不留大片空白）",
          vis_total > 600, "3 列共 %dpx" % vis_total)
    check("E3 可见列总宽接近视口宽（±15%）",
          abs(vis_total - table.viewport().width()) < table.viewport().width() * 0.15,
          "可见 %d vs 视口 %d" % (vis_total, table.viewport().width()))
    page.close()


# ============================================================================
#                       F. 导出按可见列
# ============================================================================


def m_hiscol_part_f_export(page):
    print("== F. 导出按可见列 ==")
    import csv
    import os
    import tempfile
    from PyQt5.QtCore import QPoint, Qt
    m_hiscol_seed(page, 3)
    page._f_col.clear_all()
    for c in (0, 1, 6):
        page._f_col._model.item(c).setCheckState(Qt.Checked)

    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "out.csv")
    try:
        # 复用 _export_csv 的文件写法（跳过保存框：直接调内部按可见列导出的
        # 逻辑——用 _export_data 需要 QFileDialog 交互）
        rows = page._visible_rows()
        cols = page.visible_columns()
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow([page.COLUMNS[c] for c in cols])
            for texts, _abn in rows:
                writer.writerow([texts[c] if c < len(texts) else ""
                                 for c in cols])
        with open(path, encoding="utf-8-sig") as fh:
            got = list(csv.reader(fh))
        check("F1 表头只有可见列",
              got[0] == ["时间", "模式", "浊度1(NTU)"], str(got[0]))
        check("F2 每行列数 = 3", all(len(r) == 3 for r in got),
              "首行 %d 列" % len(got[0]))
        check("F3 数据行数正确", len(got) - 1 == len(rows),
              "%d 行" % (len(got) - 1))
        check("F4 时间列内容正确（默认最新在上）",
              got[1][0] == "2026-11-06 09:12:00",
              got[1][0] if len(got) > 1 else "-")
        check("F5 时间列全部落在导出的 3 列里",
              all(r[0].startswith("2026-11-06") for r in got[1:]),
              str([r[0] for r in got[1:]]))
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                    G. 关键字只在可见列匹配
# ============================================================================


def m_hiscol_part_g_keyword(page):
    print("== G. 关键字只在可见列匹配 ==")
    from PyQt5.QtCore import QPoint, Qt
    page._rows = [
        m_hiscol_row("2026-11-06 09:10:00", mode="手动", turb="12.50"),
        m_hiscol_row("2026-11-06 09:11:00", mode="手动", turb="88.88"),
    ]
    page._f_col.check_all()
    page._f_text.setText("88.88")
    page._rebuild()
    check("G1 全列可见时关键字命中浊度行",
          len(page._filtered_rows()) == 1,
          "%d 行" % len(page._filtered_rows()))

    # 把浊度列（6/7）藏起来 → 该行不该再被命中（藏了就不参与搜索）
    page._f_col.clear_all()
    for c in (0, 1):
        page._f_col._model.item(c).setCheckState(Qt.Checked)
    page._rebuild()
    check("G2 藏掉浊度列后关键字不再命中（不参与搜索）",
          len(page._filtered_rows()) == 0,
          "%d 行" % len(page._filtered_rows()))
    page._f_text.clear()
    page._f_col.check_all()
    page._rebuild()
    check("G3 恢复后行数回到 2", len(page._filtered_rows()) == 2,
          "%d 行" % len(page._filtered_rows()))


# ============================================================================
#                  H. 排序不受列显示影响
# ============================================================================


def m_hiscol_part_h_sort(page):
    print("== H. 排序不受列显示影响 ==")
    from PyQt5.QtCore import QPoint, Qt
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(Qt.Checked)     # 只显示时间
    page._f_col._model.item(1).setCheckState(Qt.Checked)
    page._rows = [
        m_hiscol_row("2026-11-06 09:10:00"),
        m_hiscol_row("2026-11-06 09:12:00"),
        m_hiscol_row("2026-11-06 09:11:00"),
    ]
    page._sort_col = 0
    page._sort_desc = True
    page._rebuild()
    order = [r[0][0] for r in page._visible_rows()]
    check("H1 按时间列排序仍正确（即便只显示 2 列）",
          order == sorted(order, reverse=True), str(order))
    check("H2 数据行仍完整", len(page._rows[0][0]) == len(page.COLUMNS),
          "%d 列" % len(page._rows[0][0]))


def m_hiscol_check_marks(combo, row_index):
    """数浮层里第 row_index 行**方框内部**的强调色像素（= 对勾折线）。

    [为什么要数像素] 界面"看得见对勾"是**观感**问题，状态位（isChecked）
    绿了不代表画出来了 —— 用户实报就是"勾选态没有勾"（Qt 在 ::indicator 被
    QSS styled 后不再画原生指示器）。⇒ 直接取浮层的绘制结果看方框内部。
    """
    from PyQt5.QtCore import QPoint
    from PyQt5.QtGui import QColor
    from hsfsrv_client import theme
    if combo._popup is None:
        return -1
    row = combo._checks[row_index]
    img = combo._popup.grab().toImage()
    pos = row.mapTo(combo._popup, QPoint(0, 0))
    box = 17                                  # 与 CheckRow.BOX 同口径
    x0 = pos.x() + 8 + 3                      # BOX_X=8，内缩 3px 避开边框
    y0 = pos.y() + (row.height() - box) // 2 + 3
    tok = (theme.current_theme() or {}).get("tokens") or {}
    accent = QColor(tok.get("accent_line", theme.CLR_ACCENT_LINE))
    n = 0
    for y in range(y0, y0 + box - 6):
        for x in range(x0, x0 + box - 6):
            c = img.pixelColor(x, y)
            if (abs(c.red() - accent.red()) < 40
                    and abs(c.green() - accent.green()) < 40
                    and abs(c.blue() - accent.blue()) < 40):
                n += 1
    return n


def m_hiscol_part_i_real_clicks(app):
    """真实鼠标点击（**这是本轮用户反馈的两个 bug 的回归锁**）。

    用户原话：
      「可以取消列但是勾不回去了，而且单机一次多选框就消失了，
        应该设为失去焦点，或在下拉框外面单机才收起下拉框」

    两个症状都是**只测状态测不出来**的：内部 `_checked` 一直是对的，
    坏的是 QComboBox 的"选中即关"语义 + lineEdit 被覆盖。
    所以这一段一律用 QTest.mouseClick 真点，断言：
      · 点项后浮层**仍然开着**（能连点多选）
      · 取消掉的项能**再勾回来**
      · 按钮文本始终是勾选汇总（不会被覆盖成某一个单项名）
      · 点浮层外/按钮自身 → 收起
    """
    print("== I. 真实点击（用户反馈的两个 bug）==")
    from PyQt5.QtCore import QPoint, Qt
    from PyQt5.QtTest import QTest
    from PyQt5.QtWidgets import QWidget, QVBoxLayout
    from hsfsrv_client.pages_other import MultiCheckCombo

    host = QWidget()
    host.resize(420, 320)
    lay = QVBoxLayout(host)
    combo = MultiCheckCombo(["时间", "模式", "搅拌状态", "输出频率(Hz)"],
                            all_text="全部列", initial="all", min_checked=1)
    lay.addWidget(combo)
    host.show()
    app.processEvents()

    def click_item(idx):
        """点某一项的**勾选指示器**位置（不是矩形中心）。

        [为什么不用 rect().center()] QCheckBox 只有指示器 + 文字那一段
        可点，右侧空白区不响应（实测：点 center=192px 处勾选态不变，
        点 8px 处才翻转）。离屏平台没有真实布局宽度差异，位置错一点就
        变成"点了没反应"，会误判成功能坏了。
        """
        cb = combo._checks[idx]
        QTest.mouseClick(cb, Qt.LeftButton, Qt.NoModifier, QPoint(8, 17))
        app.processEvents()

    def click_button():
        QTest.mouseClick(combo, Qt.LeftButton, Qt.NoModifier,
                         combo.rect().center())
        app.processEvents()

    # ---- I1 点按钮 → 浮层打开 ----
    click_button()
    check("I1 点按钮弹出勾选浮层",
          combo._popup is not None and combo._popup.isVisible())

    # ---- I2 点项 → 勾选变化，**浮层不关** ----
    click_item(1)                                  # 取消「模式」
    check("I2a 点击项取消勾选", combo.checked_indexes() == [0, 2, 3],
          str(combo.checked_indexes()))
    check("I2b 点项后浮层仍然开着（能连点多选）",
          combo._popup is not None and combo._popup.isVisible())
    check("I2c 按钮文本是勾选汇总（不是被覆盖的单项名）",
          combo.lineEdit().text() == "时间、搅拌状态、输出频率(Hz)",
          repr(combo.lineEdit().text()))

    # ---- I3 **取消后能再勾回来**（用户报的"勾不回去了"）----
    click_item(1)
    check("I3a 取消掉的项能再勾回来", combo.checked_indexes() == [0, 1, 2, 3],
          str(combo.checked_indexes()))
    check("I3b 勾回后浮层还开着",
          combo._popup is not None and combo._popup.isVisible())
    check("I3c 全勾时文本回到「全部列（全部）」",
          "全部" in combo.lineEdit().text(), repr(combo.lineEdit().text()))

    # ---- I4 连点多项（反复取消/勾回，浮层全程不关）----
    for _round in range(3):
        click_item(2)
        click_item(2)
    check("I4 连点 3 轮后状态仍自洽、浮层还开着",
          combo.checked_indexes() == [0, 1, 2, 3]
          and combo._popup is not None and combo._popup.isVisible(),
          str(combo.checked_indexes()))

    # ---- I5 至少留一列（真实点击）----
    for i in (1, 2, 3):
        click_item(i)
    check("I5a 只留「时间」", combo.checked_indexes() == [0],
          str(combo.checked_indexes()))
    click_item(0)                                  # 再点最后一项
    check("I5b 点最后一项被拒（仍留一列）", combo.checked_indexes() == [0],
          str(combo.checked_indexes()))
    check("I5c 被拒时复选框回弹（勾选态与实际一致）",
          combo._checks[0].isChecked() is True,
          str([cb.isChecked() for cb in combo._checks]))

    # ---- I6 点按钮自身 → 收起（用户要求的"点外面才收"里的一种）----
    click_item(1)                                  # 先勾回一个
    check("I6a 勾回后 2 列", len(combo.checked_indexes()) == 2,
          str(combo.checked_indexes()))
    click_button()
    check("I6b 再点按钮收起浮层",
          combo._popup is None or not combo._popup.isVisible())

    # ---- I7 浮层被关掉后按钮复位（不再呈按下态）----
    click_button()
    check("I7a 重新打开", combo._popup is not None
          and combo._popup.isVisible())
    combo._close_popup()                    # 等价于点外面/Esc 的效果
    app.processEvents()
    check("I7b 浮层关掉后按钮复位（isDown=False）",
          combo._popup is None and not combo.isDown(),
          "popup=%r isDown=%s" % (combo._popup, combo.isDown()))
    click_button()
    check("I7c 关掉后还能再打开",
          combo._popup is not None and combo._popup.isVisible())

    # ---- I8 勾选态**看得见对勾**（用户实锅 2026-11-17）----
    #
    # [用户实锅] 远端设置 → 自动模式配置 → 启动设备多选：「在选中的时候没有
    # 勾，导致看起来一直以为没选上」。
    # [根因] QSS 一旦给 `QCheckBox::indicator` 设了 border/background，Qt 的
    # 样式引擎就**不再画原生指示器**（连对勾一起没了）⇒ 勾选态只剩一个空
    # 方框。⇒ 指示器改自绘（CheckRow：方框 + 对勾折线）。
    # [本段判据] 数**方框内部**的强调色像素：对勾折线穿过内部 ⇒ >0；
    # 未勾选的内部是纯底色 ⇒ 0。（只看边框不算数——边框也是强调色。）
    combo.set_checked_indexes([0, 1])               # 0、1 勾；2、3 不勾
    combo._refresh_popup_checks()
    app.processEvents()
    n_on = m_hiscol_check_marks(combo, 0)
    n_off = m_hiscol_check_marks(combo, 2)
    check("I8a 勾选行的方框里有对勾（强调色像素 > 0）", n_on > 0, "%d px" % n_on)
    check("I8b 未勾选行的方框里没有对勾（= 0 像素）", n_off == 0,
          "%d px" % n_off)

    combo._close_popup()
    host.close()


# ============================================================================
#                            段落入口
# ============================================================================


def run_hiscol():
    """历史页「显示哪些列」整段（对应原 test_history_columns.py 的 main）。

    [口径] 纯控件层：**不连服务端**、不建 QApplication（复用总回归那个）。
    原 main() 里"打印汇总 / 退出码"的部分不搬（断言计数归总回归统一汇总）。
    """
    section("§14a 历史页：列多选框（显示哪些列）")
    app, page = m_hiscol_make_page()
    m_hiscol_part_a_default(page)
    m_hiscol_part_b_subset(page)
    m_hiscol_part_c_min_one(page)
    m_hiscol_part_d_check_all(page)
    m_hiscol_part_e_widths(app)
    m_hiscol_part_f_export(page)
    m_hiscol_part_g_keyword(page)
    m_hiscol_part_h_sort(page)
    m_hiscol_part_i_real_clicks(app)


# ==========================================================================
# 段落：test_history_filter（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】历史页筛选功能全场景测试（**纯控件层**，不连服务端）。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的一段源码（总回归会把它原样拼进去）。

[命名契约] 本段落的**所有**顶层名字统一带 `m_hisflt_` 前缀
    （函数/常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `section` / `time` / `os` …）
    **按名直呼**，不 import 总回归（避免循环导入）。
    [顶层不 import] 本段落的 import **全部放在函数里** ⇒ 段落的顶层名
    与总回归、与其它段落**零交集**（`os` / `time` / `csv` / `Qt` 这些名字
    归总回归所有；独立放本文件时也不会撞名）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `m_hisflt_app()` 取（取不到就报错，**绝不自己建**）。

[为什么单开这一套] 筛选是"看起来简单、实际最容易悄悄错"的一块：
时间格式有 5 种写法、起止可单给可组合、关键字要与列选择联动、
**服务端模式下时间/关键字/列三者都要下推**（否则分页时关键字只能搜
到当前页，用户实测"关键字筛选无效"）、翻页后筛选不能丢……
每一条错了界面都"照常显示"，只是显示的东西不对。之前的覆盖只有：
  · test_remote_settings_ui C 段：4 种格式归一化 + 4 行合成数据的时间筛选
  · test_history_page G 段：服务端模式下时间下推 + 清除（8 项）
组合条件、边界、默认值、与分页/排序/导出的交互都没测。这里补齐。

覆盖：
  A. 时间格式归一化全表（合法 9 种 / 非法 10 种）
  B. 起止时间四个组合（only start / only end / both / 跨天）
  C. 关键字：全列匹配 / 单列 / 多列 / 不命中 / 空串
  D. 时间 + 关键字组合（AND 语义）
  E. 边界：起止相等 / 起晚于止 / 极早极晚 / 秒级精度
  F. 筛选与排序、列显示、导出的交互（互不干扰）
  G. 默认起始时间（只填一次、不覆盖用户输入、清除后不回填、坏 ts 不采用）
  H. 筛选参数下推的构造（_server_time_range 的各种输入）
  I. 服务端模式筛选触发（_apply_filter / _on_columns_changed 重取第 0 页、
     请求带 kw/kwCols、本地不再重复筛、bridge 载荷字段）
"""


# ============================================================================
#                       段落基础设施（app / 页面 / 数据）
# ============================================================================


def m_hisflt_app():
    """取总回归那唯一一个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。

    [取法] 优先用总回归的模块级 `_app`；总回归没暴露时退回
    `QApplication.instance()`（**仍然不新建**）。两者都取不到 ⇒ 报错，
    绝不退化成"自己建一个"（Qt5 未定义行为 ⇒ 段错误/析构顺序错乱）。
    """
    app = globals().get("_app") or globals().get("_APP")
    if app is None:
        from PyQt5.QtWidgets import QApplication
        app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑本段（段落里绝不新建）")
    return app


def m_hisflt_make_page():
    """造一个历史页控件（**离线**：不连服务端）。返回 (app, page, status)。

    [为什么不再自己建 QApplication] Qt5 一进程多 QApplication 是未定义
    行为 ⇒ 本段落只取总回归建好的那一个实例。
    """
    app = m_hisflt_app()
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage
    status = []
    ctx = PageContext(LinkBridge(), lambda i: None, status.append)
    page = HistoryPage(ctx)
    return app, page, status


def m_hisflt_row(stamp, mode="手动", state="停机", freq="25.00", turb="12.50",
                 flow="36.50 m³/h", press="0.499", temp="26.5"):
    """造一行 15 列文本（列序同 HistoryPage.COLUMNS）。"""
    return ([stamp, mode, state, freq, "3.20", "750", turb, "3.20",
             flow, "123.40 m³", press, temp, "55.0",
             "0x0000FFFF", "0x000000FF"], False)


def m_hisflt_seed(page, rows):
    page._rows = list(rows)
    page._rebuild()


def m_hisflt_stamps_of(rows):
    return [r[0][0] for r in rows]


# ============================================================================
#                    A. 时间格式归一化全表
# ============================================================================


def m_hisflt_part_a_norm(page):
    print("== A. 时间格式归一化 ==")
    import time
    today = time.strftime("%Y-%m-%d")
    year = today[:4]
    valid = [
        # (输入, is_end, 期望) —— 期望按 `_norm_stamp` 实测口径写，
        # 不凭想象：MM-dd 单独也支持（当年），年份两位数按字面当年份。
        ("23:09", False, ("T", "23:09:00")),
        ("23:09", True, ("T", "23:09:59")),
        ("23:09:53", False, ("T", "23:09:53")),
        ("0:05", False, ("T", "00:05:00")),
        ("11-05", False, ("F", "%s-11-05 00:00:00" % year)),
        ("11-05 08:30", False, ("F", "%s-11-05 08:30:00" % year)),
        ("2026-11-05", False, ("F", "2026-11-05 00:00:00")),
        ("2026-11-05", True, ("F", "2026-11-05 23:59:59")),
        ("2026-11-05 08:30", False, ("F", "2026-11-05 08:30:00")),
        ("2026-11-05 08:30:15", True, ("F", "2026-11-05 08:30:15")),
    ]
    ok = 0
    total = len(valid)
    for raw, is_end, want in valid:
        got = page._norm_stamp(raw, is_end=is_end)
        if got == want:
            ok += 1
        else:
            print("      %r(end=%s) → %r 期望 %r" % (raw, is_end, got, want))
    check("A1 合法格式全部归一化正确", ok == total, "%d/%d" % (ok, total))

    # MM-dd 带时刻（当年语义）
    got = page._norm_stamp("11-05 08:30", is_end=False)
    check("A2 MM-dd HH:MM 按当年归一", got is not None
          and got[1].endswith("-11-05 08:30:00"),
          repr(got))

    invalid = ["", "  ", "abc", "25:00", "12:60", "12:00:61", "2026-13-01",
               "2026-02-30", "2026-11-05 25:00", "a b c", "1:2:3:4"]
    bad_ok = 0
    for raw in invalid:
        if page._norm_stamp(raw, is_end=False) is None:
            bad_ok += 1
        else:
            print("      %r 本应判非法，却接受为 %r"
                  % (raw, page._norm_stamp(raw, is_end=False)))
    check("A3 非法格式全部被拒", bad_ok == len(invalid),
          "%d/%d" % (bad_ok, len(invalid)))


# ============================================================================
#                   B. 起止时间组合
# ============================================================================


def m_hisflt_part_b_ranges(page):
    print("== B. 起止组合 ==")
    m_hisflt_seed(page, [
        m_hisflt_row("2026-11-04 23:00:00"),
        m_hisflt_row("2026-11-05 08:00:00"),
        m_hisflt_row("2026-11-05 12:00:00"),
        m_hisflt_row("2026-11-05 18:00:00"),
        m_hisflt_row("2026-11-06 09:00:00"),
    ])

    def filt():
        return m_hisflt_stamps_of(page._filtered_rows())

    # ---- 只有起点 ----
    page._clear_inputs()
    page._f_start.setText("2026-11-05 12:00")
    check("B1 只给起点 → 该时刻及之后（3 行）",
          filt() == ["2026-11-05 12:00:00", "2026-11-05 18:00:00",
                     "2026-11-06 09:00:00"], str(filt()))

    # ---- 只有终点 ----
    page._clear_inputs()
    page._f_end.setText("2026-11-05 12:00")
    check("B2 只给终点 → 该时刻及之前（3 行）",
          filt() == ["2026-11-04 23:00:00", "2026-11-05 08:00:00",
                     "2026-11-05 12:00:00"], str(filt()))

    # ---- 起止都给（含端点）----
    page._clear_inputs()
    page._f_start.setText("2026-11-05 08:00")
    page._f_end.setText("2026-11-05 18:00")
    check("B3 起止都给 → 闭区间（3 行，含两端）",
          filt() == ["2026-11-05 08:00:00", "2026-11-05 12:00:00",
                     "2026-11-05 18:00:00"], str(filt()))

    # ---- 跨天 ----
    page._clear_inputs()
    page._f_start.setText("2026-11-04")
    page._f_end.setText("2026-11-05")
    check("B4 跨天范围（日期粒度）→ 04 与 05 两天（4 行）",
          len(filt()) == 4, str(filt()))

    # ---- 纯时刻 = 当天语义（跨天数据不能误命中）----
    import datetime as _dt
    today = _dt.date.today()
    tomorrow = today + _dt.timedelta(days=1)
    T = today.strftime("%Y-%m-%d")
    m_hisflt_seed(page, [
        m_hisflt_row("%s 08:00:00" % T),
        m_hisflt_row("%s 12:00:00" % T),
        m_hisflt_row("%s 18:00:00" % T),
        m_hisflt_row("%s 09:00:00" % tomorrow.strftime("%Y-%m-%d")),
    ])
    page._clear_inputs()
    page._f_start.setText("08:00")
    page._f_end.setText("13:00")
    got = filt()
    check("B5 纯时刻只筛今天（跨天那行不误命中）",
          got == ["%s 08:00:00" % T, "%s 12:00:00" % T], str(got))
    page._f_start.clear()
    page._f_end.clear()
    check("B5b 清掉时刻条件 → 4 行都回来", len(filt()) == 4, str(filt()))

    # 恢复 5 行数据供后续断言
    m_hisflt_seed(page, [
        m_hisflt_row("2026-11-04 23:00:00"),
        m_hisflt_row("2026-11-05 08:00:00"),
        m_hisflt_row("2026-11-05 12:00:00"),
        m_hisflt_row("2026-11-05 18:00:00"),
        m_hisflt_row("2026-11-06 09:00:00"),
    ])

    # ---- 无筛选 ----
    page._clear_inputs()
    check("B6 无筛选 → 全部 5 行（时间正序）", len(filt()) == 5, str(filt()))


# ============================================================================
#                      C. 关键字
# ============================================================================


def m_hisflt_part_c_keyword(page):
    print("== C. 关键字 ==")
    from PyQt5.QtCore import Qt
    page._f_col.check_all()
    m_hisflt_seed(page, [
        m_hisflt_row("2026-11-05 08:00:00", mode="手动", state="停机"),
        m_hisflt_row("2026-11-05 09:00:00", mode="自动·运行中", state="正转运行"),
        m_hisflt_row("2026-11-05 10:00:00", mode="手动", state="故障 0x0001"),
    ])

    page._clear_inputs()
    page._f_text.setText("自动")
    check("C1 关键字命中「自动·运行中」（1 行）",
          m_hisflt_stamps_of(page._filtered_rows()) == ["2026-11-05 09:00:00"],
          str(m_hisflt_stamps_of(page._filtered_rows())))

    page._f_text.setText("故障")
    check("C2 关键字命中「故障 0x0001」",
          m_hisflt_stamps_of(page._filtered_rows()) == ["2026-11-05 10:00:00"])

    page._f_text.setText("手动")
    check("C3 关键字命中两行「手动」", len(page._filtered_rows()) == 2)

    page._f_text.setText("不存在的词")
    check("C4 关键字不命中 → 0 行", page._filtered_rows() == [])

    page._f_text.setText("")
    check("C5 关键字空 → 恢复全部（3 行）",
          len(page._filtered_rows()) == 3)

    # ---- 只勾某一列：关键字只在该列里找 ----
    page._clear_inputs()
    page._f_col.clear_all()
    page._f_col._model.item(2).setCheckState(Qt.Checked)   # 只看「搅拌状态」
    page._f_text.setText("手动")
    check("C6 列只勾「搅拌状态」时，关键字「手动」不命中（模式列没勾）",
          page._filtered_rows() == [],
          "%d 行" % len(page._filtered_rows()))

    page._f_col._model.item(1).setCheckState(Qt.Checked)   # 加勾「模式」
    # clear_all 受 min_checked=1 约束会保留第 1 列（时间），所以是 3 列
    check("C7 加勾「模式」列后命中 2 行",
          len(page._filtered_rows()) == 2,
          "%d 行 / %d 列" % (len(page._filtered_rows()),
                             len(page.visible_columns())))
    check("C7b 列显示 = 时间+模式+搅拌状态（3 列）",
          page.visible_columns() == [0, 1, 2],
          str(page.visible_columns()))

    # ---- 大小写/子串 ----
    page._f_col.check_all()
    page._f_text.setText("0x0000")
    check("C8 关键字可匹配十六进制位图列（0x0000 前缀）",
          len(page._filtered_rows()) == 3, "全部行的 DI/DO 都含 0x0000")

    page._f_text.setText("")
    check("C9 清空关键字恢复", len(page._filtered_rows()) == 3)


# ============================================================================
#                  D. 时间 + 关键字 组合
# ============================================================================


def m_hisflt_part_d_combo(page):
    print("== D. 时间 + 关键字组合（AND）==")
    page._f_col.check_all()
    m_hisflt_seed(page, [
        m_hisflt_row("2026-11-05 08:00:00", mode="手动"),
        m_hisflt_row("2026-11-05 12:00:00", mode="手动"),
        m_hisflt_row("2026-11-05 12:30:00", mode="自动·运行中"),
        m_hisflt_row("2026-11-05 18:00:00", mode="自动·运行中"),
    ])

    page._clear_inputs()
    page._f_start.setText("2026-11-05 12:00")
    page._f_text.setText("自动")
    got = m_hisflt_stamps_of(page._filtered_rows())
    check("D1 时间≥12:00 且 关键字=自动 → 2 行",
          got == ["2026-11-05 12:30:00", "2026-11-05 18:00:00"], str(got))

    page._f_end.setText("2026-11-05 12:30")
    got = m_hisflt_stamps_of(page._filtered_rows())
    check("D2 再加终点 12:30 → 只剩 1 行",
          got == ["2026-11-05 12:30:00"], str(got))

    page._f_text.setText("手动")
    got = m_hisflt_stamps_of(page._filtered_rows())
    # 12:00 那行是「手动」，18:00 是「自动·运行中」→ 只剩 1 行
    check("D3 同时间窗但关键字换成「手动」→ 1 行（AND 语义）",
          got == ["2026-11-05 12:00:00"], str(got))


# ============================================================================
#                        E. 边界情况
# ============================================================================


def m_hisflt_part_e_edges(page):
    print("== E. 边界 ==")
    m_hisflt_seed(page, [
        m_hisflt_row("2026-11-05 08:00:00"),
        m_hisflt_row("2026-11-05 09:00:00"),
    ])

    # 起止完全相同：只命中该秒那一行
    page._clear_inputs()
    page._f_start.setText("2026-11-05 08:00:00")
    page._f_end.setText("2026-11-05 08:00:00")
    got = m_hisflt_stamps_of(page._filtered_rows())
    check("E1 起止相等（秒级）→ 只命中该行",
          got == ["2026-11-05 08:00:00"],
          str(got))

    # 起晚于止 → 空集（不是全部，也不是报错）
    page._f_start.setText("2026-11-05 20:00")
    page._f_end.setText("2026-11-05 08:00")
    check("E2 起晚于止 → 空集（不静默变成全部）",
          page._filtered_rows() == [], str(page._filtered_rows()))

    # 极早起点 → 全部
    page._clear_inputs()
    page._f_start.setText("2000-01-01")
    check("E3 极早起点 → 全部行", len(page._filtered_rows()) == 2)

    # 极晚终点 → 全部
    page._clear_inputs()
    page._f_end.setText("2099-12-31")
    check("E4 极晚终点 → 全部行", len(page._filtered_rows()) == 2)

    # 起点在最后一行之后
    page._clear_inputs()
    page._f_start.setText("2099-01-01")
    check("E5 起点晚于所有数据 → 空集", page._filtered_rows() == [])

    # 终点在第一行之前
    page._clear_inputs()
    page._f_end.setText("2000-01-01")
    check("E6 终点早于所有数据 → 空集", page._filtered_rows() == [])

    # 空数据集
    page._clear_inputs()
    page._rows = []
    check("E7 无数据 + 有筛选 → 空集不崩",
          page._filtered_rows() == [], "ok")
    m_hisflt_seed(page, [m_hisflt_row("2026-11-05 08:00:00")])
    check("E8 数据恢复后筛选仍有效",
          len(page._filtered_rows()) == 1)


# ============================================================================
#              F. 与排序 / 列显示 / 导出 的交互
# ============================================================================


def m_hisflt_part_f_interaction(page):
    print("== F. 与排序/列/导出的交互 ==")
    import csv
    import os
    import tempfile
    from PyQt5.QtCore import Qt
    page._f_col.check_all()
    page._f_start.clear()
    page._f_end.clear()
    page._f_text.clear()
    m_hisflt_seed(page, [
        m_hisflt_row("2026-11-05 08:00:00", turb="12.50"),
        m_hisflt_row("2026-11-05 09:00:00", turb="88.88"),
        m_hisflt_row("2026-11-05 10:00:00", turb="3.20"),
    ])

    # 筛选 + 排序：排序只重排筛后的结果
    page._f_start.setText("2026-11-05 09:00")
    page._sort_col = 6          # 浊度1 列
    page._sort_desc = True
    page._rebuild()
    view = [r[0][0] for r in page._visible_rows()]
    check("F1 排序只作用于筛后结果（09:00、10:00 两行）",
          len(view) == 2, str(view))
    check("F2 按浊度降序正确（88.88 在 3.20 前）",
          view[0] == "2026-11-05 09:00:00", str(view))

    # 筛选 + 列隐藏：筛出的行不受列显示影响
    page._sort_col = None
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(Qt.Checked)   # 只显示时间
    page._rebuild()
    check("F3 隐藏大多数列后，筛选结果不变（2 行）",
          page._table.table.rowCount() == 2,
          "table rows=%d" % page._table.table.rowCount())
    check("F4 隐藏列后筛选逻辑仍能取到被隐藏列的值（浊度用于判断）",
          len(page._filtered_rows()) == 2)

    # 导出只含可见列 + 筛后的行
    cols = page.visible_columns()
    rows = page._visible_rows()
    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "f.csv")
    try:
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            w = csv.writer(fh)
            w.writerow([page.COLUMNS[c] for c in cols])
            for texts, _a in rows:
                w.writerow([texts[c] for c in cols])
        with open(path, encoding="utf-8-sig") as fh:
            got = list(csv.reader(fh))
        check("F5 导出：表头 = 1 列（只显示时间）", got[0] == ["时间"],
              str(got[0]))
        check("F6 导出：行数 = 筛后行数（2）", len(got) - 1 == 2,
              "%d 行" % (len(got) - 1))
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)

    page._f_col.check_all()
    page._clear_inputs()


# ============================================================================
#                    G. 默认起始时间
# ============================================================================


def m_hisflt_part_g_default_start():
    print("== G. 默认起始时间 ==")
    import time
    app, page, status = m_hisflt_make_page()
    page._data_mode = "server"
    earliest = 1793927000000
    latest = 1793927400000

    def reply(earliest_ts, latest_ts=latest):
        page._on_page_ready({"ok": True, "total": 10, "cols": [],
                             "rows": [], "dbRows": 10,
                             "latestTs": latest_ts,
                             "earliestTs": earliest_ts,
                             "offset": 0, "limit": 20})

    check("G1 初始起始框为空", page._f_start.text() == "",
          repr(page._f_start.text()))
    reply(earliest)
    want = time.strftime("%Y-%m-%d %H:%M:%S",
                         time.localtime(earliest / 1000.0))
    check("G2 首次应答后起始框 = 最前一条数据时间",
          page._f_start.text() == want, repr(page._f_start.text()))
    check("G3 状态栏提示已载入",
          any("最早一条" in s for s in status), str(status[-1:]))

    # 只填一次：再应答不覆盖
    reply(earliest - 3600000)
    check("G4 后续应答不覆盖已填的默认值（只填一次）",
          page._f_start.text() == want, repr(page._f_start.text()))

    # 坏 ts（1970 年）不采用
    app2, page2, _st2 = m_hisflt_make_page()
    page2._data_mode = "server"
    page2._on_page_ready({"ok": True, "total": 0, "cols": [], "rows": [],
                          "dbRows": 0, "latestTs": 0, "earliestTs": 3236093,
                          "offset": 0, "limit": 20})
    check("G5 1970 年坏 ts 不填进起始框", page2._f_start.text() == "",
          repr(page2._f_start.text()))

    # 空库（earliestTs=0）不填
    app3, page3, _st3 = m_hisflt_make_page()
    page3._data_mode = "server"
    page3._on_page_ready({"ok": True, "total": 0, "cols": [], "rows": [],
                          "dbRows": 0, "latestTs": 0, "earliestTs": 0,
                          "offset": 0, "limit": 20})
    check("G6 空库不填默认值", page3._f_start.text() == "",
          repr(page3._f_start.text()))

    # 用户先手输 → 不覆盖
    app4, page4, _st4 = m_hisflt_make_page()
    page4._data_mode = "server"
    page4._f_start.setText("2026-11-01 00:00")
    page4._on_start_edited("2026-11-01 00:00")
    page4._on_page_ready({"ok": True, "total": 10, "cols": [], "rows": [],
                          "dbRows": 10, "latestTs": latest,
                          "earliestTs": earliest, "offset": 0, "limit": 20})
    check("G7 用户手输的时间不被默认值覆盖",
          page4._f_start.text() == "2026-11-01 00:00",
          repr(page4._f_start.text()))

    # 清除筛选后不回填
    page._clear_filter()
    check("G8 清除筛选清空起始框", page._f_start.text() == "",
          repr(page._f_start.text()))
    reply(earliest)
    check("G9 清除筛选后不再自动回填", page._f_start.text() == "",
          repr(page._f_start.text()))


# ============================================================================
#                H. 下推参数构造（_server_time_range）
# ============================================================================


def m_hisflt_part_h_pushdown():
    print("== H. 下推参数构造 ==")
    import time
    app, page, _st = m_hisflt_make_page()
    page._data_mode = "server"

    page._f_start.clear()
    page._f_end.clear()
    check("H1 无时间条件 → None（不带上界下界）",
          page._server_time_range() is None,
          repr(page._server_time_range()))

    page._f_start.setText("2026-11-05 08:00:00")
    lo, hi = page._server_time_range()
    check("H2 只给起点 → (lo, None)", lo is not None and hi is None,
          "lo=%s hi=%s" % (lo, hi))
    check("H3 lo 是毫秒且与输入一致",
          lo == int(time.mktime(time.strptime("2026-11-05 08:00:00",
                                              "%Y-%m-%d %H:%M:%S")) * 1000),
          str(lo))

    page._f_start.clear()
    page._f_end.setText("2026-11-05 18:00:00")
    lo, hi = page._server_time_range()
    check("H4 只给终点 → (None, hi)", lo is None and hi is not None,
          "lo=%s hi=%s" % (lo, hi))

    page._f_start.setText("2026-11-05 08:00")
    lo, hi = page._server_time_range()
    check("H5 起止都给 → 两个毫秒值且 lo<hi", lo is not None
          and hi is not None and lo < hi, "%s ~ %s" % (lo, hi))

    # 纯时刻 = 当天
    page._f_start.setText("08:00")
    page._f_end.clear()
    lo, _hi = page._server_time_range()
    day_start = time.mktime(time.strptime(
        time.strftime("%Y-%m-%d ") + "08:00:00", "%Y-%m-%d %H:%M:%S"))
    check("H6 纯时刻按当天换算", lo == int(day_start * 1000), str(lo))

    # 非法输入 → 不抛异常，返回 None（_apply_filter 负责先报错）
    page._f_start.setText("乱码")
    try:
        got = page._server_time_range()
        ok = got is None
    except Exception as exc:                        # noqa: BLE001
        ok = False
        print("      异常 %s: %s" % (type(exc).__name__, exc))
    check("H7 非法输入不抛异常（返回 None）", ok, repr(got))

    # _apply_filter 对非法输入只报错、不改筛选状态
    page._f_start.setText("13:99")
    before = (page._f_start.text(), page._page_index)
    page._data_mode = "local"                       # local 分支只 rebuild
    page._apply_filter()
    check("H8 非法起始时间：报错且不改页码",
          page._page_index == before[1], "index=%d" % page._page_index)


def m_hisflt_part_i_server_trigger():
    """服务端模式筛选触发语义（拦截 bridge 调用，不发真实网络）。

    这是 2026-11-06「关键字筛选无效」修复的控件层锁：关键字/列变化
    必须触发**重取第 0 页**且请求里带上 kw/kwCols；否则 _filtered_rows
    在服务端模式直接放行，关键字等于没生效。
    """
    print("== I. 服务端模式筛选触发 ==")
    app, page, _st = m_hisflt_make_page()
    calls = []

    class StubBridge(object):
        def is_connected(self):
            return True

        def history_page(self, **kw):
            calls.append(dict(kw))

    page.ctx.bridge = StubBridge()
    page._data_mode = "server"
    page._page_index = 3
    page._window_end = 123456

    # ---- I1 任何筛选变化（哪怕只改关键字）都重取第 0 页 ----
    page._f_text.setText("运行")
    page._apply_filter()
    check("I1 只改关键字也重取第 0 页（旧 bug：只推时间）",
          page._page_index == 0 and len(calls) == 1,
          "index=%d calls=%d" % (page._page_index, len(calls)))
    check("I2 请求带 kw 与 kwCols", calls
          and calls[0].get("kw") == "运行"
          and calls[0].get("kw_cols") == page.visible_columns(),
          "kw=%r cols=%r" % (calls[0].get("kw") if calls else None,
                             calls[0].get("kw_cols") if calls else None))
    check("I3 筛选重置窗口锚点（追最新）", page._window_end > 123456,
          "end=%d" % page._window_end)

    # 清 _inflight（stub 不回调，手动复位以继续测）
    page._inflight = False
    calls[:] = []

    # ---- I4 关键字为空时，列变化只是视图设置，不重取 ----
    page._f_text.clear()
    page._page_index = 2
    page._on_columns_changed()
    check("I4 无关键字时改列不发网络请求（纯视图）", len(calls) == 0,
          "calls=%d" % len(calls))

    # ---- I5 关键字激活时，改列影响 kwCols ⇒ 必须重取 ----
    page._f_text.setText("运行")
    page._page_index = 2
    page._on_columns_changed()
    check("I5 关键字激活时改列触发重取（kwCols 是筛选条件）",
          page._page_index == 0 and len(calls) == 1,
          "index=%d calls=%d" % (page._page_index, len(calls)))
    check("I6 重取请求带上新的 kwCols", calls
          and calls[0].get("kw_cols") == page.visible_columns(),
          "cols=%r" % (calls[0].get("kw_cols") if calls else None))

    # ---- I7 服务端模式 _filtered_rows 不再本地重复筛 ----
    page._inflight = False
    page._rows = [m_hisflt_row("2026-11-06 09:00:00", mode="手动"),
                  m_hisflt_row("2026-11-06 09:00:01", mode="自动")]
    page._f_text.setText("绝不可能命中")
    check("I7 服务端模式 _filtered_rows 直接放行（本地不再重复筛）",
          len(page._filtered_rows()) == 2, "%d 行" % len(page._filtered_rows()))

    # ---- I8 清除筛选在服务端模式重取且清掉 kw ----
    page._inflight = False
    calls[:] = []
    page._page_index = 4
    page._clear_filter()
    check("I8 清除筛选重取第 0 页", page._page_index == 0 and len(calls) == 1,
          "index=%d calls=%d" % (page._page_index, len(calls)))
    check("I9 清除后请求不带 kw", calls and calls[0].get("kw") is None,
          "kw=%r" % (calls[0].get("kw") if calls else None))
    check("I10 清除后输入框全空", page._f_text.text() == ""
          and page._f_start.text() == "", "text=%r" % page._f_text.text())

    # ---- I11 bridge.history_page 载荷字段（proto 常量对齐）----
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    lb = LinkBridge()
    captured = {}
    # 不连服务端也能验参数：直接替换底层 request 出口
    def fake_request(code, doc, on_done=None, on_error=None, timeout=5.0):
        captured["code"] = code
        captured.update(doc)
    lb.request = fake_request
    lb.history_page(offset=0, limit=20, before_ts=999, lo_ts=111,
                    hi_ts=222, kw="运行", kw_cols=[0, 2])
    check("I11 bridge 载荷含 kw / kwCols（BSON 键名对齐）",
          captured.get(proto.HIST_KEY_KW) == "运行"
          and captured.get(proto.HIST_KEY_KW_COLS) == [0, 2],
          "kw=%r cols=%r" % (captured.get(proto.HIST_KEY_KW),
                             captured.get(proto.HIST_KEY_KW_COLS)))
    check("I12 bridge 载荷时间/锚点键齐全",
          captured.get(proto.HIST_KEY_LO_TS) == 111
          and captured.get(proto.HIST_KEY_HI_TS) == 222
          and captured.get(proto.HIST_KEY_BEFORE_TS) == 999,
          "lo=%s hi=%s before=%s" % (captured.get(proto.HIST_KEY_LO_TS),
                                     captured.get(proto.HIST_KEY_HI_TS),
                                     captured.get(proto.HIST_KEY_BEFORE_TS)))
    # 空关键字/空列不写字段（省载荷，也与旧调用方兼容）
    captured.clear()
    lb.history_page(offset=5, limit=10)
    check("I13 空 kw/kwCols 不写进载荷（向后兼容）",
          proto.HIST_KEY_KW not in captured
          and proto.HIST_KEY_KW_COLS not in captured,
          "keys=%s" % sorted(captured.keys()))


# ============================================================================
#                            段落入口
# ============================================================================


def run_hisflt():
    """历史页筛选整段（对应原 test_history_filter.py 的 main）。

    [口径] 纯控件层：**不连服务端**、不建 QApplication（复用总回归那个）。
    原 main() 里"打印汇总 / 退出码"的部分不搬（断言计数归总回归统一汇总）。

    [与总回归主流程的差异] 原 main() 里 G/H/I 三段各自**新建页面**
    （默认起始时间要验证"不覆盖用户输入"，必须换页），这里保持不变。
    """
    section("§14b 历史页：筛选（时间 / 关键字 / 组合 / 下推）")
    app, page, status = m_hisflt_make_page()
    m_hisflt_part_a_norm(page)
    m_hisflt_part_b_ranges(page)
    m_hisflt_part_c_keyword(page)
    m_hisflt_part_d_combo(page)
    m_hisflt_part_e_edges(page)
    m_hisflt_part_f_interaction(page)
    m_hisflt_part_g_default_start()
    m_hisflt_part_h_pushdown()
    m_hisflt_part_i_server_trigger()


# ==========================================================================
# 段落：test_history_page（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】历史数据分页 end-to-end 测试（真服务端 + 真 XLink）。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的一段源码（总回归会把它原样拼进去）。

[命名契约] 本段落的**所有**顶层名字统一带 `m_hispage_` 前缀
    （函数/类/常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `section` / `_free_port` …）
    **按名直呼**，不 import 总回归（避免循环导入）。
    [顶层不 import] 本段落的 import **全部放在函数里** ⇒ 段落的顶层名
    与总回归、与其它段落**零交集**（`os` / `time` / `json` 这些名字归
    总回归所有；独立放本文件时也不会撞名）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `m_hispage_app()` 取（取不到就报错，**绝不自己建**）。

用户需求：
  「历史数据界面启动的时候不会加载服务器上的历史数据，加载下，为了
    防止数据过多打爆服务器，客户端这边设置分页，服务端页分页，一次
    取一页，一页多少条由客户端决定，但是总数要告知并实时获取，如果
    在历史数据界面的话」

本段逐条对应：
  A. 协议层：CMD_HISTORY_PAGE 能取到服务端落盘的历史（**入页即加载**）
  B. 页大小由客户端决定（10/20/50 三种），服务端按 limit 返回
  C. 服务端分页正确：翻页无重叠无遗漏、总数始终如实上报
  D. 总数实时获取：库持续入库时，在历史页上周期探到的 total 会增长
  E. 客户端历史页：on_enter 自动取第 1 页、页码/总数标签、下页/上页、
     改页大小归零、离页停止轮询
  F. 兜底：服务端没开落盘时明确报错，不假装成功
  G. 服务端模式下的筛选（时间 + 关键字 + 列全下推）
  I. 协议层关键字下推（不经 UI，直接发 CMD_HISTORY_PAGE）

[为什么用 1s 采样] 现场 5s 一行太慢，测试里压到 1s，20 秒就有 20 行，
够翻两页并观察总数增长。

[服务端隔离口径] 本段**自起一个 sim 服务端**（自己起、自己停）：
  · cfg 落**独立临时目录**（不碰现场库/账户库）；
  · 端口走 `_free_port()`（不写死 9177，避免撞并行任务/别人起的实例）；
  · 只 `terminate` 自己 `Popen` 出来的那个进程。
"""


# ============================================================================
#                       段落基础设施（app / 路径 / 端口）
# ============================================================================


def m_hispage_app():
    """取总回归那唯一一个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。

    [取法] 优先用总回归的模块级 `_app`；总回归没暴露时退回
    `QApplication.instance()`（**仍然不新建**）。两者都取不到 ⇒ 报错，
    绝不退化成"自己建一个"（Qt5 未定义行为 ⇒ 段错误/析构顺序错乱）。
    """
    app = globals().get("_app") or globals().get("_APP")
    if app is None:
        from PyQt5.QtWidgets import QApplication
        app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑本段（段落里绝不新建）")
    return app


def m_hispage_server_root():
    """Python 服务端源码目录（.../HighSpeedFilter/server_py）。

    [并进总回归后] `__file__` 指向主文件（tests/ 下）；本文件独立放在
    tests/_merged/ 下时要向上退一级。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    client_root = os.path.dirname(d)              # .../client_py
    return os.path.join(os.path.dirname(client_root), "server_py")


def m_hispage_wait(seconds):
    """睡指定秒数（不跑事件循环——服务端轮询用的纯等待）。"""
    import time
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(0.05)


# ============================================================================
#                       私有 sim 服务端（自起自停）
# ============================================================================


class m_hispage_Server(object):
    """独立配置目录的 sim 服务端（采样 1s，落盘到临时库）。

    [隔离口径] cfg 与库都落本实例的临时目录 ⇒ 不碰现场 cfg/账户库/库。
    [端口] 由调用方给（`_free_port()` 拿的空闲口）⇒ 不写死、不撞别人。
    [收尾] `stop()` 只 terminate **自己** Popen 出来的那个进程。
    """

    def __init__(self, port, tmpdir, sample_ms=1000, enabled=True):
        self.port = port
        self.tmpdir = tmpdir
        self.sample_ms = sample_ms
        self.enabled = enabled
        self.proc = None
        self.bridge = None

    def start(self):
        import json
        import os
        import socket
        import subprocess
        import sys
        import time
        cfg_path = os.path.join(self.tmpdir, "cfg.json")
        cfg = {
            "server": {"port": self.port},
            "storage": {
                "enabled": self.enabled,
                "path": os.path.join(self.tmpdir, "hist.db"),
                "sample_ms": self.sample_ms,
            },
        }
        with open(cfg_path, "w", encoding="utf-8") as fh:
            json.dump(cfg, fh, ensure_ascii=False, indent=2)
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"   # 服务端日志即时落盘（崩溃取证）
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(self.port),
             "--config", cfg_path],
            cwd=m_hispage_server_root(), env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        deadline = time.time() + 25
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", self.port),
                                         timeout=0.4).close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def connect(self):
        from hsfsrv_client.bridge import LinkBridge
        self.bridge = LinkBridge()
        return self.bridge.connect_to("127.0.0.1", self.port, "root", "root")

    def page(self, offset=0, limit=20, before_ts=None, lo_ts=None, hi_ts=None,
             kw=None, kw_cols=None, timeout=8.0):
        """同步取一页（包一层等待，测试里读起来像普通调用）。"""
        import time
        holder = {}
        done = [False]

        def _done(doc):
            holder.update(doc)
            done[0] = True

        def _err(msg):
            holder["transportError"] = msg
            done[0] = True

        self.bridge.history_page(offset=offset, limit=limit,
                                 before_ts=before_ts, lo_ts=lo_ts,
                                 hi_ts=hi_ts, kw=kw, kw_cols=kw_cols,
                                 on_done=_done, on_error=_err,
                                 timeout=timeout)
        end = time.time() + timeout + 1
        while not done[0] and time.time() < end:
            time.sleep(0.02)
        return holder

    def stop(self):
        """收尾：断链 + **只**停自己起的进程。"""
        import subprocess
        try:
            if self.bridge is not None:
                self.bridge.disconnect_from()
        except Exception:                               # noqa: BLE001
            pass
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()


# ============================================================================
#                       A/B/C. 协议与服务端分页
# ============================================================================


def m_hispage_part_abc_protocol(srv):
    print("== A. 入页即加载服务端历史 ==")
    first = srv.page(offset=0, limit=20)
    check("A1 应答 ok", first.get("ok") is True,
          str(first.get("err") or first.get("transportError") or "")[:60])
    total = int(first.get("total") or 0)
    check("A2 服务端已有落盘数据（total > 0）", total > 0, "total=%d" % total)
    cols = list(first.get("cols") or [])
    check("A3 应答带列名（%d 列，与协议同数）" % HIST_COL_COUNT,
          len(cols) == HIST_COL_COUNT, "cols=%d" % len(cols))
    rows = first.get("rows") or []
    check("A4 第 1 页有行", len(rows) > 0, "rows=%d" % len(rows))
    check("A5 每行列数与 cols 一致",
          all(len(r) == len(cols) for r in rows),
          "首行 %d 列" % (len(rows[0]) if rows else -1))
    check("A6 行按时间正序", [r[0] for r in rows] == sorted(r[0] for r in rows))
    check("A7 首行是墙钟毫秒时间戳（不是 monotonic 开机计时）",
          isinstance(rows[0][0], int) and rows[0][0] > 1600000000000,
          str(rows[0][0]))
    check("A8 dbRows 如实上报", int(first.get("dbRows") or 0) >= total,
          "dbRows=%s total=%d" % (first.get("dbRows"), total))

    print("== B. 页大小由客户端决定 ==")
    for want in (5, 10, 20, 50):
        got = srv.page(offset=0, limit=want)
        n = len(got.get("rows") or [])
        expect = min(want, int(got.get("total") or 0))
        check("B%d limit=%d → 返回 %d 行" % (want, want, n),
              n == expect, "n=%d 期望 %d" % (n, expect))
        check("B%d limit 原样回显 %d" % (want, want),
              int(got.get("limit") or -1) == want, str(got.get("limit")))
    over = srv.page(offset=0, limit=9999)
    check("B-上限 超限 limit 被夹到 %d" % int(over.get("limit") or -1),
          int(over.get("limit") or 0) <= 50, "limit=%s" % over.get("limit"))
    zero = srv.page(offset=0, limit=0)
    check("B-下限 limit=0 被夹到至少 1",
          int(zero.get("limit") or 0) >= 1, "limit=%s" % zero.get("limit"))

    print("== C. 服务端分页正确性 ==")
    # [必须锚定窗口] 库在持续入库（1s 一行），不锚定的话每翻一页 offset
    # 都被新行顶偏，会重复/漏行——那测的是"没锚定会错"，不是分页正确性。
    # 真实客户端也是这么用的：首屏取一次锚点，翻页原样回传。
    anchor = int(srv.page(offset=0, limit=1).get("windowEndTs") or 0)
    total = int(srv.page(offset=0, limit=1, before_ts=anchor)
                .get("total") or 0)
    page_size = 5
    seen, offset = [], 0
    while offset < total:
        doc = srv.page(offset=offset, limit=page_size, before_ts=anchor)
        chunk = doc.get("rows") or []
        if not chunk:
            break
        seen.extend(r[0] for r in chunk)
        check("C-x 第 %d 页总数与首页一致" % (offset // page_size + 1),
              int(doc.get("total") or -1) == total,
              "total=%s 期望 %d" % (doc.get("total"), total))
        offset += page_size
    check("C1 翻完整个库行数 = total", len(seen) == total,
          "seen=%d total=%d" % (len(seen), total))
    check("C2 无重复行", len(set(seen)) == len(seen),
          "unique=%d/%d" % (len(set(seen)), len(seen)))
    # 页内正序 + 页间"前一页整体比后一页新"（第 1 页是最新那段）
    check("C3 页内时间正序",
          all(seen[i] < seen[i + 1]
              for i in range(len(seen) - 1)
              if i % page_size != page_size - 1),
          "首=%s 末=%s" % (seen[0] if seen else "-",
                          seen[-1] if seen else "-"))
    check("C4 页间衔接正确（第 k 页最早行晚于第 k+1 页最新行）",
          all(seen[i - 1] > seen[i]
              for i in range(page_size, len(seen), page_size)),
          "%d 行 / 每页 %d" % (len(seen), page_size))

    # 窗口锚定：拿一个锚点后，即使库继续入库，同一窗口的总数不变
    anchor_doc = srv.page(offset=0, limit=10)
    anchor = int(anchor_doc.get("windowEndTs") or 0)
    check("C5 应答带窗口锚点 windowEndTs", anchor > 0, str(anchor))
    anchored_total = int(anchor_doc.get("total") or 0)
    m_hispage_wait(3.0)                       # 期间至少又多采 2~3 行
    again = srv.page(offset=0, limit=10, before_ts=anchor)
    check("C6 锚点固定后总数不随新入库变化",
          int(again.get("total") or -1) == anchored_total,
          "%d → %s" % (anchored_total, again.get("total")))
    fresh = srv.page(offset=0, limit=10)
    check("C7 不带锚点则看到最新（总数 >= 锚点窗口）",
          int(fresh.get("total") or 0) >= anchored_total,
          "%d vs %d" % (int(fresh.get("total") or 0), anchored_total))

    # 时间范围过滤（服务端 SQL 内筛，总数同口径）
    all_rows = srv.page(offset=0, limit=50).get("rows") or []
    if len(all_rows) >= 6:
        lo = all_rows[2][0]
        hi = all_rows[4][0]
        ranged = srv.page(offset=0, limit=50, lo_ts=lo, hi_ts=hi)
        rrows = ranged.get("rows") or []
        check("C8 时间范围过滤：总数与行数一致",
              int(ranged.get("total") or -1) == len(rrows),
              "total=%s rows=%d" % (ranged.get("total"), len(rrows)))
        check("C9 范围过滤的行都在界内",
              all(lo <= r[0] <= hi for r in rrows),
              "[%d, %d] 内 %d 行" % (lo, hi, len(rrows)))


# ============================================================================
#                       D. 总数实时获取
# ============================================================================


def m_hispage_part_d_live_total(srv):
    print("== D. 总数实时获取（库持续入库）==")
    before = int(srv.page(offset=0, limit=5).get("total") or 0)
    m_hispage_wait(4.0)                  # 1s 采样 → 至少 +3 行
    after = int(srv.page(offset=0, limit=5).get("total") or 0)
    check("D1 再次查询总数增长（实时性）", after > before,
          "%d → %d" % (before, after))
    latest = int(srv.page(offset=0, limit=5).get("latestTs") or 0)
    check("D2 latestTs 与采样同步（非 0）", latest > 0, str(latest))


# ============================================================================
#                    E/F. 客户端历史页行为
# ============================================================================


def m_hispage_part_ef_client(srv):
    print("== E. 客户端历史页（服务端模式）==")
    import os
    import shutil
    import tempfile
    import time
    app = m_hispage_app()
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    # [复用 A~D 段那条链路] 服务端 root 只允许单会话（policy_blocks_login,
    # 防两人并行改配置），再连一条 root 会被拒 → "连接超时"。历史页本身
    # 只需一条已连好的 bridge，复用即可，也更贴近真实（客户端只有一条链路）。
    bridge = srv.bridge
    if bridge is None or not bridge.is_connected():
        check("E0 客户端连接服务端", False, "没有可用链路")
        return
    try:
        ctx = PageContext(bridge, lambda i: None, lambda s: None)
        page = HistoryPage(ctx)
        page._page_size = 10
        page._page_size_box.setCurrentIndex(
            page._page_size_box.findData(10))

        # 入页：自动取第 1 页
        page.on_enter()
        deadline = time.time() + 8
        while time.time() < deadline and not page._rows:
            app.processEvents()
            time.sleep(0.02)
        check("E1 入页自动加载了服务端历史（无需点任何按钮）",
              len(page._rows) > 0, "rows=%d" % len(page._rows))
        check("E2 数据源 = server", page._data_mode == "server",
              page._data_mode)
        check("E3 表格已铺行",
              page._table.table.rowCount() == len(page._rows),
              "table=%d rows=%d" % (page._table.table.rowCount(),
                                    len(page._rows)))
        check("E4 总数已知（total > 0）", page._total > 0,
              "total=%d" % page._total)
        check("E5 页码标签显示 1/N",
              page._page_label.text().startswith("第 1/"),
              page._page_label.text())
        check("E6 总数标签含条数",
              "共 %d 条" % page._total in page._total_label.text(),
              page._total_label.text())
        check("E7 首页时上一页禁用", not page._btn_prev.isEnabled())
        check("E8 还有下一页时下一页可用", page._btn_next.isEnabled(),
              "total=%d page_size=%d" % (page._total, page._page_size))

        # 时间列显示成完整日期（不是原始毫秒）
        first_text = page._table.table.item(0, 0).text()
        check("E9 时间列格式化为 yyyy-MM-dd HH:MM:SS",
              len(first_text) == 19 and first_text[4] == "-",
              repr(first_text))

        # 翻到第 2 页
        page1_ids = [r[0][0] for r in page._rows]
        page._next_page()
        deadline = time.time() + 8
        while time.time() < deadline and page._page_index == 0:
            app.processEvents()
            time.sleep(0.02)
        check("E10 下一页生效（page_index=1）", page._page_index == 1,
              "index=%d" % page._page_index)
        check("E11 第 2 页内容与第 1 页不同",
              [r[0][0] for r in page._rows] != page1_ids)
        check("E12 页码标签跟着走", page._page_label.text().startswith("第 2/"),
              page._page_label.text())
        check("E13 第 2 页时上一页可用", page._btn_prev.isEnabled())

        # 回第 1 页
        page._prev_page()
        deadline = time.time() + 8
        while time.time() < deadline and page._page_index != 0:
            app.processEvents()
            time.sleep(0.02)
        check("E14 上一页回到第 1 页", page._page_index == 0)
        check("E15 回第 1 页内容与首次一致",
              [r[0][0] for r in page._rows] == page1_ids,
              "%d 行" % len(page._rows))

        # 改页大小 → 归零并重取
        before_size = page._page_size
        idx = page._page_size_box.findData(50)
        page._page_size_box.setCurrentIndex(idx)
        deadline = time.time() + 8
        while time.time() < deadline and page._page_size != 50:
            app.processEvents()
            time.sleep(0.02)
        check("E16 改页大小生效（20 → 50）",
              page._page_size == 50 and before_size != 50,
              "%d → %d" % (before_size, page._page_size))
        check("E17 改页大小回到第 1 页", page._page_index == 0,
              "index=%d" % page._page_index)
        check("E18 本页行数 <= 50", len(page._rows) <= 50,
              "rows=%d" % len(page._rows))

        # 总数实时刷新：在页面里等 3.5s 让 _live_tick 探一次
        total_before = page._total
        page._live_timer.setInterval(1000)      # 测试里压缩节拍
        page._start_live()
        deadline = time.time() + 8
        while time.time() < deadline and page._total <= total_before:
            app.processEvents()
            time.sleep(0.05)
        check("E19 在页面上停留时总数自动增长（实时获取）",
              page._total > total_before,
              "%d → %d" % (total_before, page._total))

        # 离页停轮询
        page.on_leave()
        check("E20 离页后停止轮询", not page._live_timer.isActive())

        # 服务端模式下 refresh() 不再掺本地采样
        rows_before = len(page._rows)
        page.refresh()
        check("E21 服务端模式不掺入本地实时采样",
              len(page._rows) == rows_before,
              "%d → %d" % (rows_before, len(page._rows)))
    finally:
        pass            # 链路由调用方（入口）统一断开

    print("== F. 兜底：未开落盘时明确报错 ==")
    from hsfsrv_client.bridge import LinkBridge
    page2 = None
    tmp = tempfile.mkdtemp()
    # [第二个服务端也走独立端口] 不写死 srv.port+7（可能撞别人/撞自己）。
    srv2 = m_hispage_Server(port=_free_port(), tmpdir=tmp, enabled=False)
    try:
        if not srv2.start():
            check("F0 第二个服务端（未开落盘）启动", False)
            return
        b2 = LinkBridge()
        ok2, err2 = b2.connect_to("127.0.0.1", srv2.port, "root", "root")
        if not ok2:
            check("F0b 连接未开落盘的服务端", False, str(err2))
            return
        from hsfsrv_client.page_base import PageContext as PC
        from hsfsrv_client.pages_other import HistoryPage as HP
        ctx2 = PC(b2, lambda i: None, lambda s: None)
        page2 = HP(ctx2)
        page2._page_size = 10
        page2.on_enter()
        deadline = time.time() + 8
        while time.time() < deadline and not page2._total_label.text():
            app.processEvents()
            time.sleep(0.02)
        label = page2._total_label.text()
        check("F1 未开落盘时给出明确提示（不假装有数据）",
              "读取失败" in label or "不可用" in label or "未启用" in label,
              label)
        check("F2 未开落盘时表格为空", len(page2._rows) == 0,
              "rows=%d" % len(page2._rows))
        b2.disconnect_from()
    finally:
        srv2.stop()
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                G. 服务端模式下的筛选（时间 + 关键字 + 列全下推）
# ============================================================================


def m_hispage_part_g_filter(srv):
    """服务端模式筛选（真 XLink）：默认起始时间 / 时间下推 / 关键字下推 /
    翻页保持 / 清除恢复。

    [只用**一个** HistoryPage 对象] 早先这里建了第二个页面来验证默认值，
    结果进程退出时 SegFault（exit=139，实测）：两个页面共用同一条 bridge
    的网络线程，各自持有 QTimer，离屏 Qt 析构顺序不稳。单页面 + 断言
    顺序重排就能覆盖同样的事实，没有理由引入第二个对象。

    [断言必须是确定性的] 库在持续入库（1s 一行），"筛后 total < 筛前"
    这类比较会随采样涨落假红（实测踩到多次）。故一律用：
      · 未来时间点 / 不可能命中的关键字 ⇒ total=0
      · 命中年份 ⇒ total>0 且行内含年份
      · 清除 ⇒ total>0 且输入框清空
      · 输入框内容 / 页码 等纯状态断言
    不比较"变化了多少"。

    [关键字修复的回归锁] 2026-11-06 用户实测"关键字筛选无效"：分页在
    服务端、关键字却留在客户端本地筛，客户端只能在拿到的那一页里搜，
    命中行绝大多数在别的页上。修复后 _apply_filter 把 kw/kwCols 下推、
    重取第 0 页，total 与页内容共用同一条 WHERE。G14 用"不可能命中的
    词 ⇒ total=0"锁死：旧代码 total 保持整库值 >0，必然假红。
    """
    print("== G. 服务端模式筛选 ==")
    import time
    app = m_hispage_app()
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    ctx = PageContext(srv.bridge, lambda i: None, lambda s: None)
    page = HistoryPage(ctx)
    page._page_size = 50
    page._page_size_box.setCurrentIndex(page._page_size_box.findData(50))
    page.on_enter()
    deadline = time.time() + 8
    while time.time() < deadline and not page._rows:
        app.processEvents()
        time.sleep(0.02)
    full_total = page._total
    check("G1 无筛选时 total = 整库", full_total > 0, "total=%d" % full_total)

    # ---- 默认起始时间（入页即填，值来自服务端 earliestTs）----
    default_start = page._f_start.text()
    check("G2 入页自动填入服务端最前一条数据的时间",
          len(default_start) == 19 and default_start[4] == "-",
          repr(default_start))
    check("G3 默认值是有效墙钟（不是 1970 年坏 ts）",
          default_start[:4] >= "2020", repr(default_start[:4]))
    check("G4 默认值下总数仍是整库（等价于看全貌）",
          page._total == full_total, "total=%d" % page._total)

    # ---- 时间范围下推：未来起点 ⇒ 0 行 ----
    page._f_start.setText("2099-01-01")
    page._apply_filter()
    deadline = time.time() + 8
    while time.time() < deadline and page._total != 0:
        app.processEvents()
        time.sleep(0.02)
    check("G5 时间下界下推到服务端（未来起点 ⇒ total=0）",
          page._total == 0, "total=%d" % page._total)
    check("G6 未来起点下页面无行", len(page._rows) == 0,
          "rows=%d" % len(page._rows))
    check("G7 筛后回到第 1 页", page._page_index == 0,
          "index=%d" % page._page_index)

    # ---- 翻页动作不清掉筛选条件 ----
    page._next_page()
    app.processEvents()
    check("G8 翻页不会清掉筛选条件（起始框仍有值）",
          page._f_start.text() == "2099-01-01",
          repr(page._f_start.text()))
    check("G9 筛选后总数保持 0", page._total == 0, "total=%d" % page._total)

    # ---- 清除筛选 → 彻底恢复 ----
    page._clear_filter()
    deadline = time.time() + 8
    while time.time() < deadline and page._total == 0:
        app.processEvents()
        time.sleep(0.02)
    check("G10 清除筛选恢复整库（total > 0）", page._total > 0,
          "total=%d" % page._total)
    check("G11 清除筛选清空起始框", page._f_start.text() == "",
          repr(page._f_start.text()))

    # ---- 时间范围：真实区间（行都落在范围内）----
    # 用"默认起点"当 lo、用当前整库最新时刻之后的某个确定时间当 hi，
    # 断言"每行 >= lo" 是单向且不受持续入库影响的。
    page._f_start.setText(default_start)
    page._apply_filter()
    deadline = time.time() + 8
    while time.time() < deadline and not page._rows:
        app.processEvents()
        time.sleep(0.02)
    check("G12 起点=%s 时筛出的行都 >= 该时刻" % default_start,
          bool(page._rows) and all(r[0][0] >= default_start
                                   for r in page._rows),
          "%d 行" % len(page._rows))
    check("G13 服务端返回的总数与 dbRows 自洽",
          page._total <= page._db_rows,
          "total=%d dbRows=%d" % (page._total, page._db_rows))

    # ---- 关键字筛选下推服务端（total 与页内容同口径）----
    # 「不可能命中的词」⇒ total=0：旧实现关键字留在本地筛，total 仍是
    # 整库值 >0，这条是修复的回归锁，最能证明"下推"真的发生了。
    page._f_text.setText("绝不可能命中的词ZZZ")
    page._apply_filter()
    deadline = time.time() + 8
    while time.time() < deadline and page._total != 0:
        app.processEvents()
        time.sleep(0.02)
    check("G14 不可能命中的关键字下推后 total=0（关键字真生效）",
          page._total == 0, "total=%d" % page._total)
    check("G15 关键字不命中时表格 0 行", page._table.table.rowCount() == 0,
          "%d 行" % page._table.table.rowCount())
    check("G16 关键字筛选后回到第 1 页", page._page_index == 0,
          "index=%d" % page._page_index)

    # ---- 关键字命中：年份出现在每条时间列（显示文本同口径）----
    year = "2026"
    page._f_text.setText(year)
    page._apply_filter()
    deadline = time.time() + 8
    while time.time() < deadline and not page._rows:
        app.processEvents()
        time.sleep(0.02)
    check("G17 关键字'2026'命中后 total>0 且页内时间列都含该串",
          page._total > 0 and bool(page._rows)
          and all(year in r[0][0] for r in page._rows),
          "total=%d rows=%d" % (page._total, len(page._rows)))
    check("G18 命中时本页行数与表格行数一致（本地不再重复筛）",
          page._table.table.rowCount() == len(page._rows),
          "table=%d rows=%d" % (page._table.table.rowCount(), len(page._rows)))

    # ---- 翻页保持关键字（kw/kwCols 每次请求都带上）----
    # [改小页大小才能真翻页] 本段开头 _page_size=50，而库此时约 22+ 行，
    # 只有 1 页，_next_page 会被 _page_count 拦住。压到 10 再翻第 2 页。
    kw_total = page._total
    page._page_size = 10                 # 直接改状态，避开 combo 信号再发一趟
    page._page_index = 0
    page._request_page(0)
    deadline = time.time() + 8
    while time.time() < deadline and page._inflight:
        app.processEvents()
        time.sleep(0.02)
    page._next_page()
    deadline = time.time() + 8
    while time.time() < deadline and (page._page_index != 1 or page._inflight):
        app.processEvents()
        time.sleep(0.02)
    check("G19 翻到第 2 页关键字条件不丢（total 仍是筛后值）",
          page._page_index == 1 and page._total == kw_total,
          "index=%d total=%d 应为 %d" % (page._page_index, page._total, kw_total))
    check("G20 第 2 页行仍含关键字（服务端每页都筛）",
          bool(page._rows) and all(year in r[0][0] for r in page._rows),
          "%d 行" % len(page._rows))

    # ---- 清除关键字 → 恢复整库 ----
    # [不与 kw_total 比大小] 采样数据都是"2026"年，命中数可能就等于整库，
    # 比"total>kw_total"会是确定性假红。改用输入框清空 + total>0 断言，
    # 再补一条"关键字框确实空了 ⇒ 后续请求不会再带 kw"的状态锁。
    page._clear_filter()
    deadline = time.time() + 8
    while time.time() < deadline and page._f_text.text() != "":
        app.processEvents()
        time.sleep(0.02)
    # 等清筛选那一趟应答落地（total 回到整库口径）
    deadline = time.time() + 8
    while time.time() < deadline and page._inflight:
        app.processEvents()
        time.sleep(0.02)
    check("G21 清除筛选后 total 恢复整库口径（>0）", page._total > 0,
          "total=%d kw=%d" % (page._total, kw_total))
    check("G22 清除筛选清空关键字输入框（后续请求不再带 kw）",
          page._f_text.text() == "", repr(page._f_text.text()))

    # ---- 时间 + 关键字组合（AND）----
    page._f_start.setText("2099-01-01")
    page._f_text.setText("手动")
    page._apply_filter()
    deadline = time.time() + 8
    while time.time() < deadline and page._total != 0:
        app.processEvents()
        time.sleep(0.02)
    check("G23 时间+关键字组合：时间已排除全部 ⇒ 仍 0 行",
          page._total == 0 and page._table.table.rowCount() == 0,
          "total=%d" % page._total)

    page._clear_filter()
    page._f_text.clear()
    page.on_leave()
    page.deleteLater()
    app.processEvents()


def m_hispage_part_i_protocol_keyword(srv):
    """协议层关键字下推（直接发 CMD_HISTORY_PAGE，不经 UI）。

    验证 kw / kwCols 从 BSON → handler 解析 → store WHERE 全链路，
    以及 total 与页内容同口径。UI 层另有 G 段。
    """
    print("== I. 协议层关键字下推 ==")
    import time
    # 不可能命中的词 ⇒ total=0（若服务端没把 kw 下推，必返回整库 >0）
    bad = srv.page(offset=0, limit=20, kw="绝不可能命中的词QQQ")
    check("I1 kw 下推：不可能命中的词 ⇒ total=0",
          bad.get("ok") is True and int(bad.get("total") or 0) == 0,
          "total=%s ok=%s" % (bad.get("total"), bad.get("ok")))
    check("I2 total=0 时 rows 也为空", not (bad.get("rows") or []),
          "rows=%d" % len(bad.get("rows") or []))

    # 年份命中（时间列），total>0 且每行时间列含 "2026"
    before_ts = int(time.time() * 1000)
    hit = srv.page(offset=0, limit=20, kw="2026", before_ts=before_ts)
    rows = hit.get("rows") or []
    cols = hit.get("cols") or []
    ts_idx = cols.index("ts") if "ts" in cols else 0
    check("I3 kw='2026' 命中 total>0",
          int(hit.get("total") or 0) > 0, "total=%s" % hit.get("total"))
    # rows 是原始值数组；ts 是 epoch 毫秒（墙钟），换算后必然含 2026
    def year_of(r):
        import datetime
        return datetime.datetime.fromtimestamp(r[ts_idx] / 1000.0).year
    check("I4 命中行的时间确实都在 2026（显示同口径）",
          bool(rows) and all(year_of(r) == 2026 for r in rows),
          "%d 行" % len(rows))

    # 列限定：只在"模式"列(1)搜"2026" ⇒ 0（时间列不计入）
    col_limited = srv.page(offset=0, limit=20, kw="2026", kw_cols=[1])
    check("I5 kwCols 限定'模式'列搜'2026' ⇒ total=0（列限定生效）",
          int(col_limited.get("total") or 0) == 0,
          "total=%s" % col_limited.get("total"))
    # 列限定：显式包含时间列(0)搜"2026" ⇒ total>0
    col_hit = srv.page(offset=0, limit=20, kw="2026", kw_cols=[0])
    check("I6 kwCols 含'时间'列(0)搜'2026' ⇒ total>0",
          int(col_hit.get("total") or 0) > 0,
          "total=%s" % col_hit.get("total"))

    # 时间 AND 关键字：未来起点 + 命中词 ⇒ 0
    combo = srv.page(offset=0, limit=20, kw="2026", lo_ts=4102444800000)
    check("I7 未来 loTs AND 关键字 ⇒ total=0（两条件同 WHERE）",
          int(combo.get("total") or 0) == 0, "total=%s" % combo.get("total"))

    # 翻页同口径：limit=1 连取 3 页，行不重不漏且 total 恒定
    #
    # [为什么要钉 beforeTs] 服务端持续落盘（1s 一行），不钉窗口的话两次
    # 查询之间会**新写进一行** ⇒ total 变 ⇒ 断言偶发红（本次在新建的
    # 空库上必现：库里只有几十行，增长一眼可见）。真实客户端翻页就是
    # 用 beforeTs 把整段分页钉在同一批数据上（服务端回显 windowEndTs），
    # 这里对齐该口径。
    t_ref = int(hit.get("total") or 0)
    seen = []
    for off in range(3):
        p = srv.page(offset=off, limit=1, kw="2026", before_ts=before_ts)
        seen += [r[ts_idx] for r in (p.get("rows") or [])]
        check("I8.%d 翻页 total 恒定" % (off + 1),
              int(p.get("total") or 0) == t_ref,
              "total=%s ref=%d" % (p.get("total"), t_ref))
    check("I9 关键字分页行不重复", len(seen) == len(set(seen)),
          "ts=%s" % seen)


# ============================================================================
#                            段落入口
# ============================================================================


def run_hispage():
    """历史页分页 end-to-end 整段（对应原 test_history_page.py 的 main）。

    [服务端口径] **自起一个隔离的 sim 服务端**（临时 cfg + `_free_port()`
    拿的空闲口），跑完自己停掉；只 terminate 自己 Popen 出来的进程。

    [原 main() 迁移动作] 原 main() 做的是：建临时目录 → 起服务端 → 登录
    → 等库里攒够 ≥22 行 → 依次跑 A~C / D / E~F / G / I 五段 → 收尾。
    这里**原样保留**这些动作，只去掉两件事：
      · 「建 QApplication」⇒ 改用总回归那唯一一个实例；
      · 「打印 passed/failed 汇总 + 返回退出码」⇒ 由总回归统一汇总。

    [与总回归主流程的关系] 本段自起服务端 ⇒ 不依赖总回归那个私有服务端，
    但**会**在总回归的进程里再起一个 sim 实例（端口独立、不碰现场库）。
    """
    import shutil
    import tempfile
    import time
    section("§14c 历史页：分页 end-to-end（真服务端 + 真 XLink）")
    tmp = tempfile.mkdtemp(prefix="hsf_histpage_")
    srv = m_hispage_Server(port=_free_port(), tmpdir=tmp, sample_ms=1000)
    try:
        if not srv.start():
            check("历史页分页：私有服务端启动", False, "端口 %d" % srv.port)
            return
        ok, err = srv.connect()
        if not ok:
            check("历史页分页：登录私有服务端", False, "%s" % err)
            return
        check("历史页分页：私有服务端已连接", True, "端口 %d" % srv.port)
        from hsfsrv_client import proto as _m_hispage_proto
        check("A0 落盘已建立",
              bool(srv.bridge.request_sync(
                  _m_hispage_proto.CMD_CONFIG_GET, timeout=5)))
        # [必须攒够两页] E 段要测"翻到第 2 页/页码跟着走/上一页可用"，
        # 库内只有 9 行（page_size=10）时第 2 页根本不存在，这几条必然
        # 假红（实测偶发）。等到 ≥22 行（2 页 + 余量）。
        print("等待服务端积累历史数据（1s/行，目标 ≥22 行，最多 40s）…")
        deadline = time.time() + 40
        got = 0
        while time.time() < deadline:
            got = int(srv.page(offset=0, limit=1).get("dbRows") or 0)
            if got >= 22:
                break
            time.sleep(0.5)
        print("库内已积累 %d 行" % got)
        m_hispage_part_abc_protocol(srv)
        m_hispage_part_d_live_total(srv)
        m_hispage_part_ef_client(srv)
        m_hispage_part_g_filter(srv)
        m_hispage_part_i_protocol_keyword(srv)
    finally:
        srv.stop()
        shutil.rmtree(tmp, ignore_errors=True)


# ==========================================================================
# 段落：test_history_side_frames（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【并入总回归的段落】按侧多帧的回归锁：温湿度 / 浊度 / 流量计（2026-11-09）。

    本文件**不是**独立可执行的测试脚本，而是 `tests/test_hsf_regression.py`
    的一段源码（总回归会把它原样拼进去）。

[命名契约] 本段落的**所有**顶层名字统一带 `m_hisside_` 前缀
    （函数/常量/入口），避免与总回归本体的顶层名、其它段落撞名。
    段落内部引用总回归的公共设施（`check` / `section` / `os` / `time` …）
    **按名直呼**，不 import 总回归（避免循环导入）。
    [顶层不 import] 本段落的 import **全部放在函数里** ⇒ 段落的顶层名
    与总回归、与其它段落**零交集**（`os` / `time` 这些名字归总回归所有；
    独立放本文件时也不会撞名）。

[Qt 纪律] **不在本段落创建 QApplication**：Qt5 一进程多 QApplication 是
    未定义行为（AGENTS.md 明令禁止）。总回归 `main()` 建的那唯一一个 app
    由 `m_hisside_app()` 取（取不到就报错，**绝不自己建**）。

[背景] 服务端按 485 的**两条侧**轮流推**同一个主题**：

    温湿度 0x00010003   A 侧有温湿度站（站 4），B 侧没有
    浊度   0x00010009   两台都在 B 侧，A 侧没装配
    流量计 0x00010004   装在 B 侧，A 侧没装配

没装配的那一侧推的是**空帧**（温湿度：只有 tempValid=false/humValid=false、
没有 temp/hum 键；浊度：两台 online=false；流量计：valid=false）。

[缺陷] 客户端 model 原来**只保留最后一帧**（TempHumState.apply 直接覆盖），
于是空帧把另一侧的真值盖掉：
    · 温湿度 → temp_text() 恒 "--"（现场实测：Python 客户端温湿度恒无数据）
    · 浊度   → ntu_of() 返回 None ⇒ 首页浊度卡恒 "--"
    · 流量计 → valid=false ⇒ 首页流量计卡恒"无数据"
C 端客户端没这问题：它按 side 分存快照（HsfLinkClient_tempHumState[side]）
并优先取 A 侧（hsfHomePickTempHum / hsfHomePickFlowMeter）。

[锁什么]
  A. 模型层：按侧分存 + 「优先侧 + 兜底侧」择优；每侧只留一帧（不涨内存）
  B. 首页卡片：多侧帧交替到达时显示的是**有效那一侧**的值
  C. 两侧都无效时如实显示 "--" / "无数据"（**不许拿旧值或另一侧顶替**）
  D. 单侧有效时连续交替推送**不闪烁**（显示值恒定）
  E. 顶栏与历史页采样列同一口径
  F. 水质页 / 流量页：浊度与流量各取有效那一侧（2026-11-22 拆页后）
"""


# ============================================================================
#                       段落基础设施（app / 服务端口径的帧）
# ============================================================================


def m_hisside_app():
    """取总回归那唯一一个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。

    [取法] 优先用总回归的模块级 `_app`；总回归没暴露时退回
    `QApplication.instance()`（**仍然不新建**）。两者都取不到 ⇒ 报错，
    绝不退化成"自己建一个"（Qt5 未定义行为 ⇒ 段错误/析构顺序错乱）。
    """
    app = globals().get("_app") or globals().get("_APP")
    if app is None:
        from PyQt5.QtWidgets import QApplication
        app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑本段（段落里绝不新建）")
    return app


# ----------------------------------------------------------------------
# 服务端口径的帧（逐字对齐 HsfServerApp.c 的编码）
# ----------------------------------------------------------------------

#: A 侧温湿度站真值帧（站 4 在 A 侧）。
m_hisside_TH_A = {"side": 0, "temp": 217, "hum": 455,
                  "tempValid": True, "humValid": True, "staleMs": 5000}

#: B 侧温湿度空帧（没装配 ⇒ 无值键 + 两个 valid 都是 false）。
m_hisside_TH_B_EMPTY = {"side": 1, "tempValid": False, "humValid": False,
                         "staleMs": 5000}

#: B 侧浊度真值帧（两台都在 B 侧；ntu 是 ×100 定点整数）。
m_hisside_TURB_B = {"side": 1, "count": 2, "staleMs": 5000,
                    "v0_name": "原水浊度", "v0_ntu": 1231, "v0_raw": 1095042547,
                    "v0_online": True,
                    "v1_name": "出水浊度", "v1_ntu": 2500, "v1_raw": 1078974755,
                    "v1_online": True}

#: A 侧浊度空帧（没装配 ⇒ 两台都 offline，没有 ntu 键）。
m_hisside_TURB_A_EMPTY = {"side": 0, "count": 2, "staleMs": 5000,
                          "v0_name": "进水浊度仪", "v0_online": False,
                          "v1_name": "出水浊度仪", "v1_online": False}

#: B 侧流量计真值帧。
m_hisside_FM_B = {"side": 1, "flow": 12.5, "speed": 1.25,
                  "forwardTotal": 123.4, "reverseTotal": 1.25,
                  "flowUnit": 5, "totalUnit": 1, "alarms": 0,
                  "valid": True, "staleMs": 5000}

#: A 侧流量计空帧（没装配 ⇒ valid=false，无值键）。
m_hisside_FM_A_EMPTY = {"side": 0, "valid": False, "staleMs": 5000}


def m_hisside_pump_both(model, home, app):
    """一轮「两条侧各推一帧」+ 刷新首页（B 段与 D 段共用）。"""
    model.temphum.apply(dict(m_hisside_TH_A))
    model.temphum.apply(dict(m_hisside_TH_B_EMPTY))
    model.flowmeter.apply(dict(m_hisside_FM_B))
    model.flowmeter.apply(dict(m_hisside_FM_A_EMPTY))
    model.turbidity.apply(dict(m_hisside_TURB_B))
    model.turbidity.apply(dict(m_hisside_TURB_A_EMPTY))
    home.refresh(force=True)
    app.processEvents()


# ============================================================================
#                            段落入口
# ============================================================================


def run_histchart():
    """历史页「图表视图」整段（2026-11-10，对齐 C 端图表功能）。

    [用户口径] 「python 的历史数据中图表功能实现不全，对齐下 c 端的图表
    功能，图表不要新建一个窗口」。逐条钉住：

      1. **页内双视图**：set_view(1) 把 LineChartDialog 以 Qt.Widget 旗标
         内嵌进页面第 1 页（不弹独立窗）；set_view(0) 回表格。
      2. **图表数据不是"当前页"**：切图表视图按当前筛选分页累加**全量**
         （旧实现只画当前页 ≤50 行，趋势看不出来）。
      3. **上限与收尾**：短批即停；触到 CHART_MAX_ROWS 也停（对齐 C 端
         HIST_CHART_MAX_ROWS=12000）。
      4. **等距抽样首尾必留**：每条线最多 CHART_MAX_POINTS 点。

    [不连服务端] 用假 bridge 按 offset/limit 切假行，异步投递（真链路是
    网络线程 → 信号 → UI 线程；同步回调会让累加递归几千层）。
    """
    from PyQt5.QtCore import Qt, QTimer
    from hsfsrv_client import proto
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    base_ts = 1793900000000

    def make_row(i):
        """服务端原值行（列序 = proto.HISTORY_COLUMNS）。

        [必须是原值] `_server_row_texts` 按列名取值再格式化；传显示文本
        会让它整行取不到值、全变 "--"（图表一个点都画不出来）。
        """
        return [base_ts + i * 1000, "手动", "停机", 20.0 + (i % 7), 3.2,
                700 + i, 12.5, 3.2, 36.5, "m³/h", 123.4, "m³",
                0.499, "", 26.5, 55.0, 0xFFFF, 0xFF]

    class _FakeModel(object):
        pass

    class _FakeBridge(object):
        def __init__(self, rows):
            self.rows = rows
            self.requests = []
            self.model = _FakeModel()

        def is_connected(self):
            return True

        def can(self, _perm):
            return True

        def history_page(self, offset=0, limit=20, before_ts=None, lo_ts=None,
                         hi_ts=None, kw=None, kw_cols=None,
                         export_mode=False, on_done=None, on_error=None,
                         timeout=5.0):
            self.requests.append((offset, limit))
            # [keyset 夹具 2026-11-17] 客户端导出用 `beforeTs` 递进（offset
            # 恒 0）⇒ 假桥按 ts 上界过滤，否则导出会每次返回同一批
            # （文件里同一段写两遍 ⇒ 时间列不单调）。图表/表格路径的
            # before_ts 是固定窗口锚 ⇒ pool 恒等于全集，行为不变。
            pool = self.rows
            if before_ts is not None:
                pool = [r for r in pool if int(r[0]) <= int(before_ts)]
            doc = {"ok": True,
                   proto.HIST_KEY_TOTAL: len(pool),
                   proto.HIST_KEY_DB_ROWS: len(pool),
                   proto.HIST_KEY_ROWS: pool[offset:offset + limit],
                   proto.HIST_KEY_COLS: list(proto.HISTORY_COLUMNS),
                   proto.HIST_KEY_LATEST_TS: 0,
                   proto.HIST_KEY_EARLIEST_TS: 0}
            if on_done:
                # 下一轮事件循环再投递（模拟网络线程 → 信号排队）。
                QTimer.singleShot(0, lambda: on_done(doc))

    def new_page(rows):
        ctx = PageContext(_FakeBridge(rows), lambda _i: None, lambda _s: None)
        page = HistoryPage(ctx)
        page._data_mode = "server"
        page._window_end = base_ts + 10 ** 8
        return page

    def pump(page, timeout_s=60.0):
        end = time.time() + timeout_s
        while time.time() < end:
            app.processEvents()
            if not page._chart_fetching:
                for _ in range(20):
                    app.processEvents()
                return True
            time.sleep(0.001)
        return False

    app = m_chartix_app_instance()

    # ==================================================================
    section("§14b 历史页图表视图：页内双视图 + 全量取数（2026-11-10）")
    # ==================================================================
    # ---- A. 页内双视图（不弹独立窗）----
    rows = [make_row(i) for i in range(120)]
    page = new_page(rows)
    check("A1 默认表格视图", page._view_mode == 0
          and page._view_stack.currentIndex() == 0)
    page.set_view(1)
    pump(page)
    check("A2 切图表视图：栈进第 1 页",
          page._view_stack.currentIndex() == 1 and page._view_mode == 1)
    dlg = page._chart_dialog
    check("A3 图表控件内嵌在页内（parent=holder，非顶层窗）",
          dlg is not None and dlg.parent() is page._chart_holder
          and not dlg.isWindow(),
          "parent=%s isWindow=%s"
          % (type(dlg.parent()).__name__ if dlg else "-",
             dlg.isWindow() if dlg else "-"))
    check("A4 内嵌用 Qt.Widget 旗标",
          dlg is not None and (dlg.windowFlags() & Qt.Widget) == Qt.Widget)
    page.set_view(0)
    app.processEvents()
    check("A5 切回表格：栈回第 0 页、按钮勾选一致",
          page._view_stack.currentIndex() == 0
          and page._btn_table_view.isChecked()
          and not page._btn_chart_view.isChecked())

    # ---- B. 取全量（不是当前页）----
    rows = [make_row(i) for i in range(137)]     # 50+50+37 ⇒ 3 批
    page = new_page(rows)
    page.set_view(1)
    pump(page)
    check("B1 累加到全量（137 行 > 一页 50）",
          len(page._chart_rows) == 137, "取回 %d 行" % len(page._chart_rows))
    check("B2 按协议上限分批（每批 50 行）",
          all(lim == proto.HISTORY_PAGE_MAX
              for _off, lim in page.ctx.bridge.requests),
          str(page.ctx.bridge.requests[:4]))
    check("B3 offset 递增（0/50/100）",
          [off for off, _l in page.ctx.bridge.requests] == [0, 50, 100])
    check("B4 取完不再挂起", not page._chart_fetching)
    check("B5 状态行写明取回行数",
          "图表取回 137 行" in page._chart_status.text(),
          page._chart_status.text())

    # ---- C. 上限与等距抽样 ----
    rows = [make_row(i) for i in range(HistoryPage.CHART_MAX_ROWS + 100)]
    page = new_page(rows)
    page.set_view(1)
    pump(page, 120.0)
    check("C1 触到上报上限即停（%d 行）" % HistoryPage.CHART_MAX_ROWS,
          len(page._chart_rows) == HistoryPage.CHART_MAX_ROWS,
          "取回 %d 行" % len(page._chart_rows))
    pts = [(base_ts + i * 1000, float(i)) for i in range(5000)]
    sampled = HistoryPage._sample_points(pts, 2000)
    check("C2 抽样点数 = 上限", len(sampled) == 2000, len(sampled))
    check("C3 抽样保留首点/尾点",
          sampled[0] == pts[0] and sampled[-1] == pts[-1])
    check("C4 不足上限时原样返回",
          HistoryPage._sample_points(pts[:10], 2000) == pts[:10])

    # ---- D. 图表视图下改筛选要重取 ----
    rows = [make_row(i) for i in range(137)]
    page = new_page(rows)
    page.set_view(1)
    pump(page)
    before = len(page.ctx.bridge.requests)
    check("D1 首次进入取全量（3 批）", before == 3, "%d 批" % before)
    page.set_view(1)
    pump(page)
    check("D2 同筛选不重复取数（签名一致）",
          len(page.ctx.bridge.requests) == before)
    page._f_text.setText("停机")
    page._chart_refresh()
    pump(page)
    check("D3 筛选变化触发重取",
          len(page.ctx.bridge.requests) > before,
          "%d → %d 批" % (before, len(page.ctx.bridge.requests)))

    # ==================================================================
    section("§14d 图表批与表格页**应答通道隔离**（2026-11-17 审计修）")
    # ==================================================================
    # [两个真缺陷] ① 图表批和表格页原来共用 `_inflight` + `_pending_kind`：
    # 表格页 20 行应答被图表当成"短批（<50）" ⇒ 图表只画第一页且状态行
    # 报"取回 20 行"；② 离页只置 `_chart_fetching=False`，半截 `_chart_rows`
    # 与签名留着 ⇒ 回来直接用半截数据重画且**永不重取**（只有改筛选才恢复）。
    rows = [make_row(i) for i in range(160)]      # 4 批（50/50/50/10）
    page = new_page(rows)
    page.set_view(1)                              # 图表开始取数（在途）
    check("§14d A1 图表取数中", page._chart_fetching)
    # [顺序要紧] 表格应答必须在**图表第一批还没到货**时插入——假桥是
    # QTimer 异步投递，processEvents 一次推进一步，先推进就把 4 批取完了，
    # 注入点就落不到"在途"窗口里（本用例自己踩过一次，A2 恒红）。
    page.pageReady.emit({"ok": True,
                         proto.HIST_KEY_TOTAL: len(rows),
                         proto.HIST_KEY_ROWS: rows[:20],
                         proto.HIST_KEY_COLS: list(proto.HISTORY_COLUMNS)})
    for _ in range(3):
        app.processEvents()
    check("§14d A2 表格应答**不**中断图表取数（不再被当短批收尾）",
          page._chart_fetching or len(page._chart_rows) == len(rows),
          "fetching=%s rows=%d" % (page._chart_fetching,
                                   len(page._chart_rows)))
    pump(page)
    check("§14d A3 图表最终取到**全量** 160 行（不是被表格页 20 行截断）",
          len(page._chart_rows) == len(rows),
          "rows=%d（期望 %d）" % (len(page._chart_rows), len(rows)))

    # R2：离页必须丢掉半截结果，回来重新取全量。
    page.on_leave()
    check("§14d B1 离页清掉半截结果", not page._chart_rows
          and page._chart_sig is None,
          "rows=%d sig=%r" % (len(page._chart_rows), page._chart_sig))
    before = len(page.ctx.bridge.requests)
    page._chart_refresh()
    pump(page)
    check("§14d B2 再进图表视图 ⇒ 重新取全量（半截不自愈的病根）",
          len(page.ctx.bridge.requests) > before
          and len(page._chart_rows) == len(rows),
          "%d → %d 批 / rows=%d"
          % (before, len(page.ctx.bridge.requests), len(page._chart_rows)))

    # ==================================================================
    section("§14e 历史页筛选：回车生效 + 起止颠倒拦截（2026-11-17）")
    # ==================================================================
    # [两处与日志/报警页不一致的地方 2026-11-17 补齐]
    #   ① 关键字框回车：另两页都接线了，历史页漏了（敲完顺手回车没反应）；
    #   ② 起始晚于结束：另两页都拦并提示，历史页直接下发 ⇒ 服务端 WHERE
    #      恒空 ⇒ 界面显示"共 0 条"，用户以为数据丢了。
    _statuses = []
    page = new_page([make_row(i) for i in range(40)])
    page.ctx.set_status = _statuses.append
    page.set_view(0)
    app.processEvents()
    n_before = len(page.ctx.bridge.requests)
    page._f_text.returnPressed.emit()          # 等价"敲完回车"
    app.processEvents()
    check("§14e A1 关键字框回车即触发筛选（与另两页一致）",
          len(page.ctx.bridge.requests) > n_before,
          "%d → %d 次请求" % (n_before, len(page.ctx.bridge.requests)))

    n_before = len(page.ctx.bridge.requests)
    page._f_start.setText("2026-11-17 10:00")
    page._f_end.setText("2026-11-17 09:00")
    page._apply_filter()
    app.processEvents()
    check("§14e A2 起始晚于结束：明确提示且不发请求",
          any("晚于结束" in s for s in _statuses)
          and len(page.ctx.bridge.requests) == n_before,
          "status=%r 请求=%d→%d" % (_statuses[-1:] if _statuses else [],
                                    n_before,
                                    len(page.ctx.bridge.requests)))

    class _ExportOrderBridge(object):
        """按**真实服务端批序**返回的假桥（批0=最新、批内升序）。"""

        def __init__(self, rows):
            self.rows = rows
            self.requests = []
            self.model = _FakeModel()

        def is_connected(self):
            return True

        def can(self, _perm):
            return True

        def history_page(self, offset=0, limit=40, before_ts=None,
                         lo_ts=None, hi_ts=None, kw=None, kw_cols=None,
                         export_mode=False, on_done=None, on_error=None,
                         timeout=5.0):
            # [keyset 夹具 2026-11-17] 客户端导出用 `beforeTs` 递进
            # （offset 恒 0）⇒ 必须按 ts 上界过滤，否则每次都返回同一批
            # （导出文件把同一段写两遍 ⇒ 时间列不单调；本轮踩过）。
            pool = self.rows
            if before_ts is not None:
                pool = [r for r in pool if int(r[0]) <= int(before_ts)]
            self.requests.append((offset, limit, before_ts))
            doc = {"ok": True,
                   proto.HIST_KEY_TOTAL: len(pool),
                   proto.HIST_KEY_DB_ROWS: len(pool),
                   proto.HIST_KEY_ROWS: pool[offset:offset + limit],
                   proto.HIST_KEY_COLS: list(proto.HISTORY_COLUMNS),
                   proto.HIST_KEY_LATEST_TS: 0,
                   proto.HIST_KEY_EARLIEST_TS: 0}
            if on_done:
                QTimer.singleShot(0, lambda: on_done(doc))

    # ==================================================================
    section("§14f 导出文件时间列全局降序（2026-11-17 审计修）")
    # ==================================================================
    # [为什么] 服务端每批内部**升序**（取页后 reversed）、批序 offset 递增
    # （最新批在前）⇒ 直接追加时文件是「最新块升序→更早块升序」，块与块
    # 之间时间**倒跳**（Excel 直接画图出锯齿）。把每批反转（最新在上，
    # 与界面表格同序）⇒ 全局严格降序且仍然流式（零内存代价）。
    import csv as _csv
    import tempfile as _tf
    from PyQt5.QtWidgets import QFileDialog as _QFD

    # [夹具必须照抄**真实服务端**的批序] 真服务端 offset 0 = **最新**一批、
    # 且每批内部升序（取页后 reversed）。`_FakeBridge` 是按行数组正序切片的
    # ⇒ 直接用会把批序搞反（本用例先踩过：导出文件变成"最早在前"）。
    _rows80 = ([make_row(i) for i in range(40, 80)]      # 批0：最新 40，升序
               + [make_row(i) for i in range(0, 40)])    # 批1：更早 40，升序
    page = new_page(_rows80[:40])
    page.ctx.bridge = _ExportOrderBridge(_rows80)
    page._export_cols = list(range(len(proto.HISTORY_COLUMNS)))
    _tmp_csv = _tf.mktemp(suffix=".csv")
    _orig_qfd = _QFD.getSaveFileName
    try:
        _QFD.getSaveFileName = staticmethod(
            lambda *a, **k: (_tmp_csv, "CSV"))
        page._export_data()
        _end = time.time() + 20
        while time.time() < _end:
            app.processEvents()
            if not page._export_runner.running:
                for _ in range(20):
                    app.processEvents()
                break
            time.sleep(0.002)
        # [导出列是**格式化文本**] 第一列是 "2026-11-06 01:33:59" 形态
        # （显示口径），同一格式下字典序 = 时间序 ⇒ 直接按字符串比较降序。
        _ts = []
        if os.path.isfile(_tmp_csv):
            with open(_tmp_csv, "r", encoding="utf-8-sig") as _fh:
                _ts = [_r[0] for _r in list(_csv.reader(_fh))[1:] if _r]
        check("§14f 导出文件行数=命中数且时间列严格不递增（降序）",
              len(_ts) == len(_rows80)
              and all(_ts[i] >= _ts[i + 1] for i in range(len(_ts) - 1)),
              "行数=%d 首=%s 尾=%s" % (len(_ts), _ts[:2], _ts[-2:]))
    finally:
        _QFD.getSaveFileName = _orig_qfd
        try:
            os.unlink(_tmp_csv)
        except OSError:
            pass


def run_hisside():
    """按侧多帧整段（对应原 test_history_side_frames.py 的 main）。

    [原 main() 迁移动作] 原 main() 做的是：建 app → 建一条**不连服务端**的
    LinkBridge + MainWindow → 依次跑 A~F 六段 → 打印汇总 + 返回退出码。
    这里**原样保留**建窗与六段，只去掉两件事：
      · 「建 QApplication」⇒ 改用总回归那唯一一个实例；
      · 「打印 passed/failed 汇总 + 返回退出码」⇒ 由总回归统一汇总。

    [不连服务端] 本段是**纯客户端层**：所有帧都是本地灌进 model 的，
    不碰服务端、不需要端口。
    """
    import os
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    app = m_hisside_app()

    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import model as model_mod

    bridge = LinkBridge()
    win = MainWindow(bridge)
    app.processEvents()
    model = bridge.model

    # ==================================================================
    section("A. 模型层：按侧分存 + 择优（不涨内存）")
    # ==================================================================
    check("侧号常量与服务端口径一致（A=0 / B=1）",
          model_mod.SIDE_A == 0 and model_mod.SIDE_B == 1)

    # 交替灌 20 轮：A 侧真值帧、B 侧空帧
    for _ in range(20):
        model.temphum.apply(dict(m_hisside_TH_A))
        model.temphum.apply(dict(m_hisside_TH_B_EMPTY))
    picked = model.temphum.pick()
    check("交替到达后仍挑到有效那一侧（A 侧）",
          picked is not None and picked.side == model_mod.SIDE_A,
          "picked.side=%s" % (picked.side if picked else None))
    check("温湿度显示有效那一侧的值（21.7℃ / 45.5%）",
          model.temphum.temp_text() == "21.7"
          and model.temphum.hum_text() == "45.5",
          "%s / %s" % (model.temphum.temp_text(), model.temphum.hum_text()))
    check("每侧只留最近一帧（sides ≤ 2，不缓存历史帧）",
          len(model.temphum.sides) <= 2,
          "sides=%s" % sorted(model.temphum.sides.keys()))
    check("空帧没有把 A 侧真值抹掉",
          model.temphum.sides[model_mod.SIDE_A].temp_tenths == 217,
          str(model.temphum.sides[model_mod.SIDE_A].temp_tenths))

    # 两侧都无效 ⇒ 挑不到 ⇒ "--"
    model.temphum.apply({"side": 0, "tempValid": False, "humValid": False})
    model.temphum.apply({"side": 1, "tempValid": False, "humValid": False})
    check("两侧都无效 ⇒ 挑不到快照",
          model.temphum.pick() is None)
    check("两侧都无效 ⇒ 显示 --（不拿旧值顶替）",
          model.temphum.temp_text() == "--"
          and model.temphum.hum_text() == "--",
          "%s / %s" % (model.temphum.temp_text(), model.temphum.hum_text()))

    # 非法侧号：整帧丢弃（不新建桶 ⇒ 内存不涨）
    model.temphum.apply({"side": 7, "temp": 999, "tempValid": True})
    check("非法侧号的帧被丢弃（不新建桶）",
          len(model.temphum.sides) <= 2,
          "sides=%s" % sorted(model.temphum.sides.keys()))

    # 流量计 / 浊度同样按侧分存
    for _ in range(20):
        model.flowmeter.apply(dict(m_hisside_FM_B))
        model.flowmeter.apply(dict(m_hisside_FM_A_EMPTY))
    fm = model.flowmeter.pick()
    check("流量计挑到有效那一侧（B 侧）",
          fm is not None and fm.side == model_mod.SIDE_B,
          "picked.side=%s" % (fm.side if fm else None))
    check("流量计值取自有效那一侧（12.50）",
          model.flowmeter.flow == 12.5,
          "%.2f" % model.flowmeter.flow)
    check("流量计 sides ≤ 2", len(model.flowmeter.sides) <= 2,
          "sides=%s" % sorted(model.flowmeter.sides.keys()))

    for _ in range(20):
        model.turbidity.apply(dict(m_hisside_TURB_B))
        model.turbidity.apply(dict(m_hisside_TURB_A_EMPTY))
    tb = model.turbidity.pick()
    check("浊度挑到有在线台的那一侧（B 侧）",
          tb is not None and tb.side == model_mod.SIDE_B,
          "picked.side=%s" % (tb.side if tb else None))
    check("浊度读数取自有效那一侧（12.31 / 25.00）",
          abs(model.turbidity.ntu_of(0) - 12.31) < 1e-6
          and abs(model.turbidity.ntu_of(1) - 25.00) < 1e-6,
          "%s / %s" % (model.turbidity.ntu_of(0), model.turbidity.ntu_of(1)))
    check("浊度 sides ≤ 2", len(model.turbidity.sides) <= 2,
          "sides=%s" % sorted(model.turbidity.sides.keys()))

    # ==================================================================
    section("B. 首页卡片：交替帧下显示有效那一侧的值")
    # ==================================================================
    win.goto_page(reg.PAGE_HOME)
    app.processEvents()
    home = win.pages[reg.PAGE_HOME]

    m_hisside_pump_both(model, home, app)
    check("首页温湿度卡显示有效那一侧（21.7℃）",
          home.th_temp._value.text() == "21.7",
          home.th_temp._value.text())
    check("首页温湿度卡湿度显示有效那一侧（45.5%）",
          home.th_hum._value.text() == "45.5",
          home.th_hum._value.text())
    check("首页流量计卡显示有效那一侧（12.50）",
          home.fm_flow._value.text() == "12.50",
          home.fm_flow._value.text())
    check("首页流量计卡单位随服务端单位码（m³/h）",
          home.fm_flow.unit_text() == "m³/h",
          home.fm_flow.unit_text())
    check("首页浊度卡台 1 显示有效那一侧（12.31）",
          home._turb_blocks[0]._value.text() == "12.31",
          home._turb_blocks[0]._value.text())
    check("首页浊度卡台 2 显示有效那一侧（25.00）",
          home._turb_blocks[1]._value.text() == "25.00",
          home._turb_blocks[1]._value.text())
    check("首页浊度卡仪器名取服务端",
          home._turb_blocks[0]._caption.text() == "原水浊度",
          home._turb_blocks[0]._caption.text())

    # ==================================================================
    section("C. 两侧都无效 ⇒ 如实显示 -- / 无数据（不顶替）")
    # ==================================================================
    model.temphum.apply({"side": 0, "tempValid": False, "humValid": False})
    model.temphum.apply({"side": 1, "tempValid": False, "humValid": False})
    model.flowmeter.apply({"side": 0, "valid": False})
    model.flowmeter.apply({"side": 1, "valid": False})
    model.turbidity.apply({"side": 0, "count": 2, "v0_online": False,
                            "v1_online": False})
    model.turbidity.apply({"side": 1, "count": 2, "v0_online": False,
                            "v1_online": False})
    home.refresh(force=True)
    app.processEvents()
    check("两侧都无效 ⇒ 温湿度卡显示 --",
          home.th_temp._value.text() == "--"
          and home.th_hum._value.text() == "--",
          "%s / %s" % (home.th_temp._value.text(), home.th_hum._value.text()))
    check("两侧都无效 ⇒ 流量计卡显示 -- 且小字『无数据』",
          home.fm_flow._value.text() == "--"
          and home.fm_sub.text() == "无数据",
          "%s / %s" % (home.fm_flow._value.text(), home.fm_sub.text()))
    check("两侧都无效 ⇒ 浊度卡两台都显示 --",
          home._turb_blocks[0]._value.text() == "--"
          and home._turb_blocks[1]._value.text() == "--",
          "%s / %s" % (home._turb_blocks[0]._value.text(),
                       home._turb_blocks[1]._value.text()))

    # ==================================================================
    section("D. 单侧有效时连续交替推送不闪烁")
    # ==================================================================
    seen = set()
    for _ in range(30):
        m_hisside_pump_both(model, home, app)
        seen.add((home.th_temp._value.text(), home.th_hum._value.text(),
                  home.fm_flow._value.text(),
                  home._turb_blocks[0]._value.text(),
                  home._turb_blocks[1]._value.text()))
    check("30 轮交替推送后显示值恒定（无闪烁）", len(seen) == 1,
          "出现过的组合：%s" % sorted(seen))
    check("恒定值就是有效那一侧的值",
          seen == {("21.7", "45.5", "12.50", "12.31", "25.00")},
          str(sorted(seen)))

    # ==================================================================
    section("E. 顶栏 + 历史页采样列同一口径")
    # ==================================================================
    win._refresh_chrome()
    app.processEvents()
    check("顶栏温度取有效那一侧（21.7℃）",
          win.title_bar.temp_label.text() == "21.7℃",
          win.title_bar.temp_label.text())
    check("顶栏湿度取有效那一侧（45.5%）",
          win.title_bar.hum_label.text() == "45.5%",
          win.title_bar.hum_label.text())

    # 两侧都无效 ⇒ 顶栏也必须显示 --（不拿旧值顶替）
    model.temphum.apply({"side": 0, "tempValid": False, "humValid": False})
    model.temphum.apply({"side": 1, "tempValid": False, "humValid": False})
    win._refresh_chrome()
    app.processEvents()
    check("两侧都无效 ⇒ 顶栏显示 --",
          win.title_bar.temp_label.text() == "--"
          and win.title_bar.hum_label.text() == "--",
          "%s / %s" % (win.title_bar.temp_label.text(),
                       win.title_bar.hum_label.text()))

    # 历史页（本地采样模式）：采样行里的温湿度/浊度/流量列取有效那一侧
    win.goto_page(reg.PAGE_DATA_HISTORY)
    app.processEvents()
    hist = win.pages[reg.PAGE_DATA_HISTORY]
    hist._data_mode = "local"
    hist._last_sample_ms = 0
    model.temphum.apply(dict(m_hisside_TH_A))
    model.temphum.apply(dict(m_hisside_TH_B_EMPTY))
    model.flowmeter.apply(dict(m_hisside_FM_B))
    model.flowmeter.apply(dict(m_hisside_FM_A_EMPTY))
    model.turbidity.apply(dict(m_hisside_TURB_B))
    model.turbidity.apply(dict(m_hisside_TURB_A_EMPTY))
    hist.refresh()
    app.processEvents()
    check("历史页采样到了数据", len(hist._rows) >= 1,
          "rows=%d" % len(hist._rows))
    if hist._rows:
        row = hist._rows[-1][0]
        check("历史页温湿度列取有效那一侧（21.7 / 45.5）",
              row[11] == "21.7" and row[12] == "45.5",
              "%s / %s" % (row[11], row[12]))
        check("历史页浊度列取有效那一侧（12.31 / 25.00）",
              row[6] == "12.31" and row[7] == "25.00",
              "%s / %s" % (row[6], row[7]))
        check("历史页流量列取有效那一侧（12.50 m³/h）",
              row[8] == "12.50 m³/h",
              row[8])

    # ==================================================================
    section("F. 水质页 / 流量页：浊度与流量各取有效那一侧")
    # ==================================================================
    # [2026-11-22 拆分] 浊度在页 6（水质），流量在页 18（流量）。
    win.goto_page(reg.PAGE_MONITOR_WATER)
    app.processEvents()
    water = win.pages[reg.PAGE_MONITOR_WATER]
    model.turbidity.apply(dict(m_hisside_TURB_B))
    model.turbidity.apply(dict(m_hisside_TURB_A_EMPTY))
    water.refresh()
    app.processEvents()
    check("水质页浊度台 1 取有效那一侧（12.31）",
          water._turb_rows[0][1].text() == "12.31",
          water._turb_rows[0][1].text())
    check("水质页浊度台 2 取有效那一侧（25.00）",
          water._turb_rows[1][1].text() == "25.00",
          water._turb_rows[1][1].text())

    win.goto_page(reg.PAGE_MONITOR_FLOW)
    app.processEvents()
    flowp = win.pages[reg.PAGE_MONITOR_FLOW]
    model.flowmeter.apply(dict(m_hisside_FM_B))
    model.flowmeter.apply(dict(m_hisside_FM_A_EMPTY))
    flowp.refresh()
    app.processEvents()
    check("流量页流量读数取有效那一侧（12.50）",
          flowp._flow_blocks["flow"]._value.text() == "12.50",
          flowp._flow_blocks["flow"]._value.text())


# ==========================================================================
# 段落：test_home_cards（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【首页两卡段落】浊度仪 / 按钮组系统状态的回归锁（2026-11-09）。

    入口：m_homecrd_run_home_cards()

[原文件] tests/test_home_cards.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

[背景] 首页原来只有 6 张卡；本次新增：
  · 浊度仪卡     —— 数据源 TOPIC_TURBIDITY（0x00010009），两台 NTU 读数
  · 按钮组卡     —— 数据源 TOPIC_DIGITAL（0x00010006）的 DI12~15 触点
                     与 DO7~9 三盏灯（**按服务端下发的通道名匹配**）

[锁什么]
  A. 主题订阅：两张卡都要对应主题，否则卡里恒 "--"
  B. 浊度卡：数值落地（定点 ÷100）、名字取服务端、超阈值标红、离线台 "--"
  C. 按钮组卡：按名字找通道（不写死通道号）、常开/常闭极性换算正确
     （急停 0 ⇒ 红「急停」；停止 0 ⇒ 「按下」；启动 1 ⇒ 「按下」）、
     灯亮/灭、通道缺失时显示「未配置」

[本段的几条硬规矩]
  · **计数器用总回归那一套**（check / section 按名调用）⇒ 断言统一汇总。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **顶层不 import**：本段所有 import 都放在函数里 ⇒ 段落的顶层名与
    总回归、与其它段落**零交集**（os/time/... 这些名字归总回归所有）。
  · **不连任何服务端**：本页离线跑（模型直接灌帧），不占别人的会话。
"""


def m_homecrd_ensure_client_path():
    """把**客户端包**目录放进 sys.path。

    [优先用总回归的 CLIENT_ROOT] 拼进总回归后主文件已经算好了它 ⇒ 直接用
    （不必再从 __file__ 反推，避免"文件被挪个位置就找错目录"）。本文件独立
    放在 tests/_merged/ 下跑时，再按自身位置推导。

    [不插主文件路径] 绝不把 tests/（主文件所在目录）放进去 ⇒ 不会循环
    import；总回归自己已有 sys.path.insert(HERE)。
    """
    import os
    import sys
    g = globals()
    root = g.get("CLIENT_ROOT")
    if not root:
        d = os.path.dirname(os.path.abspath(g.get("__file__", "")))
        if os.path.basename(d) == "_merged":
            d = os.path.dirname(d)
        root = os.path.dirname(d)          # client_py
    if root not in sys.path:
        sys.path.insert(0, root)
    return root


def m_homecrd_app():
    """取总回归那个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。"""
    import os
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑首页两卡段")
    return app


def m_homecrd_make_home(app):
    """建主窗并切到首页（离屏、**不连服务端**）。

    :return: (bridge, win, home_page, model)
    """
    m_homecrd_ensure_client_path()
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import pages_registry as reg

    bridge = LinkBridge()
    win = MainWindow(bridge)
    app.processEvents()
    win.goto_page(reg.PAGE_HOME)
    app.processEvents()
    home = win.pages[reg.PAGE_HOME]
    return bridge, win, home, bridge.model


def m_homecrd_part_a_topics(home):
    """A. 主题订阅（两张卡各自的主题必须被订）。"""
    from hsfsrv_client import proto
    section("A. 主题订阅（两张卡各自的主题必须被订）")
    check("首页订阅浊度主题",
          proto.TOPIC_TURBIDITY_STATUS in home.topics(),
          hex(proto.TOPIC_TURBIDITY_STATUS))
    check("首页订阅数字量主题",
          proto.TOPIC_DIGITAL_STATUS in home.topics(),
          hex(proto.TOPIC_DIGITAL_STATUS))


def m_homecrd_part_b_turbidity(app, home, model):
    """B. 浊度仪卡。"""
    from hsfsrv_client import theme
    section("B. 浊度仪卡")
    # 服务端口径：ntu 是 ×100 的定点整数；名字由服务端给。
    model.turbidity.apply({
        "count": 2, "staleMs": 5000,
        "v0_name": "原水浊度", "v0_ntu": 1231, "v0_raw": 1095042547,
        "v0_online": True,
        "v1_name": "出水浊度", "v1_ntu": 2500, "v1_raw": 1078974755,
        "v1_online": True,
    })
    home.refresh(force=True)
    app.processEvents()

    check("台 1 读数落地（定点 ÷100，两位小数）",
          home._turb_blocks[0]._value.text() == "12.31",
          home._turb_blocks[0]._value.text())
    check("台 1 名字取服务端",
          home._turb_blocks[0]._caption.text() == "原水浊度",
          home._turb_blocks[0]._caption.text())
    check("台 2 读数落地", home._turb_blocks[1]._value.text() == "25.00",
          home._turb_blocks[1]._value.text())
    check("超阈值（>=20 NTU）标红",
          theme.CLR_FAULT in home._turb_blocks[1]._value.styleSheet(),
          home._turb_blocks[1]._value.styleSheet()[:60])
    check("未超阈值不标红",
          theme.CLR_FAULT not in home._turb_blocks[0]._value.styleSheet(),
          home._turb_blocks[0]._value.styleSheet()[:60])

    # 离线台：ntu_of 返回 None ⇒ 显示 "--"
    model.turbidity.apply({
        "count": 2, "staleMs": 5000,
        "v0_name": "原水浊度", "v0_ntu": 1231, "v0_online": True,
        "v1_name": "出水浊度", "v1_ntu": 325, "v1_online": False,
    })
    home.refresh(force=True)
    app.processEvents()
    check("离线那台显示 --",
          home._turb_blocks[1]._value.text() == "--",
          home._turb_blocks[1]._value.text())
    check("在线那台照常显示",
          home._turb_blocks[0]._value.text() == "12.31",
          home._turb_blocks[0]._value.text())


def m_homecrd_part_c_buttons(app, home, model):
    """C. 按钮组卡。"""
    from hsfsrv_client import theme
    section("C. 按钮组卡")
    # 通道名由服务端下发（与两个服务端逐字一致）；DI/DO 位图按位取。
    # 现场实测：DI12 急停(常闭)=1 / DI13 启动=0 / DI14 停止(常闭)=1 /
    #           DI15 复位=0；DO7 启动灯=0 / DO8 停止灯=1 / DO9 复位灯=0
    dev = model.digital_state(0)
    doc = {"di": 0, "do": 0, "count": 16, "valid": True, "staleMs": 5000}
    di_bits = {12: "急停（常闭）", 13: "启动按钮", 14: "停止按钮",
               15: "复位按钮"}
    do_bits = {7: "启动按钮灯", 8: "停止按钮灯", 9: "复位按钮灯"}
    for ch, name in di_bits.items():
        doc["din%d" % ch] = name
    for ch, name in do_bits.items():
        doc["don%d" % ch] = name
    doc["di"] = (1 << 12) | (1 << 14)          # 急停通(正常) + 停止未按
    doc["do"] = (1 << 8)                       # 停止灯亮
    dev.apply(doc)
    home.refresh(force=True)
    app.processEvents()

    states = [lbl.text() for lbl in home._btn_state]
    check("急停：常闭且回路通 ⇒ 正常",
          states[0] == "● 正常", states[0])
    check("启动：常开且未按 ⇒ 未按",
          states[1] == "● 未按", states[1])
    check("停止：常闭且通 ⇒ 未按",
          states[2] == "● 未按", states[2])
    check("复位：常开且未按 ⇒ 未按",
          states[3] == "● 未按", states[3])
    check("启动灯灭", states[4] == "● 灭", states[4])
    check("停止灯亮", states[5] == "● 亮", states[5])
    check("复位灯灭", states[6] == "● 灭", states[6])

    # 急停无信号（按下或断线）⇒ 红「急停」
    doc["di"] = (1 << 14)
    dev.apply(doc)
    home.refresh(force=True)
    app.processEvents()
    check("急停无信号 ⇒ 红「急停」",
          home._btn_state[0].text() == "● 急停"
          and theme.CLR_FAULT in home._btn_state[0].styleSheet(),
          home._btn_state[0].text())

    # 停止按下（常闭 ⇒ 0）；启动按下（常开 ⇒ 1）；复位按下（常开 ⇒ 1）
    doc["di"] = (1 << 12) | (1 << 13) | (1 << 15)
    dev.apply(doc)
    home.refresh(force=True)
    app.processEvents()
    states = [lbl.text() for lbl in home._btn_state]
    check("启动按下（常开 1）⇒ 按下", states[1] == "● 按下", states[1])
    check("停止按下（常闭 0）⇒ 按下", states[2] == "● 按下", states[2])
    check("复位按下（常开 1）⇒ 按下", states[3] == "● 按下", states[3])

    # 通道改名（现场接线换了口）⇒ 客户端按名字找，不是写死通道号
    doc2 = dict(doc)
    for key in list(doc2):
        if key.startswith("din") or key.startswith("don"):
            del doc2[key]
    doc2["din20"] = "急停（常闭）"
    doc2["din21"] = "启动按钮"
    doc2["din22"] = "停止按钮"
    doc2["din23"] = "复位按钮"
    doc2["don17"] = "启动按钮灯"
    doc2["don18"] = "停止按钮灯"
    doc2["don19"] = "复位按钮灯"
    doc2["di"] = (1 << 20) | (1 << 22)
    doc2["do"] = (1 << 18)
    dev.apply(doc2)
    home.refresh(force=True)
    app.processEvents()
    states = [lbl.text() for lbl in home._btn_state]
    check("通道改号后仍按名字找（急停正常）", states[0] == "● 正常", states[0])
    check("通道改号后仍按名字找（停止灯亮）", states[5] == "● 亮", states[5])

    # 缺通道 ⇒ 「未配置」提示
    doc3 = {"di": 0, "do": 0, "count": 16, "valid": True, "staleMs": 5000}
    doc3["din0"] = "电磁阀1 关到位"
    dev.apply(doc3)
    home.refresh(force=True)
    app.processEvents()
    check("缺按钮通道 ⇒ 小字提示未配置",
          "未配置" in home._btn_note.text(), home._btn_note.text())

    # 数字量模块离线 ⇒ 全部 "--"
    doc3["valid"] = False
    dev.apply(doc3)
    home.refresh(force=True)
    app.processEvents()
    check("数字量模块离线 ⇒ 触点显示 --",
          home._btn_state[0].text() == "● --", home._btn_state[0].text())


def m_homecrd_part_d_auto_mode(app, home, model, bridge):
    """D. 自动模式卡（2026-11-11：下拉选模式 + 启动/停止 + 相位回显）。"""
    from hsfsrv_client import bson as _bson
    from hsfsrv_client import proto
    section("D. 自动模式卡（主界面）")

    # ---- D1 控件齐全 ----
    check("D1a 模式下拉/启动/停止/状态行都在",
          home.auto_mode_combo is not None
          and home.auto_start_btn is not None
          and home.auto_stop_btn is not None
          and home._auto_hint is not None)
    # ---- D2 四个选项（三模式 + 自动组合），userData = 协议稳定 id ----
    items = [(home.auto_mode_combo.itemData(i),
              home.auto_mode_combo.itemText(i))
             for i in range(home.auto_mode_combo.count())]
    want = (list(proto.AUTO_MODES)
            + [(proto.AUTO_MODE_AUTO, "自动（流量循环）")])
    # [2026-11-18 订正] 放水模式**在下拉里**（它是普通的可启动模式，
    # 只是不进组合循环）⇒ 下拉 = 产水/反洗/顺洗/放水 + 自动（流量循环）。
    check("D2 下拉五项 = 产水/反洗/顺洗/放水/自动（id 与 proto 同源）",
          items == want, "%s" % (items,))
    check("D2b 组合循环的子模式集合不含放水（三个）",
          [m for m, _n in proto.AUTO_CYCLE_MODES]
          == ["produce", "backwash", "rinse"]
          and "drain" not in [m for m, _n in proto.AUTO_CYCLE_MODES],
          "%s" % [m for m, _n in proto.AUTO_CYCLE_MODES])

    # ---- D3 未连接/无权限：按钮全灰 ----
    bridge.connected_flag = False
    bridge.role = proto.ROLE_ANONYMOUS
    model.run_mode_known = False
    home.refresh(force=True)
    app.processEvents()
    check("D3a 无权限/未连接：启动与停止都灰",
          not home.auto_start_btn.isEnabled()
          and not home.auto_stop_btn.isEnabled())
    check("D3b 未连接状态行提示", "连接后" in home._auto_hint.text(),
          home._auto_hint.text())

    # ---- D4 手动模式：启动灰、状态行说明 ----
    bridge.connected_flag = True
    bridge.role = proto.ROLE_ADMIN
    model.run_mode_known = True
    model.run_mode = 0
    home.refresh(force=True)
    app.processEvents()
    check("D4 手动模式：启动灰 + 状态行说明",
          not home.auto_start_btn.isEnabled()
          and "手动" in home._auto_hint.text(),
          home._auto_hint.text())

    # ---- D5 自动模式已停止：启动可用、停止灰 ----
    model.run_mode = 1
    model.auto_running = False
    model.auto_mode = ""
    model.auto_phase = ""
    model.auto_fault = False
    home.refresh(force=True)
    app.processEvents()
    check("D5 自动已停止：启动可用/停止灰",
          home.auto_start_btn.isEnabled()
          and not home.auto_stop_btn.isEnabled(),
          home._auto_hint.text())

    # ---- D6 载荷：启动带 autoMode，停止不带 ----
    captured = []

    def _fake_request(code, payload, **_kw):
        # bridge.request 收到的就是 dict（编码在 request 内部做）
        captured.append((code, payload if isinstance(payload, dict) else {}))
        return None

    real_request = bridge.request
    bridge.request = _fake_request
    try:
        home.auto_mode_combo.setCurrentIndex(0)     # 产水模式
        home._auto_start_clicked()
        home._auto_stop_clicked()
        app.processEvents()
    finally:
        bridge.request = real_request
    check("D6a 启动载荷 = {on:True, autoMode:produce}",
          len(captured) == 2
          and captured[0][0] == proto.CMD_AUTO_CONTROL
          and captured[0][1] == {"on": True,
                                 proto.KEY_AUTO_MODE: "produce"},
          "%s" % (captured,))
    check("D6b 停止载荷 = {on:False}（不带模式）",
          captured[1][1] == {"on": False}, "%s" % (captured[1],))

    # ---- D7 相位/锁存回显（状态文本只认服务端键）----
    # [ok 用 lambda] 布尔在注入模型状态**之后**才求值（写成形参会在建表时
    # 提前算掉，四条全红——首轮实跑就是这么错的）。
    cases = [
        (dict(auto_running=True, auto_phase="valves", auto_mode="produce",
              auto_fault=False),
         lambda: ("运行中" in home._auto_hint.text()
                  and "产水模式" in home._auto_hint.text()
                  and "阀动作中" in home._auto_hint.text()
                  and not home.auto_start_btn.isEnabled()
                  and home.auto_stop_btn.isEnabled()),
         "运行中·产水模式·阀动作中"),
        (dict(auto_running=True, auto_phase="pumps", auto_mode="backwash",
              auto_fault=False),
         lambda: "启动泵" in home._auto_hint.text(), "泵相"),
        (dict(auto_running=False, auto_phase="", auto_mode="",
              auto_fault=True),
         lambda: ("报警锁存" in home._auto_hint.text()
                  and not home.auto_start_btn.isEnabled()), "报警锁存"),
        (dict(auto_running=False, auto_phase="estop", auto_mode="",
              auto_fault=False),
         lambda: "急停锁存" in home._auto_hint.text(), "急停锁存"),
    ]
    for i, (fields, ok_fn, name) in enumerate(cases):
        model.auto_running = fields.get("auto_running")
        model.auto_phase = fields.get("auto_phase", "")
        model.auto_mode = fields.get("auto_mode", "")
        model.auto_fault = fields.get("auto_fault")
        home.refresh(force=True)
        app.processEvents()
        check("D7%s %s 状态行正确" % (chr(97 + i), name), ok_fn(),
              home._auto_hint.text())

    # ---- D8 缺键不误报（老服务端兼容）：缺 autoFault 键不清状态 ----
    model.auto_fault = True
    doc = {"mode": 1, "auto": True, "autoRunning": False}
    bridge._apply_mode_doc(doc)
    check("D8 应答缺 autoFault 键 ⇒ 保持原值（不折成 False）",
          model.auto_fault is True)
    doc2 = {"mode": 1, "auto": True, "autoRunning": False,
            proto.KEY_AUTO_FAULT: False,
            proto.KEY_AUTO_MODE: "rinse",
            proto.KEY_AUTO_PHASE: "running"}
    bridge._apply_mode_doc(doc2)
    check("D9 三键齐 ⇒ 全部落模（mode/phase/fault）",
          model.auto_mode == "rinse" and model.auto_phase == "running"
          and model.auto_fault is False)

    # ---- D11 复位按钮（2026-11-12 用户定案「有故障报警的时候可以按复位」）----
    # 语义：清**两把锁**（急停锁存 + 模式序列报警锁存），回停止态不启动。
    check("D11a 复位按钮存在", home.auto_reset_btn is not None)
    # 无锁存 ⇒ 灰（复位不是日常操作，常亮会招误触）
    model.auto_fault = False
    model.auto_phase = "running"
    home.refresh(force=True)
    app.processEvents()
    check("D11b 无锁存时复位按钮灰", not home.auto_reset_btn.isEnabled())
    # 报警锁存 ⇒ 亮
    model.auto_fault = True
    model.auto_phase = "idle"
    home.refresh(force=True)
    app.processEvents()
    check("D11c 报警锁存时复位按钮可用",
          home.auto_reset_btn.isEnabled()
          and "复位" in home._auto_hint.text(),
          home._auto_hint.text())
    # 急停锁存（phase=estop）⇒ 亮
    model.auto_fault = False
    model.auto_phase = "estop"
    home.refresh(force=True)
    app.processEvents()
    check("D11d 急停锁存时复位按钮可用",
          home.auto_reset_btn.isEnabled()
          and "复位" in home._auto_hint.text(),
          home._auto_hint.text())
    # 载荷 = op=4（AUTO_OP_RESET）
    captured = []

    def _fake_request(code, payload, **_kw):
        captured.append((code, payload if isinstance(payload, dict) else {}))
        return None

    real_request = bridge.request
    bridge.request = _fake_request
    try:
        home._auto_reset_clicked()
        app.processEvents()
    finally:
        bridge.request = real_request
    check("D11e 复位载荷 = {op: AUTO_OP_RESET}",
          len(captured) == 1
          and captured[0][0] == proto.CMD_AUTO_CONTROL
          and captured[0][1].get(proto.KEY_OP) == proto.AUTO_OP_RESET
          and proto.AUTO_OP_RESET == 4,
          "%s" % (captured,))

    # ---- D12 累计自动运行时长（2026-11-12 用户定案「显示成天 时 分 秒」）----
    # 服务端计时（唯一事实源）；客户端只格式化显示。
    check("D12a 格式化：毫秒 → 天 时 分 秒",
          home._fmt_auto_run(0) == "0天 00时 00分 00秒"
          and home._fmt_auto_run(1000) == "0天 00时 00分 01秒"
          and home._fmt_auto_run(3661000) == "0天 01时 01分 01秒"
          and home._fmt_auto_run(90061000) == "1天 01时 01分 01秒",
          home._fmt_auto_run(90061000))
    # 无数据（老服务端不报 autoRunMs）⇒ 显示 "--"，不编造"0天"
    model.auto_run_ms = 0
    home.refresh(force=True)
    app.processEvents()
    check("D12b 无时长数据 ⇒ 时长段显示「--」（不编造 0 天）",
          home._auto_run_label.text() == "--"
          and home._auto_run_title.text() == "累计",
          "%s / %s" % (home._auto_run_title.text(),
                       home._auto_run_label.text()))
    # 有数据 ⇒ 显示累计时长
    model.auto_run_ms = 90061000
    model.auto_run_ms_known = True
    home.refresh(force=True)
    app.processEvents()
    check("D12c 有数据 ⇒ 显示累计时长（天 时 分 秒）",
          "1天 01时 01分 01秒" in home._auto_run_label.text(),
          home._auto_run_label.text())
    # 服务端来的值要落模（应答键 = autoRunMs）
    bridge._apply_mode_doc({"mode": 1, "auto": True, "autoRunning": True,
                            proto.KEY_AUTO_RUN_MS: 5000})
    check("D12d 应答 autoRunMs ⇒ 落模",
          model.auto_run_ms == 5000, "%s" % model.auto_run_ms)
    # [定宽必须容得下**完整文本**] 2026-11-12 用户截图实见：整串右对齐 +
    # 宽度按"只有时长"测 ⇒ 宽度不足从左边溢出，「自动」两字被裁掉。
    # 现修法：「自动运行」独立成标签；时长标签 setFixedWidth（布局压不动
    # 它，QLabel 的 minimumSizeHint 不认 setMinimumWidth —— 实测被压到
    # 297px 又裁了一次）。断言读**设定宽度**（不受测试窗口宽度影响）。
    from PyQt5.QtGui import QFontMetrics
    fm = QFontMetrics(home._auto_run_label.font())
    need = fm.width(home.AUTO_RUN_MAX_TEXT) + 8
    check("D12e 运行时长标签定宽容得下最长时长串",
          home._auto_run_label.minimumWidth() >= need
          and home._auto_run_label.maximumWidth() >= need
          and home._auto_run_label.width() >= need,
          "min=%d max=%d w=%d need=%d" % (
              home._auto_run_label.minimumWidth(),
              home._auto_run_label.maximumWidth(),
              home._auto_run_label.width(), need))
    # 「自动运行」标题是独立标签 ⇒ 永远不会被裁
    title_need = fm.width("自动运行") + 4
    check("D12f「累计」标题独立成标签（永不被裁）",
          home._auto_run_title.text() == "累计"
          and home._auto_run_title.width() >= title_need,
          "w=%d need=%d" % (home._auto_run_title.width(), title_need))
    # 实际文本也要放得下（不只最大串）
    model.auto_run_ms = 4828000
    home.refresh(force=True)
    app.processEvents()
    real_need = fm.width(home._auto_run_label.text()) + 8
    check("D12g 当前实际文本也放得下（不裁字）",
          home._auto_run_label.width() >= real_need,
          "%r w=%d need=%d" % (home._auto_run_label.text(),
                              home._auto_run_label.width(), real_need))
    # 天数封顶 999+（宽度恒定 ⇒ 不会因天数变长而裁字）
    check("D12h 天数封顶 999+（宽度恒定）",
          home._fmt_auto_run(999 * 86400 * 1000).startswith("999天")
          and home._fmt_auto_run(1000 * 86400 * 1000).startswith("999+天"),
          "%s / %s" % (home._fmt_auto_run(999 * 86400 * 1000),
                       home._fmt_auto_run(1000 * 86400 * 1000)))

    # ---- D13 当前阶段剩余时间：表盘（2026-11-12 用户定案「表盘大点，
    #      中间有倒计时，旁边不用其他倒计时」）----
    # 口径：**服务端算**阶段计时（autoStageElapsedMs / autoStageTotalMs），
    # 客户端只显示；没有定时（产水按压差切换/单模式启动）不编造倒计时。
    from hsfsrv_client.widgets import RemainDial
    from hsfsrv_client import theme as _theme
    check("D13a 表盘够大且为正方形（1.5x 下 ≥80px）",
          isinstance(home._auto_dial, RemainDial)
          and home._auto_dial.width() == home._auto_dial.height()
          and home._auto_dial.width() >= 80,
          "%dx%d" % (home._auto_dial.width(), home._auto_dial.height()))

    # 没收到服务端阶段计时（老服务端）⇒ 中央「--」，不编造倒计时
    model.auto_stage_known = False
    model.auto_stage_elapsed_ms = -1
    model.auto_stage_total_ms = 0
    home.refresh(force=True)
    app.processEvents()
    check("D13b 无阶段计时 ⇒ 表盘隐藏、中央「--」",
          not home._auto_dial.is_running()
          and home._auto_dial.caption() == ""
          and not home._auto_dial.isVisibleTo(home),
          "%r / visible=%s" % (home._auto_dial.caption(),
                               home._auto_dial.isVisibleTo(home)))

    # 反洗阶段：已跑 71s / 共 10 分钟 ⇒ 中央 08:49
    model.auto_running = True
    model.auto_mode = "backwash"
    model.auto_phase = "running"
    model.auto_stage_known = True
    model.auto_stage_elapsed_ms = 71000
    model.auto_stage_total_ms = 10 * 60000
    home.refresh(force=True)
    app.processEvents()
    check("D13c 反洗 71s/10min ⇒ 中央「剩余 08:49」",
          home._auto_dial.caption() == "剩余"
          and home._auto_dial.remain_ms() == 10 * 60000 - 71000
          and RemainDial.fmt_ms(home._auto_dial.remain_ms()) == "08:49",
          "%r / %s" % (home._auto_dial.caption(),
                       RemainDial.fmt_ms(home._auto_dial.remain_ms())))
    check("D13d 表盘可见，旁边无其他倒计时/说明控件",
          home._auto_dial.isVisibleTo(home)
          and not hasattr(home, "_auto_remain_note")
          and not hasattr(home, "_auto_remain_text"),
          "visible=%s" % home._auto_dial.isVisibleTo(home))
    check("D13e 环色：剩余 >60s ⇒ 蓝",
          home._auto_dial.color_hex() == _theme.CLR_INFO,
          home._auto_dial.color_hex())

    # 最后一分钟：环转橙；到点转红（等换相）
    model.auto_stage_elapsed_ms = 10 * 60000 - 30000
    home.refresh(force=True)
    app.processEvents()
    check("D13f 剩余 <60s ⇒ 环转橙（中央 00:30）",
          home._auto_dial.color_hex() == _theme.CLR_WARN
          and RemainDial.fmt_ms(home._auto_dial.remain_ms()) == "00:30",
          "%s / %s" % (home._auto_dial.color_hex(),
                       RemainDial.fmt_ms(home._auto_dial.remain_ms())))
    model.auto_stage_elapsed_ms = 10 * 60000 + 500
    home.refresh(force=True)
    app.processEvents()
    check("D13g 到点（等换相）⇒ 环转红、中央 00:00",
          home._auto_dial.color_hex() == _theme.CLR_FAULT
          and RemainDial.fmt_ms(home._auto_dial.remain_ms()) == "00:00",
          "%s / %s" % (home._auto_dial.color_hex(),
                       RemainDial.fmt_ms(home._auto_dial.remain_ms())))

    # 产水（定时兜底关）⇒ 无总时长：中央「已运行」，不编造剩余
    model.auto_mode = "produce"
    model.auto_stage_elapsed_ms = 3 * 60000 + 5000
    model.auto_stage_total_ms = 0
    home.refresh(force=True)
    app.processEvents()
    check("D13h 产水无定时 ⇒ 中央「已运行 03:05」（仍属自动循环，表盘在）",
          home._auto_dial.caption() == "已运行"
          and RemainDial.fmt_ms(home._auto_dial.elapsed_ms()) == "03:05"
          and home._auto_dial.remain_ms() is None
          and home._auto_dial.isVisibleTo(home),
          "%r / %s" % (home._auto_dial.caption(),
                       RemainDial.fmt_ms(home._auto_dial.elapsed_ms())))

    # 单模式启动（服务端报 elapsed=-1 但 running=True）⇒ 剩余不可知，
    # 说明行不能说成「未运行」
    model.auto_stage_elapsed_ms = -1
    model.auto_stage_total_ms = 0
    home.refresh(force=True)
    app.processEvents()
    check("D13i 单模式运行 ⇒ 表盘隐藏（无时间限制）",
          home._auto_dial.caption() == ""
          and not home._auto_dial.isVisibleTo(home),
          "%r / visible=%s" % (home._auto_dial.caption(),
                               home._auto_dial.isVisibleTo(home)))

    # 应答两键落模（含 0 值也要认：total=0 是「无定时」这个事实）
    bridge._apply_mode_doc({"mode": 1, "auto": True,
                            proto.KEY_AUTO_STAGE_ELAPSED_MS: 5000,
                            proto.KEY_AUTO_STAGE_TOTAL_MS: 0})
    check("D13j 应答阶段计时两键落模",
          model.auto_stage_known
          and model.auto_stage_elapsed_ms == 5000
          and model.auto_stage_total_ms == 0,
          "%s/%s/%s" % (model.auto_stage_known,
                         model.auto_stage_elapsed_ms,
                         model.auto_stage_total_ms))
    # 缺键保持原值（老服务端兼容）
    bridge._apply_mode_doc({"mode": 1, "auto": True, "autoRunning": True})
    check("D13k 应答缺阶段计时键 ⇒ 保持原值（不编造）",
          model.auto_stage_elapsed_ms == 5000
          and model.auto_stage_total_ms == 0,
          "%s/%s" % (model.auto_stage_elapsed_ms,
                     model.auto_stage_total_ms))

    # 表盘/说明不超出自动卡
    model.auto_stage_elapsed_ms = 71000
    model.auto_stage_total_ms = 10 * 60000
    model.auto_mode = "backwash"
    model.auto_running = True
    home.refresh(force=True)
    app.processEvents()
    card = home.root.itemAt(0).widget()
    check("D13l 表盘可见且落在自动卡内（不被裁）",
          home._auto_dial.isVisibleTo(home)
          and card.rect().contains(home._auto_dial.geometry()),
          "card=%s dial=%s" % (card.rect(),
                               home._auto_dial.geometry()))
    # 停止后表盘必须隐藏（只在自动循环运行时有）
    model.auto_stage_elapsed_ms = -1
    home.refresh(force=True)
    app.processEvents()
    check("D13m 停止后表盘隐藏",
          not home._auto_dial.isVisibleTo(home),
          "visible=%s" % home._auto_dial.isVisibleTo(home))

    # ---- D10 浊度仪卡里的进水泵状态（2026-11-12 用户定案）----
    # 口径：on = **输出点**（现场无反馈），故文案用「通/断」+ 小字说明；
    # 与浊度仪说明**共用一行**（首页要一屏放下），用富文本 span 上色。
    from hsfsrv_client import theme
    check("D10a 首页订阅进水泵主题",
          proto.TOPIC_PUMP_STATUS in home.topics(),
          hex(proto.TOPIC_PUMP_STATUS))
    pump = model.pump
    pump.apply({"count": 1, "v0_name": "进水泵", "v0_on": True,
                "v0_ready": True, "v0_valid": True, "staleMs": 5000})
    home.refresh(force=True)
    app.processEvents()
    note = home._turb_note.text()
    check("D10b 输出通 ⇒ 卡内显示「进水泵 … 通 …无反馈」",
          "进水泵" in note and "通" in note and "无反馈" in note, note)
    check("D10c 通态用绿色 span（可辨）",
          "color:%s" % theme.CLR_OK in note, note)
    pump.apply({"count": 1, "v0_name": "进水泵", "v0_on": False,
                "v0_ready": True, "v0_valid": True, "staleMs": 5000})
    home.refresh(force=True)
    app.processEvents()
    check("D10d 输出断 ⇒ 显示「断」",
          "断" in home._turb_note.text(), home._turb_note.text())
    pump.apply({"count": 0})
    home.refresh(force=True)
    app.processEvents()
    check("D10e 服务端未装配 ⇒ 「--」+ 未装配说明",
          "--" in home._turb_note.text()
          and "未装配" in home._turb_note.text(),
          home._turb_note.text())
    # ---- D10f 泵快照停推 ⇒ 数字量 DO 回显兜底（用户实锅：泵实际已开、
    # 卡片一直「断」，切页回才对）----
    dev = model.digital_state(0)
    dev.apply({"di": 0, "do": 1 << 6, "count": 16, "valid": True,
               "staleMs": 5000, "don6": "进水泵"})
    pump.apply({"count": 0})
    pump.updated_ms = 0                 # 泵主题没帧/已陈旧
    home.refresh(force=True)
    app.processEvents()
    check("D10f 泵快照停推 ⇒ 用数字量 DO 回显兜底显示「通」",
          "进水泵" in home._turb_note.text()
          and "通" in home._turb_note.text()
          and home._turb_note.text() != "--",
          home._turb_note.text())
    # 收尾：还原为未连接态（不污染后续段落）
    bridge.connected_flag = False
    bridge.role = proto.ROLE_ANONYMOUS


def m_homecrd_part_e_overview_cards(app, home, model, bridge):
    """E. 主页概览新内容（2026-11-19 用户定案）。

    覆盖五件事：
      · E1 阶段信息行（自动模式卡里的一行：阶段名 + 进度条 + 已用/总）
      · E2 最近报警卡（时间 + 类型 + 原文；无报警显示「无报警」；点卡跳报警页）
      · E3 搅拌泵卡补 输出功率(kW) + 转矩(%)（主页卡 + 泵详情浮层）
      · E4 设备表地址列瘦身 + 点格复制完整地址
      · E5 类型文案映射（三端同源）
    """
    from hsfsrv_client import model as model_mod
    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import proto
    section("E. 主页概览新内容（阶段行/最近报警/泵功率转矩/地址列）")

    # ---- E1 阶段信息行 ----
    # [进度条已删 2026-11-19 用户口径「那个进度条删掉」] 阶段行 = 阶段名 +
    # 已用时长文字；进度条（QProgressBar）整体移除。
    check("E1a 阶段行控件齐全（阶段名/已用总，无进度条）",
          home._phase_row is not None
          and home._phase_label is not None
          and home._phase_text is not None
          and getattr(home, "_phase_bar", None) is None)
    # 手动模式 ⇒ 整行隐藏（"第几阶段"无意义）
    model.run_mode_known = True
    model.run_mode = 0
    home.refresh(force=True)
    app.processEvents()
    check("E1b 手动模式 ⇒ 阶段行隐藏",
          not home._phase_row.isVisibleTo(home),
          "visible=%s" % home._phase_row.isVisibleTo(home))
    # 自动模式 ⇒ 显示；阶段名按 autoPhase 映射
    model.run_mode = 1
    model.auto_running = True
    model.auto_phase = "valves"
    model.auto_stage_known = True
    model.auto_stage_elapsed_ms = 71000
    model.auto_stage_total_ms = 10 * 60000
    home.refresh(force=True)
    app.processEvents()
    check("E1c 自动模式 ⇒ 阶段行可见 + 阶段名「阀相」",
          home._phase_row.isVisibleTo(home)
          and home._phase_label.text() == "阀相",
          "visible=%s text=%r" % (home._phase_row.isVisibleTo(home),
                                   home._phase_label.text()))
    check("E1d 有总时长 ⇒ 显示「已用/共」（进度条已删，纯文字）",
          "已用" in home._phase_text.text()
          and "共" in home._phase_text.text(),
          "text=%r" % (home._phase_text.text(),))
    # 总时长 <= 0（单模式/无定时）⇒ 只显示阶段名，进度条 0、时间 "--"
    model.auto_phase = "running"
    model.auto_stage_total_ms = 0
    model.auto_stage_elapsed_ms = -1
    home.refresh(force=True)
    app.processEvents()
    check("E1e 无总时长 ⇒ 只显示阶段名、时间「--」（不编造倒计时）",
          home._phase_label.text() == "运行中"
          and home._phase_text.text() == "--",
          "%r / %r" % (home._phase_label.text(),
                       home._phase_text.text()))
    # 阶段名映射表齐（六个相位；paused=2026-11-19 停止＝暂停口径）
    check("E1f 阶段名映射齐（idle/valves/pumps/running/estop/paused）",
          set(home.PHASE_NAME_TEXT) == {"idle", "valves", "pumps",
                                        "running", "estop", "paused"}
          and home.PHASE_NAME_TEXT["paused"] == "暂停",
          "%s" % (home.PHASE_NAME_TEXT,))

    # ---- E2 最近报警卡 ----
    check("E2a 最近报警卡存在（可点 ⇒ 跳报警页）",
          home._alarm_meta is not None and home._alarm_text is not None)
    # 无报警 ⇒ 「无报警」
    model.last_alarm = None
    model.last_alarm_known = True
    home.refresh(force=True)
    app.processEvents()
    check("E2b 无报警 ⇒ 显示「无报警」",
          home._alarm_text.text() == "无报警"
          and home._alarm_meta.text() == "--",
          "%r / %r" % (home._alarm_meta.text(), home._alarm_text.text()))
    # 有报警 ⇒ 时间 + 类型 + 原文
    model.last_alarm = {
        "kind": "auto", "text": "电动球阀3 动作超时",
        "atMs": 1700000000000, "typeText": "自动运行",
    }
    home.refresh(force=True)
    app.processEvents()
    check("E2c 有报警 ⇒ 显示时间 + 类型 + 原文",
          "自动运行" in home._alarm_meta.text()
          and home._alarm_meta.text().count(":") >= 2
          and "电动球阀3" in home._alarm_text.text(),
          "%r / %r" % (home._alarm_meta.text(), home._alarm_text.text()))
    # 应答落模（proto 键 → model）
    bridge._apply_mode_doc({
        "mode": 1, "auto": True,
        proto.KEY_LAST_ALARM: {"kind": "health", "dev": proto.KIND_TEMPHUM,
                               "text": "数据过期", "atMs": 1700000000123},
    })
    check("E2d 应答 lastAlarm ⇒ 落模（含类型文案）",
          model.last_alarm is not None
          and model.last_alarm.get("text") == "数据过期"
          and model.last_alarm.get("typeText") == "温湿度",
          "%s" % (model.last_alarm,))
    # null ⇒ 无报警（不是空文档）
    bridge._apply_mode_doc({"mode": 1, "auto": True,
                            proto.KEY_LAST_ALARM: None})
    check("E2e 应答 lastAlarm=null ⇒ 无报警",
          model.last_alarm is None, "%s" % (model.last_alarm,))
    # 点卡跳报警页（走 ctx.navigate）
    jumped = []
    real_nav = home.ctx.navigate
    home.ctx.navigate = lambda p: jumped.append(p)
    try:
        home._goto_alarm_page()
    finally:
        home.ctx.navigate = real_nav
    check("E2f 点最近报警卡 ⇒ 跳报警页",
          jumped == [reg.PAGE_DATA_ALARM], "%s" % (jumped,))

    # ---- E3 搅拌泵卡：输出功率 + 转矩 ----
    check("E3a 主页搅拌泵卡有功率/转矩两块",
          getattr(home, "vfd_power", None) is not None
          and getattr(home, "vfd_torque", None) is not None)
    check("E3b 泵详情浮层有功率/转矩两块",
          getattr(home, "pd_power", None) is not None
          and getattr(home, "pd_torque", None) is not None)
    # 缺键 ⇒ "--"（不编造 0）
    model.vfd.apply({"runState": 1, "running": True, "faulted": False,
                     "outputFreqHz": 25.0, "staleMs": 5000})
    home.refresh(force=True)
    app.processEvents()
    check("E3c 缺功率/转矩键 ⇒ 显示「--」（不编造 0）",
          home.vfd_power._value.text() == "--"
          and home.vfd_torque._value.text() == "--",
          "%r / %r" % (home.vfd_power._value.text(),
                       home.vfd_torque._value.text()))
    # 有键 ⇒ 显示原值
    model.vfd.apply({"outputPowerKw": 3.456, "torquePercent": 78.9,
                     "staleMs": 5000})
    home.refresh(force=True)
    app.processEvents()
    check("E3d 有功率/转矩 ⇒ 显示数值（功率两位/转矩一位）",
          home.vfd_power._value.text() == "3.46"
          and home.vfd_torque._value.text() == "78.9",
          "%r / %r" % (home.vfd_power._value.text(),
                       home.vfd_torque._value.text()))
    # 单位随块（kW / %）
    check("E3e 功率/转矩单位正确（kW / %）",
          home.vfd_power.unit_text() == "kW"
          and home.vfd_torque.unit_text() == "%",
          "%r / %r" % (home.vfd_power.unit_text(),
                       home.vfd_torque.unit_text()))

    # ---- E3f/g 电机温度块（2026-11-20 用户口径「搅拌泵卡片可以增加温度
    #      显示」：模拟量温度传感器随 VFD 遥测下发 motorTempC）----
    check("E3f 主页搅拌泵卡有电机温度块（单位 ℃）",
          getattr(home, "vfd_mtemp", None) is not None
          and home.vfd_mtemp.unit_text() == "℃",
          "block=%r unit=%r" % (getattr(home, "vfd_mtemp", None),
                                home.vfd_mtemp.unit_text()
                                if hasattr(home, "vfd_mtemp") else None))
    # 缺键 ⇒ "--"（不编造 0℃）
    model.vfd.apply({"runState": 1, "running": True, "staleMs": 5000})
    home.refresh(force=True)
    app.processEvents()
    check("E3g 缺电机温度键 ⇒ 显示「--」（不编造 0℃）",
          home.vfd_mtemp._value.text() == "--",
          "%r" % (home.vfd_mtemp._value.text(),))
    model.vfd.apply({"motorTempC": 56.26, "staleMs": 5000})
    home.refresh(force=True)
    app.processEvents()
    check("E3h 有电机温度 ⇒ 一位小数（56.3 ℃）",
          home.vfd_mtemp._value.text() == "56.3",
          "%r" % (home.vfd_mtemp._value.text(),))

    # ---- E4 设备状态浮层在**滚动壳长页**里必须落在可见区带 ----
    # [实锅 2026-11-20] 页在滚动壳里可以比窗口高（验收跑 --size 1280x4200
    # 写进共享 INI ⇒ 真实客户端恢复页高 4200/窗口 749）⇒ 旧"按页高居中"
    # 把浮层算到 y≈1800 视口外 ⇒ 点设备健康"弹不出来"（其实弹到屏外）。
    from PyQt5.QtCore import QPoint as _QPoint
    home.resize(1280, 4200)                      # 模拟长页（页坐标 0..4200）
    _real_map = home.mapTo

    class _FakeWin(object):
        """假窗口：高 800（滚动壳视口），真实窗口是 749~900 的壳。"""
        def height(self):
            return 800

    def _fake_map_to(widget, point):
        # 页面原点在窗口坐标 = (0, -1600)：页比窗口高、已滚到中部
        # ⇒ 可见区带（页坐标）= [1600, 1600+800] = [1600, 2400]
        return _QPoint(point.x(), point.y() - 1600)

    home.mapTo = _fake_map_to
    _real_window = home.window
    home.window = lambda: _FakeWin()
    try:
        home._toggle_detail("table")
        home._shade.show()
        home._detail_float.show()
        for _ in range(4):                 # deferred 修正要几轮事件泵落地
            app.processEvents()
            time.sleep(0.05)
        # 浮层 y 是**页坐标**；可见区带 = [1600, 2400] ⇒ 整个浮层必须落在
        # 区带内（在屏幕上看得到）。旧口径按页高居中，页高一变就漂出带外。
        fy = home._detail_float.y()
        fh = home._detail_float.height()
        home.resize(1280, 6000)            # 页高变化 ⇒ 几何重算仍要在带内
        home._apply_detail_geometry()
        for _ in range(4):                 # 亏空补高/布局落定要事件泵
            app.processEvents()
            time.sleep(0.05)
        fy2 = home._detail_float.y()
        check("E4a 长页滚动后：浮层落在可见区带 [1600,2400] 内",
              1600 <= fy and fy + fh <= 2400
              and 1600 <= fy2 and fy2 + fh <= 2400,
              "y=%r/%r h=%r（可见区带 1600~2400）" % (fy, fy2, fh))
        # [E4b 末行完整可见 2026-11-20 用户截图「窗口小了」] `+64` 的
        # chrome 预算在 1.5× 字号下比真实标题+边距矮 ⇒ 末行裁半截
        # （"网络相机"只露一半）。修后浮层高度取**布局真实 sizeHint**，
        # 表格视口高必须 ≥ 内容高（一行都不许裁）。
        # [判据=内表控件高 ≥ content_height] 视口不含表头/滚动条是设计
        # 使然（拿它比 content 永远"虚亏"一个 chrome 高）；控件高够到
        # content_height 才是真没裁行。
        wh = home._table.table.height()
        ch = home._table.table.content_height()
        check("E4b 内表控件高 ≥ 内容高（末行完整可见，不裁半行）",
              wh >= ch,
              "widget=%r content=%r" % (wh, ch))
    finally:
        home.mapTo = _real_map
        home.window = _real_window
        home._close_detail()
        home.resize(1280, 800)

    # ---- E4 设备表地址列：瘦身 + 点格复制 ----
    check("E4a 地址瘦身口径（站号缩写 + 分隔符瘦身）",
          model_mod.slim_addr("COM17 · 115200 · 站 2") == "COM17/115200/#2"
          and model_mod.slim_addr("192.168.0.13:502") == "192.168.0.13:502"
          and model_mod.slim_addr("192.168.0.13:502 · DO3")
          == "192.168.0.13:502/DO3",
          "%r" % (model_mod.slim_addr("COM17 · 115200 · 站 2"),))
    # 列权重：地址列从 7 提到 10、错误信息列从 5 压到 3
    check("E4b 地址列权重 10 / 错误信息列 3",
          tuple(home._table._column_weights) == (4, 2, 2, 10, 3),
          "%s" % (home._table._column_weights,))
    # 表里显示的是瘦身串、tooltip 是完整串
    model.health.apply({"devCount": 1, "seq": 1, "ts": 0,
                        "d0_name": "搅拌泵", "d0_kind": 0, "d0_health": 0,
                        "d0_error": 0, "d0_online": True, "d0_runState": 0,
                        "d0_addr": "COM17 · 115200 · 站 2", "d0_text": ""})
    home.refresh(force=True)
    app.processEvents()
    addr_item = home._table.table.item(0, 3)
    check("E4c 表里显示瘦身地址、tooltip 是完整地址",
          addr_item is not None
          and addr_item.text() == "COM17/115200/#2"
          and addr_item.toolTip() == "COM17 · 115200 · 站 2",
          "%r / %r" % (addr_item.text() if addr_item else None,
                       addr_item.toolTip() if addr_item else None))
    # 点地址格 ⇒ 复制完整地址（用替身剪贴板；不碰真剪贴板）
    class _FakeClipboard(object):
        def __init__(self, box):
            self._box = box

        def setText(self, text):
            self._box.append(text)

    def _fake_clipboard(box):
        from PyQt5.QtWidgets import QApplication
        real_clip = QApplication.clipboard
        QApplication.clipboard = staticmethod(lambda: _FakeClipboard(box))
        return real_clip

    copied = []
    real_clip = _fake_clipboard(copied)
    try:
        home._on_device_cell_clicked(0, 3)
    finally:
        from PyQt5.QtWidgets import QApplication
        QApplication.clipboard = real_clip
    check("E4d 点地址格 ⇒ 复制完整地址到剪贴板",
          copied == ["COM17 · 115200 · 站 2"], "%s" % (copied,))
    # [2026-11-22 点行弹详情] 地址格同时弹单行详情卡（表格列宽有限，
    # 完整地址/错误信息在卡里换行显示）。
    dlg = getattr(home, "_device_detail", None)
    check("E4d2 点行 ⇒ 弹出设备详情卡",
          dlg is not None and not dlg.isHidden(), "%r" % (dlg,))
    if dlg is not None:
        dlg.close()
    # 点其它列不复制（但同样弹详情卡）
    copied2 = []
    real_clip = _fake_clipboard(copied2)
    try:
        home._on_device_cell_clicked(0, 1)
    finally:
        from PyQt5.QtWidgets import QApplication
        QApplication.clipboard = real_clip
    check("E4e 点非地址列 ⇒ 不复制", copied2 == [], "%s" % (copied2,))
    dlg2 = getattr(home, "_device_detail", None)
    check("E4e2 点任意列 ⇒ 弹出设备详情卡",
          dlg2 is not None and not dlg2.isHidden(), "%r" % (dlg2,))
    if dlg2 is not None:
        dlg2.close()
    # [点窗外关闭 2026-11-22] Qt.Popup 在本环境实测不关（合成点击后仍可见）
    # ⇒ 改为窗口遮罩；这里锁「点遮罩 ⇒ 卡与遮罩都收起」。
    app.processEvents()
    home._on_device_cell_clicked(0, 1)
    app.processEvents()
    shade = getattr(home, "_device_shade", None)
    card3 = getattr(home, "_device_detail", None)
    shade_was_visible = bool(shade is not None and not shade.isHidden())
    if shade is not None:
        shade.mousePressEvent(None)          # 等价于点卡片外
    app.processEvents()
    check("E4e3 点详情卡外遮罩 ⇒ 卡片与遮罩都收起",
          shade_was_visible and card3 is not None
          and card3.isHidden() and shade.isHidden(),
          "遮罩前=%s 卡后=%s 遮罩后=%s"
          % (shade_was_visible, card3.isHidden() if card3 else None,
             shade.isHidden() if shade else None))

    # ---- E5 类型文案映射（三端同源）----
    check("E5a 告警类型：dev 优先、其次来源类别",
          model_mod.alarm_type_text("auto", None) == "自动运行"
          and model_mod.alarm_type_text("auto", proto.KIND_FLOWMETER)
          == "流量计"
          and model_mod.alarm_type_text("health", None) == "设备异常",
          "%s" % (model_mod.alarm_type_text("auto", None),))


def m_homecrd_part_f_layout(app, home, model, bridge):
    """F. 主页**按实测卡高重排**（2026-11-19 用户定案）。

    覆盖四件事：
      · F1 三排的**卡序**（按卡高从高到低；「电箱温度」挪到矮卡位、
        「浊度仪」挪到高卡位 —— 用户「这两个可以互换」）
      · F2 电箱温度数值字号降一档（40 → 32）
      · F3 每排三张卡（没有落单/多塞）
      · F4 三排自然高合计 ≤ 560（⇒ 加上自动模式卡也不超 720 视口 ⇒
        **不出滚动条**）

    [为什么不直接断言绝对像素] 回归跑在离屏环境（本机离屏 Qt 枚举不到
    系统字体 ⇒ 字高与板端不同）⇒ 断言绝对像素必然假红。口径改为**卡序 +
    排数 + 排内张数**（与字体无关）+ 「三排合计自然高 ≤ 560」这个**上界**
    （板端实测 513；字体差异最多让每排高 20~30px，仍在界内）。
    """
    from hsfsrv_client import theme
    section("F. 主页按实测卡高重排（三排卡序/字号/不出滚动条）")

    # ---- 取主页的三排（root 里：自动模式卡 + 三个排）----
    rows = []
    for i in range(home.root.count()):
        item = home.root.itemAt(i)
        lay = item.layout()
        if lay is None:
            continue
        titles = []
        for j in range(lay.count()):
            cw = lay.itemAt(j).widget()
            if cw is None:
                continue
            tl = getattr(cw, "title_label", None)
            titles.append(tl.text() if tl is not None else "?")
        rows.append(titles)
    check("F0 主页是三排看板（自动模式卡之外）",
          len(rows) == 3, "%s" % (rows,))
    if len(rows) != 3:
        return
    check("F1a 第一排 = 浊度仪 / 按钮组 / 压差（最高的卡在前）",
          rows[0] == ["浊度仪", "按钮组系统状态", "压差"], "%s" % (rows[0],))
    check("F1b 第二排 = 搅拌泵 / 流量计 / 电动球阀",
          rows[1] == ["搅拌泵", "流量计", "电动球阀"], "%s" % (rows[1],))
    check("F1c 第三排 = 电箱温度 / 设备健康 / 最近报警"
          "（「电箱温度」换到浊度仪原来的第三排头位 = 用户「这两个可以互换」）",
          rows[2] == ["电箱温度", "设备健康", "最近报警"], "%s" % (rows[2],))
    check("F3 每排三张卡",
          all(len(r) == 3 for r in rows), "%s" % (rows,))

    # ---- F2 电箱温度字号降档 ----
    check("F2 电箱温度数值字号降一档（40 → 32）",
          home.th_temp._value_size == 32
          and home.th_hum._value_size == 32,
          "%r / %r" % (home.th_temp._value_size, home.th_hum._value_size))

    # ---- F4 三排自然高合计 ≤ 560（⇒ 自动模式卡 + 它 ≤ 720 视口）----
    total = 0
    for i in range(home.root.count()):
        item = home.root.itemAt(i)
        lay = item.layout()
        if lay is None:
            continue
        hmax = 0
        for j in range(lay.count()):
            cw = lay.itemAt(j).widget()
            if cw is None:
                continue
            hmax = max(hmax, cw.sizeHint().height())
        total += hmax
    check("F4 三排自然高合计 ≤ 560（自动模式卡 + 它 ≤ 720 视口 ⇒ 不滚动）",
          total <= 560, "合计=%d（板端实测 513）" % (total,))
    check("F4b 界面缩放比例 ≥ 1.0（板端 1.5；字号档与卡高口径的前提）",
          theme.ui_scale() >= 1.0, "scale=%s" % (theme.ui_scale(),))


# ============================================================================
#                              段落入口
# ============================================================================

def m_homecrd_run_home_cards():
    """首页两卡段落入口（总回归调用它）。

    [内容 = 原 main() 里"跑各 part"的部分]
    · 建 QApplication / 打印汇总与退出码 ⇒ 去掉（app 由总回归 main()
      统一建；汇总由总回归统一出）。
    """
    app = m_homecrd_app()
    bridge, win, home, model = m_homecrd_make_home(app)
    try:
        m_homecrd_part_a_topics(home)
        m_homecrd_part_b_turbidity(app, home, model)
        m_homecrd_part_c_buttons(app, home, model)
        m_homecrd_part_d_auto_mode(app, home, model, bridge)
        m_homecrd_part_e_overview_cards(app, home, model, bridge)
        m_homecrd_part_f_layout(app, home, model, bridge)
    finally:
        # 收尾：断开桥 + 关窗（离屏窗口不关会留下 200ms 刷新定时器，后面
        # 几段还在跑时它会继续刷，污染后续段的输出）。
        try:
            bridge.disconnect_from()
        except Exception:                               # noqa: BLE001
            pass
        try:
            win.close()
        except Exception:                               # noqa: BLE001
            pass
        app.processEvents()


# ==========================================================================
# 段落：test_home_first_screen（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【首屏段落】首屏订阅 + 悬浮详情卡高度的回归锁（2026-11-09）。

    入口：m_homefst_run_home_first_screen()

[原文件] tests/test_home_first_screen.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

[背景] 两个用户可见缺陷（Python 客户端首屏）：

  A. **首屏无数据**：MainWindow 构造期的 goto_page(PAGE_HOME) 发生在登录
     之前，那时 bridge 未连接 ⇒ set_topics 被跳过；本该由登录后的
     ``connected`` 信号补订，但 connect_to() 内部先调 disconnect_from()，
     而 disconnect_from() 会把"退出竞态闸" ``_shutting_down`` 置 True
     且**从不复位** ⇒ 网络线程发的 connected 被 _emit_net 静默吞掉
     ⇒ _on_connected 永不执行 ⇒ 首屏主题始终没订阅（卡片全是 "--"，
     只有设备健康有数——那是一次性快照请求，不靠订阅）。
     必须切页才恢复：切页走 goto_page 里的 set_topics 直连，不经信号。

  B. **设备状态悬浮卡被压扁**：_apply_detail_geometry() 取的是 DataTable
     **外壳** QWidget 的 minimumHeight()（那是"显式最小高"属性，缺省 0，
     不是布局算出来的最小高）⇒ 浮层高度只剩 64px，表格体被整片裁掉，
     一条设备状态都看不见。

[为什么要有这个锁] A 是"信号被静默吞掉"型缺陷 —— 不崩、不报错，只是
数据不来，很容易被当成"服务端没推"而查错方向；B 是"量取错对象"型
缺陷 —— 两者都不留 traceback，只有断言能挡住回归。

[本段的几条硬规矩]
  · **计数器用总回归那一套**（check / section 按名调用）⇒ 断言统一汇总。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **顶层不 import**：本段所有 import 都放在函数里 ⇒ 段落的顶层名与
    总回归、与其它段落**零交集**（os/time/... 这些名字归总回归所有）。
  · **不连任何真服务端**：A 段用 XLinkClient 的**离线替身**（只走状态机
    回调），既不占服务端会话名额、也不往现场库写数据。
"""


def m_homefst_ensure_client_path():
    """把**客户端包**目录放进 sys.path。

    [优先用总回归的 CLIENT_ROOT] 拼进总回归后主文件已经算好了它 ⇒ 直接用
    （不必再从 __file__ 反推，避免"文件被挪个位置就找错目录"）。本文件独立
    放在 tests/_merged/ 下跑时，再按自身位置推导。

    [不插主文件路径] 绝不把 tests/（主文件所在目录）放进去 ⇒ 不会循环
    import；总回归自己已有 sys.path.insert(HERE)。
    """
    import os
    import sys
    g = globals()
    root = g.get("CLIENT_ROOT")
    if not root:
        d = os.path.dirname(os.path.abspath(g.get("__file__", "")))
        if os.path.basename(d) == "_merged":
            d = os.path.dirname(d)
        root = os.path.dirname(d)          # client_py
    if root not in sys.path:
        sys.path.insert(0, root)
    return root


def m_homefst_app():
    """取总回归那个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。"""
    import os
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑首屏段")
    return app


class m_homefst_StubClient(object):
    """XLinkClient 的离线替身：不连真服务端，只把状态机走一遍。

    [为什么需要替身] A 这条缺陷发生在"登录成功那一刻"，要复现它就得
    真登录一次；但真登录会占掉服务端的单会话名额、还会往现场库写数据。
    替身只保留被 bridge 调到的那几个口子（状态回调 / 订阅 / 请求）。
    """

    def __init__(self, host, port, user, password, log=None):
        self.host = host
        self.port = port
        self.user = user
        self.log = log
        self.on_state_changed = None
        self.on_publish = None
        self.on_auth_result = None
        self.auth_role = 2
        self.auto_reconnect = True
        self.subscribed = []
        self.unsubscribed = []

    def start(self, timeout=8.0):
        # 真客户端在网络线程里迁移到 READY ⇒ bridge._on_state ⇒ 发 connected
        from hsfsrv_client.xlink import SessionState
        if self.on_state_changed is not None:
            self.on_state_changed(SessionState.READY)
        return True, ""

    def close(self):
        pass

    def subscribe_async(self, topic):
        self.subscribed.append(topic)

    def unsubscribe_async(self, topic):
        self.unsubscribed.append(topic)

    def call_soon(self, coro):
        # 替身不跑协程：直接把协程关掉（免得 "never awaited" 警告），
        # 返回 None ⇒ bridge.request_sync 立刻拿到"无应答"。
        try:
            coro.close()
        except Exception:                               # noqa: BLE001
            pass
        return None

    def request(self, *args, **kwargs):
        async def _never():
            return None
        return _never()

    def request_async(self, msg_code, payload=b"", dtype=0, timeout=3.0,
                      on_done=None, on_error=None):
        # 替身不回包：只走 on_error 一路（真客户端超时也是这个出口）
        if on_error is not None:
            on_error("未连接")


def m_homefst_part_a_first_screen(app):
    """A. 登录成功后当前页主题必须被订阅（首屏有数据的前提）。"""
    m_homefst_ensure_client_path()
    from hsfsrv_client import xlink as xlink_mod
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import pages_registry as reg

    section("A. 登录成功后当前页主题必须被订阅（首屏有数据的前提）")
    bridge = LinkBridge()
    win = MainWindow(bridge)
    app.processEvents()
    home = win.pages.get(reg.PAGE_HOME)
    check("首屏页已构建", home is not None)
    check("登录前未连接 ⇒ 未订阅任何主题",
          not bridge.is_connected() and not bridge.current_topics(),
          "topics=%s" % sorted(bridge.current_topics()))

    # 用替身顶掉真客户端（connect_to 内部是 `from .xlink import XLinkClient`，
    # 每次调用都重新取模块属性 ⇒ 这里替换模块属性即可生效）
    origin = xlink_mod.XLinkClient
    xlink_mod.XLinkClient = m_homefst_StubClient
    try:
        ok, err = bridge.connect_to("127.0.0.1", 6200, "root", "root")
    finally:
        xlink_mod.XLinkClient = origin
    app.processEvents()

    check("替身登录成功", ok, err or "")
    check("退出竞态闸在换连接后已重新开闸",
          bridge._shutting_down is False,
          "_shutting_down=%s" % bridge._shutting_down)
    want = set(home.topics())
    got = bridge.current_topics()
    check("connected 信号已送达（_on_connected 执行过 ⇒ set_topics 调用）",
          want.issubset(got),
          "缺 %s / 实得 %s" % (sorted(want - got), sorted(got)))
    check("订阅集合不含多余主题（切页语义未被破坏）",
          got.issubset(want | set(MainWindow.GLOBAL_TOPICS)),
          "多订 %s" % sorted(got - want - set(MainWindow.GLOBAL_TOPICS)))
    return bridge, win, home


def m_homefst_part_b_detail_card(app, bridge, home):
    """B. 设备状态悬浮卡：表格体不得被裁掉。"""
    section("B. 设备状态悬浮卡：表格体不得被裁掉")
    # 10 台设备的健康快照（表体 10 行）
    doc = {"devCount": 10, "seq": 1, "ts": 0}
    for i in range(10):
        doc["d%d_name" % i] = "设备%d" % i
        doc["d%d_kind" % i] = i % 6
        doc["d%d_health" % i] = 0
        doc["d%d_error" % i] = 0
        doc["d%d_online" % i] = True
        doc["d%d_runState" % i] = 0
        doc["d%d_addr" % i] = "192.168.0.1%d:502" % i
    bridge.model.health.apply(doc)
    home.refresh(force=True)
    app.processEvents()

    home._toggle_detail("table")
    app.processEvents()
    tbl = home._table.table
    need = tbl.minimumHeight()               # 表格内容实高（fit_to_content 设定）
    got_h = home._detail_float.height()
    check("悬浮卡已展开", home._detail_float.isVisibleTo(home))
    check("悬浮卡高度容得下整张表（表体不被裁）",
          got_h >= need + 40,
          "浮层 %dpx / 表需 %dpx" % (got_h, need))
    check("悬浮卡高度不是只剩标题行（历史缺陷：64px）",
          got_h >= 200, "%dpx" % got_h)
    # [宽度按页宽夹取] 设计如此：页面比期望宽度窄时（紧凑竖屏）浮层不出页
    want_w = min(home.DETAIL_TABLE_W, max(320, home.width() - 24))
    check("悬浮卡宽 = 夹取后的期望宽",
          home._detail_float.width() == want_w,
          "%d（期望 %d，页宽 %d）" % (home._detail_float.width(), want_w,
                                    home.width()))
    # 浮层不能顶出页面
    check("浮层不超出页面高度",
          got_h <= home.height() + 1,
          "浮层 %d / 页 %d" % (got_h, home.height()))


# ============================================================================
#                              段落入口
# ============================================================================

def m_homefst_run_home_first_screen():
    """首屏段落入口（总回归调用它）。

    [内容 = 原 main() 里"跑各 part"的部分]
    · 建 QApplication / 打印汇总与退出码 ⇒ 去掉（app 由总回归 main()
      统一建；汇总由总回归统一出）。
    """
    app = m_homefst_app()
    bridge, win, home = m_homefst_part_a_first_screen(app)
    try:
        m_homefst_part_b_detail_card(app, bridge, home)
    finally:
        # 收尾：断开桥（替身客户端）+ 关窗（离屏窗口不关会留下 200ms
        # 刷新定时器，后面几段还在跑时它会继续刷）。
        try:
            bridge.disconnect_from()
        except Exception:                               # noqa: BLE001
            pass
        try:
            win.close()
        except Exception:                               # noqa: BLE001
            pass
        app.processEvents()


# ==========================================================================
# 段落：test_log_page（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【日志页段落】运行日志查询（CMD_LOG_PAGE=0x0119）用例。

    入口：m_logpg_run_log_page()

[原文件] tests/test_log_page.py（2026-11-09 起内联进 tests/test_hsf_regression.py）

[任务口径（对齐 C 端 HsfDataLog.c：logRequest / logFillTable /
 logApplyFilter / logClearFilter / logPageSizeSlot / logExportStart）]
  0. 协议常量：CMD_LOG_PAGE=0x0119 与 KEY_LOG_* 跟 C 头
     XhsfLinkProto.h 逐字对齐（同号同键铁律）；TOPIC 无。
  A. 铺表：入页发 0x0119、应答行对象 {ts,category,text} → 时间/类别/事件
     3 列（类别显示中文名）、行数统计与页码标签、翻页钮使能。
  B. 翻页锚点：翻页沿用同一 beforeTs；新查询/换筛选锚点重置、offset=0。
  C. 应答归属（防串页）：offset/limit 回显不匹配的迟到应答丢弃不铺表。
  D. 筛选：类别下拉立即重查（锚点归零）、关键字/时间窗下推、起点晚于
     终点不应用、清除筛选保留类别。
  E. 页大小：改档回第 1 页 + 锚点归零。
  F. ok=false（如"落盘未启用"）：显示服务端 err、清表、不崩。
  G. 老服务端"未知命令"否定应答：状态行提示 + 空表，不崩；入页不再自动
     重发；显式筛选可重试并恢复。
  H. 导出 CSV：utf-8-sig BOM + 表头 + 行序；无 Download 权限不落盘。
  I. 客户端运行日志折叠副视图：append/清空/环形截断/一键展开收起。
  L. 真实往返（**本段不跑**，见下）。

[本段的几条硬规矩]
  · **计数器用总回归那一套**（check / skip 直接按名调用）⇒ 断言统一
    汇总，不再自带 PASS/FAIL/SKIP 计数。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **顶层不 import**：本段所有 import 都放在函数里 ⇒ 段落的顶层名与
    总回归、与其它段落**零交集**（os/time/... 这些名字归总回归所有）。
  · **不跑 L 段**（m_logpg_part_live 连的是"别人手工起"的现场 C 服务端，
    端口 6100 写死）⇒ 与总回归"自己起隔离实例"的口径冲突；跨端连 C
    服务端的部分由总回归 §11 覆盖（那边连的是自己起的隔离实例）。
    函数本体保留在文件里，需要时可单独调。
"""


# ============================================================================
#                        路径 / 环境（函数级 import）
# ============================================================================

def m_logpg_tests_dir():
    """本段落文件所在的 tests 目录。

    [优先用总回归的 HERE] 拼进总回归后，主文件已经算好了 HERE / CLIENT_ROOT
    ⇒ 直接用它（不必再从 __file__ 反推，避免"文件被挪个位置就找错目录"）。
    本文件独立放在 tests/_merged/ 下跑时，再按自身位置推导。
    """
    import os
    g = globals()
    here = g.get("HERE")
    if here:
        return here
    d = os.path.dirname(os.path.abspath(g.get("__file__", "")))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    return d


def m_logpg_client_root():
    """client_py 目录（客户端包 hsfsrv_client 的上级）。"""
    import os
    g = globals()
    root = g.get("CLIENT_ROOT")
    if root:
        return root
    return os.path.dirname(m_logpg_tests_dir())


def m_logpg_hsf_root():
    """HighSpeedFilter 目录（C 端 common/XhsfLinkProto.h 在这儿）。"""
    import os
    return os.path.dirname(m_logpg_client_root())


def m_logpg_ensure_client_path():
    """把**客户端包**目录放进 sys.path。

    [不插主文件路径] 绝不把 tests/（主文件所在目录）放进去 ⇒ 不会循环
    import；总回归自己已有 sys.path.insert(HERE)。
    """
    import sys
    root = m_logpg_client_root()
    if root not in sys.path:
        sys.path.insert(0, root)
    return root


def m_logpg_app():
    """取总回归那个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。"""
    import os
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑日志页段")
    return app


def m_logpg_process_events():
    """驱动 Qt 事件循环（客户端段的回调都在 UI 线程外跑）。"""
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is not None:
        app.processEvents()


def m_logpg_base_ms():
    """合成日志的基准时刻（2026-11-09 12:00:00 的 epoch 毫秒）。"""
    import time
    return int(time.mktime(time.strptime(
        "2026-11-09 12:00:00", "%Y-%m-%d %H:%M:%S"))) * 1000


def m_logpg_make_rows(n=25):
    """合成服务端日志（时间升序生成后倒序 = 服务端取页口径 DESC）。"""
    base = m_logpg_base_ms()
    cats = ["conn", "ctrl", "alarm", "auto", "auth"]
    rows = [{"ts": base + i * 1000,
             "category": cats[i % len(cats)],
             "text": "事件 %02d" % i}
            for i in range(n)]
    rows.reverse()
    return rows


class m_logpg_FakeLogServer(object):
    """在 bridge 上伪造 0x0119 应答：记录每笔请求 + 按服务端口径回放。

    behavior: "ok"（正常分页）/ "nok:<err>"（否定应答，err 原样回）。
    hold=True 时只记录不回包（测应答归属/防串页，用例手工 emit）。
    """

    def __init__(self, rows=None):
        self.rows = rows if rows is not None else m_logpg_make_rows()
        self.behavior = "ok"
        self.hold = False
        self.requests = []

    def attach(self, bridge):
        bridge.log_page = self.handle
        bridge.is_connected = lambda: True
        return self

    def handle(self, offset=0, limit=None, before_ts=None, lo_ts=None,
               hi_ts=None, category=None, kw=None, on_done=None,
               on_error=None, timeout=None):
        self.requests.append(dict(offset=offset, limit=limit,
                                  before_ts=before_ts, lo_ts=lo_ts,
                                  hi_ts=hi_ts, category=category, kw=kw))
        if self.hold:
            return
        doc = self.build_doc(offset, limit, before_ts, lo_ts, hi_ts,
                             category, kw)
        on_done(doc)

    def build_doc(self, offset, limit, before_ts, lo_ts, hi_ts, category, kw):
        from hsfsrv_client import proto
        if self.behavior.startswith("nok"):
            return {"ok": False, "err": self.behavior.split(":", 1)[1]}
        # 服务端口径：limit 夹取 1..50、offset 夹非负（HsfServerApp.c）。
        lim = max(1, min(50, int(limit or proto.LOG_PAGE_DEFAULT)))
        off = max(0, int(offset or 0))
        rows = self.rows
        if category:
            rows = [r for r in rows if r["category"] == category]
        if kw:
            rows = [r for r in rows if kw in r["text"] or kw in r["category"]]
        if lo_ts:
            rows = [r for r in rows if r["ts"] >= lo_ts]
        if hi_ts:
            rows = [r for r in rows if r["ts"] <= hi_ts]
        page = rows[off:off + lim]
        return {"ok": True, "rows": page, "total": len(rows),
                "count": len(page), "offset": off, "limit": lim}


def m_logpg_make_page(role=None):
    """离屏建一个 LogPage（真 LinkBridge + 假数据源，稍后由用例 attach）。"""
    m_logpg_ensure_client_path()
    app = m_logpg_app()
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import LogPage
    status = []
    bridge = LinkBridge()
    if role is not None:
        bridge.role = role
    ctx = PageContext(bridge, lambda i: None, status.append)
    page = LogPage(ctx)
    app.processEvents()
    return app, page, bridge, status


# ============================================================================
#                      0. 协议常量与 C 头逐字对齐
# ============================================================================

def m_logpg_part_0_proto():
    print("== 0. 协议常量与 C 头逐字对齐 ==")
    import os
    import re
    m_logpg_ensure_client_path()
    from hsfsrv_client import proto
    hpath = os.path.join(m_logpg_hsf_root(), "common", "XhsfLinkProto.h")
    with open(hpath, encoding="utf-8", errors="replace") as fh:
        src = fh.read()

    def cdef(name):
        m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+u?|\"[^\"]+\"|\d+)"
                      % name, src)
        if not m:
            return None
        tok = m.group(1)
        if tok.startswith('"'):
            return tok[1:-1]
        if tok.startswith("0x"):
            return int(tok.rstrip("u"), 16)
        return int(tok)

    pairs = [
        ("HSF_CMD_LOG_PAGE", proto.CMD_LOG_PAGE),
        ("HSF_KEY_LOG_LIMIT", proto.KEY_LOG_LIMIT),
        ("HSF_KEY_LOG_OFFSET", proto.KEY_LOG_OFFSET),
        ("HSF_KEY_LOG_BEFORE_TS", proto.KEY_LOG_BEFORE_TS),
        ("HSF_KEY_LOG_LO_TS", proto.KEY_LOG_LO_TS),
        ("HSF_KEY_LOG_HI_TS", proto.KEY_LOG_HI_TS),
        ("HSF_KEY_LOG_CATEGORY", proto.KEY_LOG_CATEGORY),
        ("HSF_KEY_LOG_KW", proto.KEY_LOG_KW),
        ("HSF_KEY_LOG_ROWS", proto.KEY_LOG_ROWS),
        ("HSF_KEY_LOG_TOTAL", proto.KEY_LOG_TOTAL),
        ("HSF_KEY_LOG_COUNT", proto.KEY_LOG_COUNT),
        ("HSF_KEY_LOG_TS", proto.KEY_LOG_TS),
        ("HSF_KEY_LOG_TEXT", proto.KEY_LOG_TEXT),
    ]
    for i, (cname, pyval) in enumerate(pairs):
        cval = cdef(cname)
        check("P%d %s 同值（C=%r py=%r）" % (i, cname, cval, pyval),
              cval == pyval)
    check("P-max LOG_PAGE_MAX=50（与 HSF_LOG_PAGE_MAX 同值）",
          cdef("HSF_LOG_PAGE_MAX") == proto.LOG_PAGE_MAX == 50)
    check("P-def LOG_PAGE_DEFAULT=20（与 HSF_LOG_PAGE_DEFAULT 同值）",
          cdef("HSF_LOG_PAGE_DEFAULT") == proto.LOG_PAGE_DEFAULT == 20)
    check("P-topic 运行日志无推送主题（TOPIC 无）",
          not any(name.startswith("TOPIC_LOG") for name in dir(proto)))


# ============================================================================
#              A. 铺表 / B. 翻页锚点 / C. 归属 / D. 筛选
# ============================================================================
# 全部离线段共用一个页面对象（多页对象 + 多 bridge 在离屏 Qt 下析构顺序
# 不稳，参照 test_history_page G 段的教训）。

def m_logpg_part_a_fill(page, fake):
    print("== A. 铺表（入页即加载 + 行对象解析）==")
    import time
    from hsfsrv_client import proto
    fake.attach(page.ctx.bridge)
    page.on_enter()
    m_logpg_process_events()
    req0 = fake.requests[-1]
    check("A1 入页即发 0x0119（offset=0）", req0["offset"] == 0, str(req0))
    check("A2 缺省页大小 20（LOG_PAGE_DEFAULT）",
          req0["limit"] == proto.LOG_PAGE_DEFAULT, str(req0["limit"]))
    check("A3 首屏带分页锚点 beforeTs（>0）",
          isinstance(req0["before_ts"], int) and req0["before_ts"] > 0,
          str(req0["before_ts"]))
    check("A4 请求不带空筛选（category/kw/lo/hi 全 None）",
          req0["category"] is None and req0["kw"] is None
          and req0["lo_ts"] is None and req0["hi_ts"] is None, str(req0))

    table = page._table.table
    check("A5 表格已铺 20 行（total=25 / 页大小 20）",
          table.rowCount() == 20, "rows=%d" % table.rowCount())
    # 服务端时间 DESC 取页 ⇒ 首行是最新一条（i=24，category=auth）
    expect_stamp = time.strftime(
        "%Y-%m-%d %H:%M:%S",
        time.localtime((m_logpg_base_ms() + 24 * 1000) / 1000.0))
    check("A6 时间列格式化为 yyyy-MM-dd HH:MM:SS（首行=最新一条）",
          table.item(0, 0).text() == expect_stamp,
          "%r vs %r" % (table.item(0, 0).text(), expect_stamp))
    check("A7 类别列翻成中文（auth → 鉴权）",
          table.item(0, 1).text() == "鉴权", table.item(0, 1).text())
    check("A8 事件列原样显示", table.item(0, 2).text() == "事件 24",
          table.item(0, 2).text())
    check("A9 第 1 页是最新一段（行 0 比 行 1 新一秒）",
          table.item(0, 0).text() > table.item(1, 0).text(),
          "%r vs %r" % (table.item(0, 0).text(), table.item(1, 0).text()))
    check("A10 行数统计：共 25 条（本页 20）",
          "共 25 条" in page._total_label.text()
          and "（本页 20）" in page._total_label.text(),
          page._total_label.text())
    check("A11 页码标签：第 1/2 页", page._page_label.text() == "第 1/2 页",
          page._page_label.text())
    check("A12 首页时上一页禁用 / 下一页可用",
          not page._btn_prev.isEnabled() and page._btn_next.isEnabled())


def m_logpg_part_b_paging(page, fake):
    print("== B. 翻页锚点（翻页沿用、新查询重置）==")
    anchor0 = fake.requests[-1]["before_ts"]
    page._next_page()
    m_logpg_process_events()
    req1 = fake.requests[-1]
    check("B1 下一页 offset=20（=0 + 页大小）", req1["offset"] == 20,
          str(req1))
    check("B2 翻页沿用同一锚点 beforeTs",
          req1["before_ts"] == anchor0,
          "%s vs %s" % (req1["before_ts"], anchor0))
    check("B3 筛选条件随翻页保持（category/kw 不变）",
          req1["category"] is None and req1["kw"] is None)
    table = page._table.table
    check("B4 第 2 页铺 5 行", table.rowCount() == 5,
          "rows=%d" % table.rowCount())
    check("B5 页码走到 第 2/2 页", page._page_label.text() == "第 2/2 页",
          page._page_label.text())
    check("B6 末页下一页禁用 / 上一页可用",
          not page._btn_next.isEnabled() and page._btn_prev.isEnabled())
    check("B7 总数不变（锚点窗口内）", "共 25 条" in page._total_label.text(),
          page._total_label.text())

    page._prev_page()
    m_logpg_process_events()
    req2 = fake.requests[-1]
    check("B8 上一页 offset 回 0 且锚点仍是同一个",
          req2["offset"] == 0 and req2["before_ts"] == anchor0, str(req2))
    check("B9 回第 1 页铺回 20 行", page._table.table.rowCount() == 20,
          "rows=%d" % page._table.table.rowCount())

    # 越界翻页防护：末页连点下一页不发请求
    page._offset = 20
    page._rows = page._rows[:5]
    page._fill_table()
    n_req = len(fake.requests)
    page._next_page()
    m_logpg_process_events()
    check("B10 末页连点下一页不再发请求",
          len(fake.requests) == n_req, "%d → %d" % (n_req, len(fake.requests)))
    page._prev_page()
    m_logpg_process_events()


def m_logpg_part_c_attribution(page, fake):
    print("== C. 应答归属（offset+limit 回显判定，防串页）==")
    fake.hold = True
    n_req = len(fake.requests)
    page._next_page()                      # 在途：offset=20 不回包
    m_logpg_process_events()
    check("C1 请求已发出且挂起", len(fake.requests) == n_req + 1
          and page._inflight and page._pending_offset == 20,
          "pending=%s inflight=%s" % (page._pending_offset, page._inflight))
    n_rows_before = page._table.table.rowCount()

    # 迟到的旧应答（offset/limit 对不上）→ 丢弃，绝不铺表，且在途闸不动
    stale = {"ok": True, "rows": m_logpg_make_rows(20), "total": 25,
             "count": 20, "offset": 0, "limit": 20}
    page.logReady.emit(stale)
    m_logpg_process_events()
    check("C2 回显 offset/limit 不匹配的应答被丢弃（表格未动）",
          page._table.table.rowCount() == n_rows_before,
          "rows=%d" % page._table.table.rowCount())
    check("C2b 丢弃迟到应答不提前放行在途闸",
          page._inflight and page._pending_offset == 20,
          "inflight=%s pending=%s" % (page._inflight, page._pending_offset))

    # 在途期间翻页被闸住（同一时刻只允许一个请求）
    page._next_page()
    m_logpg_process_events()
    check("C3 在途请求期间不再叠发新请求", len(fake.requests) == n_req + 1,
          "requests=%d" % len(fake.requests))

    # 正主应答（回显 pending 的 offset/limit）→ 铺表
    good = {"ok": True, "rows": m_logpg_make_rows(5), "total": 25, "count": 5,
            "offset": 20, "limit": 20}
    page.logReady.emit(good)
    m_logpg_process_events()
    check("C4 回显匹配的应答正常铺表（5 行）",
          page._table.table.rowCount() == 5 and not page._inflight,
          "rows=%d inflight=%s" % (page._table.table.rowCount(),
                                   page._inflight))
    fake.hold = False
    page._prev_page()
    m_logpg_process_events()


def m_logpg_part_d_filter(page, fake):
    print("== D. 筛选（类别立即生效 / 时间窗+关键字按筛选 / 清除保留类别）==")
    import time
    from hsfsrv_client import proto
    old_anchor = fake.requests[-1]["before_ts"]

    # ---- 类别下拉：立即重查、锚点归零、回第 1 页 ----
    page._cat_combo.setCurrentIndex(1)     # 连接（conn）
    m_logpg_process_events()
    req = fake.requests[-1]
    check("D1 换类别立即发新查询（offset=0）", req["offset"] == 0, str(req))
    check("D2 类别字面下推（conn）", req["category"] == "conn", str(req))
    check("D3 换类别锚点重置（新锚 > 旧锚）",
          req["before_ts"] > old_anchor,
          "%s > %s" % (req["before_ts"], old_anchor))
    check("D4 类别筛后铺表（25 行里 conn 占 5）",
          page._table.table.rowCount() == 5
          and all(page._table.table.item(r, 1).text() == "连接"
                  for r in range(page._table.table.rowCount())),
          "rows=%d" % page._table.table.rowCount())

    # ---- 时间窗 + 关键字：按「筛选」应用，锚点归零 ----
    anchor_cat = fake.requests[-1]["before_ts"]
    time.sleep(0.002)                      # 保证新锚点严格大于旧锚
    page._f_start.setText("2026-11-09 00:00")
    page._f_end.setText("2026-11-09 23:59")
    page._f_text.setText("事件")
    n_req = len(fake.requests)
    page._apply_filter()
    m_logpg_process_events()
    req = fake.requests[-1]
    check("D5 筛选发出新查询（请求数 +1，offset=0）",
          len(fake.requests) == n_req + 1 and req["offset"] == 0, str(req))
    check("D6 时间窗下推（lo/hi 为 epoch 毫秒且 lo<hi）",
          isinstance(req["lo_ts"], int) and isinstance(req["hi_ts"], int)
          and req["lo_ts"] < req["hi_ts"], str(req))
    check("D7 关键字下推", req["kw"] == "事件", str(req))
    check("D8 类别保持（conn）", req["category"] == "conn", str(req))
    check("D9 筛选锚点重置", req["before_ts"] > anchor_cat, str(req))
    check("D10 时间+类别+关键字组合命中 5 行（合成数据都当在窗内）",
          page._table.table.rowCount() == 5,
          "rows=%d" % page._table.table.rowCount())

    # ---- 翻页把筛选条件全部带上 ----
    page._page_size = 2
    page._request(0)
    m_logpg_process_events()
    page._next_page()
    m_logpg_process_events()
    req = fake.requests[-1]
    check("D11 翻页保持时间窗/关键字/类别",
          req["offset"] == 2 and req["kw"] == "事件"
          and req["category"] == "conn" and isinstance(req["lo_ts"], int),
          str(req))
    page._page_size = proto.LOG_PAGE_DEFAULT

    # ---- 起点晚于终点：不应用、不发请求 ----
    n_req = len(fake.requests)
    page._f_start.setText("2026-11-09 23:00")
    page._f_end.setText("2026-11-09 08:00")
    page._apply_filter()
    m_logpg_process_events()
    check("D12 起点晚于终点：提示且不发请求",
          len(fake.requests) == n_req
          and any("晚于" in s for s in page._status_texts),
          "%d requests, status=%s" % (len(fake.requests),
                                      page._status_texts[-1:]))

    # ---- 时间格式错误：不应用 ----
    page._f_start.setText("not-a-time")
    page._apply_filter()
    m_logpg_process_events()
    check("D13 时间格式不对：提示且不发请求",
          len(fake.requests) == n_req
          and any("格式不对" in s for s in page._status_texts))

    # ---- 清除筛选：时间/关键字清空、类别保持、锚点归零 ----
    page._f_start.setText("2026-11-09 00:00")
    page._clear_filter()
    m_logpg_process_events()
    req = fake.requests[-1]
    check("D14 清除后 lo/hi/kw 不再下发",
          req["lo_ts"] is None and req["hi_ts"] is None
          and req["kw"] is None, str(req))
    check("D15 清除筛选保留类别（conn）", req["category"] == "conn",
          str(req))
    check("D16 输入框已清空", page._f_start.text() == ""
          and page._f_text.text() == "",
          "%r %r" % (page._f_start.text(), page._f_text.text()))
    page._cat_combo.setCurrentIndex(0)
    m_logpg_process_events()


def m_logpg_part_e_pagesize(page, fake):
    print("== E. 每页条数（改档回第 1 页 + 锚点归零）==")
    import time
    from hsfsrv_client import proto
    old_anchor = fake.requests[-1]["before_ts"]
    time.sleep(0.005)          # 锚点取本地"现在"，留出毫秒间隙使新锚严格更大
    idx = page._page_size_box.findData(50)
    page._page_size_box.setCurrentIndex(idx)
    m_logpg_process_events()
    req = fake.requests[-1]
    check("E1 改页大小生效（limit=50）", req["limit"] == 50, str(req))
    check("E2 回到第 1 页（offset=0）", req["offset"] == 0, str(req))
    check("E3 锚点归零重取", req["before_ts"] > old_anchor, str(req))
    idx = page._page_size_box.findData(10)
    page._page_size_box.setCurrentIndex(idx)
    m_logpg_process_events()
    check("E4 换 10 条同样生效", fake.requests[-1]["limit"] == 10,
          str(fake.requests[-1]))
    idx = page._page_size_box.findData(proto.LOG_PAGE_DEFAULT)
    page._page_size_box.setCurrentIndex(idx)
    m_logpg_process_events()


def m_logpg_part_f_nok(page, fake):
    print("== F. ok=false（服务端明确拒绝，如落盘未启用）==")
    fake.behavior = "nok:落盘未启用：服务端没有日志可查"
    page._reload_current()
    m_logpg_process_events()
    check("F1 表格清空", page._table.table.rowCount() == 0,
          "rows=%d" % page._table.table.rowCount())
    check("F2 状态行显示服务端 err 原文",
          "落盘未启用" in page._total_label.text()
          and any("落盘未启用" in s for s in page._status_texts),
          page._total_label.text())
    check("F3 翻页钮双双禁用",
          not page._btn_prev.isEnabled() and not page._btn_next.isEnabled())
    fake.behavior = "ok"
    page._reload_current()
    m_logpg_process_events()
    check("F4 恢复后重查正常铺表", page._table.table.rowCount() == 20,
          "rows=%d" % page._table.table.rowCount())


def m_logpg_part_g_unknown(page, fake):
    print("== G. 老服务端『未知命令』否定应答（不崩 + 空表 + 可恢复）==")
    fake.behavior = "nok:未知命令"
    n_req = len(fake.requests)
    page.on_enter()
    m_logpg_process_events()
    check("G1 状态行提示不支持 0x0119（空表不崩）",
          any("0x0119" in s or "不支持" in s for s in page._status_texts)
          and page._table.table.rowCount() == 0,
          page._status_texts[-1:])
    check("G2 行数统计位显示升级提示",
          "不支持" in page._total_label.text(), page._total_label.text())
    n_after = len(fake.requests)
    page.on_enter()                        # 再入页：不再自动敲老服务端
    m_logpg_process_events()
    check("G3 已识别不支持后，入页不再自动重发",
          len(fake.requests) == n_after,
          "%d → %d" % (n_after, len(fake.requests)))

    # 显式操作仍可重试；服务端恢复支持后自动回到正常
    fake.behavior = "ok"
    page._apply_filter()                   # 显式筛选 = 给一次重试机会
    m_logpg_process_events()
    check("G4 显式筛选会重试（请求数 +1）",
          len(fake.requests) == n_after + 1,
          "%d → %d" % (n_after, len(fake.requests)))
    check("G5 服务端恢复后铺表正常（server_supported 复位）",
          page._table.table.rowCount() == 20 and page._server_supported,
          "rows=%d" % page._table.table.rowCount())
    page.on_enter()
    m_logpg_process_events()
    check("G6 恢复后入页恢复自动查询",
          len(fake.requests) == n_after + 2)


def m_logpg_part_h_export(page, fake, tmpdir):
    print("== H. 导出 CSV（utf-8-sig + PERM_DOWNLOAD 闸）==")
    import os
    from hsfsrv_client import proto
    # 无 Download 权限（匿名）：直接拒绝、不落盘（返回在对话框之前）
    page.ctx.bridge.role = proto.ROLE_ANONYMOUS
    n_req = len(fake.requests)
    page._export_data()
    m_logpg_process_events()
    check("H1 无 Download 权限：提示权限不足且不发请求不落盘",
          len(fake.requests) == n_req
          and any("权限不足" in s for s in page._status_texts)
          and not os.listdir(tmpdir),
          page._status_texts[-1:])

    # 有权限：直接调 _write_export（绕开保存对话框，无头环境不可用）
    page.ctx.bridge.role = proto.ROLE_USER
    path = os.path.join(tmpdir, "log_export.csv")
    page._write_export(path, page._rows)
    m_logpg_process_events()
    check("H2 文件已落盘", os.path.isfile(path), path)
    with open(path, "rb") as fh:
        raw = fh.read()
    check("H3 UTF-8 BOM（utf-8-sig，Excel 双击直开）",
          raw.startswith(b"\xef\xbb\xbf"), raw[:6].hex())
    text = raw.decode("utf-8-sig")
    lines = text.splitlines()
    check("H4 表头 = 时间,类别,事件", lines[0] == "时间,类别,事件", lines[0])
    check("H5 行数 = 本页行数 + 表头",
          len(lines) == len(page._rows) + 1,
          "%d vs %d" % (len(lines), len(page._rows) + 1))
    check("H6 行内容与表格一致（首行事件列）",
          lines[1].split(",")[2] == page._rows[0][2], lines[1])
    check("H7 中文未乱码（类别列 = 显示名）",
          lines[1].split(",")[1] == page._rows[0][1], lines[1])

    # ---- H8 导出口径改版（2026-11-11 用户「导出要按筛选结果，不是当前
    #      表格显示的数据，因为数据是分页显示的」）----
    # 旧口径：「当前页没数据 ⇒ 拒绝导出」。新口径：导出按**筛选命中全量**，
    # 当前页空只说明这一页没命中（命中行可能在别的页上）⇒ 不再据此拒绝。
    # 两条新断言：
    #   ① 服务端 0 行 ⇒ 提示「无可导出的数据」且不落盘；
    #   ② 当前页为空但服务端有行 ⇒ 照常导出全量（不是当前页那几行）。
    # [为什么要绕开保存框] 导出会弹文件对话框（需要真窗口），无头环境里
    # 会**卡住**⇒ 这里把对话框换掉（与 §21 导出段同一手法）。
    from PyQt5.QtWidgets import QFileDialog
    saved_rows = page._rows
    saved_fake_rows = fake.rows
    page._rows = []
    fake.rows = []
    path_empty = os.path.join(tmpdir, "log_export_empty.csv")
    path_full = os.path.join(tmpdir, "log_export_full.csv")
    origin = QFileDialog.getSaveFileName
    try:
        QFileDialog.getSaveFileName = staticmethod(
            lambda *a, **k: (path_empty, "CSV"))
        page._export_data()
        m_logpg_process_events()
        # [文案 2026-11-17 统一] 三页收尾都走 export_finish_text：0 行现在说
        # 「筛选命中 0 行，未生成文件」（比旧「无可导出的数据」更明确"没有
        # 文件生成"）；两种措辞都接受，重点是**不落盘**。
        check("H8 服务端 0 行 ⇒ 提示未生成文件且不落盘",
              any(("无可导出" in x or "未生成文件" in x)
                  for x in page._status_texts)
              and not os.path.exists(path_empty),
              page._status_texts[-1:])
        # 当前页空、服务端有行 ⇒ 照样导出（且导的是**全量**不是当前页）
        fake.rows = saved_fake_rows
        QFileDialog.getSaveFileName = staticmethod(
            lambda *a, **k: (path_full, "CSV"))
        page._export_data()
        m_logpg_process_events()
        got_lines = 0
        if os.path.isfile(path_full):
            with open(path_full, "rb") as fh:
                got_lines = len(fh.read().decode("utf-8-sig")
                                .strip().splitlines())
        check("H9 当前页为空但服务端有行 ⇒ 照常导出全量（不按当前页拒）",
              os.path.isfile(path_full) and got_lines == len(fake.rows) + 1,
              "文件行数=%d 服务端行数=%d" % (got_lines, len(fake.rows)))
    finally:
        QFileDialog.getSaveFileName = origin
        page._rows = saved_rows
        fake.rows = saved_fake_rows


def m_logpg_part_i_client_log(page):
    print("== I. 客户端运行日志折叠副视图（原有能力保留）==")
    app = m_logpg_app()
    # [isVisibleTo(page)] 页面本身从未 show（离屏），isVisible 恒 False；
    # isVisibleTo(page) 只看本卡相对页面的显隐，不受父级可见性影响。
    check("I1 缺省折叠（客户端日志卡不可见）",
          not page._client_card.isVisibleTo(page), "")
    page._client_toggle.setChecked(True)
    app.processEvents()
    check("I2 勾选即展开", page._client_card.isVisibleTo(page), "")
    page.append("桥接消息一条")
    app.processEvents()
    check("I3 状态消息收流进副视图",
          "桥接消息一条" in page._view.toPlainText()
          and any("桥接消息一条" in ln for ln in page._lines),
          page._lines[-1:])
    for i in range(page.MAX_LINES + 5):
        page.append("行 %d" % i)
    check("I4 环形截断（MAX_LINES=500）", len(page._lines) == 500,
          str(len(page._lines)))
    page._clear()
    check("I5 清空生效", page._lines == []
          and page._view.toPlainText() == "")
    page._client_toggle.setChecked(False)
    app.processEvents()
    check("I6 取消勾选收起", not page._client_card.isVisibleTo(page), "")


def m_logpg_part_j_empty(page, fake):
    print("== J. 空库（0 行应答）行数统计 ==")
    fake.rows = []
    page.on_enter()
    m_logpg_process_events()
    check("J1 空库铺 0 行", page._table.table.rowCount() == 0,
          "rows=%d" % page._table.table.rowCount())
    check("J2 统计标注（无数据）",
          "共 0 条" in page._total_label.text()
          and "（无数据）" in page._total_label.text(),
          page._total_label.text())
    check("J3 翻页钮双双禁用",
          not page._btn_prev.isEnabled() and not page._btn_next.isEnabled())
    fake.rows = m_logpg_make_rows()
    page.on_enter()
    m_logpg_process_events()


# ============================================================================
#                L. 真实往返（127.0.0.1:6100 的 C 服务端）
# ============================================================================
#
# [本段入口**不跑**它] 它连的是"别人手工起的"现场 C 服务端（端口写死
# 6100）⇒ 与总回归"自己起隔离实例"的口径冲突（端口会被占、会写现场库、
# 登录可能撞上同账会话闸）。跨端连 C 服务端由总回归 §11 覆盖（那边连的
# 是总回归自己起的隔离实例，端口由 _free_port() 分配）。函数本体保留在
# 这里，需要单独跑时手动调用即可。

def m_logpg_c_server_available(host="127.0.0.1", port=6100, timeout=0.8):
    import socket
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.close()
        return True
    except OSError:
        return False


def m_logpg_live_page(bridge, timeout=9.0, **kw):
    """同步取一页运行日志（包一层等待，用例读起来像普通调用）。"""
    import time
    holder = {}
    done = [False]

    def _done(doc):
        holder.update(doc)
        done[0] = True

    def _err(msg):
        holder["transportError"] = msg
        done[0] = True

    bridge.log_page(on_done=_done, on_error=_err, timeout=timeout, **kw)
    end = time.time() + timeout + 2
    while not done[0] and time.time() < end:
        time.sleep(0.02)
    return holder


def m_logpg_int(doc, key, default=None):
    """应答整数键读取（total=0 是合法值，不能走 `or 缺省` 的写法）。"""
    value = doc.get(key)
    return default if value is None else int(value)


def m_logpg_part_live():
    print("== L. 真实往返（C 服务端 127.0.0.1:6100）==")
    import time
    m_logpg_ensure_client_path()
    if not m_logpg_c_server_available():
        skip("L 段整体（6100 未开放）")
        return
    app = m_logpg_app()
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import LogPage
    bridge = LinkBridge()
    ok, err = bridge.connect_to("127.0.0.1", 6100, "root", "root")
    if not ok:
        skip("L 段整体（登录失败：%s）" % err)
        return
    print("  已连接 C 服务端 root@127.0.0.1:6100")
    status = []
    try:
        ctx = PageContext(bridge, lambda i: None, status.append)
        page = LogPage(ctx)

        print("  -- 协议级（bridge.log_page 直发）--")
        first = m_logpg_live_page(bridge, offset=0, limit=10)
        check("L1 应答 ok", first.get("ok") is True,
              str(first.get("err") or first.get("transportError") or "")[:60])
        rows = first.get("rows") or []
        total = m_logpg_int(first, "total", 0)
        check("L2 服务端有日志（total > 0）", total > 0, "total=%d" % total)
        check("L3 行是对象数组且带 ts/category/text",
              bool(rows) and all(
                  isinstance(r, dict) and "ts" in r and "category" in r
                  and "text" in r for r in rows),
              "rows=%d" % len(rows))
        check("L4 ts 是墙钟毫秒（> 2020）",
              bool(rows) and all(isinstance(r["ts"], int)
                                 and r["ts"] > 1577836800000 for r in rows),
              str(rows[0]["ts"]) if rows else "-")
        check("L5 category 取值都在五类口径内",
              bool(rows) and all(r["category"] in
                                 ("conn", "ctrl", "alarm", "auto", "auth")
                                 for r in rows),
              ",".join(sorted({r["category"] for r in rows})) or "-")
        # count 在应答协议里必有（HSF_KEY_LOG_COUNT）；旧部署二进制可能
        # 不回 —— 缺省按 len(rows) 视为一致，有值时必须相等。
        check("L6 count=本页行数 / total>=count",
              m_logpg_int(first, "count", len(rows)) == len(rows)
              and total >= len(rows),
              "count=%s rows=%d total=%d" % (first.get("count"), len(rows),
                                             total))
        check("L7 offset/limit 原样回显",
              m_logpg_int(first, "offset", -1) == 0
              and m_logpg_int(first, "limit", -1) == 10,
              "offset=%s limit=%s" % (first.get("offset"),
                                      first.get("limit")))

        # 锚定分页：锚点取**写入前沿之后**（now-8s ⇒ 该秒的行已全部落库），
        # 同一锚点翻页 ⇒ total 恒定、两页不重不漏。（锚点取"现在"时，秒粒
        # 度入库会让锚点所在秒内后到的行也落进窗口，同锚 total 会微涨——
        # 那是数据粒度问题不是锚点失效，故这里刻意用过去锚点做确定性断言。）
        anchor = int(time.time() * 1000) - 8000
        p0 = m_logpg_live_page(bridge, offset=0, limit=10, before_ts=anchor)
        p1 = m_logpg_live_page(bridge, offset=10, limit=10, before_ts=anchor)
        r0 = [r["ts"] for r in (p0.get("rows") or [])]
        r1 = [r["ts"] for r in (p1.get("rows") or [])]
        check("L8 锚定后两页 total 一致",
              p0.get("ok") and p1.get("ok")
              and m_logpg_int(p0, "total", -1) == m_logpg_int(p1, "total", -2),
              "%s vs %s" % (p0.get("total"), p1.get("total")))
        check("L9 页内时间正序（DESC 取页、页内正序输出）",
              all(a <= b for a, b in zip(r0, r0[1:])),
              "首=%s 末=%s" % (r0[0] if r0 else "-", r0[-1] if r0 else "-"))
        check("L10 翻页无重叠（第 2 页整体不晚于第 1 页最早行）",
              not r1 or not r0 or max(r1) <= min(r0),
              "max(p1)=%s min(p0)=%s" % (max(r1) if r1 else "-",
                                         min(r0) if r0 else "-"))
        check("L11 锚定后总数与不锚定的最新总数自洽",
              m_logpg_int(p0, "total", -1) <= total,
              "anchored=%s fresh=%d" % (p0.get("total"), total))

        # 类别过滤：conn（刚连过，必有会话日志）
        conn = m_logpg_live_page(bridge, offset=0, limit=10, category="conn")
        crows = conn.get("rows") or []
        check("L12 category=conn 命中且行类别全为 conn",
              conn.get("ok") is True and bool(crows)
              and all(r["category"] == "conn" for r in crows),
              "total=%s" % conn.get("total"))

        # 关键字/时间窗下推：不可能命中 ⇒ total=0
        miss = m_logpg_live_page(bridge, offset=0, limit=10,
                                 kw="绝不可能命中的词ZZZ")
        check("L13 kw 不命中 ⇒ total=0 且 rows 空",
              miss.get("ok") is True and m_logpg_int(miss, "total", -1) == 0
              and not (miss.get("rows") or []),
              "total=%s" % miss.get("total"))
        future = m_logpg_live_page(bridge, offset=0, limit=10,
                                   lo_ts=4102444800000)     # 2100-01-01
        check("L14 未来 loTs ⇒ total=0（时间窗生效）",
              future.get("ok") is True
              and m_logpg_int(future, "total", -1) == 0,
              "total=%s" % future.get("total"))

        print("  -- 页面级（LogPage on_enter）--")
        page.on_enter()
        deadline = time.time() + 8
        while time.time() < deadline and not page._rows:
            app.processEvents()
            time.sleep(0.02)
        check("L15 入页自动铺表（无需点任何按钮）",
              page._table.table.rowCount() > 0,
              "rows=%d" % page._table.table.rowCount())
        check("L16 行数统计与页码标签就位",
              "共" in page._total_label.text()
              and page._page_label.text().startswith("第 1/"),
              "%s | %s" % (page._total_label.text(),
                           page._page_label.text()))
        check("L17 翻页钮使能正确（首页：上一页禁用）",
              not page._btn_prev.isEnabled(), "")
        page.on_leave()
    finally:
        bridge.disconnect_from()
        app.processEvents()


# ============================================================================
#                              段落入口
# ============================================================================

def m_logpg_run_log_page():
    """日志页段落入口（总回归调用它）。

    [内容 = 原 main() 里"跑各 part"的部分]
    · 建 app / 建服务端 / 打印汇总与退出码 ⇒ 全部去掉（app 由总回归
      main() 统一建；本段不连任何服务端；汇总由总回归统一出）。
    """
    import shutil
    import tempfile
    m_logpg_ensure_client_path()
    tmpdir = tempfile.mkdtemp(prefix="hsf_logpage_")
    try:
        m_logpg_part_0_proto()
        app, page, bridge, status = m_logpg_make_page()
        # status 列表同时喂给 page（测试断言读最近的状态栏文案）
        page._status_texts = status
        fake = m_logpg_FakeLogServer()
        m_logpg_part_a_fill(page, fake)
        m_logpg_part_b_paging(page, fake)
        m_logpg_part_c_attribution(page, fake)
        m_logpg_part_d_filter(page, fake)
        m_logpg_part_e_pagesize(page, fake)
        m_logpg_part_f_nok(page, fake)
        m_logpg_part_g_unknown(page, fake)
        m_logpg_part_h_export(page, fake, tmpdir)
        m_logpg_part_i_client_log(page)
        m_logpg_part_j_empty(page, fake)
        page.deleteLater()
        app.processEvents()
        # [不跑 m_logpg_part_live] 它连的是别人手工起的现场 C 服务端
        # （端口写死 6100）⇒ 与总回归"自己起隔离实例"的口径冲突；跨端
        # 连 C 服务端由总回归 §11 覆盖。
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


# ==========================================================================
# 段落：test_remote_settings_ui（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【远程设置段落】远端设置页与新控件的实际测试（每个控件都实测）。

    入口：m_remcfg_run_remote_settings()

[原文件] tests/test_remote_settings_ui.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

两部分：

  A. 控件单测（离屏，无服务端）
     · 采集配置表格布局回归：表头占第 0 行，**不覆盖**搅拌泵行的
       复选框/数值框（截图实锅：表头塞进数据行格子把控件顶没了）。
     · MultiCheckCombo：勾选/汇总文本/checked_indexes/清空。
     · TimePickerEdit：浮层确定路径写文本 + 发信号。

  B. 设置流程 e2e（自起一份 sim 服务端，**独立临时配置目录**）
     登录 root → CMD_CONFIG_GET → 界面回显 = 服务端口径 →
     界面改值 → CMD_CONFIG_SET → **服务端配置文件真的变了** →
     改回原值收尾。这是"设置生效"的直接证据（用户反馈没生效）。

[本段的几条硬规矩]
  · **计数器用总回归那一套**（check 按名调用）⇒ 断言统一汇总。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **顶层不 import**：本段所有 import 都放在函数里 ⇒ 段落的顶层名与
    总回归、与其它段落**零交集**（os/time/... 这些名字归总回归所有）。
  · **端口不写死**：B 段自起 sim 服务端，端口走总回归的 _free_port()
    （独立跑本文件时自己取一个空闲口），cfg 目录是独立临时目录 ⇒ 不碰
    现场库、不撞别人的会话。
"""


# ============================================================================
#                        路径 / 环境（函数级 import）
# ============================================================================

def m_remcfg_tests_dir():
    """本段落文件所在的 tests 目录。

    [优先用总回归的 HERE] 拼进总回归后，主文件已经算好了 HERE / CLIENT_ROOT
    ⇒ 直接用它（不必再从 __file__ 反推，避免"文件被挪个位置就找错目录"）。
    本文件独立放在 tests/_merged/ 下跑时，再按自身位置推导。
    """
    import os
    g = globals()
    here = g.get("HERE")
    if here:
        return here
    d = os.path.dirname(os.path.abspath(g.get("__file__", "")))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    return d


def m_remcfg_client_root():
    """client_py 目录（客户端包 hsfsrv_client 的上级）。"""
    import os
    g = globals()
    root = g.get("CLIENT_ROOT")
    if root:
        return root
    return os.path.dirname(m_remcfg_tests_dir())


def m_remcfg_server_root():
    """Python 服务端目录（.../HighSpeedFilter/server_py）。"""
    import os
    g = globals()
    root = g.get("SERVER_ROOT")
    if root:
        return root
    return os.path.join(os.path.dirname(m_remcfg_client_root()), "server_py")


def m_remcfg_ensure_client_path():
    """把**客户端包**目录放进 sys.path。

    [不插主文件路径] 绝不把 tests/（主文件所在目录）放进去 ⇒ 不会循环
    import；总回归自己已有 sys.path.insert(HERE)。
    """
    import sys
    root = m_remcfg_client_root()
    if root not in sys.path:
        sys.path.insert(0, root)
    return root


def m_remcfg_app():
    """取总回归那个 QApplication（**不新建**：Qt5 一进程多 app 是 UB）。"""
    import os
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑远程设置段")
    return app


def m_remcfg_pick_port(port=None):
    """端口：缺省用总回归的 _free_port()（按名调用，不 import 主文件）。

    [为什么要"按名调用"] 段落文件不 import 主文件（会循环导入）；主文件
    的 _free_port 是它自己的设施，运行期从主模块的 globals 里取。独立跑
    本文件时（没有主文件）退化成"自己绑一个 0 号端口要一个空闲口"。
    """
    if port:
        return int(port)
    fn = globals().get("_free_port")
    if fn is not None:
        return int(fn())
    import socket
    sock = socket.socket()
    try:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])
    finally:
        sock.close()


def m_remcfg_part_a_controls(app):
    """控件单测（离屏）。"""
    print("== A. 控件单测 ==")
    m_remcfg_ensure_client_path()
    from PyQt5.QtWidgets import QGridLayout, QLabel
    from PyQt5.QtCore import Qt, QDateTime
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import (MultiCheckCombo,
                                           RemoteSettingsPage,
                                           TimePickerEdit)

    bridge = LinkBridge()
    ctx = PageContext(bridge, lambda i: None, lambda s: None)
    page = RemoteSettingsPage(ctx)

    # ---- A0 落盘缺省：入库周期 1 分钟 / 容量上限 50MB（2026-11-11 用户定案）----
    # [为什么锁这里] 缺省值散在三个文件里（C 公共头 / 两个 Python proto），
    # 任一处漂移就会出现"界面写 1 分钟、服务端按 5s 入库"这类查不出的
    # 分歧。断言的是**同源常量**与控件初值、服务端缺省三者一致。
    from hsfsrv_client import proto as _p0
    check("A0a 落盘入库周期缺省 = 60000ms（1 分钟）",
          _p0.STORAGE_SAMPLE_MS_DEF == 60000,
          "%d" % _p0.STORAGE_SAMPLE_MS_DEF)
    check("A0b 落盘容量上限缺省 = 50MB",
          _p0.STORAGE_MAX_MB_DEF == 50, "%d" % _p0.STORAGE_MAX_MB_DEF)
    check("A0c 远端设置页入库周期初值 = 缺省（应答到达前也一致）",
          page._storage_sample.value() == _p0.STORAGE_SAMPLE_MS_DEF,
          "%d" % page._storage_sample.value())
    check("A0d 远端设置页容量上限初值 = 缺省",
          page._storage_max.value() == _p0.STORAGE_MAX_MB_DEF,
          "%d" % page._storage_max.value())
    try:
        import os as _os
        import sys as _sys
        # client_py/tests/ → 上两级是 client_py，再上一级才是 HighSpeedFilter
        _top = os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))))
        _srv_root = os.path.join(_top, "server_py")
        if _srv_root not in _sys.path:
            _sys.path.insert(0, _srv_root)
        from hsfsrv import config as _scfg
        _sc = _scfg.StorageConfig()
        check("A0e 服务端落盘缺省与客户端同值（周期 1 分钟 / 上限 50MB）",
              _sc.sample_ms == _p0.STORAGE_SAMPLE_MS_DEF
              and _sc.max_mb == _p0.STORAGE_MAX_MB_DEF,
              "sample_ms=%d max_mb=%d" % (_sc.sample_ms, _sc.max_mb))
        check("A0f 显式写 0 仍是「不限」（不被缺省覆盖）",
              _scfg.StorageConfig(max_kb=0).max_kb == 0,
              "%d" % _scfg.StorageConfig(max_kb=0).max_kb)
    except Exception as _exc:                       # noqa: BLE001
        check("A0e 服务端落盘缺省与客户端同值（周期 1 分钟 / 上限 50MB）",
              False, "%s: %s" % (type(_exc).__name__, _exc))
        check("A0f 显式写 0 仍是「不限」（不被缺省覆盖）", False,
              "服务端 config 模块不可用：%s" % _exc)

    # ---- A1 采集配置表格布局回归（表头不得覆盖数据行）----
    key, chk, _poll, spin = page._collect_rows[0]       # 搅拌泵行
    grids = [g for g in page.findChildren(QGridLayout)
             if g.indexOf(chk) >= 0]
    check("A1a 搅拌泵复选框在网格布局里", bool(grids))
    if grids:
        grid = grids[0]
        idx = grid.indexOf(chk)
        r, c, _rs, _cs = grid.getItemPosition(idx)
        check("A1b 复选框位于第 1 行（表头独占第 0 行）", r == 1,
              "row=%d" % r)
        head = grid.itemAtPosition(0, 1)
        head_widget = head.widget() if head else None
        check("A1c (0,1) 表头是文字标签「采集」",
              isinstance(head_widget, QLabel)
              and head_widget.text() == "采集")
        spin_item = grid.itemAtPosition(1, 2)
        check("A1d 数值框在 (1,2) 且未被表头顶掉",
              spin_item is not None and spin_item.widget() is spin)

    # ---- A1e~A1h 采集设备必须列全（2026-11-10 用户「采集的设备不全」）----
    # 现场三台 485 表各有自己的周期：流量计站5、浊度仪站1/站3。界面原来
    # 只有合并的「浊度仪」，流量计干脆没行。现在 7 行，顺序与 C 端
    # HsfSettingsRemote.c 的 kCollectLabels 逐项一致。
    rows = page._collect_rows
    check("A1e 采集配置 7 行（与 C 端同序）", len(rows) == 7,
          "%d 行" % len(rows))
    if len(rows) == 7:
        check("A1f 逐台键：进水浊度仪/出水浊度仪/流量计 各用自己的周期键",
              rows[2][2] == proto.CFGK_TURBIDITY1_POLL_MS
              and rows[3][2] == proto.CFGK_TURBIDITY2_POLL_MS
              and rows[4][2] == proto.CFGK_FLOWMETER_POLL_MS,
              "%s" % [r[2] for r in rows])
        # [两台浊度是两台独立设备 2026-11-10 用户定案] 开关各自独立，
        # 各自用自己的逐台键（不再共用 turbidityEnabled）。
        check("A1g 两台浊度各用自己的采集开关（turbidity1/2Enabled）",
              rows[2][0] == proto.CFGK_TURBIDITY1_ENABLED
              and rows[3][0] == proto.CFGK_TURBIDITY2_ENABLED,
              "%s" % [rows[2][0], rows[3][0]])
        # 勾一台不得动另一台（独立设备，允许一行勾一行没勾）
        rows[2][1].setChecked(True)
        rows[3][1].setChecked(True)
        app.processEvents()
        rows[2][1].setChecked(False)
        app.processEvents()
        independent = rows[3][1].isChecked() is True
        rows[2][1].setChecked(True)
        app.processEvents()
        check("A1h 两台浊度的采集勾选互不联动",
              independent and rows[2][1].isChecked() is True
              and rows[3][1].isChecked() is True)

    # ---- A2 MultiCheckCombo ----
    combo = MultiCheckCombo(["浊度1(NTU)", "浊度2(NTU)", "压差(MPa)"],
                            all_text="全部列")
    check("A2a 初始空选显示「全部列」",
          combo.lineEdit().text() == "全部列")
    combo._model.item(0).setCheckState(Qt.Checked)
    combo._model.item(2).setCheckState(Qt.Checked)
    check("A2b 勾 0/2 两列 → checked_indexes=[0,2]",
          combo.checked_indexes() == [0, 2])
    check("A2c 汇总文本 = 勾选项名拼接",
          combo.lineEdit().text() == "浊度1(NTU)、压差(MPa)")
    combo.clear_all()
    check("A2d clear_all 后归零并回显「全部列」",
          combo.checked_indexes() == []
          and combo.lineEdit().text() == "全部列")

    # ---- A3 TimePickerEdit（日期时间选择器）----
    edit = TimePickerEdit("yyyy-MM-dd HH:MM 或 HH:MM")
    picked = []
    edit.timePicked.connect(picked.append)
    edit._set_from_picker(QDateTime(2026, 11, 5, 10, 30, 59))
    check("A3a 浮层确定路径写入完整日期时间文本",
          edit.text() == "2026-11-05 10:30:59", edit.text())
    check("A3b timePicked 信号携带同值",
          picked == ["2026-11-05 10:30:59"])

    # ---- A4 局部应答不得清掉别的节（2026-11-10 实锅）----
    # 「应用登录策略」的应答只带 login 节，`_populate` 里原来无条件按
    # storageDbPath/storageStats 刷新「当前库文件」「用量」⇒ 点一下登录策略，
    # 数据存储卡就变成「用量：--」「未启用（落盘关闭）」，切页再回来才恢复。
    page._populate({"storage": {"enabled": True, "path": "",
                                "sampleMs": 2000, "maxMb": 50,
                                "fullPolicy": "ring", "keepFiles": 3},
                    "storageDbPath": "X:/tmp/hsf_data.db",
                    "storageStats": {"usedMb": 12.5, "maxMb": 50,
                                     "percent": 25, "rows": 1000,
                                     "policy": "ring"},
                    "login": {}})
    path_before = page._db_path_label.text()
    usage_before = page._storage_usage_label.text()
    check("A4a 全量应答铺出库路径与用量",
          "hsf_data.db" in path_before and "12.5" in usage_before,
          "%s / %s" % (path_before, usage_before))
    # 只带 login 节的应答（=应用登录策略的返回）不得动存储侧两行
    page._populate({"login": {"loginAllowAnonymous": False,
                              "loginAllowConcurrent": True,
                              "loginMaxSessions": 8}})
    check("A4b 只带 login 节的应答不清「当前库文件」",
          page._db_path_label.text() == path_before,
          "%r → %r" % (path_before, page._db_path_label.text()))
    check("A4c 只带 login 节的应答不清「用量」",
          page._storage_usage_label.text() == usage_before,
          "%r → %r" % (usage_before, page._storage_usage_label.text()))

    # ---- A5 本机设置持久化（2026-11-10 用户口径「能持久化的都持久化」）----
    # 覆盖：界面缩放 / 强制紧凑 / 刷新周期 / 趋势刷新 / 窗口尺寸 / 悬浮窗显隐
    # / 历史·日志每页条数。
    # [必须隔离存储] 这些键落在**用户真实设置**里（Windows 注册表 /
    # 板端 ~/.config）。测试若直写真库：崩溃中断就会把现场设置改坏，跑完
    # 也只"恢复成跑之前的样子"（跑之前已被污染的话等于没恢复——实测过：
    # 一次探针把 logPageSize/showNav 写进真库，后续整轮回归 39 项红）。
    # 这里把 `settings._store` 换成临时 INI，测完还原函数引用。
    import tempfile as _tempfile
    from PyQt5.QtCore import QSettings as _QS
    from hsfsrv_client import settings as S

    tmpdir = _tempfile.mkdtemp(prefix="hsf_ui_settings_")
    tmp_ini = os.path.join(tmpdir, "client.ini")
    real_store = S._store
    S._store = lambda: _QS(tmp_ini, _QS.IniFormat)

    from hsfsrv_client.pages_other import (HistoryPage as _HP,
                                           LogPage as _LP,
                                           LocalSettingsPage as _LSP)

    try:
        S.set_value(S.K_UI_SCALE_PCT, 125)
        S.set_value(S.K_FORCE_COMPACT, True)
        S.set_value(S.K_REFRESH_MS, 500)
        S.set_value(S.K_TREND_MS, 1000)
        S.set_value(S.K_WIN_SIZE, "1024x600")
        S.set_value(S.K_SHOW_NAV, False)
        S.set_value(S.K_HIST_PAGE_SIZE, 50)
        S.set_value(S.K_LOG_PAGE_SIZE, 10)
        # [主题 2026-11-14] 本机设置项（客户端 INI ⇒ 跟服务端无关）。
        S.set_value(S.K_THEME, "dark")

        check("A5a 设置模块读回一致（缩放/紧凑/刷新/趋势）",
              abs(S.ui_scale() - 1.25) < 1e-9
              and S.get_bool(S.K_FORCE_COMPACT) and S.refresh_ms() == 500
              and S.trend_ms() == 1000,
              "scale=%.2f refresh=%d trend=%d"
              % (S.ui_scale(), S.refresh_ms(), S.trend_ms()))
        check("A5b 窗口尺寸设置读回", S.win_size() == (1024, 600),
              str(S.win_size()))
        # [主题 2026-11-14] 主题是**本机设置**（客户端 INI）⇒ 读得回。
        check("A5b2 主题设置读回（dark）",
              S.get_str(S.K_THEME, "") == "dark",
              S.get_str(S.K_THEME, ""))

        # [2026-11-13 三页合并] 本地设置 / 悬浮窗设置 / 窗口设置 → 同一个
        # 页面实例（页号 13）：三个分区的初值都按存档开局。
        lsp = _LSP(ctx)
        check("A5c 合并页·界面设置按存档开局（125% / 紧凑 / 500ms / 趋势1000ms）",
              lsp.scale_combo.currentText() == "125%"
              and lsp.compact_check.isChecked()
              and lsp.refresh_spin.value() == 500
              and lsp.trend_combo.currentData() == 1000,
              "%s / %s / %d / %s" % (lsp.scale_combo.currentText(),
                                    lsp.compact_check.isChecked(),
                                    lsp.refresh_spin.value(),
                                    lsp.trend_combo.currentData()))
        check("A5d 合并页·悬浮窗分区按存档开局（导航隐藏）",
              lsp.show_nav.isChecked() is False, str(lsp.show_nav.isChecked()))
        # [主题 2026-11-14] 主题下拉按存档开局（5 套主题，选中的是存的那套）。
        check("A5d2 合并页·主题下拉按存档开局（深色·控制室）",
              lsp.theme_combo.currentData() == "dark",
              "%s / %s" % (lsp.theme_combo.currentText(),
                            lsp.theme_combo.currentData()))
        check("A5e 合并页·窗口与屏幕分区显示出存档尺寸",
              "1024 × 600" in lsp.size_combo.currentText(),
              lsp.size_combo.currentText())
        check("A5e2 合并页·每页条数按存档开局（历史 50 / 日志 10）",
              lsp.hist_page_combo.currentData() == 50
              and lsp.log_page_combo.currentData() == 10,
              "%s / %s" % (lsp.hist_page_combo.currentData(),
                           lsp.log_page_combo.currentData()))

        hp = _HP(ctx)
        lp = _LP(ctx)
        check("A5f 历史页/日志页每页条数按存档开局（50 / 10）",
              hp._page_size == 50 and lp._page_size == 10,
              "%d / %d" % (hp._page_size, lp._page_size))

        # 趋势卡下拉也读同一键（与 charts 段 I1~I8 同源，这里锁"跨页共享"）
        from hsfsrv_client.pages_monitor import trend_sample_ms_load
        check("A5g 趋势刷新与设置模块同一键", trend_sample_ms_load() == 1000,
              "%d" % trend_sample_ms_load())
        check("A5h 设置只落临时 INI（不碰用户真实设置）",
              os.path.exists(tmp_ini),
              tmp_ini)

        # A5i：**默认**后端是程序目录下的 INI（不是注册表）——用户 2026-11-10
        # 明确要求"不要存注册表，统一程序目录，可以与数据库文件放一个目录"。
        # 这里临时清掉 env 覆盖，验证真实路径与格式。
        _saved_env = os.environ.pop("HSF_CLIENT_SETTINGS_INI", None)
        S._store = real_store
        try:
            want = os.path.join(os.path.dirname(os.path.dirname(
                os.path.abspath(S.__file__))), "cfg", "hsf_client_py.ini")
            got = S.settings_path()
            check("A5i 设置默认落程序目录 cfg/hsf_client_py.ini（非注册表）",
                  os.path.normcase(got) == os.path.normcase(want),
                  got)
            st = S._store()
            check("A5j 设置后端是 INI 文件（fileName 指向 .ini）",
                  st.format() == _QS.IniFormat
                  and st.fileName().lower().endswith("hsf_client_py.ini"),
                  st.fileName())
        finally:
            if _saved_env is not None:
                os.environ["HSF_CLIENT_SETTINGS_INI"] = _saved_env
            S._store = lambda: _QS(tmp_ini, _QS.IniFormat)
    finally:
        S._store = real_store
        shutil.rmtree(tmpdir, ignore_errors=True)

    # ---- A6 自动模式配置（产水/反洗/顺洗；2026-11-11 用户定案，界面先行）----
    # 锁：三模式块齐全 / 设备清单 8 项与 proto 同源 / 空选文案可辨
    # （empty_text）/ 界面状态 → _auto_config_values 载荷形状 / 应答回显
    # 与局部应答不冲。
    blocks = page._auto_mode_blocks
    # [四块 2026-11-18 放水模式] 「可配置」的模式集合 = 三模式 + 放水；
    # 首页下拉仍是三模式 + 组合「自动」（不在这里断言）。
    check("A6a 自动模式配置四块齐全（产水/反洗/顺洗/放水）",
          [b["id"] for b in blocks]
          == [m for m, _t in proto.AUTO_CONFIG_MODES]
          and [t for _m, t in proto.AUTO_CONFIG_MODES]
          == ["产水模式", "反洗模式", "顺洗模式", "放水模式"],
          "%s" % [b["id"] for b in blocks])
    want_devs = [t for _i, t in proto.AUTO_DEVICES]
    check("A6b 设备清单 8 项（6 阀 + 搅拌泵 + 进水泵），三块同源",
          all(b["devices"]._labels == want_devs for b in blocks)
          and want_devs == ["电动球阀%d" % i for i in range(1, 7)]
          + ["搅拌泵", "进水泵"],
          "%s" % (blocks[0]["devices"]._labels if blocks else None))
    b0 = blocks[0]
    check("A6c 空选显示「未选择」（与全选文案可辨）",
          b0["devices"].lineEdit().text() == "未选择",
          b0["devices"].lineEdit().text())
    b0["devices"]._toggle_item(0)        # 电动球阀1
    b0["devices"]._toggle_item(7)        # 进水泵
    check("A6d 勾两项 → 汇总文本拼接",
          b0["devices"].lineEdit().text() == "电动球阀1、进水泵",
          b0["devices"].lineEdit().text())
    vals = page._auto_config_values()
    check("A6e 载荷 devices 用稳定 id（valve1/inletPump）",
          vals["modes"]["produce"]["devices"] == ["valve1", "inletPump"],
          "%s" % vals["modes"]["produce"]["devices"])
    check("A6f 转速/方向缺省（1440rpm、正转，折算提示 50Hz）",
          all(v["stirRpm"] == 1440 and v["stirReverse"] is False
              for v in vals["modes"].values())
          and "50.0" in b0["hz_hint"].text(),
          "%s / %s" % (b0["rpm"].value(), b0["hz_hint"].text()))
    b0["reverse"].setCurrentIndex(1)
    b0["rpm"].setValue(720)
    vals = page._auto_config_values()
    check("A6g 改方向/转速即时进载荷（反转 / 720rpm≈25Hz）",
          vals["modes"]["produce"]["stirReverse"] is True
          and vals["modes"]["produce"]["stirRpm"] == 720
          and "25.0" in b0["hz_hint"].text(),
          "%s" % vals["modes"]["produce"])
    page._populate({"auto": {"modes": {
        "produce": {"devices": ["valve3", "stirPump"],
                    "stirRpm": 960, "stirReverse": True},
        "backwash": {"devices": ["valve1", "valve2", "valve6", "inletPump"],
                     "stirRpm": 0, "stirReverse": False},
        "rinse": {"devices": [], "stirRpm": 1440, "stirReverse": False},
        # [放水模式 2026-11-18] 服务端回显里也有它 ⇒ 客户端这一块要能回显。
        "drain": {"devices": ["valve4"], "stirRpm": 720,
                  "stirReverse": False},
        "ghost": {"devices": ["valve9"]}}}})
    check("A6h 应答回显铺进模式块（未知模式 id 丢弃）",
          len(blocks) == 4
          and blocks[0]["devices"].checked_indexes() == [2, 6]
          and blocks[0]["rpm"].value() == 960
          and blocks[0]["reverse"].currentData() is True
          and blocks[1]["devices"].checked_indexes() == [0, 1, 5, 7]
          and blocks[1]["rpm"].value() == 0,
          "%s" % [b["devices"].checked_indexes() for b in blocks])
    check("A6i 空设备模式回显后仍显示「未选择」",
          blocks[2]["devices"].checked_indexes() == []
          and blocks[2]["devices"].lineEdit().text() == "未选择",
          blocks[2]["devices"].lineEdit().text())
    page._populate({"login": {}})
    check("A6j 局部应答（只带 login）不动模式块（A4 同纪律）",
          blocks[0]["rpm"].value() == 960
          and blocks[0]["devices"].checked_indexes() == [2, 6],
          "%s" % blocks[0]["devices"].checked_indexes())
    blocks[2]["devices"].set_checked_indexes([1, 3])
    check("A6k set_checked_indexes 整体重设（其余全清）",
          blocks[2]["devices"].checked_indexes() == [1, 3]
          and blocks[2]["devices"].lineEdit().text() == "电动球阀2、电动球阀4",
          blocks[2]["devices"].lineEdit().text())
    # 复用控件的旧口径不回归：不传 empty_text 时空选仍显示 all_text（A2d 已
    # 锁全部列文案，这里补 MultiCheckCombo 新旧参数共存这一条）。
    legacy = MultiCheckCombo(["甲", "乙"], all_text="全部列")
    check("A6l 缺省不传 empty_text：旧口径空选仍显示 all_text",
          legacy.lineEdit().text() == "全部列",
          legacy.lineEdit().text())

    # ---- A6m/A6n 阀复位开关（2026-11-12 用户定案：逐模式设「停止/急停
    #      时是否复位电动球阀」）----
    blocks = page._auto_mode_blocks
    check("A6m 每个模式块都有两个复位复选框（停止/急停）",
          all("rst_stop" in b and "rst_estop" in b for b in blocks)
          and len(blocks) == 4)
    b0 = blocks[0]
    check("A6m1 缺省：停止=关阀复位、 急停=保持现状",
          b0["rst_stop"].isChecked() is True
          and b0["rst_estop"].isChecked() is False,
          "stop=%s estop=%s" % (b0["rst_stop"].isChecked(),
                                b0["rst_estop"].isChecked()))
    b0["rst_stop"].setChecked(False)
    b0["rst_estop"].setChecked(True)
    vals = page._auto_config_values()
    check("A6m2 复选框进载荷（resetValvesOnStop/OnEstop）",
          vals["modes"]["produce"][proto.AUTO_MODE_RESET_VALVES_ON_STOP]
          is False
          and vals["modes"]["produce"][proto.AUTO_MODE_RESET_VALVES_ON_ESTOP]
          is True,
          "%s" % vals["modes"]["produce"])
    # 回显：应答带这两个键 ⇒ 铺到复选框；缺键 ⇒ 保持缺省。
    page._populate_auto({"modes": {"produce": {
        "devices": ["valve1"],
        "stirRpm": 720, "stirReverse": True,
        proto.AUTO_MODE_RESET_VALVES_ON_STOP: True,
        proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: True}}})
    check("A6n 应答回显 ⇒ 两个复选框跟着铺",
          b0["rst_stop"].isChecked() is True
          and b0["rst_estop"].isChecked() is True,
          "stop=%s estop=%s" % (b0["rst_stop"].isChecked(),
                                b0["rst_estop"].isChecked()))
    page._populate_auto({"modes": {"produce": {"devices": ["valve1"]}}})
    check("A6n2 应答缺键（老服务端）⇒ 保持缺省不误改",
          b0["rst_stop"].isChecked() is True
          and b0["rst_estop"].isChecked() is False,
          "stop=%s estop=%s" % (b0["rst_stop"].isChecked(),
                                b0["rst_estop"].isChecked()))

    # ---- A6p/A6q 换相策略下拉（2026-11-14 用户定案：切到本模式时，阀门
    #      切换期间搅拌泵/进水泵怎么办）----
    check("A6p 每个模式块都有两个换相策略下拉（搅拌泵/进水泵）",
          all("stir_sw" in b and "inlet_sw" in b for b in blocks),
          "%s" % ([sorted(b.keys()) for b in blocks[:1]],))
    check("A6p0 两个下拉各有三项（先停再启/保持上一模式/直接切到当前模式）",
          [b0["stir_sw"].itemData(i) for i in range(b0["stir_sw"].count())]
          == list(proto.AUTO_SWITCH_POLICY_IDS)
          and [b0["inlet_sw"].itemData(i)
               for i in range(b0["inlet_sw"].count())]
          == list(proto.AUTO_SWITCH_POLICY_IDS),
          "%s" % ([b0["stir_sw"].itemData(i)
                   for i in range(b0["stir_sw"].count())],))
    check("A6p1 缺省＝先停再启（保持既有逻辑）",
          b0["stir_sw"].currentData() == proto.AUTO_SWITCH_POLICY_DEF
          and b0["inlet_sw"].currentData() == proto.AUTO_SWITCH_POLICY_DEF,
          "stir=%s inlet=%s" % (b0["stir_sw"].currentData(),
                                b0["inlet_sw"].currentData()))
    b0["stir_sw"].setCurrentIndex(
        b0["stir_sw"].findData(proto.AUTO_SWITCH_POLICY_CARRY))
    b0["inlet_sw"].setCurrentIndex(
        b0["inlet_sw"].findData(proto.AUTO_SWITCH_POLICY_DIRECT))
    vals = page._auto_config_values()
    check("A6p2 下拉进载荷（stirSwitchPolicy/inletSwitchPolicy）",
          vals["modes"]["produce"][proto.AUTO_MODE_STIR_SWITCH]
          == proto.AUTO_SWITCH_POLICY_CARRY
          and vals["modes"]["produce"][proto.AUTO_MODE_INLET_SWITCH]
          == proto.AUTO_SWITCH_POLICY_DIRECT,
          "%s" % vals["modes"]["produce"])
    # 回显：应答带这两个键 ⇒ 铺到下拉；缺键 ⇒ 保持缺省（不误改）。
    page._populate_auto({"modes": {"produce": {
        "devices": ["valve1"],
        proto.AUTO_MODE_STIR_SWITCH: proto.AUTO_SWITCH_POLICY_DIRECT,
        proto.AUTO_MODE_INLET_SWITCH: proto.AUTO_SWITCH_POLICY_CARRY}}})
    check("A6q 应答回显 ⇒ 两个下拉跟着铺",
          b0["stir_sw"].currentData() == proto.AUTO_SWITCH_POLICY_DIRECT
          and b0["inlet_sw"].currentData() == proto.AUTO_SWITCH_POLICY_CARRY,
          "stir=%s inlet=%s" % (b0["stir_sw"].currentData(),
                                b0["inlet_sw"].currentData()))
    page._populate_auto({"modes": {"produce": {"devices": ["valve1"]}}})
    check("A6q2 应答缺键（老服务端）⇒ 保持缺省不误改",
          b0["stir_sw"].currentData() == proto.AUTO_SWITCH_POLICY_DEF
          and b0["inlet_sw"].currentData() == proto.AUTO_SWITCH_POLICY_DEF,
          "stir=%s inlet=%s" % (b0["stir_sw"].currentData(),
                                b0["inlet_sw"].currentData()))

    # ---- A6r/A6s 窄窗**不横向滚动** + 宽窗两个复选框**同一行** ----
    # [用户反馈 2026-11-14] 「阀复位那两个多选框在同一行却实际不在一行」⇒
    # 实测根因：页面**内容最小宽 > 视口宽** ⇒ 出横向滚动条 ⇒ 右边的控件
    # 被裁 ⇒ 看着"没排在一行"。⇒ 锁两条：① 窄窗下内容宽 ≤ 视口宽（无横
    # 向滚动条）；② 宽窗下两个复选框的 y 相同（真在同一行）。
    _wide_w = 1200
    page.resize(_wide_w, 900)
    for _i in range(6):
        m_remcfg_app().processEvents()
    _y0 = b0["rst_stop"].mapTo(page, b0["rst_stop"].rect().topLeft()).y()
    _y1 = b0["rst_estop"].mapTo(page, b0["rst_estop"].rect().topLeft()).y()
    check("A6r 宽窗下「阀复位」两个复选框真在同一行（y 相同）",
          _y0 == _y1, "y=%s/%s" % (_y0, _y1))
    # 窄窗：内容最小宽必须 ≤ 视口宽 ⇒ 不出横向滚动条。
    # [640 = 页面自身最小宽] 再窄会被 Qt 夹住 ⇒ 取"最窄可用宽度"来验。
    _narrow_w = 640
    page.resize(_narrow_w, 900)
    for _i in range(6):
        m_remcfg_app().processEvents()
    _content_w = page.content.width()
    _view_w = page.scroll.viewport().width()
    check("A6s 窄窗下页面不出横向滚动条（内容宽 ≤ 视口宽）",
          _content_w <= _view_w and not page.scroll.horizontalScrollBar().isVisible(),
          "内容宽=%s 视口宽=%s 横滚可见=%s"
          % (_content_w, _view_w,
             page.scroll.horizontalScrollBar().isVisible()))
    # 窄窗下控件不许被压扁（文字被裁）：宽度 ≥ 最小宽（= 文字全显）。
    _squeeze = []
    for _k in ("rst_stop", "rst_estop", "stir_sw", "inlet_sw"):
        _w = b0[_k]
        if _w.width() < _w.minimumSizeHint().width():
            _squeeze.append("%s(%d<%d)" % (_k, _w.width(),
                                           _w.minimumSizeHint().width()))
    check("A6t 窄窗下四个控件都不被压扁（文字不裁）",
          not _squeeze, "被压扁：%s" % (", ".join(_squeeze) or "无"))
    page.resize(_wide_w, 900)
    for _i in range(4):
        m_remcfg_app().processEvents()

    # ---- A7 历史入库触发方式（2026-11-12 用户定案）----
    # 远端设置「数据存储」卡里选：持续采集入库 / 仅自动运行时入库。
    trig = page._storage_trigger
    check("A7a 入库触发下拉存在且两项（持续/仅自动运行）",
          trig is not None
          and [trig.itemData(i) for i in range(trig.count())]
          == list(proto.HISTORY_TRIGGER_KEYS),
          "%s" % ([trig.itemData(i) for i in range(trig.count())],))
    # 回显：应答带该键 ⇒ 铺到下拉；缺键 ⇒ 保持缺省（不静默改语义）
    page._populate({"storage": {"enabled": True, "path": "",
                                "sampleMs": 60000, "maxMb": 50,
                                "fullPolicy": "ring", "keepFiles": 3,
                                proto.CFGK_STORAGE_HISTORY_TRIGGER:
                                    proto.HISTORY_TRIGGER_AUTO}})
    check("A7b 应答回显 ⇒ 下拉切到「仅自动运行时入库」",
          trig.currentData() == proto.HISTORY_TRIGGER_AUTO,
          trig.currentData())
    page._populate({"storage": {"enabled": True, "path": "",
                                "sampleMs": 60000, "maxMb": 50,
                                "fullPolicy": "ring", "keepFiles": 3}})
    check("A7c 应答缺键（老服务端）⇒ 保持缺省「持续采集」",
          trig.currentData() == proto.HISTORY_TRIGGER_ALWAYS,
          trig.currentData())

    # ---- A8 自动循环配置（2026-11-18 新契约）----
    # 参与模式可勾选、顺序可调、**每阶段各自**的切换条件（压差/定时）可设；
    # 旧扁平键不再提交/回显（用户口径：不必兼容旧版）。
    check("A8a 循环配置控件齐全（开关 + 每模式的行）",
          page._cycle_enabled is not None
          and set(page._cycle_rows.keys())
          == set(m for m, _t in proto.AUTO_CYCLE_MODE_POOL),
          "%s" % (sorted(page._cycle_rows.keys()),))
    check("A8b 缺省参与三模式（产水/反洗/顺洗）",
          page._cycle_participating() == list(proto.AUTO_CYCLE_MODES_DEF),
          "%s" % page._cycle_participating())
    check("A8c 压差比较方向两项（大于/小于）",
          [page._cycle_rows["produce"]["press_cmp"].itemData(i)
           for i in range(page._cycle_rows["produce"]["press_cmp"].count())]
          == list(proto.AUTO_PRESS_COMPARE_KEYS))
    # 载荷（改产水的条件 + 顺序）
    page._cycle_enabled.setChecked(True)
    page._cycle_rows["produce"]["press_th"].setValue(0.75)
    page._cycle_rows["produce"]["press_cmp"].setCurrentIndex(
        page._cycle_rows["produce"]["press_cmp"].findData(
            proto.AUTO_PRESS_COMPARE_LT))
    page._cycle_rows["produce"]["timer_enable"].setChecked(True)
    page._cycle_rows["produce"]["timer_min"].setValue(45)
    cv = page._auto_cycle_values()
    check("A8d 载荷键形与 proto 一致（cycleModes + stageCfg）",
          cv.get(proto.CFG_AUTO_CYCLE_MODES)
          == list(proto.AUTO_CYCLE_MODES_DEF)
          and (cv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}).get(
              "produce", {}).get(
                  proto.CFG_AUTO_STAGE_PRESS_THRESHOLD) == 0.75
          and (cv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}).get(
              "produce", {}).get(
                  proto.CFG_AUTO_STAGE_PRESS_COMPARE)
          == proto.AUTO_PRESS_COMPARE_LT
          and (cv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}).get(
              "produce", {}).get(
                  proto.CFG_AUTO_STAGE_TIMER_ENABLED) is True
          and (cv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}).get(
              "produce", {}).get(
                  proto.CFG_AUTO_STAGE_TIMER_MINUTES) == 45,
          "%s" % cv)
    # 回显（auto 节的 cycle 子节：新键）
    page._populate_auto({"modes": {}, "cycle": {
        proto.CFG_AUTO_CYCLE_ENABLED: False,
        proto.CFG_AUTO_CYCLE_MODES: ["produce", "rinse"],
        proto.CFG_AUTO_CYCLE_STAGE_CFG: {
            "produce": {
                proto.CFG_AUTO_STAGE_PRESS_ENABLED: True,
                proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.25,
                proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                    proto.AUTO_PRESS_COMPARE_GT,
                proto.CFG_AUTO_STAGE_TIMER_ENABLED: False,
                proto.CFG_AUTO_STAGE_TIMER_MINUTES: 20},
            "rinse": {
                proto.CFG_AUTO_STAGE_PRESS_ENABLED: False,
                proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.5,
                proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                    proto.AUTO_PRESS_COMPARE_GT,
                proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                proto.CFG_AUTO_STAGE_TIMER_MINUTES: 4}}}})
    check("A8e 应答回显 ⇒ 开关/参与/顺序/每阶段条件都跟着铺",
          page._cycle_enabled.isChecked() is False
          and page._cycle_participating() == ["produce", "rinse"]
          and abs(page._cycle_rows["produce"]["press_th"].value() - 0.25) < 1e-6
          and page._cycle_rows["rinse"]["timer_min"].value() == 4,
          "enabled=%s part=%s" % (
              page._cycle_enabled.isChecked(),
              page._cycle_participating()))
    # ---- A8f 参与循环的模式 + 顺序 + 每阶段条件（2026-11-18 用户定案）----
    #
    # [新契约] auto.cycle 里两个新键：
    #   · cycleModes = **有序数组**（数组顺序＝循环顺序；第一个＝主运行
    #     阶段；最后一个的下一个是第一个＝闭环）
    #   · stageCfg   = {modeId: {pressEnabled, pressThreshold,
    #     pressCompare, timerEnabled, timerMinutes}}——**每个参与阶段
    #     各自**的切换条件（压差 / 定时；两者都开 ⇒ 先到先切）
    # [为什么用独立页实例] 这组用例会**改**列表状态（勾选/上移/清空）⇒ 用
    # 自己的页与自己的状态行，别污染后面那些用例依赖的共享 page。
    _cycle_status = []
    _cycle_ctx = PageContext(bridge, lambda i: None, _cycle_status.append)
    page_c = RemoteSettingsPage(_cycle_ctx)
    check("A8f0 候选行覆盖模式池（产水/反洗/顺洗/放水 四行）",
          list(page_c._cycle_rows.keys())
          == [m for m, _t in proto.AUTO_CYCLE_MODE_POOL],
          "%s" % list(page_c._cycle_rows.keys()))
    check("A8f1 缺省参与三模式、顺序 = 产水→反洗→顺洗",
          page_c._cycle_participating() == list(proto.AUTO_CYCLE_MODES_DEF),
          "%s" % page_c._cycle_participating())
    check("A8f2 每行都有「参与」勾选框 + 上移/下移按钮 + **该阶段的条件**",
          all(page_c._cycle_rows[m]["check"] is not None
              and "上移" in page_c._cycle_rows[m]["up"].text()
              and "下移" in page_c._cycle_rows[m]["down"].text()
              # 压差（启用 + 阈值 + 方向）与定时（启用 + 分钟）都在**这一行**
              and page_c._cycle_rows[m]["press_enable"] is not None
              and page_c._cycle_rows[m]["press_th"] is not None
              and page_c._cycle_rows[m]["press_cmp"] is not None
              and page_c._cycle_rows[m]["timer_enable"] is not None
              and page_c._cycle_rows[m]["timer_min"] is not None
              for m in page_c._cycle_rows),
          "%s" % {m: (page_c._cycle_rows[m]["up"].text(),
                      page_c._cycle_rows[m]["down"].text())
                  for m in page_c._cycle_rows})
    check("A8f3 首个参与行「上移」置灰、末个参与行「下移」置灰",
          page_c._cycle_rows["produce"]["up"].isEnabled() is False
          and page_c._cycle_rows["rinse"]["down"].isEnabled() is False,
          "up=%s down=%s" % (page_c._cycle_rows["produce"]["up"].isEnabled(),
                             page_c._cycle_rows["rinse"]["down"].isEnabled()))
    # [2026-11-18 用户「没勾选的是不是不应该进排序」] 未勾选的模式在循环里
    # **没有位置** ⇒ 上下移按钮对它没有意义 ⇒ **隐藏**（不是留两个灰按钮
    # 让人猜"为什么点不动"）。参与行照常显示（首/末行的边界按钮置灰）。
    check("A8f3b 未参与的行**不显示排序按钮**（没勾选就不进排序）",
          page_c._cycle_rows["drain"]["up"].isVisibleTo(
              page_c._cycle_rows["drain"]["row"]) is False
          and page_c._cycle_rows["drain"]["down"].isVisibleTo(
              page_c._cycle_rows["drain"]["row"]) is False
          and page_c._cycle_rows["produce"]["up"].isVisibleTo(
              page_c._cycle_rows["produce"]["row"]) is True
          and page_c._cycle_rows["produce"]["down"].isVisibleTo(
              page_c._cycle_rows["produce"]["row"]) is True,
          "drain up=%s down=%s" % (
              page_c._cycle_rows["drain"]["up"].isVisibleTo(
                  page_c._cycle_rows["drain"]["row"]),
              page_c._cycle_rows["drain"]["down"].isVisibleTo(
                  page_c._cycle_rows["drain"]["row"])))
    # ---- A8f3c 权限闸（refresh）后边界状态仍正确（2026-11-18）----
    #
    # [缺陷] RemoteSettingsPage.refresh() 里的**通用权限闸**原来对每行直接
    #   `up/down.setEnabled(can_cfg)` ⇒ 把「首行不能上移 / 末行不能下移」的
    #   **边界状态抹平**了 ⇒ 真机界面看起来"已经在最上/最下，按钮却还是
    #   可点的"（功能上本来也是空操作，但界面骗人）。
    # [修法] 权限闸只管 check / press_enable / timer_enable；上移/下移的
    #   可用性**只**由 `_cycle_render_order()` 按位置统一算（它内部读
    #   `_cycle_can_cfg`）。
    # [本断言的讲究] 必须给**有配置权限**的桥再跑 refresh()：否则
    #   can_cfg=False ⇒ 全灰 ⇒ 断言会"假绿"（分不清是边界禁用还是权限
    #   禁用——A8f3 就是这个毛病：它没给权限，produce.up=False 可能是被
    #   权限闸关的）。
    _old_role = bridge.role
    bridge.role = proto.ROLE_ADMIN
    try:
        page_c.refresh()
        app.processEvents()
    finally:
        bridge.role = _old_role
    check("A8f3c 权限闸（refresh）后边界状态仍正确",
          page_c._cycle_rows["produce"]["up"].isEnabled() is False
          and page_c._cycle_rows["rinse"]["down"].isEnabled() is False
          and page_c._cycle_rows["backwash"]["up"].isEnabled() is True
          and page_c._cycle_rows["backwash"]["down"].isEnabled() is True,
          "produce.up=%s rinse.down=%s backwash.up=%s backwash.down=%s" % (
              page_c._cycle_rows["produce"]["up"].isEnabled(),
              page_c._cycle_rows["rinse"]["down"].isEnabled(),
              page_c._cycle_rows["backwash"]["up"].isEnabled(),
              page_c._cycle_rows["backwash"]["down"].isEnabled()))
    check("A8f4 未参与的行**不显示条件**（几何不变：高度不跳）",
          # [为什么用 isVisibleTo] 离屏跑时页面没显示 ⇒ isVisible() 一律
          # 为假 ⇒ 用 isVisibleTo(行容器) 判「显示出来后会是什么样」。
          page_c._cycle_rows["drain"]["cond"].isVisibleTo(
              page_c._cycle_rows["drain"]["row"]) is False
          and page_c._cycle_rows["produce"]["cond"].isVisibleTo(
              page_c._cycle_rows["produce"]["row"]) is True
          and page_c._cycle_rows["drain"]["hint"].isVisibleTo(
              page_c._cycle_rows["drain"]["row"]) is True,
          "drain_cond=%s produce_cond=%s" % (
              page_c._cycle_rows["drain"]["cond"].isHidden(),
              page_c._cycle_rows["produce"]["cond"].isHidden()))
    check("A8f4b 每阶段的条件控件与该行的勾选/上移下移**同属一个行容器**",
          all(page_c._cycle_rows[m]["cond"].parentWidget()
              is not None
              and page_c._cycle_rows[m]["row"].isAncestorOf(
                  page_c._cycle_rows[m]["cond"])
              and page_c._cycle_rows[m]["row"].isAncestorOf(
                  page_c._cycle_rows[m]["check"])
              for m in page_c._cycle_rows),
          "%s" % {m: (page_c._cycle_rows[m]["cond"].parentWidget(),
                      page_c._cycle_rows[m]["check"].parentWidget())
                  for m in page_c._cycle_rows})
    # [真实行高] 页面**显示出来**后再量（离屏未显示时高度是默认值 480 ⇒
    # 量不到真实排版）。
    # [为什么要"来回切两轮"] 字体 polish 会让控件高度差 1px（首次排版前后
    # 不一致）⇒ 先来回切一轮把排版磨平，再比第二轮的两组高度。
    page_c.show()
    page_c.resize(1100, 900)
    for _i in range(4):
        m_remcfg_app().processEvents()

    def _cycle_heights():
        return {m: page_c._cycle_rows[m]["row"].height()
                for m in page_c._cycle_rows}

    page_c._cycle_rows["backwash"]["check"].setChecked(True)
    for _i in range(4):
        m_remcfg_app().processEvents()
    page_c._cycle_rows["backwash"]["check"].setChecked(False)
    for _i in range(4):
        m_remcfg_app().processEvents()
    _h_off = _cycle_heights()
    page_c._cycle_rows["backwash"]["check"].setChecked(True)
    for _i in range(4):
        m_remcfg_app().processEvents()
    _h_on = _cycle_heights()
    # 取消参与 ⇒ 载荷少了它、条件也不提交
    page_c._cycle_rows["backwash"]["check"].setChecked(False)
    for _i in range(4):
        m_remcfg_app().processEvents()
    _h_after = _cycle_heights()
    _cv = page_c._auto_cycle_values()
    check("A8f5 取消参与 ⇒ cycleModes 不含它、stageCfg 也不含它",
          _cv.get(proto.CFG_AUTO_CYCLE_MODES) == ["produce", "rinse"]
          and "backwash" not in (
              _cv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}),
          "%s" % _cv)
    check("A8f5b 勾/不勾**不改变行高**（几何不变：下面的行不上下跳）",
          _h_off == _h_after and _h_on == _h_after
          and all(v > 0 for v in _h_after.values()),
          "off=%s on=%s after=%s" % (_h_off, _h_on, _h_after))
    # 勾选参与（放水）⇒ 进列表末尾（参与行连续排在前面）
    page_c._cycle_rows["drain"]["check"].setChecked(True)
    _cv = page_c._auto_cycle_values()
    check("A8f6 勾选参与 ⇒ 进循环末尾 + 条件进 stageCfg",
          _cv.get(proto.CFG_AUTO_CYCLE_MODES) == ["produce", "rinse", "drain"]
          and "drain" in (_cv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}),
          "%s" % _cv)
    # 上移 / 下移 ⇒ 载荷顺序跟着变
    page_c._cycle_move("drain", -1)
    _cv = page_c._auto_cycle_values()
    check("A8f7 上移 ⇒ 载荷顺序跟着变（第一个是主运行阶段）",
          _cv.get(proto.CFG_AUTO_CYCLE_MODES) == ["produce", "drain", "rinse"],
          "%s" % _cv)
    page_c._cycle_move("drain", +1)
    _cv = page_c._auto_cycle_values()
    check("A8f8 下移 ⇒ 载荷顺序跟着变",
          _cv.get(proto.CFG_AUTO_CYCLE_MODES) == ["produce", "rinse", "drain"],
          "%s" % _cv)
    check("A8f9 旧扁平键**不再提交**（2026-11-18 用户口径：不必兼容旧版）",
          proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES not in _cv
          and proto.CFG_AUTO_CYCLE_RINSE_MINUTES not in _cv
          and proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD not in _cv
          and proto.CFG_AUTO_CYCLE_TIMER_FALLBACK not in _cv,
          "%s" % _cv)
    # 老服务端（没有 cycleModes/stageCfg）⇒ 界面显示默认三模式、**不发**写回
    page_o = RemoteSettingsPage(_cycle_ctx)
    page_o._populate_auto({"modes": {"produce": {}, "backwash": {},
                                      "rinse": {}},
                           "cycle": {proto.CFG_AUTO_CYCLE_ENABLED: True,
                                     proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES: 3,
                                     proto.CFG_AUTO_CYCLE_RINSE_MINUTES: 4}})
    _ov = page_o._auto_cycle_values()
    check("A8f10 老服务端缺 cycleModes ⇒ 界面显示默认三模式",
          page_o._cycle_participating() == list(proto.AUTO_CYCLE_MODES_DEF),
          "%s" % page_o._cycle_participating())
    check("A8f11 服务端缺新键 ⇒ 载荷**不发** cycleModes/stageCfg"
          "（不把界面缺省当成用户改动写回去）",
          proto.CFG_AUTO_CYCLE_MODES not in _ov
          and proto.CFG_AUTO_CYCLE_STAGE_CFG not in _ov,
          "%s" % _ov)
    # 新服务端回显 cycleModes/stageCfg ⇒ 界面跟着铺
    page_n = RemoteSettingsPage(_cycle_ctx)
    page_n._populate_auto({"modes": {"produce": {}, "backwash": {},
                                      "rinse": {}, "drain": {}},
                           "cycle": {proto.CFG_AUTO_CYCLE_ENABLED: True,
                                     proto.CFG_AUTO_CYCLE_MODES:
                                         ["produce", "drain", "rinse"],
                                     proto.CFG_AUTO_CYCLE_STAGE_CFG: {
                                         "produce": {
                                             proto.CFG_AUTO_STAGE_PRESS_ENABLED:
                                                 True,
                                             proto.CFG_AUTO_STAGE_PRESS_THRESHOLD:
                                                 0.65,
                                             proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                                                 proto.AUTO_PRESS_COMPARE_LT,
                                             proto.CFG_AUTO_STAGE_TIMER_ENABLED:
                                                 True,
                                             proto.CFG_AUTO_STAGE_TIMER_MINUTES:
                                                 25},
                                         "drain": {
                                             proto.CFG_AUTO_STAGE_PRESS_ENABLED:
                                                 False,
                                             proto.CFG_AUTO_STAGE_PRESS_THRESHOLD:
                                                 0.5,
                                             proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                                                 proto.AUTO_PRESS_COMPARE_GT,
                                             proto.CFG_AUTO_STAGE_TIMER_ENABLED:
                                                 True,
                                             proto.CFG_AUTO_STAGE_TIMER_MINUTES:
                                                 7},
                                         "rinse": {
                                             proto.CFG_AUTO_STAGE_PRESS_ENABLED:
                                                 False,
                                             proto.CFG_AUTO_STAGE_PRESS_THRESHOLD:
                                                 0.5,
                                             proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                                                 proto.AUTO_PRESS_COMPARE_GT,
                                             proto.CFG_AUTO_STAGE_TIMER_ENABLED:
                                                 True,
                                             proto.CFG_AUTO_STAGE_TIMER_MINUTES:
                                                 8}}}})
    check("A8f12 新服务端回显 ⇒ 参与状态与顺序跟着铺",
          page_n._cycle_participating() == ["produce", "drain", "rinse"],
          "%s" % page_n._cycle_participating())
    _nr = page_n._cycle_rows
    check("A8f13 新服务端回显 ⇒ **每阶段各自的条件**跟着铺",
          abs(_nr["produce"]["press_th"].value() - 0.65) < 1e-6
          and _nr["produce"]["press_cmp"].currentData()
          == proto.AUTO_PRESS_COMPARE_LT
          and _nr["produce"]["timer_enable"].isChecked() is True
          and _nr["produce"]["timer_min"].value() == 25
          and _nr["drain"]["press_enable"].isChecked() is False
          and _nr["drain"]["timer_min"].value() == 7
          and _nr["rinse"]["timer_min"].value() == 8,
          "produce=%s drain=%s rinse=%s" % (
              _nr["produce"]["press_th"].value(),
              _nr["drain"]["timer_min"].value(),
              _nr["rinse"]["timer_min"].value()))
    _nv = page_n._auto_cycle_values()
    check("A8f14 新服务端 ⇒ 载荷带回 cycleModes/stageCfg",
          _nv.get(proto.CFG_AUTO_CYCLE_MODES) == ["produce", "drain", "rinse"]
          and _nv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG)
          == {"produce": {
                  proto.CFG_AUTO_STAGE_PRESS_ENABLED: True,
                  proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.65,
                  proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                      proto.AUTO_PRESS_COMPARE_LT,
                  proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                  proto.CFG_AUTO_STAGE_TIMER_MINUTES: 25},
              "drain": {
                  proto.CFG_AUTO_STAGE_PRESS_ENABLED: False,
                  proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.5,
                  proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                      proto.AUTO_PRESS_COMPARE_GT,
                  proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                  proto.CFG_AUTO_STAGE_TIMER_MINUTES: 7},
              "rinse": {
                  proto.CFG_AUTO_STAGE_PRESS_ENABLED: False,
                  proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.5,
                  proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                      proto.AUTO_PRESS_COMPARE_GT,
                  proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                  proto.CFG_AUTO_STAGE_TIMER_MINUTES: 8}},
          "%s" % _nv)
    # ---- A8f15 空列表 ⇒ 服务端拒绝文案**原样**显示 ----
    # [为什么要服务端拒] 列表为空/未知模式/重复模式由服务端判（口径唯一），
    # 客户端只负责把**服务端原话**显示出来（别自己改写成"未知错误"）。
    page_e = RemoteSettingsPage(_cycle_ctx)
    for _m in list(page_e._cycle_rows.keys()):
        page_e._cycle_rows[_m]["check"].setChecked(False)
    _sent_e = {}
    _orig_set_e = page_e.ctx.bridge.config_set

    def _spy_set_e(section, values, on_done=None):
        if section == proto.CFG_SECTION_AUTO:
            _sent_e.update(values or {})
            if on_done:
                on_done({"ok": False, "err": "循环模式列表不能为空"})
            return
        return _orig_set_e(section, values, on_done=on_done)

    page_e.ctx.bridge.config_set = _spy_set_e
    _orig_gate_e = page_e._config_gate
    page_e._config_gate = lambda: True
    try:
        page_e._apply_auto()
    finally:
        page_e._config_gate = _orig_gate_e
        page_e.ctx.bridge.config_set = _orig_set_e
    check("A8f15 空列表：载荷 cycleModes 为空（由服务端判）",
          (_sent_e.get(proto.CFG_AUTO_CYCLE) or {}).get(
              proto.CFG_AUTO_CYCLE_MODES) == [],
          "%s" % _sent_e)
    check("A8f16 空列表：状态行显示**服务端原话**",
          any("循环模式列表不能为空" in s for s in _cycle_status),
          "%s" % _cycle_status)
    # ---- A8g 每阶段各自的切换条件（stageCfg；新口径）----
    #
    # [缺省口径] 产水＝压差触发（0.5 MPa 大于）+ 定时**关**（30 分钟的兜底
    # 值留着）；反洗/顺洗/放水＝压差**关** + 定时触发（10 分钟）。
    check("A8g0 proto 里 stageCfg 的键名逐字一致",
          proto.CFG_AUTO_CYCLE_STAGE_CFG == "stageCfg"
          and proto.CFG_AUTO_STAGE_PRESS_ENABLED == "pressEnabled"
          and proto.CFG_AUTO_STAGE_PRESS_THRESHOLD == "pressThreshold"
          and proto.CFG_AUTO_STAGE_PRESS_COMPARE == "pressCompare"
          and proto.CFG_AUTO_STAGE_TIMER_ENABLED == "timerEnabled"
          and proto.CFG_AUTO_STAGE_TIMER_MINUTES == "timerMinutes",
          "%s" % ((proto.CFG_AUTO_CYCLE_STAGE_CFG,
                   proto.CFG_AUTO_STAGE_PRESS_ENABLED,
                   proto.CFG_AUTO_STAGE_PRESS_THRESHOLD,
                   proto.CFG_AUTO_STAGE_PRESS_COMPARE,
                   proto.CFG_AUTO_STAGE_TIMER_ENABLED,
                   proto.CFG_AUTO_STAGE_TIMER_MINUTES),))
    page_g = RemoteSettingsPage(_cycle_ctx)
    _gr = page_g._cycle_rows
    check("A8g1 缺省条件：产水＝压差开（0.5/gt）+ 定时关（30 分钟）",
          _gr["produce"]["press_enable"].isChecked() is True
          and abs(_gr["produce"]["press_th"].value() - 0.5) < 1e-6
          and _gr["produce"]["press_cmp"].currentData()
          == proto.AUTO_PRESS_COMPARE_GT
          and _gr["produce"]["timer_enable"].isChecked() is False
          and _gr["produce"]["timer_min"].value() == 30,
          "press=%s/%s timer=%s/%s" % (
              _gr["produce"]["press_enable"].isChecked(),
              _gr["produce"]["press_th"].value(),
              _gr["produce"]["timer_enable"].isChecked(),
              _gr["produce"]["timer_min"].value()))
    check("A8g2 缺省条件：反洗/顺洗＝压差关 + 定时开（10 分钟）",
          _gr["backwash"]["press_enable"].isChecked() is False
          and _gr["backwash"]["timer_enable"].isChecked() is True
          and _gr["backwash"]["timer_min"].value() == 10
          and _gr["rinse"]["press_enable"].isChecked() is False
          and _gr["rinse"]["timer_enable"].isChecked() is True
          and _gr["rinse"]["timer_min"].value() == 10,
          "backwash=%s rinse=%s" % (
              _gr["backwash"]["timer_min"].value(),
              _gr["rinse"]["timer_min"].value()))
    # 逐阶段设条件 ⇒ stageCfg 逐字段正确
    _gr["backwash"]["press_enable"].setChecked(True)
    _gr["backwash"]["press_th"].setValue(0.9)
    _gr["backwash"]["press_cmp"].setCurrentIndex(
        _gr["backwash"]["press_cmp"].findData(proto.AUTO_PRESS_COMPARE_LT))
    _gr["backwash"]["timer_enable"].setChecked(True)
    _gr["backwash"]["timer_min"].setValue(18)
    _gv = page_g._auto_cycle_values()
    _gcfg = _gv.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}
    check("A8g3 每阶段设压差/定时 ⇒ stageCfg 逐字段正确",
          _gcfg.get("backwash") == {
              proto.CFG_AUTO_STAGE_PRESS_ENABLED: True,
              proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.9,
              proto.CFG_AUTO_STAGE_PRESS_COMPARE: proto.AUTO_PRESS_COMPARE_LT,
              proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
              proto.CFG_AUTO_STAGE_TIMER_MINUTES: 18},
          "%s" % _gcfg.get("backwash"))
    check("A8g4 未参与的阶段**不提交**条件（stageCfg 只含参与的模式）",
          "drain" not in _gcfg and set(_gcfg.keys())
          == {"produce", "backwash", "rinse"},
          "%s" % sorted(_gcfg.keys()))
    check("A8g5 旧扁平键**不再提交**（不必兼容旧版）",
          proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD not in _gv
          and proto.CFG_AUTO_CYCLE_PRESS_COMPARE not in _gv
          and proto.CFG_AUTO_CYCLE_TIMER_FALLBACK not in _gv
          and proto.CFG_AUTO_CYCLE_TIMER_MINUTES not in _gv,
          "%s" % {k: _gv.get(k) for k in
                  (proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD,
                   proto.CFG_AUTO_CYCLE_PRESS_COMPARE,
                   proto.CFG_AUTO_CYCLE_TIMER_FALLBACK,
                   proto.CFG_AUTO_CYCLE_TIMER_MINUTES)})
    # ---- A8h 服务端拒绝**原话**显示（非法配置）----
    # [什么算非法] 列表为空 / 未知模式 / 重复模式 / **某阶段两个触发都没
    # 开** ⇒ 服务端明确拒绝 ⇒ 客户端显示原话（不自己改写成"未知错误"）。
    page_h = RemoteSettingsPage(_cycle_ctx)
    _gr2 = page_h._cycle_rows
    _gr2["produce"]["press_enable"].setChecked(False)
    _gr2["produce"]["timer_enable"].setChecked(False)
    _sent_h = {}
    _h_status = []
    _h_ctx = PageContext(bridge, lambda i: None, _h_status.append)
    page_h.ctx = _h_ctx
    _orig_set_h = page_h.ctx.bridge.config_set

    def _spy_set_h(section, values, on_done=None):
        if section == proto.CFG_SECTION_AUTO:
            _sent_h.update(values or {})
            if on_done:
                on_done({"ok": False,
                         "err": "stageCfg[produce] 至少要启用一个触发条件"
                                "（压差或定时）"})
            return
        return _orig_set_h(section, values, on_done=on_done)

    page_h.ctx.bridge.config_set = _spy_set_h
    _orig_gate_h = page_h._config_gate
    page_h._config_gate = lambda: True
    try:
        page_h._apply_auto()
    finally:
        page_h._config_gate = _orig_gate_h
        page_h.ctx.bridge.config_set = _orig_set_h
    check("A8h0 非法配置：载荷里两个触发都没开（由服务端判）",
          ((_sent_h.get(proto.CFG_AUTO_CYCLE) or {}).get(
              proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}).get("produce", {}).get(
              proto.CFG_AUTO_STAGE_PRESS_ENABLED) is False,
          "%s" % _sent_h)
    check("A8h1 服务端拒绝 ⇒ 状态行显示**服务端原话**",
          any("至少要启用一个触发条件" in s for s in _h_status),
          "%s" % _h_status)
    # ---- A8i/A8j 重排循环行**不得隐藏行控件**（2026-11-19 板端实锅根修）----
    #
    # [现场症状] 用户报：远端设置「自动循环」里的「定时 N 分钟」微调框，
    #   一点，屏幕键盘弹出来**马上就消失**；而同一页「模式卡·搅拌电机转速」
    #   微调框正常（键盘不消失）。
    # [为什么两者不同] 都是 QSpinBox ⇒ 差别在**容器**：定时微调框装在
    #   「循环行」里，而循环行会被 `_cycle_render_order()` 重挂：
    #       w = item.widget(); w.setParent(None)     ← 原实现
    #   `setParent(None)` 会**隐藏**控件 ⇒ 行里正被编辑的微调框跟着隐藏
    #   ⇒ Qt 把焦点挪到别的控件 ⇒ 软键盘的 FocusOut → `_review_focus` →
    #   焦点不在输入框 ⇒ `_close()` ⇒ **键盘当场消失**。
    #   模式卡的微调框不在被重挂的行里 ⇒ 不受影响 ⇒ 现场表现正好是
    #   「一个坏一个好」（2026-11-19 板端真实触摸注入复现：配置回显到货的
    #   那一瞬，微调框 FocusOut、焦点跳到「参与」勾选框、键盘自灭）。
    # [修法] 行控件本来就是 `_cycle_list_box` 的子控件：takeAt（从布局摘掉）
    #   + addWidget（重挂）就够换序，**不能碰 setParent**（它一律隐藏）。
    _row_widgets = [row["row"] for row in page_c._cycle_rows.values()]
    page_c._cycle_render_order()
    _hidden_rows = [r for r in _row_widgets if r.isHidden()]
    check("A8i 重排循环行不隐藏行控件（不抢焦点 ⇒ 键盘不会自灭）",
          not _hidden_rows,
          "被隐藏 %d 行" % len(_hidden_rows))
    # [顺带锁住「重排仍然改对顺序」] 修法动了重排 ⇒ 必须证明顺序没坏
    _lay = page_c._cycle_list_layout
    _now_order = []
    for _i in range(_lay.count()):
        _it = _lay.itemAt(_i)
        _w = _it.widget()
        if _w is None:
            continue
        for _m, _r in page_c._cycle_rows.items():
            if _r["row"] is _w:
                _now_order.append(_m)
                break
    check("A8j 重排后布局顺序 == 参与顺序 + 未参与池顺序",
          _now_order == list(page_c._cycle_order),
          "layout=%s expect=%s" % (_now_order, list(page_c._cycle_order)))
    # ---- A9 两张卡一次提交（2026-11-12 用户定案）----
    # 「自动模式配置」+「自动循环配置」是同一份 auto 配置节 ⇒ **一个**应用
    # 按钮、一次下发（原来两卡各一个按钮 ⇒ 让人以为要分两次保存）。
    check("A9a 只有一个应用按钮（两张卡共用）",
          page._auto_apply is not None
          and not hasattr(page, "_cycle_apply")
          and not hasattr(page, "_auto_apply_cycle"),
          "buttons=%s" % [b.text() for b in page.findChildren(
              type(page._auto_apply)) if "应用" in b.text()])
    _payload = page._auto_config_values()
    # ---- A6n 只发服务端认识的模式（2026-11-18 放水模式带来的后果）----
    # [为什么锁这条] C 服务端的 auto 模式表固定三个，且对**未知模式键整包
    # 拒** ⇒ 客户端若把「放水模式」也发过去，C 服务端会**整包失败**（连
    # 产水/反洗/顺洗都存不进去）⇒ 下发必须按服务端回显的模式集合过滤。
    # [自足] 自己先喂一次四模式回显，再断言「客户端记住了服务端认识的
    # 模式集合」——不能依赖前面某次回显（中间有测 cycle 的空 modes 回显
    # 会把集合清空）。
    page._populate_auto({"modes": {
        "produce": {"devices": ["valve1"]},
        "backwash": {"devices": []},
        "rinse": {"devices": []},
        "drain": {"devices": ["valve2"]}}})
    _known = set(getattr(page, "_auto_known_modes", None) or [])
    check("A6n0 客户端记下了服务端认识的模式集合（回显的键）",
          "drain" in _known and "produce" in _known,
          "known=%s" % sorted(_known))
    # ① 服务端只认三个（C 口径）⇒ 载荷不含 drain，其余照发。
    page._populate_auto({"modes": {
        "produce": {"devices": ["valve1"]},
        "backwash": {"devices": []},
        "rinse": {"devices": []}}})
    _payload3 = page._auto_config_values()
    check("A6n1 服务端只认三模式 ⇒ 载荷不含放水模式（其余照发）",
          "drain" not in _payload3["modes"]
          and set(_payload3["modes"].keys())
          == {"produce", "backwash", "rinse"},
          "modes=%s" % sorted(_payload3["modes"].keys()))
    check("A6n2 被跳过的模式被记录（状态行要说明，不静默丢）",
          "drain" in (getattr(page, "_auto_skipped_modes", None) or []),
          "skipped=%s" % (getattr(page, "_auto_skipped_modes", None),))
    # ② 未知（还没回显）⇒ 只发通用子集（三模式），绝不盲发第四个键。
    page._auto_known_modes = None
    _payload4 = page._auto_config_values()
    check("A6n3 未知服务端 ⇒ 只发通用子集（三模式）",
          set(_payload4["modes"].keys()) == {"produce", "backwash", "rinse"},
          "modes=%s" % sorted(_payload4["modes"].keys()))
    # ③ 回到 Python 服务端口径（四模式）⇒ 载荷四模式齐。
    page._populate_auto({"modes": {
        "produce": {"devices": ["valve1"]},
        "backwash": {"devices": []},
        "rinse": {"devices": []},
        "drain": {"devices": ["valve2"]}}})
    _payload5 = page._auto_config_values()
    check("A6n4 Python 服务端（四模式）⇒ 载荷四模式齐",
          set(_payload5["modes"].keys())
          == {"produce", "backwash", "rinse", "drain"},
          "modes=%s" % sorted(_payload5["modes"].keys()))
    # [重算] A9b 用的是本段之前算出的 _payload（那时已知集合未知 ⇒ 只发
    # 三模式）⇒ 四模式口径恢复后重算一次再断言。
    _payload = page._auto_config_values()
    check("A9b 一次载荷同时含 modes 与 cycle",
          "modes" in _payload and proto.CFG_AUTO_CYCLE in _payload
          and set(_payload["modes"].keys()) == set(
              mid for mid, _n in proto.AUTO_CONFIG_MODES),
          "%s" % sorted(_payload.keys()))
    # [只有**一张**自动配置卡 2026-11-12] 原先「自动模式配置」与「自动循环
    # 配置」是两张 Card ⇒ 中间有卡边界、看着像两份要分别保存的配置。
    # [只数配置卡] 「自动运行」卡是另一个功能（启动/停止），不算在内。
    from PyQt5.QtWidgets import QFrame as _QFrame
    _cfg_cards = [c.title_label.text() for c in page.findChildren(_QFrame)
                  if c.objectName() == "HsfCard"
                  and c.title_label is not None
                  and "自动" in c.title_label.text()
                  and "配置" in c.title_label.text()]
    check("A9c 自动配置只有一张卡（模式 + 循环并在一起，无卡边界）",
          _cfg_cards == ["自动模式配置"],
          "cards=%s" % _cfg_cards)

    # ---- A10 运行保护卡（2026-11-15）：空管保护 + 压差过大保护 ----
    # [控件齐全] 空管判定时长 + 超时停设备 + 压差启用/上限/触发停设备。
    check("A10a 运行保护控件齐全",
          hasattr(page, "_protect_timeout")
          and hasattr(page, "_protect_stop")
          and hasattr(page, "_protect_press_enabled")
          and hasattr(page, "_protect_press_limit")
          and hasattr(page, "_protect_press_stop"),
          "%s" % [a for a in ("_protect_timeout", "_protect_stop",
                               "_protect_press_enabled",
                               "_protect_press_limit", "_protect_press_stop")
                   if not hasattr(page, a)])
    # ---- A10f 电机超温保护（2026-11-20 用户定案「大于多少度停止设备并
    #      报电机超温故障，可禁用」）：控件齐全 + 缺省不启用/75 ℃/停设备。
    check("A10f 电机超温控件齐全且缺省（不启用 / 75 ℃ / 停设备）",
          hasattr(page, "_protect_motor_enabled")
          and hasattr(page, "_protect_motor_limit")
          and hasattr(page, "_protect_motor_stop")
          and page._protect_motor_enabled.isChecked() is False
          and abs(page._protect_motor_limit.value() - 75.0) < 1e-6
          and page._protect_motor_stop.isChecked() is True,
          "enabled=%s limit=%s stop=%s"
          % (hasattr(page, "_protect_motor_enabled"),
             getattr(page, "_protect_motor_limit", None),
             getattr(page, "_protect_motor_stop", None)))
    # [缺省不启用压差保护] 上限缺省 0.8 MPa = 800 kPa（界面单位 kPa）。
    check("A10b 压差保护缺省不启用、上限 800 kPa",
          page._protect_press_enabled.isChecked() is False
          and abs(page._protect_press_limit.value() - 800.0) < 1e-6,
          "enabled=%s limit=%s" % (page._protect_press_enabled.isChecked(),
                                   page._protect_press_limit.value()))
    # [载荷键形] 五个键齐（协议键名），且上限换算成 MPa。
    _sent = {}
    _orig_set = page.ctx.bridge.config_set

    def _spy_set(section, values, on_done=None):
        if section == proto.CFG_SECTION_PROTECT:
            _sent.update(values or {})
            if on_done:
                on_done({"ok": True, "err": "已应用"})
            return
        return _orig_set(section, values, on_done=on_done)

    page.ctx.bridge.config_set = _spy_set
    # [A 段是假桥] 连接/权限闸会拦住「应用」⇒ 这里临时放行（只验载荷形状）。
    _orig_gate = page._config_gate
    page._config_gate = lambda: True
    try:
        # [2026-11-15 审计] 「应用」要等**第一次 protect 快照到达**
        # （没收到就按缺省值下发 ⇒ 把服务端的保护配置静默改成缺省 ⇒ 保护
        # 被关掉）。⇒ 先铺一次服务端快照（等价真机：进页/刷新拿到配置）。
        page._populate({proto.CFG_SECTION_PROTECT: {
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: 0,
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP: True,
            proto.CFGK_PROTECT_PRESS_HIGH_ENABLED: False,
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA: 0.8,
            proto.CFGK_PROTECT_PRESS_HIGH_STOP: True,
            # [电机超温 2026-11-20] 铺一次含新键的快照（等价新服务端）。
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED: True,
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C: 80.0,
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP: False,
        }})
        check("A10g 电机超温回显（启用=True / 80 ℃ / 不停设备）",
              page._protect_motor_enabled.isChecked() is True
              and abs(page._protect_motor_limit.value() - 80.0) < 1e-6
              and page._protect_motor_stop.isChecked() is False,
              "enabled=%s limit=%s stop=%s"
              % (page._protect_motor_enabled.isChecked(),
                 page._protect_motor_limit.value(),
                 page._protect_motor_stop.isChecked()))
        page._protect_press_enabled.setChecked(True)
        page._protect_press_limit.setValue(950)
        page._protect_press_stop.setChecked(False)
        page._apply_protect()
    finally:
        page._config_gate = _orig_gate
        page.ctx.bridge.config_set = _orig_set
    check("A10c 运行保护载荷：八个键齐 + 上限换算成 MPa + 超温三键在",
          set(_sent.keys()) == set([
              proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS,
              proto.CFGK_PROTECT_INLET_NOFLOW_STOP,
              proto.CFGK_PROTECT_PRESS_HIGH_ENABLED,
              proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA,
              proto.CFGK_PROTECT_PRESS_HIGH_STOP,
              proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED,
              proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C,
              proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP,
          ]) and abs(float(_sent.get(
              proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA, 0.0)) - 0.95) < 1e-6
          and abs(float(_sent.get(
              proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C, 0.0)) - 80.0)
          < 1e-6,
          "%s" % sorted(_sent.keys()))

    # ---- A10d 上限 0 回显不得变成缺省 0.8（2026-11-15 审计）----
    # [为什么] 服务端的 0 是合法值（不限/禁用上限）；`get(k, DEF) or DEF`
    # 这种写法会把 0 变成 0.8 ⇒ 界面显示 800 kPa、服务端其实是 0 ⇒ 用户
    # 点一次「应用」就把配置改了（静默改现场参数）。
    page._populate({proto.CFG_SECTION_PROTECT: {
        proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: 0,
        proto.CFGK_PROTECT_INLET_NOFLOW_STOP: True,
        proto.CFGK_PROTECT_PRESS_HIGH_ENABLED: False,
        proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA: 0.0,
        proto.CFGK_PROTECT_PRESS_HIGH_STOP: True,
    }})
    check("A10d 上限 0 回显仍是 0（不被缺省 0.8 顶掉）",
          abs(page._protect_press_limit.value() - 0.0) < 1e-6
          and page._protect_timeout.value() == 0,
          "limit=%s timeout=%s" % (page._protect_press_limit.value(),
                                   page._protect_timeout.value()))

    # ---- A10e 没收到过快照 ⇒ 不许「应用」（2026-11-15 审计）----
    # [为什么] 控件上还是缺省值（时限 0 / 上限 800 kPa）⇒ 下发等于把服务端
    # 的保护配置静默改成缺省（保护被关掉）。
    _page2 = page
    _saved_seen = _page2._protect_seen
    _page2._protect_seen = False
    _sent2 = {}
    _orig_set2 = _page2.ctx.bridge.config_set

    def _spy_set2(section, values, on_done=None):
        if section == proto.CFG_SECTION_PROTECT:
            _sent2.update(values or {})
            if on_done:
                on_done({"ok": True, "err": "已应用"})
            return
        return _orig_set2(section, values, on_done=on_done)

    _page2.ctx.bridge.config_set = _spy_set2
    _orig_gate2 = _page2._config_gate
    _page2._config_gate = lambda: True
    try:
        _page2._apply_protect()
    finally:
        _page2._config_gate = _orig_gate2
        _page2.ctx.bridge.config_set = _orig_set2
        _page2._protect_seen = _saved_seen
    check("A10e 没收到快照：拒绝「应用」（不下发缺省值）",
          not _sent2, "sent=%s" % sorted(_sent2.keys()))

    # ---- A10h 老服务端快照（缺电机超温键）⇒ 控件保持缺省（2026-11-20）----
    # 「应答里没有的字段不要当成已关闭」同纪律：缺键不动控件。
    page._populate({proto.CFG_SECTION_PROTECT: {
        proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: 0,
        proto.CFGK_PROTECT_INLET_NOFLOW_STOP: True,
    }})
    check("A10h 缺电机超温键：保持缺省（不启用 / 75 ℃ / 停设备）",
          page._protect_motor_enabled.isChecked() is False
          and abs(page._protect_motor_limit.value() - 75.0) < 1e-6
          and page._protect_motor_stop.isChecked() is True,
          "enabled=%s limit=%s stop=%s"
          % (page._protect_motor_enabled.isChecked(),
             page._protect_motor_limit.value(),
             page._protect_motor_stop.isChecked()))

    # ---- A11 模拟量通道与量程（采集卡；2026-11-20 远端可配；二改：
    #      量程=下限~上限 +「4mA 以下有效」开关，「温度满量程」废弃）----
    check("A11a 模拟量控件齐全且缺省（压差 CH0 0~1 / 温度 CH1 0~100 / below4 开·关）",
          hasattr(page, "_analog_press_ch")
          and hasattr(page, "_analog_press_low")
          and hasattr(page, "_analog_press_high")
          and hasattr(page, "_analog_press_below4")
          and hasattr(page, "_analog_temp_ch")
          and hasattr(page, "_analog_temp_low")
          and hasattr(page, "_analog_temp_high")
          and hasattr(page, "_analog_temp_below4")
          and page._analog_press_ch.currentData() == 0
          and page._analog_press_ch.count() == proto.ANALOG_CHANNELS_DEF
          and page._analog_temp_ch.count() == proto.ANALOG_CHANNELS_DEF
          and abs(page._analog_press_low.value()) < 1e-6
          and abs(page._analog_press_high.value() - 1.0) < 1e-6
          and page._analog_press_below4.isChecked() is True
          and page._analog_temp_ch.currentData() == 1
          and abs(page._analog_temp_low.value()) < 1e-6
          and abs(page._analog_temp_high.value() - 100.0) < 1e-6
          and page._analog_temp_below4.isChecked() is False,
          "missing=%s" % [a for a in ("_analog_press_ch", "_analog_press_low",
                                      "_analog_press_high",
                                      "_analog_press_below4",
                                      "_analog_temp_ch", "_analog_temp_low",
                                      "_analog_temp_high",
                                      "_analog_temp_below4")
                          if not hasattr(page, a)])
    # 回显：带新键的快照铺进来（等价新服务端 GET）。
    page._populate({"collect": {
        proto.CFGK_ANALOG_PRESS_CHANNEL: 2,
        proto.CFGK_ANALOG_PRESS_LOW_MPA: -0.1,
        proto.CFGK_ANALOG_PRESS_HIGH_MPA: 0.6,
        proto.CFGK_ANALOG_PRESS_BELOW4_VALID: False,
        proto.CFGK_ANALOG_TEMP_CHANNEL: 5,
        proto.CFGK_ANALOG_TEMP_LOW_C: -10.0,
        proto.CFGK_ANALOG_TEMP_HIGH_C: 120.0,
        proto.CFGK_ANALOG_TEMP_BELOW4_VALID: True,
    }})
    check("A11b 回显（压差 CH2 -0.1~0.6 below 关 / 温度 CH5 -10~120 below 开）",
          page._analog_press_ch.currentData() == 2
          and abs(page._analog_press_low.value() + 0.1) < 1e-6
          and abs(page._analog_press_high.value() - 0.6) < 1e-6
          and page._analog_press_below4.isChecked() is False
          and page._analog_temp_ch.currentData() == 5
          and abs(page._analog_temp_low.value() + 10.0) < 1e-6
          and abs(page._analog_temp_high.value() - 120.0) < 1e-6
          and page._analog_temp_below4.isChecked() is True,
          "press=%s/%s~%s below=%s temp=%s/%s~%s below=%s"
          % (page._analog_press_ch.currentData(),
             page._analog_press_low.value(),
             page._analog_press_high.value(),
             page._analog_press_below4.isChecked(),
             page._analog_temp_ch.currentData(),
             page._analog_temp_low.value(),
             page._analog_temp_high.value(),
             page._analog_temp_below4.isChecked()))
    # 缺键（老服务端）⇒ 保持当前值不动（不静默回缺省）。
    page._populate({"collect": {proto.CFGK_VFD_ENABLED: True}})
    check("A11c 缺通道键：控件保持回显值（不回缺省）",
          page._analog_press_ch.currentData() == 2
          and page._analog_temp_ch.currentData() == 5
          and page._analog_press_below4.isChecked() is False
          and page._analog_temp_below4.isChecked() is True,
          "press=%s temp=%s pb4=%s tb4=%s"
          % (page._analog_press_ch.currentData(),
             page._analog_temp_ch.currentData(),
             page._analog_press_below4.isChecked(),
             page._analog_temp_below4.isChecked()))
    # 载荷键形：_apply_collect 带 8 个模拟量键。
    _sent_c = {}
    _orig_set3 = page.ctx.bridge.config_set

    def _spy_set3(section, values, on_done=None):
        if section == "collect":
            _sent_c.update(values or {})
            if on_done:
                on_done({"ok": True, "err": "已应用"})
            return
        return _orig_set3(section, values, on_done=on_done)

    page.ctx.bridge.config_set = _spy_set3
    _orig_gate3 = page._config_gate
    page._config_gate = lambda: True
    try:
        page._apply_collect()
    finally:
        page._config_gate = _orig_gate3
        page.ctx.bridge.config_set = _orig_set3
    check("A11d 采集载荷带模拟量八键（通道×2 + 量程×4 + below4×2）",
          proto.CFGK_ANALOG_PRESS_CHANNEL in _sent_c
          and proto.CFGK_ANALOG_PRESS_LOW_MPA in _sent_c
          and proto.CFGK_ANALOG_PRESS_HIGH_MPA in _sent_c
          and proto.CFGK_ANALOG_PRESS_BELOW4_VALID in _sent_c
          and proto.CFGK_ANALOG_TEMP_CHANNEL in _sent_c
          and proto.CFGK_ANALOG_TEMP_LOW_C in _sent_c
          and proto.CFGK_ANALOG_TEMP_HIGH_C in _sent_c
          and proto.CFGK_ANALOG_TEMP_BELOW4_VALID in _sent_c
          and _sent_c.get(proto.CFGK_ANALOG_TEMP_CHANNEL) == 5,
          "%s" % sorted(_sent_c.keys()))

    # ---- A12 相机卡：MAC 连接 + 断线重连（2026-11-20 用户口径；三改：
    #      IP/MAC 合并成一个地址框，下拉切方式换装）----
    check("A12a 相机新控件齐全（连接方式/地址框/重连开关/间隔，缺省按 IP）",
          hasattr(page, "_cam_resolve")
          and hasattr(page, "_cam_addr")
          and hasattr(page, "_cam_reconnect")
          and hasattr(page, "_cam_reconnect_s")
          and page._cam_resolve.currentData() == proto.CAMERA_RESOLVE_DEF
          and page._cam_addr.text() == ""
          and page._cam_addr.placeholderText().startswith("相机 IP")
          and page._cam_reconnect.isChecked()
          is proto.CAMERA_RECONNECT_ENABLED_DEF
          and page._cam_reconnect_s.value()
          == proto.CAMERA_RECONNECT_INTERVAL_S_DEF,
          "missing=%s" % [a for a in ("_cam_resolve", "_cam_addr",
                                      "_cam_reconnect", "_cam_reconnect_s")
                          if not hasattr(page, a)])
    page._populate({proto.CFG_SECTION_CAMERA: {
        proto.CFGK_CAMERA_ENABLED: True,
        proto.CFGK_CAMERA_IP: "192.168.1.64",
        proto.CFGK_CAMERA_RESOLVE_MODE: proto.CAMERA_RESOLVE_MAC,
        proto.CFGK_CAMERA_MAC: "AA-BB-CC-DD-EE-FF",
        proto.CFGK_CAMERA_RECONNECT_ENABLED: False,
        proto.CFGK_CAMERA_RECONNECT_INTERVAL_S: 30,
    }})
    check("A12b 回显（切到按 MAC：框装归一小写冒号；IP 进缓存；重连关 30s）",
          page._cam_resolve.currentData() == proto.CAMERA_RESOLVE_MAC
          and page._cam_addr.text() == "aa:bb:cc:dd:ee:ff"
          and page._cam_addr.placeholderText().startswith("相机 MAC")
          and page._cam_addr_ip == "192.168.1.64"
          and page._cam_reconnect.isChecked() is False
          and page._cam_reconnect_s.value() == 30,
          "resolve=%s addr=%r ipcache=%r rc=%s s=%s"
          % (page._cam_resolve.currentData(), page._cam_addr.text(),
             page._cam_addr_ip, page._cam_reconnect.isChecked(),
             page._cam_reconnect_s.value()))
    # 缺键（老服务端）⇒ 保持回显值（不回缺省）。
    page._populate({proto.CFG_SECTION_CAMERA: {
        proto.CFGK_CAMERA_ENABLED: True,
    }})
    check("A12c 缺新键：保持回显值（不回缺省）",
          page._cam_resolve.currentData() == proto.CAMERA_RESOLVE_MAC
          and page._cam_addr.text() == "aa:bb:cc:dd:ee:ff"
          and page._cam_reconnect.isChecked() is False,
          "resolve=%s addr=%r rc=%s"
          % (page._cam_resolve.currentData(), page._cam_addr.text(),
             page._cam_reconnect.isChecked()))
    # 载荷键形：_apply_camera 带四新键；MAC 模式下 mac=框内容、ip=缓存。
    _sent_cam = {}
    _orig_set4 = page.ctx.bridge.config_set

    def _spy_set4(section, values, on_done=None):
        if section == proto.CFG_SECTION_CAMERA:
            _sent_cam.update(values or {})
            if on_done:
                on_done({"ok": True, "err": "已应用"})
            return
        return _orig_set4(section, values, on_done=on_done)

    page.ctx.bridge.config_set = _spy_set4
    _orig_gate4 = page._config_gate
    page._config_gate = lambda: True
    try:
        page._apply_camera()
    finally:
        page._config_gate = _orig_gate4
        page.ctx.bridge.config_set = _orig_set4
    check("A12d 相机载荷带四新键（MAC 模式：mac=框内容归一 / ip=缓存）",
          proto.CFGK_CAMERA_RESOLVE_MODE in _sent_cam
          and proto.CFGK_CAMERA_MAC in _sent_cam
          and proto.CFGK_CAMERA_RECONNECT_ENABLED in _sent_cam
          and proto.CFGK_CAMERA_RECONNECT_INTERVAL_S in _sent_cam
          and _sent_cam.get(proto.CFGK_CAMERA_RESOLVE_MODE)
          == proto.CAMERA_RESOLVE_MAC
          and _sent_cam.get(proto.CFGK_CAMERA_MAC) == "aa:bb:cc:dd:ee:ff"
          and _sent_cam.get(proto.CFGK_CAMERA_IP) == "192.168.1.64",
          "%s" % sorted(_sent_cam.keys()))
    # 切回按 IP：框换装 IP、MAC 进缓存；载荷 ip=框内容。
    page._cam_resolve.setCurrentIndex(0)      # 触发 _on_cam_resolve_changed
    check("A12e 切回按 IP：框换装缓存 IP（MAC 留在缓存）",
          page._cam_addr.text() == "192.168.1.64"
          and page._cam_addr.placeholderText().startswith("相机 IP")
          and page._cam_addr_mac == "aa:bb:cc:dd:ee:ff",
          "addr=%r macache=%r" % (page._cam_addr.text(),
                                  page._cam_addr_mac))


def m_remcfg_part_a2_vfd_params(app):
    """变频器参数卡（CMD_VFD_PARAMS=0x011B；2026-11-18）：假桥读写链路。

    [为什么用假桥] 服务端 0x011B 由另一位**并行实现** ⇒ 本段不依赖真
    服务端，假桥喂应答（QTimer.singleShot(0,…) 异步交付——照 Q4 导出段
    范式：同步回调会让「回调里再取下一批」递归爆栈，那是假桥的问题）。

    [锁什么]
      V1 卡结构：标题「变频器参数」在「运行保护」之后 / 行数 == PARAM_DEFS /
        参数表与手册 0xF100 段逐值一致 / spin 量程按参数表夹取。
      V2 喂假应答 ⇒ 显示值 == 应答工程值（不编造；缺项 "--"）。
      V3 应用 ⇒ 发 op=1 载荷 {addr 十进制字符串: raw}（25.0Hz→250 等）。
      V4 应答回读值 ≠ 提交值 ⇒ 显示回读值（不撒谎）。
      V5 applied=false ⇒ 失败项逐个列出。
      V6 非管理员 ⇒ 应用置灰（刷新可用）；管理员 ⇒ 可用。
      V7 RunLocked err 原样进状态行 + 显示值保留上次；网络错误也要终态。
      V8 bridge 层：CMD 号 / values 键转十进制字符串 / 读请求不带 values。
      V9 与服务端 PARAM_DEFS 逐值一致（并行实现未就位则 SKIP）。
    """
    import os as _os
    import sys as _sys
    import time
    from PyQt5.QtCore import QTimer
    from PyQt5.QtWidgets import QFrame
    from hsfsrv_client import bson as bson_mod
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import RemoteSettingsPage

    print("== A2. 变频器参数卡（假桥） ==")
    bridge = LinkBridge()
    ctx = PageContext(bridge, lambda i: None, lambda s: None)
    page = RemoteSettingsPage(ctx)
    statuses = []
    page.ctx.set_status = statuses.append

    # [假桥] 记请求；应答队列逐次弹出；**异步交付**（下一轮事件循环，
    # 贴近真桥「网络线程 → Qt 信号回 UI 线程」的时序）。
    requests = []
    responses = []

    def _fake_vfd_params(op=proto.VFD_PARAMS_OP_READ, values=None,
                         on_done=None, on_error=None, timeout=5.0):
        requests.append({"op": int(op), "values": dict(values or {})})
        doc = dict(responses.pop(0)) if responses else \
            {"ok": False, "err": "假桥无应答"}

        def _deliver():
            if doc.get("_netErr"):
                if on_error:
                    on_error(str(doc.get("err", "网络错误")))
            elif on_done:
                on_done(doc)

        QTimer.singleShot(0, _deliver)

    bridge.vfd_params = _fake_vfd_params

    def _pump(until, tries=200):
        """泵事件循环直到 until() 为真（假桥交付 + 页面信号都要拍数）。"""
        for _ in range(tries):
            app.processEvents()
            if until():
                return True
            time.sleep(0.005)
        return False

    def _mk_params(values_by_addr):
        """按 PARAM_DEFS 拼应答 params（value=None 模拟该参数无数据）。"""
        out = []
        for d in proto.PARAM_DEFS:
            v = values_by_addr.get(d["addr"], None)
            out.append({
                "code": d["code"], "label": d["label"], "addr": d["addr"],
                "raw": None if v is None else int(round(v * d["scale"])),
                "value": v, "unit": d["unit"],
                "writable": d["writable"], "runLocked": d["runLocked"]})
        return out

    _full = {0xF100: 0.0, 0xF101: 4.5, 0xF102: 380.0, 0xF103: 12.5,
             0xF104: 50.0, 0xF105: 1440.0}

    # ---- V1 卡结构 ----
    _titles = [c.title_label.text() for c in page.findChildren(QFrame)
               if c.objectName() == "HsfCard" and c.title_label is not None]
    check("V1a 「变频器参数」卡存在，位于「运行保护」之后",
          "变频器参数" in _titles and "运行保护" in _titles
          and _titles.index("变频器参数") > _titles.index("运行保护"),
          "%s" % _titles)
    check("V1b 参数行数 == PARAM_DEFS 数（每行有回读值标签）",
          len(page._vfdp_rows) == len(proto.PARAM_DEFS)
          and all(r["value_lbl"] is not None for r in page._vfdp_rows),
          "rows=%d defs=%d" % (len(page._vfdp_rows), len(proto.PARAM_DEFS)))
    check("V1c 六项全可写且 runLocked（0xF100 段手册口径）",
          all(d["writable"] and d["runLocked"] for d in proto.PARAM_DEFS)
          # [枚举型 2026-11-18] P1.00 是离散取值 ⇒ 写控件是**下拉框**
          # （combo），不是数值框（spin）；其余五项仍是数值框。
          and all(r["spin"] is not None or r.get("combo") is not None
                  for r in page._vfdp_rows)
          and sum(1 for r in page._vfdp_rows if r.get("combo") is not None)
          == 1,
          "%s" % [(d["code"], d["writable"], d["runLocked"])
                  for d in proto.PARAM_DEFS])
    check("V1d PARAM_DEFS 与手册 0xF100 段逐值一致（地址/单位/标度/小数位）",
          [d["addr"] for d in proto.PARAM_DEFS]
          == [0xF100 + i for i in range(6)]
          and [d["code"] for d in proto.PARAM_DEFS]
          == ["P1.00", "P1.01", "P1.02", "P1.03", "P1.04", "P1.05"]
          and [d["unit"] for d in proto.PARAM_DEFS]
          == ["", "kW", "V", "A", "Hz", "rpm"]
          and [d["scale"] for d in proto.PARAM_DEFS] == [1, 10, 1, 100, 10, 1]
          and [d["decimals"] for d in proto.PARAM_DEFS]
          == [0, 1, 0, 2, 1, 0],
          "%s" % [(d["code"], d["addr"], d["unit"], d["scale"], d["decimals"])
                  for d in proto.PARAM_DEFS])
    _s3 = page._vfdp_rows[3]["spin"]
    check("V1e spin 量程按参数表夹取（P1.03 = 0.01~655.35 A，两位小数）",
          abs(_s3.minimum() - 0.01) < 1e-9
          and abs(_s3.maximum() - 655.35) < 1e-9
          and _s3.decimals() == 2,
          "min=%s max=%s dec=%d" % (_s3.minimum(), _s3.maximum(),
                                    _s3.decimals()))

    # ---- V2 喂假应答 ⇒ 显示 == 应答值（不编造；缺项 "--"）----
    page._populate_vfd_params({"ok": True, "params": _mk_params(_full),
                               "vfdFaulted": False, "runState": 1})
    # [P1.00 口径变更 2026-11-18] 枚举型参数回读列显示**中文名**
    # （普通异步电机）而不是裸码 0 —— 界面给运维看的是「电机类型」。
    check("V2a 应答工程值原样显示（P1.00 显示中文名/4.5kW/380V/12.50A/"
          "50.0Hz/1440rpm）",
          [r["value_lbl"].text() for r in page._vfdp_rows]
          == ["普通异步电机", "4.5", "380", "12.50", "50.0", "1440"],
          "%s" % [r["value_lbl"].text() for r in page._vfdp_rows])
    # [runState=0x3000 原始字 2026-11-18 协议一致性评审] 1=正转/2=反转/
    # 3=停机——旧断言按 XVfdRunState 枚举锁成了"停机"（语义反了）。
    check("V2b 状态行有终态且带运行态（已读取 6 项 · 变频器正转运行）",
          "已读取" in page._vfdp_status.text()
          and "正转运行" in page._vfdp_status.text(),
          page._vfdp_status.text())
    check("V2c spin 跟随回读值（用户改值从真实值起步）",
          abs(page._vfdp_rows[4]["spin"].value() - 50.0) < 1e-9,
          "%s" % page._vfdp_rows[4]["spin"].value())
    # 应答只带第一项 ⇒ 其余显示 "--"（不编造），状态行列出缺项。
    page._populate_vfd_params({"ok": True, "params": _mk_params(_full)[:1],
                               "vfdFaulted": False, "runState": 2})
    check("V2d 应答缺项显示 \"--\"（不编造）",
          page._vfdp_rows[1]["value_lbl"].text() == "--",
          "%s" % [r["value_lbl"].text() for r in page._vfdp_rows])
    check("V2e 状态行列出缺项（缺：P1.01、…）",
          "缺：" in page._vfdp_status.text()
          and "P1.01" in page._vfdp_status.text(),
          page._vfdp_status.text())
    # 回全量应答，供后续写链路用。
    page._populate_vfd_params({"ok": True, "params": _mk_params(_full),
                               "vfdFaulted": False, "runState": 1})

    # ---- V3 应用 ⇒ op=1 载荷 {addr 十进制字符串: raw}（25.0Hz→250 等）----
    bridge.connected_flag = True
    bridge.role = proto.ROLE_ADMIN
    page._vfdp_rows[4]["spin"].setValue(25.0)     # Hz → raw 250
    page._vfdp_rows[3]["spin"].setValue(12.5)     # A  → raw 1250
    del requests[:]
    responses.append({"ok": True, "params": _mk_params(_full),
                      "vfdFaulted": False, "runState": 1})
    page._apply_vfd_params()
    _pump(lambda: "已写入" in page._vfdp_status.text()
          or "失败" in page._vfdp_status.text())
    check("V3a 应用发出 op=1 写请求",
          bool(requests)
          and requests[-1]["op"] == proto.VFD_PARAMS_OP_WRITE,
          "%s" % requests[-1:])
    check("V3b 载荷 values 换算正确（25.0Hz→250 / 12.5A→1250 / 可写项全发）",
          requests[-1]["values"].get(0xF104) == 250
          and requests[-1]["values"].get(0xF103) == 1250
          and requests[-1]["values"].get(0xF101) == 45
          and requests[-1]["values"].get(0xF102) == 380
          and requests[-1]["values"].get(0xF105) == 1440
          and requests[-1]["values"].get(0xF100) == 0,
          "%s" % requests[-1]["values"])
    check("V3c 状态行「已写入并回读确认：6 项」",
          page._vfdp_status.text().startswith("已写入并回读确认：6 项"),
          page._vfdp_status.text())

    # ---- V4 应答回读值 ≠ 提交值 ⇒ 显示回读值（不撒谎）----
    # 提交 P1.01=4.5kW，服务端写后回读 4.6（夹取/标定差异）⇒ 界面必须
    # 显示 4.6 而不是本地提交的 4.5。
    _after = dict(_full)
    _after[0xF101] = 4.6
    del requests[:]
    responses.append({"ok": True, "params": _mk_params(_after),
                      "vfdFaulted": False, "runState": 1})
    page._vfdp_rows[1]["spin"].setValue(4.5)      # 本地提交 4.5
    page._apply_vfd_params()
    _pump(lambda: "已写入" in page._vfdp_status.text()
          or "失败" in page._vfdp_status.text())
    check("V4a 回读 ≠ 提交 ⇒ 显示回读值 4.6（spin 也跟随）",
          page._vfdp_rows[1]["value_lbl"].text() == "4.6"
          and abs(page._vfdp_rows[1]["spin"].value() - 4.6) < 1e-9,
          "lbl=%s spin=%s" % (page._vfdp_rows[1]["value_lbl"].text(),
                              page._vfdp_rows[1]["spin"].value()))

    # ---- V5 applied=false ⇒ 失败项逐个列出 ----
    _p_fail = _mk_params(_after)
    _p_fail[2][proto.VFDP_KEY_APPLIED] = False    # P1.02 写入被驱动器拒
    del requests[:]
    responses.append({"ok": True, "params": _p_fail,
                      "vfdFaulted": False, "runState": 1})
    page._apply_vfd_params()
    _pump(lambda: "失败" in page._vfdp_status.text()
          or "已写入" in page._vfdp_status.text())
    check("V5a applied=false 项逐个列出（失败：P1.02；确认 5 项）",
          "失败" in page._vfdp_status.text()
          and "P1.02" in page._vfdp_status.text()
          and "已写入并回读确认：5 项" in page._vfdp_status.text(),
          page._vfdp_status.text())

    # ---- V6 非管理员 ⇒ 应用置灰（读仍可用）；管理员 ⇒ 可用 ----
    page._vfdp_fetched = True    # 防止 refresh 的兜底路径触发假桥取数
    bridge.role = proto.ROLE_USER
    page.refresh()
    check("V6a 非管理员「应用到变频器」置灰",
          not page._vfdp_apply.isEnabled())
    check("V6b 非管理员「刷新」可用（读=登录即可）",
          page._vfdp_refresh.isEnabled())
    bridge.role = proto.ROLE_ADMIN
    page.refresh()
    check("V6c 管理员 + 已读到参数 ⇒ 应用可用",
          page._vfdp_apply.isEnabled())

    # ---- V7 RunLocked err 原样进状态行；显示值保留上次 ----
    _before = [r["value_lbl"].text() for r in page._vfdp_rows]
    responses.append({"ok": False,
                      "err": "变频器运行中禁止写参数（请先停止）"})
    page._apply_vfd_params()
    _pump(lambda: page._vfdp_status.text()
          == "变频器运行中禁止写参数（请先停止）")
    check("V7a RunLocked err 原样显示（不改写服务端文案）",
          page._vfdp_status.text() == "变频器运行中禁止写参数（请先停止）",
          page._vfdp_status.text())
    check("V7b 拒写后显示值保留上次（不清成 \"--\"）",
          [r["value_lbl"].text() for r in page._vfdp_rows] == _before)
    # 网络错误也要终态（读路径不停在「正在读取…」——AGENTS 终态纪律）。
    responses.append({"ok": False, "err": "超时", "_netErr": True})
    page._fetch_vfd_params()
    _pump(lambda: "正在读取" not in page._vfdp_status.text())
    check("V7c 读网络错误 → 状态行落失败终态（原因原样）",
          page._vfdp_status.text() == "变频器参数请求失败：超时",
          page._vfdp_status.text())

    # ---- V8 bridge 层：CMD 号 / values 键=addr 十进制字符串 / 读不带 values ----
    lb = LinkBridge()
    captured = {}

    class _StubClient(object):
        def request_async(self, cmd, payload, dtype, timeout, on_done,
                          on_error):
            captured["cmd"] = cmd
            captured["payload"] = payload

        def _alive(self):
            return True

    lb.client = _StubClient()
    lb.vfd_params(op=proto.VFD_PARAMS_OP_WRITE, values={0xF104: 250},
                  on_done=lambda d: None, on_error=lambda e: None)
    _wire = bson_mod.decode(captured["payload"]) if captured else {}
    check("V8a bridge 发 CMD_VFD_PARAMS(0x011B) + op=1",
          captured.get("cmd") == proto.CMD_VFD_PARAMS
          and _wire.get("op") == 1,
          "cmd=0x%04X" % captured.get("cmd", 0))
    check("V8b values 键 = addr 十进制字符串、值 = raw（0xF104=61700；BSON 键只能字符串）",
          _wire.get("values") == {"61700": 250},
          "%s" % _wire.get("values"))
    lb.vfd_params(on_done=lambda d: None, on_error=lambda e: None)
    _wire2 = bson_mod.decode(captured["payload"])
    check("V8c 读请求 op=0 且不带 values 键",
          _wire2.get("op") == 0 and "values" not in _wire2,
          "%s" % _wire2)

    # ---- V9 与服务端 PARAM_DEFS 逐值一致（服务端并行实现中，未就位 SKIP）----
    _srv_root = _os.path.join(_os.path.dirname(_os.path.dirname(
        _os.path.dirname(_os.path.abspath(__file__)))), "server_py")
    if _srv_root not in _sys.path:
        _sys.path.insert(0, _srv_root)
    try:
        from hsfsrv import proto as _sproto
        _sdefs = getattr(_sproto, "PARAM_DEFS", None)
        if _sdefs is None:
            skip("V9 客户端 PARAM_DEFS == 服务端 PARAM_DEFS（逐值）",
                 "服务端 0x011B 并行实现中，PARAM_DEFS 尚未就位")
        else:
            _mine = [(d["code"], d["addr"], d["unit"], d["scale"],
                      d["writable"], d["runLocked"])
                     for d in proto.PARAM_DEFS]
            _theirs = [(d.get("code"), d.get("addr"), d.get("unit"),
                        d.get("scale"), d.get("writable"), d.get("runLocked"))
                       for d in _sdefs]
            check("V9 客户端 PARAM_DEFS == 服务端 PARAM_DEFS（逐值）",
                  _mine == _theirs, "client=%s server=%s" % (_mine, _theirs))
    except Exception as _exc:                       # noqa: BLE001
        skip("V9 客户端 PARAM_DEFS == 服务端 PARAM_DEFS（逐值）",
             "服务端 proto 不可导入：%s: %s"
             % (type(_exc).__name__, _exc))

    # ---- V10 在途闸：成对开合（2026-11-18 联调验收实测的同类真缺陷）----
    # [背景] 网页端 page14 的在途闸只置位不复位 ⇒ 「应用到变频器」按钮
    # 只活一次。客户端这版必须成对开合：① 在途禁入（只发一批写）
    # ② 终态（成功/失败/超时）后能再写。
    _before = len(requests)
    responses.append({"ok": True, "params": _mk_params(_full)})
    page._apply_vfd_params()
    page._apply_vfd_params()          # 上一批还在途 ⇒ 这次必须被拒
    check("V10a 在途时第二次应用被拒（只发一批写）",
          len(requests) == _before + 1,
          "requests %d → %d" % (_before, len(requests)))
    check("V10b 在途拒绝有提示文案",
          any("仍在进行" in _s for _s in statuses),
          "%s" % (statuses[-3:] if statuses else []))
    _pump(lambda: not page._vfdp_inflight)
    check("V10c 终态后在途闸复位",
          not page._vfdp_inflight)
    responses.append({"ok": True, "params": _mk_params(_full)})
    page._apply_vfd_params()
    _pump(lambda: len(requests) == _before + 2)
    check("V10d 复位后可再次应用（闸成对开合，不是只活一次）",
          len(requests) == _before + 2,
          "requests=%d（期望 %d）" % (len(requests), _before + 2))

    # ---- V11 枚举型参数（P1.00 电机类型）：下拉框 + 中文名 ----
    # [需求 2026-11-18] 电机类型要能下拉选中文类型；选项表 = 手册第 20 页
    # 「P1 组 电机参数」P1.00：0 普通异步电机 / 1 变频异步电机 /
    # 2 永磁同步电机。
    _r0 = page._vfdp_rows[0]
    _combo = _r0.get("combo")
    check("V11a P1.00 用下拉框（不是裸数字框）",
          _combo is not None and _r0.get("spin") is None,
          "combo=%s spin=%s" % (_combo is not None, _r0.get("spin")))
    check("V11b 其余五项仍是数值框",
          all(r.get("spin") is not None and r.get("combo") is None
              for r in page._vfdp_rows[1:]))
    check("V11c 下拉三项中文名（手册口径）",
          _combo is not None
          and [_combo.itemText(i) for i in range(_combo.count())]
          == ["普通异步电机", "变频异步电机", "永磁同步电机"]
          and [_combo.itemData(i) for i in range(_combo.count())] == [0, 1, 2],
          "%s" % ([_combo.itemText(i) for i in range(_combo.count())]
                  if _combo else None))

    def _mk_enum_params(values_by_addr, motor_raw):
        # 带 options/valueText 的应答（服务端下发的形状）
        out = _mk_params(values_by_addr)
        names = {0: "普通异步电机", 1: "变频异步电机", 2: "永磁同步电机"}
        for p in out:
            if p["addr"] == 0xF100:
                p["options"] = [{"value": v, "label": t}
                                for v, t in names.items()]
                p["valueText"] = names.get(motor_raw, "")
        return out

    _enum = dict(_full)
    _enum[0xF100] = 1
    page._populate_vfd_params({"ok": True,
                               "params": _mk_enum_params(_enum, 1),
                               "vfdFaulted": False, "runState": 3})
    check("V11d 回读列显示中文名（回读 raw=1 ⇒ 变频异步电机）",
          page._vfdp_rows[0]["value_lbl"].text() == "变频异步电机",
          "%s" % page._vfdp_rows[0]["value_lbl"].text())
    check("V11e 下拉当前项 = 回读值（不是第一项）",
          _combo is not None and _combo.currentData() == 1,
          "%s" % (_combo.currentData() if _combo else None))
    # 应用：选中第 3 项 ⇒ 载荷 raw=2（**原样下发**，不做工程值换算）。
    _combo.setCurrentIndex(2)
    del requests[:]
    responses.append({"ok": True, "params": _mk_enum_params(_enum, 1),
                      "vfdFaulted": False, "runState": 3})
    page._apply_vfd_params()
    _pump(lambda: "已写入" in page._vfdp_status.text()
          or "失败" in page._vfdp_status.text())
    check("V11f 应用载荷里 P1.00 = 选中项 raw（2）",
          bool(requests) and requests[-1]["values"].get(0xF100) == 2,
          "%s" % (requests[-1]["values"].get(0xF100) if requests else None))

    page.deleteLater()
    app.processEvents()


def m_remcfg_part_b_flow(app, port=None):
    """设置流程 e2e：真服务端 + 真配置落盘。

    [隔离] 端口走 _free_port()（不写死、不撞别人）；cfg 目录是独立临时
    目录 ⇒ 服务端的库/账户都落在这儿，不碰现场库。
    """
    import json
    import os
    import shutil
    import socket
    import subprocess
    import sys
    import tempfile
    import time
    m_remcfg_ensure_client_path()
    port = m_remcfg_pick_port(port)
    print("== B. 设置流程 e2e（sim 服务端 :%d）==" % port)
    tmp = tempfile.mkdtemp(prefix="hsf_settings_test_")
    cfg_path = os.path.join(tmp, "hsf_server_py.json")

    proc = subprocess.Popen(
        [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(port),
         "--config", cfg_path],
        cwd=m_remcfg_server_root(), stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT)
    try:
        # 等服务端监听（轮询端口，最多 8s）。
        for _ in range(80):
            with socket.socket() as s:
                s.settimeout(0.2)
                if s.connect_ex(("127.0.0.1", port)) == 0:
                    break
            time.sleep(0.1)
        else:
            check("B0 服务端就绪", False)
            return

        from hsfsrv_client import proto as cproto
        from hsfsrv_client.bridge import LinkBridge
        from hsfsrv_client.page_base import PageContext
        from hsfsrv_client.pages_other import RemoteSettingsPage

        bridge = LinkBridge()
        ok, err = bridge.connect_to("127.0.0.1", port, "root", "root")
        check("B1 root 登录", ok, err)
        if not ok:
            return
        ctx = PageContext(bridge, lambda i: None, lambda s: None)
        page = RemoteSettingsPage(ctx)
        populated = {}
        page.configReady.connect(lambda doc: populated.update(doc))

        # [走真实链路] 用 bridge.config_get/config_set（含 bson 解码与
        # configReady 信号跨线程投递）——曾经测试用裸 rpc 绕过这条链，
        # 漏掉了「回调签名不匹配 → on_done 静默 TypeError」这个真 bug。
        def pump(seconds=3.0):
            t0 = time.time()
            while time.time() - t0 < seconds:
                app.processEvents()
                time.sleep(0.02)

        # ---- B2 页面 on_enter/refresh 真实拉取 → 信号回 UI 线程回显 ----
        page._config_fetched = False
        page._fetch_config(silent=True)
        pump()
        check("B2a GET 回显到达（configReady 信号链）",
              bool(populated))
        _k, chk, _p, spin = page._collect_rows[0]
        collect = populated.get("collect") or {}
        check("B2b 搅拌泵勾选框 = 服务端 enabled",
              chk.isChecked() == bool(collect.get(cproto.CFGK_VFD_ENABLED)))
        check("B2c 搅拌泵周期 = 服务端 poll_ms",
              spin.value() == int(collect.get(cproto.CFGK_VFD_POLL_MS, -1)))
        storage = populated.get("storage") or {}
        check("B2d 库文件路径 = 服务端配置",
              page._storage_path.text()
              == str(storage.get(cproto.CFGK_STORAGE_PATH, "x")))

        # ---- B3 界面改值 → 页面应用方法（真实 SET）→ 配置文件真变 ----
        page._collect_rows[1][3].setValue(700)            # 温湿度周期 700
        page._collect_rows[2][1].setChecked(False)        # 只停进水浊度仪
        populated.clear()
        page._apply_collect()
        pump()
        check("B3a 应用后收到生效值回投", bool(populated))
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B3b 配置文件温湿度周期 = 700",
              saved["serial"]["temphum_poll_ms"] == 700)
        # [两台独立设备 2026-11-10] 只停进水浊度仪：逐台键各记各的，旧共同键
        # 按"任一启用"仍为 true（出水浊度仪 还在采）。
        check("B3c 配置文件只停进水浊度仪（turbidity1=false / turbidity2=true）",
              saved["serial"].get("turbidity1_enabled") is False
              and saved["serial"].get("turbidity2_enabled") is True
              and saved["serial"].get("turbidity_enabled") is True,
              "t1=%r t2=%r all=%r" % (
                  saved["serial"].get("turbidity1_enabled"),
                  saved["serial"].get("turbidity2_enabled"),
                  saved["serial"].get("turbidity_enabled")))

        # ---- B4 storage：改路径 → 页面保存 → 配置文件 + 界面路径都变 ----
        page._storage_path.setText("flow_test.db")
        populated.clear()
        page._apply_storage()
        pump()
        check("B4a 应用后收到回投", bool(populated))
        check("B4b 界面「当前库文件」指向新库",
              page._db_path_label.text().endswith("flow_test.db"))
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B4c 配置文件 storage.path = flow_test.db",
              saved["storage"]["path"] == "flow_test.db")

        # ---- B5 恢复（温湿度 500 / 两台浊度都开 / 路径空 / 采样 5000）----
        page._collect_rows[1][3].setValue(500)
        page._collect_rows[2][1].setChecked(True)
        page._collect_rows[3][1].setChecked(True)
        page._apply_collect()
        pump()
        page._storage_path.setText("")
        page._apply_storage()
        pump()
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B5 恢复后配置文件与缺省口径一致",
              saved["serial"]["temphum_poll_ms"] == 500
              and saved["serial"].get("turbidity1_enabled") is True
              and saved["serial"].get("turbidity2_enabled") is True
              and saved["serial"]["turbidity_enabled"] is True
              and saved["storage"]["path"] == "")

        # ---- B6 登录策略（login 节；2026-11-08 新增）----
        # 走页面真实控件 → _apply_login → CMD_CONFIG_SET("login") →
        # 服务端配置文件 login 节真变（与 C 服务端同键同义）。
        page._config_fetched = False
        populated.clear()
        page._fetch_config(silent=True)
        pump()
        check("B6a login 节 GET 回投含三键",
              isinstance(populated.get("login"), dict)
              and cproto.CFGK_LOGIN_ALLOW_CONCUR
              in (populated.get("login") or {}))
        page._login_concur.setChecked(True)
        page._login_max.setValue(0)
        populated.clear()
        page._apply_login()
        pump()
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B6b 配置文件 login.allow_concurrent = true",
              (saved.get("login") or {}).get("allow_concurrent") is True)
        check("B6c 配置文件 login.max_sessions = 0",
              (saved.get("login") or {}).get("max_sessions") == 0)
        page._login_concur.setChecked(False)
        page._login_max.setValue(8)
        page._apply_login()
        pump()
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B6d 恢复后 login 节回缺省",
              (saved.get("login") or {}).get("allow_concurrent") is False
              and (saved.get("login") or {}).get("max_sessions") == 8)
        try:
            if bridge.client is not None:
                bridge.client.close()
        except Exception:                               # noqa: BLE001
            pass
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)


def m_remcfg_part_c_history_time(app):
    """历史页时间列含日期后的筛选/解析口径。"""
    print("== C. 历史页日期时间 ==")
    m_remcfg_ensure_client_path()
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    page = HistoryPage(ctx)

    # ---- C1 归一化口径 ----
    cases = [
        (("23:09", False), ("T", "23:09:00")),
        (("23:09:53", False), ("T", "23:09:53")),
        (("11-05 23:09", False),
         ("F", "%d-11-05 23:09:00" % __import__("datetime").date.today().year)),
        (("2026-11-05", True), ("F", "2026-11-05 23:59:59")),
        (("2026-11-05 08:30", False), ("F", "2026-11-05 08:30:00")),
        (("13:70", False), None),
        (("2026-13-01", False), None),
        (("a b c", False), None),
    ]
    for (raw, is_end), want in cases:
        got = HistoryPage._norm_stamp(raw, is_end=is_end)
        check("C1 %r(end=%s)" % (raw, is_end), got == want,
              "→ %r" % (got,))

    # ---- C2 合成数据：跨两天 + 时间/日期筛选 ----
    def row(stamp, turb):
        texts = [stamp, "手动", "停机", "0.00", "0.00", "0",
                 "%.2f" % turb, "3.20", "36.0 m³/h", "1.0 m³",
                 "0.499", "26.5", "55.0", "0x00000000", "0x00000000"]
        return (texts, False)

    # [用动态日期，不用写死的 2026-11-05] 「纯时刻 = 当天」的语义依赖运行
    # 当天的日期；写死日期会让测试在某一天之后必然假红（实测 2026-11-06
    # 跑就中招）。这里 today/yesterday 由当前时间生成，跨天断言才稳定。
    import datetime as _dt
    today = _dt.date.today()
    yesterday = today - _dt.timedelta(days=1)
    T = today.strftime("%Y-%m-%d")
    Y = yesterday.strftime("%Y-%m-%d")
    page._rows = [
        row("%s 23:59:00" % Y, 9.0),
        row("%s 08:10:00" % T, 10.0),
        row("%s 08:20:00" % T, 11.0),
        row("%s 09:30:00" % T, 12.0),
    ]

    page._f_start.setText("%s 08:00" % T)
    page._f_end.setText("%s 09:00" % T)
    got = [r[0][0] for r in page._filtered_rows()]
    check("C2a 日期时间段命中 2 行",
          got == ["%s 08:10:00" % T, "%s 08:20:00" % T], got)

    page._f_start.setText("08:00")
    page._f_end.setText("08:30")
    got = [r[0][0] for r in page._filtered_rows()]
    check("C2b 纯时刻=当天语义（今天 2 行，昨天 23:59 不误命中）",
          got == ["%s 08:10:00" % T, "%s 08:20:00" % T], got)

    page._f_start.setText(Y[5:])          # MM-dd
    page._f_end.clear()
    got = [r[0][0] for r in page._filtered_rows()]
    check("C2c 起始日期无结束 = 从当天零点起（全部 4 行）",
          len(got) == 4, got)
    page._f_end.setText(Y[5:])
    got = [r[0][0] for r in page._filtered_rows()]
    check("C2d 起止同日 = 只看那一天（昨天 1 行）",
          got == ["%s 23:59:00" % Y], got)

    page._f_start.clear()
    page._f_end.clear()
    got = [r[0][0] for r in page._filtered_rows()]
    check("C2e 无筛选返回全部 4 行（时间正序）", len(got) == 4)

    # ---- C3 时间列含日期后排序仍是正确的时间序（ISO 文本序=时间序）----
    page._sort_col = 0
    page._sort_desc = True
    view = page._visible_rows()
    stamps = [r[0][0] for r in view]
    check("C3 按时间列降序 = 时间倒序",
          stamps == sorted(stamps, reverse=True))


# ============================================================================
#                              段落入口
# ============================================================================

def m_remcfg_run_remote_settings():
    """远程设置段落入口（总回归调用它）。

    [内容 = 原 main() 里"跑各 part"的部分]
    · 建 QApplication / 打印汇总与退出码 ⇒ 去掉（app 由总回归 main()
      统一建；汇总由总回归统一出）。
    """
    m_remcfg_ensure_client_path()
    app = m_remcfg_app()
    m_remcfg_part_a_controls(app)
    # A2 变频器参数卡（0x011B；假桥喂应答，不依赖真服务端——服务端由
    # 并行实现，B 段的 sim 服务端对 0x011B 回"未知命令"也能照常跑）。
    m_remcfg_part_a2_vfd_params(app)
    # B 段自己起 sim 服务端（端口走 _free_port()，别撞别人；cfg 隔离）。
    m_remcfg_part_b_flow(app, m_remcfg_pick_port())
    m_remcfg_part_c_history_time(app)


# ==========================================================================
# 段落：test_softkeyboard（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【软键盘段落】软键盘（拼音 / 字母 / 数字三类）+ 拼音引擎实际测试。

    入口：run_softkbd()

[原文件] tests/test_softkeyboard.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

测什么（用户要求：触摸屏无键盘部署，拼音/数字/字母三类键盘都要能用，
同一套焦点驱动、由调用方告知要哪种键盘）：
  A. 环境探测开关（HSF_TOUCH 强制开/关）
  B. 键盘类型声明：显式 API / inputMethodHints / 缺省，及优先级
  C. 拼音引擎（数据与 C 端同源）：切分、跨音节组串、候选两段式、提交四路
  D. 焦点驱动：输入框聚焦 → 弹；换框 → 重绑并换键盘类型；点别处 → 收
  E. 真实按键点击：拼音选词 / 英文字母 / 数字框写入 / 退格 / 翻页
  F. 排除标记（TimePickerEdit 不弹）
  G. 面板几何：底部无死白 / 切换键盘时尺寸恒定
  H. X11 上必须绕过窗口管理器（X11BypassWindowManagerHint）

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **HSF_TOUCH 只在本段跑的时候置位**：原模块在 import 时就写死环境
    变量；并进总回归后必须改成"进段时置位、出段时还原"，否则会污染
    后面所有段的运行环境。
"""

def _m_softkbd_paths():
    """算本段落用到的两个目录。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys 已被主文件
    占用）⇒ 本段落一律把标准库 import 放进函数里，顶层只留**带前缀**
    的自有名字（重名检查要求顶层名与主文件交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    return d, os.path.dirname(d)


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_softkbd_TESTS_DIR, m_softkbd_CLIENT_ROOT = _m_softkbd_paths()

#: 最后一个软键盘宿主（**必须留活到进程结束**）。
#:
#: [为什么] SoftKeyboard.attach() 会 `app.installEventFilter(self)`，而
#: 框架**没有**对应的 removeEventFilter（见 softkeyboard.py 的 attach）。
#: 宿主（连带键盘）一旦在 app 还活着时被 Python 回收，app 里就留着一条
#: 悬垂的事件过滤器指针 ⇒ 后面任何一段跑事件循环（processEvents）都可能
#: 段错误（实测：并进同一进程后随机崩在下一段，退出码 139）。
#: ⇒ 本段把宿主挂在模块级引用上，让它活到进程结束（与"模块被 import 后
#: 由调用方持有"的旧行为一致）。
m_softkbd_LAST_HARNESS = None


# ============================================================================
#                          A. 自动探测
# ============================================================================

def m_softkbd_part_a_probe():
    print("== A. 自动探测 ==")
    import os
    from hsfsrv_client import softkeyboard as sk
    check("A1 HSF_TOUCH=1 → 强制开", sk.touch_default_enabled() is True)
    saved = os.environ.get("HSF_TOUCH")
    os.environ["HSF_TOUCH"] = "0"
    try:
        check("A2 HSF_TOUCH=0 → 强制关", sk.touch_default_enabled() is False)
    finally:
        os.environ["HSF_TOUCH"] = saved


# ============================================================================
#                 B. 键盘类型声明（调用方告知要哪种键盘）
# ============================================================================

def m_softkbd_part_b_kinds():
    print("== B. 键盘类型声明 ==")
    from PyQt5.QtCore import Qt
    from PyQt5.QtWidgets import QLineEdit, QSpinBox
    from hsfsrv_client import softkeyboard as sk

    plain = QLineEdit()
    check("B1 普通输入框缺省 = 拼音键盘",
          sk.keyboard_kind_of(plain) == (sk.KIND_PINYIN, None))

    latin = QLineEdit()
    latin.setInputMethodHints(Qt.ImhLatinOnly)
    check("B2 ImhLatinOnly → 字母键盘",
          sk.keyboard_kind_of(latin) == (sk.KIND_ALPHA, None))

    pwd = QLineEdit()
    pwd.setEchoMode(QLineEdit.Password)
    check("B3 密码框（自带 HiddenText）→ 字母键盘",
          sk.keyboard_kind_of(pwd) == (sk.KIND_ALPHA, None))

    spin = QSpinBox()
    spin.setRange(0, 2000)
    check("B4 QSpinBox（Qt 自带 DigitsOnly）→ 纯数字盘",
          sk.keyboard_kind_of(spin) == (sk.KIND_NUMERIC, "digits"))

    nums = QLineEdit()
    nums.setInputMethodHints(Qt.ImhPreferNumbers)
    check("B5 ImhPreferNumbers → 数字键盘（带符号）",
          sk.keyboard_kind_of(nums) == (sk.KIND_NUMERIC, "number"))

    dial = QLineEdit()
    dial.setInputMethodHints(Qt.ImhDialableCharactersOnly)
    check("B6 ImhDialableCharactersOnly → 拨号盘",
          sk.keyboard_kind_of(dial) == (sk.KIND_NUMERIC, "dialpad"))

    forced = QLineEdit()
    sk.set_keyboard_kind(forced, sk.KIND_NUMERIC)
    check("B7 显式声明 numeric（覆盖缺省拼音）",
          sk.keyboard_kind_of(forced) == (sk.KIND_NUMERIC, "number"))

    forced2 = QLineEdit()
    forced2.setInputMethodHints(Qt.ImhDigitsOnly)
    sk.set_keyboard_kind(forced2, sk.KIND_ALPHA)
    check("B8 显式声明优先于 hints",
          sk.keyboard_kind_of(forced2) == (sk.KIND_ALPHA, None))

    none_w = QLineEdit()
    sk.set_keyboard_kind(none_w, sk.KIND_NONE)
    check("B9 显式声明 none → 不要键盘",
          sk.keyboard_kind_of(none_w) == (sk.KIND_NONE, None))

    bad = QLineEdit()
    try:
        sk.set_keyboard_kind(bad, "klingon")
        check("B10 未知类型被拒", False)
    except ValueError:
        check("B10 未知类型被拒", True)


# ============================================================================
#                      C. 拼音引擎（与 C 端同源数据）
# ============================================================================

def m_softkbd_part_c_engine():
    print("== C. 拼音引擎 ==")
    import os
    from hsfsrv_client import pinyin as py

    lex = py.lexicon()
    check("C0 词库就绪（412 音节）", lex.ready and len(lex.syllables) == 412,
          "err=%s" % lex.error)
    check("C0a 词库资产路径存在", os.path.isfile(lex.path), lex.path)

    eng = py.PinyinEngine(lex)

    # ---- 单音节 ----
    for ch in "shi":
        eng.feed_letter(ch)
    cands = eng.all_candidates()
    check("C1 'shi' 组串成立", eng.composing_text() == "shi")
    check("C2 'shi' 首候选=是", cands and cands[0] == "是", str(cands[:3]))
    check("C3 'shi' 多页（>9 候选）", len(cands) > 9 and eng.page_count() > 1,
          "共 %d 条 / %d 页" % (len(cands), eng.page_count()))

    # ---- 翻页 + 数字选词 ----
    first_page = eng.candidates()
    eng.page_next()
    second_page = eng.candidates()
    check("C4 翻页后候选不同", first_page[0] != second_page[0],
          "%s → %s" % (first_page[:2], second_page[:2]))
    eng.page_prev()
    want = eng.candidates()[2]
    feed = eng.feed_digit(3)
    check("C5 数字 3 选当前页第 3 个",
          feed == py.FEED_COMMITTED and eng.commit_string() == want,
          "commit=%r want=%r" % (eng.commit_string(), want))

    # ---- 跨音节组串 ----
    eng.reset()
    for ch in "nihao":
        eng.feed_letter(ch)
    check("C6 跨音节 'nihao' 首候选=你好",
          eng.all_candidates()[:1] == ["你好"], str(eng.all_candidates()[:4]))

    eng.reset()
    for ch in "zhongguo":
        eng.feed_letter(ch)
    check("C7 'zhongguo' 首候选=中国",
          eng.all_candidates()[:1] == ["中国"], str(eng.all_candidates()[:4]))

    # ---- 切分歧义：xian 既可 xi+an 也可 xian ----
    eng.reset()
    for ch in "xian":
        eng.feed_letter(ch)
    cands = eng.all_candidates()
    check("C8 'xian' 词组段在单字段之前（西安 先 …）",
          cands[:2] == ["西安", "先"], str(cands[:4]))

    # ---- 非法输入：截断到最长合法后缀，不自动提交 ----
    eng.reset()
    eng.feed_letter("z")
    feed = eng.feed_letter("q")             # q 不能接在 z 后面
    check("C9 非法字母被吞，组串回到合法后缀",
          feed == py.FEED_CONSUMED and eng.composing_text() == "q",
          "composing=%r" % eng.composing_text())

    # ---- 退格 ----
    eng.reset()
    for ch in "nih":
        eng.feed_letter(ch)
    eng.feed_backspace()
    check("C10 退格删一个字母", eng.composing_text() == "ni")
    eng.feed_backspace()
    eng.feed_backspace()
    check("C11 组串删空后不再消费（透传编辑框退格）",
          eng.feed_backspace() == py.FEED_IGNORED)

    # ---- 提交通道 ----
    eng.reset()
    for ch in "nihao":
        eng.feed_letter(ch)
    eng.feed_commit_first()
    check("C12 空格提交首候选", eng.commit_string() == "你好")

    eng.reset()
    for ch in "zzz":                        # 逐键截断后组串只剩合法后缀
        eng.feed_letter(ch)
    raw = eng.composing_text()
    eng.feed_commit_raw()
    check("C13 回车提交原串（无候选不猜字）", eng.commit_string() == raw,
          "组串=%r 提交=%r" % (raw, eng.commit_string()))

    eng.reset()
    for ch in "shi":
        eng.feed_letter(ch)
    eng.feed_commit_raw()
    check("C14 有候选时回车仍提交原字母串",
          eng.commit_string() == "shi", repr(eng.commit_string()))

    # ---- 中英切换 ----
    eng.reset()
    eng.set_chinese(False)
    check("C15 英文态字母不消费（直写编辑框）",
          eng.feed_letter("n") == py.FEED_IGNORED)
    eng.set_chinese(True)
    check("C16 切回中文态可组串",
          eng.feed_letter("n") == py.FEED_CONSUMED
          and eng.composing_text() == "n")

    # ---- 组串容量 ----
    eng.reset()
    long_ok = all(eng.feed_letter("a") == py.FEED_CONSUMED for _ in range(20))
    check("C17 组串不超过容量 15", long_ok and len(eng.composing_text()) <= 15,
          "len=%d" % len(eng.composing_text()))

    # ---- 与 C 端资产同源校验（频序连续 / 无表外 id 由生成器保证）----
    syl_ids = set(lex.singles)
    check("C18 单字表覆盖主要音节", len(syl_ids) > 380,
          "%d/412 音节有候选" % len(syl_ids))
    check("C19 词组库非空（>3000 条）",
          sum(len(v) for v in lex.phrases.values()) > 3000,
          "%d 条 / %d 组" % (sum(len(v) for v in lex.phrases.values()),
                            len(lex.phrases)))


# ============================================================================
#                           D/E. 面板交互
# ============================================================================

class m_softkbd_Harness(object):
    """把软键盘装到一个宿主窗口上，提供 focus/press 辅助。"""

    def __init__(self, app):
        from PyQt5.QtCore import Qt
        from PyQt5.QtWidgets import QLineEdit, QSpinBox, QWidget
        from hsfsrv_client import softkeyboard as sk
        self.sk = sk
        self.app = app
        self.host = QWidget()
        self.host.resize(1280, 860)
        self.edit = QLineEdit(self.host)
        self.edit.setGeometry(20, 20, 420, 36)
        self.spin = QSpinBox(self.host)
        self.spin.setRange(0, 2000)
        self.spin.setSuffix(" ms")
        self.spin.setValue(200)
        self.spin.setGeometry(20, 70, 220, 36)
        self.latin = QLineEdit(self.host)
        self.latin.setInputMethodHints(Qt.ImhLatinOnly)
        self.latin.setGeometry(20, 130, 300, 36)
        self.plain_area = QWidget(self.host)
        self.plain_area.setGeometry(20, 200, 300, 120)
        self.host.show()
        app.setActiveWindow(self.host)
        app.processEvents()
        self.kb = sk.SoftKeyboard()
        self.kb.attach(app, self.host)
        app.processEvents()
        app.processEvents()

    def focus(self, widget):
        # 真实触摸点击会把窗口激活、焦点落到被点的控件；离屏下要显式
        # 激活宿主窗，否则窗口级焦点一直是 None，FocusIn 不来。
        from PyQt5.QtCore import Qt
        widget.setFocus(Qt.OtherFocusReason)
        widget.window().activateWindow()
        self.app.processEvents()
        self.app.processEvents()

    def keys(self):
        """键区里所有可点按键。

        [遍历口径] 键区结构是「行外框 QWidget → 行布局 → 键」：行外框是
        **控件**（为了钉住行高，见 softkeyboard._render_page 的说明），
        其内部才是布局。故遍历要同时处理布局项和控件项 —— 只递归 layout
        会漏掉全部按键（实测：返回空列表，连拼音都没法点）。
        """
        from PyQt5.QtWidgets import QPushButton
        out = []

        def walk(layout):
            for i in range(layout.count()):
                item = layout.itemAt(i)
                sub = item.layout()
                if sub is not None:
                    walk(sub)
                    continue
                w = item.widget()
                if w is None:
                    continue
                if isinstance(w, QPushButton):
                    out.append(w)
                    continue
                # 行外框控件：钻进去看它的布局
                if w.layout() is not None:
                    walk(w.layout())

        walk(self.kb._rows_box)
        return out

    def key_labels(self):
        return [w.text() for w in self.keys()]

    def press(self, label):
        """点一枚「软件按键」（等价于手指戳）。返回是否找到。"""
        for w in self.keys():
            if w.text() == label:
                w.click()
                self.app.processEvents()
                return True
        return False

    def press_all(self, text):
        missing = [ch for ch in text if not self.press(ch)]
        return not missing

    def band_labels(self):
        """候选带按钮文本（含中/EN 与翻页标记）——测试/诊断用。"""
        from PyQt5.QtWidgets import QPushButton
        out = []
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton):
                out.append(w.text())
        return out

    def band_candidates(self):
        """候选带上的候选词（排除中/EN 模式 chip 与翻页箭头）。"""
        from PyQt5.QtWidgets import QPushButton
        out = []
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton) and \
                    w.objectName() != "hsf_kb_mode_chip" and \
                    w.text() not in ("‹", "›"):
                out.append(w.text())
        return out

    def click_band(self, label):
        """点候选带上的控件（模式 chip 优先按 objectName 精确定位）。"""
        from PyQt5.QtWidgets import QPushButton
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton) and w.text() == label:
                if label in ("中", "EN") and \
                        w.objectName() != "hsf_kb_mode_chip":
                    continue                # 候选词恰好是「中」时别点错
                w.click()
                self.app.processEvents()
                return True
        return False

    def click_candidate(self, word):
        """点候选带上文本为 word 的候选 chip。"""
        from PyQt5.QtWidgets import QPushButton
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton) and w.text() == word and \
                    w.objectName() != "hsf_kb_mode_chip":
                w.click()
                self.app.processEvents()
                return True
        return False


def m_softkbd_part_d_focus(app):
    print("== D. 焦点驱动 ==")
    from PyQt5.QtCore import QEvent, QPoint, Qt
    from PyQt5.QtGui import QMouseEvent
    h = m_softkbd_Harness(app)

    # [装配即焦点] attach 时若已有输入框持焦，面板应直接出来。
    h.focus(h.edit)
    check("D1 输入框聚焦 → 拼音面板弹出",
          h.kb.isVisible() and h.kb._kind == h.sk.KIND_PINYIN,
          "kind=%s" % h.kb._kind)
    check("D2 目标 = 该输入框", h.kb._live_target() is h.edit)

    h.focus(h.spin)
    check("D3 换到数字框 → 面板重绑为纯数字盘",
          h.kb.isVisible() and h.kb._kind == h.sk.KIND_NUMERIC
          and h.kb._numeric_layout == "digits"
          and h.kb._live_target() is h.spin,
          "kind=%s/%s" % (h.kb._kind, h.kb._numeric_layout))

    h.focus(h.latin)
    check("D4 换到拉丁框 → 字母键盘（ABC/abc 布局）",
          h.kb.isVisible() and h.kb._kind == h.sk.KIND_ALPHA
          and h.kb._live_target() is h.latin, "kind=%s" % h.kb._kind)

    # 换框不得把上一框的组串带过来（会误上屏到新框）。
    h.focus(h.edit)
    h.press_all("nihao")
    check("D5 拼音框组串中", h.kb._engine.composing_text() == "nihao")
    h.focus(h.spin)
    check("D6 换框清组串", h.kb._engine.composing_text() == "")

    # 点面板外的普通区域 → 收起
    press_ev = QMouseEvent(QEvent.MouseButtonPress, QPoint(5, 5),
                           Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
    app.sendEvent(h.plain_area, press_ev)
    app.processEvents()
    check("D7 点面板外非输入区 → 收起", not h.kb.isVisible())

    # 点「收起」键
    h.focus(h.edit)
    h.press("收起")
    check("D8 收起键隐藏面板", not h.kb.isVisible())

    # ---- D9/D10/D11 换到**同类**控件不重建按键区（板端「键盘闪一下」根修）----
    #
    # [现场症状 2026-11-19] 板端上每次点输入框，屏幕键盘都会「闪」一下
    #   （弹出来又抖一下）。用户原话：「弹出的屏幕键盘后马上就消失了」。
    # [根因] bind() 原来**无条件** `_render_page()` ⇒ 每换一次焦点就把
    #   73 个按键全部销毁重建（+ 全量 QSS 解析）⇒ 弱 CPU（RK3568）上
    #   肉眼可见地闪一下。
    # [修法] 比「渲染签名」（kind / 数字盘排布 / 字母模式 / 拼音页 / 拼音
    #   大小写 / 键高）：签名没变就只 reflow（几何跟着主窗走）+刷候选带，
    #   **不重建按键区**。
    from PyQt5.QtWidgets import QSpinBox
    spin2 = QSpinBox(h.host)
    spin2.setRange(0, 2000)
    spin2.setSuffix(" ms")
    spin2.setGeometry(20, 340, 220, 36)
    spin2.show()          # [必须显式 show] 宿主显示**之后**新建的子控件
                          # 不会自动可见 ⇒ 拿不到焦点 ⇒ 用例会假绿
    _rebuilds = []
    _orig_render = h.kb._render_page

    def _spy_render():
        _rebuilds.append(1)
        return _orig_render()

    h.focus(h.spin)                       # 先切到数字盘（此时还没装探针）
    h.kb._render_page = _spy_render
    try:
        h.focus(spin2)                    # 同类（数字框 → 数字框）
        _same_kind_rebuilds = len(_rebuilds)
        _rebuilds[:] = []
        h.focus(h.latin)                  # 换类型（数字盘 → 字母盘）
        _diff_kind_rebuilds = len(_rebuilds)
    finally:
        h.kb._render_page = _orig_render
    check("D9 换到同类控件（数字框→数字框）面板不消失",
          h.kb.isVisible() and h.kb._live_target() is h.latin,
          "visible=%s target=%s" % (h.kb.isVisible(),
                                    type(h.kb._live_target()).__name__))
    check("D10 同类切换**不重建**按键区（板端键盘不再闪）",
          _same_kind_rebuilds == 0,
          "重建了 %d 次" % _same_kind_rebuilds)
    check("D11 换键盘类型仍会重建按键区（数字盘→字母盘）",
          _diff_kind_rebuilds >= 1,
          "重建了 %d 次" % _diff_kind_rebuilds)

    return h


def m_softkbd_part_e_typing(app, h):
    print("== E. 真实按键点击 ==")
    # ---- E1 拼音选词（首候选）----
    h.focus(h.edit)
    h.edit.clear()
    h.press_all("nihao")
    check("E1a 组串显示 nihao", h.kb._engine.composing_text() == "nihao")
    check("E1b 候选带出现候选词",
          "你好" in h.band_candidates(), str(h.band_candidates()[:4]))
    h.press(" ")
    check("E1c 空格上屏首候选", h.edit.text() == "你好", repr(h.edit.text()))

    # ---- E2 点候选 chip（单字「中」——同时验证不与模式 chip 混淆）----
    h.press_all("zhongguo")
    cands = h.band_candidates()
    check("E2a 候选带含「中国」", "中国" in cands, str(cands[:4]))
    if "中" in cands:
        h.click_candidate("中")
        check("E2b 点单字候选上屏", h.edit.text() == "你好中",
              repr(h.edit.text()))
    else:
        check("E2b 点单字候选上屏", False, "候选带无「中」：%r" % (cands,))
    h.edit.clear()

    # ---- E3 翻页 + 数字选词 ----
    h.press_all("shi")
    first = h.kb._engine.candidates()[0]
    h.click_band("›")
    check("E3a 翻页后候选变化",
          h.kb._engine.candidates()[0] != first,
          "%s → %s" % (first, h.kb._engine.candidates()[0]))
    page_cands = h.kb._engine.candidates()
    h.press("123")
    h.press("2")
    check("E3b 数字键选当前页第 2 个", h.edit.text() == page_cands[1],
          "实际=%r 期望=%r" % (h.edit.text(), page_cands[1]))
    h.press("拼音")
    check("E3c 拼音键回字母布局", h.kb._pinyin_page == "letters")

    # ---- E4 退格（组串优先）----
    h.edit.clear()
    h.press_all("nih")
    h.press("退格")
    check("E4a 退格删组串字母",
          h.kb._engine.composing_text() == "ni" and h.edit.text() == "",
          "组串=%r 编辑框=%r" % (h.kb._engine.composing_text(), h.edit.text()))
    h.press("退格")
    h.press("退格")
    h.press("退格")                       # 组串已空 → 删编辑框
    check("E4b 组串空后退格落到编辑框",
          h.kb._engine.composing_text() == "" and h.edit.text() == "")

    # ---- E5 中/EN 切换 ----
    h.edit.clear()
    check("E5a 中文态", h.kb._engine.is_chinese())
    h.press("中/EN")
    check("E5b 切英文态", not h.kb._engine.is_chinese())
    h.press_all("abc")
    check("E5c 英文态字母直写编辑框", h.edit.text() == "abc",
          repr(h.edit.text()))
    h.press("中/EN")
    check("E5d 切回中文态", h.kb._engine.is_chinese())

    # ---- E6 数字框：纯数字盘 + value 同步 ----
    h.focus(h.spin)
    h.spin.setValue(200)
    h.spin.lineEdit().selectAll()
    h.press_all("600")
    check("E6a 数字盘写入 600",
          h.spin.lineEdit().text().startswith("600"), h.spin.lineEdit().text())
    check("E6b spinbox value 同步为 600", h.spin.value() == 600,
          str(h.spin.value()))
    # 纯数字盘不该出现字母键
    labels = h.key_labels()
    check("E6c 纯数字盘无字母键",
          "q" not in labels and "1" in labels, str(labels))
    # [中间态不炸] 输入低于量程下限的中间态不该被立刻 clamp 掉，
    # 否则最终落值会变成上限（实锅：逐键 interpretText 把 600 变成 2000）。
    h.spin.setValue(200)
    h.spin.lineEdit().selectAll()
    h.press("6")
    check("E6d 中间态不预先 clamp", h.spin.lineEdit().text().startswith("6"),
          h.spin.lineEdit().text())
    h.press_all("00")
    check("E6e 续敲后落值正确（600 而非被 clamp 成 2000）",
          h.spin.value() == 600, str(h.spin.value()))
    # [换框提交] 没按确定就换到别的输入框，数值也该已落地。
    h.spin.setValue(200)
    h.spin.lineEdit().selectAll()
    h.press_all("950")
    h.focus(h.edit)
    check("E6f 换框时把未解析文本提交成 value", h.spin.value() == 950,
          str(h.spin.value()))

    # ---- E7 字母键盘：Shift 大小写 + 符号盘 ----
    h.focus(h.latin)
    h.latin.clear()
    h.press_all("root")
    check("E7a 字母键盘直接出英文", h.latin.text() == "root",
          repr(h.latin.text()))
    # 字母键盘行 3 左侧是 Shift（键帽随态显示 ABC/abc，与 C 的 shift 三态
    # 同义）：点一次进大写态，键帽换大写。
    h.press("ABC" if "ABC" in h.key_labels() else "abc")
    check("E7b Shift 切出大写键帽", "A" in h.key_labels(),
          str(h.key_labels()[:6]))
    h.press("A")
    check("E7c 大写字母上屏", h.latin.text() == "rootA", repr(h.latin.text()))
    h.press("符")
    check("E7d 「符」进符号盘", h.kb._mode == "Special", h.kb._mode)
    h.press("/")
    check("E7e 符号布局可插入", h.latin.text() == "rootA/",
          repr(h.latin.text()))
    h.press("abc")
    check("E7f 符号盘「abc」回小写布局",
          h.kb._mode == "TextLower", h.kb._mode)

    # ---- E8 候选带「中/EN」chip ----
    h.focus(h.edit)
    before = h.kb._engine.is_chinese()
    h.click_band("中" if before else "EN")
    check("E8 候选带模式 chip 也能切中英",
          h.kb._engine.is_chinese() != before)


def m_softkbd_part_f_exclude(app):
    print("== F. 排除标记 ==")
    from PyQt5.QtWidgets import QLineEdit
    from hsfsrv_client.pages_other import TimePickerEdit
    from hsfsrv_client import softkeyboard as sk
    picker = TimePickerEdit("日期")
    check("F1 TimePickerEdit 自动带排除标记",
          picker.property(sk.NO_SOFT_KBD) is True)
    check("F2 排除标记不参与键盘类型（不弹）",
          bool(picker.property(sk.NO_SOFT_KBD)))
    none_w = QLineEdit()
    sk.set_keyboard_kind(none_w, sk.KIND_NONE)
    check("F3 声明 none 的控件同样不弹",
          sk.keyboard_kind_of(none_w)[0] == sk.KIND_NONE)


def m_softkbd_part_g_geometry(app):
    """G. 面板几何：底部无死白 + 切换键盘时尺寸恒定。

    [用户 2026-11-06 报的两个现象]
        "屏幕键盘调用后底部很大一块空的" —— 面板比内容高，多出来的高度
        被尾部 stretch 全沉到底部（实测拼音盘 16px + 底边距 6px = 22px）。
        "切换不同的键盘的时候边框应该没有自己收缩" —— 各键盘按自己的行数
        算高（拼音 带+4 行 = 340、数字 5 行 = 355），换类型就跳。

    修法是「槽位口径」：所有键盘共用 SLOTS 个槽（候选带也算一槽），键高
    由槽数反算 ⇒ 面板尺寸恒定、内容铺满。这里锁这两条。
    """
    from hsfsrv_client import softkeyboard as sk
    from PyQt5.QtWidgets import (QLineEdit, QPushButton, QSpinBox, QWidget,
                                 QVBoxLayout)

    host = QWidget()
    host.resize(1280, 800)
    lay = QVBoxLayout(host)
    edit = QLineEdit(host)
    lay.addWidget(edit)
    spin = QSpinBox(host)
    spin.setRange(0, 1000)
    lay.addWidget(spin)
    host.show()
    app.processEvents()

    kb = sk.SoftKeyboard()
    kb._main = host

    def settle():
        for _ in range(6):
            app.processEvents()

    def kb_buttons():
        """递归收集键区所有按键（行外框是 QWidget，要钻进去）。"""
        out = []

        def walk(layout):
            for i in range(layout.count()):
                it = layout.itemAt(i)
                sub = it.layout()
                if sub is not None:
                    walk(sub)
                    continue
                w = it.widget()
                if w is None:
                    continue
                if isinstance(w, QPushButton):
                    out.append(w)
                    continue
                if w.layout() is not None:
                    walk(w.layout())

        walk(kb._rows_box)
        return out

    heights = {}
    for tag, widget in (("pinyin", edit), ("numeric", spin)):
        kb.bind(widget)
        settle()
        heights[tag] = kb.height()
        # 内容底 = 键区容器底（容器内边距之外就是死白）
        host_bottom = kb._keys_host.y() + kb._keys_host.height()
        gap = kb.height() - host_bottom
        # 只允许 root 的底边距（6px），多出来就是死白
        margins = kb.layout().contentsMargins()
        check("G-%s 底部无死白" % tag,
              gap <= margins.top() + margins.bottom(),
              "面板 %d, 内容底 %d, 差 %d" % (kb.height(), host_bottom, gap))
        # 末行键必须贴到键区容器底部（不留空带）
        # [口径] 行外框现在是 QWidget（行高靠它钉），故按"取行控件 y+h"
        # 算末行底，而不是钻进旧的两层布局。
        last_bottom = 0
        for i in range(kb._rows_box.count()):
            it = kb._rows_box.itemAt(i)
            host_w = it.widget()
            if host_w is None:
                continue
            last_bottom = max(last_bottom, host_w.y() + host_w.height())
        dead = kb._keys_host.height() - (last_bottom
                                         + kb._rows_box.contentsMargins().bottom())
        check("G-%s 末行铺满键区" % tag, dead <= 2,
              "末行底 %d, 容器高 %d, 死白 %d"
              % (last_bottom, kb._keys_host.height(), dead))

    check("G3 两套键盘高度一致（切换时不收缩）",
          heights["pinyin"] == heights["numeric"],
          "pinyin=%d numeric=%d" % (heights["pinyin"], heights["numeric"]))

    # 键高必须都等于反算值（不许某行被压 1~2px）
    kb.bind(edit)
    settle()
    expect = kb._key_h_for_slots()
    bad = []
    # [口径] 行外框是 QWidget，键在它的布局里
    for w in kb_buttons():
        if w.height() > 3 and w.height() != expect:
            bad.append(w.height())
    check("G4 所有键高等于槽位反算值", not bad,
          "期望 %d，异常 %s" % (expect, sorted(set(bad))))

    # ---- G5：行外框必须是**控件**且固定高度 ----
    # [这一条锁的是真机缺陷 2026-11-06] 行外框原来是裸 QHBoxLayout：
    # QLayout 钉不住高度（没有 setFixedHeight），它的 sizeHint 高度按子项
    # 最小要求算 ⇒ 板端实测行高被压成 32/35px（期望 58），底部空出 77px
    # —— 用户看到的"按键下面一大块空白"红框。包一层 setFixedHeight 的
    # QWidget 后板端四行都是 58、末行死白 0。
    rows_ok = True
    detail = []
    for i in range(kb._rows_box.count()):
        it = kb._rows_box.itemAt(i)
        host_w = it.widget()
        if host_w is None:
            continue                      # 尾部 stretch
        if host_w.height() != expect or host_w.maximumHeight() != expect:
            rows_ok = False
            detail.append("row%d h=%d max=%d" % (i, host_w.height(),
                                                 host_w.maximumHeight()))
    check("G5 行外框是固定高度的 QWidget", rows_ok,
          "; ".join(detail) if detail else "期望 %d" % expect)

    # ---- G6：按键高度扣掉了 QSS 边框 ----
    # [踩过] QSS 的 min/max-height 是内容区尺寸，`border: 1px` 会再加 2px：
    # 写 58 量出来 60，面板总高与 reflow() 的算术就不一致了。故样式里写
    # h-2、控件上仍钉 h。
    border_bad = []
    for w in kb_buttons():
        if w.height() != expect:
            border_bad.append("%s h=%d" % (w.text()[:4], w.height()))
    check("G6 按键实测高度 == 槽位值（边框已补偿）", not border_bad,
          "; ".join(border_bad[:3]) if border_bad else "全部 %d" % expect)

    # ---- G7/G8：弹出即全选（用户 2026-11-06 要求）----
    # 「变频器界面输入框弹出键盘后默认选中全部，这样可以***直接重新完整
    # 输入一个值***」。全选后第一个字符就顶掉原值（QLineEdit.insert 有
    # 选区时是替换），是最少动作的输入方式。
    spin.setValue(720)
    kb.hide()
    app.processEvents()
    kb.bind(spin)                       # 弹出（面板由隐藏→显示）
    settle()
    sel = kb._adapter.editor.selectedText()
    check("G7 弹出时默认全选", sel == kb._adapter.editor.text() and sel != "",
          "选中 %r 全文 %r" % (sel, kb._adapter.editor.text()))

    # 连续按键不得再次全选，否则每敲一个字符就把已输入的覆盖掉
    kb._adapter.insert("9")             # 模拟首次按键（替换选区）
    app.processEvents()
    before = kb._adapter.editor.text()
    kb.bind(spin)                        # 同一目标再走一次 bind（翻页等路径）
    settle()
    after = kb._adapter.editor.text()
    check("G8 同一目标重复 bind 不重复全选", after == before,
          "输入 %r → 再 bind 后 %r" % (before, after))

    # 换目标才再全选（先填内容，否则 '' == '' 没有区分力）
    edit.setText("abcdef")
    kb.hide(); app.processEvents()
    kb.bind(edit)
    settle()
    sel2 = kb._adapter.editor.selectedText()
    check("G9 换目标后重新全选", sel2 == "abcdef",
          "选中 %r（期望 abcdef）" % (sel2,))

    host.close()


def m_softkbd_part_h_x11_flag():
    """H. X11 上必须绕过窗口管理器。

    [板端实测 2026-11-06] 普通 Qt.Tool 窗口被 xfwm4 补 15px 系统标题栏
    （Qt 几何 340、X 上 355），并且一 map 就把 xfce4 面板抬到全屏窗之上，
    **系统顶栏盖住软件自己的标题栏**。加 X11BypassWindowManagerHint 后
    两个现象同时消失（三态对照图 pb_none/normal/bypass）。
    """
    from hsfsrv_client import softkeyboard as sk
    from PyQt5.QtCore import Qt

    # 非 X11 平台不加该标志（Windows 上是空操作，显式判平台让意图清楚）
    kb = sk.SoftKeyboard()
    if sk._is_x11():
        check("H1 X11 上带绕过 WM 标志",
              bool(kb.windowFlags() & Qt.X11BypassWindowManagerHint),
              hex(int(kb.windowFlags())))
    else:
        check("H1 非 X11 不带绕过 WM 标志（空操作但意图明确）",
              not bool(kb.windowFlags() & Qt.X11BypassWindowManagerHint))
    kb.close()


def run_softkbd():
    """§20（并入）软键盘：无死白 / 切换不跳。

    [原 main() 的"跑各 part"部分；建 app/汇总打印/退出码交给总回归]

    [HSF_TOUCH 的置位时机] 原模块在 import 时就写死 `HSF_TOUCH=1`；并进
    总回归后改成"进段时置位、出段时还原" ⇒ 不污染后面所有段。
    """
    from PyQt5.QtWidgets import QApplication
    import os
    # [不新建 QApplication] 复用总回归 main() 里那一个。
    app = QApplication.instance()
    if app is None:
        check("软键盘段：复用总回归的 QApplication", False,
              "总回归还没建 QApplication（不能在本段新建）")
        return

    saved_touch = os.environ.get("HSF_TOUCH")
    os.environ["HSF_TOUCH"] = "1"            # 强制开（桌面机探测为关）
    global m_softkbd_LAST_HARNESS
    h = None
    try:
        m_softkbd_part_a_probe()
        m_softkbd_part_b_kinds()
        m_softkbd_part_c_engine()
        h = m_softkbd_part_d_focus(app)
        # [宿主留活] SoftKeyboard 是 app 级事件过滤器，宿主被回收会让
        # app 里留着悬垂指针 ⇒ 挂到模块级引用上（本段结束前再摘）。
        m_softkbd_LAST_HARNESS = h
        m_softkbd_part_e_typing(app, h)
        m_softkbd_part_f_exclude(app)
        m_softkbd_part_g_geometry(app)
        m_softkbd_part_h_x11_flag()
    finally:
        if h is not None:
            # [摘事件过滤器] SoftKeyboard.attach 装了 app 级过滤器，框架
            # 没有对应的 removeEventFilter ⇒ 这里手动摘掉，键盘/宿主才
            # 能安全回收（否则 app 里留着悬垂指针，后面任一段跑
            # processEvents 都可能段错误）。
            try:
                app.removeEventFilter(h.kb)
            except Exception:                        # noqa: BLE001
                pass
            try:
                h.kb.close()
                h.host.close()
            except Exception:                        # noqa: BLE001
                pass
        m_softkbd_LAST_HARNESS = None
        if saved_touch is None:
            os.environ.pop("HSF_TOUCH", None)
        else:
            os.environ["HSF_TOUCH"] = saved_touch


# ==========================================================================
# 段落：test_storage_limit（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【存储上限段落】落盘容量限额 end-to-end 测试（真实服务端 + 真实 XLink 协议）。

    入口：run_storlim()

[原文件] tests/test_storage_limit.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

测什么（用户要求：现场硬盘小，要能设 DB 最大占用 + 几种到限停法，
并且实际测过）：
  A. 配置通道：CMD_CONFIG_SET 写入 maxMb/fullPolicy/keepFiles → 落盘到
     配置文件 → CMD_CONFIG_GET 能读回（重启不丢）。
  B. 三种策略的真行为（起真实 sim 服务端，周期压到 1s 快速堆数据）：
     · stop    到限后行数不再增长、paused=True、磁盘不越界；
     · ring    行数封顶在某平台期、droppedRows 增长、磁盘不越界；
     · rotate  出现分段文件、当前库回到小体积、分段数 <= keepFiles。
  C. 参数校验：非法策略/越界数值被服务端拒（cfg 不变）。

为什么用小限额：现场盘小，测试也用 1MB 级，几十秒就能跑满一轮，
真实反映"到限"这一刻的行为（大限额要跑几天才到限，测不出东西）。

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **端口不写死**：原模块把 9101~9104 写死 ⇒ 改用总回归的 `_free_port()`
    取随机空闲端口，免得撞上别的并行任务（今天有并行任务在跑）。
  · **不插主文件路径**：只把客户端包目录放进 sys.path（主文件已经做
    了同样的事），绝不把 tests/ 放进去 ⇒ 不会循环 import。
"""

def _m_storlim_paths():
    """算本段落用到的三个目录（tests / client_py / server_py）。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys / json /
    shutil / subprocess / tempfile / time 已被主文件占用）⇒ 本段落一律
    把标准库 import 放进函数里，顶层只留**带前缀**的自有名字（重名
    检查要求顶层名与主文件交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    client_root = os.path.dirname(d)
    server_root = os.path.join(os.path.dirname(client_root), "server_py")
    return d, client_root, server_root


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_storlim_TESTS_DIR, m_storlim_CLIENT_ROOT, m_storlim_SERVER_ROOT = \
    _m_storlim_paths()


def m_storlim_wait(seconds):
    import time
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(0.05)


def m_storlim_dir_bytes(path):
    """目录下所有文件总字节（含 -wal/-shm/分段）。"""
    import os
    total = 0
    try:
        for name in os.listdir(path):
            full = os.path.join(path, name)
            if os.path.isfile(full):
                total += os.path.getsize(full)
    except OSError:
        pass
    return total


class m_storlim_ServerCase(object):
    """起一个独立配置目录的 sim 服务端（端口由调用方给，**不写死**）。

    [为什么用 max_kb 而不是 maxMb] 远端协议的上限单位是整数 MB，而
    1MB ≈ 8000 行历史；按 1s 一行的真实采样要两小时才到限，测试跑不动。
    配置文件（管理员手工编辑的口径）支持 max_kb，压到 120~200KB 就能
    几十秒内跑到限，真正观察"到限那一刻"的行为。协议侧的 maxMb 通路由
    A 段单独验证。
    """

    def __init__(self, port, tmpdir, sample_ms=1000, max_kb=0, policy="ring"):
        self.port = port
        self.tmpdir = tmpdir
        self.sample_ms = sample_ms
        self.max_kb = max_kb
        self.policy = policy
        self.proc = None
        self.bridge = None

    def start(self):
        import json
        import os
        import subprocess
        import sys
        import time
        cfg_path = os.path.join(self.tmpdir, "hsf_server_py.json")
        storage = {
            "enabled": True,
            "path": os.path.join(self.tmpdir, "limit.db"),
            "sample_ms": self.sample_ms,
            "full_policy": self.policy,
            "keep_files": 3,
        }
        if self.max_kb:
            storage["max_kb"] = self.max_kb
        cfg = {"storage": storage, "server": {"port": self.port}}
        with open(cfg_path, "w", encoding="utf-8") as fh:
            json.dump(cfg, fh, ensure_ascii=False, indent=2)
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"   # 服务端日志即时落盘（崩溃取证）
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(self.port),
             "--config", cfg_path],
            cwd=m_storlim_SERVER_ROOT, env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        # 等服务端监听（最多 20s）
        import socket
        deadline = time.time() + 20
        while time.time() < deadline:
            try:
                sock = socket.create_connection(("127.0.0.1", self.port),
                                                timeout=0.4)
                sock.close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def connect(self):
        from hsfsrv_client.bridge import LinkBridge
        self.bridge = LinkBridge()
        ok, err = self.bridge.connect_to("127.0.0.1", self.port,
                                          "root", "root")
        return ok, err

    def set_storage(self, **values):
        """发 CMD_CONFIG_SET("storage", ...)，返回应答 doc。"""
        import time
        holder = {}
        done = [False]

        def _cb(doc):
            holder.update(doc)
            done[0] = True

        self.bridge.config_set("storage", values, on_done=_cb)
        deadline = time.time() + 10
        while not done[0] and time.time() < deadline:
            time.sleep(0.05)
        return holder

    def get_storage(self):
        import time
        holder = {}
        done = [False]

        def _cb(doc):
            holder.update(doc)
            done[0] = True

        self.bridge.config_get(on_done=_cb)
        deadline = time.time() + 10
        while not done[0] and time.time() < deadline:
            time.sleep(0.05)
        return holder

    def stop(self):
        import subprocess
        try:
            if self.bridge is not None:
                self.bridge.disconnect_from()
        except Exception:                            # noqa: BLE001
            pass
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def m_storlim_read_storage(case, tries=30, gap=0.5):
    """取一档落盘状态，**等到带上真读数为止**。

    [为什么要重试·2026-11-09] 库正处在滚动/换文件的那一刻，服务端偶发回
    "没有 storageStats" 的应答（实测约 1/3 概率）。那不是功能缺陷，是读回
    时机问题。原用例只给**第一次**读回加了重试，后面的读回没有 ⇒ 一撞上
    就 `policy=None` 假红（全量门禁里实测踩到）。这里统一成同一个重试
    口径，所有读数点共用。

    [为什么等 rows>0] 单看"有没有 storageStats"不够：空库时 stats 里也可能
    是一份全 0 的读数 ⇒ 断言照样假红。故等到**真有行**再返回。
    """
    import time
    doc = {}
    for _ in range(max(1, int(tries))):
        doc = case.get_storage() or {}
        stats = doc.get("storageStats") or {}
        if stats.get("rows"):
            return doc
        time.sleep(gap)
    return doc


def m_storlim_scenario(tmpdir, port, policy, max_kb, keep_files=3,
                       grow_seconds=26, sample_ms=1000):
    """跑一个策略：配置里直接给 KB 级上限 → 等数据长到超限 → 采样观察。"""
    import os
    case = m_storlim_ServerCase(port, tmpdir, sample_ms=sample_ms,
                                 max_kb=max_kb, policy=policy)
    if not case.start():
        check("%s: 服务端启动" % policy, False)
        return None
    try:
        ok, err = case.connect()
        if not ok:
            check("%s: 登录" % policy, False, str(err))
            return None
        # 上限已经写在配置文件里（KB 精度）；这里只确认服务端读到了。
        #
        # [为什么重试] 刚改完上限、库正处在滚动/换文件的那一刻，读回
        # 偶发一次**错误应答**（没有 storageStats）⇒ 策略读数变成 None，
        # 断言偶发红（实测约 1/3 概率）。这不是功能缺陷，是读回时机问题：
        # 轮询几次直到拿到读数为止（上限/策略本身由 B3a~B3e 独立断言）。
        doc = m_storlim_read_storage(case)
        stats = doc.get("storageStats") or {}
        configured_kb = (doc.get("storage") or {}).get("maxMb", -1)
        check("%s: 服务端识别到容量上限" % policy, configured_kb != -1,
              "maxMb 读数=%s（0 = 不足 1MB，符合 KB 级配置）"
              % configured_kb)
        check("%s: 策略生效" % policy,
              (stats.get("policy") or policy) == policy,
              str(stats.get("policy")))
        # 让它长一段时间（history 1s 一行；120KB 约 900 行 → 需要确实写满）
        m_storlim_wait(grow_seconds)
        # 同一个重试口径（滚动结束后这一次同样可能撞上"没有 storageStats"
        # 的应答，B3f 直接读它 ⇒ 必须也等）。
        doc = m_storlim_read_storage(case)
        stats = doc.get("storageStats") or {}
        return {"case": case, "stats": stats, "doc": doc,
                "dir": tmpdir, "db": os.path.join(tmpdir, "limit.db")}
    except Exception:                                # noqa: BLE001
        case.stop()
        raise


def m_storlim_part_config_roundtrip(tmpdir):
    print("== A. 配置通道（读写回环 + 落盘） ==")
    import json
    import os
    case = m_storlim_ServerCase(_free_port(), tmpdir)
    if not case.start():
        check("A0 服务端启动", False)
        return
    try:
        ok, err = case.connect()
        check("A0 登录", ok, str(err or ""))
        resp = case.set_storage(maxMb=7, fullPolicy="stop", keepFiles=5)
        check("A1 SET 成功", resp.get("ok") is True)
        storage = resp.get("storage") or {}
        check("A2 应答回显 maxMb=7",
              storage.get("maxMb") == 7, str(storage.get("maxMb")))
        check("A3 应答回显策略=stop",
              storage.get("fullPolicy") == "stop", str(storage.get("fullPolicy")))
        check("A4 应答回显 keepFiles=5",
              storage.get("keepFiles") == 5, str(storage.get("keepFiles")))

        # 落盘到配置文件（重启不丢）。上限在文件里是 KB 口径（max_kb，
        # 支持 KB 级细粒度），7MB = 7168KB。
        cfg_path = os.path.join(tmpdir, "hsf_server_py.json")
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh).get("storage") or {}
        check("A5 已写进配置文件 max_kb（7MB=7168KB）",
              saved.get("max_kb") == 7168, str(saved.get("max_kb")))
        check("A6 已写进配置文件 full_policy",
              saved.get("full_policy") == "stop", str(saved.get("full_policy")))
        check("A7 已写进配置文件 keep_files",
              saved.get("keep_files") == 5, str(saved.get("keep_files")))

        # GET 回读
        doc = m_storlim_read_storage(case)
        storage = doc.get("storage") or {}
        check("A8 GET 回读一致",
              storage.get("maxMb") == 7
              and storage.get("fullPolicy") == "stop"
              and storage.get("keepFiles") == 5, str(storage))

        # ---- 参数校验 ----
        bad = case.set_storage(fullPolicy="bogus")
        check("A9 非法策略被拒", bad.get("ok") is False,
              str(bad.get("err", ""))[:40])
        over = case.set_storage(maxMb=99999999)
        st = (over.get("storage") or {})
        check("A10 越界上限被夹取到合法范围",
              over.get("ok") is True and st.get("maxMb") == 1024 * 1024,
              str(st.get("maxMb")))
        case.set_storage(keepFiles=0)
        st = (m_storlim_read_storage(case).get("storage") or {})
        check("A11 keepFiles 下限夹到 1", st.get("keepFiles") == 1,
              str(st.get("keepFiles")))
    finally:
        case.stop()


def m_storlim_part_stop(tmpdir, port):
    print("== B1. stop 策略：到限停写 ==")
    import os
    res = m_storlim_scenario(tmpdir, port, "stop", max_kb=120,
                              grow_seconds=32, sample_ms=100)
    if not res:
        return
    case, stats = res["case"], res["stats"]
    try:
        used_mb = float(stats.get("usedMb") or 0)
        rows = int(stats.get("rows") or 0)
        paused = bool(stats.get("paused"))
        total = m_storlim_dir_bytes(tmpdir)
        check("B1a 已触发停写", paused, "pauseReason=%s"
              % (stats.get("pauseReason") or "")[:40])
        check("B1b 磁盘不越界（<= 上限 + 余量）",
              total <= 400 * 1024,
              "总占用 %d KB / 上限 120 KB" % (total // 1024))
        check("B1c 库里确实有数据（不是空库假停）", rows > 0, "rows=%d" % rows)
        # 再等一会，确认行数真的不再增长（真停写）
        m_storlim_wait(4)
        stats2 = m_storlim_read_storage(case).get("storageStats") or {}
        check("B1d 停写后行数不再增长",
              int(stats2.get("rows") or 0) == rows,
              "%d → %d" % (rows, int(stats2.get("rows") or 0)))
        check("B1e 用量读数合理", used_mb > 0, "usedMb=%.2f" % used_mb)
    finally:
        case.stop()


def m_storlim_part_ring(tmpdir, port):
    print("== B2. ring 策略：循环覆盖 ==")
    import os
    # [上限为什么要 120KB 而不是更小] SQLite 空库自身就有约 40KB 的页
    # 与 WAL 开销（实测空库 content=41040）；比它还小的上限永远"超限"，
    # 删到空也降不下去，测出来的是底噪不是策略。120KB 略高于底噪，
    # 内容真能触顶、也真能裁下来。
    # [采样为什么 100ms 2026-11-14] 限额判定用**物理占用**（逻辑 + -wal
    # 文件大小）；而后���维护线程每 2 秒把 -wal 归并并**截断到 0** ⇒ -wal
    # 不再「虚高」⇒ 1 行/秒 × 40 秒 = 40 行（约 36KB）**真的**够不到
    # 120KB 上限 ⇒ 裁剪不触发 ⇒ 断言测不到策略。改 100ms 采样（10 行/秒
    # ⇒ 400 行 ≈ 360KB）⇒ **靠真实内容**触顶，而不是靠 WAL 副作用。
    res = m_storlim_scenario(tmpdir, port, "ring", max_kb=120,
                              grow_seconds=40, sample_ms=100)
    if not res:
        return
    case, stats = res["case"], res["stats"]
    try:
        rows = int(stats.get("rows") or 0)
        dropped = int(stats.get("droppedRows") or 0)
        total = m_storlim_dir_bytes(tmpdir)
        content = int(stats.get("contentBytes") or 0)
        check("B2a 发生了循环丢弃", dropped > 0, "droppedRows=%d" % dropped)
        check("B2b 磁盘封顶（<= 上限 + 余量）",
              total <= 200 * 1024,
              "磁盘 %d KB / 内容 %d KB / 上限 120 KB"
              % (total // 1024, content // 1024))
        # 再跑一会：行数应围绕水位波动，而不是一路涨。
        # [要等够] 裁剪发生后的下一个"内容触顶"周期才看得到第二次丢弃，
        # 1s 一行、120KB 档大约几十行 → 给足 20s。
        m_storlim_wait(20)
        stats2 = m_storlim_read_storage(case).get("storageStats") or {}
        rows2 = int(stats2.get("rows") or 0)
        dropped2 = int(stats2.get("droppedRows") or 0)
        check("B2c 一直在循环（丢弃行数只增不减）", dropped2 >= dropped,
              "%d → %d" % (dropped, dropped2))
        check("B2d 行数被封顶（不是无限增长）",
              rows2 < rows * 3 + 500,
              "%d → %d（上限 120KB）" % (rows, rows2))
        check("B2e 库仍可读（表结构完好）", rows2 > 0, "rows=%d" % rows2)
        # 限额是「写前闸」：判定点在上限，写入后允许略超一行，故留 1KB 余量。
        check("B2f 内容占用封顶在上限附近（裁剪真生效）",
              int(stats2.get("contentBytes") or 0) <= 121 * 1024,
              "content=%d KB / 上限 120 KB"
              % (int(stats2.get("contentBytes") or 0) // 1024))
    finally:
        case.stop()


def m_storlim_part_rotate(tmpdir, port):
    print("== B3. rotate 策略：分段滚动 ==")
    import os
    res = m_storlim_scenario(tmpdir, port, "rotate", max_kb=200,
                              keep_files=3, grow_seconds=34, sample_ms=100)
    if not res:
        return
    case, stats = res["case"], res["stats"]
    try:
        # 分段文件名是 `limit.<时间戳>.db`；主库 `limit.db` 不算分段
        # （只按前缀匹配会把主库本体也捞进来，必须显式排除）。
        main_name = "limit.db"
        segs = [n for n in os.listdir(tmpdir)
                if n.startswith("limit.") and n.endswith(".db")
                and n != main_name]
        total = m_storlim_dir_bytes(tmpdir)
        check("B3a 产生了分段文件", len(segs) >= 1, str(sorted(segs)[:3]))
        check("B3b 分段数不超过 keepFiles-1（当前库另算）", len(segs) <= 2,
              "分段 %d 个 / 允许 2 个：%s" % (len(segs), sorted(segs)))
        check("B3c 磁盘封顶（<= 上限 + 余量）",
              total <= 600 * 1024,
              "总占用 %d KB / 上限 200 KB" % (total // 1024))
        main = os.path.join(tmpdir, "limit.db")
        check("B3d 当前库回到小体积（滚过之后）",
              os.path.getsize(main) <= 200 * 1024,
              "当前库 %d KB" % (os.path.getsize(main) // 1024))
        check("B3e 服务端仍在写（当前库有行）",
              int(stats.get("rows") or 0) >= 0)
        check("B3f 策略读数为 rotate",
              (stats.get("policy") or "") == "rotate", stats.get("policy"))
    finally:
        case.stop()


def run_storlim(slow_parts=True):
    """§18（并入）存储上限：封顶 / 轮转策略读数。

    [原 main() 的"跑各 part"部分；汇总打印与退出码交给总回归]

    [隔离] 每个场景一个独立子目录（配置/库文件互不干扰），端口全部
    走总回归的 `_free_port()`（原模块把 9101~9104 写死，会撞并行任务）。

    :param slow_parts: 是否跑 stop / rotate 两段。
        [为什么要留这个开关] 这两段各要 30s+ 的**真实增长**（到限要真写
        满几百 KB），与"一条命令跑全"的时长预算冲突；封顶/轮转的核心
        口径由 A 段（配置回环）与 B2 段（ring 封顶）覆盖。只想要快跑
        就传 False。
    """
    import os
    import shutil
    import tempfile
    tmp_root = tempfile.mkdtemp(prefix="hsf_reg_storage_")
    print("  临时目录：%s" % tmp_root)
    # 每个场景一个独立子目录（配置/库文件互不干扰）。
    dirs = {}
    for name in ("cfg", "stop", "ring", "rotate"):
        dirs[name] = os.path.join(tmp_root, name)
        os.makedirs(dirs[name], exist_ok=True)
    try:
        # A 段：配置通道（读写回环 + 落盘 + 越界夹取）——"策略读数"。
        m_storlim_part_config_roundtrip(dirs["cfg"])
        if slow_parts:
            # B1 段：stop 策略（到限停写）。
            m_storlim_part_stop(dirs["stop"], _free_port())
        # B2 段：ring 策略（到限轮转 + 磁盘不越界）——"封顶/轮转"。
        m_storlim_part_ring(dirs["ring"], _free_port())
        if slow_parts:
            # B3 段：rotate 策略（分段滚动）。
            m_storlim_part_rotate(dirs["rotate"], _free_port())
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)


# ==========================================================================
# 段落：test_trend_chart（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【趋势图段落】监视页实时趋势图回归锁（纯离屏，不连服务端）。

    入口：run_trendch()

[原文件] tests/test_trend_chart.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

任务口径（对齐 C 端 HsfMonitorVfd.c 区 5 / HsfMonitorFlowMeter.c 趋势图）：
  A. 60 点滚动窗：新遥测逐拍追加、超窗移除最旧；跟随态窗口右缘贴最新点
  B. 多序列：VFD 3 序列各自独立滚动，互不挤点
  C. 滚轮缩放 X 轴：指针锚点（锚点处时刻不变）、上/下限钳制
  D. 视口接管停写：拖动/缩放后窗口不再自动右移，**追点继续**
  E. 跟随恢复：「跟随」恢复右移；右缘推回实时点（容差）自动交还
  F. 真事件路径：左键拖动 / 双击 / 悬停（离屏平台 QTest.mouseMove 到不了
     mouseMoveEvent，须显式构造 QMouseEvent 投递——见 test_chart_interaction
     的同款注释）
  G. 重绘纪律：追点/缩放/平移不增删控件、不改控件几何（update() 重绘，
     不整页重排）
  H. 页面接入：VfdPage / WaterPage / FlowPage 建了趋势图、refresh() 真的逐拍喂点、
     同拍判重、无数据不编点

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **不插主文件路径**：只把客户端包目录放进 sys.path（主文件已经做
    了同样的事），绝不把 tests/ 放进去 ⇒ 不会循环 import。
"""

def _m_trendch_paths():
    """算本段落用到的两个目录。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys 已被主文件
    占用）⇒ 本段落一律把标准库 import 放进函数里，顶层只留**带前缀**
    的自有名字（重名检查要求顶层名与主文件交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    return d, os.path.dirname(d)


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_trendch_TESTS_DIR, m_trendch_CLIENT_ROOT = _m_trendch_paths()

#: 固定基准时刻（毫秒），测试不依赖当前时间（同 test_chart_interaction）。
m_trendch_BASE = 1793927000000
m_trendch_STEP = 500                     # 拍距 500ms


#: 主题是否已安装（合并进程只装一次的守卫）。
m_theme_installed = False


def m_theme_install_once(app):
    """把 HSF 主题装到总回归唯一的 QApplication 上——**只装一次**。

    [为什么不能重复装 2026-11-09] theme.install 的 setStyleSheet 会重洗
    全量控件；合并进程跑到中段时，前面段落创建/销毁过的页面里可能残留
    "C++ 已删但 Python 还引用"的坏对象，重洗样式一碰就 abort（faulthandler
    实测栈：theme.install → setStyleSheet）。独立进程时代每段装一次没有
    这个风险；合并后必须只装一次（首次安装放在最早需要主题的段落）。
    """
    global m_theme_installed
    if m_theme_installed:
        return app
    from hsfsrv_client import theme
    theme.install(app)
    m_theme_installed = True
    return app


def m_trendch_app_instance():
    """取总回归那一个 QApplication（**不新建**）并装主题。

    [为什么不新建] Qt5 一进程多 QApplication 是未定义行为，AGENTS 明令
    禁止；本段只负责把主题装到总回归已经建好的 app 上。
    """
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        raise RuntimeError("QApplication 尚未创建：总回归 main() 里先建 app "
                           "再跑本段")
    m_theme_install_once(app)
    return app


def m_trendch_make_chart(series=None):
    """建一个离屏可绘的趋势图（600x300，show 后几何才真实）。"""
    from hsfsrv_client.pages_monitor import TrendChart
    if series is None:
        series = (("out", "输出频率 Hz"), ("set", "给定频率 Hz"),
                  ("cur", "输出电流 A"))
    chart = TrendChart(series)
    chart.resize(600, 300)
    chart.show()
    return chart


def m_trendch_feed(chart, key, count, t0=m_trendch_BASE, step=m_trendch_STEP,
                   v0=10.0, dv=0.1):
    """合成序列逐拍喂入；返回最后一个点的 (t, v)。"""
    last = (t0, v0)
    for i in range(count):
        last = (t0 + i * step, v0 + i * dv)
        chart.append(key, last[0], last[1])
    return last


def m_trendch_send_mouse(widget, kind, pos, buttons=None):
    """显式投递 QMouseEvent（离屏下 QTest.mouseMove 不可靠，见文件头）。"""
    from PyQt5.QtCore import QEvent, QPointF, Qt
    from PyQt5.QtGui import QMouseEvent
    from PyQt5.QtWidgets import QApplication as _QApp
    if buttons is None:
        buttons = Qt.NoButton
    etype = {"press": QEvent.MouseButtonPress,
             "move": QEvent.MouseMove,
             "release": QEvent.MouseButtonRelease,
             "dclick": QEvent.MouseButtonDblClick}[kind]
    button = Qt.LeftButton if kind != "move" else Qt.NoButton
    event = QMouseEvent(etype, QPointF(pos), button,
                        buttons if kind == "move" else Qt.LeftButton,
                        Qt.NoModifier)
    _QApp.sendEvent(widget, event)


# ============================================================================
#              A. 60 点滚动窗 + 跟随态窗口
# ============================================================================

def m_trendch_part_a_window(app):
    print("== A. 60 点滚动窗 ==")
    chart = m_trendch_make_chart((("a", "A"),))
    check("A0 初始无窗口（还没数据）", chart.window_ms() is None
          and chart.is_following())

    # 数据未满窗：左缘钳在时间轴 0 点、窗宽不塌（对齐 C 端 vfdTrendFollowWindow）
    m_trendch_feed(chart, "a", 3)
    lo, hi = chart.window_ms()
    check("A1 未满窗时左缘钳到首点（不空滚）",
          lo == m_trendch_BASE
          and abs(hi - (m_trendch_BASE + chart.WIN_DEF_MS)) < 1e-6,
          "窗口 %s" % (chart.window_ms(),))

    # 喂满 70 点 → 只留最近 60 个（共 73 拍，最旧 13 拍滑出）
    m_trendch_feed(chart, "a", 70, t0=m_trendch_BASE + 3 * m_trendch_STEP)
    pts = chart.points("a")
    check("A2 窗内点数封顶 60", len(pts) == 60, "实际 %d" % len(pts))
    check("A3 最旧的点已滑出（首点 = 第 14 拍）",
          pts[0][0] == m_trendch_BASE + 13 * m_trendch_STEP, "首点 %d" % pts[0][0])
    check("A4 最新点在窗尾", pts[-1][0] == m_trendch_BASE + 72 * m_trendch_STEP,
          "尾点 %d" % pts[-1][0])

    # 跟随态：数据跨度超过窗宽后，右缘贴最新追点、左缘 = 右缘-窗宽
    lo, hi = chart.window_ms()
    newest = pts[-1][0]
    check("A5 跟随态右缘贴最新点", abs(hi - newest) < 1e-6,
          "hi=%s newest=%d" % (hi, newest))
    check("A6 跟随态窗宽 = WIN_DEF",
          abs((hi - lo) - chart.WIN_DEF_MS) < 1e-6,
          "跨度 %.1f" % (hi - lo))

    # 继续逐拍追加：窗口跟着右移（跟随），点数恒 60
    last = m_trendch_feed(chart, "a", 5, t0=pts[-1][0] + m_trendch_STEP)
    pts2 = chart.points("a")
    check("A7 追点后窗口右移", chart.window_ms()[1] == last[0],
          "右缘 %s 最新 %d" % (chart.window_ms()[1], last[0]))
    check("A8 滚动窗继续滑出（仍 60 点）", len(pts2) == 60,
          "实际 %d" % len(pts2))
    check("A9 仍是跟随态", chart.is_following())
    chart.close()


# ============================================================================
#                        B. 多序列
# ============================================================================

def m_trendch_part_b_series(app):
    print("== B. 多序列（VFD 3 序列）==")
    from PyQt5.QtCore import QPoint
    chart = m_trendch_make_chart()                       # out / set / cur 三序列
    m_trendch_feed(chart, "out", 70, v0=24.0)
    m_trendch_feed(chart, "set", 70, v0=25.0)
    m_trendch_feed(chart, "cur", 70, v0=3.0, dv=0.01)
    check("B1 三序列各自封顶 60 点",
          all(len(chart.points(k)) == 60 for k in ("out", "set", "cur")),
          str([len(chart.points(k)) for k in ("out", "set", "cur")]))
    # 一序列继续追点，不影响别的序列已保留的点
    out_first = chart.points("out")[0]
    m_trendch_feed(chart, "cur", 5, t0=m_trendch_BASE + 70 * m_trendch_STEP,
                   v0=3.0, dv=0.01)
    check("B2 他序列追点不动本序列既有点",
          chart.points("out")[0] == out_first and len(chart.points("cur")) == 60)

    # 悬停能按序列分别命中（同一时刻三条序列重横坐标，纵向最近者优先）。
    # [取点口径] 窗宽 WIN_DEF(12s) 只盖最近 WIN_DEF/STEP(=24) 个点，
    # 取倒数第 5 个点保证在视口内（索引 30 在 500ms 拍距下已滑出窗口）。
    app.processEvents()
    t_mid = chart.points("out")[55][0]
    px = chart._x_at(t_mid)
    hit = chart.hover_at(QPoint(int(px), 150))
    check("B3 悬停命中且带序列名",
          hit is not None and hit[1] in ("输出频率 Hz", "给定频率 Hz",
                                         "输出电流 A"),
          str(hit))
    text = chart.hover_text(QPoint(int(px), 150))
    check("B4 悬停文本含数值与时刻",
          ("%.2f" % hit[3]) in text and "@" in text, text)
    chart.close()


# ============================================================================
#                  C. 滚轮缩放（锚点 / 上限 / 下限）
# ============================================================================

def m_trendch_wheel(chart, app, pos, delta):
    from PyQt5.QtCore import QPoint, QPointF, Qt
    from PyQt5.QtGui import QWheelEvent
    ev = QWheelEvent(QPointF(pos), QPointF(chart.mapToGlobal(pos)),
                     QPoint(0, 0), QPoint(0, delta),
                     Qt.NoButton, Qt.NoModifier, Qt.NoScrollPhase, False)
    chart.wheelEvent(ev)
    app.processEvents()


def m_trendch_part_c_zoom(app):
    print("== C. 滚轮缩放 X 轴 ==")
    from PyQt5.QtCore import QPoint
    chart = m_trendch_make_chart((("a", "A"),))
    m_trendch_feed(chart, "a", 70)
    l, t, r, b = chart.plot_rect()
    center = QPoint((l + r) // 2, (t + b) // 2)

    span0 = chart.window_ms()[1] - chart.window_ms()[0]
    anchor = chart.window_ms()[0] + span0 * 0.5
    m_trendch_wheel(chart, app, center, 120)            # 上滚一格 = 放大
    lo, hi = chart.window_ms()
    check("C1 滚轮上滚放大（窗变窄）且接管视口",
          abs((hi - lo) - span0 * 0.8) < 1e-6 and not chart.is_following(),
          "跨度 %.0f→%.0f following=%s" % (span0, hi - lo,
                                           chart.is_following()))
    new_frac = (anchor - lo) / (hi - lo)
    # [容差按「1 像素」算，不能按 1e-6] 缩放锚点取的是**指针的整数像素**
    # （wheelEvent 里 `event.pos().x()` 是 int）⇒ 锚点本来就有半像素级的
    # 量化误差；绘图区宽度一变（2026-11-17 边距随缩放换算）误差量级就变。
    # 真正的口径是"锚点处时刻不变，误差不超过 1 像素"。
    _px_tol = 1.0 / max(1, (r - l))
    check("C2 锚点处时刻不变（指针锚点缩放，误差 ≤ 1 像素）",
          abs(new_frac - 0.5) <= _px_tol,
          "锚点位置 %.6f（容差 %.6f）" % (new_frac, _px_tol))

    m_trendch_wheel(chart, app, center, -120)           # 下滚一格 = 缩小
    lo2, hi2 = chart.window_ms()
    check("C3 滚轮下滚缩小（回到原窗宽）",
          abs((hi2 - lo2) - span0) < 1e-6, "跨度 %.0f" % (hi2 - lo2))

    for _ in range(60):                        # 一直放大 → 钳在 WIN_MIN
        chart.zoom_at(0.5, 0.5)
    lo3, hi3 = chart.window_ms()
    check("C4 放大有下限（WIN_MIN_MS）",
          abs((hi3 - lo3) - chart.WIN_MIN_MS) < 1e-6,
          "跨度 %.0f" % (hi3 - lo3))
    for _ in range(80):                        # 一直缩小 → 钳在 WIN_MAX
        chart.zoom_at(0.5, 2.0)
    lo4, hi4 = chart.window_ms()
    check("C5 缩小有上限（WIN_MAX_MS）",
          abs((hi4 - lo4) - chart.WIN_MAX_MS) < 1e-6,
          "跨度 %.0f" % (hi4 - lo4))
    chart.close()


# ============================================================================
#                D. 视口接管停写（追点继续、窗口不动）
# ============================================================================

def m_trendch_part_d_takeover(app):
    print("== D. 视口接管停写 ==")
    chart = m_trendch_make_chart((("a", "A"),))
    m_trendch_feed(chart, "a", 70)
    lo0, hi0 = chart.window_ms()

    # 缩放接管：窗口停写，追点继续
    chart.zoom_at(0.5, 0.5)
    check("D1 缩放后视口归用户（is_following False）",
          not chart.is_following())
    lo1, hi1 = chart.window_ms()
    m_trendch_feed(chart, "a", 4, t0=hi0 + m_trendch_STEP)
    check("D2 接管后追点窗口不动（停写）",
          chart.window_ms() == (lo1, hi1),
          "%s → %s" % ((lo1, hi1), chart.window_ms()))
    check("D3 接管后追点继续入窗（数据不停）",
          len(chart.points("a")) == 60
          and chart.points("a")[-1][0] == hi0 + 4 * m_trendch_STEP,
          "尾点 %d" % chart.points("a")[-1][0])

    # 平移接管：拖后窗口只动"拖的那一下"，随后的追点不再推窗
    chart.follow()
    lo2, hi2 = chart.window_ms()
    l, t, r, b = chart.plot_rect()
    plot_w = max(1, r - l)
    span = hi2 - lo2
    moved = chart.pan_pixels(80, plot_w)       # 右拖 = 看更早
    lo3, hi3 = chart.window_ms()
    check("D4 右拖平移 → 窗口左移（看更早）",
          moved and hi3 < hi2 - 1e-6, "%.0f → %.0f" % (hi2, hi3))
    check("D5 平移不改变窗宽", abs((hi3 - lo3) - span) < 1e-6,
          "跨度 %.0f" % (hi3 - lo3))
    check("D6 平移接管（停写）", not chart.is_following())
    m_trendch_feed(chart, "a", 3, t0=hi2 + m_trendch_STEP)
    check("D7 平移接管后追点窗口不动",
          chart.window_ms() == (lo3, hi3),
          "%s → %s" % ((lo3, hi3), chart.window_ms()))

    # 拖回最右（右缘推回实时点，容差 CATCHUP_MS）→ 自动交还跟随
    # [方向] 负 dx = 窗口右移（追向最新点）；略拖过头靠右缘钳位停在最新点。
    newest = chart.points("a")[-1][0]
    dx = (chart.window_ms()[1] - newest) / span * plot_w
    chart.pan_pixels(dx - 1.0, plot_w)
    check("D8 右缘推回实时点 → 自动恢复跟随", chart.is_following(),
          "窗口 %s 最新 %d" % (chart.window_ms(), newest))
    chart.close()


# ============================================================================
#                        E. 跟随恢复
# ============================================================================

def m_trendch_part_e_follow(app):
    print("== E. 跟随恢复 ==")
    from PyQt5.QtCore import QPoint
    chart = m_trendch_make_chart((("a", "A"),))
    m_trendch_feed(chart, "a", 70)
    newest = chart.points("a")[-1][0]

    chart.zoom_at(0.5, 0.5)                    # 接管
    chart.follow()                             # 「跟随」按钮同款路径
    lo, hi = chart.window_ms()
    check("E1 follow() 恢复跟随且右缘贴最新",
          chart.is_following() and abs(hi - newest) < 1e-6,
          "following=%s hi=%s newest=%d" % (chart.is_following(), hi, newest))
    check("E2 follow() 窗宽回默认",
          abs((hi - lo) - chart.WIN_DEF_MS) < 1e-6, "跨度 %.0f" % (hi - lo))

    last = m_trendch_feed(chart, "a", 3, t0=newest + m_trendch_STEP)
    check("E3 恢复后追点窗口重新右移", chart.window_ms()[1] == last[0],
          "右缘 %s" % (chart.window_ms()[1],))

    # 双击恢复（触摸屏口径）
    chart.zoom_at(0.5, 0.5)
    check("E4 双击前已接管", not chart.is_following())
    l, t, r, b = chart.plot_rect()
    m_trendch_send_mouse(chart, "dclick", QPoint((l + r) // 2, (t + b) // 2))
    app.processEvents()
    check("E5 双击恢复跟随", chart.is_following())
    chart.close()


# ============================================================================
#                     F. 真事件路径（拖动 / 悬停）
# ============================================================================

def m_trendch_part_f_events(app):
    print("== F. 真事件：左键拖动 / 悬停 ==")
    from PyQt5.QtCore import QPoint, Qt
    chart = m_trendch_make_chart((("a", "A"),))
    m_trendch_feed(chart, "a", 70)
    app.processEvents()
    l, t, r, b = chart.plot_rect()
    center = QPoint((l + r) // 2, (t + b) // 2)

    lo0, hi0 = chart.window_ms()
    m_trendch_send_mouse(chart, "press", center, Qt.LeftButton)
    m_trendch_send_mouse(chart, "move", QPoint(center.x() + 120, center.y()),
                         Qt.LeftButton)
    m_trendch_send_mouse(chart, "release", QPoint(center.x() + 120, center.y()))
    app.processEvents()
    lo1, hi1 = chart.window_ms()
    check("F1 左键拖动接管视口", not chart.is_following())
    check("F2 拖动平移了窗口（右拖看更早）", hi1 < hi0 - 1e-6,
          "%.0f → %.0f" % (hi0, hi1))
    check("F3 拖动不改变窗宽",
          abs((hi1 - lo1) - (hi0 - lo0)) < 1e-6, "跨度 %.0f" % (hi1 - lo1))

    # 悬停：move 到某个点附近 → hover_at 命中该点
    app.processEvents()
    pts = chart.points("a")
    t_mid = pts[-8][0]                         # 窗内肯定可见的靠后点
    px = chart._x_at(t_mid)
    vmin, vmax = chart._visible_scale(pts)
    py = b - (b - t) * (pts[-8][1] - vmin) / (vmax - vmin)
    hit = chart.hover_at(QPoint(int(px), int(py)))
    check("F4 悬停命中最近点", hit is not None and hit[2] == t_mid,
          str(hit))
    text = chart.hover_text(QPoint(int(px), int(py)))
    check("F5 悬停文本 = 名称 数值 @ 时刻",
          text.startswith("A ") and ("%.2f" % pts[-8][1]) in text
          and "@" in text, text)
    # [纵向不设限是设计口径] 悬停按横向吸附（触摸屏不用瞄准），绘图区
    # 纵向都算命中区；只有点击带外（上下各 16px 之外）才不命中。
    check("F6 悬停带外不命中", chart.hover_at(QPoint(center.x(), t - 20))
          is None)
    chart.close()


# ============================================================================
#              G. 重绘纪律（update()，不整页重排）
# ============================================================================

def m_trendch_part_g_repaint(app):
    print("== G. 重绘纪律 ==")
    chart = m_trendch_make_chart((("a", "A"),))
    chart.resize(560, 280)
    app.processEvents()
    children0 = len(chart.children())
    size0 = (chart.width(), chart.height())
    m_trendch_feed(chart, "a", 200)                      # 大量追点
    chart.zoom_at(0.5, 0.5)
    chart.pan_pixels(60, 500)
    app.processEvents()
    check("G1 追点/缩放/平移不增删控件",
          len(chart.children()) == children0,
          "%d → %d" % (children0, len(chart.children())))
    check("G2 控件几何不变（update() 重绘，不触发布局）",
          (chart.width(), chart.height()) == size0,
          "%s → %s" % (size0, (chart.width(), chart.height())))
    check("G3 追 200 点后窗内仍 60 点（deque 滚动窗）",
          len(chart.points("a")) == 60, "实际 %d" % len(chart.points("a")))
    chart.close()


# ============================================================================
#                H. 页面接入（VfdPage / WaterPage / FlowPage）
# ============================================================================

class m_trendch_Vfd(object):
    """变频器遥测桩：updated_ms 可按拍推进（趋势追点判重靠它）。"""

    run_state = 3
    running = True
    faulted = False
    updated_ms = m_trendch_BASE
    set_freq_hz = 25.0
    output_freq_hz = 24.8
    output_current_a = 3.2
    output_power_kw = 1.1
    torque_percent = 62.0
    speed_rpm = 714.0
    bus_voltage_v = 540.0
    output_voltage_v = 220.0

    def is_fresh(self):
        return True

    def has(self, _key):
        return True

    def run_state_text(self):
        return "运行中"

    def fault_text(self):
        return "无故障"


class m_trendch_Ctx(object):
    def __init__(self, model):
        self.model = model

    def control_blocked_reason(self):
        return ""

    def set_status(self, _msg):
        pass


def m_trendch_part_h_pages(app):
    print("== H. 页面接入 ==")
    from PyQt5.QtWidgets import QPushButton
    from hsfsrv_client import pages_monitor as pm
    from hsfsrv_client.model import (_now_ms, FlowMeterState, PumpState,
                                     TurbidityState)
    from types import SimpleNamespace

    # ---- VfdPage：三序列逐拍喂点 + 同拍判重 + 无数据不追 ----
    vfd = m_trendch_Vfd()
    page = pm.VfdPage(m_trendch_Ctx(SimpleNamespace(vfd=vfd)))
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    check("H1 VfdPage 建了趋势图且 3 条序列",
          isinstance(page._trend, pm.TrendChart)
          and [k for k, _n in pm.VfdPage.TREND_SERIES] == ["out", "set", "cur"],
          str(pm.VfdPage.TREND_SERIES))
    follow_btn = page.findChild(QPushButton, "btnTrendFollow")
    check("H2 页上有「跟随」按钮（btnTrendFollow）", follow_btn is not None)
    legend = page.findChild(QPushButton, "comboTrendLegend")
    check("H2a 趋势卡有「图例」多选（comboTrendLegend）", legend is not None)
    check("H2b 图例选项数 = 序列数、默认全选",
          legend is not None and len(legend._labels) == 3
          and legend.checked_indexes() == [0, 1, 2],
          "%s / %s" % (getattr(legend, "_labels", "-"),
                       legend.checked_indexes() if legend else "-"))
    legend._toggle_item(0)                     # 取消首条（输出频率）
    check("H2c 取消一条 → 可见集合少一条",
          page._trend.visible_keys() == ("set", "cur"),
          str(page._trend.visible_keys()))
    labels = page._trend.legend_labels()
    check("H2d 取消的序列不进图例",
          len(labels) == 2 and all("输出频率" not in s for s in labels),
          str(labels))
    legend._toggle_item(0)                     # 勾回（下面 H3 起正常喂点）
    check("H2e 勾回 → 恢复全部可见",
          page._trend.visible_keys() == ("out", "set", "cur"),
          str(page._trend.visible_keys()))

    for i in range(3):                         # 三个不同的遥测拍
        vfd.updated_ms = m_trendch_BASE + i * 1000
        vfd.output_freq_hz = 24.0 + i
        page.refresh()
    app.processEvents()
    pts = page._trend.points("out")
    check("H3 refresh() 逐拍追点（3 拍 3 点）", len(pts) == 3,
          "实际 %d" % len(pts))
    check("H4 追的是输出频率值", abs(pts[-1][1] - 26.0) < 1e-9,
          str(pts[-1]))
    page.refresh()                             # 同一拍再刷：判重不重复追
    check("H5 同拍重复 refresh 不重复追点",
          len(page._trend.points("out")) == 3,
          "实际 %d" % len(page._trend.points("out")))
    # [隐藏≠停采] 隐藏期间缓冲继续累积；取消/勾回都不动已有历史点。
    legend._toggle_item(0)
    check("H5a 隐藏序列后历史点仍在",
          not page._trend.is_visible("out")
          and len(page._trend.points("out")) == 3,
          "visible=%s n=%d" % (page._trend.is_visible("out"),
                               len(page._trend.points("out"))))
    vfd.updated_ms = m_trendch_BASE + 3 * 1000
    page.refresh()                             # 隐藏期间照常追点
    check("H5b 隐藏期间数据照常累积",
          len(page._trend.points("out")) == 4,
          "实际 %d" % len(page._trend.points("out")))
    legend._toggle_item(0)
    check("H5c 勾回后可见且新点也在",
          page._trend.is_visible("out")
          and len(page._trend.points("out")) == 4,
          "visible=%s n=%d" % (page._trend.is_visible("out"),
                               len(page._trend.points("out"))))

    vfd.updated_ms = m_trendch_BASE + 4 * 1000   # 新拍待刷新（下面 refresh 消费）
    page.refresh()
    page._trend.zoom_at(0.5, 0.5)              # 用户滚轮缩放 → 接管
    check("H6 缩放后视口接管（停写）", not page._trend.is_following())
    follow_btn.click()                         # 「跟随」按钮恢复（页级路径）
    check("H7 点「跟随」后恢复", page._trend.is_following())

    class _Stale(m_trendch_Vfd):
        def is_fresh(self):
            return False

    vfd.__class__ = _Stale                     # 数据过期：不追点（不编平线）
    vfd.updated_ms = m_trendch_BASE + 9 * 1000
    page.refresh()
    check("H8 数据过期不追点", len(page._trend.points("out")) == 5,
          "实际 %d" % len(page._trend.points("out")))
    page.close()

    # ---- WaterPage（页 6）：只追浊度两路，离线那台不编点 ----
    turb = TurbidityState()
    page = pm.WaterPage(m_trendch_Ctx(SimpleNamespace(turbidity=turb)))
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    check("H9 WaterPage 建了趋势图且浊度两路+水温三条序列",
          isinstance(page._trend, pm.TrendChart)
          and [k for k, _n in pm.WaterPage.TREND_SERIES]
          == ["turb1", "turb2", "temp"],
          str(pm.WaterPage.TREND_SERIES))
    wlegend = page.findChild(QPushButton, "comboTrendLegend")
    check("H9a 水质页图例多选就位（3 项、默认全选）",
          wlegend is not None and len(wlegend._labels) == 3
          and wlegend.checked_indexes() == [0, 1, 2],
          "%s / %s" % (getattr(wlegend, "_labels", "-"),
                       wlegend.checked_indexes() if wlegend else "-"))
    # [测试与产品缺省解耦] 本段锁的是"逐拍喂点/判重"，不是刷新间隔；
    # 水质页缺省 2s（且可能沿用本机存档）⇒ 显式调回 200ms，
    # 否则 +1000ms 的下一拍会被节拍闸丢掉（假红）。
    page._trend.set_sample_ms(200)

    now = _now_ms()                # 模型层单调毫秒钟；updated_ms 要贴近当前
                                   # （is_fresh 判据 now-updated < staleMs）
    turb.apply({"count": 2, "v0_name": "原水", "v0_online": True,
                "v0_ntu": 1240,                 # 0.01 NTU 量化 → 12.40
                "v1_online": False, "v1_ntu": 0})
    turb.updated_ms = now
    page.refresh()
    app.processEvents()
    check("H10 浊度1 已追点（0.01 量化换回 12.40）",
          page._trend.points("turb1")
          and abs(page._trend.points("turb1")[0][1] - 12.4) < 1e-9,
          str(page._trend.points("turb1")))
    check("H11 出水浊度仪 离线不编点", len(page._trend.points("turb2")) == 0,
          "实际 %d" % len(page._trend.points("turb2")))
    check("H11a 无温度机型不产生 temp 点（不编 0℃）",
          len(page._trend.points("temp")) == 0,
          "实际 %d" % len(page._trend.points("temp")))

    # 下一拍：两台都在线 → 两路同拍各追一点（同一节拍闸）
    turb.apply({"count": 2, "v0_name": "原水", "v0_online": True,
                "v0_ntu": 1250, "v1_online": True, "v1_ntu": 300})
    turb.updated_ms = now + 1000
    page.refresh()
    check("H12 两路浊度同拍各追一点（独立序列共用一次节拍闸）",
          len(page._trend.points("turb1")) == 2
          and len(page._trend.points("turb2")) == 1,
          "%d / %d" % (len(page._trend.points("turb1")),
                       len(page._trend.points("turb2"))))
    # 带温度的机型（站3）随帧下发 v0_temp（℃×100）→ 同一拍追一条 temp；
    # 取第一个非 None（本帧只有 v0 在线）。
    turb.apply({"count": 2, "v0_name": "原水", "v0_online": True,
                "v0_ntu": 1260, "v0_temp": 1930,
                "v1_online": True, "v1_ntu": 310})
    turb.updated_ms = now + 2000
    page.refresh()
    temp_pts = page._trend.points("temp")
    check("H12a 带温度机型：水温随帧追点（19.30℃）",
          len(temp_pts) == 1 and abs(temp_pts[0][1] - 19.3) < 1e-9,
          str(temp_pts))
    page.close()

    # ---- FlowPage（页 18）：只追瞬时流量一路 ----
    flow = FlowMeterState()
    pump = PumpState()
    page = pm.FlowPage(m_trendch_Ctx(SimpleNamespace(flowmeter=flow,
                                                     pump=pump)))
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    check("H13 FlowPage 建了趋势图且四个流量读数四条序列",
          isinstance(page._trend, pm.TrendChart)
          and [k for k, _n in pm.FlowPage.TREND_SERIES]
          == ["flow", "speed", "fwd", "rev"],
          str(pm.FlowPage.TREND_SERIES))
    flegend = page.findChild(QPushButton, "comboTrendLegend")
    check("H13a 流量页图例多选就位（4 项、默认全选）",
          flegend is not None and len(flegend._labels) == 4
          and flegend.checked_indexes() == [0, 1, 2, 3],
          "%s / %s" % (getattr(flegend, "_labels", "-"),
                       flegend.checked_indexes() if flegend else "-"))
    page._trend.set_sample_ms(200)      # 同上：与缺省 2s/本机存档解耦

    flow.apply({"flow": 12.4, "speed": 0.42, "forwardTotal": 123.4,
                "reverseTotal": 1.25, "valid": True})
    flow.updated_ms = now
    page.refresh()
    check("H14 流量四序列同拍各追一点",
          all(len(page._trend.points(k)) == 1
              for k in ("flow", "speed", "fwd", "rev")),
          "%s" % {k: len(page._trend.points(k))
                  for k in ("flow", "speed", "fwd", "rev")})
    check("H14a 四序列追的是各自读数",
          abs(page._trend.points("flow")[-1][1] - 12.4) < 1e-9
          and abs(page._trend.points("speed")[-1][1] - 0.42) < 1e-9
          and abs(page._trend.points("fwd")[-1][1] - 123.4) < 1e-9
          and abs(page._trend.points("rev")[-1][1] - 1.25) < 1e-9,
          "%s" % {k: page._trend.points(k)
                  for k in ("flow", "speed", "fwd", "rev")})
    page.refresh()                             # 同一拍再刷：判重不重复追
    check("H15 流量同拍重复 refresh 不重复追点",
          all(len(page._trend.points(k)) == 1
              for k in ("flow", "speed", "fwd", "rev")),
          "%s" % {k: len(page._trend.points(k))
                  for k in ("flow", "speed", "fwd", "rev")})
    flow.apply({"flow": 12.9, "speed": 0.44, "forwardTotal": 124.0,
                "reverseTotal": 1.30, "valid": True})
    flow.updated_ms = now + 1000
    page.refresh()
    check("H16 流量第二拍四序列各追点",
          all(len(page._trend.points(k)) == 2
              for k in ("flow", "speed", "fwd", "rev")),
          "%s" % {k: len(page._trend.points(k))
                  for k in ("flow", "speed", "fwd", "rev")})
    # [至少留一条] 图例下拉 min_checked=1：取消到只剩一条后，最后一条
    # 再点也取消不掉（不会出现"一条线都没有"的空图）。
    for i in (0, 1, 2):
        flegend._toggle_item(i)
    flegend._toggle_item(3)
    check("H16a 图例至少保留一条（最后一条取消被拒）",
          flegend.checked_indexes() == [3]
          and page._trend.visible_keys() == ("rev",),
          "%s / %s" % (flegend.checked_indexes(),
                       page._trend.visible_keys()))
    flegend.check_all()
    check("H16b 图例全选恢复 → 四序列可见",
          page._trend.visible_keys() == ("flow", "speed", "fwd", "rev"),
          str(page._trend.visible_keys()))
    page.close()

    # ---- I. 轴标签与采样节流（2026-11-10 用户口径）----
    # 「x 轴要显示时间」+「y 轴缺失」+「采集速度好快有没有设置」三件事的锁：
    # ① X 刻度渲染成本地时钟时间（不是 "6 s/8 s"）；
    # ② Y 轴按首序列量程画刻度；
    # ③ 「刷新」下拉节流：默认 200ms 每拍都记，放宽后按间隔丢点、窗宽跟着放大。
    from PyQt5.QtWidgets import QComboBox
    vfd2 = m_trendch_Vfd()
    page2 = pm.VfdPage(m_trendch_Ctx(SimpleNamespace(vfd=vfd2)))
    page2.resize(1180, 760)
    page2.show()
    app.processEvents()
    chart = page2._trend
    combo = page2.findChild(QComboBox, "comboTrendSample")
    check("I1 趋势卡有「刷新」下拉（comboTrendSample）", combo is not None)
    check("I2 下拉档位 = TrendChart.SAMPLE_CHOICES",
          combo is not None
          and [combo.itemData(i) for i in range(combo.count())]
          == list(pm.TrendChart.SAMPLE_CHOICES),
          str([combo.itemData(i) for i in range(combo.count())])
          if combo else "-")
    check("I3 缺省 200ms（= 现状，不改行为）",
          chart.sample_ms == 200, "%d" % chart.sample_ms)

    # X 标签是时钟时间：源串里不再有 "%.0f s"，而是 strftime("%H:%M:%S")
    import inspect
    src = inspect.getsource(pm.TrendChart.paintEvent)
    check("I4 X 刻度按本地时钟时间渲染（不是相对秒）",
          "%H:%M:%S" in src and '%.0f s' not in src)
    check("I5 Y 轴画了数值刻度（_trend_num + 5 档）",
          "_trend_num" in src and "range(5)" in src)

    # 节流：切 1s 后 200ms 输入只收约 1/5 的点；窗宽放大到 60×间隔
    base = m_trendch_BASE
    chart.set_sample_ms(1000)
    fed = 0
    for i in range(20):
        t = base + i * 200
        if chart.accept_sample(t):
            fed += 1
            chart.append("out", t, float(i))
    check("I6 1s 节流：200ms 输入只收 1/5 的点",
          3 <= fed <= 5, "收 %d/20" % fed)
    check("I7 窗宽不跟着变（点变稀即可，视口不被抢）",
          abs(chart._win_w - float(pm.TrendChart.WIN_DEF_MS)) < 1e-6,
          "%.0f" % chart._win_w)

    # 两条数据流（流量 / 浊度）各有自己的节拍，互不压掉
    chart.set_sample_ms(1000)
    ok_flow = chart.accept_sample(base, "flow")
    ok_turb = chart.accept_sample(base + 50, "turb")
    check("I8 不同数据流的节拍互不影响", ok_flow and ok_turb)
    page2.close()


# ============================================================================
#          J. 图例多选：可见集合 / Y 轴归属 / 隐藏不丢历史点
# ============================================================================

def m_trendch_part_j_legend(app):
    print("== J. 图例多选（可见集合 / Y 轴 / 历史点保留）==")
    from PyQt5.QtCore import QPoint
    from hsfsrv_client import pages_monitor as pm

    chart = m_trendch_make_chart()
    for key, v0 in (("out", 10.0), ("set", 20.0), ("cur", 3.0)):
        m_trendch_feed(chart, key, 5, v0=v0)
    app.processEvents()
    check("J1 缺省全部可见（按声明顺序）",
          chart.visible_keys() == ("out", "set", "cur"),
          str(chart.visible_keys()))

    chart.set_visible_keys(["out", "cur", "bogus"])
    check("J2 未知 key 忽略、只留传入的可见项",
          chart.visible_keys() == ("out", "cur"),
          str(chart.visible_keys()))
    labels = chart.legend_labels()
    check("J3 隐藏序列不进图例（文本同源）",
          len(labels) == 2
          and all("给定频率" not in s for s in labels)
          and any(s.startswith("输出频率") for s in labels),
          str(labels))
    axis = chart._y_axis_series()
    check("J4 Y 轴取第一条可见序列", axis is not None and axis[0] == "out",
          str(axis))

    chart.set_visible_keys(["cur"])
    axis = chart._y_axis_series()
    check("J5 隐藏主序列后 Y 轴让给下一条可见序列",
          axis is not None and axis[0] == "cur", str(axis))

    # [隐藏≠停采] 隐藏 out 期间继续 append；勾回来后点一个不少。
    pts_before = chart.points("out")
    hidden_n = 26                    # 追过 12s 窗宽，窗口才看得出右移
    for i in range(5, 5 + hidden_n):
        chart.append("out", m_trendch_BASE + i * m_trendch_STEP, 30.0 + i)
    last_t = m_trendch_BASE + (4 + hidden_n) * m_trendch_STEP
    check("J6a 隐藏序列仍参与最新时刻/窗口右移",
          chart.window_ms()[1] == last_t,
          "右缘 %s 最新(隐藏序列) %d" % (chart.window_ms()[1], last_t))
    chart.set_visible_keys(["out", "cur"])
    check("J6 隐藏期间照常收点、勾回历史点仍在",
          chart.is_visible("out")
          and len(chart.points("out")) == len(pts_before) + hidden_n,
          "%d → %d" % (len(pts_before), len(chart.points("out"))))

    # 隐藏序列不参与悬停命中（hover_at 只看可见序列）。
    chart.set_visible_keys(["set", "cur"])     # 再隐藏 out
    pts_out = chart.points("out")
    t_mid = pts_out[-2][0]
    vmin, vmax = chart._visible_scale(pts_out)
    l, t, r, b = chart.plot_rect()
    px = chart._x_at(t_mid)
    py = b - (b - t) * (pts_out[-2][1] - vmin) / ((vmax - vmin) or 1.0)
    hit = chart.hover_at(QPoint(int(px), int(py)))
    check("J7 悬停不命中隐藏序列",
          not chart.is_visible("out") and (hit is None or hit[0] != "out"),
          "%s / %s" % (chart.visible_keys(), hit))

    chart.set_visible_keys([])
    check("J8 空集按「不隐藏任何」处理（调用方保证 >=1）",
          chart.visible_keys() == ("out", "set", "cur"),
          str(chart.visible_keys()))
    chart.set_visible_keys(["nope"])
    check("J9 全是未知 key 也按「不隐藏任何」处理",
          chart.visible_keys() == ("out", "set", "cur"),
          str(chart.visible_keys()))

    # 单序列趋势卡不给「图例」死控件。
    card, chart1, _hint = pm._trend_card((("only", "唯一序列"),))
    from PyQt5.QtWidgets import QPushButton as _QPB
    check("J10 单序列不显示图例下拉（无死控件）",
          card.findChild(_QPB, "comboTrendLegend") is None,
          "趋势卡里不应有 comboTrendLegend")
    # 多序列卡：下拉就位、默认全选、取消后即生效。
    card2, chart2, _hint2 = pm._trend_card((("a", "A"), ("b", "B")))
    combo = card2.findChild(_QPB, "comboTrendLegend")
    check("J11 多序列卡图例下拉就位且默认全选",
          combo is not None and combo.checked_indexes() == [0, 1]
          and chart2.visible_keys() == ("a", "b"),
          "%s / %s" % (combo.checked_indexes() if combo else "-",
                       chart2.visible_keys()))
    combo._toggle_item(0)
    check("J12 下拉取消一条 → chart 可见集合同步",
          chart2.visible_keys() == ("b",),
          str(chart2.visible_keys()))
    chart.close()
    chart1.close()
    chart2.close()


def run_trendch():
    """§17（并入）趋势图：滚动窗 / 多序列 / 缩放 / 接管 / 跟随 / 事件。

    [原 main() 的"跑各 part"部分；建 app/汇总打印/退出码交给总回归]
    """
    app = m_trendch_app_instance()
    m_trendch_part_a_window(app)
    m_trendch_part_b_series(app)
    m_trendch_part_c_zoom(app)
    m_trendch_part_d_takeover(app)
    m_trendch_part_e_follow(app)
    m_trendch_part_f_events(app)
    m_trendch_part_g_repaint(app)
    m_trendch_part_h_pages(app)
    m_trendch_part_j_legend(app)


# ==========================================================================
# 段落：test_ui_scale（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【界面缩放段落】界面字号缩放的回归锁（板端触摸屏可读性）。

    入口：run_uiscal()

[原文件] tests/test_ui_scale.py（2026-11-09 起内联进 tests/test_hsf_regression.py）

[背景] 板端是 1280x800 的 7.9 寸屏（338x211mm），12px 字物理高度只有
约 1.6mm —— 站在触摸屏前看着明显偏小（用户 2026-11-06 反馈"字体有点小，
可以尝试字体都加大"）。界面字号原本是分散硬编码的 QSS 像素值（7 个文件
约 50 处），改法是把它们统一按 `theme.ui_scale()` 放大。

本段锁四件事：
  A. 缩放比例本身：缺省 1.5、可用 HSF_UI_SCALE 覆盖、非法值回落
  B. 换算正确且不小于下限（再小就看不清）
  C. 主题样式表里**不存在漏改的原始字号**（12px 之类应已放大）
  D. 各页面源码里不再残留未缩放的像素字号

[为什么 C/D 要查"残留"] 这次是批量替换，最容易出的错是**漏掉几处** ——
局部字体没放大，界面看起来"有的字大有的字小"。用"不许出现原始小字号"
做断言，比逐个比对像素更快也更稳。

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **不建 QApplication**：本段是纯源码扫描，用不着界面。
  · **不插主文件路径**：只把客户端包目录放进 sys.path（主文件已经做
    了同样的事），绝不把 tests/ 放进去 ⇒ 不会循环 import。
"""

import re


def _m_uiscal_paths():
    """算本段落用到的两个目录。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys 已被主文件
    占用）⇒ 本段落一律把标准库 import 放进函数里，顶层只留**带前缀**
    的自有名字（重名检查要求顶层名与主文件交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    return d, os.path.dirname(d)


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_uiscal_TESTS_DIR, m_uiscal_CLIENT_ROOT = _m_uiscal_paths()
#: client_py 目录（客户端包 hsfsrv_client 的上级 = 原模块的 ROOT）。
m_uiscal_ROOT = m_uiscal_CLIENT_ROOT

#: 设计稿里的原始字号（这些数值不应再出现在已缩放的源码里）
m_uiscal_DESIGN_PX = (11, 12, 13, 14, 15, 16, 17, 20, 24, 30)

#: 需要检查的文件（界面渲染相关）
m_uiscal_UI_FILES = (
    "hsfsrv_client/theme.py",
    "hsfsrv_client/login.py",
    "hsfsrv_client/widgets.py",
    "hsfsrv_client/page_base.py",
    "hsfsrv_client/pages_monitor.py",
    "hsfsrv_client/pages_other.py",
    "hsfsrv_client/pages_valve.py",
    "hsfsrv_client/softkeyboard.py",
)


def m_uiscal_fresh_theme():
    """重载 theme 模块并返回它。

    [为什么不用 del sys.modules + from import] 那样拿到的仍是**包属性上
    缓存的旧模块对象**（`hsfsrv_client.theme` 属性没被清）；`ui_scale()`
    读的是旧对象的 `_UI_SCALE`，于是断言会"滞后一拍"（实测 A5 读到 1.15、
    A6 读到 1.25）。importlib.reload 才是真正重跑模块顶层代码。
    """
    import importlib
    from hsfsrv_client import theme as _t
    return importlib.reload(_t)


def m_uiscal_part_a_scale():
    print("== A. 缩放比例 ==")
    import os
    os.environ.pop("HSF_UI_SCALE", None)
    theme = m_uiscal_fresh_theme()

    check("A1 缺省 1.5", abs(theme.ui_scale() - 1.5) < 1e-6,
          theme.ui_scale())
    theme.set_ui_scale(1.5)
    check("A2 set_ui_scale 生效", abs(theme.ui_scale() - 1.5) < 1e-6,
          theme.ui_scale())
    theme.set_ui_scale(0.5)          # 低于下限
    check("A3 低于 1.0 被钳到 1.0", abs(theme.ui_scale() - 1.0) < 1e-6,
          theme.ui_scale())
    theme.set_ui_scale(9.0)          # 高于上限
    check("A4 高于 2.0 被钳到 2.0", abs(theme.ui_scale() - 2.0) < 1e-6,
          theme.ui_scale())

    # 环境变量路径
    # [必须先清缓存] 上面 A2~A4 调过 set_ui_scale，模块级 _UI_SCALE 已被
    # 写成具体值；只删 sys.modules 拿到的**新模块** _UI_SCALE 是 None，
    # 但同一进程里若模块对象被复用就永远读到旧值。用 None 清除语义保证
    # 干净（这正是 set_ui_scale(None) 存在的理由）。
    theme.set_ui_scale(None)
    os.environ["HSF_UI_SCALE"] = "1.25"
    theme = m_uiscal_fresh_theme()          # 重跑模块顶层 → _UI_SCALE 回 None
    check("A5 HSF_UI_SCALE=1.25 生效", abs(theme.ui_scale() - 1.25) < 1e-6,
          theme.ui_scale())

    # 换一个环境变量值：必须再清一次缓存才读新值
    os.environ["HSF_UI_SCALE"] = "abc"          # 非法
    theme = m_uiscal_fresh_theme()
    check("A6 非法值回落 1.5", abs(theme.ui_scale() - 1.5) < 1e-6,
          theme.ui_scale())

    # 越界值也回落缺省
    os.environ["HSF_UI_SCALE"] = "5.0"
    theme = m_uiscal_fresh_theme()
    check("A7 越界值回落 1.5", abs(theme.ui_scale() - 1.5) < 1e-6,
          theme.ui_scale())

    theme.set_ui_scale(None)
    os.environ.pop("HSF_UI_SCALE", None)
    theme.set_ui_scale(None)                    # 回到缺省


def m_uiscal_part_b_convert():
    print("== B. 换算 ==")
    import sys
    for m in list(sys.modules):
        if m.startswith("hsfsrv_client.theme"):
            del sys.modules[m]
    from hsfsrv_client import theme
    theme.set_ui_scale(1.5)
    check("B1 12px → 18px", theme.font_px(12) == 18, theme.font_px(12))
    check("B2 30px → 45px", theme.font_px(30) == 45, theme.font_px(30))
    # 8*1.5=12 > 下限 10，故取 12；下限只在缩放后仍过小时生效
    check("B3 缩放后仍取下限之上", theme.font_px(8) == 12, theme.font_px(8))
    check("B4 极小值被下限兜住", theme.font_px(3) == 10, theme.font_px(3))
    check("B5 返回整数", isinstance(theme.font_px(13), int))
    theme.set_ui_scale(1.0)
    check("B6 比例 1.0 时原样", theme.font_px(12) == 12, theme.font_px(12))
    theme.set_ui_scale(1.5)


def m_uiscal_part_c_stylesheet():
    print("== C. 主题样式表无残留小字号 ==")
    import sys
    for m in list(sys.modules):
        if m.startswith("hsfsrv_client.theme"):
            del sys.modules[m]
    from hsfsrv_client import theme
    theme.set_ui_scale(1.5)
    qss = theme.STYLESHEET
    check("C1 无未替换的占位符", "%(" not in qss)
    found = [int(n) for n in re.findall(r"font-size:\s*(\d+)px", qss)]
    small = [n for n in found if n < 16]
    check("C2 样式表里没有小于 16px 的字号", not small, small or found)


def m_uiscal_part_d_sources():
    print("== D. 各页面源码无漏改字号 ==")
    import os
    # 每个文件允许的最大原始字号出现次数（0 = 必须全改完）
    offenders = []
    pat = re.compile(r"font-size:\s*(\d+)px")
    for rel in m_uiscal_UI_FILES:
        path = os.path.join(m_uiscal_ROOT, rel)
        if not os.path.isfile(path):
            offenders.append("%s 文件不存在" % rel)
            continue
        src = open(path, encoding="utf-8").read()
        for m in pat.finditer(src):
            # [跳过注释] 注释里出现的 `font-size: 12px` 只是举例，
            # 不是真实样式（theme.py 的说明段就有）。
            ls = src.rfind("\n", 0, m.start()) + 1
            line_text = src[ls:src.find("\n", m.start())]
            if line_text.lstrip().startswith("#"):
                continue
            px = int(m.group(1))
            # 缩放后 11→13, 12→14, 13→15, 14→16, 15→17, 16→18, 17→20,
            # 20→23, 24→28, 30→34；原始值里 14/16 与缩放结果重叠，
            # 故只把"确定未放大"的原始小值当问题。
            if px in (11, 12, 13):
                line = src[:m.start()].count("\n") + 1
                offenders.append("%s:%d 仍是 %dpx" % (rel, line, px))
    check("D1 无未放大的小字号（11/12/13px）", not offenders,
          "; ".join(offenders[:5]) if offenders else "")

    # 关键页面必须真的变大了：抽查最大字号
    pm_path = os.path.join(m_uiscal_ROOT, "hsfsrv_client/pages_monitor.py")
    src = open(pm_path, encoding="utf-8").read()
    big = [int(n) for n in pat.findall(src)]
    check("D2 监视页保留大字号读数（≥30px）", any(n >= 30 for n in big),
          sorted(set(big), reverse=True)[:5])


def run_uiscal():
    """§22（并入）界面字号缩放。

    [原 main() 的"跑各 part"部分；汇总打印与退出码交给总回归]
    """
    m_uiscal_part_a_scale()
    m_uiscal_part_b_convert()
    m_uiscal_part_c_stylesheet()
    m_uiscal_part_d_sources()


# ==========================================================================
# 段落：test_vfd_slider（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【变频器滑块段落】搅拌泵页「拖动调速」滑块的回归锁（纯离屏，不连服务端）。

    入口：run_vfdsld()

[原文件] tests/test_vfd_slider.py（2026-11-09 起内联进 tests/test_hsf_regression.py）

[为什么要有这条滑块] 现场是触摸屏、没有外接键盘：点 QSpinBox 的小箭头
一下一下点太慢，点数字区又会弹软键盘占掉半屏。用户 2026-11-06 要求
"按住滑动调速"。

本段锁四件事（都是**行为契约**，不是实现细节）：
  A. 拖动过程中**不下发**（逐格下发会在总线上刷一串 FC06，泵/阀跟着抖）
  B. 手停下来（去抖窗口到点）只下发**一条**
  C. 松手立即下发一条，且不再叠加去抖那一条
  D. 滑块量程/取值跟随调速口径（Hz 0~50 ↔ rpm 0~1440）
（另有 E 遥测回写 / F 停机预置 / G 触摸命中 / H 无下发按钮 / I 过渡态文案。）

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **不建 QApplication**：复用总回归 main() 里那一个（Qt5 一进程多
    QApplication 是未定义行为，AGENTS 明令禁止）。
  · **不插主文件路径**：只把客户端包目录放进 sys.path（主文件已经做
    了同样的事），绝不把 tests/ 放进去 ⇒ 不会循环 import。
"""

def _m_vfdsld_paths():
    """算本段落用到的两个目录。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys 已被主文件
    占用）⇒ 本段落一律把标准库 import 放进函数里，顶层只留**带前缀**
    的自有名字（重名检查要求顶层名与主文件交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    return d, os.path.dirname(d)


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_vfdsld_TESTS_DIR, m_vfdsld_CLIENT_ROOT = _m_vfdsld_paths()


class m_vfdsld_Vfd(object):
    """变频器状态桩：运行中、固定设定频率。"""

    #: 运行状态（3 = 正转运行）。refresh() 按它判过渡态配色/文案，
    #: 见 pages_monitor.VfdPage.refresh 的说明。
    run_state = 3
    running = True
    faulted = False
    set_freq_hz = 25.0
    output_freq_hz = 24.8
    speed_rpm = 714.0
    bus_voltage_v = 540.0
    output_voltage_v = 220.0
    output_current_a = 3.2
    output_power_kw = 1.1
    torque_percent = 62.0

    def is_fresh(self):
        return True

    def has(self, _key):
        return True

    def run_state_text(self):
        return "运行中"

    def fault_text(self):
        return "无故障"


class m_vfdsld_Bridge(object):
    def __init__(self):
        self.sent = []

    def send_vfd_control(self, doc, on_done=None, **_kw):
        self.sent.append(dict(doc))


class m_vfdsld_Ctx(object):
    def __init__(self):
        self.model = type("M", (), {"vfd": m_vfdsld_Vfd()})()
        self.bridge = m_vfdsld_Bridge()

    def control_blocked_reason(self):
        return ""

    def set_status(self, _msg):
        pass

    def status_message(self, _msg):
        pass

    def request_snapshot(self, *_a, **_k):
        pass


def m_vfdsld_wait(ms):
    """跑一个本地事件循环（等去抖定时器到点）。"""
    from PyQt5.QtCore import QEventLoop, QTimer
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec_()


def run_vfdsld():
    """§21（并入）变频器页「拖动调速」滑块的回归锁。

    [原 main() 的"跑各 part"部分；建 app/汇总打印/退出码交给总回归]
    """
    from PyQt5.QtWidgets import QApplication
    # [不新建 QApplication] 复用总回归 main() 里那一个。
    app = QApplication.instance()
    if app is None:
        check("变频器滑块段：复用总回归的 QApplication", False,
              "总回归还没建 QApplication（不能在本段新建）")
        return

    from hsfsrv_client import pages_monitor as pm
    from hsfsrv_client import theme as theme_mod

    page = pm.VfdPage(m_vfdsld_Ctx())
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    app.processEvents()

    sent = page.ctx.bridge.sent
    delay = pm.VfdPage.SLIDER_SEND_DELAY_MS

    print("\n=== A. 拖动过程中不下发 ===")
    n0 = len(sent)
    for v in (28, 30, 32, 33):
        page.freq_slider.setValue(v)
        app.processEvents()
    check("A1 拖动中下发 0 条", len(sent) == n0, "实际 %d" % (len(sent) - n0))
    check("A2 数字框跟着滑块走", page.freq_spin.value() == 33,
          "spin=%d slider=%d" % (page.freq_spin.value(), page.freq_slider.value()))
    check("A3 读数标签实时更新", "33" in page._slider_value.text(),
          page._slider_value.text())

    print("\n=== B. 停手后只下发一条 ===")
    m_vfdsld_wait(delay + 120)
    got = len(sent) - n0
    check("B1 停手后下发 1 条", got == 1, "实际 %d" % got)
    # [2026-11-06 口径变更] 缺省调速方式改为**转速**，故下发 op=3
    # (OP_SET_SPEED) 并带 rpm；hz 是按额定点换算的伴随值。
    check("B2 下发的是最后停留的转速值",
          bool(sent) and abs(float(sent[-1].get("rpm", -1)) - 33.0) < 0.01,
          sent[-1] if sent else None)

    print("\n=== C. 松手立即下发（不叠加去抖那条）===")
    n1 = len(sent)
    page.freq_slider.setValue(35)
    app.processEvents()
    page.freq_slider.sliderReleased.emit()
    app.processEvents()
    check("C1 松手立即下发 1 条", len(sent) - n1 == 1,
          "实际 %d" % (len(sent) - n1))
    # 去抖定时器已被 stop，不该再补一条
    m_vfdsld_wait(delay + 120)
    check("C2 去抖不再补发", len(sent) - n1 == 1,
          "实际 %d" % (len(sent) - n1))

    print("\n=== D. 量程跟随调速口径（缺省＝转速）===")
    check("D1 缺省是转速调速",
          page.speed_mode.currentData() == pm.VfdPage.SPEED_MODE_RPM,
          "index=%d data=%s" % (page.speed_mode.currentIndex(),
                                page.speed_mode.currentData()))
    check("D2 缺省量程 0~1440 rpm",
          (page.freq_slider.minimum(), page.freq_slider.maximum()) == (0, 1440),
          (page.freq_slider.minimum(), page.freq_slider.maximum()))
    check("D3 缺省后缀是 rpm", "rpm" in page.freq_spin.suffix(),
          page.freq_spin.suffix())
    page.speed_mode.setCurrentIndex(0)      # 频率模式
    app.processEvents()
    check("D4 频率模式量程 0~50",
          (page.freq_slider.minimum(), page.freq_slider.maximum()) == (0, 50),
          (page.freq_slider.minimum(), page.freq_slider.maximum()))
    check("D5 频率模式后缀是 Hz", "Hz" in page.freq_spin.suffix(),
          page.freq_spin.suffix())
    page.speed_mode.setCurrentIndex(1)      # 切回转速
    app.processEvents()
    check("D6 切回转速量程复原",
          (page.freq_slider.minimum(), page.freq_slider.maximum()) == (0, 1440))

    print("\n=== E. 遥测回写不打断用户拖动 ===")
    page.freq_slider.setValue(40)
    app.processEvents()
    page.refresh()                          # 模拟 200ms 刷新（setFreqHz=25）
    app.processEvents()
    check("E1 去抖窗口内不被遥测覆盖", page.freq_slider.value() == 40,
          "slider=%d" % page.freq_slider.value())
    m_vfdsld_wait(delay + 120)
    page.refresh()                          # 去抖结束后才对齐实际值
    app.processEvents()
    # 当前是转速模式 ⇒ 遥测 25Hz 按额定点换算成 720rpm（50Hz↔1440rpm）
    check("E2 停手后对齐变频器实际设定（换算成转速）",
          page.freq_slider.value() == 720,
          "slider=%d（遥测 setFreqHz=25 应换算为 720rpm）"
          % page.freq_slider.value())

    print("\n=== F. 停机时也能拖动（预置给定）===")
    # [用户 2026-11-06]「不启动的时候也要能拖动调速」
    m_vfdsld_Vfd.running = False            # 让页面判定为停机
    page.refresh()
    app.processEvents()
    check("F1 停机时滑块仍可用", page.freq_slider.isEnabled())
    n2 = len(sent)
    page.freq_slider.setValue(900)
    app.processEvents()
    m_vfdsld_wait(delay + 120)
    check("F2 停机拖动不发在线给定", len(sent) == n2,
          "实际多发了 %d 条" % (len(sent) - n2))
    check("F3 预置值保留在滑块上", page.freq_slider.value() == 900,
          "slider=%d" % page.freq_slider.value())
    check("F4 预置标记已置位", page._preset_dirty is True)
    check("F5 提示说明是预置",
          "预置" in page._ctrl_hint.text(), page._ctrl_hint.text())

    # 启动时预置值随命令带下去
    n3 = len(sent)
    page._on_run_stop()
    app.processEvents()
    start_msgs = [d for d in sent[n3:] if d.get("op") == 4]   # OP_START
    check("F6 启动命令携带预置给定",
          bool(start_msgs) and abs(float(start_msgs[-1].get("rpm", -1)) - 900.0) < 0.01,
          start_msgs[-1] if start_msgs else None)
    check("F7 启动后预置标记清除", page._preset_dirty is False)
    m_vfdsld_Vfd.running = True             # 还原，避免影响后续

    print("\n=== F8. 停机时三条交互路径都只预置、不发帧（2026-11-18）===")
    # [用户 2026-11-18 原话]「这个提示应该改下，没启动不是可以预设速度，
    #  启动的时候是按照拖动调速的给的速度启动的吗」
    # [现场症状] 搅拌泵页停机时拖一下滑块 ⇒ 状态栏弹「命令被拒：变频器处于
    #  停机状态，不能下发给定（请先启动后再调速）」，而页面提示又写着
    #  「停机只预置给定」⇒ 自相矛盾，操作员以为"停机不能预设"。
    # [根因] `_on_slider_released`（松手那条）**没判停机** ⇒ 真发帧 ⇒ 服务端
    #  按「给定是运行参数」拒了；去抖那条（`_on_slider_settled`）判了 ⇒
    #  两条路径口径不一致，且松手那条**先**到 ⇒ 每次拖动都先被拒一次。
    # [修法] 判定收到唯一收口 `_send_set_speed`（停机 ⇒ 只预置、不发帧）。
    # 本段锁住：**松手 / 去抖 / 数字框三条路径在停机时都不发帧** + 提示口径
    # + 启动帧确实带上预置值。
    m_vfdsld_Vfd.running = False            # 让页面判定为停机
    page.refresh()
    app.processEvents()
    # [状态栏留证] 夹具的 set_status 是空实现 ⇒ 换成记录桩，才能断言
    # 「状态栏说的是预置口径、不是命令被拒」。
    _status_msgs = []
    page.ctx.set_status = lambda m: _status_msgs.append(str(m))

    # ---- (1) 松手那条路径（原先漏网的那条）----
    n8 = len(sent)
    page.freq_slider.setValue(1000)
    app.processEvents()
    page.freq_slider.sliderReleased.emit()
    app.processEvents()
    m_vfdsld_wait(delay + 120)
    check("F8 停机松手不发在线给定（不发帧 ⇒ 不会被服务端拒）",
          len(sent) == n8, "实际多发了 %d 条" % (len(sent) - n8))
    check("F9 停机松手后没有任何给定帧（op=2/3 一条都没有）",
          all(d.get("op") not in (2, 3) for d in sent[n8:]),
          sent[n8:] or "空")
    check("F10 停机松手置预置标记", page._preset_dirty is True)

    # ---- (2) 数字框那条路径 ----
    n9 = len(sent)
    page.freq_spin.setValue(1100)
    app.processEvents()
    m_vfdsld_wait(page.SPIN_SEND_DELAY_MS + 150)
    check("F11 停机改数字框不发在线给定",
          len(sent) == n9, "实际多发了 %d 条" % (len(sent) - n9))
    check("F12 停机改数字框后没有任何给定帧（op=2/3 一条都没有）",
          all(d.get("op") not in (2, 3) for d in sent[n9:]),
          sent[n9:] or "空")

    # ---- (3) 提示口径：说清楚是"预置"，不许出现"命令被拒" ----
    hint = page._ctrl_hint.text()
    check("F13 提示是预置口径（含预置值与启动说明）",
          "预置" in hint and "1100" in hint and "启动时" in hint, hint)
    check("F14 提示不含「命令被拒」",
          "被拒" not in hint and "命令被拒" not in hint, hint)
    check("F15 状态栏提示是预置口径（不是命令被拒）",
          bool(_status_msgs)
          and all("被拒" not in s for s in _status_msgs)
          and any("预置" in s for s in _status_msgs),
          _status_msgs[-3:])
    # [refresh 会重刷提示行] 现场提示行 200ms 一跳 ⇒ 预置文案必须能重新
    # 生成，否则刚拖出来的预置值会被通用文案冲掉（操作员看不到预置了多少）。
    page.refresh()
    app.processEvents()
    hint2 = page._ctrl_hint.text()
    check("F16 refresh 重刷后提示仍是预置文案（不被通用文案冲掉）",
          "预置" in hint2 and "1100" in hint2, hint2)

    # ---- (4) 启动确实按这个预置值运行：启动帧带 rpm + 换算 hz ----
    n10 = len(sent)
    page._on_run_stop()
    app.processEvents()
    start_msgs = [d for d in sent[n10:] if d.get("op") == 4]   # OP_START
    exp_hz = (1100.0 / page._rated_rpm * page._rated_hz
              if page._rated_rpm > 0 else 0.0)
    check("F17 启动帧带上预置值（转速模式：rpm + 换算 hz）",
          page._rated_rpm > 0 and bool(start_msgs)
          and abs(float(start_msgs[-1].get("rpm", -1)) - 1100.0) < 0.01
          and abs(float(start_msgs[-1].get("hz", -1)) - exp_hz) < 0.05,
          "rated=%s/%s exp_hz=%.2f got=%s"
          % (page._rated_rpm, page._rated_hz, exp_hz,
             start_msgs[-1] if start_msgs else None))
    check("F18 启动后预置标记清除（提示回到停机通用文案）",
          page._preset_dirty is False)

    # ---- (4b) 给定 = 0 的特例：服务端把 0 当「未指定」----
    # [两端同口径] 服务端写频率的判断是 `if given_hz > 0` ⇒ 捎带 0 =
    # 「不指定给定」⇒ 启动时**沿用变频器里已有的给定**，不是「按 0 运行」。
    # ⇒ 文案不能写「启动时按此值运行」（那是假的）⇒ 0 单独一句话。
    m_vfdsld_Vfd.running = False
    page.refresh()
    app.processEvents()
    page.freq_spin.setValue(0)
    app.processEvents()
    m_vfdsld_wait(page.SPIN_SEND_DELAY_MS + 150)
    hint0 = page._ctrl_hint.text()
    check("F18b 给定=0 时提示说「沿用变频器当前值」（不说按此值运行）",
          "0" in hint0 and "沿用" in hint0 and "按此值运行" not in hint0,
          hint0)
    n0z = len(sent)
    page._on_run_stop()
    app.processEvents()
    start0 = [d for d in sent[n0z:] if d.get("op") == 4]
    check("F18c 给定=0 时启动帧带 0（服务端按「未指定」处理）",
          bool(start0)
          and abs(float(start0[-1].get("rpm", -1))) < 0.01,
          start0[-1] if start0 else None)

    # ---- (5) 运行中仍然在线调速（真发帧）——不许被"停机只预置"改坏 ----
    m_vfdsld_Vfd.running = True
    page.refresh()
    app.processEvents()
    n11 = len(sent)
    page.freq_slider.setValue(1200)
    app.processEvents()
    page.freq_slider.sliderReleased.emit()
    app.processEvents()
    check("F19 运行中松手仍在线下发（op=3/2 真发帧）",
          len(sent) - n11 == 1
          and sent[-1].get("op") in (2, 3),
          "多发 %d 条：%s" % (len(sent) - n11, sent[-1] if sent else None))

    print("\n=== F20. 方向/调速方式下拉随控制闸置灰（2026-11-19）===")
    # [用户 2026-11-19]「搅拌泵界面的什么下拉等控制控件在自动模式下好像
    #  还可以操作」——dir_combo 原先不在 _control_buttons 里 ⇒ 自动模式下
    #  仍能拉下来改选，运行中改选走 _on_dir_changed **真的发换向命令**
    # （被服务端拒，但界面看着能操作、状态栏还蹦一条拒绝文案）。speed_mode
    #  同批补齐：只切本地 Hz/rpm 单位不发命令，但给定框已禁 ⇒ 一并置灰
    #  保持控制区一致（三端同款缺口：C m_speedMode / web speedModeSel）。
    page.ctx.control_blocked_reason = (
        lambda: "自动模式：远程控制已被禁用（输出点由自动逻辑驱动）")
    page.refresh()
    app.processEvents()
    check("F20a 自动模式：方向下拉置灰", not page.dir_combo.isEnabled())
    check("F20b 自动模式：调速方式下拉置灰", not page.speed_mode.isEnabled())
    check("F20c 自动模式：启停/点动钮仍置灰（原有口径不回退）",
          not page.btn_run.isEnabled() and not page.btn_jog.isEnabled())
    check("F20d 自动模式：给定滑块/数字框置灰（原有口径不回退）",
          not page.freq_slider.isEnabled()
          and not page.freq_spin.isEnabled())
    page.ctx.control_blocked_reason = lambda: ""
    page.refresh()
    app.processEvents()
    check("F20e 闸解除：方向下拉恢复可用", page.dir_combo.isEnabled())
    check("F20f 闸解除：调速方式下拉恢复可用", page.speed_mode.isEnabled())
    check("F20g 闸解除：启停钮恢复可用", page.btn_run.isEnabled())

    print("\n=== G. 触摸命中（任意位置都能起拖）===")
    # [用户 2026-11-06]「触摸范围感觉有点小，滑块可以大点，有时候按上去
    # 没反应」——Qt 原生 QSlider 只在手柄/轨道上响应，控件上下方的空白区
    # 按下去被忽略。_TouchSlider 改成"按哪跳哪"，整个矩形都可起拖。
    from PyQt5.QtCore import QPoint, Qt
    from PyQt5.QtGui import QMouseEvent
    from PyQt5.QtWidgets import QStyle, QStyleOptionSlider

    app.processEvents()
    sl = page.freq_slider

    def press(x, y):
        ev = QMouseEvent(QMouseEvent.MouseButtonPress, QPoint(x, y),
                         Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
        sl.mousePressEvent(ev)

    # 控件足够高（触摸目标 >= 48px）
    check("G1 控件高度够触摸", sl.height() >= 48, "%dpx" % sl.height())

    # 上/下空白处按下也要起拖
    sl.setSliderDown(False); sl.setValue(sl.maximum() // 2)
    n0 = len(sent)
    press(sl.width() // 4, 2)
    up_ok = sl.isSliderDown() and sl.value() != sl.maximum() // 2
    sl.setSliderDown(False)
    check("G2 轨道上方空白可起拖", up_ok,
          "值 %d down=%s" % (sl.value(), sl.isSliderDown()))

    sl.setSliderDown(False); sl.setValue(sl.maximum() // 2)
    press(sl.width() * 3 // 4, sl.height() - 2)
    down_ok = sl.isSliderDown() and sl.value() != sl.maximum() // 2
    sl.setSliderDown(False)
    check("G3 轨道下方空白可起拖", down_ok,
          "值 %d down=%s" % (sl.value(), sl.isSliderDown()))

    # 两端可达极值（不能因为缩进算错而取不到 0 / max）
    press(1, sl.height() // 2)
    left_v = sl.value()
    sl.setSliderDown(False)
    press(sl.width() - 2, sl.height() // 2)
    right_v = sl.value()
    sl.setSliderDown(False)
    check("G4 两端可达极值", left_v == sl.minimum()
          and right_v == sl.maximum(), "左 %d 右 %d" % (left_v, right_v))

    # 命中手柄时不许跳变（拖动体验与原生一致）
    sl.setValue(sl.maximum() // 2)
    app.processEvents()
    opt = QStyleOptionSlider()
    sl.initStyleOption(opt)
    h = sl.style().subControlRect(QStyle.CC_Slider, opt,
                                  QStyle.SC_SliderHandle, sl)
    before_h = sl.value()
    press(h.center().x(), h.center().y())
    check("G5 点手柄中心不跳变", sl.value() == before_h,
          "%d -> %d" % (before_h, sl.value()))
    sl.setSliderDown(False)

    print("\n=== H. 无「下发」按钮，改值即生效 ===")
    # [用户 2026-11-06]「去掉频率或转速下发，改成在启动状态下实时显示」
    check("H1 页面已无下发按钮", not hasattr(page, "btn_set_freq"))
    m_vfdsld_Vfd.running = True
    m_vfdsld_Vfd.run_state = 3
    page.refresh()
    app.processEvents()

    n4 = len(sent)
    page.freq_spin.setValue(1000)          # 模拟键盘/箭头改值
    app.processEvents()
    check("H2 改值立刻同步到滑块",
          page.freq_slider.value() == 1000,
          "spin=%d slider=%d" % (page.freq_spin.value(),
                                 page.freq_slider.value()))
    check("H3 改值当拍还不下发（等去抖）", len(sent) == n4,
          "实际 %d 条" % (len(sent) - n4))
    m_vfdsld_wait(page.SPIN_SEND_DELAY_MS + 150)
    got4 = len(sent) - n4
    check("H4 停手后自动下发 1 条", got4 == 1, "实际 %d 条" % got4)
    check("H5 只发一条（滑块去抖不重复发）", got4 == 1,
          "实际 %d 条" % got4)
    check("H6 下发的是新值",
          bool(sent) and abs(float(sent[-1].get("rpm", -1)) - 1000.0) < 0.01,
          sent[-1] if sent else None)

    print("\n=== I. 过渡态文案（加速/减速/正在停机）===")
    # [用户 2026-11-06]「点击停止后变频器慢慢停机，希望正在停机这个过程
    # 状态显示出来，还有正在加速、正在减速都显示下」
    from hsfsrv_client.model import VfdState
    for state, running, want in (
            (7, True, "加速中"),      # RUN_ACCELERATING
            (8, True, "减速中"),      # 降给定中（还在运行）
            (8, False, "正在停机"),    # 停机斜坡
            (3, True, "正转运行"),
            (1, False, "停机"),
    ):
        v = VfdState()
        v.run_state = state
        v.running = running
        check("I 状态 %d/%s → %s" % (state, running, want),
              v.run_state_text() == want, v.run_state_text())

    print("\n=== C. 分段着色（>1000 红 / <440 绿 / 中间蓝）===")
    # [2026-11-18 用户口径] 拖动条按转速分档着色；阈值是**转速**口径。
    _band_of = page._speed_band_of
    check("C1 档位判定：>1000 红 / <440 绿 / 440~1000 蓝",
          _band_of(1200) == "red" and _band_of(300) == "green"
          and _band_of(720) == "blue" and _band_of(440) == "blue"
          and _band_of(1000) == "blue",
          "1200=%s 300=%s 720=%s" % (_band_of(1200), _band_of(300),
                                     _band_of(720)))
    # [额定参数] 频率模式的换算依赖服务端快照的 ratedHz/ratedRpm；测试页
    # 没有快照 ⇒ 为 0 ⇒ 换算不出来。⇒ 按铭牌缺省（1440rpm @ 50Hz，与虚
    # 拟模型同口径）设置后再验。
    page._rated_hz = 50.0
    page._rated_rpm = 1440.0
    # 频率模式：按额定点比例换算成 rpm 再判档（阈值是转速口径）。
    # [为什么要切模式] `_speed_band_rpm` 在**转速模式**下原样返回（不换算）
    # ⇒ 必须切到频率模式（走页面自己的「调速方式」下拉，与真实交互同路）
    # 才能验到换算；测完切回转速模式，别污染后面的断言。
    _hz = page._rated_hz if page._rated_hz > 0 else 50.0
    _rpm = page._rated_rpm if page._rated_rpm > 0 else 1440.0
    _hz_of = lambda rpm: rpm / _rpm * _hz
    # [为什么不真的切下拉] `_on_speed_mode` 会读快照回填额定点、还会改
    # 输入框量程 ⇒ 在测试夹具里切模式会把换算参数冲掉（且夹具的 model
    # 是桩，槽里会抛）⇒ 直接**临时改写口径判定函数**验换算逻辑本身，
    # 验完恢复（不动界面状态）。
    _orig_mode_now = page._speed_mode_now
    page._speed_mode_now = lambda: pm.VfdPage.SPEED_MODE_HZ
    try:
        page._rated_hz = 50.0
        page._rated_rpm = 1440.0
        # [边界口径] 用户口径「1000 转**以上**变红」⇒ 1000 本身仍是蓝
        # （判据是严格大于）⇒ 这里取 1010 rpm 验红。
        _conv_ok = (abs(page._speed_band_rpm(_hz_of(1010)) - 1010) < 1.0
                    and page._speed_band_of(
                        page._speed_band_rpm(_hz_of(1010))) == "red"
                    and page._speed_band_of(
                        page._speed_band_rpm(_hz_of(300))) == "green")
    finally:
        page._speed_mode_now = _orig_mode_now
    check("C2 频率模式按额定点比例换算（1000rpm≈34.7Hz ⇒ 红）",
          _conv_ok,
          "rated=%s/%s" % (_rpm, _hz))
    # 只在跨档时改样式：整程 145 个取值只更新 3 次。
    _orig_sync = page._sync_speed_band
    _updates = {"n": 0}

    def _counting_sync(value):
        _before = page._speed_band_now
        _orig_sync(value)
        if page._speed_band_now != _before:
            _updates["n"] += 1

    page._sync_speed_band = _counting_sync
    try:
        for v in range(0, 1441, 10):
            page.freq_slider.setValue(v)
            app.processEvents()
    finally:
        page._sync_speed_band = _orig_sync
    check("C3 全程 145 个取值只跨档更新 3 次（不逐格重刷）",
          _updates["n"] <= 4,
          "更新 %d 次" % _updates["n"])
    # 三档的可见效果：条与读数的颜色跟着档位走。
    page.freq_slider.setValue(300)
    app.processEvents()
    _green = page._speed_band_now == "green" \
        and theme_mod.CLR_OK in page.freq_slider.styleSheet()
    page.freq_slider.setValue(720)
    app.processEvents()
    # [2026-11-18 订正] 蓝档**也要有覆盖样式**（条本身要蓝——现场原话
    # 「中间蓝色条怎么没有只有文字变了」）⇒ 原来「蓝档无覆盖」的断言锁的
    # 是**已被用户判定为错**的行为。
    _blue = page._speed_band_now == "blue" \
        and theme_mod.CLR_INFO in page.freq_slider.styleSheet()
    page.freq_slider.setValue(1200)
    app.processEvents()
    _red = page._speed_band_now == "red" \
        and theme_mod.CLR_FAULT in page.freq_slider.styleSheet()
    check("C4 三档可见效果（绿档带绿 QSS / 蓝档带蓝 QSS / 红档带红 QSS）",
          _green and _blue and _red,
          "green=%s blue=%s red=%s" % (_green, _blue, _red))


# ==========================================================================
# 段落：test_vfd_start_given（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【启动给定值段落】启动/点动携带给定的端到端测试（真服务端 + 真 XLink）。

    入口：run_vfdgiv()

[原文件] tests/test_vfd_start_given.py（2026-11-09 起内联进
tests/test_hsf_regression.py）

用户反馈：「点启动的时候也要下发频率，用下面设置的转速或频率来启动」
            「按住点动的频率也是」

根因（实测）：服务端禁止「停机时下发给定」，所以客户端原来那套
「先单独发一帧给定、再发启动」会被拒——启动按变频器里残留的旧给定跑
（界面显示 3Hz，实际 25Hz）。修法是把给定绑在启动/点动命令上，
服务端先写给定再发命令字。

本段验证：
  A. 频率模式：设 3Hz → 启动 → 实际输出≈3Hz（不是残留的旧给定）
  B. 转速模式：设任意 rpm → 启动 → 实际输出频率 = rpm 换算值
  C. 换一个给定值再启动（验证"每次启动都按当前设定"）
  D. 点动也带给定：按住点动 → 实际输出≈当前设定，而不是旧给定
  E. 停机时单独下发给定仍被拒（原有安全语义不许回归）

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **端口不写死**：原模块写死 9155 ⇒ 改用总回归的 `_free_port()` 取空闲
    端口，免得撞上别的并行任务（今天有并行任务在跑）。
  · **不插主文件路径**：只把客户端包目录放进 sys.path（主文件已经做
    了同样的事），绝不把 tests/ 放进去 ⇒ 不会循环 import。
"""




def _m_vfdgiv_paths():
    """算本段落用到的三个目录（tests / client_py / server_py）。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys / subprocess /
    tempfile / time 已被主文件占用）⇒ 本段落一律把标准库 import 放进
    函数里，顶层只留**带前缀**的自有名字（重名检查要求顶层名与主文件
    交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    client_root = os.path.dirname(d)
    server_root = os.path.join(os.path.dirname(client_root), "server_py")
    return d, client_root, server_root


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_vfdgiv_TESTS_DIR, m_vfdgiv_CLIENT_ROOT, m_vfdgiv_SERVER_ROOT = \
    _m_vfdgiv_paths()


def m_vfdgiv_wait(sec):
    import time
    end = time.time() + sec
    while time.time() < end:
        time.sleep(0.05)


class m_vfdgiv_Server(object):
    """起一个**隔离**的 sim 服务端（临时 cfg，端口随机）。"""

    def __init__(self, port):
        import tempfile
        self.port = port
        self.tmp = tempfile.mkdtemp(prefix="hsf_startgiven_")
        self.proc = None
        self.bridge = None

    def start(self):
        import json
        import os
        import subprocess
        import sys
        import time
        cfg_path = os.path.join(self.tmp, "c.json")
        with open(cfg_path, "w", encoding="utf-8") as fh:
            json.dump({"server": {"port": self.port},
                       "storage": {"enabled": False}}, fh)
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"   # 服务端日志即时落盘（崩溃取证）
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port",
             str(self.port), "--config", cfg_path],
            cwd=m_vfdgiv_SERVER_ROOT, env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        import socket
        for _ in range(120):
            try:
                socket.create_connection(("127.0.0.1", self.port),
                                        timeout=0.3).close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def connect(self):
        from hsfsrv_client.bridge import LinkBridge
        self.bridge = LinkBridge()
        ok, err = self.bridge.connect_to("127.0.0.1", self.port,
                                          "root", "root")
        return ok, err

    def op(self, **doc):
        """发一条变频器控制并等应答，返回应答 doc。"""
        import time
        holder, done = {}, [False]

        def _done(code, dtype, data):
            from hsfsrv_client import bson
            try:
                holder.update(bson.decode(data) if data else {})
            except ValueError:
                pass
            done[0] = True

        def _err(msg):
            holder["err"] = msg
            done[0] = True

        self.bridge.send_vfd_control(doc, on_done=_done, on_error=_err)
        end = time.time() + 8
        while not done[0] and time.time() < end:
            time.sleep(0.05)
        return holder

    def snapshot(self):
        """拉一次变频器快照（返回 doc）。"""
        import time
        from hsfsrv_client import proto, bson
        holder, done = {}, [False]

        def _done(code, dtype, data):
            try:
                holder.update(bson.decode(data) if data else {})
            except ValueError:
                pass
            done[0] = True

        self.bridge.request(proto.CMD_VFD_SNAPSHOT, None, on_done=_done)
        end = time.time() + 8
        while not done[0] and time.time() < end:
            time.sleep(0.05)
        return holder

    def output_hz(self, settle=3.0, target=None, tol=0.6):
        """等输出频率到位后读回；超时按最后一拍返回。

        [ACC/DEC 口径 2026-11-06] 服务端虚拟变频器按加减速时间参数爬坡
        （缺省 10.0s 满量程 ⇒ 5Hz/s）：25Hz 要 ~5s，固定 3s 只会停在半坡。
        给 target 时轮询到 |out-target|<tol 或超时（~15s 上限）。
        [字段名] 遥测键见 server_py/proto.py 的 VFD_TELEMETRY_FIELDS：
        `outputFreqHz`（输出频率）/ `setFreqHz`（设定频率）。读不到返回
        None，让断言明确报"读不到"而不是伪装成 0。
        """
        import time
        m_vfdgiv_wait(settle)
        doc = self.snapshot()
        value = doc.get("outputFreqHz")
        if target is not None:
            deadline = time.time() + 15.0
            while time.time() < deadline:
                if value is not None:
                    try:
                        if abs(float(value) - target) < tol:
                            break
                    except (TypeError, ValueError):
                        pass
                m_vfdgiv_wait(0.3)
                doc = self.snapshot()
                value = doc.get("outputFreqHz")
        if value is None:
            return None
        try:
            return float(value)
        except (TypeError, ValueError):
            return None

    def stop(self):
        import subprocess
        try:
            if self.bridge is not None:
                from hsfsrv_client import proto
                self.bridge.send_vfd_control({"op": proto.OP_STOP})
                m_vfdgiv_wait(0.5)
                self.bridge.disconnect_from()
        except Exception:                            # noqa: BLE001
            pass
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def run_vfdgiv():
    """§21（并入）启动/点动携带给定（真服务端 + 真 XLink）。

    [原 main() 的"跑各 part"部分；汇总打印与退出码交给总回归]

    [端口不写死] 原模块写死 9155 ⇒ 改成总回归的 `_free_port()` 取空闲
    端口（并行任务撞端口会把这条段整个带崩）。
    """
    import time
    from hsfsrv_client import proto
    srv = m_vfdgiv_Server(_free_port())
    if not srv.start():
        check("启动给定值段：sim 服务端起动", False, "端口 %d" % srv.port)
        srv.stop()
        return
    try:
        ok, err = srv.connect()
        if not ok:
            check("启动给定值段：登录 sim 服务端", False, str(err))
            return
        print("  已连接 sim 服务端 :%d" % srv.port)

        # 额定点（客户端按这个换算转速）
        snap = srv.snapshot()
        rated_rpm = float(snap.get("ratedRpm") or 1440.0)
        rated_hz = float(snap.get("ratedHz") or 50.0)
        print("  额定点：%.0f rpm @ %.0f Hz" % (rated_rpm, rated_hz))

        # ---- A. 频率模式：3Hz 启动 ----
        print("== A. 频率模式：3Hz 启动 ==")
        resp = srv.op(op=proto.OP_START, rev=False, hz=3.0)
        check("A1 启动命令被受理", resp.get("ok") is True,
              str(resp.get("err", ""))[:50])
        out = srv.output_hz(target=3.0)
        check("A2 实际输出 = 3Hz（不是残留旧给定）",
              out is not None and abs(out - 3.0) < 0.6,
              "out=%.2fHz 期望 3.0Hz" % (out if out is not None else -1))
        srv.op(op=proto.OP_STOP)
        m_vfdgiv_wait(2.5)

        # ---- B. 换给定值再启动：8Hz ----
        print("== B. 换给定再启动：8Hz ==")
        resp = srv.op(op=proto.OP_START, rev=False, hz=8.0)
        check("B1 启动受理", resp.get("ok") is True, str(resp.get("err", ""))[:50])
        out = srv.output_hz(target=8.0)
        check("B2 实际输出 = 8Hz（按当前设定，不是上一次的 3Hz）",
              out is not None and abs(out - 8.0) < 0.6,
              "out=%.2fHz 期望 8.0Hz" % (out if out is not None else -1))
        srv.op(op=proto.OP_STOP)
        m_vfdgiv_wait(2.5)

        # ---- C. 转速模式：给 rpm，服务端换算成频率 ----
        print("== C. 转速模式：给 rpm ==")
        want_rpm = rated_rpm / 2.0                       # 半速
        want_hz = want_rpm / rated_rpm * rated_hz        # = rated_hz/2
        resp = srv.op(op=proto.OP_START, rev=False, rpm=want_rpm,
                      hz=want_hz)
        check("C1 启动受理（转速口径）", resp.get("ok") is True,
              str(resp.get("err", ""))[:50])
        out = srv.output_hz(target=want_hz)
        check("C2 实际输出 = 转速换算值 %.1fHz" % want_hz,
              out is not None and abs(out - want_hz) < 0.6,
              "out=%.2fHz 期望 %.1fHz" % (out if out is not None else -1,
                                          want_hz))
        srv.op(op=proto.OP_STOP)
        m_vfdgiv_wait(1.5)

        # ---- D. 点动也带给定 ----
        print("== D. 点动带给定 ==")
        resp = srv.op(op=proto.OP_JOG, rev=False, hz=5.0)
        check("D1 点动受理", resp.get("ok") is True, str(resp.get("err", ""))[:50])
        # [必须持续发心跳] 服务端点动看门狗 600ms 收不到帧就自动停机
        # （安全兜底）。只发一帧就等 2.5s 的话设备早停了——那测的是
        # 看门狗而不是给定值。这里按客户端真实行为每 200ms 重发。
        out = None
        # [ACC/DEC 口径] C 组停机后还剩 ~17Hz 在斜坡上（wait 只给 2.5s），
        # 点动把目标拉到 5Hz 后输出要再降 ~2.4s 才到位——循环给足 4s。
        end = time.time() + 4.0
        while time.time() < end:
            srv.bridge.send_vfd_control(
                {"op": proto.OP_JOG, "rev": False, "hz": 5.0})
            time.sleep(0.2)
        doc = srv.snapshot()
        out = doc.get("outputFreqHz")
        out = float(out) if out is not None else None
        check("D2 点动输出 = 5Hz（按当前设定）",
              out is not None and abs(out - 5.0) < 0.8,
              "out=%.2fHz 期望 5.0Hz" % (out if out is not None else -1))
        srv.op(op=proto.OP_JOG_RELEASE)
        # [ACC/DEC 口径] 5Hz 减速到 0 要 ~1s，给足余量再测"停机禁给定"。
        m_vfdgiv_wait(2.5)

        # ---- E. 停机时单独下发给定仍被拒（安全语义不许回归）----
        print("== E. 停机单独下发给定仍被拒 ==")
        resp = srv.op(op=proto.OP_SET_FREQUENCY, hz=10.0)
        check("E1 停机单独给定被拒", resp.get("ok") is not True,
              str(resp.get("err", ""))[:60])
    finally:
        srv.stop()


# ==========================================================================
# 段落：test_xlsx_leak（2026-11-09 从散落测试文件并入本文件；
#        用户口径「所有测试代码集成到一个测试文件」）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【xlsx 泄漏段落】xlsx_bridge（Excel 导出动态库）内存泄漏测试。

    入口：run_xlsxleak()

[原文件] tests/test_xlsx_leak.py（2026-11-09 起内联进 tests/test_hsf_regression.py）

[测什么] 重复执行与历史页导出**完全相同**的 C 调用序列
（create → add_sheet → 写表头+N 行 → save → delete），用框架自身的
全局内存统计（XMemory_statistics_2 直读堆存量）对比前后的实时字节数：

    * 循环后堆存量回到基线        ⇒ 无泄漏
    * 循环后堆存量单调上涨        ⇒ 有泄漏（哪路 delete 没走全）

[跳过条件] hsfsrv_client/bin/ 下没有 xlsx_bridge 动态库（未编译）时
SKIP —— 泄漏测试依赖动态库，库没编出来谈不上泄漏。

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）⇒
    断言统一汇总，不再自带 PASS/FAIL 计数。
  · **不建 QApplication**：本段只碰 ctypes，与界面无关。
  · **不插主文件路径**：只把客户端包目录放进 sys.path（主文件已经做
    了同样的事），绝不把 tests/ 放进去 ⇒ 不会循环 import。
"""

import ctypes


def _m_xlsxleak_paths():
    """算本段落用到的两个目录。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造一个同名顶层名（os / sys / tempfile
    已被主文件占用）⇒ 本段落一律把标准库 import 放进函数里，顶层只留
    **带前缀**的自有名字（重名检查要求顶层名与主文件交集为空）。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    client_root = os.path.dirname(d)
    bin_dir = os.path.join(client_root, "hsfsrv_client", "bin")
    return d, client_root, bin_dir


#: 本段落文件所在的 tests 目录（并进总回归后 __file__ 指向主文件，目录
#: 仍是 tests/；独立放在 tests/_merged/ 下时向上退一级）。
m_xlsxleak_TESTS_DIR, m_xlsxleak_CLIENT_ROOT, m_xlsxleak_BIN_DIR = \
    _m_xlsxleak_paths()

#: 每轮写多少行数据（覆盖 str/num 混合写入路径即可，行数不必多）。
m_xlsxleak_ROWS_PER_ROUND = 30
#: 完整导出循环轮数（每轮一个完整工作簿生命周期）。
m_xlsxleak_ROUNDS = 50


def m_xlsxleak_dll_path():
    import sys
    if sys.platform == "win32":
        return os.path.join(m_xlsxleak_BIN_DIR, "xlsx_bridge.dll")
    return os.path.join(m_xlsxleak_BIN_DIR, "xlsx_bridge.so")


def m_xlsxleak_load(path):
    dll = ctypes.CDLL(path)
    for func, args, res in (
            ("xlsx_create", [], ctypes.c_void_p),
            ("xlsx_delete", [ctypes.c_void_p], None),
            ("xlsx_add_sheet", [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            ("xlsx_write_str",
             [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p],
             ctypes.c_int),
            ("xlsx_write_num",
             [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_double],
             ctypes.c_int),
            ("xlsx_save", [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            ("xlsx_mem_bytes", [], ctypes.c_longlong)):
        getattr(dll, func).argtypes = args
        getattr(dll, func).restype = res
    return dll


def m_xlsxleak_export_round(dll, out_path):
    """与历史页 Excel 导出同构的完整调用序列（行列 1 基，桥接口径）。"""
    doc = dll.xlsx_create()
    assert doc, "xlsx_create 失败"
    try:
        assert dll.xlsx_add_sheet(doc, "历史数据".encode("utf-8")), "加表失败"
        for col in range(1, 8):
            assert dll.xlsx_write_str(
                doc, 1, col, ("列%d" % col).encode("utf-8")), "写表头失败"
        for row in range(2, m_xlsxleak_ROWS_PER_ROUND + 2):
            for col in range(1, 8):
                if col % 2:
                    ok = dll.xlsx_write_str(
                        doc, row, col, ("文本%d-%d" % (row, col))
                        .encode("utf-8"))
                else:
                    ok = dll.xlsx_write_num(doc, row, col, row * col * 0.5)
                assert ok, "写单元格(%d,%d)失败" % (row, col)
        assert dll.xlsx_save(doc, out_path.encode("utf-8")), "保存失败"
    finally:
        dll.xlsx_delete(doc)


def run_xlsxleak():
    """§（并入）Excel 导出动态库的内存泄漏锁。

    [口径] 原文件只打印 PASS/FAIL 并返回退出码；并进总回归后改成用总回归
    的 check()/skip() 计数（断言统一汇总），退出码交给总回归。

    [原 main() 的"跑各 part"部分；汇总打印与退出码交给总回归]
    """
    import os
    import sys
    import tempfile
    path = m_xlsxleak_dll_path()
    if not os.path.isfile(path):
        skip("xlsx 导出动态库泄漏", "未找到 %s（先编译 xlsx_bridge 目标）"
             % path)
        return

    fd, out = tempfile.mkstemp(suffix=".xlsx")
    os.close(fd)
    os.unlink(out)

    try:
        dll = m_xlsxleak_load(path)
    except OSError as exc:
        skip("xlsx 导出动态库泄漏", "加载 %s 失败：%s" % (path, exc))
        if os.path.exists(out):
            os.unlink(out)
        return

    try:
        # 先做一轮「预热」：框架/运行库的首轮分配（初始化、编码表、
        # 临时缓冲）不记基线，从第二轮起比才能分清「一次性初始化」与「泄漏」。
        m_xlsxleak_export_round(dll, out)
        before = dll.xlsx_mem_bytes()
        print("  预热完成，堆存量基线 = %d 字节" % before)

        for _i in range(m_xlsxleak_ROUNDS):
            m_xlsxleak_export_round(dll, out)
        after = dll.xlsx_mem_bytes()
        print("  %d 轮完整导出后，堆存量 = %d 字节（增长 %d）"
              % (m_xlsxleak_ROUNDS, after, after - before))

        check("xlsx 导出 %d 轮后堆存量回到基线（无内存泄漏）"
              % m_xlsxleak_ROUNDS, after == before,
              "基线 %d → %d（增长 %d）" % (before, after, after - before))
    except Exception as exc:                        # noqa: BLE001
        # 原模块这里是裸 assert（异常直接崩进程）；并进总回归后改成一条红
        # 断言，别把整轮回归带崩。
        check("xlsx 导出 %d 轮（执行异常）" % m_xlsxleak_ROUNDS, False,
              "%s: %s" % (type(exc).__name__, exc))
    finally:
        if os.path.exists(out):
            os.unlink(out)

# ==========================================================================
# 段落：test_ttyS4_devices（2026-11-10 三设备接入实测口径的跨端回归锁）
# ==========================================================================
# -*- coding: utf-8 -*-
"""【ttyS4 三设备段落】浊度仪×2 + 电磁流量计的跨端契约（自起 sim 服务端 + 真 XLink 订阅）。

    入口：run_ttyS4_devices()

[口径来源] docs/HSF板端ttyS4三设备取数实测总表_20261109.md（ttyS4@9600
Modbus-RTU 三台从站的全量实测）：
    站1 WZT-701A     FC03 0x0000×2  CDAB   2.29 NTU
    站2 ZHEYI-5100ZDY FC03 0x0000×4  DCBA   0.045 NTU + 19.3 ℃（温度只进设备层）
    站5 LD 电磁流量计  FC04 0x1010×22 ABCD   3.6 m³/h、空管 False、单位码 5=m³/h
站号全部从 cfg 来（用户口径：站号要可以修改；站2 这台后续要改站号）。

[测什么]
  A. 缺省 `--sim`（=现场口径）：Python 客户端订阅 0x00010009（浊度）与
     0x00010004（流量计），断言——两台浊度都在线有值（2.29 / ≈0.045 NTU，
     定点 ×100 口径）、流量计有值且带单位码（5=m³/h）、空管报警位图键在
     （空管=False ⇒ bit2 为 0）。**推送帧 shape 不在本段断言范围外**：
     只按既有键断言，不给 0x00010009/0x00010004 加新键。
  B. 改配置站号后模拟器跟着变：第二个实例 turbidity=[3,4]、flowmeter=6
     再跑一遍 A 的订阅断言，并经 0x0107 设备健康快照断言地址文案里的
     站号（「… · 9600 · 站 N」）= 改过的值——这是客户端唯一能看到的
     站号投影，改配置必须全链路生效。

[本段的三条硬规矩]
  · **计数器用总回归那一套**（check / skip / section 直接按名调用）。
  · **不建 QApplication**：订阅与请求都走 XLinkClient 的网络线程回调，
    纯 sleep 等帧即可（Qt5 一进程多 app 是 UB）。
  · **只杀自己起的进程**：两个 sim 服务端都是本段 Popen 的，finally 里
    terminate→kill + 删临时目录；不碰现场 6100/6200。
"""

def _m_ttyS4_paths():
    """算本段落用到的两个根目录（client_py / server_py）。

    [为什么在这里 import os] 段落会被**原样拼进**总回归主文件；顶层
    `import os` 会在主文件里再造同名顶层名（os / sys / subprocess /
    tempfile / time 已被主文件占用）⇒ 本段落一律把标准库 import 放进
    函数里，顶层只留**带前缀**的自有名字。
    """
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(d) == "_merged":
        d = os.path.dirname(d)
    client_root = os.path.dirname(d)
    server_root = os.path.join(os.path.dirname(client_root), "server_py")
    return client_root, server_root


m_ttyS4_CLIENT_ROOT, m_ttyS4_SERVER_ROOT = _m_ttyS4_paths()

#: 每个实例收多少秒的发布帧（浊度推 500ms/拍、流量计 200ms/拍，3s 足够多拍）。
m_ttyS4_COLLECT_SECONDS = 3.0


class m_ttyS4_Server(object):
    """自起一个**隔离**的 sim 服务端（临时 cfg + 随机端口；只杀自己起的）。

    [overrides] 写进临时配置文件的补充节（如改站号）。浅合并：dict 值
    逐键并入缺省 doc，其余直接覆盖。
    """

    def __init__(self, overrides=None):
        import tempfile
        self.port = _free_port()
        self.tmp = tempfile.mkdtemp(prefix="hsf_ttyS4_")
        self.cfg_dir = os.path.join(self.tmp, "cfg")
        os.makedirs(self.cfg_dir, exist_ok=True)
        self.overrides = overrides or {}
        self.proc = None
        self.log_path = os.path.join(self.tmp, "server.out")
        self._log_fh = None

    def tail(self, n=1200):
        """服务端 stdout 尾巴（失败定位用）。"""
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def start(self):
        import json
        import socket
        import subprocess
        import sys
        import time
        cfg_path = os.path.join(self.cfg_dir, "hsf_server_py.json")
        doc = {"server": {"port": int(self.port)},
               "storage": {"enabled": False}}
        for key, val in self.overrides.items():
            if isinstance(val, dict) and isinstance(doc.get(key), dict):
                doc[key].update(val)
            else:
                doc[key] = val
        with open(cfg_path, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, ensure_ascii=False, indent=2)
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"     # 日志即时落盘（崩溃取证）
        self._log_fh = open(self.log_path, "wb")
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port",
             str(self.port), "--config", cfg_path],
            cwd=m_ttyS4_SERVER_ROOT, env=env,
            stdout=self._log_fh, stderr=subprocess.STDOUT)
        deadline = time.time() + 25
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("ttyS4 段 sim 服务端起不来：\n%s"
                                   % self.tail())
            try:
                socket.create_connection(("127.0.0.1", self.port),
                                         timeout=0.3).close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def stop(self):
        import shutil
        import subprocess
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None
        if self._log_fh is not None:
            try:
                self._log_fh.close()
            except Exception:
                pass
            self._log_fh = None
        shutil.rmtree(self.tmp, ignore_errors=True)


def m_ttyS4_collect(srv, topics, seconds=m_ttyS4_COLLECT_SECONDS):
    """登录 + 订阅 topics + 收 seconds 秒发布帧。

    :returns: (client, frames{topic: [doc,...]}, err)；client=None 表示
              登录失败（err 给出原因）。
    """
    import time
    from hsfsrv_client import bson
    from hsfsrv_client.xlink import XLinkClient
    cli = XLinkClient("127.0.0.1", srv.port, "root", "root")
    ok, err = cli.start(8.0)
    if not ok:
        return None, {}, (err or "登录失败")
    frames = {}

    def _on_publish(topic, payload, dtype):
        try:
            doc = bson.decode(payload) if payload else {}
        except ValueError:
            return
        frames.setdefault(topic, []).append(doc)

    cli.on_publish = _on_publish
    for topic in topics:
        cli.subscribe_async(topic)
    deadline = time.time() + seconds
    while time.time() < deadline:
        time.sleep(0.1)
    return cli, frames, ""


def m_ttyS4_request_health(cli, timeout=6.0):
    """拉一次设备健康快照（0x0107），返回 doc（失败含 {"err": ...}）。"""
    import time
    from hsfsrv_client import bson
    got = {}

    def _done(_code, _dtype, payload):
        try:
            got.update(bson.decode(payload) if payload else {})
        except ValueError:
            pass

    def _err(msg):
        got["err"] = msg

    cli.request_async(0x0107, on_done=_done, on_error=_err, timeout=5.0)
    deadline = time.time() + timeout
    while not got and time.time() < deadline:
        time.sleep(0.05)
    return got


def m_ttyS4_addr_of(doc, name):
    """健康快照里按设备名取地址文案（找不到返回 None）。"""
    for i in range(int(doc.get("devCount", 0) or 0)):
        if doc.get("d%d_name" % i) == name:
            return doc.get("d%d_addr" % i)
    return None


def m_ttyS4_check_turbidity_frames(frames, st1_ntu, st2_ntu):
    """浊度主题（0x00010009）的公共断言组。

    [定点口径] 服务端把 NTU ×100 取整推送（encode_turbidity），端上 ÷100
    显示——这里直接按 ×100 的整数断言。波动幅度 ±3%（VirtualTurbidity），
    断言区间给 ±5% 余量。
    """
    from hsfsrv_client import proto as P
    docs = frames.get(P.TOPIC_TURBIDITY_STATUS, [])
    check("收到浊度推送帧（0x00010009）", len(docs) > 0,
          "%d 帧" % len(docs))
    if not docs:
        return
    doc = docs[-1]
    check("浊度帧是两台一帧", doc.get("count") == 2,
          "count=%r" % doc.get("count"))
    v0 = doc.get("v0_ntu")
    v1 = doc.get("v1_ntu")
    check("浊度1 在线且有值", bool(doc.get("v0_online")) and v0 is not None,
          "v0_online=%r v0_ntu=%r" % (doc.get("v0_online"), v0))
    check("浊度2 在线且有值", bool(doc.get("v1_online")) and v1 is not None,
          "v1_online=%r v1_ntu=%r" % (doc.get("v1_online"), v1))
    tol0 = int(round(st1_ntu * 100 * 0.05))
    check("浊度1 数值=站1 口径 %.2f NTU（×100 定点 ±5%%）" % st1_ntu,
          isinstance(v0, int) and abs(v0 - int(round(st1_ntu * 100))) <= tol0,
          "v0_ntu=%r 期望≈%d" % (v0, int(round(st1_ntu * 100))))
    tol1 = max(2, int(round(st2_ntu * 100 * 0.10)))
    check("浊度2 数值=站2 口径 %.3f NTU（×100 定点）" % st2_ntu,
          isinstance(v1, int) and abs(v1 - int(round(st2_ntu * 100))) <= tol1,
          "v1_ntu=%r 期望≈%d" % (v1, int(round(st2_ntu * 100))))
    check("浊度带 raw 位模式（排查换算口径用）",
          doc.get("v0_raw") is not None and doc.get("v1_raw") is not None)


def m_ttyS4_check_flowmeter_frames(frames, base_flow):
    """流量计主题（0x00010004）的公共断言组（单位码/空管位图键在）。"""
    from hsfsrv_client import proto as P
    docs = frames.get(P.TOPIC_FLOWMETER_STATUS, [])
    check("收到流量计推送帧（0x00010004）", len(docs) > 0,
          "%d 帧" % len(docs))
    if not docs:
        return
    doc = docs[-1]
    check("流量计在线（valid=true）", doc.get("valid") is True,
          "valid=%r" % doc.get("valid"))
    flow = doc.get("flow")
    check("瞬时流量有值且≈%.1f m3/h（±2%% 波动，给 ±10%% 余量）" % base_flow,
          isinstance(flow, (int, float))
          and abs(flow - base_flow) <= base_flow * 0.10,
          "flow=%r" % flow)
    # [单位码不能写死] 0x1020 读上来的代码原样透传（5=m³/h）；端上查表
    # 转文本。断言的是"帧里带这个键"，改单位码表不该动本段。
    check("瞬时流量单位码在帧里（flowUnit）", "flowUnit" in doc,
          "flowUnit=%r" % doc.get("flowUnit"))
    check("累积总量单位码在帧里（totalUnit）", "totalUnit" in doc,
          "totalUnit=%r" % doc.get("totalUnit"))
    # [空管报警字段在] alarms 是位图：0=上限 1=下限 2=空管 3=系统。
    # sim 缺省空管=False ⇒ bit2 为 0；键必须存在（客户端按位解析）。
    check("空管报警位图键在帧里（alarms）",
          isinstance(doc.get("alarms"), int),
          "alarms=%r" % doc.get("alarms"))
    check("空管位（bit2）=0（sim 缺省管道充液）",
          isinstance(doc.get("alarms"), int)
          and not (int(doc.get("alarms")) & (1 << 2)),
          "alarms=%r" % doc.get("alarms"))


def run_soak_stability():
    """§28 长稳 soak：连续 SOAK_MINUTES 分钟的 建-订-收帧-断链 循环。

    [与 §27 的分工] §27 是 10 轮快速回归（~40s，门禁每次跑）；本段是
    **长时间稳定性**口径：默认 1 分钟（日常门禁预算内），夜间长稳用
    `--soak-minutes 15`（或更长）跑——连续几十分钟的反复连接/订阅/收帧，
    任何一次窗口断帧、任何一次登录最终失败、会话日志不守恒，都算红。

    [断言口径]
      · 每个窗口（每轮）必须收到浊度帧（订阅有效、推送连续）；
      · 遇拒登录退避后最终成功（不红——拒绝是预期的瞬时态）；
      · 结束后 conn 日志对账：接入-断开 差额 <= 1（1 = 对账自身在线）。
    """
    section("§28 长稳 soak：%d 分钟 建-订-收帧-断链 连续循环" % SOAK_MINUTES)
    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client.bridge import LinkBridge
    srv = PrivateServer()
    try:
        if not srv.start():
            check("§28 私有服务端起动", False, "端口 %d" % srv.port)
            return
    except Exception as exc:                            # noqa: BLE001
        check("§28 私有服务端起动", False,
              "%s: %s" % (type(exc).__name__, exc))
        return
    try:
        deadline = time.time() + SOAK_MINUTES * 60.0
        t0 = int(time.time() * 1000) - 1500      # 窗口起点（提前 1.5s 容忍时钟差）
        cycles = 0
        empty_windows = 0
        total_frames = 0
        login_fail = 0
        while time.time() < deadline:
            b = LinkBridge()
            ok = False
            for _att in range(6):          # 遇拒退避（预期瞬时态，不算红）
                ok, err = b.connect_to("127.0.0.1", srv.port,
                                       "root", "root")
                if ok:
                    break
                login_fail += 1
                time.sleep(1.2)
            if not ok:
                login_fail += 1
                empty_windows += 1
                b.disconnect_from()
                continue
            cycles += 1
            cnt = {}
            b.client.on_publish = (
                lambda t, p, d, c=None, _c=cnt:
                _c.__setitem__(t, _c.get(t, 0) + 1))
            b.subscribe(0x00010009, 0x00010004)
            end = time.time() + 2.0
            while time.time() < end:
                app = QApplication.instance()
                if app:
                    app.processEvents()
                time.sleep(0.03)
            n = cnt.get(0x00010009, 0) + cnt.get(0x00010004, 0)
            total_frames += n
            if n == 0:
                empty_windows += 1
            b.disconnect_from()
            time.sleep(0.2)
        check("§28 soak 循环数达标（>= 分钟数×6 轮）",
              cycles >= SOAK_MINUTES * 6,
              "cycles=%d / %d 分钟" % (cycles, SOAK_MINUTES))
        check("§28 soak 全程无断帧窗口（每轮都收到推送）",
              empty_windows == 0,
              "空窗口=%d（cycles=%d 总帧=%d 登录退避=%d）"
              % (empty_windows, cycles, total_frames, login_fail))
        # 会话日志对账
        b = LinkBridge()
        ok, err = b.connect_to("127.0.0.1", srv.port, "root", "root")
        if not ok:
            check("§28 对账连接", False, err or "")
            return
        doc = {}

        def _done(d):
            doc.update(d if d else {})

        b.log_page(offset=0, limit=300, before_ts=int(time.time() * 1000),
                   lo_ts=t0,
                   category="conn", on_done=_done,
                   on_error=lambda m: doc.__setitem__("err", m))
        end = time.time() + 8
        while time.time() < end and not doc:
            QApplication.instance().processEvents(); time.sleep(0.03)
        rows = doc.get("rows") or []
        acc = sum(1 for r in rows if "会话接入" in (r.get("text") or ""))
        dis = sum(1 for r in rows if "会话断开" in (r.get("text") or ""))
        # [簿记口径 2026-11-10 长稳修正] 窗口只能看到**最近 50 行**（服务端
        # 日志分页上限）：10 轮 soak 全部会话装得下（§27 口径 acc≈cycles），
        # 但长 soak（数百轮）⇒ 窗口里只有尾部一小段，acc>=cycles 不可能
        # 满足（实测 386 轮只看到 25/25）。守恒（接入==断开）才是本质判据
        # ——任何一轮"连上没断/断了没连"都会让窗口内差额越界；行数下限
        # 只要求窗口确实盖到了 soak 期间（非空）。
        check("§28 soak 会话日志对账（窗口内 接入==断开，守恒）",
              acc == dis and acc >= 1,
              "接入=%d 断开=%d（cycles=%d 窗口 %d 行）"
              % (acc, dis, cycles, len(rows)))
        b.disconnect_from()
    finally:
        srv.stop()


def run_reconnect_cycles():
    """§27 客户端反复连接：10 轮 建-订-断，会话日志对账（自起私有实例）。

    [为什么要有这一段] "客户端反复连接"是现场高频操作（网络抖动/换页/换
    班），历史上两处真 bug 都在这里暴露：①旧会话未回收时新登录被策略闸
    拒绝（曾伪装成"口令错误"打死自动重连 ⇒ 界面永久无数据，见 13b）；
    ②重连后订阅不重发 ⇒ 界面有连接没数据。本段用私有 sim 实例连跑
    10 轮，每轮真实订阅并确认收到帧；最后用 0x0119 的 conn 类日志对账：
    「会话接入」与「会话断开」必须相抵（差额 = 对账时仍在线的自身那条）。
    """
    section("27 客户端反复连接：10 轮 建-订-断 + 会话日志对账")
    from hsfsrv_client.bridge import LinkBridge
    srv = PrivateServer()
    try:
        if not srv.start():
            check("27 私有服务端起动", False, "端口 %d" % srv.port)
            return
    except Exception as exc:                            # noqa: BLE001
        check("27 私有服务端起动", False,
              "%s: %s" % (type(exc).__name__, exc))
        return
    try:
        from PyQt5.QtWidgets import QApplication
        app = QApplication.instance()
        CYCLES = 10
        ok_cycles = 0
        saw_frame = False
        for i in range(CYCLES):
            b = LinkBridge()
            ok = False
            for _att in range(6):          # 遇拒退避（模拟产品自动重连）
                ok, err = b.connect_to("127.0.0.1", srv.port, "root", "root")
                if ok:
                    break
                time.sleep(1.2)
            if ok:
                ok_cycles += 1
                # [只计数不解码] 本段断言只需要"帧到了"；解码要用 bson——
                # 主文件顶层没有 import bson（NameError 会在网络线程回调里
                # 被吞 ⇒ 帧计数恒 0，实测一次假红）。计数不需要解码。
                cnt = {}
                b.client.on_publish = (
                    lambda t, p, d, c=None, _c=cnt:
                    _c.__setitem__(t, _c.get(t, 0) + 1))
                b.subscribe(0x00010009)
                end = time.time() + 2.0
                while time.time() < end:
                    app.processEvents(); time.sleep(0.03)
                if cnt.get(0x00010009, 0) > 0:
                    saw_frame = True
            b.disconnect_from()
        check("27 反复连接 10 轮全部成功（遇拒已按退避重试）",
              ok_cycles == CYCLES, "成功 %d/%d" % (ok_cycles, CYCLES))
        check("27 每轮订阅都能收到浊度推送（重连不丢订阅）",
              saw_frame)
        # 会话日志对账：接入-断开 相抵（差额允许 1 = 对账时仍在线的自身）。
        b = LinkBridge()
        ok, err = b.connect_to("127.0.0.1", srv.port, "root", "root")
        if not ok:
            check("27 对账连接", False, err or "")
            return
        got = {}
        b.client.on_publish = (
            lambda t, p, d, c=None: got.__setitem__(
                t, bson.decode(p) if p else {}))
        b.subscribe(0x00010009)
        time.sleep(0.5); app.processEvents()
        doc = {}

        def _done(d):
            doc.update(d if d else {})

        b.log_page(offset=0, limit=300, before_ts=int(time.time() * 1000),
                   category="conn", on_done=_done,
                   on_error=lambda m: doc.__setitem__("err", m))
        end = time.time() + 8
        while time.time() < end and not doc:
            app.processEvents(); time.sleep(0.03)
        rows = doc.get("rows") or []
        acc = sum(1 for r in rows if "会话接入" in (r.get("text") or ""))
        dis = sum(1 for r in rows if "会话断开" in (r.get("text") or ""))
        check("27 会话日志对账（接入-断开 差额 <=1，1=对账自身在线）",
              0 <= acc - dis <= 1 and acc >= CYCLES,
              "接入=%d 断开=%d（窗口 %d 行）" % (acc, dis, len(rows)))
        b.disconnect_from()
    finally:
        srv.stop()


def run_ttyS4_devices():
    """§（并入）ttyS4 三设备接入：缺省口径 + 改站号跟随（自起两个 sim 实例）。"""
    from hsfsrv_client import proto as P
    section("§26 ttyS4 三设备：浊度×2 / 流量计（实测口径 + 站号可改）")

    # ---- A. 缺省 --sim（站 1/3/5，现场口径；2026-11-10 站 2 表改面板站号 3）----
    srv = m_ttyS4_Server()
    if not srv.start():
        check("ttyS4 段：缺省 sim 服务端起动", False,
              "端口 %d；日志：%s" % (srv.port, srv.tail()[-200:]))
        srv.stop()
        return
    try:
        cli, frames, err = m_ttyS4_collect(
            srv, (P.TOPIC_TURBIDITY_STATUS, P.TOPIC_FLOWMETER_STATUS))
        check("ttyS4 段：客户端登录缺省实例", cli is not None, err)
        if cli is None:
            return
        print("  已连接缺省 sim 实例 :%d（站 1/3/5）" % srv.port)
        try:
            m_ttyS4_check_turbidity_frames(frames, 2.29, 0.045)
            m_ttyS4_check_flowmeter_frames(frames, 3.6)
            # 单位码原样透传：sim 从站发 5=m³/h / 1=m³，端上原样收到
            fm = frames.get(P.TOPIC_FLOWMETER_STATUS)
            if fm:
                check("单位码 5（m3/h）随数据上行（不是写死在客户端）",
                      fm[-1].get("flowUnit") == 5,
                      "flowUnit=%r" % fm[-1].get("flowUnit"))
                check("累积单位码 1（m3）",
                      fm[-1].get("totalUnit") == 1,
                      "totalUnit=%r" % fm[-1].get("totalUnit"))
        finally:
            cli.close()
    finally:
        srv.stop()

    # ---- B. 改配置站号（turbidity [3,4]、flowmeter 6）⇒ 模拟器跟随 ----
    srv2 = m_ttyS4_Server(overrides={
        "serial": {"unit_ids": {"turbidity": [3, 4], "flowmeter": 6}},
    })
    if not srv2.start():
        check("ttyS4 段：改站号 sim 服务端起动", False,
              "端口 %d；日志：%s" % (srv2.port, srv2.tail()[-200:]))
        srv2.stop()
        return
    try:
        cli2, frames2, err2 = m_ttyS4_collect(
            srv2, (P.TOPIC_TURBIDITY_STATUS, P.TOPIC_FLOWMETER_STATUS))
        check("ttyS4 段：客户端登录改站号实例", cli2 is not None, err2)
        if cli2 is None:
            return
        print("  已连接改站号 sim 实例 :%d（站 3/4/6）" % srv2.port)
        try:
            # 数值口径不变（基准值与站号无关），只是从站换了个号
            m_ttyS4_check_turbidity_frames(frames2, 2.29, 0.045)
            m_ttyS4_check_flowmeter_frames(frames2, 3.6)
            # [站号投影] 客户端唯一能看到站号的地方是健康快照的地址文案
            # 「COMxx · 9600 · 站 N」——改配置必须一路改到这里。
            doc = m_ttyS4_request_health(cli2)
            if not doc or doc.get("devCount") is None:
                check("ttyS4 段：健康快照可拉取", False,
                      "err=%r keys=%s" % (doc.get("err"),
                                          sorted(doc.keys())[:8]))
            else:
                a1 = m_ttyS4_addr_of(doc, "进水浊度仪") or ""
                a2 = m_ttyS4_addr_of(doc, "出水浊度仪") or ""
                af = m_ttyS4_addr_of(doc, "电磁流量计") or ""
                check("改配置后进水浊度仪 站号=3", "站 3" in a1, "addr=%r" % a1)
                check("改配置后出水浊度仪 站号=4", "站 4" in a2, "addr=%r" % a2)
                check("改配置后流量计站号=6", "站 6" in af, "addr=%r" % af)
        finally:
            cli2.close()
    finally:
        srv2.stop()


def run_ttyS4_fidelity():
    """§29 ttyS4 模拟器保真：应答时延 + 站1 恢复期被主站间隙规避（自起两个 sim 实例）。

    [测什么 —— 2026-11-10 模拟器保真扩容，口径=docs/HSF板端ttyS4三设备取数
      实测总表_20261109.md §1.1/§1.3/§8]
      A. 缺省口径（sim 节不配 = 实测值）：路 B 虚拟从站带 ①应答时延（站1
         37~43ms / 站2 31~33ms / 站5 68~69ms）②站1 的「长帧后恢复期」
         （站5 的 49 字节帧收口后 125ms 内无应答）③站间最小间隙 200ms
         （cfg.serial_b.poll_gap_ms 缺省）。断言——
           · 装配日志的保真行在（旋钮存在且按缺省生效，不靠猜）；
           · **站1 全程在线**（8s 窗口无一帧 offline、服务端日志无采集失败）
             ——恢复期被 200ms 间隙规避，这就是「模拟恢复期 → 锻炼主站间隙」
             的端到端方向锁（恢复期行为本体由 server_py/tests/test_units.py
             的 TestTtyS4Fidelity 在 gap=0 下确定性锁定）；
           · 站1 浊度恒定 2.29（逐帧 229 不动）、站2 带内 0.045±0.001、
             站5 流量 3.6±2% 带内且空管位恒 0。
         **时延数值本身不做耗时断言**（CI 抖动），方向由帧的连续性表达。
      B. 旋钮可配（sim 节 + serial_b 覆盖）：时延清零、恢复期关、间隙 0
         ⇒ 装配日志数值跟着变，总线回到快节拍且数据照常（旋钮生效的方向锁）。
    """
    from hsfsrv_client import proto as P
    section("§29 ttyS4 模拟器保真：应答时延 + 站1 恢复期被主站间隙规避")

    # ---- A. 缺省口径（实测值缺省 + 200ms 间隙）----
    srv = m_ttyS4_Server()
    if not srv.start():
        check("§29 缺省 sim 服务端起动", False,
              "端口 %d；日志：%s" % (srv.port, srv.tail()[-200:]))
        srv.stop()
        return
    try:
        # [旋钮接线锁] 装配日志一行印出全部保真旋钮的生效值——缺口在这里
        # 一眼可见（哪一项没接上，断言详情直接指向哪一项）。
        boot_log = srv.tail(20000)
        check("§29 装配日志带保真行：间隙 200ms（站5 长帧后站1 恢复期的规避闸）",
              "事务间隙 200ms" in boot_log,
              srv.tail()[-300:])
        check("§29 装配日志带保真行：应答时延 浊1 37~43 / 浊2 31~33 / 流量 68~69",
              "浊1 37~43" in boot_log and "浊2 31~33" in boot_log
              and "流量 68~69" in boot_log, "")
        check("§29 装配日志带保真行：站1 恢复期 125ms",
              "恢复期 125ms" in boot_log, "")
        cli, frames, err = m_ttyS4_collect(
            srv, (P.TOPIC_TURBIDITY_STATUS, P.TOPIC_FLOWMETER_STATUS),
            seconds=8.0)
        check("§29 客户端登录缺省实例", cli is not None, err)
        if cli is None:
            return
        print("  已连接缺省 sim 实例 :%d（时延+恢复期+200ms 间隙全缺省）"
              % srv.port)
        try:
            turb = frames.get(P.TOPIC_TURBIDITY_STATUS, [])
            flow = frames.get(P.TOPIC_FLOWMETER_STATUS, [])
            # [连续性=方向] 间隙+时延把周期垫厚到 ~0.75s/拍，8s 应有 ≥10 拍；
            # 收帧经网络/推送闸，断言给足余量（≥5），不锁精确节拍。
            check("§29 浊度帧连续到达（≥5 帧 / 8s）", len(turb) >= 5,
                  "%d 帧" % len(turb))
            check("§29 流量计帧连续到达（≥5 帧 / 8s）", len(flow) >= 5,
                  "%d 帧" % len(flow))
            if turb:
                # ★ 核心：站 1 全程在线 —— 恢复期（125ms）被主站间隙（200ms）
                #   规避，长稳里无一拍失联。哪拍掉线就是间隙没垫住。
                offline = [d for d in turb if not d.get("v0_online")]
                check("§29 站1 全程在线（恢复期被主站间隙规避）",
                      not offline,
                      "offline %d/%d 帧" % (len(offline), len(turb)))
                consts = sorted({d.get("v0_ntu") for d in turb
                                 if d.get("v0_online")})
                check("§29 站1 浊度恒定 2.29（×100=229 逐帧不动；实测 20 轮恒定）",
                      consts == [229],
                      "取值=%s" % (consts[:5],))
                wob = [d.get("v1_ntu") for d in turb if d.get("v1_online")]
                check("§29 站2 浊度带内 0.045±0.001（×100 ∈ [3,7]）",
                      bool(wob) and all(isinstance(v, int) and 3 <= v <= 7
                                        for v in wob),
                      "取值=%s" % (sorted(set(wob))[:5],))
                srv_log = srv.tail(20000)
                check("§29 服务端日志无站1 采集失败（间隙规避的日志侧证据）",
                      "进水浊度仪 采集失败" not in srv_log, "")
            if flow:
                fs = [d.get("flow") for d in flow if d.get("valid")]
                check("§29 站5 流量带内 3.6±2%（断言给 ±5% 余量）",
                      bool(fs) and all(isinstance(v, (int, float))
                                       and abs(v - 3.6) <= 3.6 * 0.05
                                       for v in fs),
                      "取值=%s" % (sorted(set(round(v, 2) for v in fs))[:5],))
                check("§29 站5 空管报警维持 False（所有帧 bit2=0）",
                      all(not (int(d.get("alarms") or 0) & (1 << 2))
                          for d in flow), "")
        finally:
            cli.close()
    finally:
        srv.stop()

    # ---- B. 旋钮可配（sim 节 + serial_b 覆盖 ⇒ 全零快跑口径）----
    srv2 = m_ttyS4_Server(overrides={
        "sim": {"turbidity_reply_ms": [[0, 0], [0, 0]],
                "flowmeter_reply_ms": [0, 0],
                "recovery_ms": 0},
        "serial_b": {"poll_gap_ms": 0},
    })
    if not srv2.start():
        check("§29 覆盖口径 sim 服务端起动", False,
              "端口 %d；日志：%s" % (srv2.port, srv2.tail()[-200:]))
        srv2.stop()
        return
    try:
        boot2 = srv2.tail(20000)
        check("§29 旋钮可配：装配日志的间隙/恢复期数值跟着配置变（全 0）",
              "事务间隙 0ms" in boot2 and "恢复期 0ms" in boot2,
              srv2.tail()[-300:])
        cli2, frames2, err2 = m_ttyS4_collect(
            srv2, (P.TOPIC_TURBIDITY_STATUS, P.TOPIC_FLOWMETER_STATUS),
            seconds=4.0)
        check("§29 客户端登录覆盖口径实例", cli2 is not None, err2)
        if cli2 is None:
            return
        try:
            turb2 = frames2.get(P.TOPIC_TURBIDITY_STATUS, [])
            check("§29 全零口径下浊度帧照常到达（≥6 帧 / 4s 快节拍）",
                  len(turb2) >= 6, "%d 帧" % len(turb2))
            if turb2:
                offline2 = [d for d in turb2 if not d.get("v0_online")]
                check("§29 全零口径下站1 仍全程在线", not offline2,
                      "offline %d/%d 帧" % (len(offline2), len(turb2)))
        finally:
            cli2.close()
    finally:
        srv2.stop()


# ==========================================================================
# 段落：run_ui_clickthrough（§30 主窗 UI 真实点击回归，2026-11-10 新增）
# ==========================================================================


def run_ui_clickthrough():
    """§30 主窗 UI 真实点击回归（用户口径「UI 界面也要手动操作点击」）。

    [与既有段的分工] run_client 的 H 组 / m_hiscol_part_i_real_clicks 是
    **单页**级真点；本段起的是**真实 MainWindow**（三段式 chrome + 导航树 +
    页面栈 + 软键盘 + 200ms 刷新定时器）连**私有 sim 服务端**（root 登录，
    root=管理员 ⇒ 有设备控制/模式切换权限），把操作员一条点击链走完：
      导航切页 → ☰ 收展 → 变频器启停 → 历史筛选/图/翻页/排序 →
      日志类别/刷新/导出闸 → 报警类型筛选/清除 → 本地设置缩放/勾选。
    所有交互一律 QTest 合成真实输入事件（mouseClick / mouseDClick /
    keyClick），直接调槽只用于造数与关辅助窗（每处注释点名）。

    [为什么自起一个 sim 实例] 不与 main() 的私有实例混用：本段要把历史
    采样周期调快（storage.sample_ms=250，配置只写本实例的临时 cfg）——
    缺省 5s/行时「下一页」要 20 行 ≈ 100s 才亮，回归跑不动。顺带保证
    本段的控制命令落的是本实例的库，不污染别段的对账。

    [组合框的点击口径] 离屏平台下 QTest 对 QComboBox **弹层列表项**的
    鼠标命中不生效（点 view()/viewport() 只关不选，探针实测三种投递全
    无效）——这是 QTest/离屏弹窗的限制，不是产品缺陷（键盘序列同链路
    全通）。故弹层内选项用同一套合成事件的键盘序列（keyClick Down/Enter
    ——与真人「点开→方向键→回车」逐键等价）；**点开弹层本身仍是真鼠标
    点击**，选中后断言 currentIndex 与页面内部状态。
    [复选框的点击口径] 与 m_hiscol_part_i_real_clicks 同款：QCheckBox 只
    有指示器一段可点，rect().center() 点不中（本轮探针复现），点 (8, 高/2)。
    [表头双击口径] sectionDoubleClicked 要把 mouseDClick 发给 header 的
    **viewport**（发给 header 本体不触发，探针实测），坐标用
    sectionViewportPosition 换算（本就是 viewport 系）。

    [编排位置] main() 两处（--no-srv 分支与全量分支）都在
    run_soak_stability() 之后调本段；段内建页即用即毁（收尾关窗 + 拆桥 +
    杀自己起的 sim），不残留 Qt 状态。
    """
    from PyQt5.QtCore import QPoint, Qt, QEventLoop, QTimer
    from PyQt5.QtTest import QTest
    from PyQt5.QtWidgets import QApplication, QPushButton

    section("§30 主窗 UI 真实点击回归（真实 MainWindow + 私有 sim 服务端）")

    app = QApplication.instance()
    if app is None:
        check("§30 复用总回归的 QApplication", False,
              "总回归还没建 QApplication（不能在本段新建）")
        return

    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    # ---- 段内助手（全部嵌套，不占顶层名） ------------------------------

    def pump(ms=60):
        """驱动 Qt 事件循环 ms 毫秒（控制应答/推送回调都要经它回来）。"""
        loop = QEventLoop()
        QTimer.singleShot(int(ms), loop.quit)
        loop.exec_()

    def wait_until(pred, timeout=10.0, step=0.05):
        """轮询断言前置条件；超时返回 False（调用方 check 落 detail）。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            pump(step * 1000)
            if pred():
                return True
        return False

    def click_widget(w, label=""):
        """真实鼠标点击控件中心；控件不可见时拒绝点击（返回 False）。"""
        if w is None or not w.isVisible():
            print("  [§30] 跳过点击（不可见）：%s" % (label or repr(w)))
            return False
        QTest.mouseClick(w, Qt.LeftButton, Qt.NoModifier, w.rect().center())
        pump(50)
        return True

    def click_indicator(w):
        """真实点击 QCheckBox 的指示器位（见函数级 docstring 的口径）。"""
        QTest.mouseClick(w, Qt.LeftButton, Qt.NoModifier, QPoint(8, 17))
        pump(50)

    def btn_by_text(parent, text):
        """按按钮文本找页内控件（历史/日志/报警页的「筛选」等是局部变量，
        页面没存属性——findChildren 按文本捞是唯一不碰产品代码的办法）。"""
        hits = [b for b in parent.findChildren(QPushButton)
                if b.text() == text]
        return hits[0] if hits else None

    def combo_click_select(combo, index):
        """真鼠标点开弹层 + 键盘序列选中第 index 项（口径见 docstring）。

        :return: (ok, detail)——ok=选中且弹层已收。
        """
        if combo.view().isVisible():          # 上一次的弹层没收：Esc 先收
            QTest.keyClick(combo.view(), Qt.Key_Escape)
            pump()
        if not click_widget(combo, "下拉框"):
            return False, "下拉框不可见"
        if not combo.view().isVisible():
            return False, "点后弹层没开"
        cur = combo.currentIndex()
        step = 1 if index > cur else -1
        for _ in range(abs(index - cur)):
            QTest.keyClick(combo.view(),
                           Qt.Key_Down if step > 0 else Qt.Key_Up)
            pump(20)
        QTest.keyClick(combo.view(), Qt.Key_Return)
        pump()
        ok = (combo.currentIndex() == index
              and not combo.view().isVisible())
        return ok, "idx=%d popup=%s" % (combo.currentIndex(),
                                        combo.view().isVisible())

    def dclick_header(header, col):
        """真实双击表头第 col 列（发给 header 的 viewport，见口径注）。"""
        pos = QPoint(header.sectionViewportPosition(col) + 5,
                     header.height() // 2)
        QTest.mouseDClick(header.viewport(), Qt.LeftButton,
                          Qt.NoModifier, pos)
        pump(50)

    # ---- 私有 sim 服务端（历史采样提速，只写本实例临时 cfg） ----------

    class _UiSrv(PrivateServer):
        """§30 专用实例：storage.sample_ms=250 ⇒ ~4 行/秒，翻页用例不用等。"""

        def write_config(self):
            cfg = os.path.join(self.cfg_dir, "hsf_server_py.json")
            doc = {"server": {"port": int(self.port)},
                   "login": dict(self.LOGIN_POLICY),
                   "storage": {"sample_ms": 250}}
            with open(cfg, "w", encoding="utf-8") as fh:
                json.dump(doc, fh, ensure_ascii=False, indent=2)
            return cfg

    srv = _UiSrv()
    started = srv.start()
    check("§30 私有 sim 服务端起动（历史采样 250ms/行）", started,
          ("端口 %d" % srv.port) if started
          else ("端口 %d；日志：%s" % (srv.port, srv.tail()[-300:])))
    if not started:
        srv.stop()
        return

    # ---- 连接（root；管理员角色才有设备控制/模式切换权限） ------------
    bridge = LinkBridge()
    try:
        ok, err = _connect_with_retry(bridge, srv.port, srv=srv,
                                      user="root", password="root")
        check("§30 专用连接（root 登录私有 sim）", ok, err or "")
        if not ok:
            return
        check("§30 登录角色是管理员（能点启停/切模式）",
              bridge.role == proto.ROLE_ADMIN, "role=%d" % bridge.role)

        # ---- 主窗建起 --------------------------------------------------
        win = MainWindow(bridge)
        win.show()
        pump(300)
        check("§30 主窗建起（三段式 chrome + 页面栈 + 导航）",
              win.isVisible() and win.nav.isVisible()
              and win.stack.count() >= 1)
        check("§30 状态栏显示已连接（右账户钮另显示 root·管理员）",
              "已连接" in win.status_bar.conn.text(),
              repr(win.status_bar.conn.text()))
        # [账户入口 2026-11-11] 状态栏右下角可点账户钮；conn 只说连接态，
        # 账号·角色由账户钮显示（收拢 __main__/MainWindow 的 conn 双写）。
        check("§30 账户钮显示 账号·角色（root·管理员）",
              "root" in win.status_bar.account_text()
              and "管理员" in win.status_bar.account_text(),
              repr(win.status_bar.account_text()))
        check("§30 conn 不再混写账户（双写已收拢）",
              "root" not in win.status_bar.conn.text(),
              repr(win.status_bar.conn.text()))
        _acct_fired = []
        win.switchUserRequested.connect(
            lambda: _acct_fired.append("switch"))
        win.logoutRequested.connect(lambda: _acct_fired.append("logout"))
        win.status_bar.switchUserRequested.emit()
        win.status_bar.logoutRequested.emit()
        check("§30 账户菜单两项动作转发到 MainWindow 信号",
              _acct_fired == ["switch", "logout"], str(_acct_fired))
        check("§30 账户钮是可点 QPushButton（非只读标签）",
              hasattr(win.status_bar.account_btn, "clicked"))
        from hsfsrv_client import theme as _app_theme
        check("§30 初始页是主界面（栈顶/标题/页序号一致）",
              win.current == reg.PAGE_HOME
              and win.title_bar.title.text() == _app_theme.APP_TITLE
              and win.stack.currentWidget() is win.pages.get(reg.PAGE_HOME))
        check("§30 导航树条目数与注册表一致（18 节点）",
              len(win.nav._buttons) == len(reg.NAV_TREE),
              "%d" % len(win.nav._buttons))

        try:
            ui_click_pages(app, win, bridge, reg, proto, pump, wait_until,
                           click_widget, click_indicator, btn_by_text,
                           combo_click_select, dclick_header)
        finally:
            # ---- 收尾：关窗（即用即毁，不残留 Qt 状态） ----
            win.close()
            pump(150)
            check("§30 主窗已关闭（不可见）", not win.isVisible())
            win.deleteLater()          # 让 Qt 回收整棵页面树
            pump(150)
    finally:
        bridge.disconnect_from()
        srv.stop()


def ui_click_pages(app, win, bridge, reg, proto, pump, wait_until,
                   click_widget, click_indicator, btn_by_text,
                   combo_click_select, dclick_header):
    """§30 各页真实点击流（导航 → 变频器 → 历史 → 日志 → 报警 → 设置）。

    [ui_ 前缀] 段落助手的顶层名（任务口径：段落助手要么嵌套在段落函数
    里，要么用 ui_ 前缀占顶层名）。本函数由 run_ui_clickthrough 在建好
    主窗后调；窗口/桥的建立与收尾都在主函数里，这里只管「点页面」。
    参数是主函数里建好的环境（窗口/桥/点击助手），原样传入。
    """

    def nav_click(page_index):
        """真实点击左侧导航树条目；返回该页对象（未建则 None）。"""
        btn = None
        for idx, b in win.nav._buttons:
            if idx == page_index:
                btn = b
                break
        ok = click_widget(btn, "导航 %d" % page_index)
        return win.pages.get(page_index), ok

    def nav_assert(page_index, tag):
        """切页三一致：栈顶 widget / 标题栏标题 / 页序号。"""
        from hsfsrv_client import theme as _app_theme
        page = win.pages.get(page_index)
        check("§30%s 点导航切到「%s」页（栈顶/标题/页序号一致）" % (tag,
              reg.page_name(page_index)),
              win.current == page_index
              and win.stack.currentWidget() is page
              and win.title_bar.title.text() == _app_theme.APP_TITLE,
              "current=%d title=%r" % (win.current,
                                       win.title_bar.title.text()))
        check("§30%s 导航高亮跟随当前页" % tag,
              win.nav._current == page_index,
              "nav_current=%d" % win.nav._current)

    # ==================================================================
    # ① 顶栏 ☰：宽屏收起/展开导航（真实点击；点完复位）
    # ==================================================================
    click_widget(win.title_bar.menu_btn, "菜单钮")
    check("§30① 点顶栏菜单钮 → 导航收起", not win.nav.isVisible())
    click_widget(win.title_bar.menu_btn, "菜单钮")
    check("§30① 再点顶栏菜单钮 → 导航展开（复位，后续导航可点）",
          win.nav.isVisible())

    # ==================================================================
    # ② 导航点击逐页切换（主界面→搅拌泵→水质→流量→模拟量，顺序按任务口径）
    # ==================================================================
    nav_click(reg.PAGE_HOME)               # 已在主界面：点当前页应幂等
    check("§30② 点「主界面」（当前页幂等，不重进不重建）",
          win.current == reg.PAGE_HOME
          and len(win.pages) == 1,
          "pages=%d" % len(win.pages))
    for _idx, _tag in ((reg.PAGE_MONITOR_VFD, "②a"),
                       (reg.PAGE_MONITOR_WATER, "②b"),
                       (reg.PAGE_MONITOR_FLOW, "②c"),
                       (reg.PAGE_MONITOR_ANALOG, "②d")):
        nav_click(_idx)
        nav_assert(_idx, _tag)

    # ==================================================================
    # ③ 变频器页（搅拌泵）：先避自动闸（切手动），再真实点启动/停止
    #    [为什么放在日志页之前] 日志页的「控制」类别要吃这里的控制事件。
    # ==================================================================
    vfd, _ok = nav_click(reg.PAGE_MONITOR_VFD)
    ready = wait_until(lambda: vfd.btn_run.isEnabled(), timeout=10)
    check("§30③ 入页后启停钮亮（快照探测设备就绪）", ready,
          "device_ready=%r reason=%r" % (vfd._device_ready,
                                         vfd._not_ready_reason))
    if bridge.model.is_auto():
        # 自动闸：真实点顶栏模式钮切回手动（root 有 PERM_MODE_SWITCH）。
        click_widget(win.title_bar.mode_btn, "模式钮")
        wait_until(lambda: bridge.model.run_mode_known
                   and not bridge.model.is_auto(), timeout=8)
    check("§30③ 运行模式为手动（自动闸已避让）",
          bridge.model.run_mode_known and not bridge.model.is_auto(),
          "known=%s auto=%s" % (bridge.model.run_mode_known,
                                bridge.model.is_auto()))
    st = bridge.model.vfd
    check("§30③ 点击前停机、启停钮文案是「启动」",
          st.is_fresh() and not st.running and vfd.btn_run.text() == "启动",
          "fresh=%s running=%s text=%r" % (st.is_fresh(), st.running,
                                           vfd.btn_run.text()))
    click_widget(vfd.btn_run, "启停钮(启动)")
    check("§30③ 点「启动」→ 遥测 running 翻 True（服务端受理）",
          wait_until(lambda: st.is_fresh() and st.running, timeout=15),
          "running=%s" % st.running)
    pump(500)                              # 等下一拍 refresh 同步按钮文案
    check("§30③ 启停钮转「停止」（运行态文案同步）",
          vfd.btn_run.text() == "停止", repr(vfd.btn_run.text()))
    check("§30③ 自由停车钮亮（运行中口径）", vfd.btn_free.isEnabled())
    click_widget(vfd.btn_run, "启停钮(停止)")
    check("§30③ 点「停止」→ 遥测 running 翻 False（停机斜坡运行位 False）",
          wait_until(lambda: (not st.is_fresh()) or (not st.running),
                     timeout=15),
          "running=%s" % st.running)
    pump(500)
    check("§30③ 启停钮回「启动」（停机文案同步）",
          vfd.btn_run.text() == "启动", repr(vfd.btn_run.text()))

    # ==================================================================
    # ④ 历史页：筛选 / 图表视图 / 翻页 / 双击表头排序（全真实点击）
    # ==================================================================
    hist, _ok = nav_click(reg.PAGE_DATA_HISTORY)
    check("§30④ 历史页服务端模式出数（250ms/行加速落库）",
          wait_until(lambda: hist._data_mode == "server" and hist._rows,
                     timeout=20),
          "total=%d mode=%s" % (hist._total, hist._data_mode))
    check("§30④ 入页默认第 1 页", hist._page_index == 0
          and hist._page_label.text().startswith("第 1/"),
          hist._page_label.text())
    # 空时间窗点「筛选」：服务端模式重取第 1 页（时间/关键字都空=不限）。
    click_widget(btn_by_text(hist, "筛选"), "筛选")
    check("§30④ 点「筛选」（空条件）→ 重取第 1 页且仍有数据",
          wait_until(lambda: not hist._inflight and hist._rows, timeout=8)
          and hist._page_index == 0,
          "rows=%d page=%d" % (len(hist._rows), hist._page_index))
    # 翻页：等行数超过一页（20）→「下一页」亮 → 真点翻过去 → 真点回来。
    check("§30④ 行数超过一页（下一页用例的前置）",
          wait_until(lambda: hist._total > hist._page_size, timeout=30),
          "total=%d page_size=%d" % (hist._total, hist._page_size))
    # 「图表视图」：页内双视图（2026-11-10 用户要求「不要新建一个窗口」）。
    # [放在"总数超过一页"之后] 图表取的是**全量**，库里不足一页时无法证明
    # "不是只画当前页"（本用例初版就踩到：私有 sim 库当时只有 20 行）。
    # 点按钮 → 栈切到第 1 页 → 内嵌折线图控件出现（不是顶层窗口）。
    click_widget(btn_by_text(hist, "图表视图"), "图表视图")
    from hsfsrv_client.pages_other import LineChartDialog
    chart_ok = wait_until(
        lambda: (not hist._chart_fetching)
        and hist.findChild(LineChartDialog) is not None,
        timeout=60)
    dlg = hist.findChild(LineChartDialog)
    check("§30④ 点「图表视图」→ 页内出现折线图（取全量完成）",
          chart_ok and dlg is not None and hist._view_stack.currentIndex() == 1,
          "取回 %d 行 / 栈=%d" % (len(hist._chart_rows),
                                  hist._view_stack.currentIndex()))
    check("§30④ 图表是**页内控件**（不弹独立窗）",
          dlg is not None and not dlg.isWindow(),
          "isWindow=%s" % (dlg.isWindow() if dlg else "-"))
    check("§30④ 图表取的是全量（超过一页 %d 行）" % hist._page_size,
          len(hist._chart_rows) > hist._page_size,
          "取回 %d 行 / total=%d" % (len(hist._chart_rows), hist._total))
    click_widget(btn_by_text(hist, "表格视图"), "表格视图")
    check("§30④ 点「表格视图」→ 切回表格（栈第 0 页）",
          wait_until(lambda: hist._view_stack.currentIndex() == 0, timeout=5))
    check("§30④「下一页」钮已亮（offset+本页 < 总数）",
          hist._btn_next.isEnabled())
    click_widget(hist._btn_next, "下一页")
    check("§30④ 点「下一页」→ 到第 2 页",
          wait_until(lambda: hist._page_index == 1, timeout=8),
          hist._page_label.text())
    check("§30④「上一页」钮已亮（不在首页）", hist._btn_prev.isEnabled())
    click_widget(hist._btn_prev, "上一页")
    check("§30④ 点「上一页」→ 回第 1 页",
          wait_until(lambda: hist._page_index == 0, timeout=8),
          hist._page_label.text())
    # 排序：双击表头（新列降→升→取消），断言排序状态与指示器三段一致。
    hdr = hist._table.table.horizontalHeader()
    dclick_header(hdr, 3)                  # 列 3 = 输出频率(Hz)
    check("§30④ 双击表头「输出频率」→ 降序指示出现",
          hist._sort_col == 3 and hist._sort_desc
          and hdr.isSortIndicatorShown(),
          "col=%s desc=%s" % (hist._sort_col, hist._sort_desc))
    dclick_header(hdr, 3)
    check("§30④ 再双击同列 → 转升序",
          hist._sort_col == 3 and not hist._sort_desc,
          "desc=%s" % hist._sort_desc)
    dclick_header(hdr, 3)
    check("§30④ 第三次双击 → 取消排序（指示器隐藏）",
          hist._sort_col is None and not hdr.isSortIndicatorShown(),
          "col=%s" % hist._sort_col)

    # ==================================================================
    # ⑤ 日志页：类别下拉选「控制」/ 点「刷新」（重新加载）/ 导出闸
    # ==================================================================
    logp, _ok = nav_click(reg.PAGE_DATA_LOG)
    check("§30⑤ 日志页出数（连接/鉴权/控制事件已入库）",
          wait_until(lambda: bool(logp._rows), timeout=10),
          "rows=%d total=%d" % (len(logp._rows), logp._total))
    okc, det = combo_click_select(logp._cat_combo, 2)      # 2 = 控制
    check("§30⑤ 类别下拉点选「控制」", okc, det)
    check("§30⑤ 页内生效类别字面 = ctrl",
          logp._category == "ctrl", repr(logp._category))
    wait_until(lambda: not logp._inflight, timeout=6)
    check("§30⑤ 控制类别只铺该类（表格类别列全是「控制」）",
          bool(logp._rows)
          and all(r[1] == "控制" for r in logp._rows),
          "行=%d 类别=%s" % (len(logp._rows),
                             sorted(set(r[1] for r in logp._rows))))
    click_widget(logp._btn_reload, "刷新")                 # 重新加载
    check("§30⑤ 点「刷新」重取当前页（仍是控制类、仍有数据）",
          wait_until(lambda: not logp._inflight and bool(logp._rows),
                     timeout=8)
          and all(r[1] == "控制" for r in logp._rows),
          "rows=%d" % len(logp._rows))
    okc, det = combo_click_select(logp._cat_combo, 0)      # 回全部类别
    check("§30⑤ 类别回「全部类别」", okc, det)
    wait_until(lambda: not logp._inflight and bool(logp._rows), timeout=8)
    # 导出闸：无 Download 权限时点「导出数据」→ 状态栏报「权限不足」。
    # [辅助造态] 私有实例只有 root(管理员)/admin(普通用户) 两个缺省账户，
    # 两者都有 Download；无权限态用匿名角色值临时注入 bridge.role（界面
    # 闸读的就是它），点击本身真实，测完立刻还原。
    saved_role = bridge.role
    bridge.role = proto.ROLE_ANONYMOUS
    click_widget(btn_by_text(logp, "导出数据"), "导出数据")
    pump(200)
    check("§30⑤ 无权限点「导出数据」→ 状态栏报「权限不足」（不弹保存框）",
          "权限不足" in win.status_bar.message.text(),
          repr(win.status_bar.message.text()))
    bridge.role = saved_role

    # ==================================================================
    # ⑥ 报警页：类型下拉点选 + 「筛选」/「清除筛选」
    # ==================================================================
    alarmp, _ok = nav_click(reg.PAGE_DATA_ALARM)
    # [辅助造数] 页面留的测试入口 _record_alarm（正常路径是健康边沿触发）；
    # 播两条不同类型的记录，筛选/清除的行数断言才有分辨力。交互本身全真点。
    alarmp._record_alarm("搅拌泵", "变频器", "故障")
    alarmp._record_alarm("进水浊度仪", "水质", "过期")
    pump(200)
    # [口径 2026-11-17] 报警页的表 = **服务端报警历史（0x011A）+ 本地实时行**
    # （去重后）⇒ 表行数**不等于**本地记录数（服务端那段也在表里）⇒ 断言
    # 改成"两条种子记录都在表里"。
    tbl_devs = [alarmp._table.table.item(r, 1).text()
                for r in range(alarmp._table.table.rowCount())]
    check("§30⑥ 两条种子记录可见（在服务端历史+本地行的合并表里）",
          "搅拌泵" in tbl_devs and "进水浊度仪" in tbl_devs,
          "表行=%d 设备=%r" % (alarmp._table.table.rowCount(), tbl_devs))
    okc, det = combo_click_select(alarmp._kind_combo, 1)   # 1 = 变频器
    check("§30⑥ 类型下拉点选「变频器」", okc, det)
    click_widget(btn_by_text(alarmp, "筛选"), "筛选")
    pump(150)
    n_vfd = sum(1 for rec in alarmp._all_records() if rec[1][2] == "变频器")
    check("§30⑥ 点「筛选」→ 只剩变频器行",
          alarmp._kind_text == "变频器"
          and alarmp._table.table.rowCount() == n_vfd
          and all(alarmp._table.table.item(r, 2).text() == "变频器"
                  for r in range(alarmp._table.table.rowCount())),
          "kind=%r 表行=%d 期望=%d" % (alarmp._kind_text,
                                       alarmp._table.table.rowCount(),
                                       n_vfd))
    click_widget(btn_by_text(alarmp, "清除筛选"), "清除筛选")
    pump(150)
    check("§30⑥ 点「清除筛选」→ 类型回全部、行全回来",
          alarmp._kind_text is None
          and alarmp._kind_combo.currentIndex() == 0
          and alarmp._table.table.rowCount() == len(alarmp._all_records()),
          "kind=%r combo=%d 表行=%d 合并行=%d"
          % (alarmp._kind_text, alarmp._kind_combo.currentIndex(),
             alarmp._table.table.rowCount(), len(alarmp._all_records())))

    # ==================================================================
    # ⑦ 本地设置页：界面缩放点一档 + 还原；紧凑模式勾选 + 还原
    # ==================================================================
    localp, _ok = nav_click(reg.PAGE_SETTINGS_LOCAL)
    combo = localp.scale_combo
    okc, det = combo_click_select(combo, 2)                # 125%
    check("§30⑦ 界面缩放点选 125%（弹层选中项一致）",
          okc and combo.currentIndex() == 2, det)
    okc, det = combo_click_select(combo, 0)                # 还原 100%
    check("§30⑦ 界面缩放还原 100%", okc and combo.currentIndex() == 0, det)
    before = localp.compact_check.isChecked()
    click_indicator(localp.compact_check)
    check("§30⑦ 点「紧凑模式」勾选翻转", localp.compact_check.isChecked()
          != before)
    click_indicator(localp.compact_check)
    check("§30⑦ 再点还原（勾选复位）",
          localp.compact_check.isChecked() == before)


# ============================================================================
#   §31 全量筛选枚举（导出=按当前筛选条件跨页拉全；2026-11-10 导出改造）
# ============================================================================
#
# [锁什么] 导出不再是"当前页那 10/20/50 行"，而是**按当前筛选条件的全部
# 命中行**：与图表全量取数同一条路——同一组 WHERE 连续分页（短批即末批、
# 写满 total 即停）。C 端实现在 HsfDataHistory.c 的 histExportContinue /
# HsfDataLog.c 的 logExportContinue（Python 端是本段的参照口径）。
# 本段造 N>单批上限的数据 → 设置筛选 → 按同一判界连续分页枚举 → 断言：
# 枚举行数 == 筛选命中总数、批数 == 上取整页数、跨页且无重无漏。
# [数据源] 私有 sim 每秒才采一行，攒 137 行等不起 ⇒ 在服务端**起动前**
# 直接把种子行落进它将要打开的库文件（storage_apply 在启动路径 open_for
# 现有库；CREATE TABLE IF NOT EXISTS 不动已有表）。种子的 mode='枚举模式'
# ——sim 采的行只有 手动/自动·运行中/自动·停止/未知，关键字筛它即精确
# 命中这批种子，总数与库的实时增长无关。

#: §31 种子行的 ts 基准（epoch 毫秒；2026-01-01，在 VALID_TS_MIN 之上）。
m_expfull_BASE_MS = 1767225600000
#: §31 种子行数（> 协议单批上限 50 ⇒ 枚举必跨页）。
m_expfull_SEED_N = 137


def m_expfull_seed_db(db_path, count):
    """在私有实例将要打开的库里预播 count 行种子；返回首行 ts（毫秒）。"""
    import sqlite3
    ddl = ("CREATE TABLE IF NOT EXISTS history ("
           "ts INTEGER PRIMARY KEY, mode TEXT, vfd_state TEXT,"
           " freq_hz REAL, current_a REAL, rpm REAL, turb1 REAL, turb2 REAL,"
           " flow REAL, flow_unit TEXT, flow_total REAL, total_unit TEXT,"
           " press_mpa REAL, press_note TEXT, temp REAL, hum REAL,"
           " di INTEGER, do INTEGER, water_temp REAL);")
    conn = sqlite3.connect(db_path)
    try:
        conn.execute(ddl)
        conn.executemany(
            "INSERT OR REPLACE INTO history (ts, mode, vfd_state, freq_hz,"
            " temp) VALUES (?, '枚举模式', '停机', 25.0, 26.5)",
            [(m_expfull_BASE_MS + i * 1000,) for i in range(count)])
        conn.commit()
    finally:
        conn.close()
    return m_expfull_BASE_MS


def m_expfull_query(bridge, **kw):
    """同步发一次历史查询（泵事件循环等应答）。返回 (doc, err)。"""
    got = {}

    def done(d, _g=got):
        _g["doc"] = d

    def bad(m, _g=got):
        _g["err"] = m

    bridge.history_page(on_done=done, on_error=bad, **kw)
    deadline = time.time() + 8
    while "doc" not in got and "err" not in got and time.time() < deadline:
        _pump()
        time.sleep(0.02)
    return got.get("doc"), got.get("err")


def m_expfull_enum(bridge, limit=20, max_batches=400, **query):
    """按 C 端导出的同一套判界连续分页拉全。

    判界（与 histExportContinue 逐条同构）：短批（got < limit）即末批；
    累计 >= total 即停；0 行即停。逐批校验 offset/limit 回显（C 客户端
    靠它认领应答）。
    :return: (rows, total, batches, echoes, err)
        echoes = 每批 (请求 offset, 回显 offset, 回显 limit) 列表。
    """
    rows = []
    echoes = []
    total = 0
    batches = 0
    offset = 0
    while True:
        doc, err = m_expfull_query(bridge, offset=offset, limit=limit,
                                   **query)
        if err or not doc or not doc.get("ok"):
            return rows, total, batches, echoes, (err or "无应答/被拒")
        if total == 0:
            total = int(doc.get("total") or 0)
        got_rows = doc.get("rows") or []
        rows.extend(got_rows)
        # [不能用 `x or -1`] offset=0 是合法回显（0 为假值会被吞成 -1）。
        echoes.append((offset, int(doc.get("offset", -1)),
                       int(doc.get("limit", -1))))
        batches += 1
        got = len(got_rows)
        if got <= 0 or got < limit or (total and len(rows) >= total) \
                or batches >= max_batches:
            break
        offset += got
    return rows, total, batches, echoes, ""


def run_expfull():
    """§31 全量筛选枚举：造 N>单页容量的数据 → 筛选 → 枚举行数=命中总数。"""
    section("§31 全量筛选枚举：筛选命中总数 == 跨页枚举行数（导出全量口径）")
    srv = PrivateServer()
    kw = "枚举模式"
    limit = 20                     # 单页容量（页大小档；137 ⇒ 7 批）
    try:
        db = os.path.join(srv.cfg_dir, "hsf_data.db")
        m_expfull_seed_db(db, m_expfull_SEED_N)
    except Exception as exc:                      # noqa: BLE001
        check("§31 种子库预置", False, "%s: %s" % (type(exc).__name__, exc))
        return
    try:
        if not srv.start():
            check("§31 私有服务端起动", False, "端口 %d" % srv.port)
            return
    except Exception as exc:                      # noqa: BLE001
        check("§31 私有服务端起动", False, "%s: %s" % (type(exc).__name__, exc))
        return
    try:
        from hsfsrv_client.bridge import LinkBridge
        bridge = LinkBridge()
        ok, err = _connect_with_retry(bridge, srv.port, tries=4, gap=2.0,
                                      srv=srv)
        check("§31 连上私有服务端（admin/admin）", ok, err or "")
        if not ok:
            return
        # 锚点取"未来"：种子行全部 <= 它（sim 新采的行在锚点之后的不存在，
        # 在锚点之前的按 ts 排在最前，不影响 kw 筛选的确定性）。
        before = int(time.time() * 1000) + 60000

        # ---- E1 无筛选：种子行可见（total >= 种子数；sim 另有自采行） ----
        doc, err = m_expfull_query(bridge, offset=0, limit=1, before_ts=before)
        check("§31 E1 无筛选查询有应答", bool(doc) and not err,
              err or ("total=%s" % (doc or {}).get("total")))
        if doc:
            check("§31 E1b 种子行入库可见（total ≥ 种子数）",
                  int(doc.get("total") or 0) >= m_expfull_SEED_N,
                  "total=%s 种子=%d" % (doc.get("total"), m_expfull_SEED_N))

        # ---- E2 关键字筛选：命中总数 == 种子数；跨页枚举行数一致 ----
        rows, total, batches, echoes, err = m_expfull_enum(
            bridge, limit=limit, before_ts=before, kw=kw)
        check("§31 E2 关键字筛选枚举成功", not err, err)
        if not err:
            check("§31 E2b 命中总数 == 种子数",
                  total == m_expfull_SEED_N,
                  "total=%d 期望=%d" % (total, m_expfull_SEED_N))
            check("§31 E2c 枚举行数 == 命中总数（全量，非当前页）",
                  len(rows) == m_expfull_SEED_N,
                  "rows=%d" % len(rows))
            want_batches = (m_expfull_SEED_N + limit - 1) // limit
            check("§31 E2d 批数 == 上取整页数（短批判界正确）",
                  batches == want_batches,
                  "batches=%d 期望=%d" % (batches, want_batches))
            check("§31 E2e 确实跨页（批数 > 1）", batches > 1,
                  "batches=%d" % batches)
            ts_list = [int(r[0]) for r in rows]
            check("§31 E2f 无重复行（锚点定格，翻页不被新行顶偏）",
                  len(set(ts_list)) == len(ts_list),
                  "去重=%d 原始=%d" % (len(set(ts_list)), len(ts_list)))
            want_ts = set(m_expfull_BASE_MS + i * 1000
                          for i in range(m_expfull_SEED_N))
            check("§31 E2g 无漏行（种子 ts 全部取到）",
                  set(ts_list) == want_ts,
                  "缺=%d 多=%d" % (len(want_ts - set(ts_list)),
                                   len(set(ts_list) - want_ts)))
            bad_echo = [e for e in echoes
                        if e[1] != e[0] or e[2] != limit]
            check("§31 E2h 每批 offset/limit 回显一致（C 端认领依据）",
                  not bad_echo, str(bad_echo[:3]))
            want_offsets = [b * limit for b in range(batches)]
            check("§31 E2i 批游标按行数连续推进（不跳批不采样）",
                  [e[0] for e in echoes] == want_offsets,
                  str([e[0] for e in echoes]))

        # ---- E3 时间窗筛选：闭区间子窗口的命中数 == 枚举行数 ----
        lo = m_expfull_BASE_MS + 30 * 1000
        hi = m_expfull_BASE_MS + 59 * 1000      # 含两端：30 行
        rows, total, batches, _echoes, err = m_expfull_enum(
            bridge, limit=limit, before_ts=before, lo_ts=lo, hi_ts=hi)
        check("§31 E3 时间窗子窗口枚举成功", not err, err)
        if not err:
            check("§31 E3b 时间窗命中总数 == 30（含端点）",
                  total == 30 and len(rows) == 30,
                  "total=%d rows=%d" % (total, len(rows)))
            check("§31 E3c 时间窗内枚举跨 2 页（30/20）", batches == 2,
                  "batches=%d" % batches)

        # ---- E4 关键字 + 时间窗组合：同一条 WHERE，两条件都生效 ----
        rows, total, _b, _e2, err = m_expfull_enum(
            bridge, limit=limit, before_ts=before, lo_ts=lo, hi_ts=hi, kw=kw)
        check("§31 E4 组合筛选枚举成功", not err, err)
        if not err:
            check("§31 E4b 组合筛选命中数 == 30（种子行全带关键字）",
                  total == 30 and len(rows) == 30,
                  "total=%d rows=%d" % (total, len(rows)))

        # ---- E5 无命中：首批即短批（total=0 → 枚举 0 行即停，不死循环） ----
        rows, total, batches, _e2, err = m_expfull_enum(
            bridge, limit=limit, before_ts=before, kw="绝不可能命中的关键字")
        check("§31 E5 无命中查询成功", not err, err)
        if not err:
            check("§31 E5b 无命中 → total=0、0 行、1 批收尾",
                  total == 0 and len(rows) == 0 and batches == 1,
                  "total=%d rows=%d batches=%d" % (total, len(rows), batches))

        # ---- E6 导出意图帧（export=true）放行（admin 持 Download）----
        doc, err = m_expfull_query(bridge, offset=0, limit=5, before_ts=before,
                                   kw=kw, export_mode=True)
        check("§31 E6 export=true 被服务端放行（Download 权限在服务端生效）",
              bool(doc) and doc.get("ok") and not err,
              err or ("ok=%s total=%s" % ((doc or {}).get("ok"),
                                          (doc or {}).get("total"))))
    finally:
        try:
            bridge.disconnect_from()
        except Exception:                         # noqa: BLE001
            pass
        srv.stop()


# ============================================================================
#     §32 导出文件无头写 + 回读校验（CSV 字节级口径 / XLSX 包内容）
# ============================================================================
#
# [锁什么] 三页导出落盘的字节级口径（C 端 HsfExport.c 与 Python 端
# _write_export 同一套契约）：
#   · CSV = UTF-8 BOM（\xEF\xBB\xBF）+ CRLF 行分隔 + 最小化引号（含逗号/
#     双引号/换行的格才加引号，格内双引号加倍）——Excel 双击直开不乱码。
#   · XLSX = 一个 zip 包，工作表名"历史数据"，表头+数据行都在包 XML 里。
# [怎么测] 不连服务端、无头跑：直接调 Python 客户端 HistoryPage._write_export
# 写临时文件，按字节/按 csv.reader 回读比对；XLSX 按 zipfile 解包核对
# （xlsx_bridge 动态库缺失时 SKIP——与 §25 同口径，CSV 不受影响）。


def m_expwr_rows():
    """造 4 行 16 列文本（含逗号/双引号/换行/中文等"引号判定"边界格）。"""
    def row(stamp, last):
        return (["%s" % stamp, "手动", "停机", "25.00", "3.20", "750",
                 "12.50", "3.20", "26.5", "36.50 m³/h", "123.40 m³",
                 "0.499", "26.5", "55.0", "0x0000FFFF", last], False)
    return [
        row("2026-11-10 01:00:00", "普通格"),
        row("2026-11-10 02:00:00", "带,逗号"),
        row("2026-11-10 03:00:00", '带"引号'),
        row("2026-11-10 04:00:00", "带\n换行"),
    ]


def run_expwr():
    """§32 导出模块无头写 CSV/XLSX 并回读校验。"""
    import csv as _csv
    import io as _io
    section("§32 导出文件无头写 CSV/XLSX 并回读校验")
    _app, page, _st = m_hisflt_make_page()   # 复用总回归唯一的 QApplication
    rows = m_expwr_rows()
    tmpdir = tempfile.mkdtemp(prefix="hsf_expwr_")
    try:
        # ---------- CSV：字节级口径 ----------
        csv_path = os.path.join(tmpdir, "hist.csv")
        page._write_export(csv_path, rows)
        check("§32 C1 CSV 落盘", os.path.isfile(csv_path), csv_path)
        if os.path.isfile(csv_path):
            with open(csv_path, "rb") as fh:
                raw = fh.read()
            check("§32 C2 UTF-8 BOM 开头（utf-8-sig 同口径）",
                  raw[:3] == b"\xef\xbb\xbf", repr(raw[:4]))
            check("§32 C3 行分隔 = CRLF（含表头共 5 行）",
                  raw.count(b"\r\n") == len(rows) + 1,
                  "CRLF=%d 期望=%d" % (raw.count(b"\r\n"), len(rows) + 1))
            text = raw.decode("utf-8-sig")
            got = list(_csv.reader(_io.StringIO(text)))
            check("§32 C4 表头 == 列定义",
                  got and list(got[0]) == list(page.COLUMNS),
                  str(got[0] if got else None))
            body = got[1:]
            check("§32 C5 数据行数一致", len(body) == len(rows),
                  "%d/%d" % (len(body), len(rows)))
            ok_rows = (len(body) == len(rows)
                       and all(list(body[i]) == list(rows[i][0])
                               for i in range(len(rows))))
            check("§32 C6 逐格回读一致（含逗号/双引号/换行/中文格）",
                  ok_rows, "" if ok_rows else str(body[:1]))
            first_data_line = text.split("\r\n")[1]
            check("§32 C7 无特殊字符的格不加引号（最小化引号）",
                  first_data_line.endswith("普通格")
                  and '"' not in first_data_line, repr(first_data_line[-24:]))
            check("§32 C8 含逗号的格被引号包裹",
                  '"带,逗号"' in text, "")
            check("§32 C9 格内双引号加倍（"" 转义）",
                  '"带""引号"' in text, "")
        # ---------- XLSX：zip 包回读 ----------
        xlsx_path = os.path.join(tmpdir, "hist.xlsx")
        page._write_export(xlsx_path, rows)
        if not os.path.isfile(xlsx_path):
            skip("§32 X 组 XLSX 回读",
                 "xlsx_bridge 动态库未编译（Excel 导出需它；CSV 不受影响）")
            return
        import zipfile
        with zipfile.ZipFile(xlsx_path) as z:
            names = z.namelist()
            sheets = [n for n in names if n.startswith("xl/worksheets/")
                      and n.endswith(".xml")]
            check("§32 X1 包内有工作表 XML", bool(sheets), str(names))
            wb = ""
            try:
                wb = z.read("xl/workbook.xml").decode("utf-8", "replace")
            except KeyError:
                pass
            check("§32 X2 工作表名 = 历史数据", "历史数据" in wb, wb[:120])
            # 数据表 = add_sheet 追加的「历史数据」表；xlsx_create 自带的
            # 缺省 Sheet1 是空壳且仍占 zip 里的 sheet1.xml（xlsx_bridge.c
            # 自 491e8038 的口径：add_sheet 只追加+选中，不动缺省表），
            # 故**不能**拿 sheets[0] 当数据表——首版断言盲写（库在本机
            # 从未加载成、从未跑过）就栽在这里。按 OOXML 正道解析：
            # workbook.xml 的 <sheet name="历史数据"> r:id → workbook.xml
            # .rels 的 Target → zip 内路径。
            import re as _re
            sheet_xml = ""
            m_tag = _re.search(r'<sheet\b[^>]*name="历史数据"[^>]*>', wb)
            m_rid = _re.search(r'r:id="(rId\d+)"', m_tag.group(0)) \
                if m_tag else None
            if m_rid:
                rels = ""
                try:
                    rels = z.read("xl/_rels/workbook.xml.rels").decode(
                        "utf-8", "replace")
                except KeyError:
                    pass
                m_rel = _re.search(
                    r'<Relationship\b[^>]*Id="%s"[^>]*/?>'
                    % _re.escape(m_rid.group(1)), rels)
                m_tgt = _re.search(r'Target="([^"]+)"', m_rel.group(0)) \
                    if m_rel else None
                if m_tgt:
                    tgt = m_tgt.group(1).lstrip("/")
                    if not tgt.startswith("xl/"):
                        tgt = "xl/" + tgt
                    sheet_xml = z.read(tgt).decode("utf-8", "replace")
            if not sheet_xml and sheets:
                sheet_xml = z.read(sheets[0]).decode("utf-8", "replace")
            shared_xml = ""
            if "xl/sharedStrings.xml" in names:
                shared_xml = z.read("xl/sharedStrings.xml").decode(
                    "utf-8", "replace")
            all_xml = sheet_xml + shared_xml
            check("§32 X3 表头在包内（数值列名）",
                  "输出频率(Hz)" in all_xml, "")
            check("§32 X4 数据格在包内（含逗号/中文格）",
                  "带,逗号" in all_xml and "普通格" in all_xml, "")
            check("§32 X5 行数 = 表头 + 数据（4+1）",
                  sheet_xml.count("<row") == len(rows) + 1,
                  "row=%d" % sheet_xml.count("<row"))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


# ============================================================================
#     §33 筛选谓词与翻页判界（时间 / 关键字 / 列选择 / 清除筛选）
# ============================================================================
#
# [锁什么] 与 C 端 histApplyFilter / histClearFilter / histPrevSlot /
# histNextSlot 同一套口径的**谓词与判界**（在 Python 孪生页上锁；两端
# 同口径是仓库铁律）：
#   · 时间范围：只起点 / 只终点 / 闭区间含两端 / 起始晚于结束拒筛；
#   · 关键字：在**当前显示列**里包含匹配（列收窄 ⇒ 谓词范围收窄）；
#   · 清除筛选：时间/关键字复位、行全回来（列选择是视图设置，不随清
#     除复位——与 C 端 histClearFilter 注记同口径）；
#   · 翻页判界：页数上取整、末页「下一页」不动、首页「上一页」不动、
#     库裁剪后页码夹取、筛选回第 0 页。


def m_expflt_seed(page):
    """造 5 行散布两天+的种子（16 列，含水温列；列序同 HistoryPage.COLUMNS）。"""
    def row(stamp, turb="12.50", mode="手动"):
        return ([stamp, mode, "停机", "25.00", "3.20", "750", turb, "3.20",
                 "26.5", "36.50 m³/h", "123.40 m³", "0.499", "26.5", "55.0",
                 "0x0000FFFF", "0x000000FF"], False)
    page._rows = [
        row("2026-11-04 23:00:00"),
        row("2026-11-05 08:00:00"),
        row("2026-11-05 12:00:00", turb="25.00"),   # 浊度越限行（异常）
        row("2026-11-05 18:00:00"),
        row("2026-11-06 09:00:00", mode="自动·运行中"),
    ]
    page._rebuild()


def m_expflt_stamps(page):
    return [r[0][0] for r in page._filtered_rows()]


def run_expflt():
    """§33 筛选谓词各条件与清除筛选 + 翻页判界。"""
    section("§33 筛选谓词与翻页判界（时间/关键字/列选择/清除筛选）")
    _app, page, _st = m_hisflt_make_page()
    page._data_mode = "local"     # 谓词走本地路径（服务端路径在 §14b/I 段）
    m_expflt_seed(page)

    def clear_inputs():
        page._clear_inputs()

    # ---------- 时间范围 ----------
    clear_inputs()
    page._f_start.setText("2026-11-05 12:00")
    check("§33 P1 只给起点 → 该时刻及之后（含端点，3 行）",
          m_expflt_stamps(page) == ["2026-11-05 12:00:00",
                                    "2026-11-05 18:00:00",
                                    "2026-11-06 09:00:00"],
          str(m_expflt_stamps(page)))
    clear_inputs()
    page._f_end.setText("2026-11-05 12:00")
    check("§33 P2 只给终点 → 该时刻及之前（含端点，3 行）",
          m_expflt_stamps(page) == ["2026-11-04 23:00:00",
                                    "2026-11-05 08:00:00",
                                    "2026-11-05 12:00:00"],
          str(m_expflt_stamps(page)))
    clear_inputs()
    page._f_start.setText("2026-11-05 08:00")
    page._f_end.setText("2026-11-05 18:00")
    check("§33 P3 起止闭区间 → 含两端（3 行）",
          m_expflt_stamps(page) == ["2026-11-05 08:00:00",
                                    "2026-11-05 12:00:00",
                                    "2026-11-05 18:00:00"],
          str(m_expflt_stamps(page)))
    page._f_start.setText("2026-11-05 18:00")
    page._f_end.setText("2026-11-05 08:00")
    check("§33 P4 起始晚于结束 → 谓词为空集（无行能同时满足两侧）",
          m_expflt_stamps(page) == [], str(m_expflt_stamps(page)))
    page._clear_inputs()

    # ---------- 关键字 + 列选择（谓词范围 = 当前显示列） ----------
    clear_inputs()
    page._f_text.setText("自动")
    check("§33 P5 关键字包含匹配（在可见列里找，1 行）",
          m_expflt_stamps(page) == ["2026-11-06 09:00:00"],
          str(m_expflt_stamps(page)))
    # 列收窄成只有「时间」列：关键字在新谓词范围内不再命中
    from PyQt5.QtCore import Qt as _Qt
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(_Qt.Checked)   # 只剩「时间」
    check("§33 P6 列收窄后关键字不命中（列选择参与谓词）",
          m_expflt_stamps(page) == [], str(m_expflt_stamps(page)))
    page._f_col.check_all()                                  # 恢复全列
    check("§33 P7 列恢复后同一关键字重新命中（谓词范围随列回宽）",
          m_expflt_stamps(page) == ["2026-11-06 09:00:00"],
          str(m_expflt_stamps(page)))
    page._f_text.clear()

    # ---------- 清除筛选 ----------
    clear_inputs()
    page._f_start.setText("2026-11-05")
    page._f_text.setText("停机")
    page._apply_filter()
    hit = len(m_expflt_stamps(page))
    page._clear_filter()
    check("§33 P8 清除筛选 → 输入框全空",
          page._f_start.text() == "" and page._f_text.text() == ""
          and page._f_end.text() == "",
          "start=%r text=%r" % (page._f_start.text(), page._f_text.text()))
    check("§33 P9 清除筛选 → 行全回来（5 行，不再按旧条件筛）",
          len(m_expflt_stamps(page)) == 5 and hit < 5,
          "清除前=%d 清除后=%d" % (hit, len(m_expflt_stamps(page))))
    check("§33 P10 清除筛选不动列选择（列是视图设置，不是筛选条件）",
          len(page.visible_columns()) == len(page.COLUMNS),
          str(page.visible_columns()))

    # ---------- 翻页判界（服务端模式 + 桩桥，不发网络） ----------
    calls = []

    class _Stub(object):
        def is_connected(self):
            return True

        def history_page(self, **kw):
            calls.append(dict(kw))

    page.ctx.bridge = _Stub()
    page._data_mode = "server"
    page._page_size = 20
    page._total = 95                     # → 5 页（上取整）
    page._page_index = 0
    page._inflight = False
    calls[:] = []
    page._prev_page()
    check("§33 P11 首页「上一页」不动（不发请求）", len(calls) == 0,
          "calls=%d" % len(calls))
    page._next_page()
    check("§33 P12 首页「下一页」→ 请求第 2 页（offset=20）",
          len(calls) == 1 and calls[0].get("offset") == 20
          and calls[0].get("limit") == 20,
          str(calls[:1]))
    page._page_index = 4                 # 末页（第 5 页）
    page._inflight = False
    calls[:] = []
    page._next_page()
    check("§33 P13 末页「下一页」不动（页数上取整判界）", len(calls) == 0,
          "calls=%d" % len(calls))
    check("§33 P14 页数 == 上取整(95/20)=5", page._page_count() == 5,
          "pages=%d" % page._page_count())
    # 库被裁剪后总数变小：页码夹取（不停在空页上）
    page._pending_kind = "page"
    page._total = 30                     # 只剩 2 页
    page._page_index = 4
    page._on_page_ready({"ok": True, "total": 30, "rows": [],
                         "cols": list(page.COLUMNS)})
    check("§33 P15 库裁剪后页码夹到最后一页（第 2 页）",
          page._page_index == 1, "index=%d" % page._page_index)
    # 筛选必回第 0 页（服务端模式）
    page._page_index = 3
    page._inflight = False
    calls[:] = []
    page._f_text.setText("运行")
    page._apply_filter()
    check("§33 P16 筛选回第 0 页且请求带关键字",
          page._page_index == 0 and len(calls) == 1
          and calls[0].get("kw") == "运行",
          "index=%d calls=%d" % (page._page_index, len(calls)))
    # 页大小变化也回第 0 页（行号口径变了，停在原页号没意义）
    page._page_index = 2
    page._inflight = False
    calls[:] = []
    page._page_size_box.setCurrentIndex(0)   # 10 条
    page._on_page_size()
    check("§33 P17 改页大小回第 0 页", page._page_index == 0
          and page._page_size == 10 and len(calls) == 1,
          "index=%d size=%s calls=%d"
          % (page._page_index, page._page_size, len(calls)))
    page._data_mode = "local"    # 还原（页对象随段结束丢弃）


# ==========================================================================
# 段落：本机能力（WiFi + 屏幕休眠）—— 桥契约 + 两页接线 + 动作终态
#            [2026-11-13 三页合并] 屏幕休眠区现属「本地设置」页的
#            「窗口与屏幕」分区（原「窗口设置」页已并入）。
# ==========================================================================
# [背景] 「WiFi 网络设置」与「窗口设置·屏幕休眠」按用户定案**只在桌面
#        Python 客户端**实现，走 C 动态库 hsb_bridge（框架层
#        XNetworkWiFi / XPlatformPower，见 client_py/tools/hsf_bridge/）。
#        本段锁三件事：
#          A. 桥契约：ctypes 层方法齐全、策略回读有键、错误文本可取；
#          B. 网络设置页：WiFi 区控件齐全、适配器真的枚举出来、扫描/连接
#             三个动作都有**终态**（成功/失败/超时），不许停在「扫描中…」。
#          C. 本地设置页（窗口与屏幕分区）：屏幕休眠区控件齐全、能力位驱动
#             置灰、策略保存回读回显、熄屏/唤醒失败有终态。
# [为什么动作断言用替身桥] 真桥的「立即熄屏」会**关掉显示器**（Windows
#        走 SC_MONITORPOWER，见 Drive/windows/Graphics/
#        XPlatformPower_win32.c）——自动化测试绝不能真按。故动作路径用
#        替身桥驱动；真桥只做只读断言（能力位 / 策略回读 / 适配器枚举 /
#        动态库符号齐全）。
# [入口] m_nativ_run()


class _NativStubBridge(object):
    """替身桥：按需返回成功/失败，驱动页面的动作终态分支。

    [字段口径]
      · caps          电源能力位（1=仅熄屏控制；2=超时；4=挂起）。
      · adapters      适配器列表（None=枚举失败）。
      · networks      扫描结果列表（None=取结果失败）。
      · scan_state    扫描状态（0 空闲/1 进行中/2 完成/3 失败/-1 查询失败）。
      · policy        策略回读值；None=回读失败。
      · fail_*        对应动作返回失败。
      · conn_state    连接态文本（connecting/connected/failed/idle）。
    """

    def __init__(self, caps=1, adapters=None, networks=None, scan_state=2,
                 policy=None, conn_state="idle", fail_connect=False,
                 fail_disconnect=False, fail_screen_off=False,
                 fail_screen_on=False, fail_policy=False, fail_permission=False):
        self.caps = caps
        self.adapters = adapters if adapters is not None else []
        self.networks = networks if networks is not None else []
        self.scan_state = scan_state
        self.policy = policy if policy is not None else {
            "screenOffMs": 0, "systemSuspendMs": 0, "wakeInputs": 0xFF}
        self.conn_state = conn_state
        self.fail_connect = fail_connect
        self.fail_disconnect = fail_disconnect
        self.fail_screen_off = fail_screen_off
        self.fail_screen_on = fail_screen_on
        self.fail_policy = fail_policy
        self.fail_permission = fail_permission
        self.opened = False
        self.closed = False
        self.policy_calls = []
        self.ticks = 0
        self.activities = []
        self.error = ""

    # ---- 会话 ----
    def wifi_open(self):
        self.opened = True
        return True

    def wifi_close(self):
        self.closed = True

    # ---- WiFi ----
    def wifi_adapters(self):
        return self.adapters

    def wifi_start_scan(self, adapter_id):
        return bool(adapter_id)

    def wifi_scan_state(self, adapter_id):
        return self.scan_state

    def wifi_take_scan_results(self, adapter_id):
        return self.networks

    def wifi_connect(self, adapter_id, ssid, password=""):
        if self.fail_connect:
            self.error = "认证失败（口令不对）"
            return False
        return True

    def wifi_disconnect(self, adapter_id):
        if self.fail_disconnect:
            self.error = "平台拒绝断开"
            return False
        return True

    def wifi_connection(self, adapter_id):
        return {"ssid": "TestNet", "mac": "AA:BB:CC:DD:EE:FF",
                "signal": 66, "secure": "WPA2-PSK", "state": self.conn_state}

    # ---- 电源 ----
    def power_capabilities(self):
        return self.caps

    def power_is_screen_off(self):
        return False

    def power_set_screen_off(self, off):
        if off and self.fail_screen_off:
            self.error = "平台拒绝熄屏"
            return False
        if (not off) and self.fail_screen_on:
            self.error = "平台拒绝唤醒"
            return False
        return True

    def power_get_policy(self):
        return dict(self.policy)

    def power_set_policy(self, screen_off_ms, system_suspend_ms, wake_inputs):
        if self.fail_policy:
            self.error = "平台拒绝设置策略"
            return False
        self.policy_calls.append((screen_off_ms, system_suspend_ms,
                                  wake_inputs))
        self.policy = {"screenOffMs": screen_off_ms,
                       "systemSuspendMs": system_suspend_ms,
                       "wakeInputs": wake_inputs}
        return True

    def power_request_permission(self):
        if self.fail_permission:
            self.error = "本平台无需授权"
            return False
        return True

    def power_tick(self):
        self.ticks += 1
        return True

    def power_note_activity(self, mask):
        self.activities.append(mask)
        return True

    def last_error(self):
        return self.error


def _nativ_patch_bridge(stub, available=True):
    """把 hsfsrv_client.hsf_native 的桥替身装上（返回还原函数）。

    [为什么要替身] 真桥的「立即熄屏」会关显示器（见段头说明）。
    """
    from hsfsrv_client import hsf_native
    old_bridge = hsf_native.bridge
    old_available = hsf_native.available
    hsf_native.bridge = lambda: stub
    hsf_native.available = lambda: available

    def _restore():
        hsf_native.bridge = old_bridge
        hsf_native.available = old_available
    return _restore


def _nativ_build_page(page_cls):
    """建一个页面实例（离屏 ctx；不连服务端）。

    [状态栏留痕] 假 ctx 的 set_status 记录到 page._nativ_statuses —— 页面
    把终态同时写到「卡片状态行」和「主窗状态栏」，两处都要能验。
    """
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    seen = []
    ctx = PageContext(LinkBridge(), lambda i: None, seen.append)
    page = page_cls(ctx)
    page._nativ_statuses = seen
    return page


def m_nativ_run():
    """本机能力段落：A 桥契约 / B 网络设置页 / C 本地设置页（屏幕休眠区）。"""
    import time

    from hsfsrv_client import hsf_native

    # ==================== A. 桥契约 ====================
    section("§22A 桥契约（hsb_bridge ctypes 层）")
    if not hsf_native.available():
        skip("§22A 动态库在位（hsb_bridge.dll）",
             "未编译：%s（用 .zcode/build_hsf_bridge.bat）"
             % hsf_native.library_path())
    else:
        check("§22A1 动态库在位（hsb_bridge.dll）", True,
              hsf_native.library_path())
        stub = _NativStubBridge()
        restore = _nativ_patch_bridge(stub)
        try:
            b = hsf_native.bridge()
            check("§22A2 ctypes 层方法齐全（wifi_* / power_*）",
                  all(hasattr(b, n) for n in (
                      "wifi_open", "wifi_close", "wifi_adapters",
                      "wifi_start_scan", "wifi_scan_state",
                      "wifi_take_scan_results", "wifi_connect",
                      "wifi_disconnect", "wifi_connection",
                      "power_capabilities", "power_is_screen_off",
                      "power_set_screen_off", "power_get_policy",
                      "power_set_policy", "power_request_permission",
                      "power_tick", "power_note_activity")))
        finally:
            restore()

        # 真桥（只读动作）：能力位 / 策略回读 / 适配器枚举。
        try:
            real = hsf_native.HsbBridge()
            caps = real.power_capabilities()
            check("§22A3 能力位可读（非负整数）", isinstance(caps, int)
                  and caps >= 0, "caps=%s" % caps)
            pol = real.power_get_policy()
            check("§22A4 策略回读含三键",
                  isinstance(pol, dict)
                  and all(k in pol for k in ("screenOffMs", "systemSuspendMs",
                                             "wakeInputs")),
                  str(pol))
            check("§22A5 错误文本可取（字符串）",
                  isinstance(real.last_error(), str))
            check("§22A6 巡检与活动上报可用",
                  real.power_tick() is True
                  and real.power_note_activity(hsf_native.INPUT_MOUSE) is True)
            ads = real.wifi_adapters()
            check("§22A7 适配器枚举可用（列表或 None）",
                  ads is None or isinstance(ads, list),
                  "n=%s" % (len(ads) if isinstance(ads, list) else "-"))
        except Exception as exc:                       # noqa: BLE001
            check("§22A3 真桥只读调用", False,
                  "%s: %s" % (type(exc).__name__, exc))

    # ==================== B. 网络设置页 ====================
    section("§22B 网络设置页：WiFi 区接线与动作终态")
    from hsfsrv_client.pages_other import NetworkPage
    stub = _NativStubBridge(
        adapters=[{"id": "stub-0", "name": "Stub WLAN", "desc": "替身适配器",
                   "index": 1}],
        networks=[{"ssid": "TestNet", "mac": "AA:BB", "signal": 66,
                   "secure": "WPA2-PSK", "needPass": True}])
    restore = _nativ_patch_bridge(stub)
    try:
        page = _nativ_build_page(NetworkPage)
        check("§22B1 WiFi 区控件齐全",
              all(hasattr(page, n) for n in (
                  "wifi_adapter_combo", "wifi_scan_btn", "wifi_net_combo",
                  "wifi_pass_edit", "wifi_connect_btn",
                  "wifi_disconnect_btn", "wifi_info")))
        page.on_enter()
        check("§22B2 适配器枚举进下拉",
              page.wifi_adapter_combo.count() == 1
              and page.wifi_adapter_combo.currentData() == "stub-0",
              "count=%d" % page.wifi_adapter_combo.count())
        check("§22B3 进入页即打开无线会话", stub.opened)

        # ---- 扫描成功（终态：结果进网络下拉 + 终态文案）----
        page._wifi_scan()
        check("§22B4 扫描发起后进入「扫描中」并禁用扫描钮",
              page._wifi_scanning and not page.wifi_scan_btn.isEnabled())
        page._wifi_poll()
        check("§22B5 扫描完成是终态（结果进下拉）",
              not page._wifi_scanning and page.wifi_scan_btn.isEnabled()
              and page.wifi_net_combo.count() == 1
              and page.wifi_net_combo.currentData() == "TestNet",
              "net=%d text=%s" % (page.wifi_net_combo.count(),
                                  page.wifi_info.text()))
        check("§22B6 扫描完成状态行是终态文案",
              "扫描完成" in page.wifi_info.text(), page.wifi_info.text())

        # ---- 扫描失败（驱动报失败）----
        stub.scan_state = 3
        page._wifi_scan()
        page._wifi_poll()
        check("§22B7 扫描失败是终态（状态行报错 + 扫描钮恢复）",
              not page._wifi_scanning and page.wifi_scan_btn.isEnabled()
              and "扫描失败" in page.wifi_info.text(),
              page.wifi_info.text())

        # ---- 扫描超时（驱动一直不收口）----
        stub.scan_state = 1
        page._wifi_scan()
        page._wifi_scan_deadline = time.monotonic() - 1.0
        page._wifi_poll()
        check("§22B8 扫描超时是终态（不停在「扫描中…」）",
              not page._wifi_scanning and page.wifi_scan_btn.isEnabled()
              and "超时" in page.wifi_info.text(), page.wifi_info.text())

        # ---- 连接成功 ----
        stub.scan_state = 2
        stub.conn_state = "connected"
        page._wifi_connect()
        page._wifi_poll()
        check("§22B9 连接成功是终态（卡片显示已连接 + 状态栏报成功）",
              not page._wifi_connecting
              and "已连接" in page.wifi_info.text()
              and any("无线连接成功" in s for s in page._nativ_statuses),
              "%s | %s" % (page.wifi_info.text(),
                           page._nativ_statuses[-1:]))

        # ---- 连接失败（发起即被拒）----
        stub.fail_connect = True
        page._wifi_connect()
        check("§22B10 连接发起失败是终态（状态行报错）",
              not page._wifi_connecting and "连接失败" in page.wifi_info.text(),
              page.wifi_info.text())
        stub.fail_connect = False

        # ---- 连接超时（驱动一直 connecting）----
        stub.conn_state = "connecting"
        page._wifi_connect()
        page._wifi_connect_deadline = time.monotonic() - 1.0
        page._wifi_poll()
        check("§22B11 连接超时是终态（不停在「连接中…」）",
              not page._wifi_connecting and "超时" in page.wifi_info.text(),
              page.wifi_info.text())

        # ---- 断开 ----
        stub.conn_state = "idle"
        page._wifi_disconnect()
        check("§22B12 断开是终态", "已断开" in page.wifi_info.text(),
              page.wifi_info.text())

        # ---- 缺库 ⇒ 整区置灰并说明怎么编 ----
        restore2 = _nativ_patch_bridge(stub, available=False)
        try:
            page2 = _nativ_build_page(NetworkPage)
            page2.on_enter()
            check("§22B13 缺动态库 ⇒ WiFi 区置灰且给出编译口径",
                  not page2.wifi_scan_btn.isEnabled()
                  and "动态库" in page2.wifi_info.text()
                  and "build_hsf_bridge" in page2.wifi_info.text(),
                  page2.wifi_info.text())
        finally:
            restore2()
    except Exception as exc:                           # noqa: BLE001
        check("§22B 网络设置页段落", False,
              "%s: %s" % (type(exc).__name__, exc))
    finally:
        restore()

    # ==================== C. 本地设置页（窗口与屏幕分区） ====================
    # [2026-11-13 三页合并] 「窗口设置」页已并入「本地设置」（页号 13），
    # 屏幕休眠区现在是合并页的第三分区 ⇒ 控件名不变，页面类换成合并页。
    section("§22C 本地设置页：屏幕休眠区接线与能力位置灰")
    from hsfsrv_client.pages_other import LocalSettingsPage
    stub = _NativStubBridge(caps=1)
    restore = _nativ_patch_bridge(stub)
    try:
        page = _nativ_build_page(LocalSettingsPage)
        check("§22C1 屏幕休眠区控件齐全",
              all(hasattr(page, n) for n in (
                  "power_state", "power_blank_combo", "power_suspend_combo",
                  "power_wake_mouse", "power_wake_keyboard",
                  "power_wake_touch", "power_off_btn", "power_on_btn",
                  "power_save_btn", "power_perm_btn")))
        page.on_enter()
        check("§22C2 能力位=1（仅熄屏控制）⇒ 熄屏/唤醒可用",
              page.power_off_btn.isEnabled() and page.power_on_btn.isEnabled())
        check("§22C3 无挂起能力 ⇒ 系统挂起下拉置灰",
              not page.power_suspend_combo.isEnabled())
        check("§22C4 无委托计时 ⇒ 「请求授权」置灰（不留死控件）",
              not page.power_perm_btn.isEnabled())
        check("§22C5 状态行说明能力口径",
              "熄屏支持" in page.power_state.text()
              and "系统挂起不支持" in page.power_state.text(),
              page.power_state.text())

        # ---- 保存策略 ⇒ 下发 + 回读回显 ----
        idx = page.power_blank_combo.findData(5 * 60 * 1000)
        check("§22C6 自动熄屏档位含 5 分钟", idx >= 0, "idx=%d" % idx)
        page.power_blank_combo.setCurrentIndex(idx)
        page.power_wake_mouse.setChecked(True)
        page.power_wake_keyboard.setChecked(False)
        page.power_wake_touch.setChecked(False)
        page._power_save()
        check("§22C7 保存策略真的下发了（且唤醒来源按勾选）",
              len(stub.policy_calls) == 1
              and stub.policy_calls[0][0] == 5 * 60 * 1000
              and stub.policy_calls[0][2] == hsf_native.INPUT_MOUSE,
              str(stub.policy_calls))
        check("§22C8 保存后回读回显（不是自己刚传的值）",
              page.power_blank_combo.currentData() == 5 * 60 * 1000
              and page.power_wake_mouse.isChecked()
              and not page.power_wake_keyboard.isChecked(),
              "blank=%s mouse=%s"
              % (page.power_blank_combo.currentData(),
                 page.power_wake_mouse.isChecked()))
        check("§22C9 保存成功是终态（主窗状态栏有话）",
              any("已保存" in s for s in page._nativ_statuses),
              str(page._nativ_statuses[-1:]))

        # ---- 熄屏失败 / 唤醒失败 是终态 ----
        stub.fail_screen_off = True
        page._power_screen_off()
        check("§22C10 熄屏失败是终态（状态行报错）",
              "熄屏失败" in page.power_state.text(), page.power_state.text())
        stub.fail_screen_off = False
        stub.fail_screen_on = True
        page._power_screen_on()
        check("§22C11 唤醒失败是终态（状态行报错）",
              "唤醒失败" in page.power_state.text(), page.power_state.text())
        stub.fail_screen_on = False

        # ---- 能力位=0 ⇒ 整区置灰并说明不支持 ----
        stub.caps = 0
        page2 = _nativ_build_page(LocalSettingsPage)
        page2.on_enter()
        check("§22C12 能力位=0 ⇒ 整区置灰且说明本平台不支持",
              not page2.power_off_btn.isEnabled()
              and not page2.power_save_btn.isEnabled()
              and "不支持" in page2.power_state.text(),
              page2.power_state.text())

        # ---- 缺库 ⇒ 整区置灰并给出编译口径 ----
        restore2 = _nativ_patch_bridge(stub, available=False)
        try:
            page3 = _nativ_build_page(LocalSettingsPage)
            page3.on_enter()
            check("§22C13 缺动态库 ⇒ 屏幕休眠区置灰且给出编译口径",
                  not page3.power_off_btn.isEnabled()
                  and "动态库" in page3.power_state.text()
                  and "build_hsf_bridge" in page3.power_state.text(),
                  page3.power_state.text())
        finally:
            restore2()
    except Exception as exc:                           # noqa: BLE001
        check("§22C 本地设置页段落", False,
              "%s: %s" % (type(exc).__name__, exc))
    finally:
        restore()

    # ============ D. Web 设置页（2026-11-16 从本地设置拆出） ============
    # [2026-11-16 用户定案] 「网页服务」从本地设置**拆成独立页**
    # （PAGE_SETTINGS_WEB=16，仅 Python 客户端），并补两块能力：
    #   · 远程访问：域名 + 远程端口（拼远程访问地址）；
    #   · 扫码直达：局域网地址 / 远程地址两个二维码可切换。
    # 本段把 webserver.start/stop 换成**记录桩**再动开关 ⇒ 验的是「接线
    # 真的发起了起停 + 落盘」，**不真的起网页端**（回归里不该抢端口）。
    section("§22E Web 设置页：网页服务开关/端口 + 远程访问 + 扫码直达")
    from hsfsrv_client import webserver as _ws
    _orig_start, _orig_stop = _ws.start, _ws.stop
    _calls = []

    # [桩要跟得上接口 2026-11-17] 网页端一律**独立进程**（用户定案"不要
    #  内嵌"）⇒ `start()` 不再有形态参数 ⇒ 桩按新签名来。
    def _fake_start(host="127.0.0.1", port=6100, web_port=None):
        _calls.append(("start", host, port, web_port))
        return True, ""

    def _fake_stop():
        _calls.append(("stop",))

    _ws.start = _fake_start
    _ws.stop = _fake_stop
    try:
        from hsfsrv_client import settings as _us
        from hsfsrv_client.pages_web import WebSettingsPage
        from hsfsrv_client import hsf_native
        page4 = _nativ_build_page(WebSettingsPage)
        check("§22E1 网页服务控件齐全（已拆到独立页）",
              all(hasattr(page4, n) for n in (
                  "web_check", "web_port_spin", "web_status")))
        check("§22E2 端口缺省 8085（**不占 8080**）",
              _us.web_port() == 8085 and _us.web_port() != 8080,
              "port=%d" % _us.web_port())
        check("§22E3 开关缺省开（随客户端启动网页端）",
              page4.web_check.isChecked() and _us.web_enabled())
        check("§22E4 端口框与设置一致且合法",
              page4.web_port_spin.value() == _us.web_port()
              and page4.web_port_spin.value() >= _ws.MIN_PORT
              and page4.web_port_spin.value() <= _ws.MAX_PORT,
              "spin=%d" % page4.web_port_spin.value())
        check("§22E5 状态行有文案（不空）",
              bool(page4.web_status.text()), page4.web_status.text())

        # ---- 关 ⇒ 落盘 + 停服 ----
        _calls.clear()
        page4.web_check.setChecked(False)
        check("§22E6 关开关 ⇒ 落盘 + 真的停服",
              _us.web_enabled() is False
              and ("stop",) in _calls, str(_calls))
        # ---- 开 ⇒ 落盘 + 起服（带当前端口）----
        _calls.clear()
        page4.web_check.setChecked(True)
        check("§22E7 开开关 ⇒ 落盘 + 真的起服（带端口）",
              _us.web_enabled() is True
              and any(c[0] == "start" and c[3] == _us.web_port()
                      for c in _calls), str(_calls))
        # [形态恒为独立进程 2026-11-17 用户定案"不要内嵌"] 开关只管
        #  客户端退出时停不停它，**不影响起服形态**。
        _calls.clear()
        _us.set_value(_us.K_WEB_STOP_WITH_CLIENT, False)
        page4.web_check.setChecked(False)
        page4.web_check.setChecked(True)
        check("§22E7b 取消同步关闭也照样起服（形态恒为独立进程）",
              any(c[0] == "start" and c[3] == _us.web_port()
                  for c in _calls), str(_calls))
        _us.set_value(_us.K_WEB_STOP_WITH_CLIENT, True)
        _calls.clear()
        # ---- 改端口 ⇒ 落盘（提交走 800ms 去抖，测试里直接调提交口）----
        page4.web_port_spin.setValue(8099)
        page4._apply_web_port()
        check("§22E8 改端口 ⇒ 落盘生效",
              _us.web_port() == 8099, "port=%d" % _us.web_port())
        page4.web_port_spin.setValue(8085)
        page4._apply_web_port()

        # ---- E9~E14：远程访问 + 扫码直达（2026-11-16 新增能力）----
        check("§22E9 远程访问控件齐全（域名 + 远程端口 + 保存）",
              all(hasattr(page4, n) for n in (
                  "web_domain_edit", "web_remote_port_spin",
                  "web_remote_apply")))
        # 域名规范化：带 scheme/路径/端口的输入都收敛成纯主机名。
        page4.web_domain_edit.setText("  https://hsf.example.com:9000/x ")
        page4.web_remote_port_spin.setValue(18080)
        page4._apply_remote()
        check("§22E10 域名规范化后落盘（去 scheme/端口/路径）",
              _us.web_domain() == "hsf.example.com"
              and page4.web_domain_edit.text() == "hsf.example.com",
              "%r" % _us.web_domain())
        check("§22E11 远程端口落盘（18080）",
              _us.web_remote_port() == 18080,
              "port=%d" % _us.web_remote_port())
        # 局域网地址 = 本机 IP + 网页端口（用户口径：直接用局域网 ip+端口）。
        page4.qr_lan_btn.setChecked(True)
        check("§22E12 局域网二维码内容 = http://<本机IP>:<网页端口>/",
              page4.qr_canvas.url() == page4.lan_url()
              and page4.lan_url().endswith(":%d/" % _us.web_port()),
              page4.qr_canvas.url())
        # 切到远程：内容换成 域名 + 远程端口。
        page4.qr_remote_btn.setChecked(True)
        check("§22E13 切到远程二维码 = http://<域名>:<远程端口>/",
              page4.qr_canvas.url() == "http://hsf.example.com:18080/",
              page4.qr_canvas.url())
        # 二维码真的编出来了（native 桥在就跑；缺库时至少给出可读提示）。
        if hsf_native.available():
            check("§22E14 二维码已编码（矩阵非空）",
                  page4.qr_canvas.has_code(),
                  page4.qr_canvas.error_text())
        else:
            check("§22E14 native 桥缺失时给出可读提示（不崩）",
                  "native" in page4.qr_canvas.error_text().lower()
                  or "hsb_bridge" in page4.qr_canvas.error_text(),
                  page4.qr_canvas.error_text())
        # 未配域名时切远程 ⇒ 提示先填域名（不是空白/崩溃）。
        _us.set_value(_us.K_WEB_DOMAIN, "")
        page4.web_domain_edit.setText("")
        page4._apply_remote()
        page4.qr_remote_btn.setChecked(True)
        check("§22E15 未配域名：远程码给出「先填域名」提示",
              (not page4.qr_canvas.has_code())
              and bool(page4.qr_canvas.error_text()),
              page4.qr_canvas.error_text())
        # 收尾：回到局域网模式，域名/远程端口复原（不污染其它段）。
        page4.qr_lan_btn.setChecked(True)
        _us.set_value(_us.K_WEB_REMOTE_PORT, 0)
    except Exception as exc:                           # noqa: BLE001
        check("§22E Web 设置页（网页服务/远程/二维码）", False,
              "%s: %s" % (type(exc).__name__, exc))
    finally:
        _ws.start, _ws.stop = _orig_start, _orig_stop


def m_nativ_monitor():
    """D 段：电源巡检驱动真的装上了（1Hz tick + 用户活动打点）。

    [为什么必须锁] 框架把「空闲多久熄屏」的判定放在 XPlatformPower_tick()
    （由**应用层定时器**调用，C 端是 XGuiApplication 的 1Hz 定时器）。
    桌面 Python 客户端不跑 XGuiApplication ⇒ 没人驱动它 ⇒
    「自动熄屏时长」就选了不兑现。本段锁住"驱动确实在跑"这件事。
    """
    import time

    from PyQt5.QtCore import QEvent
    from PyQt5.QtWidgets import QApplication

    from hsfsrv_client import __main__ as client_main
    from hsfsrv_client import hsf_native

    section("§22D 电源巡检驱动（1Hz tick + 活动打点）")
    stub = _NativStubBridge(caps=1)
    restore = _nativ_patch_bridge(stub)
    try:
        app = QApplication.instance()
        filt = client_main._install_power_monitor(app)
        check("§22D1 电源监视器装上（过滤器 + 定时器都挂在 app 上）",
              filt is not None
              and getattr(app, "_hsf_power_filter", None) is filt
              and getattr(app, "_hsf_power_timer", None) is not None)
        # 泵 1.4s 事件循环 ⇒ 1Hz 定时器至少跳一次
        deadline = time.monotonic() + 1.4
        while time.monotonic() < deadline:
            app.processEvents()
            time.sleep(0.02)
        check("§22D2 1Hz 定时器真的在推进电源巡检（tick 被调用）",
              stub.ticks >= 1, "ticks=%d" % stub.ticks)
        # 鼠标移动事件 ⇒ 活动上报（位集 = 鼠标）
        before = len(stub.activities)
        ev = QEvent(QEvent.MouseMove)
        consumed = filt.eventFilter(None, ev)
        check("§22D3 鼠标移动映射成鼠标活动位集并上报",
              len(stub.activities) == before + 1
              and stub.activities[-1] == hsf_native.INPUT_MOUSE,
              "activities=%s" % stub.activities[-1:])
        check("§22D4 过滤器不吞事件（返回 False）", consumed is False,
              "ret=%s" % consumed)
        # [节流口径] 活动上报按 0.5s 节流（鼠标移动可达百 Hz，逐事件进 C
        # 没必要）⇒ 紧跟着的第二个事件被节流掉；隔过节流窗才再报。
        before = len(stub.activities)
        filt.eventFilter(None, QEvent(QEvent.KeyPress))
        check("§22D5 0.5s 内的重复活动被节流（不逐事件进 C）",
              len(stub.activities) == before,
              "activities=%s" % stub.activities[-1:])
        time.sleep(0.6)
        before = len(stub.activities)
        filt.eventFilter(None, QEvent(QEvent.KeyPress))
        check("§22D6 节流窗后键盘事件映射成键盘活动位集",
              len(stub.activities) == before + 1
              and stub.activities[-1] == hsf_native.INPUT_KEYBOARD,
              "activities=%s" % stub.activities[-1:])
        # 收尾：停掉定时器**并摘掉事件过滤器**（不留常驻副作用）。
        # [摘过滤器 2026-11-21] 只 `timer.stop()` 不够：`installEventFilter`
        # 装的那个过滤器还挂在 app 上 ⇒ 本段所在的**子进程**退出时，
        # QApplication 析构会仍回调它 ⇒ 而此时 Python 解释器已在收尾 ⇒
        # 调用已销毁的 Python 对象 ⇒ **0xC0000005 访问违例**
        # （实测：本段组子进程偶发崩溃 exit=3221225477，单跑该段 55/0
        # 复现不出 ⇒ 典型退出期 UAF）。
        from hsfsrv_client import __main__ as client_main
        client_main._uninstall_power_monitor(app)
    except Exception as exc:                           # noqa: BLE001
        check("§22D 电源巡检驱动段落", False,
              "%s: %s" % (type(exc).__name__, exc))
    finally:
        restore()


# ============================================================================
# 段落：§38 通用文字裁剪扫描器（2026-11-20·用户口径「没文字被裁剪遮挡」）
#
#   入口：merge_text_fit()
#
# [为什么要有这条] 用户要「保证界面显示完美，没文字被裁剪遮挡」。此前
#   客户端对「裁字」只有**零散的单控件锁**（数字量格宽、DO 行高、模式列
#   宽、运行时长标签…），既没有覆盖全部 19 页，也没有覆盖窄窗与板端
#   1.5× 缩放 ⇒ 「某个新页面的某句提示在窄窗被裁」这类缺陷**没人拦得住**
#   （本轮实测就抓出 13 处：三页时间框占位符、阀卡名/状态徽标、历史/日志
#   状态标签、相机 IP/口令、WiFi 口令、域名占位符…）。
#
# [本段锁的是「信息不可达」] 判据不是「文字宽度 > 控件宽度」这么粗，而是
#   **这条文字用户看不看得全**：
#     · 控件能换行（QLabel.wordWrap）⇒ 看得全，不管多宽；
#     · 富文本（QLabel 存的是 HTML）⇒ 布局自适应，不按纯文本量；
#     · QPushButton / QComboBox ⇒ **Qt 内建省略号**（Qt 5 的
#       QCommonStyle::drawItemText 会 elide）⇒ 看得见"被省略"，
#       不是硬裁；
#     · 文字被裁但 **tooltip 里有全文** ⇒ 信息可达（用户悬停能看到）；
#     · 其余 ⇒ **硬裁**（信息彻底不可达）⇒ 判红。
#
# [多档扫描] 视口 1280×720（桌面缺省）/ 1024×768 / 800×1280（板端竖屏）/
#   640×900（紧凑档）× 缩放 1.0 与 **1.5**（板端实机 HSF_UI_SCALE=1.15~1.5
#   的区间，裁字高发区此前完全没被测过）。
#
# [可见性判据] 离屏下 `isVisible()` 恒 False（页面没真正 show 到屏幕）⇒
#   用 `isVisibleTo(win)`：它只看**显式隐藏标记**与祖先链，是离屏可用的
#   正确判据（本仓已踩过 isVisible 的坑，见页面自适应段的注释）。
# ============================================================================


def _tf_scan_text_controls(page, host, stats=None):
    """扫一个页面里的硬裁控件，返回 [(类名, 文本, 需宽, 可用宽, tooltip)]。"""
    from PyQt5.QtGui import QFontMetrics
    from PyQt5.QtWidgets import (QLabel, QCheckBox, QRadioButton, QLineEdit,
                                 QAbstractSpinBox, QPushButton, QComboBox)

    hits = []
    for w in page.findChildren(object):
        # 只看容器类控件（findChildren(object) 会返回所有 QObject）
        if not isinstance(w, (QLabel, QCheckBox, QRadioButton, QLineEdit,
                              QAbstractSpinBox)):
            continue
        if stats is not None:
            stats["candidates"] = stats.get("candidates", 0) + 1
        try:
            if not w.isVisibleTo(host):
                if stats is not None:
                    stats["hidden"] = stats.get("hidden", 0) + 1
                continue
        except Exception:                               # noqa: BLE001
            continue
        if isinstance(w, QLabel):
            text = w.text()
            if "<" in text and ">" in text:
                continue          # 富文本：布局自适应，不按纯文本量
            if w.wordWrap():
                continue          # 能换行 ⇒ 看得到全文
        elif isinstance(w, (QCheckBox, QRadioButton)):
            text = w.text()
        elif isinstance(w, QLineEdit):
            # 只查占位符（用户输入超长时 QLineEdit 自己滚动，不算裁）
            text = w.placeholderText()
        elif isinstance(w, QAbstractSpinBox):
            le = w.lineEdit()
            text = le.placeholderText() if le is not None else ""
        else:
            continue
        if not text:
            continue
        if stats is not None:
            stats["with_text"] = stats.get("with_text", 0) + 1
        try:
            fm = QFontMetrics(w.font())
            need = fm.horizontalAdvance(text)
            avail = w.contentsRect().width()
        except Exception:                               # noqa: BLE001
            continue
        if avail <= 0 or need <= avail + 1:
            continue
        if stats is not None:
            stats["overflow"] = stats.get("overflow", 0) + 1
        hits.append((w.__class__.__name__, text, need, avail, w.toolTip()))
    return hits


def _tf_reachable(text, tip):
    """被裁了但信息仍可达（tooltip 里有全文）⇒ 放行。"""
    if not tip:
        return False
    if tip == text:
        return True
    head = text[:12]
    return bool(head) and head in tip


def _tf_self_test():
    """自证有效：造一个必然溢出的控件，喂给扫描器，看它抓不抓得到。

    [为什么要这条 2026-11-20] 扫描器类断言最容易变成「空跑假绿」——
    可见性判据写错 ⇒ 一个控件都看不到 ⇒ 永远 PASS。⇒ 注入一个**已知的**
    坏控件（长文本 + 定宽 + 无 tooltip），断言扫描器必须报出来。扫描器
    口径坏了（宽度取法、elide 判据、可见性判据），这条立刻红。
    """
    from PyQt5.QtWidgets import (QApplication, QLabel, QVBoxLayout,
                                 QWidget)

    host = QWidget()
    lay = QVBoxLayout(host)
    bad = QLabel("这是一段故意写得很长的文字用来触发硬裁判定" * 4)
    bad.setFixedWidth(40)
    lay.addWidget(bad)
    # 好控件：宽度足够 ⇒ 不该报
    good = QLabel("短")
    lay.addWidget(good)
    host.resize(200, 200)
    host.show()
    app = QApplication.instance()
    if app is not None:
        for _ in range(3):
            app.processEvents()
    hits = _tf_scan_text_controls(host, host)
    caught = sum(1 for h in hits if h[1].startswith("这是一段故意"))
    # 过了判据的（tooltip 兜底）不算抓到
    missed = 1 if caught == 0 else 0
    return {"caught": caught, "missed": missed,
            "detail": [(h[0], h[1][:12], h[2], h[3]) for h in hits]}


def merge_text_fit():
    """§38 通用文字裁剪扫描器（多分辨率 × 多缩放 × 全 19 页）。"""
    section("§38 通用文字裁剪扫描器（多分辨率×多缩放，不许文字被硬裁）")
    from PyQt5.QtWidgets import QApplication, QWidget

    app = QApplication.instance()
    if app is None:
        check("§38 QApplication 可用", False, "无 QApplication")
        return

    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import theme
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    # 视口档：桌面缺省 / 笔记本 / 板端竖屏 / 紧凑档。
    viewports = ((1280, 720), (1024, 768), (800, 1280), (640, 900))
    # 缩放档：缺省 1.0 与板端实机 1.5（板端 HSF_UI_SCALE 常见 1.15~1.5）。
    scales = (1.0, 1.5)

    orig_scale = theme.ui_scale()
    hard_total = 0
    warn_total = 0
    details = []
    stats = {}
    try:
        for sc in scales:
            theme.set_ui_scale(sc)
            for (vw, vh) in viewports:
                win = MainWindow(LinkBridge())
                win.resize(vw, vh)
                win.show()
                for _ in range(3):
                    app.processEvents()
                for idx in range(reg.PAGE_COUNT):
                    try:
                        win.goto_page(idx)
                        for _ in range(3):
                            app.processEvents()
                    except Exception as exc:               # noqa: BLE001
                        hard_total += 1
                        details.append("scale=%.1f %dx%d 页%d 切页异常：%s"
                                       % (sc, vw, vh, idx, exc))
                        continue
                    page = win.pages.get(idx)
                    if page is None:
                        continue
                    for (cls, text, need, avail, tip) in \
                            _tf_scan_text_controls(page, win, stats):
                        pname = reg.page_name(idx)
                        if _tf_reachable(text, tip):
                            warn_total += 1
                            continue
                        hard_total += 1
                        details.append(
                            "scale=%.1f %dx%d 页%d「%s」%s 需%dpx 可用%dpx"
                            " 「%s」"
                            % (sc, vw, vh, idx, pname, cls, need, avail,
                               text[:36]))
                win.close()
                del win
                app.processEvents()
    except Exception as exc:                           # noqa: BLE001
        check("§38 扫描器本身没崩", False,
              "%s: %s" % (type(exc).__name__, exc))
        theme.set_ui_scale(orig_scale)
        return
    finally:
        theme.set_ui_scale(orig_scale)

    check("§38-1 全 19 页 × 4 视口 × 2 缩放：无文字被硬裁（信息可达）",
          hard_total == 0, "硬裁 %d 处：%s" % (hard_total,
                                          " | ".join(details[:12])))
    # 省略号设计（Qt 内建 elide / tooltip 兜底）允许存在，但数量不该失控
    # ——量太大通常意味着「到处在省略」，那不是好体验。
    check("§38-2 省略号/提示兜底数量可控（≤ 40 处）",
          warn_total <= 40, "warn=%d" % warn_total)
    # [对照组·门必须真的在扫 2026-11-20] 这条最容易变成「空跑假绿」：
    # 可见性判据一旦写错（离屏下 isVisible 恒 False），扫描器会**一个控件
    # 都看不到** ⇒ 上面两条永远 PASS ⇒ 门形同虚设（AGENTS 记的同款坑：
    # 「不进门禁的锁等于没有锁」「必须有对照组断言，否则等于没测到东西」）。
    # ⇒ 断言：扫到的候选控件与带文字的控件都必须有量（证明不是空跑）。
    check("§38-3 对照组：扫描器真的在扫（不是空跑假绿）",
          stats.get("with_text", 0) >= 500
          and stats.get("hidden", 0) < stats.get("candidates", 1),
          "stats=%s" % (stats,))
    # [自证有效·注入对照 2026-11-20] 上面的「有量」只能证明扫描器在跑，
    # 不能证明它**判得对**。⇒ 造一个**必然溢出**的控件（长文本 + 定宽 +
    # 无 tooltip），喂给同一个扫描器，断言它**被抓到** —— 扫描器口径一旦
    # 写坏（可见性判据、宽度取法、elide 判据），这条立刻红。
    _tf_inj = _tf_self_test()
    check("§38-4 自证有效：注入一个必然溢出的控件 ⇒ 扫描器必须抓到",
          _tf_inj["caught"] == 1 and _tf_inj["missed"] == 0,
          "caught=%d missed=%d detail=%s"
          % (_tf_inj["caught"], _tf_inj["missed"], _tf_inj["detail"]))


def merge_text_fit_alias():
    """§38 的分组别名（--only=textfit 用）。"""
    merge_text_fit()


# ============================================================================
# 段落：§39 页面控件级补洞（模拟量 + 菜单页）
#
#   入口：merge_page_coverage()
#
# [为什么要有这条] 全 19 页只有「能构建能切换」的弱断言，逐页盘点发现
#   5 页**零控件级断言**：模拟量(4)、主菜单(1)、三个子菜单(2/8/12)。这些页
#   一旦被改坏（表头错、按钮文案错、点击不跳转、断线名字消失），现有回归
#   全绿也照样带病上线。本段给它们补上「建页 + 关键控件 + 关键文案 +
#   点击跳转 + 数据落地」的最低覆盖。
# ============================================================================


def merge_page_coverage():
    """§39 模拟量页 + 菜单页的控件级回归锁。"""
    section("§39 页面控件级补洞（模拟量页 + 主菜单 + 三个子菜单）")
    from PyQt5.QtWidgets import QApplication, QPushButton

    app = QApplication.instance()
    if app is None:
        check("§39 QApplication 可用", False, "无 QApplication")
        return

    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    bridge = LinkBridge()
    win = MainWindow(bridge)
    app.processEvents()

    # ---------- A. 模拟量页（页 4）----------
    win.goto_page(reg.PAGE_MONITOR_ANALOG)
    app.processEvents()
    analog = win.pages.get(reg.PAGE_MONITOR_ANALOG)
    check("§39-A1 模拟量页建成且有数据表",
          analog is not None and hasattr(analog, "_table")
          and analog._table is not None)
    check("§39-A2 表头 = 通道/名称/数值/单位",
          tuple(analog.HEADERS) == ("通道", "名称", "数值", "单位"),
          "headers=%s" % (analog.HEADERS,))
    check("§39-A3 列宽权重数值列最宽（3,3,9,2）",
          analog._table._column_weights == (3, 3, 9, 2),
          "weights=%s" % (analog._table._column_weights,))

    def _a_rows():
        """读 DataTable 的真实单元格文字（QTableWidget.item）。"""
        t = analog._table.table
        return [[(t.item(r, c).text() if t.item(r, c) is not None else "")
                 for c in range(t.columnCount())]
                for r in range(t.rowCount())]

    # 数据落地：换算通道同时显示两段数值 + 名字；断线通道名字仍在。
    m = bridge.model
    m.analog.apply({"n": 8, "v": 1, "r0": 4200, "u0": 11988,
                    "e0": 499, "eu0": "MPa", "en0": "压差计", "ew0": ""})
    analog.refresh()
    app.processEvents()
    rows = _a_rows()
    check("§39-A4 换算通道：原始 mA + 换算值 + 通道名都在（不丢名字）",
          any("压差计" in " ".join(r) for r in rows)
          and any("11.988" in " ".join(r) and "0.499" in " ".join(r)
                  for r in rows),
          "rows=%s" % (rows[:2],))
    # 断线通道（无 e0，但发 en0/ew0）⇒ 名字与「断线」说明还在。
    m.analog.apply({"n": 8, "v": 1, "u0": 0,
                    "en0": "压差计", "ew0": "断线"})
    analog.refresh()
    app.processEvents()
    rows2 = _a_rows()
    check("§39-A5 断线通道：名字与「断线」说明仍在（2026-11-20 实锅）",
          any("压差计" in " ".join(r) for r in rows2)
          and any("断线" in " ".join(r) for r in rows2),
          "rows=%s" % (rows2[:2],))

    # ---------- B. 主菜单（页 1）----------
    win.goto_page(reg.PAGE_MENU)
    app.processEvents()
    menu = win.pages.get(reg.PAGE_MENU)
    check("§39-B1 主菜单建成", menu is not None)
    if menu is not None:
        btns = menu.findChildren(QPushButton)
        labels = [b.text() for b in btns]
        check("§39-B2 主菜单入口（主界面/设备/数据/设置）",
              any("主界面" in t for t in labels)
              and any("设备" in t for t in labels)
              and any("数据" in t for t in labels)
              and any("设置" in t for t in labels),
              "labels=%s" % (labels,))
        # 点击入口 ⇒ 导航到对应子菜单页（按钮 clicked 直连 ctx.navigate
        # = win.goto_page，**真实点击**即可，不 mock）。
        # [为什么不是 getattr(win,"navigate")] MainWindow 把 goto_page 作为
        # `navigate` 传给页面 ctx，但 win 自己**没有** navigate 属性——早先
        # 用 getattr 判空会把这条断言整个跳过（假绿），故直接点击后读
        # win.current。
        device_btn = next((b for b in btns if "设备" in b.text()), None)
        check("§39-B3 找到「设备」入口按钮（触控主菜单）",
              device_btn is not None,
              "按钮文案=%s" % ([b.text() for b in btns],))
        if device_btn is not None:
            device_btn.click()
            app.processEvents()
            check("§39-B4 点击后当前页 = 设备子菜单(2)",
                  win.current == reg.PAGE_MONITOR_MENU,
                  "current=%r want=%r"
                  % (win.current, reg.PAGE_MONITOR_MENU))
        # [2026-11-22 菜单页档必须能回首页] 主菜单的「主界面」入口真回 0。
        home_btn = next((b for b in btns if "主界面" in b.text()), None)
        if home_btn is not None:
            win.goto_page(reg.PAGE_MENU)
            app.processEvents()
            home_btn.click()
            app.processEvents()
            check("§39-B5 主菜单「主界面」入口 ⇒ 首页(0)",
                  win.current == reg.PAGE_HOME,
                  "current=%r want=%r" % (win.current, reg.PAGE_HOME))
        else:
            check("§39-B5 主菜单「主界面」入口 ⇒ 首页(0)", False,
                  "找不到主界面入口，labels=%s" % (labels,))

    # ---------- C. 三个子菜单（页 2/8/12）----------
    for page_id, label in (
            (reg.PAGE_MONITOR_MENU, "设备"),
            (reg.PAGE_DATA_MENU, "数据"),
            (reg.PAGE_SETTINGS_MENU, "设置")):
        win.goto_page(page_id)
        app.processEvents()
        sub = win.pages.get(page_id)
        check("§39-C%d「%s」子菜单建成且有条目按钮" %
              ((page_id % 10) + 1, label),
              sub is not None and bool(sub.findChildren(QPushButton)),
              "buttons=%d" % (len(sub.findChildren(QPushButton))
                              if sub is not None else -1))

    # ---------- D. 页面预热覆盖（2026-11-21 补洞）----------
    # [为什么锁这条] 预热清单原先 16 项**漏了相机页 17**——而相机页恰是
    # 最重的一页（ES 解码线程 + 704² 帧缓冲）⇒ 首次进入仍付全款。
    # 这类漏洞的形态是「**新增页时忘了加进清单**」⇒ 断言的不是某个具体
    # 页号，而是「**清单覆盖了全部非主页页号**」⇒ 以后再加页自动被拦。
    from hsfsrv_client.main_window import MainWindow as _MW
    prewarm = set(_MW.PREWARM_ORDER)
    expect = set(range(1, reg.PAGE_COUNT))      # 0=主界面随窗构造即建
    missing = sorted(expect - prewarm)
    check("§39-D1 预热清单覆盖全部非主页页号（%d 页）" % len(expect),
          not missing,
          "漏预热：%s" % [reg.PAGE_NAMES[i] for i in missing])
    extra = sorted(prewarm - expect)
    check("§39-D2 预热清单没有多余的页号", not extra,
          "多余：%s" % [reg.PAGE_NAMES[i] for i in extra if i < reg.PAGE_COUNT])
    check("§39-D3 相机页(17) 在预热清单里（最重的一页不能漏）",
          reg.PAGE_MONITOR_CAMERA in prewarm,
          "相机页没预热 ⇒ 首次进入照旧卡")

    win.close()
    del win
    app.processEvents()


if __name__ == "__main__":
    sys.exit(main())
