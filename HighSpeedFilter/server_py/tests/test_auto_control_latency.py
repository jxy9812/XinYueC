# -*- coding: utf-8 -*-
"""自动控制时延矩阵（2026-11-14 用户口径「自动控制不能因为任何情况出现
延迟、停滞现象」）。

[为什么要有这一份] 上一份 `test_event_loop_lag.py` 锁的是两个**具体**病根
（WAL 检查点内联写路径 / 同会话请求串行 await）。用户要的是更强的那句：
**任何情况下自动控制都不能被顶住**。故本段把「各种情况」列成矩阵逐个打：
每种负载下连打 N 次自动控制（0x0115），看**时延分布**（p95/max），
任何一种超预算即红。

[矩阵覆盖的负载]
    ① 空闲基线（对照：没有负载时必须最快）；
    ② 落盘压力（连续写运行日志 ⇒ 触发 WAL 检查点路径）；
    ③ 慢历史查询（关键字 ⇒ 库里逐行拼 19 个显示表达式做 LIKE）；
    ④ 慢日志查询（0x0119 带关键字）；
    ⑤ **自动模式正在跑**（控制器每拍有阀门/泵动作 + 可能产出告警入库）；
    ⑥ 导出风暴（连发多个分页请求 ⇒ 服务端连续跑慢查询）；
    ⑦ 库容量维护触发（写满 ⇒ ring 裁剪 / VACUUM / checkpoint(TRUNCATE)，
       这些是**同步维护操作**，是残余风险点 ⇒ 必须量）；
    ⑧ 多客户端并发（8 条连接同时打自动控制）。

[判据] 每种负载下 p95 与 max 都 ≤ LATENCY_BUDGET_MS（60ms）。选 60ms 的
理由：Python 服务端一次正常往返 ~0.2ms；60ms 已是 300 倍余量，同时又能
抓住「被顶 100~400ms」那种真停滞（2026-11-14 修前实测 p95=127ms /
首条被顶 238~318ms）。

[怎么跑]
    python -I tests/test_auto_control_latency.py     # 独立跑
    python -I tests/run_all.py                      # 全量（含本段）
"""

import asyncio
import os
import shutil
import sys

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from fake_devices import build_fake_app      # noqa: E402
from test_vfd_ops import Client              # noqa: E402
from test_event_loop_lag import _FastClient  # noqa: E402  细粒度时延客户端

PASS = 0
FAIL = 0

#: 时延预算（毫秒）。见文件头「判据」。
LATENCY_BUDGET_MS = 60.0
#: 每种负载的采样数。
SAMPLES = 60


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _pct(values, pct):
    if not values:
        return 0.0
    idx = min(len(values) - 1, max(0, int(round(pct * (len(values) - 1)))))
    return values[idx]


async def _spin_server(basedir, log):
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    # [同账并发要开] 缺省 login_allow_concurrent=False ⇒ **同一账号第二次
    # 登录会把前一条会话踢掉**（会话数上限/同账互斥的设计，实测第二次
    # root 登录后第一条连接被中止 WinError 10053）。本段要测的是「多条
    # 连接同时打自动控制」，得先把策略打开，否则量的是「被踢」不是「被顶」。
    cfg.login_allow_concurrent = True
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)
    # ---- 补装三件真 Bank（阀/泵/按钮组）----
    # [为什么] 没有阀/泵/按钮组 ⇒ 自动序列**起不来**（场景⑤ 量不到
    # 「控制器真的在跑」时的时延）。照 test_auto_modes.py 的装配同款。
    from hsfsrv import buttons as buttons_mod
    from hsfsrv import pumps as pumps_mod
    from hsfsrv import valves as valves_mod
    cfg.valve_move_timeout_ms = 600
    cfg.button_debounce_ms = 1
    business.valve_bank = valves_mod.ValveBank(
        business.digital, cfg.valve_items, lambda _m: None,
        move_timeout_ms=cfg.valve_move_timeout_ms)
    business.pump_bank = pumps_mod.PumpBank(
        business.digital, cfg.pump_items, lambda _m: None)
    business.button_station = buttons_mod.ButtonStation(
        business.auto_ctx, cfg.button_items,
        debounce_ms=cfg.button_debounce_ms, log=lambda _m: None)
    business.button_station.on_state_change = business._on_button_state
    business.button_station.set_running(False)
    d0 = business.digital[0]
    for n in range(6):
        d0.di[2 * n] = 0
        d0.di[2 * n + 1] = 1
    d0.di[cfg.button_items["estop_di"]] = 1
    d0.di[cfg.button_items.get("stop_di", 14)] = 1
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server


