# -*- coding: utf-8 -*-
"""Python 服务端「两条独立 485」配置与地址文案回归锁。

    python server_py/tests/test_dual_485.py

用户反馈：
  「变频器那路是115200是对的，另一路是9600，而且显示的串口号是一样的，
    他们明明是两路独立的，先改python就行」
  「去掉什么ab侧我总共就两个都是一个485总线与流量计都是一个485总线」
  「变频器与温湿度计是另一条485总线」

即：现场两条独立 485
  · 路 A：变频器(站2) + 温湿度(站4)，115200
  · 路 B：流量计(站5) + 浊度仪 ×2(站1/3)，9600
早先 Python 版只配了一路 serial，5 台设备全显示 "COM17 · 115200"，
且真实链路会共用一个 master（9600 设备被按 115200 访问 → 全不通）。

覆盖：
  A. 两路配置缺省值（口不同、速率不同）
  B. 配置 load/save round-trip（serial_b 不丢、路 A 不被污染）
  C. 自定义 serial_b 口/速率、撞车兜底
  D. _addr_rtu 按 bus 显示不同口与速率
  E. 健康表：路 A 设备归 A、路 B 设备归 B、浊度仪恰好 2 台
  F. make_rtu 按 (port,baudrate) 分成两个 master（源码结构锁）
"""

import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import config                              # noqa: E402

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


def new_config(path=None):
    c = config.Config()
    if path:
        c.config_path = path
    return c


# ============================================================================
#                        A. 缺省值
# ============================================================================

def part_a_defaults():
    print("== A. 两路缺省 ==")
    c = new_config()
    check("A1 路 A 115200", c.serial.baudrate == 115200,
          str(c.serial.baudrate))
    check("A2 路 B 9600", c.serial_b.baudrate == 9600,
          str(c.serial_b.baudrate))
    check("A3 两条线串口号不同", c.serial.port != c.serial_b.port,
          "%s vs %s" % (c.serial.port, c.serial_b.port))
    check("A4 路 B 口从路 A 推导（COM17→COM18）",
          c.serial_b.port == config._next_serial_port(c.serial.port),
          "%s → %s" % (c.serial.port, c.serial_b.port))
    check("A5 站号：A=2/4，B=5/1/3",
          (c.vfd_unit_id, c.temphum_unit_id, c.flowmeter_unit_id,
           tuple(c.turbidity_unit_ids)) == (2, 4, 5, (1, 3)),
          str((c.vfd_unit_id, c.temphum_unit_id, c.flowmeter_unit_id,
               c.turbidity_unit_ids)))


# ============================================================================
#                     B/C. 配置 round-trip 与自定义
# ============================================================================

def part_bc_roundtrip():
    print("== B/C. 配置读写 ==")
    tmp = tempfile.mkdtemp()
    try:
        path = os.path.join(tmp, "cfg.json")
        c = new_config(path)
        c.save()
        c2 = new_config(path)
        c2.load()
        check("B1 save/load 后路 A 不变",
              (c2.serial.port, c2.serial.baudrate)
              == (c.serial.port, c.serial.baudrate),
              "%s/%d" % (c2.serial.port, c2.serial.baudrate))
        check("B2 save/load 后路 B 不丢",
              (c2.serial_b.port, c2.serial_b.baudrate)
              == (c.serial_b.port, c.serial_b.baudrate),
              "%s/%d" % (c2.serial_b.port, c2.serial_b.baudrate))
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        check("B3 落盘有独立 serial_b 节",
              "serial_b" in doc and "baudrate" in doc["serial_b"],
              str(sorted(doc.keys())))

        # 自定义 B 路
        json.dump({"serial": {"port": "COM17", "baudrate": 115200},
                   "serial_b": {"port": "COM23", "baudrate": 9600}},
                  open(path, "w", encoding="utf-8"))
        c3 = new_config(path)
        c3.load()
        check("C1 自定义 B 口生效", c3.serial_b.port == "COM23",
              c3.serial_b.port)
        check("C2 自定义 B 速率生效", c3.serial_b.baudrate == 9600,
              str(c3.serial_b.baudrate))
        check("C3 路 A 不被 B 的配置污染",
              (c3.serial.port, c3.serial.baudrate) == ("COM17", 115200),
              "%s/%d" % (c3.serial.port, c3.serial.baudrate))

        # B 口与 A 撞车 → 兜底重推
        json.dump({"serial": {"port": "COM17", "baudrate": 115200},
                   "serial_b": {"port": "COM17", "baudrate": 9600}},
                  open(path, "w", encoding="utf-8"))
        c4 = new_config(path)
        c4.load()
        check("C4 B 口与 A 撞车时兜底换口",
              c4.serial_b.port != c4.serial.port,
              "A=%s B=%s" % (c4.serial.port, c4.serial_b.port))

        # 老配置（无 serial_b）→ 用缺省
        json.dump({"serial": {"port": "COM31", "baudrate": 115200}},
                  open(path, "w", encoding="utf-8"))
        c5 = new_config(path)
        c5.load()
        check("C5 老配置无 serial_b 也能起（缺省 9600）",
              c5.serial_b.baudrate == 9600
              and c5.serial_b.port != c5.serial.port,
              "%s/%d" % (c5.serial_b.port, c5.serial_b.baudrate))
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                     D. 地址文案按路分开
# ============================================================================

