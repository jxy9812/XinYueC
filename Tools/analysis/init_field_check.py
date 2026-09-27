#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
init_field_check.py —— XGui「控件 init 未初始化全部结构体字段」静态检查工具。

背景
    2026-09-16 修复 XLineEdit.m_completer 野指针：结构体注释承诺
    「默认 NULL」，但 XLineEdit_init 从未给它赋值，堆残留垃圾导致
    解引用野指针（ASan malloc_fill_byte=0xBE 下必现）。本工具用于
    排查同类型的漏网：字段在结构体里声明了，但对应 `X<类>_init`
    函数体从未提及。

工作原理（纯字面量对比，python3 标准库，不依赖编译器）
    1. 递归扫描 Src/XGui 下所有 .h，提取每个 `typedef struct ...{...} 名字;`
       的字段列表（跳过基类成员 m_base / m_class；定长数组字段会标注）。
    2. 在同目录树的 .c 中查找 `<类名>_init` 函数定义，提取函数体，
       收集其中被提及的成员（`self->m_xxx = ...`、`XMemset(&self->m_xxx,...)`
       等任何出现均视为已初始化）；`XMemset(self, 0, sizeof(...))` 或
       `*self = (X类){0}` 视为全量初始化。
    3. 对比：结构体有、init 体从未提及的字段 → 漏网候选，按风险排序
       （指针字段未初始化 = 高）。
    4. 已知局限（按设计接受）：
       - init 调用辅助函数完成初始化（如 xtx_ensureBoolTable）时，
         本工具按字面量对比会漏报（false negative）——目标是抓
         「完全未提及的指针/布尔字段」，漏报没关系；
       - init 通过 memcpy 模板/静态默认对象赋值时同样漏报；
       - 不展开 #if 条件编译，字段与赋值按文本两侧同时纳入；
       - 回调函数指针字段若在 setter 侧有判空逻辑、字段注释标注
         「预留/保留」者，工具照常列出，需人工复核标注。

用法
    python3 tools/init_field_check.py                    # 默认扫描 Src/XGui，
                                                         # 报告写 .tmpdbg/init-check-report.md
    python3 tools/init_field_check.py --src-dir Src/XGui/Widget
    python3 tools/init_field_check.py --check XLineEdit.m_completer --check XWidget.m_visible
    python3 tools/init_field_check.py --output /tmp/r.md

    完整参数说明见 `python3 tools/init_field_check.py --help`。

退出码
    0 = 正常完成（无论是否发现漏网）；2 = 参数/路径错误。
    （发现漏网不代表编译失败，是否修复由人工复核决定，故不返回非零。）
