# -*- coding: utf-8 -*-
"""变频器参数读写（0x011B，2026-11-18 新增）。

[测什么]
    A. **读面**：模拟器默认参数组原样读出（工程值换算对）、应答键面、
       读=登录即可（PERM_VIEW）、未装配包络。
    B. **写面**：停机写 2 项 → 命令落寄存器 + 回读值 == 写入值（写后回读
       口径）；RunLocked（0x3000=1/2）拒写且不发总线写；**回读不撒谎**
       （驱动器拒写但 ACK 时，应答 params 用寄存器实际值、applied=False）；
       权限（写=管理员，PERM_ACCOUNT_ADMIN，照 0x0117 文案）；载荷预检
       fail-closed（未知地址/超量程/非 int/键非法 → 整包拒、不发总线）。
    C. **虚拟模型**：VirtualVfd 0xF100~0xF105 寄存器（读路径/停机落/
       运行中拒写）+ PARAM_DEFS 与 PARAM_DEFAULTS 两表同步。
    D. **参数表镜像**：PARAM_DEFS 六项与手册（docs §9.5 / XVfdM512.c
       s_m512Params[]）逐项对账——防止表被静默改错。

[怎么跑]
    python -I tests/test_vfd_params.py        # 独立跑
    pytest tests/test_vfd_params.py -q        # pytest 收集

服务端在本进程内起（假设备 + 真业务层）；变频器用 FakeVfd + FakeRtuMaster
——**真设备层代码**（read_params_snapshot/write_param）跑真路径，
只有"电信号"是内存 raw 表。
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

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto                     # noqa: E402
from fake_devices import build_fake_app      # noqa: E402


PASS = 0
FAIL = 0

#: P1 组地址（十进制，写 values 键用它的字符串形式）。
ADDR_P100 = 0xF100            # 61440 电机类型选择
ADDR_P101 = 0xF101            # 61441 额定功率 kW（scale 10）
ADDR_P102 = 0xF102            # 61442 额定电压 V（scale 1）
ADDR_P103 = 0xF103            # 61443 额定电流 A（scale 100）
ADDR_P104 = 0xF104            # 61444 额定频率 Hz（scale 10）
ADDR_P105 = 0xF105            # 61445 额定转速 rpm（scale 1）
P1_ADDRS = (ADDR_P100, ADDR_P101, ADDR_P102, ADDR_P103, ADDR_P104,
            ADDR_P105)


def check(name, ok, detail=""):
    """记一条断言结果（口径与其它 tests 一致：PASS/FAIL 计数 + 明细）。"""
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


class _FakeServer(object):
    """假服务端：0x011B 是请求/应答命令（不走 publish），只留壳。"""

    def __init__(self):
        self.frames = []

    async def publish(self, topic, payload):
        self.frames.append((topic, payload))


class _Session(object):
    """假会话：role() 决定权限档；send_response 只收最后一份应答。"""

    def __init__(self, role=proto.ROLE_ADMIN):
        self._role = role
        self.resp = None

    def role(self):
        return self._role

    async def send_response(self, request_id, msg_code, payload):
        self.resp = bson.decode(payload)


def _rig(basedir):
    """假设备业务层（与 test_protect._rig 同款）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    business = build_fake_app(cfg, log=lambda m: None)
    business.server = _FakeServer()
    business.store.insert_alarm = lambda *a, **k: True
    return business, cfg


async def _request(business, session, doc):
    """走真 dispatch 面发一次 0x011B（bson 编解码 + 处理器 + 应答包络）。"""
    await business.handle_request(session, 0x11B1, proto.CMD_VFD_PARAMS,
                                  2, bson.encode(doc))
    return session.resp


def _params_by_addr(resp):
    return dict((p["addr"], p) for p in resp.get(proto.KEY_VFD_PARAMS, []))


async def _set_running(business, now_ms, running):
    """把假变频器切到运行/停机并刷新状态字（0x3000 由 poll 写入 raw）。"""
    business.vfd._running = bool(running)
    await business.vfd.poll(now_ms)


# ============================================================================
#                              A 读面
# ============================================================================

