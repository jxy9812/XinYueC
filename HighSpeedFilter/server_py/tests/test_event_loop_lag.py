# -*- coding: utf-8 -*-
"""事件循环时延回归（2026-11-14 用户口径「检查下 python 的服务端是否可能
出现事件循环停滞，影响自动模式的及时响应」）。

[测什么] 服务端在**落盘压力**与**慢查询**下，自动控制命令（0x0115）的
往返时延不能被顶掉：

    1. **落盘压力**：连续写运行日志（每次控制/连接事件都写一条）⇒
       WAL 检查点若内联在写入里，单次可堵 130~150ms（实测 12MB 库）。
    2. **慢查询**：历史分页带关键字（库里把 19 个显示表达式逐行拼一遍做
       LIKE）⇒ 12MB 库实测单次 273~397ms。

[2026-11-14 实测的病根与修法]
    · 病根 A：WAL 自动检查点**内联在写入的那次 execute 里**（写连接
      `wal_autocheckpoint=128`）⇒ 事件循环被磁盘 IO 堵住。
      修法：写连接关掉自动检查点，检查点交给**独立连接的后台线程**
      （PASSIVE，不阻塞写者）。
    · 病根 B：同一会话的请求**串行 await** ⇒ 一个慢查询把这条会话后续
      所有帧（含 0x0115）压住。
      修法：纯读的分页命令（0x0118/0x0119）另起任务处理；控制命令
      仍串行（启动/停止/急停/复位的先后语义不能乱）。
    修前：自动控制往返 中位 0.9ms / **p95=134.6ms / max=147.7ms**
    （120 次里 18 次 >10ms）；修后：中位 0.9ms / p95≈2ms / max≈3.6ms。

[怎么跑]
    python -I tests/test_event_loop_lag.py        # 独立跑
    python -I tests/run_all.py                    # 全量（含本段）

服务端在本进程内起（假设备 + 真 XLink 协议栈 + 真 SQLite），客户端走真
socket ⇒ 测的是**真链路**上的时延。
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
from test_vfd_ops import Client              # noqa: E402  复用协议客户端

PASS = 0
FAIL = 0

#: 自动控制往返的**上限**（毫秒）。取 60ms：修前 p95=134ms 必红，
#: 修后实测 p95≈2ms / max≈3.6ms ⇒ 余量 20 倍以上仍绿。
LATENCY_BUDGET_MS = 60.0
#: 样本数（够看出 p95 尾巴，又不至于跑太久）。
SAMPLES = 120


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


async def _spin_server(basedir, log):
    """起一份假设备服务端（真 XLink + 真 SQLite 落盘）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0                       # 临时端口，避免撞上在跑的实例
    cfg.enable_serial = False
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server


def _pct(values, pct):
    """分位数（values 已排序；pct 取 0..1）。"""
    if not values:
        return 0.0
    idx = min(len(values) - 1, max(0, int(round(pct * (len(values) - 1)))))
    return values[idx]


class _FastClient(Client):
    """**细粒度**时延客户端：应答一到立刻返回。

    [为什么不能直接用 Client.request] 它每次 pump(0.08) 之后才看 pending ⇒
    **量测粒度 ≥ 80ms** ⇒ 服务端真实时延（修后 ~1ms）会被自己的等待淹没
    （实测量出 95ms 的"假尾巴"）。这里改成：收包超时 5ms 的紧循环，收到
    本请求的应答即返回 ⇒ 量到的是**服务端**的真实往返。
    """

    def __init__(self, host, port, timeout=6.0):
        Client.__init__(self, host, port, timeout)
        self.sock.settimeout(0.005)      # 收包粒度 5ms

    def request(self, code, payload=b"", data_type=proto.DATATYPE_BSON,
                wait=5.0):
        self.req_id += 1
        rid = self.req_id
        self._send(proto.MSGCLASS_REQUEST, code, data_type, payload, rid)
        deadline = time.time() + wait
        while time.time() < deadline:
            frame = self._read_frame()
            if frame:
                mclass, mcode, frid, dtype, pl = frame
                if mclass == proto.MSGCLASS_RESPONSE:
                    body = (bson.decode(pl) if dtype == proto.DATATYPE_BSON
                            else {})
                    if frid == rid:
                        return (mcode, body)
                    # [别的请求的应答要**存起来**] 慢查询的应答会先到，若
                    # 直接丢掉，后面的 wait_response 就永远等不到（实测
                    # 「查询跑完但等不到应答」= 这个坑，不是服务端挂）。
                    self.pending[frid] = (mcode, body)
            else:
                time.sleep(0.0005)
        return None

    def send_only(self, code, payload=b"", data_type=proto.DATATYPE_BSON):
        """**只发不等**（返回 requestId；应答稍后用 wait_response 取）。

        [为什么需要] 复现「同会话串行队列」的原病灶：慢查询先发、控制命令
        后发，两条都排在**同一条连接**上 ⇒ 改前控制应答会被顶到查询结束。
        """
        self.req_id += 1
        rid = self.req_id
        self._send(proto.MSGCLASS_REQUEST, code, data_type, payload, rid)
        return rid

    def wait_response(self, rid, wait=10.0):
        """等某个 requestId 的应答（先前存下的先看）。"""
        if rid in self.pending:
            return self.pending.pop(rid)
        deadline = time.time() + wait
        while time.time() < deadline:
            frame = self._read_frame()
            if frame:
                mclass, mcode, frid, dtype, pl = frame
                if mclass == proto.MSGCLASS_RESPONSE:
                    body = (bson.decode(pl) if dtype == proto.DATATYPE_BSON
                            else {})
                    if frid == rid:
                        return (mcode, body)
                    self.pending[frid] = (mcode, body)
            else:
                time.sleep(0.0005)
        return None


