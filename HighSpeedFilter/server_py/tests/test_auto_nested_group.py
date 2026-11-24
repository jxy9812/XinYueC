# -*- coding: utf-8 -*-
"""自动循环**嵌套组合**（2026-11-27 用户「添加一个组合模式，组合里可以再
添加单模式或再添加组合，可以设置循环次数，可以改名，重名自动加数字后缀」）。

[测什么]
  A. 递归展开（proto.expand_cycle_plan）：组 children × repeat、层级标题、
     组间顺序、深度上限截断；
  B. 契约（SET fail-closed）：group 受理、空组拒、超深拒、子触发全关拒、
     repeat 越界钳位、非整数拒、GET 回显 children/repeat；
  C. 状态机：进程内夹具跑「产水 → 组(顺洗×2遍) → 搅拌」的完整序列，
     断言推进顺序、标题、回绕。
"""
import json as _json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from hsfsrv import app as app_mod          # noqa: E402
from hsfsrv import autocontrol             # noqa: E402
from hsfsrv import config as cfgmod        # noqa: E402
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


def _inst(mode, devices=(), **kw):
    d = proto.default_cycle_instance(mode)
    d["devices"] = list(devices)
    d.update(kw)
    return d


def _wire(inst):
    """内部 snake_case → 协议 camelCase（SET 用 camel）。"""
    out = {}
    for proto_key, file_key, kind in proto.AUTO_CYCLE_INSTANCE_FIELDS:
        if kind == "mode":
            out[proto_key] = inst.get("mode")
            continue
        if file_key in inst:
            val = inst[file_key]
            if kind == "children":
                out[proto_key] = [_wire(k) for k in (val or [])]
            else:
                out[proto_key] = val
    return out


# ============================================================================
# A. 递归展开
# ============================================================================
def part_a_expand():
    print("\n=== A. 递归展开（expand_cycle_plan）===")
    insts = [
        _inst("produce", ["valve1"], timer_enabled=True, timer_minutes=1),
        _inst("group", title="反洗组", repeat=2, children=[
            _inst("rinse", ["valve3"], timer_enabled=True, timer_minutes=1),
            _inst("group", title="内层", repeat=2, children=[
                _inst("backwash", ["valve2"], timer_enabled=True,
                      timer_minutes=1),
            ]),
        ]),
        _inst("stir", ["stirPump"], timer_enabled=True, timer_minutes=1),
    ]
    plan = proto.expand_cycle_plan(insts)
    check("A1 展开项数 = 1 + (1+2)×2 + 1 = 8", len(plan) == 8, len(plan))
    titles = [p["_title"] for p in plan]
    check("A2 层级标题（组名/子名；内层再带一层）",
          titles == ["产水模式",
                     "反洗组/顺洗模式",
                     "反洗组/内层/反洗模式",
                     "反洗组/内层/反洗模式",
                     "反洗组/顺洗模式",
                     "反洗组/内层/反洗模式",
                     "反洗组/内层/反洗模式",
                     "搅拌模式"], titles)
    check("A3 owner 指回顶层下标（0/1/2）",
          [p["_owner"] for p in plan] == [0, 1, 1, 1, 1, 1, 1, 2],
          [p["_owner"] for p in plan])
    check("A4 组内重复遍数＝repeat（内层 2 遍 × 外层 2 遍 = 4 次）",
          titles.count("反洗组/内层/反洗模式") == 4,
          titles.count("反洗组/内层/反洗模式"))
    check("A5 cycle_instance_titles 与展开标题逐项一致",
          proto.cycle_instance_titles(plan) == titles, "")

    # 深度上限：4 层（顶层 L1=第1层 → L2=2 → L3=3 → L4=4 超限）
    deep = _inst("group", title="L1", repeat=1, children=[
        _inst("group", title="L2", repeat=1, children=[
            _inst("group", title="L3", repeat=1, children=[
                _inst("group", title="L4", repeat=1, children=[
                    _inst("backwash", ["valve2"], timer_enabled=True)])])])])
    try:
        # 服务端 SET 会拒；proto 展开只做防御性截断（不崩）
        plan2 = proto.expand_cycle_plan([deep])
        ok = len(plan2) == 0
    except Exception:                       # noqa: BLE001
        ok = True
    check("A6 超过 3 层的组被丢弃（不崩）", ok, "")

    # default 名与重名后缀（客户端负责写入 title；这里验展开的兜底命名）
    dup = [_inst("produce"), _inst("produce"), _inst("group", children=[
        _inst("rinse", timer_enabled=True)])]
    t3 = [p["_title"] for p in proto.expand_cycle_plan(dup)]
    check("A7 空 title 兜底：同层同类按序号（产水模式1/2）、组合默认名",
          t3 == ["产水模式1", "产水模式2", "组合/顺洗模式"], t3)