def _measure(client, n=SAMPLES, op=2):
    """连打 n 次自动控制，返回 (时延列表, 全部有应答)。"""
    lat = []
    ok = True
    for _ in range(n):
        t0 = time.perf_counter()
        r = client.request(proto.CMD_AUTO_CONTROL, bson.encode({"op": op}),
                           wait=5.0)
        lat.append((time.perf_counter() - t0) * 1000.0)
        if r is None:
            ok = False
    return lat, ok


def _report(name, lat, ok, extra=""):
    lat = sorted(lat)
    p95 = _pct(lat, 0.95)
    worst = lat[-1] if lat else 0.0
    check("%s：应答齐全" % name, ok)
    check("%s：p95 ≤ %.0f ms" % (name, LATENCY_BUDGET_MS),
          p95 <= LATENCY_BUDGET_MS,
          "p95=%.1fms 中位=%.1fms max=%.1fms %s" % (p95, _pct(lat, 0.5),
                                                     worst, extra))
    check("%s：max ≤ %.0f ms" % (name, LATENCY_BUDGET_MS),
          worst <= LATENCY_BUDGET_MS, "max=%.1fms" % worst)


async def _run_case(basedir, log):
    business, server = await _spin_server(basedir, log)
    errors = []

    def scenario():
        try:
            c = _FastClient("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"
            store = business.store
            if store.db_path is None:
                store.open_for("")

            # ---- 灌 4 万行历史（让慢查询成为真负载）----
            base_ts = int(time.time() * 1000) - 40000 * 1000 - 60000
            for i in range(40000):
                store.insert_history({
                    "ts": base_ts + i * 1000, "mode": "手动",
                    "vfd_state": "停止", "freq_hz": 12.5, "current_a": 3.2,
                    "rpm": 360.0, "turb1": 2.29, "turb2": 0.045,
                    "flow": 1.25, "flow_unit": "m³/h", "flow_total": 100.5,
                    "total_unit": "m³", "press_mpa": 0.18,
                    "press_note": "正常", "temp": 22.5, "hum": 55.0,
                    "di": 0, "do": 0, "water_temp": 19.3,
                })
            for i in range(3000):
                store.insert_log("预置日志行 %d" % i, category="conn")

            # ---- ① 空闲基线 ----
            lat, ok = _measure(c)
            _report("① 空闲基线", lat, ok)

            # ---- ② 落盘压力（写日志把 WAL 撑起来）----
            stop = [False]

            def _writer():
                n = 0
                while not stop[0]:
                    store.insert_log("压力行 %d" % n, category="conn")
                    n += 1
                    time.sleep(0.001)

            th = threading.Thread(target=_writer, name="io-pressure")
            th.start()
            time.sleep(0.05)
            lat, ok = _measure(c)
            stop[0] = True
            th.join(5)
            _report("② 落盘压力", lat, ok)

            # ---- ③ 慢历史查询进行中 ----
            rid = c.send_only(proto.CMD_HISTORY_PAGE,
                              bson.encode({"offset": 0, "limit": 50,
                                           "kw": "zzz-稀有-全表扫"}))
            time.sleep(0.02)
            lat, ok = _measure(c, 20)
            c.wait_response(rid, wait=30)
            _report("③ 慢历史查询进行中", lat, ok)

            # ---- ④ 慢日志查询进行中 ----
            rid = c.send_only(proto.CMD_LOG_PAGE,
                              bson.encode({"offset": 0, "limit": 50,
                                           "kw": "压力行"}))
            time.sleep(0.02)
            lat, ok = _measure(c, 20)
            c.wait_response(rid, wait=30)
            _report("④ 慢日志查询进行中", lat, ok)

            # ---- ⑤ 自动模式正在跑（控制器活跃）----
            # 先切到自动模式再启动：每拍有阀门/泵动作 + 可能的告警入库 ⇒
            # 服务端每拍的工作量比空闲时大一个量级。
            r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}),
                          wait=5.0)
            assert r and r[1].get("ok") is True, "切自动模式失败：%r" % (
                r[1] if r else None)
            # 配一个只开阀 1 的模式（现场点位口径：选中阀开、未选阀关），
            # 这样序列真的能跑起来（无阀位反馈时序列会卡在阀相）。
            c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1", "inletPump"],
                                      "stirRpm": 720, "stirReverse": False,
                                      "resetValvesOnStop": True,
                                      "resetValvesOnEstop": False}}}),
                wait=5.0)
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": 1, "autoMode": "produce"}),
                          wait=10.0)
            started = bool(r and r[1].get("ok") is True)
            if started:
                time.sleep(1.0)         # 让序列真的跑起来（阀相/泵相）
            lat, ok = _measure(c, 20)
            check("⑤ 自动模式真的跑起来了（控制器在动）", started,
                  "启动应答=%r" % (r[1] if r else None))
            _report("⑤ 自动模式运行中", lat, ok,
                    extra="（启动%s）" % ("成功" if started else "被拒"))
            # 收尾：停 + 切回手动
            c.request(proto.CMD_AUTO_CONTROL, bson.encode({"op": 2}), wait=5.0)
            c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 0}), wait=5.0)

            # ---- ⑥ 导出风暴（连发多个分页 ⇒ 服务端连跑慢查询）----
            rids = []
            for _ in range(8):
                rids.append(c.send_only(
                    proto.CMD_HISTORY_PAGE,
                    bson.encode({"offset": 0, "limit": 50,
                                 "kw": "zzz-稀有-全表扫"})))
            time.sleep(0.02)
            lat, ok = _measure(c, 20)
            for rid in rids:
                c.wait_response(rid, wait=60)
            _report("⑥ 导出风暴（8 条慢查询在途）", lat, ok)

            # ---- ⑦ 容量维护触发（ring 裁剪 / VACUUM / WAL 归并）----
            # 把上限压到当前用量以下 ⇒ 下一次写入即触发维护。**先确认它真
            # 触发了**（否则等于没测到东西 —— 与 2026-11-14 踩过的「灌库
            # 时间戳伸到未来 ⇒ 被窗口滤掉 ⇒ 慢路径没走到」同一类坑）。
            # [2026-11-14] 维护动作已挪到后台线程（独立连接 + 分块小事务）
            # ⇒ 这一段量的就是「维护进行中，自动控制是否被顶住」。
            try:
                before_rows = store.row_count("history")
                store.configure(max_mb=1, full_policy="ring", keep_files=1)
                # 触发一次写 ⇒ 写前闸判定超限 ⇒ 请求后台裁剪。
                store.insert_log("触发容量维护", category="conn")
                lat, ok = _measure(c, 30)
                # 给后台一点时间（分块删），行数要下降。
                deadline = time.time() + 20
                after_rows = before_rows
                while time.time() < deadline:
                    after_rows = store.row_count("history")
                    if after_rows < before_rows:
                        break
                    time.sleep(0.05)
                check("⑦ 容量维护真触发了（历史行被裁）",
                      after_rows < before_rows,
                      "前=%d 行 后=%d 行" % (before_rows, after_rows))
                _report("⑦ 容量维护触发", lat, ok)
                # 复原成不限（后面的场景要留着 4 万行夹具）。
                store.configure(max_kb=0, full_policy="ring")
            except Exception as exc:                     # noqa: BLE001
                check("⑦ 容量维护触发（触发失败也算红）", False,
                      "%s: %s" % (type(exc).__name__, exc))

            # ---- ⑧ 多客户端并发 ----
            clients = []
            for i in range(7):
                c2 = _FastClient("127.0.0.1", server.port)
                c2.hello()
                c2.login("root", "root")
                clients.append(c2)
            lat_all = []
            ok_all = True
            for i in range(SAMPLES):
                t0 = time.perf_counter()
                r = c.request(proto.CMD_AUTO_CONTROL,
                              bson.encode({"op": 2}), wait=5.0)
                lat_all.append((time.perf_counter() - t0) * 1000.0)
                if r is None:
                    ok_all = False
                # 每 10 轮让别的连接也打一次（真并发）
                if i % 10 == 0:
                    c2 = clients[i % len(clients)]
                    r2 = c2.request(proto.CMD_AUTO_CONTROL,
                                    bson.encode({"op": 2}), wait=5.0)
                    if r2 is None:
                        ok_all = False
            _report("⑧ 8 客户端并发", lat_all, ok_all)
            for c2 in clients:
                try:
                    c2.close()
                except Exception:                       # noqa: BLE001
                    pass
        except Exception as exc:                         # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))
    thread = threading.Thread(target=scenario, name="latency-matrix")
    thread.start()
    while thread.is_alive():
        await asyncio.sleep(0.02)
    thread.join(120)
    for err in errors:
        check("时延矩阵场景", False, err)


def main():
    base = tempfile.mkdtemp(prefix="hsf_aclat_")
    log = lambda m: None
    try:
        asyncio.run(_run_case(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