def part_d_addr():
    print("== D. 健康表地址文案 ==")
    from hsfsrv.app import App
    c = new_config()
    a = App.__new__(App)
    a.cfg = c

    a_addrs = [a._addr_rtu(c.vfd_unit_id, "a"),
               a._addr_rtu(c.temphum_unit_id, "a")]
    b_addrs = [a._addr_rtu(c.flowmeter_unit_id, "b"),
               a._addr_rtu(c.turbidity_unit_ids[0], "b"),
               a._addr_rtu(c.turbidity_unit_ids[1], "b")]
    for x in a_addrs:
        check("D-A %s 用 115200 + 路A口" % x.split(" · 站")[0],
              "115200" in x and c.serial.port in x and c.serial_b.port not in x,
              x)
    for x in b_addrs:
        check("D-B %s 用 9600 + 路B口" % x.split(" · 站")[0],
              "9600" in x and c.serial_b.port in x, x)
    check("D1 路 A 地址不出现 9600",
          all("9600" not in x for x in a_addrs))
    check("D2 路 B 地址不出现 115200",
          all("115200" not in x for x in b_addrs))
    check("D3 每条地址都带站号",
          all("站 " in x for x in a_addrs + b_addrs))
    check("D4 五台设备地址不再全一样（原 bug）",
          len(set(a_addrs + b_addrs)) == 5,
          str(sorted(set(a_addrs + b_addrs))))
    # 不传 bus 默认路 A（向后兼容旧调用）
    check("D5 不传 bus 默认路 A（向后兼容）",
          a._addr_rtu(c.vfd_unit_id) == a_addrs[0])


# ============================================================================
#                       E. 健康表分组与台数
# ============================================================================

def part_e_health():
    print("== E. 健康表分组 ==")
    src_path = os.path.join(os.path.dirname(HERE), "hsfsrv", "app.py")
    with open(src_path, encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    # 路 B 设备（流量计/浊度仪）的 _addr_rtu 调用必须带 "b"
    check("E1 流量计健康条目用路 B",
          'self._addr_rtu(\n                                               self.cfg.flowmeter_unit_id, "b"))'
          in src or '_addr_rtu(self.cfg.flowmeter_unit_id, "b")' in src,
          "源码里流量计应传 bus=b")
    check("E2 浊度仪健康条目用路 B",
          'self._addr_rtu(unit, "b")' in src,
          "源码里浊度仪应传 bus=b")
    check("E3 变频器/温湿度仍用路 A（不传 b）",
          'self._addr_rtu(self.cfg.vfd_unit_id)' in src
          and 'self._addr_rtu(self.cfg.temphum_unit_id)' in src)
    # 浊度仪恰好 2 台（循环取 [:2]）
    check("E4 浊度仪恰好 2 台（turbidity_unit_ids[:2]）",
          "self.cfg.turbidity_unit_ids[:2]" in src, "不按侧复制")
    check("E5 设备名不带 -A/-B 侧标",
          'HealthEntry("浊度仪%d"' in src
          and '浊度仪%d-%s' not in src and '变频器-%s' not in src)


# ============================================================================
#                   F. 真实链路分成两个 master
# ============================================================================

def part_f_masters():
    print("== F. 两路各自 master ==")
    src_path = os.path.join(os.path.dirname(HERE), "hsfsrv", "app.py")
    with open(src_path, encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    check("F1 make_rtu 带 bus 参数",
          "def make_rtu(unit_id, bus=\"a\")" in src)
    check("F2 按 bus 选 cfg.serial / cfg.serial_b",
          'c = self.cfg.serial_b if bus == "b" else self.cfg.serial' in src)
    check("F3 master 缓存 key 含口与速率（两路天然分开）",
          "key = (c.port, c.baudrate)" in src)
    check("F4 流量计/浊度仪走路 B",
          'make_rtu(self.cfg.flowmeter_unit_id, "b")' in src
          and 'make_rtu(uid, "b")' in src)
    check("F5 变频器/温湿度走路 A",
          'make_rtu(self.cfg.vfd_unit_id, "a")' in src
          and 'make_rtu(self.cfg.temphum_unit_id, "a")' in src)


def main():
    part_a_defaults()
    part_bc_roundtrip()
    part_d_addr()
    part_e_health()
    part_f_masters()
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
