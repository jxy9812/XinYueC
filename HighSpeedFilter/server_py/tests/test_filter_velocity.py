# -*- coding: utf-8 -*-
"""滤速（m/h = 瞬时流量 / 过滤面积）服务端换算 + 配置面（2026-11-25）。

[用户口径] 「这里增加滤速显示，单位 m/h，公式是瞬时流量/面积，面积在远端
设置中设置一个固定值，每次更新瞬时流量的时候，计算下滤速并更新」。

[为什么在服务端算] 桌面/网页/未来 C 端只显示同一个值，避免三端各写一份
除法；面积改完下一帧流量计快照就按新面积算（流量计每拍推）。
"""

import os
import shutil
import sys
import tempfile

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import bson, config as cfgmod       # noqa: E402
from hsfsrv import proto                        # noqa: E402
from fake_devices import build_fake_app         # noqa: E402

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


class _FM(object):
    """只提供 encode_flowmeter 用到的字段。"""

    def __init__(self):
        self.online = True
        self.flow = 3.6
        self.speed = 0.12
        self.forward_total = 10.5
        self.reverse_total = 0.0
        self.flow_unit = 5
        self.total_unit = 1
        self.alarm_empty = False
        self.alarm_upper = False
        self.alarm_lower = False
        self.alarm_system = False

    def alarm_mask(self):
        return 0


def _rig(basedir):
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    business = build_fake_app(cfg, log=lambda _m: None)
    business.flowmeter = _FM()
    return business, cfg


def _doc(business):
    return bson.decode(business.encode_flowmeter(business.flowmeter))


def main():
    basedir = tempfile.mkdtemp(prefix="hsf_filtervel_")
    try:
        business, cfg = _rig(basedir)

        # A1 缺省面积 0 ⇒ 不计算、不下发滤速键（界面 "--"，不是 0）
        doc = _doc(business)
        check("A1 缺省面积 0：不下发滤速键", "filterVelocityMh" not in doc
              and "filterAreaM2" not in doc, "%r" % doc)

        # A2 设置面积 0.6 m² ⇒ 3.6 / 0.6 = 6.0 m/h，随帧下发
        changed, restart = business._cfg_set_collect_key(
            proto.CFGK_FILTER_AREA_M2, 0.6)
        check("A2 SET 面积生效且不重启轮询",
              changed is True and restart is False
              and abs(cfg.filter_area_m2 - 0.6) < 1e-9,
              "changed=%r restart=%r area=%r"
              % (changed, restart, cfg.filter_area_m2))
        doc = _doc(business)
        check("A2b 滤速 = 3.6 / 0.6 = 6.0 m/h",
              abs(float(doc.get("filterVelocityMh", -1)) - 6.0) < 1e-6
              and abs(float(doc.get("filterAreaM2", -1)) - 0.6) < 1e-6,
              "%r" % doc)

        # A3 改面积后下一帧立即按新面积（1.2 m² ⇒ 3.0 m/h）
        business._cfg_set_collect_key(proto.CFGK_FILTER_AREA_M2, 1.2)
        doc = _doc(business)
        check("A3 改面积 ⇒ 下一帧滤速 3.0 m/h",
              abs(float(doc.get("filterVelocityMh", -1)) - 3.0) < 1e-6,
              "%r" % doc.get("filterVelocityMh"))

        # A4 离线：保留面积、不下发滤速（不拿旧流量继续算）
        business.flowmeter.online = False
        doc = _doc(business)
        check("A4 流量计离线：不写滤速键（valid=false）",
              "filterVelocityMh" not in doc and doc.get("valid") is False,
              "%r" % doc)
        business.flowmeter.online = True

        # A5 流量为 0 时滤速是 0（真值，不是缺数据）
        business.flowmeter.flow = 0.0
        doc = _doc(business)
        check("A5 流量 0 ⇒ 滤速 0.000（真值）",
              "filterVelocityMh" in doc
              and abs(float(doc.get("filterVelocityMh"))) < 1e-9,
              "%r" % doc)
        # [2026-11-25 用户实报「显示 -0.00」] 零点微小负值/反向流量 ⇒
        # 过滤方向速率夹到 0（不能让界面出现负号）。
        business.flowmeter.flow = -0.001
        doc = _doc(business)
        check("A5b 微小负流量 -0.001 ⇒ 滤速 0.000（不显示 -0.00）",
              "filterVelocityMh" in doc
              and float(doc.get("filterVelocityMh")) == 0.0,
              "%r" % doc)
        business.flowmeter.flow = -2.0
        doc = _doc(business)
        check("A5c 反向流量 -2.0 ⇒ 过滤方向滤速 0.000",
              "filterVelocityMh" in doc
              and float(doc.get("filterVelocityMh")) == 0.0,
              "%r" % doc)
        business.flowmeter.flow = 3.6

        # A6 GET collect 节回显 filterAreaM2
        cdoc = business._config_collect_doc()
        check("A6 collect GET 回显 filterAreaM2=1.2",
              abs(float(cdoc.get(proto.CFGK_FILTER_AREA_M2)) - 1.2) < 1e-6,
              "%r" % cdoc.get(proto.CFGK_FILTER_AREA_M2))

        # A7 非法值 fail-closed / 越界夹取
        try:
            business._cfg_set_collect_key(proto.CFGK_FILTER_AREA_M2, "abc")
            check("A7a 非数字面积被拒", False, "未抛异常")
        except ValueError:
            check("A7a 非数字面积被拒", True)
        business._cfg_set_collect_key(proto.CFGK_FILTER_AREA_M2, -1)
        check("A7b 负数夹到 0", cfg.filter_area_m2 == 0.0,
              "%r" % cfg.filter_area_m2)
        business._cfg_set_collect_key(proto.CFGK_FILTER_AREA_M2, 99999)
        check("A7c 超上限夹到 10000",
              abs(cfg.filter_area_m2 - proto.FILTER_AREA_M2_MAX) < 1e-9,
              "%r" % cfg.filter_area_m2)

        # A8 落盘往返
        business._cfg_set_collect_key(proto.CFGK_FILTER_AREA_M2, 0.75)
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = cfg.config_path
        cfg2.load()
        check("A8 落盘往返 filter_area_m2=0.75",
              abs(cfg2.filter_area_m2 - 0.75) < 1e-9,
              "%r" % cfg2.filter_area_m2)
    finally:
        shutil.rmtree(basedir, ignore_errors=True)
    print("\n===== 滤速换算: PASS %d / FAIL %d =====" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
