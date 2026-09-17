#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XGui ↔ Qt 6.8.3 全模块 API 复扫脚本（Phase 3.1 v2）。

用途
----
对 qtbase/src/widgets 范围内、且在 Src/XGui 下存在 C 适配层的 Qt 类，
逐类解析其"本类自有"公开 API 面（public 成员函数、Q_SIGNALS 区的
void 信号名、public Q_SLOTS、Q_PROPERTY 的 READ/WRITE 访问器），与
XGui 侧 `X<类>_<Qt方法驼峰名>`（含全部祖先类的函数，_signal/_base
后缀归一）做匹配，输出剩余缺口清单：

    docs/xgui-audit/2026-09-16/xgui-api-gaps-phase3-v2.txt

匹配规则：Qt 方法 foo 被满足，当且仅当 XGui 侧（本类 + 祖先链）存在
foo、foo_2 或 foo_3（QString 双版本约定：主版本返回 XString*，
`_2` 后缀版本用 const char*）；或命中 RENAMED 显式改名表；
或命中 SKIP 豁免清单（每条豁免注明理由）。

与已丢失的 tools/xgui_api_scan.sh 相比修复的三个致命缺陷：
1. 旧脚本提取的方法名首字母被截断（如 "abstractButton" 变成
   "bstractButton"）；本脚本按标识符边界提取方法名，并在自检阶段
   验证每个 MISS 名都能在对应 Qt 头文件中 grep 到 `name(` 或
   `name;`。
2. 旧脚本未做继承归并，把 QComboBox::sizeHint 这类由祖先类
   （XWidget_sizeHint）满足的方法误报为缺口；本脚本从 Src/XGui
   头文件的 XCLASS_DEFINE_EXTEND_END(Class, Parent) /
   XCLASS_DEFINE_ENUM(Class, X) = XCLASS_VTABLE_GET_SIZE(Parent)
   宏解析 XGui 继承链，祖先类方法同样计入满足集。
3. 旧脚本带 UTF-8 BOM 导致 shebang 失效；本文件为无 BOM UTF-8，
   python3 可直接运行。

用法
----
    python3 tools/xgui_api_scan.py            # 扫描 + 产出清单 + stdout 汇总
    python3 tools/xgui_api_scan.py --check    # 仅运行自检（首字母截断校验）
