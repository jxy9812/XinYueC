# -*- coding: utf-8 -*-
"""\u7535\u78c1\u9600\u771f\u673a\u95ed\u73af\uff1a\u5f00/\u5173\u52a8\u4f5c + DO \u53d8\u5316\u6838\u5bf9 + \u72b6\u6001\u5224\u8bfb\u3002

[\u5b89\u5168\u7ea6\u675f]
  \u00b7 \u53ea\u64cd\u4f5c\u7b2c 0 \u8def\uff08\u9a8c\u8bc1\u73af\u5883\u4e34\u65f6\u6620\u5c04\u5230 DO0\uff09\uff1b
  \u00b7 \u6bcf\u6b65\u5199\u5b8c\u7acb\u5373\u56de\u8bfb\u4f4d\u56fe\u6838\u5bf9\uff0c\u6700\u540e**\u5f3a\u5236\u5199\u56de 0**\uff08\u6062\u590d\u65ad\u7535\u6001\uff09\uff1b
  \u00b7 \u4e0d\u505a\u957f\u65f6\u95f4\u4fdd\u6301\u8f93\u51fa\u3002

[\u9a8c\u8bc1\u70b9]
  1) \u5f00\u9600 \u2192 DO \u4f4d\u56fe\u7f6e 1 \u2192 \u56de\u8bfb\u786e\u8ba4
  2) \u5173\u9600 \u2192 DO \u4f4d\u56fe\u6e05 0 \u2192 \u56de\u8bfb\u786e\u8ba4
  3) \u72b6\u6001\u5224\u8bfb\uff1a\u65e0\u5230\u4f4d\u53cd\u9988\u65f6\u5e94\u4e3a\u300c\u672a\u77e5 + \u65e0\u5230\u4f4d\u53cd\u9988\u300d\u800c\u975e\u8c0e\u62a5\u300c\u5df2\u5f00/\u5df2\u5173\u300d
  4) \u5f00/\u5173\u672a\u914d\u7f6e\u7684\u8def\u4ecd\u88ab\u62d2\uff08\u70b9\u4f4d\u7f3a\u5931\uff09

\u7528\u6cd5\uff1apython -I verify/digital_io/e2e_valve_real.py [host] [port]
"""
import importlib.util
import os
import sys
import time

_spec = importlib.util.spec_from_file_location(
    "e2e", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "e2e_xlink.py"))
e2e = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(e2e)

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6100