async def test_read():
    """A1~A5：默认值读出 / 键面 / 读权限 / 运行状态回显。"""
    print("=== A 读面 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_vfdp_")
    try:
        business, cfg = _rig(basedir)
        vfd = business.vfd
        await _set_running(business, 1000, True)      # 缺省态=运行中

        # A1 默认值原样读出（工程值换算对）：raw 来自模拟器缺省，
        # value = raw ÷ scale（param_value_of 口径）。
        sess = _Session(proto.ROLE_USER)              # 读=登录即可
        resp = await _request(business, sess,
                              {"op": proto.VFD_PARAMS_OP_READ})
        check("A1 读应答 ok=True", resp is not None and resp.get("ok") is True,
              "%r" % (resp,))
        params = resp.get(proto.KEY_VFD_PARAMS, [])
        check("A2 参数组共 6 项（P1.00~P1.05）", len(params) == 6,
              "n=%d" % len(params))
        by_addr = _params_by_addr(resp)
        from hsfsrv import virtualdev
        defaults = virtualdev.VirtualVfd.PARAM_DEFAULTS
        ok_raw = all(by_addr.get(a, {}).get("raw") == defaults[a]
                     for a in P1_ADDRS if a in by_addr)
        check("A3 六项 raw 与模拟器缺省一致", ok_raw and len(by_addr) == 6,
              "%r" % (sorted(by_addr),))
        # 工程值抽验：P1.04 raw 500 ÷ 10 = 50.0 Hz；P1.03 raw 3000 ÷ 100 =
        # 30.0 A；P1.02 scale 1 → 380 V（int）；P1.05 = 1440 rpm。
        check("A4a P1.04 额定频率 = 50.0 Hz",
              by_addr[ADDR_P104]["value"] == 50.0
              and by_addr[ADDR_P104]["unit"] == "Hz",
              "%r" % (by_addr[ADDR_P104],))
        check("A4b P1.03 额定电流 = 30.0 A",
              by_addr[ADDR_P103]["value"] == 30.0
              and by_addr[ADDR_P103]["unit"] == "A",
              "%r" % (by_addr[ADDR_P103],))
        check("A4c P1.02/P1.05 整数工程值（380 V / 1440 rpm）",
              by_addr[ADDR_P102]["value"] == 380
              and by_addr[ADDR_P105]["value"] == 1440,
              "%r %r" % (by_addr[ADDR_P102], by_addr[ADDR_P105]))
        # A5 键面（契约：code/label/addr/raw/value/unit/writable/runLocked；
        # 读操作不带 applied）。
        item = by_addr[ADDR_P101]
        keys = set(item.keys())
        check("A5 参数项键面与契约一致（读不带 applied）",
              keys == {"code", "label", "addr", "raw", "value", "unit",
                       "writable", "runLocked"},
              "%r" % (sorted(keys),))
        check("A5b P1.01 条目内容（code/label/writable/runLocked）",
              item["code"] == "P1.01" and item["label"] == "电机额定功率"
              and item["writable"] is True and item["runLocked"] is True,
              "%r" % (item,))
        check("A6 应答包络：vfdFaulted=False + runState=1（运行中）",
              resp.get(proto.KEY_VFD_FAULTED) is False
              and resp.get(proto.KEY_VFD_RUN_WORD) == 1,
              "faulted=%r runState=%r" % (resp.get(proto.KEY_VFD_FAULTED),
                                          resp.get(proto.KEY_VFD_RUN_WORD)))

        # A7 停机后读：runState=3（0x3000 原始字如实回显）。
        await _set_running(business, 2000, False)
        resp = await _request(business, _Session(),
                              {"op": proto.VFD_PARAMS_OP_READ})
        check("A7 停机读：runState=3",
              resp.get(proto.KEY_VFD_RUN_WORD) == 3,
              "runState=%r" % (resp.get(proto.KEY_VFD_RUN_WORD),))

        # ---- A8 枚举型参数（P1.00 电机类型）：options + valueText ----
        # [需求 2026-11-18] 电机类型要能下拉选中文类型 ⇒ 选项随应答下发。
        by_addr = {p["addr"]: p for p in (resp.get("params") or [])
                   if isinstance(p, dict)}
        p100 = by_addr.get(ADDR_P100) or {}
        opts = p100.get("options")
        check("A8a P1.00 带 options（3 项，手册口径的中文名）",
              isinstance(opts, list) and len(opts) == 3
              and [o.get("value") for o in opts] == [0, 1, 2]
              and [o.get("label") for o in opts]
              == ["普通异步电机", "变频异步电机", "永磁同步电机"],
              "%r" % (opts,))
        check("A8b P1.00 带 valueText（按**回读** raw 取名：raw=0⇒普通异步电机）",
              p100.get("valueText") == "普通异步电机",
              "raw=%r valueText=%r" % (p100.get("raw"), p100.get("valueText")))
        p101 = by_addr.get(ADDR_P101) or {}
        check("A8c 数值型参数不带 options/valueText（老客户端键面不变）",
              "options" not in p101 and "valueText" not in p101,
              "%r" % (sorted(p101.keys()),))
        # A8d 写 1（变频异步电机）⇒ 回读的名字跟着**回读值**走。
        await _request(business, _Session(proto.ROLE_ADMIN), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P100): 1}})
        resp = await _request(business, _Session(),
                              {"op": proto.VFD_PARAMS_OP_READ})
        p100 = {p["addr"]: p for p in (resp.get("params") or [])
                if isinstance(p, dict)}.get(ADDR_P100) or {}
        check("A8d 写 1 后回读 valueText=变频异步电机（名字跟回读值走）",
              p100.get("raw") == 1
              and p100.get("valueText") == "变频异步电机",
              "raw=%r valueText=%r" % (p100.get("raw"),
                                       p100.get("valueText")))
        # A8e raw 落在选项表外 ⇒ **不给** valueText（不编名字），但 options
        # 仍下发（前端照样能下拉）。
        business.vfd.raw[ADDR_P100] = 5
        resp = await _request(business, _Session(),
                              {"op": proto.VFD_PARAMS_OP_READ})
        p100 = {p["addr"]: p for p in (resp.get("params") or [])
                if isinstance(p, dict)}.get(ADDR_P100) or {}
        check("A8e 表外 raw（5）⇒ 不给 valueText（不编）+ 仍带 options",
              p100.get("raw") == 5 and "valueText" not in p100
              and isinstance(p100.get("options"), list),
              "%r" % (p100,))
        business.vfd.raw[ADDR_P100] = 0
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                              B 写面
# ============================================================================

