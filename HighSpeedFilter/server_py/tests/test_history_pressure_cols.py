# -*- coding: utf-8 -*-
"""历史「进水压力(MPa)/出水压力(MPa)」两列的取值与落库（2026-11-23 用户口径
「压力1和压力2也要进历史数据被采集」）。

[为什么单独一套] 这两列的链路是
    模拟量通道(raw mV) → 按**名字**找换算表 → valid 闸 → engineering()
    → `_history_snapshot()` 成品键 → store 线上列（末尾两列）→ 显示表达式
跨 config / app / store 三层；任何一层写错的表现都是"历史里压力永远 --"
（不崩、不报错、测试不红）⇒ 这里用假设备把中间值**钉死**：

    CH2 = 2000mV ⇒ 8.032mA ⇒ 压力1 = 0.252 MPa
    CH3 = 3000mV ⇒ 12.048mA ⇒ 压力2 = 0.503 MPa
    CH1 = 4975mV ⇒ 19.980mA ⇒ 电机温度 = 99.87 ℃（顺带锁同链路）

再验：通道无效 ⇒ 键缺失（入库 NULL，不是 0）；改了通道号 ⇒ 跟着名字走。

    python -I tests/test_history_pressure_cols.py
"""

import os
import shutil
import sys
import tempfile

# [输出编码 2026-11-14] cp936 编不了「⇒」⇒ 打印断言时整套被打断（见
# AGENTS）。统一 UTF-8 + errors="replace"。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import config as cfgmod          # noqa: E402
from hsfsrv import store                     # noqa: E402
from fake_devices import build_fake_app      # noqa: E402

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