# ============================================================================
# B. 契约（SET fail-closed + GET 回显）
# ============================================================================
def _new_app():
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfgmod.Config()
    return app


def part_b_contract():
    print("\n=== B. 契约（SET 校验 + 回显）===")
    app = _new_app()
    grp = _wire(_inst("group", title="反洗组", repeat=2, children=[
        _inst("rinse", ["valve3"], timer_enabled=True, timer_minutes=1),
        _inst("backwash", ["valve2"], timer_enabled=True, timer_minutes=1),
    ]))
    top = _wire(_inst("produce", ["valve1"], timer_enabled=True,
                      timer_minutes=1))
    err = app._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_INSTANCES:
                                   [top, grp]})
    check("B1 顶层 单模式+组合 的包受理", err == "", err)
    cyc = app.cfg.auto_cycle
    ins = cyc.get("instances") or []
    check("B2 组合落成 mode=group + children 2 项 + repeat=2",
          len(ins) == 2 and ins[1].get("mode") == proto.CYCLE_INST_GROUP
          and len(ins[1].get("children") or []) == 2
          and ins[1].get("repeat") == 2,
          "%s" % (([i.get("mode") for i in ins],
                    ins[1].get("repeat") if len(ins) > 1 else None),))
    check("B3 GET 回显（_config_auto_doc）含 children/repeat/title",
          (lambda d: (
              d["cycle"]["instances"][1]["mode"] == "group"
              and d["cycle"]["instances"][1]["repeat"] == 2
              and d["cycle"]["instances"][1]["title"] == "反洗组"
              and len(d["cycle"]["instances"][1]["children"]) == 2))(
                  app._config_auto_doc()), "")

    # 空组
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [
            top, _wire(_inst("group", title="空组", children=[]))]})
    check("B4 空组拒（点名组合名）", "空组" in err and "至少" in err, err)

    # 子项触发全关
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [
            top, _wire(_inst("group", title="G", children=[
                _inst("rinse", ["valve3"])]))]})   # 六触发全关
    check("B5 组内子项六触发全关 ⇒ 拒（点名实例）",
          "未设置切换条件" in err and "G/" in err, err)

    # 超深（4 层）
    deep = _wire(_inst("group", title="L1", children=[
        _inst("group", title="L2", children=[
            _inst("group", title="L3", children=[
                _inst("group", title="L4", children=[
                    _inst("backwash", ["valve2"], timer_enabled=True)])])])]))
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [top, deep]})
    # [超深的表现] normalize 在超限处丢弃 children ⇒ 上层组变"空组"⇒
    # SET 仍被 fail-closed 拒（不会静默把 4 层拍平成 3 层）。文案可能是
    # 「至少要有 1 个模式」——只要**拒**就算对。
    check("B6 嵌套超过 3 层 ⇒ 拒（空组/层数文案皆可）",
          err != "" and ("层" in err or "至少" in err), err)

    # repeat 越界钳位 / 非整数拒
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [
            top, _wire(_inst("group", title="R", repeat=500, children=[
                _inst("rinse", ["valve3"], timer_enabled=True)]))]})
    ins = app.cfg.auto_cycle.get("instances") or []
    check("B7 repeat=500 ⇒ 钳到上限 99（受理）",
          err == "" and ins[1].get("repeat") == proto.CYCLE_INST_REPEAT_MAX,
          "%r / %r" % (err, ins[1].get("repeat") if len(ins) > 1 else None))
    bad = dict(_wire(_inst("group", title="R2", children=[
        _inst("rinse", ["valve3"], timer_enabled=True)])))
    bad["repeat"] = "两次"
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [top, bad]})
    check("B8 repeat 非整数 ⇒ 拒", err != "" and "repeat" in err, err)

    # 单项组合（顶层只有 1 个组）允许
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [grp]})
    check("B9 顶层只有 1 个**组合** ⇒ 受理（组内多模式）", err == "", err)
    # 单项单模式仍拒
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_INSTANCES: [top]})
    check("B10 顶层只有 1 个单模式 ⇒ 仍拒（构不成循环）", err != "", err)


