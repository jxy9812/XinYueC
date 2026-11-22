# -*- coding: utf-8 -*-
"""一键跑客户端全部测试（总回归 + 设置合并专项）。

    python tests/run_all.py                # 全跑（总回归含服务端段）
    python tests/run_all.py --no-srv       # 只跑不依赖服务端的段
    python tests/run_all.py --settings     # 只跑「设置三页合并」专项

[为什么有这个小入口] 客户端的回归锁散在两处：
  · `tests/test_hsf_regression.py` —— 总回归（单文件，自起私有服务端）；
  · `tests/test_settings_merge.py` —— 设置三页合并专项（2026-11-13）。
本文件把两处**串成一键**，避免"只跑了总回归、漏了专项"或反之。
（专项也已注册进总回归的 `_ui_groups()`，总回归单独跑时会带上它。）
"""

import argparse
import os
import subprocess
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
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(cmd, label, timeout=1800):
    print("\n" + "=" * 70)
    print("  " + label)
    print("=" * 70)
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, cwd=ROOT, timeout=timeout)
        ok = proc.returncode == 0
    except subprocess.TimeoutExpired:
        print("[TIMEOUT] %s" % label)
        ok = False
    print("  -> %s  (%.1fs)" % ("PASS" if ok else "FAIL", time.time() - t0))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-srv", action="store_true",
                    help="总回归只跑不依赖服务端的段")
    ap.add_argument("--settings", action="store_true",
                    help="只跑「设置三页合并」专项")
    args = ap.parse_args()

    py = sys.executable
    results = []

    if args.settings:
        results.append(("设置三页合并", run(
            [py, "tests/test_settings_merge.py"], "设置三页合并专项")))
        return _summary(results)

    results.append(("设置三页合并", run(
        [py, "tests/test_settings_merge.py"], "设置三页合并专项")))
    cmd = [py, "tests/test_hsf_regression.py"]
    if args.no_srv:
        cmd.append("--no-srv")
    results.append(("总回归", run(cmd, "总回归（test_hsf_regression.py）")))
    # [客户端泄漏门 2026-11-14] tracemalloc 堆级 × 3 场景（会话生命周期 /
    # 流式 XLSX 导出 / 内嵌网页端起停）× 3 轮。
    results.append(("客户端泄漏门", run(
        [py, "tests/test_leak_gate.py"],
        "客户端内存泄漏门（3 场景 × 3 轮 · 堆级）", timeout=900)))
    # [主题矩阵 2026-11-14] 5 套主题 × 19 个页面 × 三项检查（对比度/裁字/
    # 白板）⇒ 主题改了不会漏掉"某个页面在某个主题下看不清"。
    results.append(("主题矩阵", run(
        [py, "tests/test_theme_matrix.py"],
        "主题矩阵自检（5 主题 × 19 页 · 对比度/裁字/白板）", timeout=600)))
    # [触摸手势 2026-11-14] 拖动能滚 + 松手有惯性 + 点击不被误判成拖动。
    results.append(("触摸手势", run(
        [py, "tests/test_flick.py"],
        "触摸/拖动 + 惯性滚动自检", timeout=300)))
    # [XLSX 数值格 2026-11-19] 导出后数值列必须是**数值格**（Excel 里能
    # 求和/排序，不是左对齐的文本）——用户实报「数值列保存成 xlsx 后是
    # 文本型」。零依赖、无服务端，秒级。
    results.append(("XLSX数值格", run(
        [py, "tests/test_xlsx_numeric.py"],
        "XLSX 导出数值格自检（数值列/文本列/bool/空值/NaN·Inf/精度）",
        timeout=300)))
    # [2026-11-22 孤儿元门禁] 放最后：上面所有 test_*.py 都注册完了再扫差集。
    # 服务端同款门曾实测抓出 2 个已腐化的孤儿（断言早已废弃的口径）。
    results.append(("孤儿测试元门禁", run(
        [py, "tests/test_no_orphans.py"],
        "所有 test_*.py 都注册进 run_all（防静默腐化）")))
    return _summary(results)


def _summary(results):
    print("\n" + "=" * 70)
    print("  汇总")
    print("=" * 70)
    for label, ok in results:
        print("  [%s] %s" % ("PASS" if ok else "FAIL", label))
    failed = [label for label, ok in results if not ok]
    print("\n  %d/%d 组通过" % (len(results) - len(failed), len(results)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
