# -*- coding: utf-8 -*-
"""远端配置**持久化**（服务器端）回归（2026-11-14 用户口径「远端设置有做
持久化(服务器端)」）。

[测什么]
========
远端设置页改的东西（自动模式配置 / 落盘配置 / 登录策略）**必须存在服务端**
⇒ 服务端重启（= 重新 load 配置）后还在 ⇒ 客户端重连看到的还是同一份。

口径：
    · **远端设置**（自动模式 / 采集 / 落盘 / 登录策略）⇒ **服务器端**存
      （`cfg/hsf_server_py.json`）⇒ 重启还在；
    · **本机设置**（界面缩放 / 主题 / 窗口尺寸 …）⇒ **客户端**存（INI），
      与服务端无关 ⇒ 另有 `client_py/tests/test_hsf_regression.py` §19
      A5a~A5j 锁。

[怎么跑]
========
    python -I tests/test_config_persist.py
"""

import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

# [输出编码 2026-11-14] 中文 Windows 默认 cp936 ⇒ 打印「⇒」会 UnicodeEncodeError
# ⇒ 整套测试在打印断言时被打断（退出码还是 0，看着像"跑完了"）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

from hsfsrv import config as cfgmod          # noqa: E402
from hsfsrv import proto                    # noqa: E402

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


def _new_cfg(base):
    cfg = cfgmod.Config()
    cfg.resolve_paths(base)
    return cfg