def main():
    fails = []
    ok = []

    def check(cond, name, detail=""):
        (ok if cond else fails).append(name)
        print("  [%s] %s %s" % ("OK" if cond else "FAIL", name, detail))

    print("=== \u7535\u78c1\u9600\u771f\u673a\u95ed\u73af @ %s:%d ===" % (HOST, PORT))
    c = e2e.Client(HOST, PORT)
    c.hello()
    st, role = c.auth("root", "root")
    check(st == 0 and role >= 2, "root \u8ba4\u8bc1", "role=%d" % role)

    # \u786e\u4fdd\u624b\u52a8\u6a21\u5f0f
    c.request(e2e.CMD_RUN_MODE, e2e.BsonDoc().add("mode", 0))

    base = c.request(e2e.CMD_DIGITAL_SNAPSHOT)
    do_before = base.get("do", 0)
    print("      \u521d\u59cb DO=0x%04X DI=0x%04X" % (do_before, base.get("di", 0)))

    vs = c.request(e2e.CMD_VALVE_SNAPSHOT)
    ready0 = vs.get("v0_ready", False)
    check(ready0, "\u96000 \u70b9\u4f4d\u5df2\u914d\u7f6e\uff08DO0/DI0/DI1\uff09", "ready=%s" % ready0)
    if not ready0:
        print("!! \u96000 \u672a\u914d\u7f6e\uff0c\u540e\u7eed\u52a8\u4f5c\u6d4b\u8bd5\u8df3\u8fc7")
        c.sock.close()
        return 1

    if do_before & 1:
        print("      \u9884\u5904\u7406\uff1aDO0 \u5f53\u524d\u4e3a ON\uff0c\u5148\u5199 0 \u6062\u590d")
        c.request(e2e.CMD_DIGITAL_CONTROL, e2e.BsonDoc().add("index", 0).add("on", False))
        base = c.request(e2e.CMD_DIGITAL_SNAPSHOT)
        do_before = base.get("do", 0)

    # ---- 1) \u5f00\u9600 ----
    print("--- \u5f00\u9600 ---")
    r = c.request(e2e.CMD_VALVE_CONTROL, e2e.BsonDoc().add("index", 0).add("on", True))
    check(r.get("ok") is True, "\u5f00\u9600\u547d\u4ee4\u53d7\u7406", str(r.get("err", "")))
    time.sleep(0.4)
    snap = c.request(e2e.CMD_DIGITAL_SNAPSHOT)
    do1 = snap.get("do", 0)
    check(bool(do1 & 1), "\u5f00\u9600\u540e DO0 \u4e3a ON\uff08\u56de\u8bfb\u4f4d\u56fe\uff09", "do=0x%04X" % do1)

    v1 = c.request(e2e.CMD_VALVE_SNAPSHOT)
    st1 = v1.get("v0_state", -1)
    txt1 = v1.get("v0_text", "")
    print("      \u96000 \u72b6\u6001=%d \u8bf4\u660e=%s openFb=%s closeFb=%s"
          % (st1, txt1, v1.get("v0_openFb"), v1.get("v0_closeFb")))
    # \u65e0\u884c\u7a0b\u5f00\u5173\u65f6\uff1a\u4e0d\u5f97\u8c0e\u62a5\u5df2\u5f00\uff08state!=2\uff09
    if not v1.get("v0_openFb"):
        check(st1 != 2, "\u65e0\u5f00\u5230\u4f4d\u53cd\u9988\u65f6\u4e0d\u8c0e\u62a5\u300c\u5df2\u5f00\u300d",
              "state=%d" % st1)

    # ---- 2) \u5173\u9600 ----
    print("--- \u5173\u9600 ---")
    r2 = c.request(e2e.CMD_VALVE_CONTROL, e2e.BsonDoc().add("index", 0).add("on", False))
    check(r2.get("ok") is True, "\u5173\u9600\u547d\u4ee4\u53d7\u7406", str(r2.get("err", "")))
    time.sleep(0.4)
    snap2 = c.request(e2e.CMD_DIGITAL_SNAPSHOT)
    do2 = snap2.get("do", 0)
    check(not (do2 & 1), "\u5173\u9600\u540e DO0 \u4e3a OFF\uff08\u56de\u8bfb\u4f4d\u56fe\uff09", "do=0x%04X" % do2)

    v2 = c.request(e2e.CMD_VALVE_SNAPSHOT)
    print("      \u96000 \u72b6\u6001=%d \u8bf4\u660e=%s"
          % (v2.get("v0_state", -1), v2.get("v0_text", "")))

    # ---- 3) \u672a\u914d\u7f6e\u8def\u4ecd\u88ab\u62d2 ----
    print("--- \u672a\u914d\u7f6e\u8def ---")
    r3 = c.request(e2e.CMD_VALVE_CONTROL, e2e.BsonDoc().add("index", 1).add("on", True))
    check(r3.get("ok") is False, "\u96001 \u672a\u914d\u7f6e \u2192 \u63a7\u5236\u88ab\u62d2\uff08\u7b26\u5408\u8bbe\u8ba1\uff09",
          str(r3.get("err", "")))

    # ---- 4) \u5f3a\u5236\u6062\u590d ----
    print("--- \u6062\u590d\u65ad\u7535\u6001 ---")
    c.request(e2e.CMD_VALVE_CONTROL, e2e.BsonDoc().add("index", 0).add("on", False))
    fin = c.request(e2e.CMD_DIGITAL_SNAPSHOT)
    dof = fin.get("do", 0)
    check(not (dof & 1), "\u6700\u7ec8 DO0 \u5df2\u6062\u590d OFF", "do=0x%04X" % dof)

    c.sock.close()
    print("=== %s\uff08\u901a\u8fc7 %d / \u5931\u8d25 %d\uff09==="
          % ("PASS" if not fails else "FAIL", len(ok), len(fails)))
    for f in fails:
        print("   \u672a\u901a\u8fc7: %s" % f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