def _app(basedir):
    """假设备业务层（不开服务端、不轮询；只借它的模拟量/变频器等对象）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.enable_serial = False
    return cfg, build_fake_app(cfg, log=lambda _m: None)


def _set_raw(app, ch, mv):
    app.analog.raw[ch] = int(mv)
    app.analog.valid[ch] = True


def main():
    basedir = tempfile.mkdtemp(prefix="hsf_hist_press_")
    try:
        cfg, app = _app(basedir)
        # ---- A. 列序契约（store 层）----
        print("\n[A] store 列序契约")
        check("A1 线上列末尾两列 = press1_mpa/press2_mpa（旧下标不变）",
              tuple(store.Store.HISTORY_COLUMNS[-2:])
              == ("press1_mpa", "press2_mpa"),
              "%r" % (store.Store.HISTORY_COLUMNS[-2:],))
        disp = list(store.Store.DISPLAY_COLUMNS)
        check("A2 显示列 19 项且压差后紧跟压力1/压力2",
              len(disp) == 19
              and disp[12:15] == ["压差(MPa)", "进水压力(MPa)", "出水压力(MPa)"],
              "%r" % (disp[12:15],))
        check("A3 DISPLAY_EXPR 与 DISPLAY_COLUMNS 等长",
              len(store.Store.DISPLAY_EXPR) == len(disp),
              "%d vs %d" % (len(store.Store.DISPLAY_EXPR), len(disp)))

        # ---- B. 快照取值（按名字找通道 + valid 闸）----
        print("\n[B] _history_snapshot 取值")
        _set_raw(app, 2, 2000)          # 压力1 缺省 CH2
        _set_raw(app, 3, 3000)          # 压力2 缺省 CH3
        _set_raw(app, 1, 4975)          # 电机温度缺省 CH1
        row = app._history_snapshot()
        check("B1 压力1 = 0.252 MPa（CH2 2000mV）",
              abs(float(row.get("press1_mpa", -1)) - 0.252) < 0.002,
              "press1_mpa=%r" % (row.get("press1_mpa"),))
        check("B2 压力2 = 0.503 MPa（CH3 3000mV）",
              abs(float(row.get("press2_mpa", -1)) - 0.503) < 0.002,
              "press2_mpa=%r" % (row.get("press2_mpa"),))
        check("B3 电机温度 99.87℃（CH1 4975mV，同链路）",
              abs(float(row.get("motor_temp", -1)) - 99.87) < 0.1,
              "motor_temp=%r" % (row.get("motor_temp"),))

        # 通道本拍无读数 ⇒ 键缺失（入库 NULL，不是 0）
        app.analog.valid[2] = False
        row = app._history_snapshot()
        check("B4 压力1 通道无效 ⇒ 键缺失（NULL，不是 0）",
              "press1_mpa" not in row,
              "keys=%r" % (sorted(k for k in row if "press" in k),))
        check("B5 另一路不受影响（压力2 仍在）",
              "press2_mpa" in row,
              "press2_mpa=%r" % (row.get("press2_mpa"),))

        # 改通道号 ⇒ 按名字跟到新通道（远端可配，不写死下标）
        cfg.analog.press1_channel = 4
        cfg.analog.rebuild_scales()
        _set_raw(app, 4, 4000)          # 16.064mA ⇒ 0.754 MPa
        app.analog.valid[5] = False
        row = app._history_snapshot()
        check("B6 压力1 通道改到 CH4 ⇒ 取值跟着名字走（0.754）",
              abs(float(row.get("press1_mpa", -1)) - 0.754) < 0.002,
              "press1_mpa=%r" % (row.get("press1_mpa"),))

        # ---- C. 落库 + SELECT 线上列（行值末尾两格）----
        print("\n[C] 入库与线上列")
        st = store.Store(basedir, log=lambda _m: None)
        if not st.open_for(""):
            check("C0 临时库可打开", False, basedir)
            raise SystemExit(1)
        row_with = dict(row)
        row_with["ts"] = 1750000000000
        st.insert_history(row_with)
        # 两路都无读数 ⇒ 两列都是 NULL
        app.analog.valid[4] = False
        app.analog.valid[3] = False
        row_without = app._history_snapshot()
        row_without["ts"] = 1750000005000
        st.insert_history(row_without)
        rows, total = st.query_history_page(limit=10)
        check("C1 两行都入库", len(rows) == 2 and total == 2,
              "rows=%d total=%d" % (len(rows), total))
        cols = list(store.Store.HISTORY_COLUMNS)
        i1, i2 = cols.index("press1_mpa"), cols.index("press2_mpa")
        first = rows[0]                   # 时间正序 ⇒ 第一行是带值那条
        check("C2 线上行末尾两格是压力1/压力2 的真值",
              abs(float(first[i1]) - 0.754) < 0.002
              and abs(float(first[i2]) - 0.503) < 0.002,
              "press1=%r press2=%r" % (first[i1], first[i2]))
        second = rows[1]
        check("C3 无读数那条入库 NULL（不是 0）",
              second[i1] is None and second[i2] is None,
              "press1=%r press2=%r" % (second[i1], second[i2]))

        # ---- D. 显示表达式（NULL → '--'）----
        print("\n[D] 显示表达式")
        exprs = list(store.Store.DISPLAY_EXPR)
        sel = "SELECT " + ", ".join("(%s)" % e for e in exprs) \
            + " FROM history ORDER BY ts ASC"
        texts = []
        try:
            texts = [list(t) for t in
                     st._reader_conn().execute(sel).fetchall()]
        except Exception as exc:                  # noqa: BLE001
            check("D0 显示表达式可执行", False, "%s" % exc)
        if texts:
            check("D1 显示文本 19 项",
                  len(texts[0]) == 19, "%d" % len(texts[0]))
            check("D2 有值行压力列三位小数",
                  texts[0][13] == "0.754" and texts[0][14] == "0.503",
                  "%r" % (texts[0][12:15],))
            check("D3 无值行压力列显示 --",
                  texts[1][13] == "--" and texts[1][14] == "--",
                  "%r" % (texts[1][12:15],))
        st.close()

        print("\n===== 历史压力两列: %d failed（PASS %d / FAIL %d）====="
              % (FAIL, PASS, FAIL))
        return 0 if FAIL == 0 else 1
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