def main():
    global PASS, FAIL
    base = tempfile.mkdtemp(prefix="hsf_cfg_persist_")
    try:
        cfg = _new_cfg(base)
        # ---- 改一批"远端设置"（与服务端同一份 cfg 文件）----
        cfg.auto["produce"].update({
            "devices": ["valve1", "stirPump", "inletPump"],
            "stir_rpm": 720,
            "stir_reverse": True,
            proto.AUTO_MODE_RESET_VALVES_ON_STOP: False,
            proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: True,
            # [2026-11-26 实例化] 两换相策略键已从单模式 modes 删除
            # （策略逐实例，写在 cycle.instances 里）⇒ 这里不再设它们。
        })
        cfg.auto["backwash"].update({
            "devices": ["valve2"],
            "stir_rpm": 960,
        })
        cfg.auto_cycle.update({
            "enabled": True,
            "press_threshold": 0.42,
            "press_compare": "lt",
            "timer_fallback": True,
            "timer_minutes": 17,
            "backwash_minutes": 13,
            "rinse_minutes": 11,
            # [2026-11-18 配置化] 参与循环的模式列表（**有序**）与**各阶段
            # 的切换条件**（压差触发/定时触发）。
            # [2026-11-26 实例化] backwash 也放进参与列表（缺席模式的
            # stage_cfg 条目已降级为"合成缺省"，不再保留手写值）。
            "cycle_modes": ["rinse", "produce", "drain", "backwash"],
            "stage_cfg": {
                "rinse": {"press_enabled": True, "press_threshold": 0.42,
                          "press_compare": "lt", "timer_enabled": True,
                          "timer_minutes": 11},
                "produce": {"press_enabled": False, "press_threshold": 0.5,
                            "press_compare": "gt", "timer_enabled": True,
                            "timer_minutes": 21},
                "drain": {"press_enabled": False, "press_threshold": 0.5,
                          "press_compare": "gt", "timer_enabled": True,
                          "timer_minutes": 4},
                "backwash": {"press_enabled": False, "press_threshold": 0.5,
                             "press_compare": "gt", "timer_enabled": True,
                             "timer_minutes": 13},
            },
        })
        # [旧键 = stage_cfg 的派生视图] 同步一次（服务端收到配置改动时
        # 也是这么做的）⇒ 内存里旧键与新配置永远一致。
        cfg._sync_auto_cycle_legacy()
        cfg.storage.enabled = True
        cfg.storage.sample_ms = 45000
        cfg.storage.set_max_kb(700)
        cfg.storage.full_policy = "rotate"
        cfg.storage.keep_files = 4
        cfg.storage.history_trigger = "auto"
        cfg.login_allow_anonymous = True
        cfg.login_allow_concurrent = True
        cfg.login_max_sessions = 6
        # [显示渐变范围 2026-11-24] 浊度逐台 / 电箱温度 / 电机转速。
        cfg.turbidity1_range_min = 0.5
        cfg.turbidity1_range_max = 8.0
        cfg.turbidity2_range_min = 0.0
        cfg.turbidity2_range_max = 1.5
        cfg.temphum_temp_low_c = 5.0
        cfg.temphum_temp_high_c = 45.0
        cfg.vfd_speed_range_min = 100.0
        cfg.vfd_speed_range_max = 1200.0
        cfg.vfd_motor_temp_range_min = 10.0
        cfg.vfd_motor_temp_range_max = 90.0
        cfg.turbidity1_gradient_enabled = False
        cfg.press1_gradient_enabled = False
        cfg.save()

        # ---- 「重启」= 新建 Config 再 load ----
        cfg2 = _new_cfg(base)
        ok = cfg2.load()
        check("配置文件可加载", bool(ok), "")

        p = cfg2.auto.get("produce") or {}
        check("自动模式：设备清单留存",
              list(p.get("devices") or []) == ["valve1", "stirPump", "inletPump"],
              "%s" % (p.get("devices"),))
        check("自动模式：转速/方向留存",
              p.get("stir_rpm") == 720 and bool(p.get("stir_reverse")),
              "rpm=%s rev=%s" % (p.get("stir_rpm"), p.get("stir_reverse")))
        check("自动模式：阀复位两个开关留存",
              p.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP) is False
              and p.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP) is True,
              "stop=%s estop=%s" % (p.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP),
                                    p.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP)))
        # [2026-11-26 实例化] 单模式 modes **不再有**换相策略键（策略逐
        # 实例，落盘在 cycle.instances 里）⇒ 文件里不该出现它们。
        check("自动模式：旧换相策略键不再落盘（实例化后删）",
              proto.AUTO_MODE_STIR_SWITCH not in p
              and proto.AUTO_MODE_INLET_SWITCH not in p,
              "%s" % sorted(p.keys()))
        b = cfg2.auto.get("backwash") or {}
        check("自动模式：另一个模式也留存（转速）",
              b.get("stir_rpm") == 960,
              "rpm=%s" % (b.get("stir_rpm"),))

        c = cfg2.auto_cycle or {}
        # [旧键 = stage_cfg 的派生视图] 压差/兜底定时/分钟数取**首元素**
        # （rinse）的值；反洗/顺洗分钟取对应模式的值。
        check("自动循环配置留存（开关 + 旧键派生视图）",
              bool(c.get("enabled")) and abs(
                  float(c.get("press_threshold") or 0) - 0.42) < 1e-6
              and c.get("press_compare") == "lt"
              and bool(c.get("timer_fallback"))
              and c.get("timer_minutes") == 11
              and c.get("backwash_minutes") == 13
              and c.get("rinse_minutes") == 11,
              "%s" % (c,))
        # [2026-11-18 配置化] 参与循环的模式列表**保序**往返 + 各阶段切换
        # 条件往返（唯一事实源）。
        check("自动循环：参与模式列表保序留存",
              list(c.get("cycle_modes") or []) ==
              ["rinse", "produce", "drain", "backwash"],
              "%s" % (c.get("cycle_modes"),))
        sc = c.get("stage_cfg") or {}
        check("自动循环：各阶段切换条件留存（压差/定时逐阶段）",
              sc.get("rinse", {}).get("press_enabled") is True
              and abs(float(sc.get("rinse", {}).get("press_threshold") or 0)
                      - 0.42) < 1e-6
              and sc.get("rinse", {}).get("press_compare") == "lt"
              and sc.get("rinse", {}).get("timer_enabled") is True
              and sc.get("rinse", {}).get("timer_minutes") == 11
              and sc.get("produce", {}).get("press_enabled") is False
              and sc.get("produce", {}).get("timer_minutes") == 21
              and sc.get("drain", {}).get("timer_minutes") == 4
              and sc.get("backwash", {}).get("timer_minutes") == 13,
              "%s" % (sc,))
        # [兼容视图] 落盘文件里还写 stageMinutes（已上线的旧客户端靠它
        # 显示"每阶段多少分钟"）⇒ 它由 stage_cfg 派生，不是第二份真值。
        import json
        with open(os.path.join(base, "cfg", "hsf_server_py.json"),
                  encoding="utf-8") as fh:
            file_cycle = ((json.load(fh).get(proto.CFG_SECTION_AUTO) or {})
                          .get(proto.CFG_AUTO_CYCLE) or {})
        check("自动循环：兼容视图 stageMinutes 也落盘（派生自 stage_cfg）",
              (file_cycle.get("stage_minutes") or {})
              .get("drain") == 4,
              "%s" % (file_cycle.get("stage_minutes"),))
        # [2026-11-26 实例化] 显式 instances（含逐实例 press1/换相策略）
        # 落盘往返。
        cfg.auto_cycle["instances"] = [
            dict(proto.default_cycle_instance("produce"),
                 devices=["valve1", "stirPump"], stir_rpm=700,
                 stir_reverse=True, press_enabled=True, press_threshold=0.3,
                 timer_enabled=False, timer_minutes=30,
                 stir_switch_policy=proto.AUTO_SWITCH_POLICY_CARRY,
                 press1_enabled=True, press1_threshold=1.2,
                 inlet_switch_policy=proto.AUTO_SWITCH_POLICY_DIRECT),
            dict(proto.default_cycle_instance("rinse"),
                 devices=["valve3"], timer_enabled=True, timer_minutes=7),
        ]
        cfg.auto_cycle["instances_explicit"] = True
        cfg.save()
        cfg3 = _new_cfg(base)
        check("显式 instances 落盘往返（逐实例参数一字不差）",
              cfg3.load()
              and cfg3.auto_cycle.get("instances_explicit") is True
              and len(cfg3.auto_cycle.get("instances") or []) == 2
              and cfg3.auto_cycle["instances"][0]["stir_switch_policy"]
              == proto.AUTO_SWITCH_POLICY_CARRY
              and cfg3.auto_cycle["instances"][0]["press1_threshold"] == 1.2
              and cfg3.auto_cycle["instances"][0]["stir_rpm"] == 700
              and cfg3.auto_cycle["instances"][1]["timer_minutes"] == 7,
              "%s" % (cfg3.auto_cycle.get("instances"),))
        check("显式 instances 下派生视图一致（cycleModes 去重保序）",
              list(cfg3.auto_cycle.get("cycle_modes") or [])
              == ["produce", "rinse"],
              "%s" % (cfg3.auto_cycle.get("cycle_modes"),))

        st = cfg2.storage
        check("落盘配置留存（开关/周期/上限/策略/触发）",
              bool(st.enabled) and st.sample_ms == 45000
              and st.max_kb == 700 and st.full_policy == "rotate"
              and st.keep_files == 4 and st.history_trigger == "auto",
              "enabled=%s sample=%s kb=%s policy=%s keep=%s trig=%s"
              % (st.enabled, st.sample_ms, st.max_kb, st.full_policy,
                 st.keep_files, st.history_trigger))

        check("登录策略留存（匿名/同账并发/会话上限）",
              bool(cfg2.login_allow_anonymous)
              and bool(cfg2.login_allow_concurrent)
              and cfg2.login_max_sessions == 6,
              "anon=%s conc=%s max=%s" % (cfg2.login_allow_anonymous,
                                          cfg2.login_allow_concurrent,
                                          cfg2.login_max_sessions))
        # [显示渐变范围 2026-11-24] 全部 8 个键落盘往返；文件里是
        # snake_case 的 display 节。
        check("显示渐变范围留存（浊度×2/电箱温度/电机转速）",
              abs(cfg2.turbidity1_range_min - 0.5) < 1e-9
              and abs(cfg2.turbidity1_range_max - 8.0) < 1e-9
              and abs(cfg2.turbidity2_range_max - 1.5) < 1e-9
              and abs(cfg2.temphum_temp_low_c - 5.0) < 1e-9
              and abs(cfg2.temphum_temp_high_c - 45.0) < 1e-9
              and abs(cfg2.vfd_speed_range_min - 100.0) < 1e-9
              and abs(cfg2.vfd_speed_range_max - 1200.0) < 1e-9
              and abs(cfg2.vfd_motor_temp_range_min - 10.0) < 1e-9
              and abs(cfg2.vfd_motor_temp_range_max - 90.0) < 1e-9,
              "t1=%.2f~%.2f t2=0~%.2f th=%.1f~%.1f spd=%.0f~%.0f mt=%.0f~%.0f"
              % (cfg2.turbidity1_range_min, cfg2.turbidity1_range_max,
                 cfg2.turbidity2_range_max, cfg2.temphum_temp_low_c,
                 cfg2.temphum_temp_high_c, cfg2.vfd_speed_range_min,
                 cfg2.vfd_speed_range_max, cfg2.vfd_motor_temp_range_min,
                 cfg2.vfd_motor_temp_range_max))
        import json as _json
        with open(os.path.join(base, "cfg", "hsf_server_py.json"),
                  encoding="utf-8") as fh:
            _disp = _json.load(fh).get("display") or {}
        check("显示渐变范围在 cfg display 节落盘",
              abs(float(_disp.get("turbidity1_range_max", 0)) - 8.0) < 1e-9
              and abs(float(_disp.get("vfd_speed_range_max", 0))
                      - 1200.0) < 1e-9,
              "%s" % (_disp,))
        check("显示渐变逐项开关留存（关掉的两项仍为关）",
              cfg2.turbidity1_gradient_enabled is False
              and cfg2.press1_gradient_enabled is False
              and cfg2.turbidity2_gradient_enabled is True
              and cfg2.vfd_speed_gradient_enabled is True
              and _disp.get("turbidity1_gradient_enabled") is False,
              "t1=%s p1=%s t2=%s"
              % (cfg2.turbidity1_gradient_enabled,
                 cfg2.press1_gradient_enabled,
                 cfg2.turbidity2_gradient_enabled))
    finally:
        shutil.rmtree(base, ignore_errors=True)

    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
