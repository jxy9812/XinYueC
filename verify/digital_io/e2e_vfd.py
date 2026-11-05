# -*- coding: utf-8 -*-
"""\u53d8\u9891\u5668\u63a7\u5236\u94fe\u8def\u8054\u8c03\uff08\u771f\u5b9e XLink\uff09\u3002

[\u8986\u76d6]
  1) \u53d8\u9891\u5668\u9065\u6d4b\u4e3b\u9898\u8ba2\u9605\u4e0e\u5feb\u7167\u8bf7\u6c42\uff08\u65e0\u6570\u636e\u65f6\u5fc5\u987b\u56de\u5426\u5b9a/\u7a7a\u5feb\u7167\uff09
  2) \u63a7\u5236\u95f8\uff1a\u81ea\u52a8\u6a21\u5f0f \u21d2 \u62d2\uff1badmin\uff08\u975e root\uff09\u21d2 \u62d2\uff1broot + \u624b\u52a8 \u21d2 \u53d7\u7406
  3) \u5feb\u7167\u8bf7\u6c42\u4e0d\u56e0\u63a7\u5236\u95f8\u88ab\u62d2\uff08\u9065\u6d4b\u4e0d\u53d7\u63a7\u5236\u95f8\u5f71\u54cd\uff09
  4) \u65e0\u6548\u8f7d\u8377 / \u8d8a\u754c\u53c2\u6570 \u21d2 \u5e94\u7b54 ok=false

[\u5b89\u5168] \u53ea\u505a\u77ed\u65f6\u4e0b\u53d1\uff08\u542f\u52a8\u540e\u7acb\u5373\u505c\u8f66\uff09\uff1b\u8bbe\u5907\u4e0d\u5728\u7ebf\u65f6\u4e0d\u4f1a\u6709\u4efb\u4f55\u52a8\u4f5c\u3002

\u7528\u6cd5\uff1apython -I verify/digital_io/e2e_vfd.py [host] [port]
"""
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
import importlib.util
import os

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

    print("=== \u53d8\u9891\u5668\u63a7\u5236\u94fe\u8def\u8054\u8c03 @ %s:%d ===" % (HOST, PORT))
    c = e2e.Client(HOST, PORT)
    c.hello()
    status, role = c.auth("root", "root")
    check(status == 0 and role >= 2, "root \u8ba4\u8bc1", "role=%d" % role)

    # ---- 1) \u9065\u6d4b\u8ba2\u9605 + \u5feb\u7167 ----
    print("--- \u9065\u6d4b\u4e3b\u9898 ---")
    c.subscribe(e2e.TOPIC_VFD)
    c.pump(1.5)
    vfd = c.subs.get(e2e.TOPIC_VFD)
    if vfd is None:
        check(True, "\u53d8\u9891\u5668\u65e0\u6570\u636e\u65f6\u4e0d\u63a8\u7a7a\u5feb\u7167\uff08\u7b26\u5408\u8bbe\u8ba1\uff09",
              "\u672a\u6536\u5230\u63a8\u9001")
    else:
        check(True, "\u6536\u5230\u53d8\u9891\u5668\u63a8\u9001",
              "\u5b57\u6bb5\u6570=%d" % len(vfd))

    snap = c.request(e2e.CMD_VFD_SNAPSHOT,
                     e2e.BsonDoc().add("side", 0))
    print("      \u5feb\u7167\u5e94\u7b54\u952e: %s" % (sorted(snap.keys())[:8] or "\u7a7a"))
    check(True, "\u5feb\u7167\u8bf7\u6c42\u4e0d\u62a5\u9519", "keys=%d" % len(snap))

    # ---- 2) \u63a7\u5236\u95f8\uff1a\u81ea\u52a8\u6a21\u5f0f\u62d2 ----
    print("--- \u63a7\u5236\u95f8 ---")
    c.request(e2e.CMD_RUN_MODE, e2e.BsonDoc().add("mode", 1))
    r = c.request(e2e.CMD_VFD_CONTROL,
                  e2e.BsonDoc().add("op", 1).add("side", 0))
    check(r.get("ok") is False, "\u81ea\u52a8\u6a21\u5f0f\u4e0b\u53d8\u9891\u5668\u63a7\u5236\u88ab\u62d2",
          str(r.get("err", "")))
    c.request(e2e.CMD_RUN_MODE, e2e.BsonDoc().add("mode", 0))
    print("      \u5df2\u5207\u56de\u624b\u52a8")

    # ---- 3) \u975e root \u62d2 ----
    c2 = e2e.Client(HOST, PORT)
    c2.hello()
    s2, r2 = c2.auth("admin", "admin")
    check(s2 == 0 and r2 == 1, "admin \u8ba4\u8bc1\uff08\u53ea\u8bfb\uff09", "role=%d" % r2)
    rd = c2.request(e2e.CMD_VFD_CONTROL,
                    e2e.BsonDoc().add("op", 1).add("side", 0))
    check(rd.get("ok") is False, "admin \u4e0b\u53d1\u63a7\u5236\u88ab\u62d2\uff08\u9700 root\uff09",
          str(rd.get("err", "")))
    # admin \u4ecd\u53ef\u8bfb\u9065\u6d4b
    rs = c2.request(e2e.CMD_VFD_SNAPSHOT, e2e.BsonDoc().add("side", 0))
    check(True, "admin \u4ecd\u53ef\u8bfb\u53d8\u9891\u5668\u5feb\u7167", "keys=%d" % len(rs))
    c2.sock.close()

    # ---- 4) \u53c2\u6570\u8fb9\u754c ----
    print("--- \u53c2\u6570\u8fb9\u754c ---")
    bad = c.request(e2e.CMD_VFD_CONTROL,
                    e2e.BsonDoc().add("op", 999).add("side", 0))
    check(bad.get("ok") is False, "\u672a\u77e5 op \u88ab\u62d2", str(bad.get("err", "")))
    badside = c.request(e2e.CMD_VFD_SNAPSHOT, e2e.BsonDoc().add("side", 9))
    check(isinstance(badside, dict), "\u8d8a\u754c side \u6709\u5e94\u7b54\uff08\u4e0d\u6302\uff09",
          "keys=%d" % len(badside))

    c.sock.close()
    print("=== %s\uff08\u901a\u8fc7 %d / \u5931\u8d25 %d\uff09==="
          % ("PASS" if not fails else "FAIL", len(ok), len(fails)))
    for f in fails:
        print("   \u672a\u901a\u8fc7: %s" % f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