async def _run_case(basedir, log):
    business, server = await _spin_server(basedir, log)
    errors = []

    def scenario():
        try:
            # [量测粒度] 用 _FastClient（收包超时 5ms 的紧循环）：基类
            # Client.request 每次 pump(0.08) 才看一次 pending ⇒ 量测粒度
            # ≥80ms ⇒ 服务端真实时延会被自己的等待淹没。
            c = _FastClient("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"
            # [订阅遥测] 服务端只在**有订阅者**时才推 ⇒ 不订阅就测不到
            # 推送路径（2026-11-15 实锅：推送里的一个写错的 API 之所以能
            # 让整个 tick 停摆，正是因为它每拍都在推；本用例必须真的订阅，
            # 「tick 无异常」那条断言才咬得住）。
            for topic in (proto.TOPIC_VFD_STATUS, proto.TOPIC_DIGITAL_STATUS):
                c._send(proto.MSGCLASS_SUBSCRIBE, topic & 0xFFFF,
                        proto.DATATYPE_RAW, proto.pack_topic(topic))

            # ---- ① 造一批历史行（让关键字查询变真扫描）----
            # 直接灌库：走 store 的公开写入口（不依赖采集跑满）。
            # [行数口径] 要让「关键字查询」单次耗时**明显超过**时延预算
            # （60ms）——否则「查询把控制顶住」这个回归复现不出来（查询
            # 太快 ⇒ 顶不住也看不出来）。4 万行 ⇒ 实测查询 ~250ms。
            store = business.store
            if store.db_path is None:
                store.open_for("")
            # [放开容量上限 2026-11-14] 本段要的是「慢查询」场景 ⇒ 夹具
            # 必须**留住** 4 万行；否则后台容量维护（ring 裁剪）会把它们
            # 裁掉（那正是新机制在工作）。**必须在灌数据之前**放开 ⇒
            # 否则灌的过程中就被裁了（实测灌完只剩 36569 行）。
            store.configure(max_kb=0, full_policy="ring")
            # [时间戳必须全在**过去**] 分页查询带窗口上界（= 现在）⇒ 若
            # 行的时间戳伸到未来，它们会被窗口滤掉 ⇒ 慢路径根本没走到
            # （实测踩过：查询 22ms 而不是 300ms，因为 4 万行排到了
            # now+6 小时）。故起点往回挪 4 万秒。
            base_ts = int(time.time() * 1000) - 40000 * 1000 - 60000
            for i in range(40000):
                # [各列都填真值] 关键：关键字条件要把 **19 个显示表达式**
                # 逐行拼一遍（NULL 列走 COALESCE 便宜些）⇒ 填满才复现得出
                # 「一次查询几百毫秒」的真实负载（实测 4 万行 ≈ 250ms）。
                store.insert_history({
                    "ts": base_ts + i * 1000,
                    "mode": "手动",
                    "vfd_state": "停止",
                    "freq_hz": 12.5, "current_a": 3.2, "rpm": 360.0,
                    "turb1": 2.29, "turb2": 0.045,
                    "flow": 1.25, "flow_unit": "m³/h", "flow_total": 100.5,
                    "total_unit": "m³",
                    "press_mpa": 0.18, "press_note": "正常",
                    "temp": 22.5, "hum": 55.0,
                    "di": 0x0000, "do": 0x0000,
                    "water_temp": 19.3,
                })

            # [自检] 库里真有 4 万行、且关键字查询在 store 层确实慢
            # （否则下面的时延断言是「空跑」——测不到东西）。
            _n = store.row_count("history")
            _t0 = time.perf_counter()
            _rows, _tot = store.query_history_page(0, 50,
                                                  kw="zzz-稀有关键字-不走索引")
            _store_ms = (time.perf_counter() - _t0) * 1000.0
            check("对照组：库里有 4 万行历史", _n >= 40000, "行数=%d" % _n)
            check("对照组：关键字查询在 store 层是「慢查询」"
                  "（> %.0f ms，否则顶不住也测不出）" % LATENCY_BUDGET_MS,
                  _store_ms > LATENCY_BUDGET_MS, "store 层 %.0f ms" % _store_ms)

            # ---- ①a 容量维护进行中：自动控制仍要快 ----
            # [2026-11-14 新增] 容量维护（ring 裁剪 / 还盘 / WAL 归并）曾
            # 整轮**同步**做在写路径上：实测整轮裁剪 108~271ms、WAL 截断
            # 187ms、VACUUM 23ms ⇒ 事件循环被堵 ⇒ 自动控制应答被顶到几百
            # 毫秒之后。改法：判定留前台、动作交后台（独立连接 + 分块小
            # 事务）。本场景把上限压到远小于当前内容 ⇒ 裁剪必然触发，
            # 然后在**裁剪进行中**测自动控制时延。
            store.configure(max_kb=256, full_policy="ring", keep_files=2)
            _rows_before = store.row_count("history")
            # 触发一次写 ⇒ 写前闸判定超限 ⇒ 请求后台裁剪。
            store.insert_log("触发容量维护", category="conn")
            _lat_m = []
            for i in range(20):
                t0 = time.perf_counter()
                r = c.request(proto.CMD_AUTO_CONTROL,
                              bson.encode({"op": 2}), wait=5.0)
                _lat_m.append((time.perf_counter() - t0) * 1000.0)
                assert r is not None, "容量维护期间自动控制无应答"
            _lat_m.sort()
            check("容量维护进行中自动控制 max ≤ %.0f ms" % LATENCY_BUDGET_MS,
                  _lat_m[-1] <= LATENCY_BUDGET_MS,
                  "max=%.1fms p95=%.1fms" % (_lat_m[-1], _lat_m[18]))
            # [后台确实在裁] 给它一点时间（后台线程分块删），行数要下降。
            _deadline = time.time() + 20
            _rows_after = _rows_before
            while time.time() < _deadline:
                _rows_after = store.row_count("history")
                if _rows_after < _rows_before:
                    break
                time.sleep(0.05)
            check("容量维护真在后台跑（历史行被裁）",
                  _rows_after < _rows_before,
                  "前=%d 行 后=%d 行" % (_rows_before, _rows_after))
            # 复原成不限（后面的场景要 4 万行夹具留着）。
            store.configure(max_kb=0, full_policy="ring")
            for i in range(40000):
                store.insert_history({
                    "ts": base_ts + i * 1000, "mode": "手动",
                    "vfd_state": "停止", "freq_hz": 12.5, "current_a": 3.2,
                    "rpm": 360.0, "turb1": 2.29, "turb2": 0.045,
                    "flow": 1.25, "flow_unit": "m³/h", "flow_total": 100.5,
                    "total_unit": "m³", "press_mpa": 0.18,
                    "press_note": "正常", "temp": 22.5, "hum": 55.0,
                    "di": 0x0000, "do": 0x0000, "water_temp": 19.3,
                })

            # ---- ①b 写入尾巴（WAL 检查点内联 ⇒ 单次写可堵 130ms+）----
            # 直接量 store 的写入耗时分布：改前（检查点内联在写连接的
            # execute 里）实测 200 次写入里 6 次 >10ms、最大 134~146ms；
            # 改后（检查点挪到独立连接的后台线程）写入恒 0.1ms 量级。
            _wlat = []
            for i in range(300):
                _t = time.perf_counter()
                store.insert_log("时延压测行 %d" % i, category="conn")
                _wlat.append((time.perf_counter() - _t) * 1000.0)
            _wlat.sort()
            # [灵敏度说明] 这条在**大库 + 慢盘/杀毒扫描**的环境里才是
            # 硬锁（实测 12MB 库：内联检查点时单次写 130~146ms vs 改后
            # 0.1ms）；本测试的小库里检查点只有 ~14ms ⇒ 抓不满 60ms 预算
            # ⇒ 它的作用是「别把检查点挂回写路径」的常设提醒。**硬锁是
            # 下面那条慢查询用例**（A/B 实测：撤掉并发分页 ⇒ 317.9ms 红）。
            check("写入尾巴：300 次写入 max ≤ %.0f ms"
                  "（WAL 检查点不得内联在写路径）" % LATENCY_BUDGET_MS,
                  _wlat[-1] <= LATENCY_BUDGET_MS,
                  "max=%.1fms p95=%.1fms" % (_wlat[-1], _wlat[284]))

            # ---- ② 落盘压力 + 自动控制时延 ----
            # 每次 0x0115（停止）服务端都会写一条运行日志 ⇒ 写路径被反复
            # 触发；改前 WAL 检查点内联 ⇒ 偶发 130ms+ 的停顿。
            lat = []
            for i in range(SAMPLES):
                t0 = time.perf_counter()
                r = c.request(proto.CMD_AUTO_CONTROL,
                              bson.encode({"op": 2}), wait=5.0)
                lat.append((time.perf_counter() - t0) * 1000.0)
                assert r is not None, "自动控制无应答（第 %d 次）" % i
            lat.sort()
            p95 = _pct(lat, 0.95)
            worst = lat[-1]
            check("落盘压力下自动控制 p95 ≤ %.0f ms" % LATENCY_BUDGET_MS,
                  p95 <= LATENCY_BUDGET_MS,
                  "p95=%.1fms max=%.1fms 中位=%.1fms"
                  % (p95, worst, _pct(lat, 0.5)))
            check("落盘压力下自动控制 max ≤ %.0f ms" % LATENCY_BUDGET_MS,
                  worst <= LATENCY_BUDGET_MS, "max=%.1fms" % worst)
            check("落盘压力下自动控制无超时（应答齐全）",
                  len(lat) == SAMPLES, "%d/%d" % (len(lat), SAMPLES))

            # ---- ③ 慢查询进行中，自动控制仍要快 ----
            # 关键字查询 = 逐行拼 19 个显示表达式做 LIKE（库里筛 ⇒ 扫全表）。
            # 改前它与控制命令共用一条**串行**队列（同会话逐帧 await）⇒ 控制
            # 应答被顶到查询结束。复现手法：**同一条连接上只发不等**——查询
            # 排在前面，控制命令排在后面 ⇒ 正好压住原病灶。
            slow_rid = c.send_only(
                proto.CMD_HISTORY_PAGE,
                bson.encode({"offset": 0, "limit": 50,
                             "kw": "zzz-稀有关键字-不走索引"}))
            t_slow = time.perf_counter()
            time.sleep(0.02)          # 让查询先跑起来
            lat2 = []
            for i in range(10):
                t0 = time.perf_counter()
                r = c.request(proto.CMD_AUTO_CONTROL,
                              bson.encode({"op": 2}), wait=5.0)
                lat2.append((time.perf_counter() - t0) * 1000.0)
                assert r is not None, "慢查询期间自动控制无应答"
            # 等慢查询的应答回来（对照组：确认查询真跑完且成功）。
            slow_doc = c.wait_response(slow_rid, wait=30.0)
            slow_ms = (time.perf_counter() - t_slow) * 1000.0
            lat2.sort()
            check("慢查询进行中自动控制 max ≤ %.0f ms" % LATENCY_BUDGET_MS,
                  lat2[-1] <= LATENCY_BUDGET_MS,
                  "查询 %.0fms；控制 max=%.1fms" % (slow_ms, lat2[-1]))
            check("慢查询确实跑完了（对照组有效）",
                  bool(slow_doc) and slow_doc[1].get("ok") is True,
                  "查询耗时 %.0fms err=%r"
                  % (slow_ms, (slow_doc[1].get("err") if slow_doc else "无应答")))
        except Exception as exc:                     # noqa: BLE001
            errors.append("%s: %s" % (type(exc).__name__, exc))
    thread = threading.Thread(target=scenario, name="lag-client")
    thread.start()
    # [事件循环必须照常转] 客户端线程跑期间用 await 让出控制权 ⇒ 服务端的
    # 事件循环继续跑（否则服务端根本不处理帧，测的就是"假死"而不是时延）。
    while thread.is_alive():
        await asyncio.sleep(0.02)
    thread.join(60)
    for err in errors:
        check("时延场景", False, err)


def main():
    base = tempfile.mkdtemp(prefix="hsf_lag_")
    logs = []
    log = logs.append
    try:
        asyncio.run(_run_case(base, log))
        # [服务端 tick 不能抛异常 —— 2026-11-15 实锅]
        # 推送路径里写错一个 API（StreamWriter 没有 get_write_buffer_size，
        # 它在 transport 上）⇒ **每拍**抛 AttributeError ⇒ 整个 tick 停摆
        # （遥测/按钮灯/自动控制一起停），而本用例量的是**命令往返**
        # （命令在会话协程里处理，不在 tick 里）⇒ 时延照样全绿 ⇒ 只能靠
        # 显式盯日志里的「tick 异常」抓住它。
        bad = [m for m in logs if "tick 异常" in m]
        check("服务端 tick 无异常（日志里无「tick 异常」）", not bad,
              "%s" % (bad[:2],))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
