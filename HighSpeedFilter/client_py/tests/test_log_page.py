# -*- coding: utf-8 -*-
"""日志页（服务端运行日志查询，CMD_LOG_PAGE=0x0119）测试。

    python tests/test_log_page.py

任务口径（2026-11-09 对齐 C 端 HsfDataLog.c：logRequest / logFillTable /
logApplyFilter / logClearFilter / logPageSizeSlot / logExportStart）：

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
  L. 真实往返：能连 127.0.0.1:6100 的 C 服务端时登录 root:root 做协议级
     与页面级用例（连不上则整段 SKIP，不算失败）。

离线段不连任何服务端：在 bridge 上 monkeypatch log_page（伪造 0x0119
应答），先验证纯客户端逻辑——协议字段真伪由 L 段对真服务端兜底。
"""

import os
import re
import shutil
import socket
import sys
import tempfile
import time

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)

from hsfsrv_client import proto          # noqa: E402

PASS = 0
FAIL = 0
SKIP = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def skip(name, detail=""):
    global SKIP
    SKIP += 1
    print("  [SKIP] %s %s" % (name, detail))


BASE_MS = int(time.mktime(time.strptime(
    "2026-11-09 12:00:00", "%Y-%m-%d %H:%M:%S"))) * 1000


def make_rows(n=25):
    """合成服务端日志（时间升序生成后倒序 = 服务端取页口径 DESC）。"""
    cats = ["conn", "ctrl", "alarm", "auto", "auth"]
    rows = [{"ts": BASE_MS + i * 1000,
             "category": cats[i % len(cats)],
             "text": "事件 %02d" % i}
            for i in range(n)]
    rows.reverse()
    return rows


class FakeLogServer(object):
    """在 bridge 上伪造 0x0119 应答：记录每笔请求 + 按服务端口径回放。

    behavior: "ok"（正常分页）/ "nok:<err>"（否定应答，err 原样回）。
    hold=True 时只记录不回包（测应答归属/防串页，用例手工 emit）。
    """

    def __init__(self, rows=None):
        self.rows = rows if rows is not None else make_rows()
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


def make_page(role=None):
    """离屏建一个 LogPage（真 LinkBridge + 假数据源，稍后由用例 attach）。"""
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
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

def part_0_proto():
    print("== 0. 协议常量与 C 头逐字对齐 ==")
    hpath = os.path.join(os.path.dirname(CLIENT_ROOT), "common",
                         "XhsfLinkProto.h")
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

def QApplication_processEvents():
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is not None:
        app.processEvents()


def part_a_fill(page, fake):
    print("== A. 铺表（入页即加载 + 行对象解析）==")
    fake.attach(page.ctx.bridge)
    page.on_enter()
    QApplication_processEvents()
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
        time.localtime((BASE_MS + 24 * 1000) / 1000.0))
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


def part_b_paging(page, fake):
    print("== B. 翻页锚点（翻页沿用、新查询重置）==")
    anchor0 = fake.requests[-1]["before_ts"]
    page._next_page()
    QApplication_processEvents()
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
    QApplication_processEvents()
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
    QApplication_processEvents()
    check("B10 末页连点下一页不再发请求",
          len(fake.requests) == n_req, "%d → %d" % (n_req, len(fake.requests)))
    page._prev_page()
    QApplication_processEvents()


