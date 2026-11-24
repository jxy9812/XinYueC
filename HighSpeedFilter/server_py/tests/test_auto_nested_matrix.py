# -*- coding: utf-8 -*-
"""自动循环「组合形态」全矩阵（2026-11-27 用户「所有组合都试试」）。

进程内夹具 + 假时钟：把每种组合形态**实际推进一遍**（不是只看展开），
断言执行序列、层级标题、回绕、暂停续跑位置。每种形态用同一个驱动器：
每个实例都开定时 1 分钟，每拍把 `_cycle_stage_at` 往回拨 61s ⇒ 一拍换相
一次，瞬间跑完整轮。
"""
import os
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsfsrv import autocontrol             # noqa: E402
from hsfsrv import proto                   # noqa: E402

PASS = 0
FAIL = 0
FAILED = []


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  [PASS] %s" % name)
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s  %s" % (name, detail))


def _inst(mode, **kw):
    d = proto.default_cycle_instance(mode)
    # 每个实例都能切走（否则 SET/展开会判"卡死"）；定时 1 分钟。
    d["timer_enabled"] = True
    d["timer_minutes"] = 1
    d.update(kw)
    return d


def _grp(title, children, repeat=1):
    d = proto.default_cycle_instance(proto.CYCLE_INST_GROUP)
    d["title"] = title
    d["children"] = children
    d["repeat"] = repeat
    return d


class _FakeApp(object):
    def __init__(self, cycle):
        self.analog = None
        self.digital = []
        self.vfd = None
        self.pump_bank = None

        class Cfg(object):
            auto = {}
            auto_cycle = cycle
            stir_max_rpm = 1440
        self.cfg = Cfg()


def _ctl(insts, logs=None):
    cycle = {"enabled": True, "instances": insts}
    app = _FakeApp(cycle)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctx.press_mpa = lambda: None
    ctl = autocontrol.AutoController(
        ctx, log=(lambda m: logs.append(m)) if logs is not None
        else (lambda m: None))
    ctl.mode = proto.AUTO_MODE_AUTO
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = cycle
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING
    ctl._cycle_stage_idx = 0
    # [别用原始首项] 顶层首项可能是**组合**（mode="group"），而状态量指向
    # **展开序列**的首个可运行模式 ⇒ 不一致会让 _cycle_stage_index 按
    # mode 兜底找不到（返回 -1）、tick 直接防御返回（一拍都不换）。
    ctl.mode = ctl._cycle_stages()[0].get("mode")
    now = ctx.now_ms()
    ctl._cycle_stage_at = now
    ctl._run_start_ms = now
    return ctl, ctx


def _titles(ctl):
    return [s.get("_title") for s in ctl._cycle_stages()]


def _drive(ctl, ctx, steps):
    """假时钟推进 steps 拍；每拍换相一次。返回经过的 stage 下标序列。"""
    seen = []
    for _ in range(steps):
        ctl._cycle_stage_at = ctx.now_ms() - 61 * 1000
        before = ctl._cycle_stage_idx
        ctl._cycle_tick(ctx.now_ms())
        if ctl._cycle_stage_idx == before:
            break                       # 没换相（不该发生：都开了定时）
        seen.append(ctl._cycle_stage_idx)
        ctl.mode = ctl._cycle_stages()[ctl._cycle_stage_idx].get("mode")
        ctl.phase = ctl.PHASE_RUNNING
    return seen


def run_case(tag, insts, want_titles, want_walk):
    print("\n=== %s ===" % tag)
    ctl, ctx = _ctl(insts)
    got = _titles(ctl)
    check("%s：展开标题" % tag, got == want_titles, "got=%r" % (got,))
    seen = _drive(ctl, ctx, len(want_titles))
    check("%s：推进顺序（含回绕）" % tag, seen == want_walk,
          "got=%r want=%r" % (seen, want_walk))