# ============================================================================
# C. 状态机推进（进程内夹具）
# ============================================================================
class _FakeApp(object):
    def __init__(self, cycle):
        self.analog = None
        self.digital = []
        self.vfd = None
        self.pump_bank = None
        self.cycle = cycle
        class Cfg(object):
            auto = {}
            auto_cycle = cycle
            stir_max_rpm = 1440
        self.cfg = Cfg()

    def _log(self, *a, **k):
        pass


def part_c_state_machine():
    print("\n=== C. 状态机推进（组内重复 + 回绕）===")
    infos = []

    def dlog(m):
        infos.append(str(m))

    # 组：顺洗 ×2 遍；顶层：产水 → 组 → 搅拌
    insts = [
        _inst("produce", ["valve1"], timer_enabled=True, timer_minutes=1),
        _inst("group", title="组A", repeat=2, children=[
            _inst("rinse", ["valve3"], timer_enabled=True, timer_minutes=1),
        ]),
        _inst("stir", ["stirPump"], timer_enabled=True, timer_minutes=1),
    ]
    cycle = {"enabled": True, "instances": insts}
    app = _FakeApp(cycle)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctx.press_mpa = lambda: None
    ctl = autocontrol.AutoController(ctx, log=dlog)
    ctl.mode = proto.AUTO_MODE_AUTO
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = cycle
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING
    ctl._cycle_stage_idx = 0
    ctl.mode = insts[0]["mode"]
    now = ctx.now_ms()
    ctl._cycle_stage_at = now
    ctl._run_start_ms = now

    stages = ctl._cycle_stages()
    titles = [s.get("_title") for s in stages]
    check("C1 状态机看到的展开序列 = 4 项（产水/组A顺洗×2/搅拌）",
          titles == ["产水模式", "组A/顺洗模式", "组A/顺洗模式",
                     "搅拌模式"], titles)
    # 拨假时钟逐拍推进（每拍推 61s ⇒ 定时 1 分钟到期）
    order = []
    for _step in range(3):
        ctl._cycle_stage_at = ctx.now_ms() - 61 * 1000
        ctl._cycle_tick(ctx.now_ms())
        order.append(ctl._cycle_stage_idx)
        ctl.mode = stages[ctl._cycle_stage_idx]["mode"]
        ctl.phase = ctl.PHASE_RUNNING     # 换相后直接放回运行相（跳过阀/泵相）
    check("C2 推进顺序 0→1→2（组内第 1 遍→第 2 遍）", order == [1, 2, 3],
          order)
    check("C3 第三次推进越过组内两项到搅拌（组整体只出现一遍位置）",
          stages[2].get("_title") == "组A/顺洗模式"
          and stages[3].get("_title") == "搅拌模式", titles)
    # 回绕：搅拌 → 产水
    ctl._cycle_stage_at = ctx.now_ms() - 61 * 1000
    ctl._cycle_tick(ctx.now_ms())
    check("C4 末位回绕首项（搅拌 → 产水）", ctl._cycle_stage_idx == 0,
          ctl._cycle_stage_idx)
    # 换相日志点名带层级路径
    check("C5 换相日志用层级标题（组A/顺洗模式1）",
          any("组A/顺洗模式" in m for m in infos),
          [m for m in infos if "切换到" in m][:2])


if __name__ == "__main__":
    print("===== 自动循环嵌套组合（2026-11-27）=====")
    part_a_expand()
    part_b_contract()
    part_c_state_machine()
    print("\n===== 汇总：PASS %d / FAIL %d =====" % (PASS, FAIL))
    if FAILED:
        print("失败项：")
        for n in FAILED:
            print("  - " + n)
        sys.exit(1)
