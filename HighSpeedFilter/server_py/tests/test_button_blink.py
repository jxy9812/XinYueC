# -*- coding: utf-8 -*-
"""按钮灯闪烁（2026-11-24 用户定案）单元回归。

    启动灯：切自动且可启动（start_ready + start_prompt）⇒ 闪；运行后常亮。
    停止灯：下发停止后设备仍在减速（stopping）⇒ 闪；停稳后常亮。
    复位灯：需要复位 ⇒ 闪；不需要 ⇒ 熄灭（按键点动镜像）。

灯的目标值是**时间函数**：亮 on_ms、灭 off_ms 由远端设置分别配置。
本测试用假 ctx 记录 DO 写入，按毫秒推进时间验证相位。
"""
import asyncio
import os
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsfsrv import buttons  # noqa: E402
from hsfsrv import proto  # noqa: E402

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


class FakeCtx(object):
    def __init__(self):
        # read_di 收的是**通道号**（items 里映射 12~15），不是键名。
        self.di = {
            12: True,    # 急停常闭且正常
            13: False,   # 启动常开、未按
            14: True,    # 停止常闭、未按
            15: False,   # 复位常开、未按
        }
        self.do = {7: False, 8: True, 9: False}
        self.writes = []

    def read_di(self, channel, module=0):
        return self.di.get(channel)

    def read_do(self, channel, module=0):
        return self.do.get(channel)

    async def write_do(self, channel, on, module=0):
        self.writes.append((channel, bool(on), module))
        self.do[channel] = bool(on)
        return True, ""


def items():
    return {
        buttons.KEY_ESTOP: 12,
        buttons.KEY_START: 13,
        buttons.KEY_STOP: 14,
        buttons.KEY_RESET: 15,
        buttons.KEY_START_LAMP: 7,
        buttons.KEY_STOP_LAMP: 8,
        buttons.KEY_RESET_LAMP: 9,
        buttons.KEY_MODULE: 0,
    }


async def settle():
    for _ in range(6):
        await asyncio.sleep(0)


def station(ctx):
    st = buttons.ButtonStation(ctx, items(), debounce_ms=0)
    st.set_blink_config({"start": (100, 100), "stop": (100, 100),
                         "reset": (100, 100)})
    return st


async def test_start_blink_and_run_steady():
    print("=== A 启动灯：可启动闪 / 运行后常亮 ===")
    ctx = FakeCtx()
    st = station(ctx)
    st.set_start_ready(True)
    st.set_start_prompt(True)
    st.poll(0, auto_mode=True)
    await settle()
    check("A1 可启动 ⇒ 启动灯亮（闪相位起点）", ctx.do[7] is True)
    check("A2 启动灯闪烁时停止灯保持常亮（2026-11-24 新口径）",
          ctx.do[8] is True, str(ctx.do))
    st.poll(100, auto_mode=True)       # 100ms 处 = 灭段
    await settle()
    check("A3 100ms 到灭段 ⇒ 启动灯灭", ctx.do[7] is False, str(ctx.do))
    st.poll(200, auto_mode=True)       # 下一周期亮段
    await settle()
    check("A4 200ms 回亮段 ⇒ 启动灯亮", ctx.do[7] is True, str(ctx.do))

    st.set_running(True)               # 已启动
    st.poll(300, auto_mode=True)
    await settle()
    check("A5 运行后启动灯常亮、停止灯灭",
          ctx.do[7] is True and ctx.do[8] is False, str(ctx.do))
    for t in (400, 500, 600):
        st.poll(t, auto_mode=True)
        await settle()
    check("A6 运行后持续常亮（不再闪）", ctx.do[7] is True, str(ctx.do))


async def test_stop_blink_then_steady():
    print("=== B 停止灯：减速中闪 / 停稳后常亮 ===")
    ctx = FakeCtx()
    st = station(ctx)
    st.set_running(False)
    st.set_stopping(True)
    st.poll(1000, auto_mode=True)
    await settle()
    check("B1 停机过程中停止灯亮、启动灯灭",
          ctx.do[8] is True and ctx.do[7] is False, str(ctx.do))
    st.poll(1100, auto_mode=True)
    await settle()
    check("B2 停止灯进入灭段", ctx.do[8] is False, str(ctx.do))
    st.poll(1200, auto_mode=True)
    await settle()
    check("B3 停止灯回到亮段", ctx.do[8] is True, str(ctx.do))

    st.set_stopping(False)             # 完全停稳
    st.set_start_ready(True)
    st.set_start_prompt(True)          # 自动模式停稳后应重新提示可启动
    st.poll(1300, auto_mode=True)
    await settle()
    check("B4 完全停止后停止灯常亮、启动灯进入亮段",
          ctx.do[8] is True and ctx.do[7] is True, str(ctx.do))
    st.poll(1800, auto_mode=True)
    await settle()
    check("B5 完全停止后启动灯继续闪（进入灭段）、停止灯仍常亮",
          ctx.do[8] is True and ctx.do[7] is False, str(ctx.do))