def main():
    print("===== 自动循环组合形态全矩阵 =====")

    # ① 五种单模式混合（产水/反洗/顺洗/放水/搅拌）
    run_case(
        "① 五种单模式混合",
        [_inst("produce"), _inst("backwash"), _inst("rinse"),
         _inst("drain"), _inst("stir")],
        ["产水模式", "反洗模式", "顺洗模式", "放水模式", "搅拌模式"],
        [1, 2, 3, 4, 0])

    # ② 单模式 + 组合（组内 2 个单模式 × 1 遍）
    run_case(
        "② 产水 → 组合(反洗×2) → 顺洗",
        [_inst("produce"),
         _grp("反洗组", [_inst("backwash"), _inst("rinse")], repeat=1),
         _inst("stir")],
        ["产水模式", "反洗组/反洗模式", "反洗组/顺洗模式", "搅拌模式"],
        [1, 2, 3, 0])

    # ③ 组合 × 3 遍（组内 2 项 ⇒ 展开 6 项）
    run_case(
        "③ 组合 × 3 遍",
        [_grp("洗涤组", [_inst("backwash"), _inst("rinse")], repeat=3),
         _inst("produce")],
        ["洗涤组/反洗模式", "洗涤组/顺洗模式",
         "洗涤组/反洗模式", "洗涤组/顺洗模式",
         "洗涤组/反洗模式", "洗涤组/顺洗模式",
         "产水模式"],
        [1, 2, 3, 4, 5, 6, 0])

    # ④ 两层嵌套（外层 2 遍，内层 2 遍）
    run_case(
        "④ 两层嵌套（外层2遍 × 内层2遍）",
        [_grp("外层", [
            _inst("produce"),
            _grp("内层", [_inst("rinse")], repeat=2),
        ], repeat=2)],
        ["外层/产水模式", "外层/内层/顺洗模式",
         "外层/内层/顺洗模式", "外层/产水模式",
         "外层/内层/顺洗模式", "外层/内层/顺洗模式"],
        [1, 2, 3, 4, 5, 0])

    # ⑤ 三层嵌套
    run_case(
        "⑤ 三层嵌套",
        [(_grp("L1", [
            _inst("produce"),
            _grp("L2", [
                _inst("backwash"),
                _grp("L3", [_inst("rinse")], repeat=2),
            ], repeat=1),
        ], repeat=1))],
        ["L1/产水模式", "L1/L2/反洗模式",
         "L1/L2/L3/顺洗模式", "L1/L2/L3/顺洗模式"],
        [1, 2, 3, 0])

    # ⑥ 组合内搅拌模式 + 组内重复同一模式
    run_case(
        "⑥ 组合内搅拌 + 组内重复反洗",
        [_grp("清洗组", [
            _inst("stir"),
            _inst("backwash"),
            _inst("backwash"),
        ], repeat=1),
         _inst("produce")],
        ["清洗组/搅拌模式", "清洗组/反洗模式1", "清洗组/反洗模式2",
         "产水模式"],
        [1, 2, 3, 0])

    # ⑦ 顶层两个组相邻（同名组靠自定义名区分）
    run_case(
        "⑦ 顶层两个组（各自 1 遍）",
        [_grp("甲组", [_inst("produce"), _inst("rinse")], repeat=1),
         _grp("乙组", [_inst("backwash"), _inst("drain")], repeat=1)],
        ["甲组/产水模式", "甲组/顺洗模式",
         "乙组/反洗模式", "乙组/放水模式"],
        [1, 2, 3, 0])

    # ⑧ 组内子项带自定义名（改名优先于默认序号）
    run_case(
        "⑧ 组内改名（自定义名原样显示）",
        [_grp("工艺组", [
            _inst("produce", title="一级产水"),
            _inst("rinse", title="终洗"),
        ], repeat=1),
         _inst("produce", title="二级产水")],
        ["工艺组/一级产水", "工艺组/终洗", "二级产水"],
        [1, 2, 0])

    # ⑨ 重名自动数字后缀（同层同名 → 第 2 个带 2）
    print("\n=== ⑨ 同层重名自动数字后缀 ===")
    ctl, ctx = _ctl([_inst("produce"), _inst("produce"),
                     _inst("produce"), _inst("rinse")])
    got = _titles(ctl)
    check("⑨ 同层三个产水 ⇒ 产水模式1 / 产水模式2 / 产水模式3",
          got[:3] == ["产水模式1", "产水模式2", "产水模式3"], got)

    # ⑩ 暂停在组内第 2 遍 → 续跑位置正确；复位从头
    print("\n=== ⑩ 暂停（组内第 2 遍）→ 续跑 / 复位从头 ===")
    insts = [_inst("produce"),
             _grp("组A", [_inst("rinse")], repeat=3),
             _inst("stir")]
    ctl, ctx = _ctl(insts)
    # 推进到组内第 2 遍（下标 2）
    _drive(ctl, ctx, 2)
    check("⑩a 推进到组内第 2 遍（展开下标 2、标题 组A/顺洗模式）",
          ctl._cycle_stage_idx == 2
          and _titles(ctl)[2] == "组A/顺洗模式",
          (ctl._cycle_stage_idx, _titles(ctl)))
    # 停止（记住阶段）
    ctl.stop()
    check("⑩b 停止后记忆下标=2 且 root 清空",
          int(getattr(ctl, "_cycle_resume_idx", -1)) == 2
          and ctl._cycle_root == "",
          (ctl._cycle_resume_idx, ctl._cycle_root))
    # 再启动：从下标 2 继续
    ctl.start()
    check("⑩c 再启动从组内第 2 遍继续（下标 2）",
          ctl._cycle_stage_idx == 2, ctl._cycle_stage_idx)
    # 复位 → 从头
    ctl.stop()
    ctl.reset()
    ctl.start()
    check("⑩d 复位后再启动从头（下标 0）",
          ctl._cycle_stage_idx == 0, ctl._cycle_stage_idx)

    print("\n===== 汇总：PASS %d / FAIL %d =====" % (PASS, FAIL))
    if FAILED:
        print("失败项：")
        for n in FAILED:
            print("  - " + n)
        sys.exit(1)


if __name__ == "__main__":
    main()