def part_c_attribution(page, fake):
    print("== C. 应答归属（offset+limit 回显判定，防串页）==")
    fake.hold = True
    n_req = len(fake.requests)
    page._next_page()                      # 在途：offset=20 不回包
    QApplication_processEvents()
    check("C1 请求已发出且挂起", len(fake.requests) == n_req + 1
          and page._inflight and page._pending_offset == 20,
          "pending=%s inflight=%s" % (page._pending_offset, page._inflight))
    n_rows_before = page._table.table.rowCount()

    # 迟到的旧应答（offset/limit 对不上）→ 丢弃，绝不铺表，且在途闸不动
    stale = {"ok": True, "rows": make_rows(20), "total": 25, "count": 20,
             "offset": 0, "limit": 20}
    page.logReady.emit(stale)
    QApplication_processEvents()
    check("C2 回显 offset/limit 不匹配的应答被丢弃（表格未动）",
          page._table.table.rowCount() == n_rows_before,
          "rows=%d" % page._table.table.rowCount())
    check("C2b 丢弃迟到应答不提前放行在途闸",
          page._inflight and page._pending_offset == 20,
          "inflight=%s pending=%s" % (page._inflight, page._pending_offset))

    # 在途期间翻页被闸住（同一时刻只允许一个请求）
    page._next_page()
    QApplication_processEvents()
    check("C3 在途请求期间不再叠发新请求", len(fake.requests) == n_req + 1,
          "requests=%d" % len(fake.requests))

    # 正主应答（回显 pending 的 offset/limit）→ 铺表
    good = {"ok": True, "rows": make_rows(5), "total": 25, "count": 5,
            "offset": 20, "limit": 20}
    page.logReady.emit(good)
    QApplication_processEvents()
    check("C4 回显匹配的应答正常铺表（5 行）",
          page._table.table.rowCount() == 5 and not page._inflight,
          "rows=%d inflight=%s" % (page._table.table.rowCount(),
                                   page._inflight))
    fake.hold = False
    page._prev_page()
    QApplication_processEvents()


def part_d_filter(page, fake):
    print("== D. 筛选（类别立即生效 / 时间窗+关键字按筛选 / 清除保留类别）==")
    old_anchor = fake.requests[-1]["before_ts"]

    # ---- 类别下拉：立即重查、锚点归零、回第 1 页 ----
    page._cat_combo.setCurrentIndex(1)     # 连接（conn）
    QApplication_processEvents()
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
    QApplication_processEvents()
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
    QApplication_processEvents()
    page._next_page()
    QApplication_processEvents()
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
    QApplication_processEvents()
    check("D12 起点晚于终点：提示且不发请求",
          len(fake.requests) == n_req
          and any("晚于" in s for s in page._status_texts),
          "%d requests, status=%s" % (len(fake.requests),
                                      page._status_texts[-1:]))

    # ---- 时间格式错误：不应用 ----
    page._f_start.setText("not-a-time")
    page._apply_filter()
    QApplication_processEvents()
    check("D13 时间格式不对：提示且不发请求",
          len(fake.requests) == n_req
          and any("格式不对" in s for s in page._status_texts))

    # ---- 清除筛选：时间/关键字清空、类别保持、锚点归零 ----
    page._f_start.setText("2026-11-09 00:00")
    page._clear_filter()
    QApplication_processEvents()
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
    QApplication_processEvents()


def part_e_pagesize(page, fake):
    print("== E. 每页条数（改档回第 1 页 + 锚点归零）==")
    old_anchor = fake.requests[-1]["before_ts"]
    time.sleep(0.005)          # 锚点取本地"现在"，留出毫秒间隙使新锚严格更大
    idx = page._page_size_box.findData(50)
    page._page_size_box.setCurrentIndex(idx)
    QApplication_processEvents()
    req = fake.requests[-1]
    check("E1 改页大小生效（limit=50）", req["limit"] == 50, str(req))
    check("E2 回到第 1 页（offset=0）", req["offset"] == 0, str(req))
    check("E3 锚点归零重取", req["before_ts"] > old_anchor, str(req))
    idx = page._page_size_box.findData(10)
    page._page_size_box.setCurrentIndex(idx)
    QApplication_processEvents()
    check("E4 换 10 条同样生效", fake.requests[-1]["limit"] == 10,
          str(fake.requests[-1]))
    idx = page._page_size_box.findData(proto.LOG_PAGE_DEFAULT)
    page._page_size_box.setCurrentIndex(idx)
    QApplication_processEvents()


def part_f_nok(page, fake):
    print("== F. ok=false（服务端明确拒绝，如落盘未启用）==")
    fake.behavior = "nok:落盘未启用：服务端没有日志可查"
    page._reload_current()
    QApplication_processEvents()
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
    QApplication_processEvents()
    check("F4 恢复后重查正常铺表", page._table.table.rowCount() == 20,
          "rows=%d" % page._table.table.rowCount())


