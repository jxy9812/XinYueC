# -*- coding: utf-8 -*-
"""两台浊度仪逐台配置回归（2026-11-10 用户定案）。

[测什么] 用户原话「两台浊度仪要可以分开设置，他们是两台设备」——现场两台
浊度仪是站1（WZT-701A，CDAB×2 字）与站3（ZHEYI-5100ZDY，DCBA×4 字），
型号/字节序/采集周期都不同，**只装一台或单独停一台检修**都可能。锁死：

    1. 默认两台都开；`turbidityNEnabled` 优先，缺键回落共同键
       `turbidityEnabled`（旧配置/旧客户端兼容）。
    2. 逐台键只影响该台，另一台照采。
    3. 旧共同键单独出现时视作"两台一起"；同一载荷里既带共同键又带逐台键
       时**以逐台为准**（新客户端会把共同键当"任一启用"一起发，兼容旧
       服务端——曾经在这里把刚停的台又盖回开，B3c 实测红）。
    4. 采集循环逐台门控：停一台只少一个轮询任务；停用的健康条目报
       "已停用"（不伪装掉线）。

[怎么跑]
    python tests/test_turbidity_per_meter.py
    python tests/run_all.py --units        # 已挂在单元组里
"""

import asyncio
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import app as app_mod            # noqa: E402
from hsfsrv import bson                      # noqa: E402
from hsfsrv import config as cfgmod          # noqa: E402
from hsfsrv import proto                     # noqa: E402


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


class DummyTurb(object):
    """只给采集循环用的最小设备（name + poll）。"""

    def __init__(self, name):
        self.name = name

    async def poll(self, now_ms):
        return None

    def health(self, now_ms, stale_ms):
        return proto.HEALTH_OFFLINE, 0, False, "无数据"


class FakeSession(object):
    """只收应答的会话壳（_reply → send_response）。"""

    def __init__(self):
        self.resp = None

    async def send_response(self, request_id, msg_code, payload):
        self.resp = bson.decode(payload)


def _make_app(tmpdir):
    cfg = cfgmod.Config()
    cfg.base_dir = tmpdir
    cfg.config_path = os.path.join(tmpdir, "hsf_server_py.json")
    a = app_mod.App.__new__(app_mod.App)
    a.cfg = cfg
    a.vfd = None
    a.temphum = None
    a.flowmeter = None
    a.analog = None
    a.digital = []
    a.pump_bank = None
    a.valve_bank = None
    a.button_station = None
    a.turbidity = [DummyTurb("浊度仪1"), DummyTurb("浊度仪2")]
    a._poll_tasks = []
    a._log = lambda msg: None
    a._log_with_store = lambda msg, cat=None: None
    a._now_ms = lambda: 1000
    a.store = None
    a.auto_controller = None
    return a


async def _run(tmpdir):
    a = _make_app(tmpdir)
    cfg = a.cfg
    pollled = []

    async def fake_poll(dev, period_ms, label, period_attr=None):
        pollled.append((label, period_ms))

    a._poll_loop = fake_poll

    # ---- 1) 默认两台都开 ----
    check("T1a 默认两台浊度都启用",
          cfg.turbidity_enabled(0) and cfg.turbidity_enabled(1))
    await a.start_polling()
    await asyncio.sleep(0)          # 让 ensure_future 起的任务真正开跑
    check("T1b 默认两台各起一个轮询任务",
          [p[0] for p in pollled] == ["浊度仪1", "浊度仪2"], pollled)
    for t in a._poll_tasks:
        t.cancel()
    for t in a._poll_tasks:
        try:
            await t
        except (asyncio.CancelledError, Exception):     # noqa: BLE001
            pass
    a._poll_tasks = []

    # ---- 2) 逐台键只影响该台 ----
    a._cfg_set_collect_key(proto.CFGK_TURBIDITY1_ENABLED, False)
    check("T2a 只停浊度仪1：t1=False t2=True",
          (not cfg.turbidity_enabled(0)) and cfg.turbidity_enabled(1))
    pollled[:] = []
    await a.start_polling()
    await asyncio.sleep(0)          # 让 ensure_future 起的任务真正开跑
    check("T2b 采集循环只起浊度仪2（停用的台不轮询）",
          [p[0] for p in pollled] == ["浊度仪2"], pollled)
    for t in a._poll_tasks:
        t.cancel()
    for t in a._poll_tasks:
        try:
            await t
        except (asyncio.CancelledError, Exception):     # noqa: BLE001
            pass
    a._poll_tasks = []

    # ---- 3) 旧共同键单独出现 = 两台一起 ----
    orig_can = app_mod.permissions.can
    app_mod.permissions.can = lambda session, perm: True
    try:
        sess = FakeSession()
        await a._h_config_set(sess, 1, proto.CMD_CONFIG_SET, bson.encode({
            "section": "collect",
            proto.CFGK_TURBIDITY_ENABLED: True,
        }))
        check("T3a 只发共同键 true ⇒ 两台都开",
              cfg.turbidity_enabled(0) and cfg.turbidity_enabled(1),
              "resp=%s" % sess.resp)
        # ---- 4) 同载荷：逐台为准 ----
        sess = FakeSession()
        await a._h_config_set(sess, 1, proto.CMD_CONFIG_SET, bson.encode({
            "section": "collect",
            proto.CFGK_TURBIDITY1_ENABLED: False,
            proto.CFGK_TURBIDITY2_ENABLED: True,
            # 客户端把共同键当"任一启用"一起发（兼容旧服务端）
            proto.CFGK_TURBIDITY_ENABLED: True,
        }))
        check("T3b 同载荷带逐台键 ⇒ 以逐台为准（t1 保持停）",
              (not cfg.turbidity_enabled(0)) and cfg.turbidity_enabled(1),
              "resp=%s" % sess.resp)
    finally:
        app_mod.permissions.can = orig_can

    # ---- 5) 落盘三键 + 加载往返 ----
    cfg.save()
    with open(cfg.config_path, encoding="utf-8") as fh:
        ser = json.load(fh)["serial"]
    check("T4a 落盘：t1=false t2=true，旧键=任一启用(true)",
          ser.get("turbidity1_enabled") is False
          and ser.get("turbidity2_enabled") is True
          and ser.get("turbidity_enabled") is True, ser)
    cfg2 = cfgmod.Config()
    cfg2.config_path = cfg.config_path
    cfg2.load()
    check("T4b 加载往返保持逐台状态",
          (not cfg2.turbidity_enabled(0)) and cfg2.turbidity_enabled(1))

    # ---- 6) 健康条目"已停用"不伪装掉线 ----
    entries = a.collect_health(1000)
    turb = [e for e in entries if e.name.startswith("浊度仪")]
    check("T5a 健康仍列两台（停用不消失）", len(turb) == 2,
          [e.name for e in turb])
    check("T5b 停用的台报「已停用」",
          len(turb) == 2 and turb[0].text == "已停用"
          and turb[0].online is False, [e.text for e in turb])


def main():
    tmpdir = tempfile.mkdtemp(prefix="hsf_turb_per_meter_")
    try:
        asyncio.run(_run(tmpdir))
    finally:
        import shutil
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("\n  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