async def test_write():
    """B1~B4：停机写落寄存器 + 回读一致 / RunLocked 拒写 / 回读不撒谎。"""
    print("=== B 写面 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_vfdp_")
    try:
        business, cfg = _rig(basedir)
        vfd = business.vfd
        await _set_running(business, 1000, False)     # 停机
        sess = _Session(proto.ROLE_ADMIN)

        # ---- B1 停机写 2 项：命令落寄存器 + 回读值 == 写入值 ----
        resp = await _request(business, sess, {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 220,      # 22.0 kW
                       str(ADDR_P105): 1480},    # 1480 rpm
        })
        check("B1 停机写应答 ok=True",
              resp is not None and resp.get("ok") is True, "%r" % (resp,))
        by_addr = _params_by_addr(resp)
        check("B1b 命令落寄存器（0xF101=220 / 0xF105=1480）",
              vfd.raw[ADDR_P101] == 220 and vfd.raw[ADDR_P105] == 1480,
              "raw=%r" % ((vfd.raw[ADDR_P101], vfd.raw[ADDR_P105]),))
        check("B1c 写后回读值 == 写入值（params 用回读填表）",
              by_addr[ADDR_P101]["raw"] == 220
              and by_addr[ADDR_P101]["value"] == 22.0
              and by_addr[ADDR_P105]["raw"] == 1480
              and by_addr[ADDR_P105]["value"] == 1480,
              "%r %r" % (by_addr[ADDR_P101], by_addr[ADDR_P105]))
        check("B1d 写过的项 applied=True",
              by_addr[ADDR_P101].get(proto.KEY_PARAM_APPLIED) is True
              and by_addr[ADDR_P105].get(proto.KEY_PARAM_APPLIED) is True,
              "%r" % (by_addr[ADDR_P101],))
        check("B1e 没写的项不带 applied",
              proto.KEY_PARAM_APPLIED not in by_addr[ADDR_P100],
              "%r" % (by_addr[ADDR_P100],))

        # ---- B2 RunLocked：运行中写被拒（不发总线写、寄存器不变）----
        await _set_running(business, 2000, True)
        before = (vfd.raw[ADDR_P101], len(vfd.write_log))
        resp = await _request(business, sess, {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 300},
        })
        check("B2 运行中写被拒 ok=False",
              resp.get("ok") is False, "%r" % (resp,))
        check("B2b err 文案 = 三端同款「变频器运行中禁止写参数（请先停止）」",
              resp.get("err") == proto.VFD_PARAMS_ERR_RUN_LOCKED,
              "err=%r" % (resp.get("err"),))
        check("B2c 拒写时不发总线写（寄存器与写记录均不变）",
              vfd.raw[ADDR_P101] == before[0]
              and len(vfd.write_log) == before[1],
              "raw=%r writes=%d" % (vfd.raw[ADDR_P101], len(vfd.write_log)))
        check("B2d 拒绝包络不带 params",
              proto.KEY_VFD_PARAMS not in resp, "%r" % (resp,))

        # ---- B3 回读不撒谎：驱动器拒写但 ACK ⇒ params 用寄存器实际值 ----
        await _set_running(business, 3000, False)
        vfd.deny_write_addrs.add(ADDR_P102)      # 模拟"真机拒写但返回成功"
        resp = await _request(business, sess, {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P102): 400,      # 想写 400 V
                       str(ADDR_P103): 2500},    # 25.00 A（应落）
        })
        vfd.deny_write_addrs.discard(ADDR_P102)
        check("B3 拒写但 ACK：请求整体 ok=True（写帧 ACK 了）",
              resp.get("ok") is True, "%r" % (resp,))
        by_addr = _params_by_addr(resp)
        check("B3b 拒写项 applied=False",
              by_addr[ADDR_P102].get(proto.KEY_PARAM_APPLIED) is False,
              "%r" % (by_addr[ADDR_P102],))
        check("B3c 拒写项回读值 = 寄存器实际值（380 V，不是提交的 400）",
              by_addr[ADDR_P102]["raw"] == 380
              and by_addr[ADDR_P102]["value"] == 380,
              "%r" % (by_addr[ADDR_P102],))
        check("B3d 寄存器实际没变（拒写未落）",
              vfd.raw[ADDR_P102] == 380,
              "raw=%r" % (vfd.raw[ADDR_P102],))
        check("B3e 正常项不受影响（applied=True + 回读 2500）",
              by_addr[ADDR_P103].get(proto.KEY_PARAM_APPLIED) is True
              and by_addr[ADDR_P103]["raw"] == 2500
              and by_addr[ADDR_P103]["value"] == 25.0,
              "%r" % (by_addr[ADDR_P103],))

        # ---- B4 权限：写=管理员（PERM_ACCOUNT_ADMIN，照 0x0117 文案）----
        from hsfsrv import permissions
        await _set_running(business, 4000, False)
        before = (vfd.raw[ADDR_P101], len(vfd.write_log))
        resp = await _request(business, _Session(proto.ROLE_OPERATOR), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 500},
        })
        check("B4 操作者写被拒 ok=False", resp.get("ok") is False,
              "%r" % (resp,))
        check("B4b 拒绝文案与 0x0117 同款",
              resp.get("err") == permissions.denied_text(
                  proto.PERM_ACCOUNT_ADMIN),
              "err=%r" % (resp.get("err"),))
        check("B4c 被拒时不发总线写",
              vfd.raw[ADDR_P101] == before[0]
              and len(vfd.write_log) == before[1],
              "raw=%r" % (vfd.raw[ADDR_P101],))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def test_envelopes_and_validation():
    """B5~B12：未装配包络 / op 非法 / values 预检 fail-closed。"""
    print("=== B 包络与载荷预检 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_vfdp_")
    try:
        business, cfg = _rig(basedir)
        vfd = business.vfd
        await _set_running(business, 1000, False)

        # ---- B5 未装配（读）⇒ ok=false「变频器未就绪」----
        business.vfd = None
        resp = await _request(business, _Session(),
                              {"op": proto.VFD_PARAMS_OP_READ})
        check("B5 未装配读：ok=False + 文案「变频器未就绪」",
              resp.get("ok") is False
              and resp.get("err") == proto.VFD_PARAMS_ERR_NOT_READY,
              "%r" % (resp,))
        # ---- B6 未装配（写）同款包络 ----
        resp = await _request(business, _Session(), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 220},
        })
        check("B6 未装配写：ok=False + 同款包络（不抛异常）",
              resp.get("ok") is False
              and resp.get("err") == proto.VFD_PARAMS_ERR_NOT_READY,
              "%r" % (resp,))
        business.vfd = vfd

        # ---- B7 op 非法 ⇒ 拒（fail-closed）----
        resp = await _request(business, _Session(), {"op": 7})
        check("B7 op 非法被拒",
              resp.get("ok") is False and "op" in str(resp.get("err")),
              "%r" % (resp,))

        # ---- B8~B11 values 预检（整包校验，不许写半截/不许盲转地址）----
        def _writes_count():
            return len(vfd.write_log)

        before = _writes_count()
        resp = await _request(business, _Session(), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(0xF106): 1, str(ADDR_P101): 220},   # 未知地址
        })
        check("B8 PARAM_DEFS 之外的地址整包拒（不发总线写）",
              resp.get("ok") is False and "未知参数地址" in str(resp.get("err"))
              and _writes_count() == before,
              "err=%r writes=%d" % (resp.get("err"), _writes_count()))

        resp = await _request(business, _Session(), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 99999},    # 超 [10, 10000]
        })
        check("B9 超量程整包拒（err 带参数名与量程）",
              resp.get("ok") is False and "超出量程" in str(resp.get("err"))
              and _writes_count() == before,
              "err=%r" % (resp.get("err"),))

        resp = await _request(business, _Session(), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 22.5},     # 非 int 原始码
        })
        check("B10 浮点 raw 被拒（BSON double 不当原始码用）",
              resp.get("ok") is False and "int" in str(resp.get("err")),
              "err=%r" % (resp.get("err"),))

        resp = await _request(business, _Session(), {
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {"P1.01": 220},             # 键不是十进制数
        })
        check("B11 非十进制地址键被拒（fail-closed，不猜测）",
              resp.get("ok") is False and "十进制" in str(resp.get("err")),
              "err=%r" % (resp.get("err"),))

        # ---- B12 缺 values ⇒ 拒 ----
        resp = await _request(business, _Session(),
                              {"op": proto.VFD_PARAMS_OP_WRITE})
        check("B12 写缺 values 被拒",
              resp.get("ok") is False and "values" in str(resp.get("err")),
              "%r" % (resp,))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                          C 虚拟模型（VirtualVfd）
