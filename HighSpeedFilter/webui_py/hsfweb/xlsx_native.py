# -*- coding: utf-8 -*-
"""网页端 XLSX 导出：**C 动态库路径**（与桌面端同一个 xlsx_bridge.dll）。

[为什么要有这条路径] 网页端原本只有纯 Python 的 `export.build_xlsx`
（zipfile + SpreadsheetML 最小集，零依赖）。本仓库框架里已经有成熟的 C 端
XExcel（`Src/XCore/XData/XExcel`），桌面端已把它编成 `xlsx_bridge.dll` 并
用 ctypes 调（见 `client_py/hsfsrv_client/xlsx_writer.py`）。网页端按
**同一个 DLL** 走 ctypes：产物更规整（真实 OOXML，含 sharedStrings 等），
且与桌面端同一条代码路径。

[回落口径] 动态库缺失（板端 / 未编译 / 纯前端部署）⇒ `available()` 为
False，调用方**回落到纯 Python 路径**（`export.build_xlsx`）。两条路都
必须能产出合法 OOXML —— 网页端不能因为"没 DLL"就导不出 Excel。
**显式关闭**：`HSF_XLSX_NATIVE=0` ⇒ `available()` 恒 False（板端部署
或复现纯 Python 路径时用；网页端随即静默走纯 Python，不报 5xx）。

[与桌面端的差异（有意保留）] C 侧 `xlsx_create()` 自带一张缺省空表
"Sheet1"，`xlsx_add_sheet` 只追加（不删缺省表）⇒ C 路径产物里有一张空的
"Sheet1" + 业务表；纯 Python 路径只有业务表。桌面端回归 §32 已把这条
口径写死（`tests/test_hsf_regression.py` 的 §32 X 组），两端保持一致。

[save 成本不是常数 ⇒ 必须实测标定]（2026-11-13 网页端实测）
C 路径的**写入**很快（3.2 万格 0.10 s），但 `xlsx_save` 的成本随**格数
平方**增长、且随字符串变长更陡：

    格数        字符串      save 耗时
    2 000      1 字符      0.07 s
    4 000      1 字符      0.25 s
    8 000      1 字符      0.95 s
    16 000     1 字符      4.12 s
    32 000     1 字符      54.97 s
    4 000      30 字符     1.54 s

⇒ 2 032 行×16 列（3.2 万格）的一次历史导出，C 路径要 **1~2 分钟**
（网页端 HTTP 端到端实测 137 s，直接把浏览器/客户端拖到超时），
而纯 Python 路径同数据只要 **0.05 s**。

[因此：C 路径按"实测成本模型"择优] 本模块提供 `estimate_save_seconds()`
——用**真实样本行**做两档探针（32 行 / 64 行）标定 save 的成本模型
（`秒 ≈ t_fixed + k · 格数²`），进程内缓存；调用方（`export.write_xlsx`）
拿这个预测值与预算比较：**预测超预算就不用 C 路径**，改走纯 Python
（O(n)、0.05 s、同样是合法 OOXML）。缺库/探测失败/写入失败一律**静默
回落**，网页端不会因为"没有 DLL"或"C 路径太慢"而导不出 Excel。
"""

import ctypes
import os
import tempfile
import time

# [数值格口径 2026-11-19] 与纯 Python 路径（``export.build_xlsx``）**同一
# 份判定**：纯数字字面量的显示文本也写数值格 ⇒ 同一份导出，走 C 动态库
# 与走纯 Python 出来的列类型一样。
from .xlsx_stream import _number_text, _numeric_text

#: 动态库文件名（按平台）。
_DLL_NAME = "xlsx_bridge.dll" if os.name == "nt" else "xlsx_bridge.so"

#: 覆盖动态库路径的环境变量（部署到别处时用）。
ENV_LIB = "HSF_XLSX_BRIDGE"

#: **显式关掉 C 路径**的环境变量（板端无 .so、或要复现纯 Python 路径时用）：
#: 置 0/off/false/no ⇒ `available()` 返回 False ⇒ 网页端静默走纯 Python 路径。
ENV_DISABLE = "HSF_XLSX_NATIVE"

_DLL = None
_DLL_TRIED = False


class XlsxNativeError(RuntimeError):
    """C 侧 XLSX 写出失败（调用方应回落到纯 Python 路径）。"""


def _package_dir():
    return os.path.dirname(os.path.abspath(__file__))


