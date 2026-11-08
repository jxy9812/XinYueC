# -*- coding: utf-8 -*-
"""远端设置页与新控件的实际测试（用户要求：每个控件都要实测）。

    python tests/test_remote_settings_ui.py

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
"""

import json
import os
import subprocess
import sys
import tempfile
import threading
import time

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PyQt5.QtCore import QDateTime, Qt      # noqa: E402
from PyQt5.QtWidgets import QApplication, QLabel, QGridLayout  # noqa: E402

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def part_a_controls(app):
    """控件单测（离屏）。"""
    print("== A. 控件单测 ==")
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


def part_b_flow(app, port=6298):
    """设置流程 e2e：真服务端 + 真配置落盘。"""
    print("== B. 设置流程 e2e（sim 服务端 :%d）==" % port)
    client_py = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    server_py = os.path.join(os.path.dirname(client_py), "server_py")
    tmp = tempfile.mkdtemp(prefix="hsf_settings_test_")
    cfg_path = os.path.join(tmp, "hsf_server_py.json")

    proc = subprocess.Popen(
        [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(port),
         "--config", cfg_path],
        cwd=server_py, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        # 等服务端监听（轮询端口，最多 8s）。
        import socket
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
        page._collect_rows[2][1].setChecked(False)        # 浊度仪停用
        populated.clear()
        page._apply_collect()
        pump()
        check("B3a 应用后收到生效值回投", bool(populated))
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B3b 配置文件温湿度周期 = 700",
              saved["serial"]["temphum_poll_ms"] == 700)
        check("B3c 配置文件浊度仪开关 = false",
              saved["serial"]["turbidity_enabled"] is False)

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

        # ---- B5 恢复（温湿度 500 / 浊度开 / 路径空 / 采样 5000）----
        page._collect_rows[1][3].setValue(500)
        page._collect_rows[2][1].setChecked(True)
        page._apply_collect()
        pump()
        page._storage_path.setText("")
        page._apply_storage()
        pump()
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B5 恢复后配置文件与缺省口径一致",
              saved["serial"]["temphum_poll_ms"] == 500
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
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


def part_c_history_time(app):
    """历史页时间列含日期后的筛选/解析口径。"""
    print("== C. 历史页日期时间 ==")
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


def main():
    app = QApplication.instance() or QApplication(sys.argv[:1])
    part_a_controls(app)
    part_b_flow(app)
    part_c_history_time(app)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
