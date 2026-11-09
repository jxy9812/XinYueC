# -*- coding: utf-8 -*-
"""S8 端到端：数字量通道名脏帧（2026-11-09）。

[测什么] 对齐 C 侧 HsfServerApp.c 的 names-dirty 通路：
    · mark_digital_names_dirty() 置位（= HsfServerApp_notifyChannelNamesChanged）；
    · 下一拍主动下发一帧**只含名字键的 full 帧**（= hsfAppEncodeDigitalNames：
      full=true + count + din%d/don%d 名字键；不带 di/do/valid/staleMs 状态键）；
    · 一次置位只补一帧（消费即清标志），不随周期刷屏；
    · 名字内容来自阀门点位映射（"电磁阀1 控制/开到位/关到位"），与
      encode_digital(full=True) 同一张汇合表。

[怎么跑]
    python3 -I tests/test_digital_names_dirty.py
    pytest tests/test_digital_names_dirty.py -q
"""

import asyncio
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto                     # noqa: E402
from fake_devices import build_fake_app      # noqa: E402


async def _spin_app(basedir, log):
    """本用例不走网络：直接驱动 app 的 tick，用桩截获 server.publish。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    business = build_fake_app(cfg, log=log)
    # build_fake_app 只注设备不装阀组；名字帧的名字正来自阀门点位映射，
    # 故这里按缺省点位（6 路全在模块 0）把阀组装上 —— 与 build_devices
    # 的装配式一致（App.build_devices 依赖 TCP master，测试里不适用）。
    from hsfsrv import valves as valves_mod
    business.valve_bank = valves_mod.ValveBank(
        business.digital, cfg.valve_items, log,
        move_timeout_ms=cfg.valve_move_timeout_ms)
    return business


def _name_frames(sent):
    """名字专用帧 = full=true 且不带状态位图（周期帧 full=false，天然区分）。"""
    return [doc for (topic, doc) in sent
            if topic == proto.TOPIC_DIGITAL_STATUS
            and doc.get(proto.KEY_FULL) is True
            and proto.KEY_DI not in doc]


async def _case_dirty_frame(basedir, log):
    business = await _spin_app(basedir, log)

    sent = []
    class _StubServer(object):
        async def publish(self, topic_id, payload, data_type=None):
            sent.append((topic_id, bson.decode(payload)))
            return 0

    business.server = _StubServer()
    try:
        # ---- ① 置脏 → 下一拍：每台 publish 模块恰好一帧名字帧 ----
        business.mark_digital_names_dirty("测试置脏")
        assert business._digital_names_dirty is True
        await business(1000)                     # 手动跑一拍
        frames = _name_frames(sent)
        assert len(frames) == 2, "两台 publish 模块各一帧名字帧，实得 %d" % (
            len(frames),)
        assert business._digital_names_dirty is False, "消费后应清标志"

        # ---- ② 键口径：full=true + dev/name/count + 名字键，无状态键 ----
        d0 = [d for d in frames if d.get(proto.KEY_DEV) == 0][0]
        d1 = [d for d in frames if d.get(proto.KEY_DEV) == 1][0]
        for doc in (d0, d1):
            assert doc.get(proto.KEY_FULL) is True
            assert doc.get(proto.KEY_COUNT) == 16
            for forbidden in (proto.KEY_DI, proto.KEY_DO, proto.KEY_VALID,
                              proto.KEY_STALE_MS):
                assert forbidden not in doc, \
                    "名字帧不应带状态键 %s：%r" % (forbidden, sorted(doc))
        # 名字来自阀门点位映射（缺省 6 路全在模块 0：DO n / 开到位 2n+1 /
        # 关到位 2n）。
        assert d0.get("din1") == "电磁阀1 开到位", \
            "din1 应为阀1 开到位名：%r" % d0.get("din1")
        assert d0.get("din0") == "电磁阀1 关到位", \
            "din0 应为阀1 关到位名：%r" % d0.get("din0")
        assert d0.get("don0") == "电磁阀1 控制", \
            "don0 应为阀1 控制名：%r" % d0.get("don0")
        # 模块 1 无点位占用 ⇒ 无名字键（客户端显示 CH<n>，不发默认名）。
        name_keys_1 = [k for k in d1 if k.startswith(("din", "don"))]
        assert name_keys_1 == [], "模块 1 不应有名字键：%r" % name_keys_1

        # ---- ③ 只补一帧：下一拍不再有名字帧（周期状态帧照常）----
        sent.clear()
        await business(1200)
        assert _name_frames(sent) == [], \
            "一次置脏只补一帧：%r" % (_name_frames(sent),)
        periodic = [d for (t, d) in sent
                    if t == proto.TOPIC_DIGITAL_STATUS]
        assert periodic, "周期状态推送应照常"

        # ---- ④ 重复置脏幂等：再置脏再置脏仍只补一拍两帧 ----
        business.mark_digital_names_dirty()
        business.mark_digital_names_dirty()
        sent.clear()
        await business(1400)
        assert len(_name_frames(sent)) == 2, \
            "重复置脏合并为一拍两帧，实得 %d" % len(_name_frames(sent))
    finally:
        business.server = None


def test_names_dirty_frame_published_once_with_name_keys_only():
    base = tempfile.mkdtemp(prefix="hsf_names_")
    try:
        asyncio.run(_case_dirty_frame(base, lambda m: None))
    finally:
        shutil.rmtree(base, ignore_errors=True)


def test_names_frame_matches_full_frame_names():
    """名字帧的名字键与 full 帧逐字一致（同一张汇合表，不许两处口径）。"""
    base = tempfile.mkdtemp(prefix="hsf_names_b_")
    try:
        business = asyncio.run(_spin_app(base, lambda m: None))
        dev = business.digital[0]
        full = bson.decode(business.encode_digital(dev, 0, full=True))
        names = bson.decode(business.encode_digital_names(dev, 0))
        full_names = {k: v for k, v in full.items()
                      if k.startswith(("din", "don"))}
        name_keys = {k: v for k, v in names.items()
                     if k.startswith(("din", "don"))}
        assert name_keys == full_names, \
            "两帧名字键不一致：%r vs %r" % (name_keys, full_names)
    finally:
        shutil.rmtree(base, ignore_errors=True)


def main():
    failed = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("  [PASS] %s" % name)
            except AssertionError as exc:
                failed.append(name)
                print("  [FAIL] %s：%s" % (name, exc))
            except Exception as exc:                    # noqa: BLE001
                failed.append(name)
                print("  [ERROR] %s：%s: %s" % (name, type(exc).__name__, exc))
    print("\n===== 名字脏帧: %d failed =====" % len(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