# ============================================================================

async def test_virtual_model():
    """C1~C4：读路径 / 停机写落 / 运行中拒写（不应用）/ 两表同步。"""
    print("=== C 虚拟模型参数寄存器 ===")
    from hsfsrv import virtualdev

    defaults = virtualdev.VirtualVfd.PARAM_DEFAULTS
    master = virtualdev.make_vfd_master()
    model = master.model

    # C1 虚拟 master 的 read 路径能把 0xF1xx 读出来（一帧连读 6 个）。
    vals = await master.read(3, 0xF100, 6, unit=2)
    check("C1 FC03 连读 0xF100×6 = 缺省参数表",
          list(vals) == [defaults[a] for a in P1_ADDRS],
          "%r" % (vals,))

    # C2 停机写 → 落寄存器，读回 == 写入值。
    await master.write_single(6, ADDR_P103, 2500, unit=2)
    vals = await master.read(3, ADDR_P103, 1, unit=2)
    check("C2 停机写 0xF103 落寄存器（读回 2500）", list(vals) == [2500],
          "%r" % (vals,))

    # C3 运行中写 → **不应用**（模拟真机 RunLocked 拒写）：FC06 应答成功
    # 但寄存器不落 ⇒ 服务端写后回读看到不一致（applied=False）。
    model.running = True
    try:
        await master.write_single(6, ADDR_P101, 999, unit=2)
        vals = await master.read(3, ADDR_P101, 1, unit=2)
        check("C3 运行中写不应用（寄存器保持缺省 150）",
              list(vals) == [defaults[ADDR_P101]],
              "%r" % (vals,))
    finally:
        model.running = False

    # C4 PARAM_DEFS 与虚拟模型缺省表的地址集一致（两表同步守门）。
    check("C4 PARAM_DEFS 地址集 == PARAM_DEFAULTS 地址集",
          set(proto.PARAM_BY_ADDR.keys()) == set(defaults.keys()),
          "defs=%r defaults=%r" % (sorted(proto.PARAM_BY_ADDR),
                                   sorted(defaults)))
    # C5 缺省值的工程换算抽验（P1.04 = 50.0 Hz，铭牌额定点）。
    check("C5 缺省铭牌换算（50Hz / 1440rpm 同一口径）",
          proto.param_value_of(defaults[ADDR_P104], 10) == 50.0
          and proto.param_value_of(defaults[ADDR_P105], 1) == 1440,
          "%r" % (defaults,))