def candidate_paths():
    """候选动态库路径（按优先级）。

    1) 环境变量 `HSF_XLSX_BRIDGE`；
    2) 网页端包旁边的 `hsfsrv_client/bin/`（同目录部署时的常规位置）；
    3) 仓库里桌面端的 `client_py/hsfsrv_client/bin/`（**同一个 DLL**）；
    4) 仓库根 `bin/`。
    """
    out = []
    env = os.environ.get(ENV_LIB, "").strip()
    if env:
        out.append(env)
    here = _package_dir()
    # 网页端包在 <repo>/HighSpeedFilter/webui_py/hsfweb ⇒ 上溯三层到仓库根。
    repo_root = os.path.abspath(os.path.join(here, os.pardir, os.pardir,
                                             os.pardir))
    highspeed = os.path.join(repo_root, "HighSpeedFilter")
    out.append(os.path.join(here, os.pardir, "hsfsrv_client", "bin",
                            _DLL_NAME))
    out.append(os.path.join(highspeed, "client_py", "hsfsrv_client", "bin",
                            _DLL_NAME))
    out.append(os.path.join(repo_root, "bin", _DLL_NAME))
    return out


def library_path():
    """第一个存在的动态库路径；都没有则返回首选路径（供报错文案用）。"""
    for path in candidate_paths():
        if os.path.isfile(path):
            return path
    return candidate_paths()[0]


def available():
    """C 路径是否可用（False ⇒ 调用方回落纯 Python）。

    [显式关闭] 环境变量 `HSF_XLSX_NATIVE` 置 0/off/false/no ⇒ 恒 False
    （板端无 .so、或要复现纯 Python 路径时用）。
    """
    flag = str(os.environ.get(ENV_DISABLE, "")).strip().lower()
    if flag in ("0", "off", "false", "no"):
        return False
    if _DLL is not None:
        return True
    return os.path.isfile(library_path())


def _load():
    """懒加载并绑定符号（进程内一次）。"""
    global _DLL, _DLL_TRIED
    if _DLL is not None:
        return _DLL
    if _DLL_TRIED:
        raise XlsxNativeError("xlsx_bridge 动态库不可用")
    _DLL_TRIED = True
    path = library_path()
    if not os.path.isfile(path):
        raise XlsxNativeError("未找到 xlsx_bridge 动态库：%s" % path)
    try:
        dll = ctypes.CDLL(path)
    except OSError as exc:
        raise XlsxNativeError("加载 %s 失败：%s" % (path, exc))
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
        fn = getattr(dll, func)
        fn.argtypes = args
        fn.restype = res
    _DLL = dll
    return _DLL


def _fill_doc(dll, doc, sheets):
    """把 sheets 写进已创建的工作簿句柄（C 路径与成本探针共用）。"""
    for name, headers, rows in sheets:
        if not dll.xlsx_add_sheet(doc, str(name).encode("utf-8")):
            raise XlsxNativeError("添加工作表失败：%s" % name)
        for col, text in enumerate(headers):
            if text is None or text == "":
                continue
            if not dll.xlsx_write_str(doc, 1, col + 1,
                                      str(text).encode("utf-8")):
                raise XlsxNativeError("写表头失败：%s 第 %d 列"
                                      % (name, col + 1))
        for r, row in enumerate(rows):
            for c, value in enumerate(row):
                if value is None or value == "":
                    continue
                num = _number_text(value)
                if num is None and isinstance(value, str):
                    num = _numeric_text(value)
                if num is not None:
                    if not dll.xlsx_write_num(doc, r + 2, c + 1,
                                              float(num)):
                        raise XlsxNativeError(
                            "写数值失败：%s (%d,%d)" % (name, r + 2, c + 1))
                    continue
                if isinstance(value, bool):
                    text = "TRUE" if value else "FALSE"
                else:
                    text = str(value)
                if not dll.xlsx_write_str(doc, r + 2, c + 1,
                                          text.encode("utf-8")):
                    raise XlsxNativeError(
                        "写文本失败：%s (%d,%d)" % (name, r + 2, c + 1))


def _build_to_file(sheets, path):
    """C 路径产出：写工作簿并保存到 path（不读回字节）。"""
    dll = _load()
    doc = dll.xlsx_create()
    if not doc:
        raise XlsxNativeError("xlsx_create 失败")
    try:
        _fill_doc(dll, doc, sheets)
        if not dll.xlsx_save(doc, str(path).encode("utf-8")):
            raise XlsxNativeError("保存失败：%s" % path)
    finally:
        dll.xlsx_delete(doc)


def build_xlsx(sheets):
    """用 C 库写一张工作簿并返回字节。

    :param sheets: ``[(表名, 表头, 数据行), ...]``（与纯 Python 路径同形状）。
    :return: XLSX 字节。
    :raises XlsxNativeError: 动态库缺失或任一步失败（调用方应回落）。
    """
    fd, tmp_path = tempfile.mkstemp(suffix=".xlsx")
    os.close(fd)
    os.unlink(tmp_path)               # XDocument 自己建文件
    try:
        _build_to_file(sheets, tmp_path)
        with open(tmp_path, "rb") as fh:
            return fh.read()
    finally:
        if os.path.exists(tmp_path):
            try:
                os.unlink(tmp_path)
            except OSError:
                pass


