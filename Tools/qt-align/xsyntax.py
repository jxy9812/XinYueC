#!/usr/bin/env python3
"""只读语法自检：用 build/compile_commands.json 的同一套编译参数跑 -fsyntax-only。

用途（Qt 6.8 对齐工作流）：并行开发时禁止各自调用 cmake/ninja（共享 build 目录会
互相踩踏），单文件语法自检走本脚本即可。脚本只读编译数据库、不修改任何源码。

用法：
    python3 -I Tools/qt-align/xsyntax.py Src/XGui/Widget/XLabel.c
    python3 -I Tools/qt-align/xsyntax.py Test/QtAlign/XLabelAlignTest.c
"""
import json
import os
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DB = os.path.join(ROOT, "build", "compile_commands.json")

# 按目标路径选择一条“同编译域”的参考命令（新增文件不在数据库里）。
REFS = (
    ("Test/", "Test/XGuiTest/XWidgetApiTest.c"),
    ("Src/XGui/", "Src/XGui/Widget/XWidget.c"),
    ("Src/XCore/XClass/", "Src/XCore/XClass/XObject.c"),
    ("Src/XCore/XIO/", "Src/XCore/XIO/XIODevice/XIODevice.c"),
)
DEFAULT_REF = "Src/XGui/Widget/XWidget.c"


def pick_ref(target_rel):
    for prefix, ref in REFS:
        if target_rel.startswith(prefix):
            return ref
    return DEFAULT_REF


def load_entry(ref):
    with open(DB, "r", encoding="utf-8") as f:
        data = json.load(f)
    for entry in data:
        if entry.get("file", "").replace("\\", "/").endswith(ref):
            return entry
    raise SystemExit("参考编译项不存在：%s" % ref)


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    target = os.path.abspath(argv[1])
    if not os.path.isfile(target):
        raise SystemExit("文件不存在：%s" % argv[1])
    target_rel = os.path.relpath(target, ROOT).replace(os.sep, "/")
    entry = load_entry(pick_ref(target_rel))
    args = shlex.split(entry["command"])
    # 去掉 -o <obj> 与 -c <src>，换成 -fsyntax-only <target>
    cleaned = []
    skip = False
    for i, a in enumerate(args):
        if skip:
            skip = False
            continue
        if a in ("-o", "-c"):
            skip = True
            continue
        if a.endswith((".o", ".obj")) or a.endswith((".c", ".cpp")):
            continue
        cleaned.append(a)
    cleaned.extend(["-fsyntax-only", target])
    proc = subprocess.run(
        cleaned,
        cwd=entry.get("directory", ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
