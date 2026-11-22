# -*- coding: utf-8 -*-
"""元门禁：客户端 tests/ 下的 test_*.py 必须注册进 run_all.py（防孤儿腐化）。

    python tests/test_no_orphans.py

[为什么要这一道] 服务端 2026-11-22 实测过：5 个孤儿里 2 个已随实现演进烂掉
（还断言早已废弃的拒绝码/设备名）。客户端同样有先例（test_store_limit 之类）。
「不进门禁的锁等于没有锁」——这里把差集变成每次 run_all 都会跑的断言。

[提取口径] run_all.py 有两种注册形态：
    ① run([py, "tests/test_xxx.py", ...])        —— 直接字面量
    ② cmd = [py, "tests/test_xxx.py"]; run(cmd)  —— 先赋值
⇒ 扫**所有**「首元素是 Name、次元素是 tests/test_*.py 字面量」的 List/Tuple，
这样既能覆盖 ②，又不会把 docstring/注释里的路径误算成"已注册"。
"""
import ast
import glob
import os
import re
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))

#: 已知的「不进门禁」脚本（每条都要写理由，防止变成垃圾桶）。
ALLOW = {
    # 由 verify/hsf_leak_gate.py 间接调用（客户端泄漏门的一环），不重复注册。
    "test_stress_reconnect.py",
    # 手工验收脚本：38 步变频器组合链路，**不自起服务端**、固定连 6200。
    # 直接进 run_all 会因"6200 没在跑"恒红（不是产品缺陷）⇒ 保持手工执行。
    "test_vfd_combo.py",
}


def _registered(run_all_path):
    with open(run_all_path, "r", encoding="utf-8") as fh:
        tree = ast.parse(fh.read(), filename=run_all_path)
    found = set()
    for node in ast.walk(tree):
        if not isinstance(node, (ast.List, ast.Tuple)):
            continue
        elts = node.elts
        if len(elts) < 2:
            continue
        if not isinstance(elts[0], ast.Name):
            continue
        second = elts[1]
        if isinstance(second, ast.Constant) and isinstance(second.value, str):
            m = re.fullmatch(r"tests/(test_[A-Za-z0-9_]+\.py)", second.value)
            if m:
                found.add(m.group(1))
    return found


def main():
    registered = _registered(os.path.join(HERE, "run_all.py"))
    all_tests = {os.path.basename(p)
                 for p in glob.glob(os.path.join(HERE, "test_*.py"))}
    orphans = sorted(all_tests - registered - ALLOW)
    print("元门禁（客户端）：test_*.py %d 个 / run_all 注册 %d 个 / 豁免 %d 个"
          % (len(all_tests), len(registered), len(ALLOW)))
    if orphans:
        print("[FAIL] 孤儿测试（未注册进 run_all，会静默腐化）:")
        for name in orphans:
            print("  - %s" % name)
        return 1
    print("[PASS] 无孤儿测试：所有 test_*.py 都进了 run_all 或白名单")
    return 0


if __name__ == "__main__":
    sys.exit(main())