# ============================================================================
#                    save 成本模型（实测标定，进程内缓存）
# ============================================================================

#: 成本模型缓存：键 = (列数, 平均字符长度档)，值 = (k, t_fixed)。
_COST_MODEL = {}

#: 探针档位（数据行数）：两档相乘即格数翻倍 ⇒ 可解出二次项与固定项。
_PROBE_ROWS = (32, 64)


def _columns_of(sheets):
    """工作簿的列数（取第一张表的表头列数）。"""
    for _name, headers, _rows in sheets:
        return len(headers)
    return 0


def _cells_of(sheets):
    """工作簿的格子总数（表头行 + 数据行）。"""
    total = 0
    for _name, headers, rows in sheets:
        total += len(headers) + sum(len(r) for r in rows)
    return total


def _avg_len_bucket(sheets):
    """样本字符串的平均长度档（每 4 字符一档）——缓存键的一部分。"""
    chars = 0
    count = 0
    for _name, _headers, rows in sheets:
        for row in rows[:16]:
            for value in row:
                chars += len(str(value or ""))
                count += 1
    if not count:
        return 0
    return (chars // count) // 4


def _padded_rows(data, count):
    """取 count 行样本；不足则**重复**已有行（保证探针尺寸固定）。

    [为什么要补齐] 探针尺寸必须与数据规模无关，否则同一台机器上会算出
    两套不同的成本模型（短表探针便宜 ⇒ 模型偏乐观 ⇒ 大表误用 C 路径）。
    """
    if not data:
        return []
    out = []
    while len(out) < count:
        out.extend(data[:count - len(out)])
    return out[:count]


def _probe_save_seconds(sheets):
    """用**真实样本行**做两档探针，测 save 成本。

    :returns: ``[(格数, 秒数), ...]``（格数 = 探针行数 × 列数）。
    :raises XlsxNativeError: 动态库不可用或探针失败。
    """
    columns = _columns_of(sheets)
    points = []
    for rows in _PROBE_ROWS:
        sample = [(name, headers, _padded_rows(data, rows))
                  for (name, headers, data) in sheets]
        if not any(data for _n, _h, data in sample):
            continue
        fd, tmp_path = tempfile.mkstemp(suffix=".xlsx")
        os.close(fd)
        os.unlink(tmp_path)
        t0 = time.perf_counter()
        try:
            _build_to_file(sample, tmp_path)
        finally:
            if os.path.exists(tmp_path):
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass
        points.append((rows * columns, time.perf_counter() - t0))
    if not points:
        raise XlsxNativeError("save 成本探针取不到样本")
    return points


def _fit_cost_model(sheets):
    """两档探针 → 成本模型 (k, t_fixed)：秒 ≈ t_fixed + k · 格数²。

    [拟合口径] 用**边际成本**再折成二次项：save 的每格成本随格数增长
    （实测 16 字符样本：512 格 0.024 s → 1024 格 0.050 s → 32 528 格
    实测 122 s），二次模型比"两档直接解方程"更保守（不会低估 ⇒ 不会
    误用慢路径）。t_fixed 取负则归零（把固定开销算进二次项更保守）。
    """
    points = _probe_save_seconds(sheets)
    (c1, t1) = points[0]
    (c2, t2) = points[-1]
    if c2 > c1:
        marginal = (t2 - t1) / float(c2 - c1)      # 秒/格
    else:
        marginal = t1 / float(c1) if c1 else 0.0
    k = marginal / float(c1) if c1 else 0.0        # 秒/格²
    t_fixed = max(0.0, t1 - k * float(c1 * c1))
    return (k, t_fixed)


def estimate_save_seconds(sheets):
    """预测 C 路径保存这张工作簿要花多少秒（**实测标定**，进程内缓存）。

    :raises XlsxNativeError: 动态库不可用 / 探针失败（调用方回落纯 Python）。
    """
    if not available():
        raise XlsxNativeError("未找到 xlsx_bridge 动态库")
    columns = _columns_of(sheets)
    key = (columns, _avg_len_bucket(sheets))
    model = _COST_MODEL.get(key)
    if model is None:
        model = _fit_cost_model(sheets)
        _COST_MODEL[key] = model
    k, t_fixed = model
    cells = _cells_of(sheets)
    return t_fixed + k * float(cells) ** 2