def part_g_unknown(page, fake):
    print("== G. 老服务端『未知命令』否定应答（不崩 + 空表 + 可恢复）==")
    fake.behavior = "nok:未知命令"
    n_req = len(fake.requests)
    page.on_enter()
    QApplication_processEvents()
    check("G1 状态行提示不支持 0x0119（空表不崩）",
          any("0x0119" in s or "不支持" in s for s in page._status_texts)
          and page._table.table.rowCount() == 0,
          page._status_texts[-1:])
    check("G2 行数统计位显示升级提示",
          "不支持" in page._total_label.text(), page._total_label.text())
    n_after = len(fake.requests)
    page.on_enter()                        # 再入页：不再自动敲老服务端
    QApplication_processEvents()
    check("G3 已识别不支持后，入页不再自动重发",
          len(fake.requests) == n_after,
          "%d → %d" % (n_after, len(fake.requests)))

    # 显式操作仍可重试；服务端恢复支持后自动回到正常
    fake.behavior = "ok"
    page._apply_filter()                   # 显式筛选 = 给一次重试机会
    QApplication_processEvents()
    check("G4 显式筛选会重试（请求数 +1）",
          len(fake.requests) == n_after + 1,
          "%d → %d" % (n_after, len(fake.requests)))
    check("G5 服务端恢复后铺表正常（server_supported 复位）",
          page._table.table.rowCount() == 20 and page._server_supported,
          "rows=%d" % page._table.table.rowCount())
    page.on_enter()
    QApplication_processEvents()
    check("G6 恢复后入页恢复自动查询",
          len(fake.requests) == n_after + 2)


def part_h_export(page, fake, tmpdir):
    print("== H. 导出 CSV（utf-8-sig + PERM_DOWNLOAD 闸）==")
    # 无 Download 权限（匿名）：直接拒绝、不落盘（返回在对话框之前）
    page.ctx.bridge.role = proto.ROLE_ANONYMOUS
    n_req = len(fake.requests)
    page._export_data()
    QApplication_processEvents()
    check("H1 无 Download 权限：提示权限不足且不发请求不落盘",
          len(fake.requests) == n_req
          and any("权限不足" in s for s in page._status_texts)
          and not os.listdir(tmpdir),
          page._status_texts[-1:])

    # 有权限：直接调 _write_export（绕开保存对话框，无头环境不可用）
    page.ctx.bridge.role = proto.ROLE_USER
    path = os.path.join(tmpdir, "log_export.csv")
    page._write_export(path, page._rows)
    QApplication_processEvents()
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
    QApplication_processEvents()
    check("H8 无数据时导出被拒（提示且不落盘）",
          any("无可导出" in s for s in page._status_texts)
          and len(os.listdir(tmpdir)) == 1, page._status_texts[-1:])
    page._rows = saved_rows


def part_i_client_log(page):
    print("== I. 客户端运行日志折叠副视图（原有能力保留）==")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
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


def part_j_empty(page, fake):
    print("== J. 空库（0 行应答）行数统计 ==")
    fake.rows = []
    page.on_enter()
    QApplication_processEvents()
    check("J1 空库铺 0 行", page._table.table.rowCount() == 0,
          "rows=%d" % page._table.table.rowCount())
    check("J2 统计标注（无数据）",
          "共 0 条" in page._total_label.text()
          and "（无数据）" in page._total_label.text(),
          page._total_label.text())
    check("J3 翻页钮双双禁用",
          not page._btn_prev.isEnabled() and not page._btn_next.isEnabled())
    fake.rows = make_rows()
    page.on_enter()
    QApplication_processEvents()


# ============================================================================
#                L. 真实往返（127.0.0.1:6100 的 C 服务端）
# ============================================================================

def c_server_available(host="127.0.0.1", port=6100, timeout=0.8):
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.close()
        return True
    except OSError:
        return False


def live_page(bridge, timeout=9.0, **kw):
    """同步取一页运行日志（包一层等待，用例读起来像普通调用）。"""
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


def _int(doc, key, default=None):
    """应答整数键读取（total=0 是合法值，不能走 `or 缺省` 的写法）。"""
    value = doc.get(key)
    return default if value is None else int(value)


