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
            proto.AUTO_MODE_STIR_SWITCH: proto.AUTO_SWITCH_POLICY_CARRY,
            proto.AUTO_MODE_INLET_SWITCH: proto.AUTO_SWITCH_POLICY_DIRECT,
        })
        cfg.auto["backwash"].update({
            "devices": ["valve2"],
            "stir_rpm": 960,
            proto.AUTO_MODE_STIR_SWITCH: proto.AUTO_SWITCH_POLICY_DIRECT,
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
            # 的切换条件**（压差触发/定时触发＝唯一事实源）。
            "cycle_modes": ["rinse", "produce", "drain"],
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
        check("自动模式：**换相策略**留存（搅拌泵=保持上一模式）",
              p.get(proto.AUTO_MODE_STIR_SWITCH)
              == proto.AUTO_SWITCH_POLICY_CARRY,
              "%s" % (p.get(proto.AUTO_MODE_STIR_SWITCH),))
        check("自动模式：**换相策略**留存（进水泵=直接切到当前模式）",
              p.get(proto.AUTO_MODE_INLET_SWITCH)
              == proto.AUTO_SWITCH_POLICY_DIRECT,
              "%s" % (p.get(proto.AUTO_MODE_INLET_SWITCH),))
        b = cfg2.auto.get("backwash") or {}
        check("自动模式：另一个模式也留存（转速/策略）",
              b.get("stir_rpm") == 960
              and b.get(proto.AUTO_MODE_STIR_SWITCH)
              == proto.AUTO_SWITCH_POLICY_DIRECT,
              "rpm=%s sw=%s" % (b.get("stir_rpm"),
                                b.get(proto.AUTO_MODE_STIR_SWITCH)))

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
              ["rinse", "produce", "drain"],
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
    finally:
        shutil.rmtree(base, ignore_errors=True)

    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
