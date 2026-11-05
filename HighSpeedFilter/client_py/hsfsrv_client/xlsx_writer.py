# -*- coding: utf-8 -*-
"""Excel 导出：加载 xlsx_bridge 动态库（C 端 XExcel 框架的 ctypes 封装）。

[为什么走 C 动态库而不是 Python 库] 板端没有（也装不了）openpyxl；
XExcel 是本仓库现成的 C 框架（XCore/XData/XExcel），编译成动态库后
ctypes 直接调，桌面/板端同一套代码。桥接层见
client_py/tools/xlsx_bridge/xlsx_bridge.c（目标在 HighSpeedFilter/
CMakeLists.txt，编完自动拷到 hsfsrv_client/bin/）。

[行列基约] 桥接层透传 XExcel 原生 **1 基**；本模块对 Python 调用方
暴露 **0 基**（内部 +1 换算），调用方不必关心 C 侧约定。

[动态库缺失] available() 为 False 时 write_rows 抛 XlsxUnavailable，
调用方自行降级（历史页此时仍可导出 CSV——CSV 是纯标准库路径）。
"""

import ctypes
import os

_DLL = None
_DLL_TRIED = False


class XlsxUnavailable(RuntimeError):
    """xlsx_bridge 动态库未编译/未找到。"""


def _dll_dir():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin")


def _dll_name():
    return "xlsx_bridge.dll" if os.name == "nt" else "xlsx_bridge.so"


def _load():
    """懒加载并绑定函数签名（进程内一次；失败后不再重试）。"""
    global _DLL, _DLL_TRIED
    if _DLL is not None:
        return _DLL
    if _DLL_TRIED:
        raise XlsxUnavailable("动态库不可用")
    _DLL_TRIED = True
    path = os.path.join(_dll_dir(), _dll_name())
    if not os.path.isfile(path):
        raise XlsxUnavailable("未找到 %s" % path)
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
            ("xlsx_set_col_width",
             [ctypes.c_void_p, ctypes.c_int, ctypes.c_double], ctypes.c_int),
            ("xlsx_save", [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int)):
        getattr(dll, func).argtypes = args
        getattr(dll, func).restype = res
    _DLL = dll
    return _DLL


def available():
    """动态库是否就绪（界面可据此启用/禁用 Excel 导出按钮）。"""
    if _DLL is not None:
        return True
    return os.path.isfile(os.path.join(_dll_dir(), _dll_name()))


def write_rows(path, sheet_name, headers, rows, col_widths=None):
    """写一张工作表。

    :param path: 目标 .xlsx 文件路径
    :param sheet_name: 工作表名
    :param headers: 表头（字符串列表）
    :param rows: 数据行；每格 str/float/int/None（None 写空）
    :param col_widths: {列号0基: 宽度}（可选，Excel 字符宽口径）
    :raises XlsxUnavailable: 动态库缺失
    :raises RuntimeError: 任一写入/保存失败（C 侧返回否）
    """
    dll = _load()
    doc = dll.xlsx_create()
    if not doc:
        raise RuntimeError("xlsx_create 失败")
    try:
        if not dll.xlsx_add_sheet(doc, str(sheet_name).encode("utf-8")):
            raise RuntimeError("添加工作表失败：%s" % sheet_name)
        for col, text in enumerate(headers):
            if not dll.xlsx_write_str(doc, col + 1, col + 1,
                                      str(text).encode("utf-8")):
                raise RuntimeError("写表头第 %d 列失败" % (col + 1))
        for r, row in enumerate(rows):
            for c, value in enumerate(row):
                # 桥接层是 1 基（透传 XExcel 原生口径），这里 +1。
                row_c, col_c = r + 2, c + 1
                if value is None or value == "":
                    continue                     # 空格跳过（视觉即空单元）
                if isinstance(value, (int, float)) and not isinstance(value,
                                                                      bool):
                    if not dll.xlsx_write_num(doc, row_c, col_c,
                                              float(value)):
                        raise RuntimeError("写数值(%d,%d)失败"
                                           % (row_c, col_c))
                else:
                    if not dll.xlsx_write_str(doc, row_c, col_c,
                                              str(value).encode("utf-8")):
                        raise RuntimeError("写文本(%d,%d)失败"
                                           % (row_c, col_c))
        for col, width in (col_widths or {}).items():
            dll.xlsx_set_col_width(doc, int(col) + 1, float(width))
        if not dll.xlsx_save(doc, str(path).encode("utf-8")):
            raise RuntimeError("保存失败：%s" % path)
        return True
    finally:
        dll.xlsx_delete(doc)