async def test_reset_blink_and_off():
    print("=== C 复位灯：需要复位闪 / 不需要灭 ===")
    ctx = FakeCtx()
    st = station(ctx)
    st.set_need_reset(True)
    st.poll(2000, auto_mode=True)
    await settle()
    check("C1 需要复位 ⇒ 复位灯亮（闪相位起点）", ctx.do[9] is True)
    st.poll(2100, auto_mode=True)
    await settle()
    check("C2 复位灯进入灭段", ctx.do[9] is False, str(ctx.do))
    st.poll(2200, auto_mode=True)
    await settle()
    check("C3 复位灯回到亮段", ctx.do[9] is True, str(ctx.do))

    st.set_need_reset(False)
    st.poll(2300, auto_mode=True)
    await settle()
    check("C4 不需要复位 ⇒ 复位灯熄灭", ctx.do[9] is False, str(ctx.do))


async def test_manual_stop_blink():
    print("=== D 手动模式停止：同样闪 ===")
    ctx = FakeCtx()
    st = station(ctx)
    st.set_manual_running(True)
    st.set_stopping(True)
    st.poll(3000, auto_mode=False)
    await settle()
    check("D1 手动停机中：停止灯闪亮段、启动灯灭",
          ctx.do[8] is True and ctx.do[7] is False, str(ctx.do))
    st.poll(3100, auto_mode=False)
    await settle()
    check("D2 手动停止灯进入灭段", ctx.do[8] is False, str(ctx.do))


def test_lights_config_roundtrip():
    print("=== E 灯闪烁配置落盘/读回 ===")
    import json
    import tempfile
    from hsfsrv import config as cfgmod

    tmp = tempfile.mkdtemp(prefix="hsf_blink_cfg_")
    cfg = cfgmod.Config()
    cfg.base_dir = tmp
    cfg.config_path = os.path.join(tmp, "hsf_server_py.json")
    cfg.auto_lights[proto.AUTO_LIGHT_START_ON] = 120
    cfg.auto_lights[proto.AUTO_LIGHT_START_OFF] = 340
    cfg.auto_lights[proto.AUTO_LIGHT_RESET_ON] = 250
    cfg.save()
    with open(cfg.config_path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    lights = (doc.get("auto") or {}).get("lights") or {}
    check("E1 保存的 auto.lights 是六键对象",
          set(lights) == set(proto.AUTO_LIGHT_KEYS), str(lights))
    cfg2 = cfgmod.Config()
    cfg2.base_dir = tmp
    cfg2.config_path = cfg.config_path
    cfg2.load()
    check("E2 读回值一致",
          cfg2.auto_lights[proto.AUTO_LIGHT_START_ON] == 120
          and cfg2.auto_lights[proto.AUTO_LIGHT_START_OFF] == 340
          and cfg2.auto_lights[proto.AUTO_LIGHT_RESET_ON] == 250,
          str(cfg2.auto_lights))
    # 非法值夹到范围（配置文件手改也不炸）
    doc["auto"]["lights"][proto.AUTO_LIGHT_START_ON] = 999999
    with open(cfg.config_path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh)
    cfg3 = cfgmod.Config()
    cfg3.base_dir = tmp
    cfg3.config_path = cfg.config_path
    cfg3.load()
    check("E3 非法毫秒被夹到上限",
          cfg3.auto_lights[proto.AUTO_LIGHT_START_ON]
          == proto.AUTO_BLINK_MS_MAX,
          str(cfg3.auto_lights.get(proto.AUTO_LIGHT_START_ON)))


def main():
    asyncio.get_event_loop().run_until_complete(test_start_blink_and_run_steady())
    asyncio.get_event_loop().run_until_complete(test_stop_blink_then_steady())
    asyncio.get_event_loop().run_until_complete(test_reset_blink_and_off())
    asyncio.get_event_loop().run_until_complete(test_manual_stop_blink())
    test_lights_config_roundtrip()
    print("\nBRIGHT-BLINK RESULT PASS %d FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
