# -*- coding: utf-8 -*-
"""元门禁：tests/ 下的 test_*.py 必须注册进 run_all.py（防「孤儿测试」）。

    python tests/test_no_orphans.py

[为什么要这一道] 孤儿测试（从不在 run_all 注册）会随实现演进静默腐化：
2026-11-22 实测 5 个服务端孤儿里 2 个已红——login_policy 还断言登录拒绝码
为 BAD_USER(1)（产品 11-09 已改 REJECTED(4)）、dual_485 还断言设备名
「浊度仪%d」（产品已改「进水/出水浊度仪」）。它们红着无人知晓，因为
根本没人跑。仓库早有先例（test_store_limit.py 长期孤儿、口径烂掉后才被发现）。

[判定] ast 扫 run_all.py 的**字符串字面量**取已注册集合（它用字面量
"tests/test_xxx.py" 硬编码列组，无动态拼装），glob 取全部 test_*.py，
差集即孤儿；白名单只放**被间接调用**、文件仍在 tests/ 目录的测试。
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

#: 被间接调用、故不必（也不该重复）出现在 run_all 字面量注册表里的测试。
ALLOW = {
    # C 端服务端健康表命名——按用户口径「C 端不管」，不收编进 Python run_all。
    "test_c_health_naming.py",
    # test_stress_reconnect.py 由 verify/hsf_leak_gate.py 间接调用（客户端侧）；
    # 服务端目录若无同名文件则不影响，留着仅为跨端对称说明。
    "test_stress_reconnect.py",
}


def _registered(run_all_path):
    """只扫 `run([py, "tests/test_xxx.py", ...])` 的参数字面量。

    [2026-11-22 复审加固] 不能用 ast.walk 扫所有字符串常量：将来只要在
    run_all 的 docstring / 提示语里写一句 tests/test_foo.py，那个文件就会
    被误判"已注册"而假绿。限定在 Call(func=Name('run')) 的首个列表参数里。
    """
    with open(run_all_path, "r", encoding="utf-8") as fh:
        tree = ast.parse(fh.read(), filename=run_all_path)
    found = set()
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "run"
                and node.args):
            continue
        arg = node.args[0]
        if not isinstance(arg, (ast.List, ast.Tuple)):
            continue
        for elt in arg.elts:
            if isinstance(elt, ast.Constant) and isinstance(elt.value, str):
                m = re.fullmatch(r"tests/(test_[A-Za-z0-9_]+\.py)", elt.value)
                if m:
                    found.add(m.group(1))
    return found


def main():
    registered = _registered(os.path.join(HERE, "run_all.py"))
    all_tests = {os.path.basename(p)
                 for p in glob.glob(os.path.join(HERE, "test_*.py"))}
    orphans = sorted(all_tests - registered - ALLOW)
    print("元门禁：test_*.py %d 个 / run_all 注册 %d 个 / 豁免 %d 个"
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