"""

import argparse
import datetime
import os
import re
import sys

# ---------------------------------------------------------------------------
# 通用文本处理
# ---------------------------------------------------------------------------

DOC_COMMENT_RE = re.compile(r'/\*\*.*?\*/', re.S)   # 文档注释（含 /**< ... */）


def read_text(path):
    """读文件：兼容 UTF-8 BOM 与 CRLF，返回原文。"""
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as fh:
        return fh.read().replace('\r\n', '\n')


def blank_comments(text):
    """把所有注释替换成等长空白（保留换行），保证偏移量与原文一致。

    返回 (blanked_text, doc_comments)：
      doc_comments: [(start, end, 内文), ...] 仅为 /** ... */ 文档注释，
                    用于回填字段尾注（/**< ... */）。
    """
    doc_spans = [(m.start(), m.end(), m.group(0)) for m in DOC_COMMENT_RE.finditer(text)]
    out = []
    i, n = 0, len(text)
    # 顺序扫描：区分 字符串/字符字面量、行注释、块注释
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '\\' and i + 1 < n:
                    out.append(text[i + 1] if i + 1 < n else '')
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n - 2 if j < 0 else j
            seg = text[i:j + 2]
            # 保留换行，其余置空格 → 行列号与原文一致
            out.append(''.join(ch if ch == '\n' else ' ' for ch in seg))
            i = j + 2
            continue
        out.append(c)
        i += 1
    return ''.join(out), doc_spans


def match_brace(text, open_idx):
    """text[open_idx] 必须是 '{'，返回配对 '}' 的下标；找不到返回 -1。"""
    depth = 0
    for i in range(open_idx, len(text)):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
    return -1


def match_paren(text, open_idx):
    """text[open_idx] 必须是 '('，返回配对 ')' 的下标；找不到返回 -1。"""
    depth = 0
    for i in range(open_idx, len(text)):
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
    return -1


# ---------------------------------------------------------------------------
# 头文件解析：结构体与字段
# ---------------------------------------------------------------------------

# 函数指针成员：Ret (*m_name)(args)
FN_PTR_MEMBER_RE = re.compile(r'\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(')
# 位字段：名字 : 宽度（结尾）
BITFIELD_TAIL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*:\s*\d+\s*$')
# 数组后缀：[...]（可重复）
ARRAY_TAIL_RE = re.compile(r'\[[^\[\]]*\]\s*$')
# 末尾标识符（字段名）
NAME_TAIL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*$')
# 函数指针 typedef：typedef ... (*名字)(...)
FN_PTR_TYPEDEF_RE = re.compile(r'\btypedef\b[^;]*\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(')


class Field(object):
    """结构体字段记录。"""
    __slots__ = ('name', 'base_type', 'is_pointer', 'is_array',
                 'is_bitfield', 'is_bool', 'comment',
                 'cond_depth', 'cond_in_else')

    def __init__(self, name, base_type, is_pointer, is_array,
                 is_bitfield, is_bool, comment):
        self.name = name
        self.base_type = base_type
        self.is_pointer = is_pointer
        self.is_array = is_array
        self.is_bitfield = is_bitfield
        self.is_bool = is_bool
        self.comment = comment
        self.cond_depth = 0      # 声明所处的 #if 嵌套深度
        self.cond_in_else = False  # 声明位于某层 #if 的 #else 回退分支内


class StructDef(object):
    """结构体定义记录。"""
    __slots__ = ('name', 'header', 'fields', 'is_class_object',
                 'cond_depth', 'cond_in_else')

    def __init__(self, name, header, fields, is_class_object):
        self.name = name
        self.header = header
        self.fields = fields
        self.is_class_object = is_class_object  # 首成员为 m_base/m_class 的类对象
        self.cond_depth = 0
        self.cond_in_else = False


def split_top_level(chunk, sep):
    """按分隔符切分，跳过括号/方括号内出现的分隔符。"""
    parts, depth, cur = [], 0, []
    for ch in chunk:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append(''.join(cur))
    return parts


def parse_field_decl(raw_chunk, fnptr_typedefs):
    """把一个成员声明文本解析成 Field 列表；无法解析返回 []。

    支持普通指针/值字段、定长数组、位字段、内联函数指针成员，
    以及一行多声明（int a, b;）。
    """
    # 丢弃预处理行（#if/#define 等，可能因分号切分黏进声明块）
    lines = [ln for ln in raw_chunk.split('\n') if not ln.strip().startswith('#')]
    chunk = ' '.join(ln.strip() for ln in lines)
    chunk = chunk.strip().rstrip(';').strip()
    if not chunk or chunk.startswith('typedef'):
        return []
    if '(' in chunk and not FN_PTR_MEMBER_RE.search(chunk):
        # 含普通括号但不是函数指针成员的（老式写法等），放弃解析
        return []
    decls = [d.strip() for d in split_top_level(chunk, ',')]
    fields = []
    inherited_base = None
    for idx, decl in enumerate(decls):
        if not decl:
            continue
        base, name, is_ptr, is_arr, is_bit = inherited_base, None, False, False, False
        if idx == 0:
            m = FN_PTR_MEMBER_RE.search(decl)
            if m:
                # 函数指针成员：Ret (*m_name)(...)
                fields.append(Field(m.group(1), '<func ptr>', True, False, False, False, ''))
                inherited_base = None
                continue
            work = decl
            bm = BITFIELD_TAIL_RE.search(work)
            if bm:
                name, is_bit = bm.group(1), True
                work = work[:bm.start()]
            else:
                while True:
                    am = ARRAY_TAIL_RE.search(work)
                    if not am:
                        break
                    is_arr = True
                    work = work[:am.start()]
            nm = NAME_TAIL_RE.search(work)
            if not nm:
                continue
            name = nm.group(1)
            base = work[:nm.start()].strip()
            inherited_base = base
        else:
            # 后续声明符继承首声明符的基类型
            work = decl
            bm = BITFIELD_TAIL_RE.search(work)
            if bm:
                name, is_bit = bm.group(1), True
                work = work[:bm.start()]
            else:
                while True:
                    am = ARRAY_TAIL_RE.search(work)
                    if not am:
                        break
                    is_arr = True
                    work = work[:am.start()]
            nm = NAME_TAIL_RE.search(work.strip())
            if not nm:
                continue
            name = nm.group(1)
            lead = work[:nm.start()].strip()
            is_ptr = '*' in lead
            base = inherited_base or lead
        if idx == 0:
            inherited_base = base
        # 指针判定：基类型带 '*'，或是函数指针 typedef（回调字段本质是指针）
        is_ptr = ('*' in base) or (base in fnptr_typedefs)
        base_clean = base.replace('*', ' ').strip()
        is_bool = base_clean in ('bool', '_Bool', 'XBool')
        fields.append(Field(name, base_clean or '?', is_ptr, is_arr, is_bit, is_bool, ''))
    return fields


DIRECTIVE_RE = re.compile(r'^\s*#\s*(ifdef|ifndef|if|else|elif|endif)\b', re.M)


def build_cond_events(blanked):
    """预处理条件编译事件表：返回 [(offset, 嵌套深度, 顶层是否在 #else)]。

    offset 为该指令行首在 blanked 中的位置；用于反查某个偏移处
    是否位于 #if 内、是否位于 #else 回退分支（模块裁剪时的占位
    结构体，如 XPALETTE_ON=0 的 XPalette）。
    """
    events = []
    stack = []
    for m in DIRECTIVE_RE.finditer(blanked):
        kw = m.group(1)
        if kw in ('ifdef', 'ifndef', 'if'):
            stack.append(False)
        elif kw == 'else':
            if stack:
                stack[-1] = True
        elif kw == 'elif':
            if stack:
                stack[-1] = False
        elif kw == 'endif':
            if stack:
                stack.pop()
        events.append((m.start(), len(stack), bool(stack and stack[-1])))
    return events


def cond_state_at(events, pos):
    """二分查询 pos 处的条件编译状态，返回 (深度, 是否在 #else 分支)。"""
    lo, hi = 0, len(events)
    depth, in_else = 0, False
    while lo < hi:
        mid = (lo + hi) // 2
        if events[mid][0] < pos:
            depth, in_else = events[mid][1], events[mid][2]
            lo = mid + 1
        else:
            hi = mid
    return depth, in_else


def find_structs(header_path, fnptr_typedefs):
    """解析一个头文件，返回其中的 StructDef 列表（正文以注释抹白后的文本分析）。"""
    text = read_text(header_path)
    blanked, doc_spans = blank_comments(text)
    cond_events = build_cond_events(blanked)
    structs = []
    for m in re.finditer(r'\btypedef\s+struct(?:\s+[A-Za-z_]\w*)?\s*\{', blanked):
        open_idx = blanked.index('{', m.start())
        close_idx = match_brace(blanked, open_idx)
        if close_idx < 0:
            continue
        tail = re.match(r'\s*\}\s*([A-Za-z_]\w*)\s*;', blanked[close_idx:])
        if not tail:
            continue
        sname = tail.group(1)
        s_depth, s_in_else = cond_state_at(cond_events, m.start())
        body = blanked[open_idx + 1:close_idx]

        # 按顶层分号切成员声明；声明之间的文档注释回填给前一个字段
        fields = []
        decl_start = None
        depth = 0
        pieces = []  # (起始偏移, 结束偏移含分号)
        piece_begin = 0
        for i, ch in enumerate(body):
            if ch in '([':
                depth += 1
            elif ch in ')]':
                depth -= 1
            elif ch == ';' and depth == 0:
                pieces.append((piece_begin, i))
                piece_begin = i + 1
        last_end = 0
        for (s, e) in pieces:
            # 本声明之前的文档注释（位于上一个分号与本声明之间）→ 归前一字段
            between = [d for d in doc_spans if last_end + open_idx + 1 <= d[0] < s + open_idx + 1]
            raw_chunk = body[s:e + 1]
            parsed = parse_field_decl(raw_chunk, fnptr_typedefs)
            # 注释文本（/**< ... */ 内文），清理换行与星号
            comment = ''
            if between:
                inner = between[-1][2]
                inner = re.sub(r'/\*\*<?', '', inner)
                inner = inner.replace('*/', '')
                comment = re.sub(r'\s*\n\s*\*\s*', ' ', inner)
                comment = re.sub(r'\s+', ' ', comment).strip()
            for f in parsed:
                f.comment = comment if comment else f.comment
                f.cond_depth, f.cond_in_else = cond_state_at(
                    cond_events, open_idx + 1 + s)
            fields.extend(parsed)
            last_end = e + 1
        # 过滤基类成员
        fields = [f for f in fields if f.name not in ('m_base', 'm_class')]
        first_is_base = False
        # 判断首成员是否为基类：看第一个分号前的声明是否以 m_base/m_class 结尾
        first_decl = blanked[open_idx + 1: open_idx + 1 + pieces[0][1]] if pieces else ''
        if re.search(r'\b(m_base|m_class)\s*$', first_decl.strip()):
            first_is_base = True
        sd = StructDef(sname, header_path, fields, first_is_base)
        sd.cond_depth, sd.cond_in_else = s_depth, s_in_else
        structs.append(sd)
    return structs


def collect_fnptr_typedefs(files):
    """收集全部函数指针 typedef 名（如 XLineEditValidatorFunc），用于识别
    「typedef 过的回调字段」本质上仍是指针。"""
    names = set()
    for path in files:
        blanked, _ = blank_comments(read_text(path))
        for m in FN_PTR_TYPEDEF_RE.finditer(blanked):
            names.add(m.group(1))
    return names


# ---------------------------------------------------------------------------
# C 文件解析：X<类>_init 函数体
# ---------------------------------------------------------------------------

class InitInfo(object):
    """某个类的 init 函数信息（含委托链合并后的结果）。"""
    __slots__ = ('func_name', 'files', 'self_name', 'full_init',
                 'n_definitions', 'mentions')

    def __init__(self, func_name, files, self_name, full_init,
                 n_definitions, mentions):
        self.func_name = func_name
        self.files = files          # init 家族所在文件列表
        self.self_name = self_name  # 入口函数的 self 形参名
        self.full_init = full_init  # 链上任一函数体 XMemset(self,0,...)
        self.n_definitions = n_definitions
        self.mentions = mentions    # 合并后被提及的成员名集合


def find_init_definitions(func_name, c_texts):
    """在所有 .c 中找 func_name 的函数定义，返回 [(file, body, self_param)]。

    定义判定：形参表右括号之后（跳过空白）紧跟 '{'。
    """
    results = []
    pat = re.compile(r'\b' + re.escape(func_name) + r'\s*\(')
    for path, blanked in c_texts:
        for m in pat.finditer(blanked):
            open_p = blanked.index('(', m.start())
            close_p = match_paren(blanked, open_p)
            if close_p < 0:
                continue
            after = re.match(r'\s*\{', blanked[close_p + 1:])
            if not after:
                continue  # 是调用而非定义
            open_b = blanked.index('{', close_p + 1)
            close_b = match_brace(blanked, open_b)
            if close_b < 0:
                continue
            params = blanked[open_p + 1:close_p]
            first_param = split_top_level(params, ',')[0] if params.strip() else ''
            toks = re.findall(r'[A-Za-z_]\w*', first_param)
            self_name = toks[-1] if toks and toks != ['void'] else None
            body = blanked[open_b + 1:close_b]
            results.append((path, body, self_name))
    return results


def analyze_init(struct, c_texts):
    """对单个结构体提取 init 信息（含委托链）。

    代码库惯例：`X<类>_init` 常是薄包装，真正的逐字段初始化在
    `X<类>_init_2` / `X<类>_init_ex` 等变体里（如 XMenu_init →
    XMenu_init_2 内含 XMemset(self,0,...)）。因此从 `<类>_init`
    出发，沿函数体内对 `<类>_init*` 家族的调用递归收集全部函数体，
    再做字面量对比，避免把薄包装误判为「什么都没初始化」。
    """
    base = struct.name + '_init'
    defs = find_init_definitions(base, c_texts)
    if not defs:
        # 回退：没有 <类>_init 定义时，改用 <类>_init_* 变体作为入口
        fallback = re.compile(r'\b(' + re.escape(struct.name) + r'_init_\w+)\s*\(')
        seen_fb = set()
        for path, blanked in c_texts:
            for m in fallback.finditer(blanked):
                fname = m.group(1)
                if fname in seen_fb:
                    continue
                seen_fb.add(fname)
                defs.extend(find_init_definitions(fname, c_texts))
        if not defs:
            return None
    # 入口定义统一为 (函数名, 文件, 函数体, self 形参名) 四元组
    pending = [(base, path, body, self_name) for path, body, self_name in defs]
    # 家族委托链：沿函数体内对 <类>_init* 的调用递归收集全部函数体
    family_pat = re.compile(r'\b(' + re.escape(struct.name) + r'_init(?:_\w+)?)\s*\(')
    seen = set()      # 已收集的函数名（去重）
    bodies = []       # (self_name, body)
    files = []        # init 家族所在文件（按出现顺序去重）
    while pending:
        fname, path, body, self_name = pending.pop(0)
        if fname in seen:
            continue
        seen.add(fname)
        bodies.append((self_name, body))
        if path not in files:
            files.append(path)
        # 扫描该函数体对家族其它变体的调用 → 继续收集
        for m in family_pat.finditer(body):
            callee = m.group(1)
            if callee in seen:
                continue
            for path2, body2, self2 in find_init_definitions(callee, c_texts):
                pending.append((callee, path2, body2, self2))
    # 合并分析：任一函数体全量清零 → 全量初始化；成员提及取并集
    full_init = False
    mentions = set()
    struct_name = struct.name
    for self_name, body in bodies:
        if not self_name:
            continue
        esc = re.escape(self_name)
        # 规则一：XMemset/memset(self, 0, ...)（可带强转），或 *self = (X类){0}
        full_pat = re.compile(
            r'\b(?:XMemset|memset)\s*\(\s*(?:\([^)]*\))?\s*' + esc + r'\s*,\s*0\b'
            r'|\(\s*\*\s*' + esc + r'\s*\)\s*=\s*[A-Za-z_]\w*\s*\{\s*0\s*\}')
        if full_pat.search(body):
            full_init = True
        # 规则二：尾部清零 —— XMemset(((基类*)app)+1, 0, sizeof(X类)-sizeof(基类))
        # 这类写法清零「本结构体自己声明的全部字段」，等价全量初始化
        # （基类成员不在本结构体字段清单内）。判定：memset 语句的
        # 地址参数含 self、长度参数含 sizeof(本结构体名) 或 sizeof(*self)。
        for zm in re.finditer(r'\b(?:XMemset|memset)\s*\(', body):
            open_p = body.index('(', zm.start())
            close_p = match_paren(body, open_p)
            if close_p < 0:
                continue
            args = split_top_level(body[open_p + 1:close_p], ',')
            if not args:
                continue
            addr_has_self = re.search(r'\b' + esc + r'\b', args[0]) is not None
            size_text = args[-1] if len(args) >= 3 else ''
            size_ok = (re.search(r'\bsizeof\s*\(\s*' + re.escape(struct_name) +
                                 r'\s*\)', size_text) is not None or
                       re.search(r'\bsizeof\s*\(\s*\*\s*' + esc + r'\s*\)',
                                 size_text) is not None)
            if addr_has_self and size_ok and '0' in ','.join(args[1:-1] or []):
                full_init = True
        for mm in re.finditer(esc + r'\s*->\s*([A-Za-z_]\w*)', body):
            mentions.add(mm.group(1))
        for mm in re.finditer(r'\(\s*' + esc + r'\s*\)\s*->\s*([A-Za-z_]\w*)', body):
            mentions.add(mm.group(1))
    info = InitInfo(base, files, bodies[0][0] if bodies else None,
                    full_init, len(defs), mentions)
    return info


# ---------------------------------------------------------------------------
# 风险评级与备注
# ---------------------------------------------------------------------------

PROMISE_PAT = re.compile(r'默认\s*NULL|NULL\s*=|默认空|置空|初始.{0,4}NULL|NULL 之前')
RESERVED_PAT = re.compile(r'预留|保留|[Rr]eserved')
FUTURE_PAT = re.compile(r'后续扩展|仅存储|暂不|TODO')
OWNED_PAT = re.compile(r'拥有|借用|内部类')

RISK_ORDER = {'高': 0, '中': 1, '低': 2}


def classify_field(field, struct=None):
    """返回 (风险等级, 类型特征, 风险备注)。字段未被 init 提及时调用。"""
    notes = []
    if field.is_pointer:
        risk = '高'
        kind = '指针'
        if field.is_array:
            kind = '指针数组'
        notes.append('指针字段未在 init 中赋值，堆残留垃圾即野指针'
                     '（XLineEdit.m_completer 同类漏网）')
        if field.base_type == '<func ptr>' or field.base_type.endswith('Func'):
            notes.append('回调函数指针：判空保护依赖默认 NULL，必须显式置空')
    elif field.is_bool or field.is_bitfield:
        risk = '中'
        kind = '位域' if field.is_bitfield else '布尔'
        notes.append('布尔/标志位未清零，读到非 0 垃圾即逻辑为真')
    elif field.is_array:
        risk = '中'
        kind = '定长数组'
        notes.append('定长数组未清零，元素为堆残留垃圾')
    else:
        risk = '低'
        kind = '值字段'
        notes.append('数值/结构体字段未赋值（可能依赖后续 set/惰性初始化，人工确认）')
    if field.comment:
        if PROMISE_PAT.search(field.comment):
            notes.append('结构体注释承诺默认空/NULL，但 init 未赋值 —— 注释与实现不符')
        if RESERVED_PAT.search(field.comment):
            notes.append('注释标注预留/保留，人工复核后可标注豁免')
        if FUTURE_PAT.search(field.comment):
            notes.append('注释标注后续扩展/仅存储')
        if OWNED_PAT.search(field.comment):
            notes.append('注释涉及所有权（拥有/借用），未初始化风险更高')
    # 条件编译上下文：字段/结构体位于 #if 内或 #else 回退分支
    if field.cond_depth > 0:
        notes.append('字段位于条件编译 #if 内（深度 %d），需在对应配置下核查' % field.cond_depth)
    if field.cond_in_else or (struct is not None and struct.cond_in_else):
        notes.append('位于条件编译 #else 回退分支（模块关闭时的占位/回退定义），可人工豁免')
    return risk, kind, '；'.join(notes)


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def gather_files(src_dir):
    """递归收集 (headers, c_sources)。"""
    headers, cs = [], []
    for root, _dirs, files in os.walk(src_dir):
        for fn in sorted(files):
            p = os.path.join(root, fn)
            if fn.endswith('.h'):
                headers.append(p)
            elif fn.endswith('.c'):
                cs.append(p)
    return headers, cs


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog='init_field_check.py',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description='XGui「控件 init 未初始化全部结构体字段」静态检查工具'
                    '（纯字面量对比，python3 标准库）。',
        epilog='示例:\n'
               '  python3 tools/init_field_check.py\n'
               '  python3 tools/init_field_check.py --src-dir Src/XGui/Widget '
               '--output .tmpdbg/init-check-report.md\n'
               '  python3 tools/init_field_check.py --check XLineEdit.m_completer\n'
               '\n'
               '已知局限（设计如此）: init 调用辅助函数/模板拷贝时按字面量对比会'
               '漏报；工具目标是抓「完全未提及的指针/布尔字段」，漏网候选需人工复核。')
    repo_default = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument('--src-dir', default=os.path.join(repo_default, 'Src', 'XGui'),
                        help='扫描的源码目录（递归收集 .h/.c，默认 <仓库>/Src/XGui）')
    parser.add_argument('--output', default=os.path.join(repo_default, '.tmpdbg', 'init-check-report.md'),
                        help='报告输出路径（默认 <仓库>/.tmpdbg/init-check-report.md）')
    parser.add_argument('--check', action='append', default=[], metavar='类.字段',
                        help='专项核查指定字段（可多次），如 XLineEdit.m_completer；'
                             '结果写入报告「专项核查」一节')
    parser.add_argument('--quiet', action='store_true', help='不在 stdout 打印摘要')
    args = parser.parse_args(argv)

    src_dir = os.path.abspath(args.src_dir)
    if not os.path.isdir(src_dir):
        print('错误：--src-dir 不存在：%s' % src_dir, file=sys.stderr)
        return 2

    headers, cs = gather_files(src_dir)
    c_texts = [(p, blank_comments(read_text(p))[0]) for p in cs]

    fnptr_typedefs = collect_fnptr_typedefs(headers)

    structs = []
    for h in headers:
        structs.extend(find_structs(h, fnptr_typedefs))

    rows = []            # 漏网候选 (risk, struct, field, kind, note, assigned)
    full_zero = []       # 全量清零类
    no_init = []         # 未找到 init 的类
    checked = 0          # 实际核查的类数
    focus_results = []   # 专项核查结果

    for st in structs:
        if not st.fields:
            continue
        info = analyze_init(st, c_texts)
        if info is None:
            if st.name not in [s.name for s in no_init]:
                no_init.append(st)
            continue
        checked += 1
        mentions = getattr(info, 'mentions', set())
        if info.full_init:
            if st.name not in [s.name for s, _i in full_zero]:
                full_zero.append((st, info))
            assigned = set(f.name for f in st.fields)
        else:
            assigned = mentions
        for f in st.fields:
            ok = f.name in assigned
            if not ok:
                risk, kind, note = classify_field(f, st)
                rows.append((risk, st, f, kind, note))
        # 专项核查
        for spec in args.check:
            cls, _, fld = spec.partition('.')
            if cls == st.name and fld:
                focus_results.append((spec, st, info, fld in assigned))

    rows.sort(key=lambda r: (RISK_ORDER[r[0]], r[1].name, r[2].name))

    # ---------------- 生成报告 ----------------
    out_lines = []
    w = out_lines.append
    w('# XGui 控件 init 字段初始化静态检查报告')
    w('')
    w('- 生成时间：%s' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    w('- 工具：`tools/init_field_check.py`（python3 标准库，字面量对比）')
    w('- 扫描目录：`%s`（.h %d 个 / .c %d 个）' % (src_dir, len(headers), len(cs)))
    w('- 检查对象：结构体字段 vs `X<类>_init` 函数体中是否被提及；'
      '`XMemset(self, 0, sizeof(...))` 视为全量初始化')
    w('- 背景教训：XLineEdit.m_completer（结构体注释承诺「默认 NULL」，'
      'init 未赋值 → 堆垃圾野指针，2026-09-16 修复）')
    w('- 局限：init 调用辅助函数（如 xtx_ensureBoolTable）或模板拷贝时'
      '按字面量对比会漏报；本报告目标是抓「完全未提及的指针/布尔字段」，'
      '所有候选需人工复核标注')
    w('')
    w('## 结论摘要')
    w('')
    w('| 指标 | 数量 |')
    w('| --- | ---: |')
    w('| 结构体（含字段者，按名字去重） | %d |' % len(set(s.name for s in structs if s.fields)))
    w('| 找到 init 函数并核查 | %d |' % checked)
    w('| —— 其中全量清零类（XMemset(self,0,...)，无需逐字段） | %d |' % len(full_zero))
    w('| 未找到 init 的类（无法核查） | %d |' % len(no_init))
    w('| **漏网候选合计** | **%d** |' % len(rows))
    for rk in ('高', '中', '低'):
        w('| —— %s风险 | %d |' % (rk, len([r for r in rows if r[0] == rk])))
    w('')

    def emit_table(title, risk):
        sub = [r for r in rows if r[0] == risk]
        w('## %s（%d 项）' % (title, len(sub)))
        w('')
        if not sub:
            w('（无）')
            w('')
            return
        w('| 类 | 字段 | init 是否赋值 | 风险备注 |')
        w('| --- | --- | --- | --- |')
        for _risk, st, f, kind, note in sub:
            note_full = '【%s】%s' % (kind, note)
            w('| %s | `%s` | 否 | %s |' % (st.name, f.name, note_full))
        w('')

    emit_table('高风险漏网（指针字段未在 init 提及）', '高')
    emit_table('中风险（布尔/位域/定长数组未清零）', '中')
    emit_table('低风险（值字段，可能有意为之，人工确认）', '低')

    # 专项核查
    if args.check:
        w('## 专项核查')
        w('')
        w('| 核查项 | init 是否赋值 | 结论 |')
        w('| --- | --- | --- |')
        for spec, st, info, ok in focus_results:
            rel = '、'.join('`%s`' % os.path.relpath(p, src_dir)
                            for p in (info.files if info else [])) or '-'
            concl = ('已在 %s 提及，修复有效' % rel) if ok else \
                    ('init 未提及该字段！文件：%s' % rel)
            w('| `%s` | %s | %s |' % (spec, '是' if ok else '否', concl))
        w('')

    # 全量清零类
    w('## 全量清零类（豁免逐字段检查）')
    w('')
    if full_zero:
        w('| 类 | init 位置 |')
        w('| --- | --- |')
        for st, info in full_zero:
            w('| %s | `%s` |' % (st.name, os.path.relpath(info.files[0], src_dir)))
    else:
        w('（无）')
    w('')

    # 未找到 init
    w('## 未找到 init 函数的类（无法核查）')
    w('')
    if no_init:
        w('| 类 | 头文件 | 字段数 |')
        w('| --- | --- | ---: |')
        for st in sorted(no_init, key=lambda s: s.name):
            w('| %s | `%s` | %d |' % (st.name, os.path.relpath(st.header, src_dir), len(st.fields)))
    else:
        w('（无）')
    w('')

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(out_lines))

    if not args.quiet:
        print('扫描完成：%d 个结构体，核查 %d 个，全量清零 %d 个，未找到 init %d 个'
              % (len([s for s in structs if s.fields]), checked, len(full_zero), len(no_init)))
        print('漏网候选：高 %d / 中 %d / 低 %d（合计 %d）'
              % (len([r for r in rows if r[0] == '高']),
                 len([r for r in rows if r[0] == '中']),
                 len([r for r in rows if r[0] == '低']), len(rows)))
        for rk in ('高', '中'):
            for _risk, st, f, _kind, _note in [r for r in rows if r[0] == rk][:10]:
                print('  [%s] %s.%s' % (rk, st.name, f.name))
        print('报告：%s' % os.path.abspath(args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
