# -*- coding: utf-8 -*-
"""xlsx_bridge（Excel 导出动态库）内存泄漏测试。

用法：
    python tests/test_xlsx_leak.py

[测什么] 重复执行与历史页导出**完全相同**的 C 调用序列
（create → add_sheet → 写表头+N 行 → save → delete），用框架自身的
全局内存统计（XMemory_statistics_2 直读堆存量）对比前后的实时字节数：

    * 循环后堆存量回到基线        ⇒ 无泄漏
    * 循环后堆存量单调上涨        ⇒ 有泄漏（哪路 delete 没走全）

[跳过条件] hsfsrv_client/bin/ 下没有 xlsx_bridge 动态库（未编译）时
SKIP —— 泄漏测试依赖动态库，库没编出来谈不上泄漏。
"""

import ctypes
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

BIN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "hsfsrv_client", "bin")

#: 每轮写多少行数据（覆盖 str/num 混合写入路径即可，行数不必多）。
ROWS_PER_ROUND = 30
#: 完整导出循环轮数（每轮一个完整工作簿生命周期）。
ROUNDS = 50


def _dll_path():
    if sys.platform == "win32":
        return os.path.join(BIN_DIR, "xlsx_bridge.dll")
    return os.path.join(BIN_DIR, "xlsx_bridge.so")


def _load(path):
    dll = ctypes.CDLL(path)
    for func, args, res in (
            ("xlsx_create", [], ctypes.c_void_p),
            ("xlsx_delete", [ctypes.c_void_p], None),
            ("xlsx_add_sheet", [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            ("xlsx_write_str",
             [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p],
             ctypes.c_int),
            ("xlsx_write_num",
             [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_double],
             ctypes.c_int),
            ("xlsx_save", [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            ("xlsx_mem_bytes", [], ctypes.c_longlong)):
        getattr(dll, func).argtypes = args
        getattr(dll, func).restype = res
    return dll


def _export_round(dll, out_path):
    """与历史页 Excel 导出同构的完整调用序列（行列 1 基，桥接口径）。"""
    doc = dll.xlsx_create()
    assert doc, "xlsx_create 失败"
    try:
        assert dll.xlsx_add_sheet(doc, "历史数据".encode("utf-8")), "加表失败"
        for col in range(1, 8):
            assert dll.xlsx_write_str(
                doc, 1, col, ("列%d" % col).encode("utf-8")), "写表头失败"
        for row in range(2, ROWS_PER_ROUND + 2):
            for col in range(1, 8):
                if col % 2:
                    ok = dll.xlsx_write_str(
                        doc, row, col, ("文本%d-%d" % (row, col))
                        .encode("utf-8"))
                else:
                    ok = dll.xlsx_write_num(doc, row, col, row * col * 0.5)
                assert ok, "写单元格(%d,%d)失败" % (row, col)
        assert dll.xlsx_save(doc, out_path.encode("utf-8")), "保存失败"
    finally:
        dll.xlsx_delete(doc)


def main():
    path = _dll_path()
    if not os.path.isfile(path):
        print("SKIP：未找到 %s（先编译 xlsx_bridge 目标）" % path)
        return 0
    dll = _load(path)

    fd, out = tempfile.mkstemp(suffix=".xlsx")
    os.close(fd)
    os.unlink(out)

    try:
        # 先做一轮「预热」：框架/运行库的首轮分配（初始化、编码表、
        # 临时缓冲）不记基线，从第二轮起比才能分清「一次性初始化」与「泄漏」。
        _export_round(dll, out)
        before = dll.xlsx_mem_bytes()
        print("预热完成，堆存量基线 = %d 字节" % before)

        for i in range(ROUNDS):
            _export_round(dll, out)
        after = dll.xlsx_mem_bytes()
        print("%d 轮完整导出后，堆存量 = %d 字节（增长 %d）"
              % (ROUNDS, after, after - before))

        if after != before:
            print("FAIL：堆存量未归还，存在泄漏")
            return 1
        print("PASS：无内存泄漏")
        return 0
    finally:
        if os.path.exists(out):
            os.unlink(out)


if __name__ == "__main__":
    sys.exit(main())