# ============================================================================
#                          D 参数表镜像（防漂移）
# ============================================================================

def test_param_defs_table():
    """D1~D2：PARAM_DEFS 六项与手册表（docs §9.5 / XVfdM512.c）逐项对账。"""
    print("=== D 参数表镜像（docs §9.5 / XVfdM512.c s_m512Params[]）===")
    expect = {
        ADDR_P100: ("P1.00", "电机类型选择", "", 1, 0, 2),
        ADDR_P101: ("P1.01", "电机额定功率", "kW", 10, 10, 10000),
        ADDR_P102: ("P1.02", "电机额定电压", "V", 1, 1, 2000),
        ADDR_P103: ("P1.03", "电机额定电流", "A", 100, 1, 65535),
        ADDR_P104: ("P1.04", "电机额定频率", "Hz", 10, 1, 50000),
        ADDR_P105: ("P1.05", "电机额定转速", "rpm", 1, 1, 65535),
    }
    ok = True
    detail = []
    for d in proto.PARAM_DEFS:
        e = expect.get(d["addr"])
        if e is None:
            ok = False
            detail.append("多余项 addr=0x%04X" % d["addr"])
            continue
        code, label, unit, scale, mn, mx = e
        if (d["code"] != code or d["label"] != label or d["unit"] != unit
                or d["scale"] != scale or d["min"] != mn or d["max"] != mx):
            ok = False
            detail.append("%s 不匹配：%r vs %r" % (code, d, e))
    check("D1 六项 code/label/unit/scale/min/max 与手册一致",
          ok and len(proto.PARAM_DEFS) == 6,
          "; ".join(detail))
    check("D2 第一期全部 writable=True + runLocked=True（手册 RunLocked 列）",
          all(d["writable"] is True and d["runLocked"] is True
              for d in proto.PARAM_DEFS),
          "%r" % ([(d["code"], d["writable"], d["runLocked"])
                   for d in proto.PARAM_DEFS],))
    # D3 RunLocked 判定：0x3000 = 1/2 拒写；3（停机）/未知不拒。
    check("D3 vfd_param_run_locked（1/2 锁、3 与未知不锁）",
          proto.vfd_param_run_locked(1) is True
          and proto.vfd_param_run_locked(2) is True
          and proto.vfd_param_run_locked(3) is False
          and proto.vfd_param_run_locked(0) is False
          and proto.vfd_param_run_locked(None) is False,
          "")
    # D4 工程值换算的防浮点噪音抽验（raw 6553 ÷ 100 应为 65.53，不是
    # 65.529999…）。
    check("D4 param_value_of 取整到 scale 位数（65.53）",
          proto.param_value_of(6553, 100) == 65.53
          and proto.param_value_of(0, 10) == 0
          and proto.param_value_of(380, 1) == 380,
          "%r %r %r" % (proto.param_value_of(6553, 100),
                        proto.param_value_of(0, 10),
                        proto.param_value_of(380, 1)))


async def _run():
    await test_read()
    await test_write()
    await test_envelopes_and_validation()
    await test_virtual_model()
    test_param_defs_table()
    # [让出事件循环] 写路径内的 asyncio.sleep（回读重试间隔）需要 loop；
    # 收尾再转一圈，保证没有悬空任务告警。
    await asyncio.sleep(0)


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