def part_live():
    print("== L. 真实往返（C 服务端 127.0.0.1:6100）==")
    if not c_server_available():
        skip("L 段整体（6100 未开放）")
        return
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
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
        first = live_page(bridge, offset=0, limit=10)
        check("L1 应答 ok", first.get("ok") is True,
              str(first.get("err") or first.get("transportError") or "")[:60])
        rows = first.get("rows") or []
        total = _int(first, "total", 0)
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
              _int(first, "count", len(rows)) == len(rows)
              and total >= len(rows),
              "count=%s rows=%d total=%d" % (first.get("count"), len(rows),
                                             total))
        check("L7 offset/limit 原样回显",
              _int(first, "offset", -1) == 0
              and _int(first, "limit", -1) == 10,
              "offset=%s limit=%s" % (first.get("offset"),
                                      first.get("limit")))

        # 锚定分页：锚点取**写入前沿之后**（now-8s ⇒ 该秒的行已全部落库），
        # 同一锚点翻页 ⇒ total 恒定、两页不重不漏。（锚点取"现在"时，秒粒
        # 度入库会让锚点所在秒内后到的行也落进窗口，同锚 total 会微涨——
        # 那是数据粒度问题不是锚点失效，故这里刻意用过去锚点做确定性断言。）
        anchor = int(time.time() * 1000) - 8000
        p0 = live_page(bridge, offset=0, limit=10, before_ts=anchor)
        p1 = live_page(bridge, offset=10, limit=10, before_ts=anchor)
        r0 = [r["ts"] for r in (p0.get("rows") or [])]
        r1 = [r["ts"] for r in (p1.get("rows") or [])]
        check("L8 锚定后两页 total 一致",
              p0.get("ok") and p1.get("ok")
              and _int(p0, "total", -1) == _int(p1, "total", -2),
              "%s vs %s" % (p0.get("total"), p1.get("total")))
        check("L9 页内时间正序（DESC 取页、页内正序输出）",
              all(a <= b for a, b in zip(r0, r0[1:])),
              "首=%s 末=%s" % (r0[0] if r0 else "-", r0[-1] if r0 else "-"))
        check("L10 翻页无重叠（第 2 页整体不晚于第 1 页最早行）",
              not r1 or not r0 or max(r1) <= min(r0),
              "max(p1)=%s min(p0)=%s" % (max(r1) if r1 else "-",
                                         min(r0) if r0 else "-"))
        check("L11 锚定后总数与不锚定的最新总数自洽",
              _int(p0, "total", -1) <= total,
              "anchored=%s fresh=%d" % (p0.get("total"), total))

        # 类别过滤：conn（刚连过，必有会话日志）
        conn = live_page(bridge, offset=0, limit=10, category="conn")
        crows = conn.get("rows") or []
        check("L12 category=conn 命中且行类别全为 conn",
              conn.get("ok") is True and bool(crows)
              and all(r["category"] == "conn" for r in crows),
              "total=%s" % conn.get("total"))

        # 关键字/时间窗下推：不可能命中 ⇒ total=0
        miss = live_page(bridge, offset=0, limit=10,
                         kw="绝不可能命中的词ZZZ")
        check("L13 kw 不命中 ⇒ total=0 且 rows 空",
              miss.get("ok") is True and _int(miss, "total", -1) == 0
              and not (miss.get("rows") or []),
              "total=%s" % miss.get("total"))
        future = live_page(bridge, offset=0, limit=10,
                           lo_ts=4102444800000)     # 2100-01-01
        check("L14 未来 loTs ⇒ total=0（时间窗生效）",
              future.get("ok") is True
              and _int(future, "total", -1) == 0,
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


def main():
    tmpdir = tempfile.mkdtemp(prefix="hsf_logpage_")
    try:
        part_0_proto()
        app, page, bridge, status = make_page()
        # status 列表同时喂给 page（测试断言读最近的状态栏文案）
        page._status_texts = status
        fake = FakeLogServer()
        part_a_fill(page, fake)
        part_b_paging(page, fake)
        part_c_attribution(page, fake)
        part_d_filter(page, fake)
        part_e_pagesize(page, fake)
        part_f_nok(page, fake)
        part_g_unknown(page, fake)
        part_h_export(page, fake, tmpdir)
        part_i_client_log(page)
        part_j_empty(page, fake)
        page.deleteLater()
        app.processEvents()
        part_live()
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("==== %d passed, %d failed, %d skipped ====" % (PASS, FAIL, SKIP))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