"""

import os
import re
import sys
from collections import OrderedDict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QT_SRC = os.environ.get("QT_SRC", "/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets")
XGUI_SRC = os.path.join(REPO, "Src", "XGui")
OUT_PATH = os.path.join(REPO, "docs", "xgui-audit", "2026-09-16",
                        "xgui-api-gaps-phase3-v2.txt")

# ============================================================
# 显式改名对照表（Qt API 名 → XGui 侧名字，因与旧 C API 冲突而改名）。
# 每条注明依据（核实方式：对照 Src/XGui 对应头文件的声明与注释）。
# ============================================================
RENAMED = {
    # 无 —— 截至本次复扫未发现需要登记的改名项；如后续出现，
    # 以 "Qt类.方法名" 为键，值为 XGui 侧方法名，并注明依据。
    # 示例（勿删格式）：
    # "QComboBox.setView": {"to": "setPopupView",
    #   "why": "XComboBox.h 注释：setView 与内部 m_view 字段访问器冲突"},
}

# ============================================================
# 豁免清单 SKIP：key 为 "Qt类.方法名"（类名精确匹配）或 "Qt类.*"
# 或 "*方法名"（全局按方法名豁免）。value 为豁免理由，必须注明。
# ============================================================
SKIP = {
    # --- macOS/ Cocoa 平台私有 API，XGui 明确不做（C 应用层直接用 X11/自有窗口） ---
    "QMenu.toNSMenu": "macOS Cocoa 专属桥接（NSMenu*），XGui 面向 Linux/X11，设计上不做",
    "QMenuBar.toNSMenu": "macOS Cocoa 专属桥接（NSMenu*），XGui 面向 Linux/X11，设计上不做",
    "QMenuBar.toNSMenuOf": "macOS Cocoa 专属桥接，同 toNSMenu，XGui 不做",
    # --- Qt 内部/实现细节，非应用层 API ---
    "*.qt_findObjChild": "QObject 内部辅助（qobject.h 模板内联），非应用层 API",
    # --- Phase 3.1 分类处置（2026-09-17）：设计边界豁免，逐条注明理由 ---
    "QWidget.setupUi": "uic 构建期生成器产物，非运行时 API，XGui 不做",
    "QWidget.createWindowContainer": "QWindow/OpenGL 外来窗口容器体系不在 XGui 范围",
    "QWidget.graphicsProxyWidget": "QGraphicsView/GraphicsProxyWidget 体系未建，不在对齐范围",
    "QWidget.hasEditFocus": "Qtopia 时代嵌入式编辑焦点 API，XGui 不做",
    "QWidget.setEditFocus": "同 hasEditFocus，XGui 不做",
    "QWidget.grabGesture": "手势识别体系不在范围（嵌入式无触摸手势需求）",
    "QWidget.ungrabGesture": "同 grabGesture，XGui 不做",
    "QWidget.grabShortcut": "快捷键以 XShortcut 对象承载（Task 2.19），不建 Qt 的快捷键 id 注册表",
    "QWidget.releaseShortcut": "同 grabShortcut，XGui 不做",
    "QWidget.setShortcutEnabled": "同 grabShortcut，XGui 不做",
    "QWidget.setShortcutAutoRepeat": "同 grabShortcut，XGui 不做",
    "QWidget.find": "静态 WId→QWidget 查找注册表不做；XGui 无原生窗口句柄注册表",
    "QApplication.navigationMode": "Qt 已废弃的键盘导航模式 API（自 Qt 5.10 弃用）",
    "QApplication.setNavigationMode": "同 navigationMode，Qt 已废弃",
    "QPlainTextEdit.print": "打印子系统不在 XGui 范围（XTextEdit.print 为历史遗留独立实现，不新增）",
    "QLineEdit.timerEvent": "QObject 事件分发内部钩子，非应用层 API 面",
    "QAbstractScrollArea.setupViewport": "Qt protected 视口初始化钩子，XGui 滚动区不设该扩展点",
    "QScrollArea.focusNextPrevChild": "Qt protected 焦点遍历虚函数，XGui 焦点链按自有模型处理",
    "QCommandLinkButton.initStyleOption": "样式选项结构体（XStyleOptionCommandLinkButton）未建，属内部绘制辅助",
    "QDateTimeEdit.calendar": "QCalendar 备选历法对象体系不做（XGui 纯公历，11b 偏差）",
    "QDateTimeEdit.setCalendar": "同 calendar，QCalendar 体系不做",
    "QCalendarWidget.calendar": "QCalendar 备选历法体系不做（XGui 纯公历，11b 偏差）",
    "QCalendarWidget.setCalendar": "同 calendar，QCalendar 体系不做",
    "QCalendarWidget.dateTextFormat": "按日期的 QTextCharFormat 映射未建模（富文本格式子集边界）",
    "QCalendarWidget.setDateTextFormat": "同 dateTextFormat",
    "QFileDialog.currentUrlChanged": "URL 承载族不做（XFileDialog 以本地路径承载，11b 偏差）",
    "QFileDialog.directoryUrlEntered": "URL 承载族不做",
    "QFileDialog.getExistingDirectoryUrl": "URL 承载族不做",
    "QFileDialog.getOpenFileContent": "远程内容获取（依赖 QNetworkAccessManager）不做",
    "QFileDialog.getOpenFileUrl": "URL 承载族不做",
    "QFileDialog.getOpenFileUrls": "URL 承载族不做",
    "QFileDialog.getSaveFileUrl": "URL 承载族不做",
    "QFileDialog.saveFileContent": "远程内容保存（依赖 QNetworkAccessManager）不做",
    "QFileDialog.urlSelected": "URL 承载族不做",
    "QFileDialog.urlsSelected": "URL 承载族不做",
    # --- Phase 3.2 P2 收口（2026-09-17）：QTimeZone 对象体系 ---
    "QDateTimeEdit.timeZone": "QTimeZone 对象体系未建（XGui 纯公历嵌入式边界）；时区规格由 setTimeSpec 承载，11b 偏差",
    "QDateTimeEdit.setTimeZone": "同 timeZone，QTimeZone 对象体系未建",
}

# ------------------------------------------------------------
# Qt 侧解析
# ------------------------------------------------------------

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
PREPROC_RE = re.compile(r"(?m)^#[^\n]*(?:\\\n[^\n]*)*\n")
QT_DEPRECATED_RE = re.compile(r"QT_DEPRECATED_VERSION[A-Za-z0-9_]*\s*(?:\([^()]*(?:\([^()]*\)[^()]*)*\))?")
Q_PROPERTY_RE = re.compile(r"Q_PROPERTY\s*\(", )
CLASS_DECL_RE = re.compile(
    r"\bclass\s+(?:Q_[A-Z_]+_EXPORT\s+|Q_?[A-Z_]*EXPORT[A-Z_]*\s+)?(Q[A-Z]\w*)\s*[^{;]*?\{")
WORD_BEFORE_PAREN_RE = re.compile(r"([A-Za-z_]\w*)\s*\(")

# 永不视为 API 的名字（关键字/宏/元对象污染产物/纯内部）
NON_API_NAMES = {
    "if", "for", "while", "switch", "return", "sizeof", "defined",
    "signals", "slots", "emit", "Q_OBJECT", "Q_GADGET", "Q_ENUM",
    "Q_ENUMS", "Q_FLAG", "Q_FLAGS", "Q_PROPERTY", "Q_SIGNALS",
    "Q_SLOTS", "Q_INVOKABLE", "Q_DECL_DEPRECATED_X", "Q_REQUIRED_RESULT",
    "QT_CONFIG", "QT_REQUIRE_VERSION", "QT_DEPRECATED_VERSION",
    "d_func", "q_func", "metaObject", "qt_metacall", "qt_metacast",
    "operator", "override", "class", "struct", "enum", "union",
    "template", "using", "friend", "typedef", "explicit", "static",
    "virtual", "inline", "const", "static_assert", "Q_DECLARE_PRIVATE",
    "Q_DECLARE_PUBLIC", "Q_DISABLE_COPY", "Q_DISABLE_COPY_MOVE",
    "private", "public", "protected",
}

ACCESS_LABELS = {
    "public": "public",
    "private": "private",
    "protected": "protected",
    "Q_SIGNALS": "signals",
    "signals": "signals",
    "Q_SLOTS": "slots",
    "slots": "slots",
    "public Q_SLOTS": "public_slots",
    "public slots": "public_slots",
    "private Q_SLOTS": "private_slots",
    "private slots": "private_slots",
    "protected Q_SLOTS": "protected_slots",
    "protected slots": "protected_slots",
}


def strip_comments_and_preproc(text):
    text = COMMENT_RE.sub("", text)
    text = PREPROC_RE.sub("", text)
    text = QT_DEPRECATED_RE.sub(" ", text)
    # Q_OBJECT/Q_GADGET/Q_ENUM/Q_FLAG 不带分号，会把后续 `public:` 标签
    # 吞进同一条语句导致访问区段丢失；替换成空语句补齐分号边界。
    text = re.sub(r"\bQ_OBJECT\b|\bQ_GADGET\b"
                  r"|\bQ_ENUMS?\s*\([^)]*\)|\bQ_FLAGS?\s*\([^)]*\)", ";", text)
    # Q_PROPERTY(...) 自身无分号：补成分号结尾的独立语句，
    # 访问器名随后在 parse_class_body 中单独提取。
    text = re.sub(r"\bQ_PROPERTY\s*\(([^)]*)\)\s*;?", r"Q_PROPERTY(\1);", text)
    return text


def find_matching_brace(text, open_idx):
    """返回与 text[open_idx]=='{' 配对的 '}' 下标。"""
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


def parse_class_body(body, classname):
    """解析 Qt 类体，返回 dict：{'methods': set, 'signals': set,
    'properties': set(READ/WRITE 访问器)}。

    用大括号配对跳过嵌套类/内联函数体/枚举体，按访问区段归类。
    """
    methods = set()
    signals = set()
    properties = set()
    access = "private"  # class 默认 private
    pending = []
    i, n = 0, len(body)

    def record(stmt):
        stmt = " ".join(stmt.split())
        if "(" not in stmt:
            return
        m = WORD_BEFORE_PAREN_RE.search(stmt)
        if not m:
            return
        name = m.group(1)
        if name in NON_API_NAMES:
            return
        if name == classname:  # 构造函数
            return
        if stmt.startswith("~"):  # 析构函数
            return
        if name.startswith("operator"):  # 运算符重载
            return
        if name.isupper():  # 全大写宏污染产物
            return
        if name.endswith("_p") or name.startswith("qt_"):
            return
        if access == "signals":
            if re.match(r"\bvoid\b", stmt):
                signals.add(name)
        elif access in ("public", "public_slots"):
            methods.add(name)

    while i < n:
        c = body[i]
        if c == "{":
            stmt = "".join(pending).strip()
            pending = []
            if re.match(r"(class|struct|enum|union)\b", stmt) or not stmt:
                # 嵌套类型定义：整体跳过（大括号配对）
                i = find_matching_brace(body, i) + 1
            else:
                # 内联方法定义：声明部分记账，函数体跳过
                record(stmt)
                i = find_matching_brace(body, i) + 1
            continue
        if c == ";":
            record("".join(pending))
            pending = []
            i += 1
            continue
        if c == ":":
            stmt = " ".join("".join(pending).split())
            if stmt in ACCESS_LABELS:
                access = ACCESS_LABELS[stmt]
                pending = []
                i += 1
                continue
            pending.append(c)
            i += 1
            continue
        if c == "}":
            # 类体结束
            break
        pending.append(c)
        i += 1

    # Q_PROPERTY 的 READ/WRITE 访问器
    for m in re.finditer(r"Q_PROPERTY\s*\(([^)]*)\)", body):
        args = m.group(1)
        rm = re.search(r"\bREAD\s+(\w+)", args)
        wm = re.search(r"\bWRITE\s+(\w+)", args)
        if rm:
            properties.add(rm.group(1))
        if wm:
            properties.add(wm.group(1))
    return {"methods": methods, "signals": signals, "properties": properties}


def index_qt_classes():
    """扫描 Qt widgets 头文件，返回 {类名: {'file': 路径, 'api': 解析结果}}。"""
    index = OrderedDict()
    for root, _dirs, files in os.walk(QT_SRC):
        for fn in sorted(files):
            if not fn.endswith(".h") or fn.endswith("_p.h") \
                    or fn.endswith("-private.h") or fn.endswith("_impl.h"):
                continue
            path = os.path.join(root, fn)
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    raw = f.read()
            except OSError:
                continue
            text = strip_comments_and_preproc(raw)
            for m in CLASS_DECL_RE.finditer(text):
                cls = m.group(1)
                open_idx = text.find("{", m.start())
                if open_idx == -1:
                    continue
                close_idx = find_matching_brace(text, open_idx)
                body = text[open_idx + 1:close_idx]
                api = parse_class_body(body, cls)
                if cls not in index:  # 首个（非前向）声明优先
                    index[cls] = {"file": path, "api": api}
    return index


# ------------------------------------------------------------
# XGui 侧解析
# ------------------------------------------------------------

def index_xgui_classes():
    """扫描 Src/XGui/**/*.h，返回 (classes, parents)。

    classes: {类名: set(方法名)}  —— 收集该类在全部 XGui 头文件中
             出现的 `X<类>_<名>(`（声明或宏），含 _Protected.h；
    parents: {类名: 父类名}。
    """
    call_re = re.compile(r"\b(X[A-Za-z0-9]+)_([A-Za-z0-9_]+)\s*\(")
    # 宏别名：`#define XApplication_exec XGuiApplication_exec` 这类把
    # 父类/兄弟类 API 以本类名暴露的对象宏，同样属于本类公开 API 面。
    macro_re = re.compile(r"(?m)^#\s*define\s+(X[A-Za-z0-9]+)_([A-Za-z0-9_]+)\s+(?!XGUI_|XGUI|X[A-Z]+_ON)")
    beging_re = re.compile(r"XCLASS_DEFINE_BEGING\s*\(\s*(\w+)\s*\)")
    extend_re = re.compile(
        r"XCLASS_DEFINE_EXTEND_END\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
    enum_base_re = re.compile(
        r"XCLASS_DEFINE_ENUM\s*\(\s*(\w+)\s*,\s*\w+\s*\)\s*=\s*"
        r"XCLASS_VTABLE_GET_SIZE\s*\(\s*(\w+)\s*\)")

    classes = {}
    parents = {}
    declared = set()
    for root, _dirs, files in os.walk(XGUI_SRC):
        for fn in sorted(files):
            if not fn.endswith(".h"):
                continue
            path = os.path.join(root, fn)
            with open(path, encoding="utf-8-sig", errors="replace") as f:
                text = f.read()
            for m in beging_re.finditer(text):
                declared.add(m.group(1))
            for m in extend_re.finditer(text):
                parents[m.group(1)] = m.group(2)
            for m in enum_base_re.finditer(text):
                cls = m.group(1)
                if cls not in parents:
                    parents[cls] = m.group(2)
            for m in call_re.finditer(text):
                cls, name = m.group(1), m.group(2)
                if cls in declared or cls in classes:
                    classes.setdefault(cls, set()).add(name)
            for m in macro_re.finditer(text):
                cls, name = m.group(1), m.group(2)
                if cls in declared or cls in classes:
                    classes.setdefault(cls, set()).add(name)
    return classes, parents


LIFECYCLE_NAMES = {
    # 系统自有生命周期/类工厂函数，不对应任何 Qt API
    "init", "create", "create_ex", "deinit", "deinit_impl",
    "deinit_base", "delete_base", "class_init",
}


def normalize_xgui_names(names):
    """XGui 方法名归一化：
    - 排除生命周期函数（_init/_create/_deinit_base/_delete_base/
      class_init 等系统自有函数，不对应 Qt API）；
    - `*_signal` 是 Qt 信号 foo 的回调登记接口，归一为 foo；
    - `*_base` 是虚函数槽派发入口（如 hidePopup_base 对应 Qt
      hidePopup 虚函数），归一为去掉 _base 的名字。
    """
    out = set()
    for name in names:
        if name in LIFECYCLE_NAMES:
            continue
        base = name
        if name.endswith("_signal"):
            base = name[:-len("_signal")]
        elif name.endswith("_base") and name not in (
                "deinit_base", "delete_base"):
            base = name[:-len("_base")]
        if base in LIFECYCLE_NAMES:
            continue
        out.add(base)
        out.add(name)  # 原名也保留（防过度归一）
    return out


def ancestor_chain(cls, parents):
    """返回 [cls, parent, grandparent, ...]，到无父类为止（防环）。"""
    chain = [cls]
    seen = {cls}
    cur = cls
    while cur in parents:
        cur = parents[cur]
        if cur in seen:
            break
        chain.append(cur)
        seen.add(cur)
    return chain


# ------------------------------------------------------------
# 映射：Qt 类 → XGui 类
# ------------------------------------------------------------

MANUAL_QT_TO_XGUI = {
    "QWidget": "XWidget",   # 特例：QWidget → XWidget（非简单 Q→X 替换）
}


def build_mapping(qt_index, xgui_classes):
    """返回 {Qt类名: XGui类名}，要求 XGui 类确实存在且 Qt 类已索引。"""
    mapping = {}
    for xcls in xgui_classes:
        base = xcls[1:]
        cands = ["Q" + base, "Q" + base[0].lower() + base[1:]]
        for cand in cands:
            if cand in qt_index:
                mapping[cand] = xcls
                break
    for q, x in MANUAL_QT_TO_XGUI.items():
        if q in qt_index and x in xgui_classes:
            mapping[q] = x
    return mapping


# ------------------------------------------------------------
# 匹配
# ------------------------------------------------------------

def method_satisfied(qt_name, xgui_names):
    """Qt 方法 foo 满足 ⇔ XGui 侧有 foo / foo_2 / foo_3。"""
    return qt_name in xgui_names or \
        (qt_name + "_2") in xgui_names or (qt_name + "_3") in xgui_names


def scan():
    qt_index = index_qt_classes()
    xgui_classes, parents = index_xgui_classes()
    xgui_norm = {cls: normalize_xgui_names(names)
                 for cls, names in xgui_classes.items()}
    mapping = build_mapping(qt_index, xgui_classes)

    results = []
    stats = {"miss": 0, "skip": 0, "ok": 0}
    for qcls in sorted(mapping):
        xcls = mapping[qcls]
        chain = ancestor_chain(xcls, parents)
        satisfied = set()
        for anc in chain:
            satisfied |= xgui_norm.get(anc, set())
        api = qt_index[qcls]["api"]
        surface = sorted(api["methods"] | api["signals"] | api["properties"])
        entry = {"qt": qcls, "xgui": xcls, "file": qt_index[qcls]["file"],
                 "miss": [], "skip": [], "ok": 0}
        for name in surface:
            renamed_to = RENAMED.get("%s.%s" % (qcls, name))
            if renamed_to:
                entry["ok"] += 1
                stats["ok"] += 1
                continue
            if method_satisfied(name, satisfied):
                entry["ok"] += 1
                stats["ok"] += 1
                continue
            skip_key = "%s.%s" % (qcls, name)
            if skip_key in SKIP:
                entry["skip"].append((name, SKIP[skip_key]))
                stats["skip"] += 1
                continue
            if "*%s" % name in SKIP:
                entry["skip"].append((name, SKIP["*" + name]))
                stats["skip"] += 1
                continue
            entry["miss"].append(name)
            stats["miss"] += 1
        results.append(entry)
    return results, stats, mapping, xgui_norm, parents, qt_index


# ------------------------------------------------------------
# 输出
# ------------------------------------------------------------

def write_report(results, stats, out_path):
    lines = []
    lines.append("XGui ↔ Qt 6.8.3 API 缺口清单（Phase 3.1 v2）")
    lines.append("生成脚本：tools/xgui_api_scan.py（python3 可直接运行，UTF-8 无 BOM）")
    lines.append("匹配规则：Qt 类自有公开 API（成员函数/Q_SIGNALS/public Q_SLOTS/"
                 "Q_PROPERTY READ/WRITE）")
    lines.append("满足条件：XGui 本类或祖先链存在同名（或 _2/_3 双版本），"
                 "或命中 RENAMED 改名表；豁免项见脚本 SKIP 表（含理由）。")
    lines.append("")
    for entry in results:
        if not entry["miss"]:
            continue
        lines.append("== %s → %s  (%s)" % (entry["qt"], entry["xgui"],
                                           os.path.basename(entry["file"])))
        for name in entry["miss"]:
            lines.append("MISS [%s] %s" % (entry["qt"], name))
        if entry["skip"]:
            for name, why in entry["skip"]:
                lines.append("SKIP [%s] %s  # %s" % (entry["qt"], name, why))
        lines.append("")
    lines.append("================ 统计 ================")
    lines.append("映射类数        : %d" % len(results))
    lines.append("总缺口数 (MISS) : %d" % stats["miss"])
    lines.append("豁免数   (SKIP) : %d" % stats["skip"])
    lines.append("满足数          : %d" % stats["ok"])
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def print_summary(results):
    by_cls = sorted(((e["qt"], len(e["miss"])) for e in results
                     if e["miss"]), key=lambda t: -t[1])
    print("每类缺口计数汇总（缺口 > 0 的类）：")
    for qcls, cnt in by_cls:
        print("  %-28s %d" % (qcls, cnt))
    total = sum(cnt for _q, cnt in by_cls)
    print("合计缺口：%d（涉及 %d 类 / 映射类 %d）"
          % (total, len(by_cls), len(results)))


# ------------------------------------------------------------
# 自检
# ------------------------------------------------------------

def check_no_truncation(results, qt_index):
    """自检：每个 MISS 方法名必须能在对应 Qt 头文件原文中找到
    `name(` 或 `name;`（防首字母截断/幻觉名）。返回失败列表。"""
    bad = []
    for entry in results:
        if not entry["miss"]:
            continue
        path = entry["file"]
        with open(path, encoding="utf-8", errors="replace") as f:
            raw = f.read()
        for name in entry["miss"]:
            if (re.search(r"\b%s\s*\(" % re.escape(name), raw) is None and
                    re.search(r"\b%s\s*;" % re.escape(name), raw) is None):
                bad.append((entry["qt"], name, path))
    return bad


def main():
    argv = sys.argv[1:]
    results, stats, mapping, _xnorm, _parents, qt_index = scan()
    out_path = OUT_PATH
    write_report(results, stats, out_path)
    print_summary(results)
    print("已写出：%s" % out_path)
    bad = check_no_truncation(results, qt_index)
    if bad:
        print("自检失败：以下 MISS 名在 Qt 头文件中 grep 不到（疑似截断/幻觉）：")
        for q, name, p in bad:
            print("  %s.%s  (%s)" % (q, name, p))
        sys.exit(2)
    print("自检通过：全部 MISS 名均可在对应 Qt 头文件 grep 到 `name(` 或 `name;`；"
          "无首字母截断。")


if __name__ == "__main__":
    main()
