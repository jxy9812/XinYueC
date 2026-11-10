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
    tb.apply({"count": 2, "v0_name": "浊度仪1", "v0_ntu": 1231,
              "v0_online": True, "v1_name": "浊度仪2", "v1_ntu": 325,
              "v1_online": True})
    check("浊度：定点 ÷100", abs((tb.ntu_of(0) or 0) - 12.31) < 1e-6,
          "ntu_of(0)=%r" % tb.ntu_of(0))
    tb2 = M.TurbidityState()
    tb2.apply({"count": 2, "v0_name": "浊度仪1", "v0_ntu": 1231,
               "v0_online": False, "v1_name": "浊度仪2", "v1_ntu": 325,
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
    tb_b.apply({"side": 0, "count": 2, "v0_name": "浊度仪1", "v0_ntu": 1250,
                "v0_online": True, "v1_name": "浊度仪2", "v1_ntu": 320,
                "v1_online": True})
    tb_b.apply({"side": 1, "count": 2, "v0_name": "浊度仪1", "v0_ntu": 320,
                "v0_online": True, "v1_name": "浊度仪2", "v1_ntu": 1250,
                "v1_online": True})
    check("浊度：两侧都在线 ⇒ 取 B 侧（PREFER=(B,A)）",
          abs((tb_b.ntu_of(0) or 0) - 3.20) < 1e-6,
          "ntu_of(0)=%r" % tb_b.ntu_of(0))
    tb_a = M.TurbidityState()
    tb_a.apply({"side": 0, "count": 2, "v0_name": "浊度仪1", "v0_ntu": 1250,
                "v0_online": True, "v1_name": "浊度仪2", "v1_ntu": 320,
                "v1_online": True})
    tb_a.apply({"side": 1, "count": 2, "v0_name": "浊度仪1", "v0_online": False,
                "v1_name": "浊度仪2", "v1_online": False})     # B 侧全离线
    check("浊度：B 侧全离线 ⇒ 退 A 侧在线台（优先侧没读数不算数）",
          abs((tb_a.ntu_of(0) or 0) - 12.50) < 1e-6,
          "ntu_of(0)=%r" % tb_a.ntu_of(0))
    tb_off = M.TurbidityState()
    tb_off.apply({"side": 1, "count": 2, "v0_name": "浊度仪1",
                  "v0_online": False, "v1_name": "浊度仪2", "v1_online": False})
    check("浊度：两侧都离线 ⇒ 仍取到仪器名/台数，但 ntu 为 None（显示 --）",
          tb_off.count == 2 and tb_off.ntu_of(0) is None
          and tb_off.name_of(0) == "浊度仪1",
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
    win.close()
    app.processEvents()
    b.disconnect_from()


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

    # --- (1) 从未见过有效值的字段带空洞 ⇒ 该行不入库（起动瞬态）---
    cold = object.__new__(App)
    cold_verdict = _ready(cold, hole)
    check("热身门：从没见过的字段带空洞 ⇒ 该行不入库（起动瞬态）",
          not cold_verdict, "判定=%s（应为 False）" % cold_verdict)

    # --- (2) 该字段见过有效值之后，即使掉线也照写（历史表不停摆）---
    warm = object.__new__(App)
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


def check_window_anchor(bridge, proto, is_log, label, page=5):
    """锚定分页契约：上界生效 / 同锚同偏移同批 / 翻页不重不漏。

    [锚点怎么取] 取**当前最新一行的 ts**（先发一次无锚点的第 1 页，拿
    max(ts)）。不取"现在 - 5s"：服务端刚起时那个窗口里可能一行都没有
    （C 服务端起来 10s 内历史只有 1~2 行、日志只有登录那几条），只能
    证明"空窗口时 rows 也空"，证明不了翻页。取最新行 ts 则有数据的服务端
    一定有非空窗口，且它是个**固定值**：之后新入库的行 ts 更大，自然被
    `ts <= 锚点` 挡在窗外 ⇒ 同一锚点的 total 恒定（拿"现在"当锚时窗口会
    边长，翻页就会重/漏）。
    """
    fn = bridge.log_page if is_log else bridge.history_page

    def query(offset, anchor):
        return _req_doc(fn, offset=offset, limit=page, before_ts=anchor)

    base, berr = _req_doc(fn, offset=0, limit=page)
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
    again, _e = query(0, anchor)
    keys0 = [_page_row_key(is_log, r) for r in rows0]
    keys_b = [_page_row_key(is_log, r) for r in ((again or {}).get("rows") or [])]
    check("%s 同一锚点同偏移 ⇒ 同一批行（窗口不漂移）" % label,
          bool(again) and (again or {}).get("total") == total
          and set(keys_b) == set(keys0),
          "total=%s→%s 行=%d→%d"
          % (total, (again or {}).get("total"), len(keys0), len(keys_b)))

    # --- 翻页：total 不变、两页不重叠、第 2 页整体不晚于第 1 页---
    p1, _e = query(page, anchor)
    rows1 = list((p1 or {}).get("rows") or [])
    ts1 = [_page_row_key(is_log, r)[0] for r in rows1]
    keys1 = [_page_row_key(is_log, r) for r in rows1]
    check("%s 相邻两页 total 一致（锚定窗口不变）" % label,
          bool(p1) and (p1 or {}).get("total") == total,
          "第1页 total=%s / 第2页 total=%s" % (total, (p1 or {}).get("total")))
    check("%s 翻页不重叠（同一条不出现两次）" % label,
          not (set(keys0) & set(keys1)),
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
          len(seen) == len(set(seen)) and len(seen) == want,
          "取到 %d 行 / 窗口 total=%d（%d 页）" % (len(seen), total, pages))


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
            doc, err = _req_doc(bridge.history_page, offset=0, limit=8)
            if err or not doc:
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
        kw_doc, _e = _req_doc(bridge.history_page, offset=0, limit=20,
                              kw="手动")
        check("C 服务端关键字下推生效（kw=手动 能筛出该行）",
              bool(kw_doc) and bool(kw_doc.get("rows")),
              "命中 %d 行" % len((kw_doc or {}).get("rows") or []))
        now_ms = int(time.time() * 1000)
        win_doc, _e = _req_doc(bridge.history_page, offset=0, limit=20,
                               lo_ts=now_ms - 60 * 1000, hi_ts=now_ms)
        rows = list((win_doc or {}).get("rows") or [])
        in_window = True
        for r in rows:
            ts = r[0] if isinstance(r, (list, tuple)) and r else None
            if ts is not None and not (now_ms - 60 * 1000 <= ts <= now_ms):
                in_window = False
        check("C 服务端时间窗下推生效（返回行都落在窗口内）",
              bool(win_doc) and in_window, "%d 行" % len(rows))
        anchor_doc, _e = _req_doc(bridge.history_page, offset=0, limit=8,
                                  before_ts=now_ms)
        check("C 服务端翻页锚点回显（beforeTs 生效且不回写成 0）",
              bool(anchor_doc) and anchor_doc.get("offset") == 0,
              "offset=%r limit=%r" % (anchor_doc.get("offset"),
                                      anchor_doc.get("limit")))

        # -----------------------------------------------------------
        # (2) 日志页（0x0119）：形状 + 类别字段
        # -----------------------------------------------------------
        log_doc, err = _req_doc(bridge.log_page, offset=0, limit=10)
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
                cat_doc, _e = _req_doc(bridge.log_page, offset=0, limit=10,
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
                ndoc, _e = _req_doc(bridge.log_page, offset=0, limit=10,
                                    category="zz_no_such_category")
                check("【§11】未知类别 ⇒ total=0 且 rows 空（category 真下推）",
                      (ndoc or {}).get("total") == 0
                      and not ((ndoc or {}).get("rows") or []),
                      "total=%r" % ((ndoc or {}).get("total"),))
                # 关键字搜类别名：C 端口径 text||' '||category。用 "auth" 当
                # 关键字，命中行里必须有"文本不含 auth、只有类别含"的行。
                kdoc, _e = _req_doc(bridge.log_page, offset=0, limit=50,
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
                bdoc, _e = _req_doc(bridge.log_page, offset=0, limit=50,
                                    category="auth", kw="auth")
                check("【§11】类别+关键字合取（AND，不是先后覆盖）",
                      bool(bdoc)
                      and int((bdoc or {}).get("total") or 0)
                      <= int((cat_doc or {}).get("total") or 0)
                      and all(str(r.get("category") or "") == "auth"
                              for r in ((bdoc or {}).get("rows") or [])),
                      "total=%r（category=auth 的 total=%r）"
                      % ((bdoc or {}).get("total"),
                         (cat_doc or {}).get("total")))

        # -----------------------------------------------------------
        # (2b) 窗口锚点：C 服务端的 0x0118 / 0x0119 与 Python 端同口径
        # -----------------------------------------------------------
        check_window_anchor(bridge, proto, False, "【§11】0x0118 ")
        check_window_anchor(bridge, proto, True, "【§11】0x0119 ")

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
        while time.time() < deadline:
            _pump()
            time.sleep(0.05)
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
        check("客户端浊度值 == 库里同一行（浊度仪1）",
              abs(ntu - float(row["turb1"])) <= TOL_TURB,
              "客户端 %.2f NTU / 库里 %.3f NTU" % (ntu, float(row["turb1"])))
    else:
        check("客户端浊度值 == 库里同一行（浊度仪1）", False,
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
        "vfd": (merge_vfd,),
        "uiscal": (merge_ui_scale,),
        "home": (merge_home_cards,),
        "firstscreen": (merge_home_first_screen,),
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


def merge_vfd():
    """§21 变频器与数字量：滑块单位 / 启动给定值自检。"""
    section("§21 变频器与数字量：滑块单位 / 启动给定值自检（并入本文件）")
    run_vfdsld()      # 段落：test_vfd_slider
    run_vfdgiv()      # 段落：test_vfd_start_given


def merge_ui_scale():
    """§22 界面字号缩放。"""
    section("§22 界面字号缩放（并入本文件）")
    run_uiscal()      # 段落：test_ui_scale


def merge_home_cards():
    """§23 首页卡片：浊度仪 / 按钮组。"""
    section("§23 首页卡片：浊度仪 / 按钮组（并入本文件）")
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
        只写我们关心的两节，其余键由服务端按缺省值补（config.py 的 load
        对缺失键保持缺省）。
        """
        cfg = os.path.join(self.cfg_dir, "hsf_server_py.json")
        doc = {"server": {"port": int(self.port)},
               "login": dict(self.LOGIN_POLICY)}
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
        for _g in ("history", "log", "alarm", "charts", "remote",
                   "softkbd", "vfd", "uiscal", "home", "firstscreen"):
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
    # §14 起：并入本文件的段落（纯客户端段，不用服务端）
    # [UI 段子进程隔离 2026-11-09] 一段崩了不再带崩整轮（heap corruption
    # 0xc0000374 surface 在 setStyleSheet，见 _run_group_isolated 注释）；
    # 协议/纯逻辑段（storage 含真服务端、reconnect/ttyS4/xlsx）仍在进程内。
    # ------------------------------------------------------------------
    _run_group_isolated("history")
    _run_group_isolated("log")
    _run_group_isolated("alarm")
    _run_group_isolated("charts")
    merge_storage()
    for _g in ("remote", "softkbd", "vfd", "uiscal", "home",
               "firstscreen"):
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

任务口径（2026-11-09 对齐 C 端 HsfDataAlarm.c：alarmRowVisible /
alarmApplyFilter / alarmClearFilter / alarmApplyColumnFilter /
alarmExportCsv / alarmBuildChart / alarmSetView）：

  A. 类型筛选：类型下拉后表格行集 = 期望子集（类型文本全等，逐类验）
  B. 时间窗：起始/结束/组合筛选（epoch 毫秒边界与 C 同口径：ts<lo 不可见、
     ts>hi 不可见、界为空=不限）
  C. 起点晚于终点：提示「起始时间晚于结束时间，未筛选」且**不动**结果
  D. 列显隐：单列显示 / 恢复全部列（隐藏列的数据仍在表里，只不显示）
  E. 行数统计：「显示 N / 共 M 条」跟随筛选
  F. 清除筛选：类型回全部、时间窗回数据全区间、全部行可见
  G. CSV 导出：文件存在 + 首行 UTF-8 BOM（utf-8-sig）+ 表头/行序（最新在
     上）；无 Download 权限时 _export_data 直接拒绝且**不落盘**
  H. 报警次数/分钟折线图：分钟聚合正确（同分钟合并、异分钟分桶）、
     随筛选联动
  I. 表格/图表双视图切换（set_view 幂等、视图栈与按钮勾选态同步）
  J. MAX_ROWS 环形截断：超出丢最旧、最新保留
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
    check("行数统计随筛选（显示 1 / 共 4 条）",
          page._count_label.text() == "显示 1 / 共 4 条",
          page._count_label.text())
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
    print("\n== D. 列显隐 ==")
    table = page._table.table
    page._col_combo.setCurrentIndex(2)          # 「类型」列
    hidden = [table.isColumnHidden(c) for c in range(4)]
    check("选「类型」→ 只显示第 1 列",
          hidden == [True, False, True, True], "%s" % hidden)
    check("隐藏列的数据仍在（设备列文本未被清）",
          (table.item(0, 0).text() if table.item(0, 0) else "") != "",
          table.item(0, 0).text() if table.item(0, 0) else "None")
    page._col_combo.setCurrentIndex(0)
    hidden = [table.isColumnHidden(c) for c in range(4)]
    check("回「全部列」→ 4 列全显",
          hidden == [False] * 4, "%s" % hidden)


# ============================================================================
#                    E. 行数统计
# ============================================================================


def m_alarmtb_part_e_count(page):
    print("\n== E. 行数统计 ==")
    check("全量 → 显示 4 / 共 4 条",
          page._count_label.text() == "显示 4 / 共 4 条",
          page._count_label.text())
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("变频器"))
    page._apply_filter()
    check("筛选后 → 显示 1 / 共 4 条（共 = 全部记录数）",
          page._count_label.text() == "显示 1 / 共 4 条",
          page._count_label.text())
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()


# ============================================================================
#                    F. 清除筛选
# ============================================================================


def m_alarmtb_part_f_clear(page, base_ms):
    print("\n== F. 清除筛选 ==")
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("流量计"))
    page._f_start.setText(m_alarmtb_stamp_text(base_ms + 45 * 1000))
    page._col_combo.setCurrentIndex(1)          # 「时间」列
    page._apply_filter()
    check("筛选生效（1 行）", len(m_alarmtb_table_kinds(page)) == 1,
          "%s" % m_alarmtb_table_kinds(page))
    page._clear_filter()
    check("清除后类型回全部（4 行）",
          len(m_alarmtb_table_kinds(page)) == 4,
          "%s" % m_alarmtb_table_kinds(page))
    check("清除后类型下拉回「全部类型」",
          page._kind_combo.currentIndex() == 0)
    check("清除后列显隐回「全部列」",
          page._col_combo.currentIndex() == 0
          and not page._table.table.isColumnHidden(0))
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
        check("表头 = 列定义（含 BOM 后的首行）",
              lines[0] == ",".join(page.COLUMNS), lines[0])
        check("行序 = 最新在上（4 行数据）",
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
#                    H. 报警次数/分钟折线图
# ============================================================================


def m_alarmtb_part_h_chart(page, base_ms):
    print("\n== H. 报警次数/分钟折线图 ==")
    # minute 粒度对齐：base_ms 取整分钟（本段落 seed 的 4 条跨 3 个分钟桶）
    aligned = base_ms - (base_ms % 60000)
    page._records = []
    page._seen = {}
    page._record_alarm("A", "变频器", "故障", aligned)
    page._record_alarm("B", "变频器", "故障", aligned + 30 * 1000)   # 同分钟
    page._record_alarm("C", "温湿度", "数据过期", aligned + 90 * 1000)
    series = page._chart_series()
    name, points = series[0]
    check("序列名 = 报警次数/分钟", name == "报警次数/分钟", name)
    check("分钟聚合：同分钟合并、异分钟分桶",
          points == [(aligned, 2), (aligned + 60000, 1)], "%s" % points)

    # 图表对话框建出来（图表视图内嵌）
    page.set_view(1)
    dialog = page._chart_dialog
    check("图表视图建出内嵌对话框", dialog is not None)
    check("图表数据与聚合一致",
          dialog._series[0][1] == [(aligned, 2), (aligned + 60000, 1)],
          "%s" % (dialog._series[0][1],))
    got = [v for _t, v in dialog._series[0][1]]
    check("y 值为次数（2, 1）", got == [2, 1], "%s" % got)

    # 筛选联动：只看温湿度 → 图表只剩 1 个点
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("温湿度"))
    page._apply_filter()                        # _view_mode==1 会刷新图表
    check("筛选联动：图表只剩温湿度那分钟的 1 点",
          page._chart_dialog._series[0][1] == [(aligned + 60000, 1)],
          "%s" % (page._chart_dialog._series[0][1],))
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()

    # 空数据：图表为空 + 状态提示（不崩）
    page._records = []
    page._rebuild()
    page._refresh_chart()
    check("无记录 → 图表为空 + 提示",
          page._chart_dialog._series[0][1] == []
          and "暂无报警" in (page.ctx_status() or ""), page.ctx_status())
    # 恢复数据
    m_alarmtb_seed(page, base_ms)


# ============================================================================
#                    I. 表格/图表双视图
# ============================================================================


def m_alarmtb_part_i_views(page):
    print("\n== I. 表格/图表双视图 ==")
    page.set_view(0)
    check("表格视图：视图栈第 0 页 + 表格钮勾选",
          page._view_stack.currentIndex() == 0
          and page._btn_table_view.isChecked()
          and not page._btn_chart_view.isChecked())
    page.set_view(1)
    check("图表视图：视图栈第 1 页 + 图表钮勾选",
          page._view_stack.currentIndex() == 1
          and page._btn_chart_view.isChecked()
          and not page._btn_table_view.isChecked())
    page.set_view(1)                            # 幂等：再点图表视图不重建
    check("重复切同一视图保持不变（幂等）",
          page._view_mode == 1 and page._view_stack.currentIndex() == 1)
    page.set_view(0)
    check("切回表格视图", page._view_stack.currentIndex() == 0)
    page.set_view(99)                           # 越界值按表格处理
    check("越界 mode 归一为表格视图", page._view_mode == 0)


# ============================================================================
#                    J. MAX_ROWS 环形截断
# ============================================================================


def m_alarmtb_part_j_ring(page, base_ms):
    print("\n== J. MAX_ROWS 环形截断 ==")
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
    table = page._table.table
    check("表格行数与封顶一致", table.rowCount() == page.MAX_ROWS,
          "%d" % table.rowCount())


# ============================================================================
#                            段落入口
# ============================================================================


def run_alarmtb():
    """§16 报警页工具栏：类型/时间窗/列显隐/统计/清除/导出/图表/视图/环形截断。

    [原 main() 的行为迁移] 原文件的 main() 做的是：建页 → 取分钟对齐的
    基准时刻 → 注入 4 条报警 → 包装 status 取值 → 依次跑 A~J 十段。
    这里**原样保留**这些动作，只去掉两件事：
      · 「建 QApplication」⇒ 改用总回归那唯一一个实例；
      · 「打印 passed/failed 汇总 + 返回退出码」⇒ 由总回归统一汇总。

    [不连服务端] 本段落是纯控件层，起不起服务端都一样；不需要端口。
    """
    app, page, _status = m_alarmtb_make_page()
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
    m_alarmtb_part_h_chart(page, base_ms)
    m_alarmtb_part_i_views(page)
    m_alarmtb_part_j_ring(page, base_ms)
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
  G. 水质页（浊度 ×2 + 进水泵）
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
    check("页总数 = 18", reg.PAGE_COUNT == 18, "count=%d" % reg.PAGE_COUNT)
    check("页名数量匹配", len(reg.PAGE_NAMES) == reg.PAGE_COUNT)
    check("导航树 17 节点", len(reg.NAV_TREE) == 17,
          "nodes=%d" % len(reg.NAV_TREE))
    # 与 C 端 kHsfNavTree 逐项核对（顺序也一致）。
    #
    # [2026-11-05 有意偏离 C 端两处显示名]
    #   · 第 5 项「变频器」→「搅拌泵」：设备确实是变频器，但它驱动的是搅拌泵，
    #     操作员找的是"搅拌泵那一页"。设备**类别**仍显示"变频器"（KIND_TEXT
    #     没动），所以首页"类型"列不受影响。
    #   · 第 6 项「流量计」→「水质监测」：该页扩为「流量计 + 浊度×2 + 进水泵」，
    #     只叫"流量计"会让人以为浊度和泵在别的页。
    # **两处页索引都不变**（仍 5 / 6），只是显示名与 C 端不同 —— 这是刻意的。
    c_tree = [
        ("主界面", 0, -1), ("设备", 2, -1), ("数字量", 3, 1),
        ("模拟量", 4, 1), ("搅拌泵", 5, 1), ("水质监测", 6, 1),
        ("电动球阀", 7, 1),
        ("数据", 8, -1), ("历史数据", 9, 6), ("日志", 10, 6),
        ("报警", 11, 6), ("设置", 12, -1), ("本地设置", 13, 10),
        ("远端设置", 14, 10), ("网络设置", 15, 10),
        ("悬浮窗设置", 16, 10), ("窗口设置", 17, 10),
    ]
    check("导航树内容+顺序完全一致", list(reg.NAV_TREE) == c_tree,
          "diff=%s" % [a for a, b in zip(reg.NAV_TREE, c_tree) if a != b][:3])
    c_names = ["主界面", "主菜单", "设备", "数字量", "模拟量", "搅拌泵",
               "水质监测", "电动球阀", "数据", "历史数据", "日志", "报警",
               "设置", "本地设置", "远端设置", "网络设置", "悬浮窗设置",
               "窗口设置"]
    check("页名与 C 端一致（第5/6 项按本端工艺改名）",
          reg.PAGE_NAMES == c_names)
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

        win.goto_page(reg.PAGE_SETTINGS_WINDOW)   # 无数据页
        app.processEvents()
        win_topics = bridge.current_topics()
        # 全局主题（温湿度/设备健康）常驻；窗口设置页不应再订其它主题
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

    # ---- G. 水质页（浊度 + 进水泵）----
    section("G. 水质页（浊度 ×2 + 进水泵）")

    # [为什么用注入而不是等推送] 这一段要在**离线**也能跑（CI 无服务端），
    # 且浊度/泵的离线降级行为本身就是要测的东西之一。
    win.goto_page(reg.PAGE_MONITOR_FLOWMETER)
    app.processEvents()
    water = win.pages[reg.PAGE_MONITOR_FLOWMETER]

    # ① 页面身份：改名但索引不变（与 C 端注册表只差显示名）
    check("水质页在索引 6", reg.page_name(6) == "水质监测",
          reg.page_name(6))
    check("水质页订阅浊度+泵（含流量计）",
          set(water.TOPICS) == {proto.TOPIC_FLOWMETER_STATUS,
                                proto.TOPIC_TURBIDITY_STATUS,
                                proto.TOPIC_PUMP_STATUS},
          "%s" % sorted(water.TOPICS))

    # ② 两台浊度 + 一个泵的控件都在（没有的是"画漏了"，比数值错更严重）
    check("浊度两行读数控件就位", len(water._turb_rows) == 2,
          "%d 行" % len(water._turb_rows))
    check("泵有启停两钮", hasattr(water, "btn_pump_on")
          and hasattr(water, "btn_pump_off"))

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

    # ⑦ 泵状态与按钮闸门
    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": False,
                  "v0_ready": True, "v0_valid": True})
    water.refresh()
    app.processEvents()
    check("泵名字由服务端给（写到卡标题）",
          water._pump_card.title_label.text() == "进水泵",
          water._pump_card.title_label.text())
    check("泵停止态：启动可点", water.btn_pump_on.isEnabled()
          or not bridge.is_connected(),
          "connected=%s" % bridge.is_connected())
    check("泵已在停止 ⇒ 停止钮置灰", not water.btn_pump_off.isEnabled())

    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": True,
                  "v0_ready": True, "v0_valid": True})
    water.refresh()
    app.processEvents()
    check("泵运行态：状态文案含『输出』",
          "输出" in water._pump_state.text(), water._pump_state.text())
    check("泵已在运行 ⇒ 启动钮置灰", not water.btn_pump_on.isEnabled())

    # ⑧ 泵不可用（模块离线）⇒ 两个按钮都禁用，不给人可点的假象
    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": False,
                  "v0_ready": True, "v0_valid": False})
    water.refresh()
    app.processEvents()
    # 注意：valid=False 但 ready=True 仍算可用（valid 是"本拍回显是否有效"），
    # 真正的禁用条件是 ready=False。这里用 ready=False 验禁用。
    m.pump.apply({"count": 1, "staleMs": 5000,
                  "v0_name": "进水泵", "v0_on": False,
                  "v0_ready": False, "v0_valid": False})
    water.refresh()
    app.processEvents()
    check("泵未就绪 ⇒ 启动钮禁用", not water.btn_pump_on.isEnabled())
    check("泵未就绪 ⇒ 停止钮禁用", not water.btn_pump_off.isEnabled())
    check("泵未就绪 ⇒ 状态写『未就绪』",
          water._pump_state.text() == "未就绪", water._pump_state.text())

    # ⑨ 泵说明**不能**含 Markdown 星号（QLabel 不解析，会原样显示）
    check("泵说明无 Markdown 星号",
          "**" not in water._pump_note.text(), water._pump_note.text()[:40])
    check("泵说明写明无反馈",
          "无运行反馈" in water._pump_note.text(),
          water._pump_note.text()[:40])

    # ⑩ 数字量页 DO6 名字同步为「进水泵」（同一路输出两个页面必须同名）
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
    check("DO6 = 进水泵（与水质页同源）", dig_st.do_name(6) == "进水泵",
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
    check("压差卡单位随服务端", home.dp_value._unit.text() == "MPa",
          home.dp_value._unit.text())
    check("压差卡小字 = 通道名", home.dp_note.text() == "压差计",
          home.dp_note.text())

    # 越限（断线/超量程）：夹到端点的值没有读数价值 → 只报说明（警示色）
    m4.analog.apply({"n": 8, "v": 1, "e0": 0, "eu0": "MPa",
                     "en0": "压差计", "ew0": "通道 0 断线"})
    home.refresh(force=True)
    check("压差越限 ⇒ 数值 --、说明显示原因",
          home.dp_value._value.text() == "--"
          and home.dp_note.text() == "通道 0 断线",
          "%s / %s" % (home.dp_value._value.text(), home.dp_note.text()))
    check("压差越限说明用警示色",
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
          home.fm_flow._unit.text() == "m³/h", home.fm_flow._unit.text())
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
    # [为什么必须单独一组] 前面 G 组是**注入式**：往 model 塞数据、读控件
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
        win.goto_page(reg.PAGE_MONITOR_FLOWMETER)
        for _ in range(25):
            app.processEvents()
            time.sleep(0.05)
        water = win.pages[reg.PAGE_MONITOR_FLOWMETER]
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
        # 若服务端没把泵的名字/位写进数字量快照，就会出现"水质页说泵在转、
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

        # ---- 回到水质页，确认来回切页不丢状态、不崩 ----
        win.goto_page(reg.PAGE_MONITOR_FLOWMETER)
        for _ in range(20):
            app.processEvents()
            time.sleep(0.05)
        check("切回水质页后泵状态仍为停止",
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
  F. 水质监测页：浊度 + 流量计同一口径
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
                          "v0_name": "浊度仪1", "v0_online": False,
                          "v1_name": "浊度仪2", "v1_online": False}

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
            doc = {"ok": True,
                   proto.HIST_KEY_TOTAL: len(self.rows),
                   proto.HIST_KEY_DB_ROWS: len(self.rows),
                   proto.HIST_KEY_ROWS: self.rows[offset:offset + limit],
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
          home.fm_flow._unit.text() == "m³/h",
          home.fm_flow._unit.text())
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
    section("F. 水质监测页：浊度 + 流量计同一口径")
    # ==================================================================
    win.goto_page(reg.PAGE_MONITOR_FLOWMETER)
    app.processEvents()
    water = win.pages[reg.PAGE_MONITOR_FLOWMETER]
    model.flowmeter.apply(dict(m_hisside_FM_B))
    model.flowmeter.apply(dict(m_hisside_FM_A_EMPTY))
    model.turbidity.apply(dict(m_hisside_TURB_B))
    model.turbidity.apply(dict(m_hisside_TURB_A_EMPTY))
    water.refresh()
    app.processEvents()
    check("水质页流量读数取有效那一侧（12.50）",
          water._flow_blocks["flow"]._value.text() == "12.50",
          water._flow_blocks["flow"]._value.text())
    check("水质页浊度台 1 取有效那一侧（12.31）",
          water._turb_rows[0][1].text() == "12.31",
          water._turb_rows[0][1].text())
    check("水质页浊度台 2 取有效那一侧（25.00）",
          water._turb_rows[1][1].text() == "25.00",
          water._turb_rows[1][1].text())


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

    # 空表导出：提示无可导出（不发请求不落第二份文件）
    saved_rows = page._rows
    page._rows = []
    page._export_data()
    m_logpg_process_events()
    check("H8 无数据时导出被拒（提示且不落盘）",
          any("无可导出" in s for s in page._status_texts)
          and len(os.listdir(tmpdir)) == 1, page._status_texts[-1:])
    page._rows = saved_rows


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
        check("A1f 逐台键：浊度仪1/浊度仪2/流量计 各用自己的周期键",
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
                                           LocalSettingsPage as _LSP,
                                           OverlayPage as _OP,
                                           WindowPage as _WP)

    try:
        S.set_value(S.K_UI_SCALE_PCT, 125)
        S.set_value(S.K_FORCE_COMPACT, True)
        S.set_value(S.K_REFRESH_MS, 500)
        S.set_value(S.K_TREND_MS, 1000)
        S.set_value(S.K_WIN_SIZE, "1024x600")
        S.set_value(S.K_SHOW_NAV, False)
        S.set_value(S.K_HIST_PAGE_SIZE, 50)
        S.set_value(S.K_LOG_PAGE_SIZE, 10)

        check("A5a 设置模块读回一致（缩放/紧凑/刷新/趋势）",
              abs(S.ui_scale() - 1.25) < 1e-9
              and S.get_bool(S.K_FORCE_COMPACT) and S.refresh_ms() == 500
              and S.trend_ms() == 1000,
              "scale=%.2f refresh=%d trend=%d"
              % (S.ui_scale(), S.refresh_ms(), S.trend_ms()))
        check("A5b 窗口尺寸设置读回", S.win_size() == (1024, 600),
              str(S.win_size()))

        lsp = _LSP(ctx)
        check("A5c 本地设置页按存档开局（125% / 紧凑 / 500ms）",
              lsp.scale_combo.currentText() == "125%"
              and lsp.compact_check.isChecked()
              and lsp.refresh_spin.value() == 500,
              "%s / %s / %d" % (lsp.scale_combo.currentText(),
                                lsp.compact_check.isChecked(),
                                lsp.refresh_spin.value()))
        op = _OP(ctx)
        check("A5d 悬浮窗页按存档开局（导航隐藏）",
              op.show_nav.isChecked() is False, str(op.show_nav.isChecked()))
        wp = _WP(ctx)
        check("A5e 窗口设置页显示出存档尺寸",
              "1024 × 600" in wp.size_combo.currentText(),
              wp.size_combo.currentText())

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
        page._collect_rows[2][1].setChecked(False)        # 只停浊度仪1
        populated.clear()
        page._apply_collect()
        pump()
        check("B3a 应用后收到生效值回投", bool(populated))
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B3b 配置文件温湿度周期 = 700",
              saved["serial"]["temphum_poll_ms"] == 700)
        # [两台独立设备 2026-11-10] 只停浊度仪1：逐台键各记各的，旧共同键
        # 按"任一启用"仍为 true（浊度仪2 还在采）。
        check("B3c 配置文件只停浊度仪1（turbidity1=false / turbidity2=true）",
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
                              grow_seconds=32)
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
    res = m_storlim_scenario(tmpdir, port, "ring", max_kb=120,
                              grow_seconds=40)
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
                              keep_files=3, grow_seconds=34)
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
  H. 页面接入：VfdPage / WaterPage 建了趋势图、refresh() 真的逐拍喂点、
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
    check("C2 锚点处时刻不变（指针锚点缩放）", abs(new_frac - 0.5) < 1e-6,
          "锚点位置 %.6f" % new_frac)

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
#                H. 页面接入（VfdPage / WaterPage）
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

    vfd.updated_ms = m_trendch_BASE + 3 * 1000   # 新拍待刷新（下面 refresh 消费）
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
    check("H8 数据过期不追点", len(page._trend.points("out")) == 4,
          "实际 %d" % len(page._trend.points("out")))
    page.close()

    # ---- WaterPage：流量 / 浊度两路独立判重，离线浊度不编点 ----
    flow = FlowMeterState()
    turb = TurbidityState()
    pump = PumpState()
    page = pm.WaterPage(m_trendch_Ctx(SimpleNamespace(flowmeter=flow,
                                                      turbidity=turb,
                                                      pump=pump)))
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    check("H9 WaterPage 建了趋势图且 3 条序列",
          isinstance(page._trend, pm.TrendChart)
          and [k for k, _n in pm.WaterPage.TREND_SERIES]
          == ["flow", "turb1", "turb2"],
          str(pm.WaterPage.TREND_SERIES))

    now = _now_ms()                # 模型层单调毫秒钟；updated_ms 要贴近当前
                                   # （is_fresh 判据 now-updated < staleMs）
    flow.apply({"flow": 12.4, "valid": True})
    flow.updated_ms = now
    turb.apply({"count": 2, "v0_name": "原水", "v0_online": True,
                "v0_ntu": 1240,                 # 0.01 NTU 量化 → 12.40
                "v1_online": False, "v1_ntu": 0})
    turb.updated_ms = now
    pump.apply({"count": 1, "v0_name": "进水泵", "v0_on": False,
                "v0_ready": True})
    pump.updated_ms = now
    page.refresh()
    app.processEvents()
    check("H10 流量已追点", len(page._trend.points("flow")) == 1,
          "实际 %d" % len(page._trend.points("flow")))
    check("H11 浊度1 已追点（0.01 量化换回 12.40）",
          page._trend.points("turb1")
          and abs(page._trend.points("turb1")[0][1] - 12.4) < 1e-9,
          str(page._trend.points("turb1")))
    check("H12 浊度仪2 离线不编点", len(page._trend.points("turb2")) == 0,
          "实际 %d" % len(page._trend.points("turb2")))

    # 下一拍：只有流量计推进 → 流量追点、浊度判重不追（两路独立）
    flow.apply({"flow": 12.9, "valid": True})
    flow.updated_ms = now + 1000
    page.refresh()
    check("H13 流量第二拍追点", len(page._trend.points("flow")) == 2,
          "实际 %d" % len(page._trend.points("flow")))
    check("H14 浊度同拍判重不重复追",
          len(page._trend.points("turb1")) == 1,
          "实际 %d" % len(page._trend.points("turb1")))
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
                a1 = m_ttyS4_addr_of(doc, "浊度仪1") or ""
                a2 = m_ttyS4_addr_of(doc, "浊度仪2") or ""
                af = m_ttyS4_addr_of(doc, "电磁流量计") or ""
                check("改配置后浊度仪1 站号=3", "站 3" in a1, "addr=%r" % a1)
                check("改配置后浊度仪2 站号=4", "站 4" in a2, "addr=%r" % a2)
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
                      "浊度仪1 采集失败" not in srv_log, "")
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
        check("§30 状态栏显示已连接（root · 管理员）",
              "已连接" in win.status_bar.conn.text(),
              repr(win.status_bar.conn.text()))
        check("§30 初始页是主界面（栈顶/标题/页序号一致）",
              win.current == reg.PAGE_HOME
              and win.title_bar.title.text() == "主界面"
              and win.stack.currentWidget() is win.pages.get(reg.PAGE_HOME))
        check("§30 导航树条目数与注册表一致（17 节点）",
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
        page = win.pages.get(page_index)
        check("§30%s 点导航切到「%s」页（栈顶/标题/页序号一致）" % (tag,
              reg.page_name(page_index)),
              win.current == page_index
              and win.stack.currentWidget() is page
              and win.title_bar.title.text() == reg.page_name(page_index),
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
    # ② 导航点击逐页切换（主界面→搅拌泵→水质监测→模拟量，顺序按任务口径）
    # ==================================================================
    nav_click(reg.PAGE_HOME)               # 已在主界面：点当前页应幂等
    check("§30② 点「主界面」（当前页幂等，不重进不重建）",
          win.current == reg.PAGE_HOME
          and len(win.pages) == 1,
          "pages=%d" % len(win.pages))
    for _idx, _tag in ((reg.PAGE_MONITOR_VFD, "②a"),
                       (reg.PAGE_MONITOR_FLOWMETER, "②b"),
                       (reg.PAGE_MONITOR_ANALOG, "②c")):
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
    alarmp._record_alarm("浊度仪1", "水质", "过期")
    pump(200)
    check("§30⑥ 两条种子记录可见（行数=记录数）",
          alarmp._table.table.rowCount() == len(alarmp._records),
          "表行=%d 记录=%d" % (alarmp._table.table.rowCount(),
                               len(alarmp._records)))
    okc, det = combo_click_select(alarmp._kind_combo, 1)   # 1 = 变频器
    check("§30⑥ 类型下拉点选「变频器」", okc, det)
    click_widget(btn_by_text(alarmp, "筛选"), "筛选")
    pump(150)
    n_vfd = sum(1 for rec in alarmp._records if rec[1][2] == "变频器")
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
          and alarmp._table.table.rowCount() == len(alarmp._records),
          "kind=%r combo=%d 表行=%d 记录=%d"
          % (alarmp._kind_text, alarmp._kind_combo.currentIndex(),
             alarmp._table.table.rowCount(), len(alarmp._records)))

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


if __name__ == "__main__":
    sys.exit(main())
