# -*- coding: utf-8 -*-
"""历史数据分页 end-to-end 测试（真实服务端 + 真实 XLink 协议）。

    python tests/test_history_page.py

用户需求：
  「历史数据界面启动的时候不会加载服务器上的历史数据，加载下，为了
    防止数据过多打爆服务器，客户端这边设置分页，服务端页分页，一次
    取一页，一页多少条由客户端决定，但是总数要告知并实时获取，如果
    在历史数据界面的话」

本测试逐条对应：
  A. 协议层：CMD_HISTORY_PAGE 能取到服务端落盘的历史（**入页即加载**）
  B. 页大小由客户端决定（10/20/50 三种），服务端按 limit 返回
  C. 服务端分页正确：翻页无重叠无遗漏、总数始终如实上报
  D. 总数实时获取：库持续入库时，在历史页上周期探到的 total 会增长
  E. 客户端历史页：on_enter 自动取第 1 页、页码/总数标签、下页/上页、
     改页大小归零、离页停止轮询
  F. 兜底：服务端没开落盘时明确报错，不假装成功

[为什么用 1s 采样] 现场 5s 一行太慢，测试里压到 1s，20 秒就有 20 行，
够翻两页并观察总数增长。
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
SERVER_ROOT = os.path.join(os.path.dirname(CLIENT_ROOT), "server_py")
sys.path.insert(0, CLIENT_ROOT)

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


def wait(seconds):
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(0.05)


class Server(object):
    """独立配置目录的 sim 服务端（采样 1s，落盘到临时库）。"""

    def __init__(self, port, tmpdir, sample_ms=1000, enabled=True):
        self.port = port
        self.tmpdir = tmpdir
        self.sample_ms = sample_ms
        self.enabled = enabled
        self.proc = None
        self.bridge = None

    def start(self):
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
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(self.port),
             "--config", cfg_path],
            cwd=SERVER_ROOT, env=env,
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

def part_abc_protocol(srv):
    print("== A. 入页即加载服务端历史 ==")
    first = srv.page(offset=0, limit=20)
    check("A1 应答 ok", first.get("ok") is True,
          str(first.get("err") or first.get("transportError") or "")[:60])
    total = int(first.get("total") or 0)
    check("A2 服务端已有落盘数据（total > 0）", total > 0, "total=%d" % total)
    cols = list(first.get("cols") or [])
    check("A3 应答带列名（18 列）", len(cols) == 18, "cols=%d" % len(cols))
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
    wait(3.0)                       # 期间至少又多采 2~3 行
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

def part_d_live_total(srv):
    print("== D. 总数实时获取（库持续入库）==")
    before = int(srv.page(offset=0, limit=5).get("total") or 0)
    wait(4.0)                  # 1s 采样 → 至少 +3 行
    after = int(srv.page(offset=0, limit=5).get("total") or 0)
    check("D1 再次查询总数增长（实时性）", after > before,
          "%d → %d" % (before, after))
    latest = int(srv.page(offset=0, limit=5).get("latestTs") or 0)
    check("D2 latestTs 与采样同步（非 0）", latest > 0, str(latest))


# ============================================================================
#                    E/F. 客户端历史页行为
# ============================================================================

def part_ef_client(srv):
    print("== E. 客户端历史页（服务端模式）==")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    # [复用 A~D 段那条链路] 服务端 root 只允许单会话（policy_blocks_login，
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
        pass            # 链路由调用方（main）统一断开

    print("== F. 兜底：未开落盘时明确报错 ==")
    from hsfsrv_client.bridge import LinkBridge
    page2 = None
    tmp = tempfile.mkdtemp()
    srv2 = Server(port=srv.port + 7, tmpdir=tmp, enabled=False)
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

def part_g_filter(srv):
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
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
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


def part_i_protocol_keyword(srv):
    """协议层关键字下推（直接发 CMD_HISTORY_PAGE，不经 UI）。

    验证 kw / kwCols 从 BSON → handler 解析 → store WHERE 全链路，
    以及 total 与页内容同口径。UI 层另有 G 段。
    """
    print("== I. 协议层关键字下推 ==")
    # 不可能命中的词 ⇒ total=0（若服务端没把 kw 下推，必返回整库 >0）
    bad = srv.page(offset=0, limit=20, kw="绝不可能命中的词QQQ")
    check("I1 kw 下推：不可能命中的词 ⇒ total=0",
          bad.get("ok") is True and int(bad.get("total") or 0) == 0,
          "total=%s ok=%s" % (bad.get("total"), bad.get("ok")))
    check("I2 total=0 时 rows 也为空", not (bad.get("rows") or []),
          "rows=%d" % len(bad.get("rows") or []))

    # 年份命中（时间列），total>0 且每行时间列含 "2026"
    hit = srv.page(offset=0, limit=20, kw="2026")
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
    t_ref = int(hit.get("total") or 0)
    seen = []
    for off in range(3):
        p = srv.page(offset=off, limit=1, kw="2026")
        seen += [r[ts_idx] for r in (p.get("rows") or [])]
        check("I8.%d 翻页 total 恒定" % (off + 1),
              int(p.get("total") or 0) == t_ref,
              "total=%s ref=%d" % (p.get("total"), t_ref))
    check("I9 关键字分页行不重复", len(seen) == len(set(seen)),
          "ts=%s" % seen)


def main():
    tmp = tempfile.mkdtemp(prefix="hsf_histpage_")
    srv = Server(port=9177, tmpdir=tmp, sample_ms=1000)
    if not srv.start():
        print("[FAIL] 服务端启动失败")
        return 1
    try:
        ok, err = srv.connect()
        if not ok:
            print("[FAIL] 登录失败：%s" % err)
            return 1
        print("已连接 sim 服务端 :%d" % srv.port)
        check("A0 落盘已建立",
              bool(srv.bridge.request_sync(
                  __import__("hsfsrv_client.proto", fromlist=["x"])
                  .CMD_CONFIG_GET, timeout=5)))
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
        part_abc_protocol(srv)
        part_d_live_total(srv)
        part_ef_client(srv)
        part_g_filter(srv)
        part_i_protocol_keyword(srv)
    finally:
        srv.stop()
        shutil.rmtree(tmp, ignore_errors=True)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
