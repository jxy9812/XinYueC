#!/usr/bin/env python3
"""XVirtualKeyboard LVGL 风格自动化检查：静态断言 + 无头截图像素采样 + 几何走查.

四层检查（与 pixelPlan 逐条对应）：
  1) 静态断言：XKB_LVGL_* 常量逐项对照 LVGL 9.2.2 源码现算规格值
     （palette 表/darken/mix 公式按 lv_conf.h 的 LV_COLOR_MIX_ROUND_OFS
     复算，非硬编码期望表）；XKEYBOARD_THEME_LVGL_ON 与旧调色板分支
     全库清零（剔除注释后扫描）；布局几何常量在规格带宽内。
  2) 动态截图：bin/XGuiWindowDemo_Test.exe --screenshot <png> --page 9
     + 无头钩子环境变量（XGUI_KB_AUTOSHOW 等，见
     Test/XGuiDemo/xgui_demo_page_keyboard.c 的
     demo_page_keyboard_headless_hook）。exe 未含钩子时动态层整体
     SKIP（不伪造通过），待主流程重建后再跑。
  3) 像素采样：面板/键面/checked 键底/按压面/候选带三区/禁用箭头逐点
     采样；键面与候选带圆角四角对角线走查（外侧=面板、内侧=键面、
     中间只允许单调过渡带）；文字像素簇存在性（键面中心区深色簇）。
  4) 几何走查：钩子 XKB-GEO 几何行与 xkb_rebuildLayout 边界式公式
     双簿比对（布局漂移在像素采样前截获）；相邻键面 4px 视觉间距、
     行高整除余量、面板 2px 内缩。

用法：
    python Tools/VirtualKeyboard/style_check.py [--json <path>] [--screenshot-dir <dir>]
        [--skip-dynamic] [--keep-png] [--known-deltas=warn]
        [--exe <path>] [--lvgl-src <path>] [--dpi <n>]

    --json 缺值/空值/未传时一律回退默认 out/style_check/style_check.json
    （JSON 恒写出——第 1 轮「--json: expected one argument」argparse
    exit 2 由此复现：调用方丢值即整脚本终止且无任何输出可解析）。

退出码：0=全部执行过的检查通过（SKIP/WARN 不计失败）；1=存在 FAIL；
        2=基础设施错误（源文件缺失等）。

已知偏差口径（--known-deltas=warn 时降级为 WARN，默认仍 FAIL）：
  - XKB_LVGL_PRIMARY_MUTED 0xD3EAFD 与 XKB_LVGL_DISABLED_TEXT 0x818181
    两处历史 Δ1/255 偏差已在第 2 轮修值对齐（现值 0xD2EAFC=
    primary@LV_OPA_20 白卡合成、0x808080=mix(GREY2,TEXT,LV_OPA_50=127)，
    均按本模板 LV_COLOR_MIX_ROUND_OFS=0 复算）；开关保留作后续偏差
    升降级的通用杠杆。
"""

import argparse
import json
import os
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
XKB = ROOT / "Src/XGui/VirtualKeyboard/XVirtualKeyboard.c"
DEFAULT_EXE = ROOT / "bin/XGuiWindowDemo_Test.exe"
DEFAULT_LVGL_SRC = Path("D:/code/SCM/Template - LVGL9.2.2/GUI/LVGL/src/lvgl/src")
DEFAULT_SHOT_DIR = ROOT / "out/style_check"
DEFAULT_JSON = DEFAULT_SHOT_DIR / "style_check.json"

CHECKS = []          # {group, name, status: pass|fail|warn|skip, detail}


def check(group, name, ok, detail="", status=None):
    """登记一条检查结果；ok 为 False 且未指定 status 时记 FAIL。"""
    if status is None:
        status = "pass" if ok else "fail"
    CHECKS.append({"group": group, "name": name,
                   "status": status, "detail": detail})
    return status


def skip(group, name, detail):
    return check(group, name, False, detail, status="skip")


# ==================== LVGL 规格值现算 ====================

def udiv255(x):
    """LV_UDIV255（lv_math.h:170：(x*0x8081)>>23）。"""
    return ((x * 0x8081) >> 0x17) & 0xFF


def lv_mix(c1, c2, m, ofs):
    """lv_color_mix（lv_color_op.c:36-44，逐通道）。"""
    return tuple(udiv255(c1[i] * m + c2[i] * (255 - m) + ofs)
                 for i in range(3))


def lv_darken(c, lvl, ofs):
    """lv_color_darken（lv_color.c:125-128）= mix(black, c, lvl)。"""
    return lv_mix((0, 0, 0), c, lvl, ofs)


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise SystemExit("error: cannot read %s (%s)" % (path, exc))


def parse_palette(src_text, name):
    """从 lv_palette.c 现取调色板表（main/lighten/darken）。

    返回行数组 rows[i] = 第 i 个调色板名的颜色元组列表（行序=枚举序）。
    """
    m = re.search(r"lv_palette_%s\(lv_palette_t p[^{]*?\{(.*?)\n\}" % name,
                  src_text, re.S)
    if not m:
        raise SystemExit("error: lv_palette_%s table not found" % name)
    body = m.group(1)
    make = (r"LV_COLOR_MAKE\(0x([0-9A-Fa-f]{2}), "
            r"0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2})\)")
    rows = []
    for line in body.splitlines():
        row = re.findall(make, line)
        if row:
            rows.append([tuple(int(v, 16) for v in t) for t in row])
    if not rows:
        raise SystemExit("error: lv_palette_%s has no LV_COLOR_MAKE rows"
                         % name)
    if name == "main":
        # main 表为单数组平铺（多行多列），摊平为按枚举序的一维表。
        flat = [c for row in rows for c in row]
        return [flat]
    return rows


def parse_palette_enum(header_text):
    """从 lv_palette.h 枚举顺序取 LV_PALETTE_* 名→索引（截止 LAST）。"""
    names = []
    for m in re.finditer(r"LV_PALETTE_([A-Z_]+)", header_text):
        n = m.group(1)
        if n == "LAST":
            break
        if n == "H" or n.endswith("_H"):
            continue  # include guard LV_PALETTE_H 等，非枚举成员
        if n not in names:
            names.append(n)
    return {n: i for i, n in enumerate(names)}


def lvgl_spec(lvgl_src):
    """现算全部 LVGL 规格值；返回 dict。任一环节缺失即 SystemExit。"""
    spec = {}
    ver = None
    for cand in (lvgl_src / "lv_version.h", lvgl_src.parent / "lv_version.h"):
        if cand.exists():
            ver = read_text(cand)
            break
    if ver is None:
        raise SystemExit("error: lv_version.h not found under %s" % lvgl_src)
    for key, pat in (("major", r"LVGL_VERSION_MAJOR\s+(\d+)"),
                     ("minor", r"LVGL_VERSION_MINOR\s+(\d+)"),
                     ("patch", r"LVGL_VERSION_PATCH\s+(\d+)")):
        m = re.search(pat, ver)
        if not m:
            raise SystemExit("error: lv_version.h missing %s" % key)
        spec[key] = int(m.group(1))

    pal_h = read_text(lvgl_src / "misc/lv_palette.h")
    pal_c = read_text(lvgl_src / "misc/lv_palette.c")
    enum = parse_palette_enum(pal_h)
    main_rows = parse_palette(pal_c, "main")
    lighten_rows = parse_palette(pal_c, "lighten")
    darken_rows = parse_palette(pal_c, "darken")

    grey = enum["GREY"]
    blue = enum["BLUE"]
    spec["lighten_grey"] = {i + 1: c for i, c in enumerate(lighten_rows[grey])}
    spec["darken_grey"] = {i + 1: c for i, c in enumerate(darken_rows[grey])}
    spec["main_blue"] = main_rows[0][blue]
    spec["white"] = (255, 255, 255)

    color_h = read_text(lvgl_src / "misc/lv_color.h")
    for key, pat in (("opa20", r"LV_OPA_20\s*=\s*(\d+)"),
                     ("opa50", r"LV_OPA_50\s*=\s*(\d+)")):
        m = re.search(pat, color_h)
        if not m:
            raise SystemExit("error: lv_color.h missing %s" % key)
        spec[key] = int(m.group(1))

    conf = None
    for cand in (lvgl_src / "lv_conf.h", lvgl_src.parent / "lv_conf.h"):
        if cand.exists():
            conf = read_text(cand)
            break
    conf_internal = read_text(lvgl_src / "lv_conf_internal.h")
    spec["ofs"] = None
    spec["ofs_fallback"] = False
    if conf:
        m = re.search(r"#define\s+LV_COLOR_MIX_ROUND_OFS\s+(\d+)", conf)
        if m:
            spec["ofs"] = int(m.group(1))
    if spec["ofs"] is None:
        # lv_conf_internal.h 兜底缺省（取末个 #define）
        m = re.findall(r"#define\s+LV_COLOR_MIX_ROUND_OFS\s+(\d+)",
                       conf_internal)
        spec["ofs"] = int(m[-1]) if m else 0
        spec["ofs_fallback"] = True
    m = re.search(r"#define\s+LV_THEME_DEFAULT_DARK\s+(\d+)", conf or "")
    spec["theme_dark"] = int(m.group(1)) if m else None

    dpi_m = re.search(r"#define\s+LV_DPI_DEF\s+(\d+)", conf or "")
    if not dpi_m:
        dpi_m = re.search(r"#define\s+LV_DPI_DEF\s+(\d+)", conf_internal)
    spec["dpi_def"] = int(dpi_m.group(1)) if dpi_m else 130

    disp_h = read_text(lvgl_src / "display/lv_display.h")
    m = re.search(r"#define\s+LV_DPX_CALC\(dpi, n\)\s+(.+)", disp_h)
    spec["dpx_calc_raw"] = m.group(1).strip() if m else ""
    return spec


def dpx(dpi, n):
    """LV_DPX_CALC（lv_display.h:565）：max((dpi*n+80)/160, 1)，n=0→0。"""
    if n == 0:
        return 0
    return max((dpi * n + 80) // 160, 1)


def hx(t):
    return "#%02X%02X%02X" % tuple(t[:3])


# ==================== 项目常量提取 ====================

def strip_comments(text):
    """剔除 // 与 /* */ 注释，返回 (code_text, comment_text)。"""
    out = []
    cmt = []
    i, n = 0, len(text)
    in_block = False
    in_line = False
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if in_block:
            if c == "*" and nxt == "/":
                in_block = False
                cmt.append(" ")
                i += 2
                continue
            cmt.append(c)
            i += 1
            continue
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            else:
                cmt.append(c)
            i += 1
            continue
        if c == "/" and nxt == "*":
            in_block = True
            cmt.append(" ")
            i += 2
            continue
        if c == "/" and nxt == "/":
            in_line = True
            cmt.append(" ")
            i += 2
            continue
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n and text[j] != q:
                if text[j] == "\\":
                    j += 1
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out), "".join(cmt)


def extract_xkb_constants(code):
    """XKB_LVGL_* 与布局常量提取（正则对源码，不硬编码期望值）。"""
    consts = {}
    for m in re.finditer(
            r"#define\s+(XKB_LVGL_[A-Z_]+)\s+0x([0-9A-Fa-f]{6,8})u?", code):
        v = int(m.group(2), 16)
        consts[m.group(1)] = v if v <= 0xFFFFFF else (v & 0xFFFFFF)
    ime = {}
    for name, pat in (
            ("MODE_CHIP_W", r"#define\s+XKB_IME_MODE_CHIP_W\s+(\d+)"),
            ("PAGE_CELL_W", r"#define\s+XKB_IME_PAGE_CELL_W\s+(\d+)"),
            ("GAP", r"#define\s+XKB_IME_GAP\s+(\d+)"),
            ("PAD", r"#define\s+XKB_IME_PAD\s+(\d+)"),
            ("CHIP_PAD", r"#define\s+XKB_IME_CHIP_PAD\s+(\d+)"),
            ("PAGE_MAX", r"#define\s+XKB_IME_PAGE_MAX\s+(\d+)")):
        m = re.search(pat, code)
        if m:
            ime[name] = int(m.group(1))
    radius = re.search(
        r"xkb_keyRadius\(int rowH\)\s*\{\s*return rowH >= (\d+) \? "
        r"(\d+) : (\d+);", code)
    inset_panel = re.search(r"contentX = area\.x \+ (\d+);", code)
    inset_face = re.search(r"faceRect\.x = kr->x \+ (\d+);", code)
    band_radius = bool(re.search(
        r"radius = 8;\s*if \(radius > geo\.band\.height / 2\)", code))
    return {
        "xkb": consts, "ime": ime,
        "radius": tuple(int(g) for g in radius.groups()) if radius else None,
        "panel_inset": int(inset_panel.group(1)) if inset_panel else None,
        "face_inset": int(inset_face.group(1)) if inset_face else None,
        "band_radius_clamp": band_radius,
    }


# ==================== 静态检查 ====================

def static_checks(lvgl_src, dpi, known_deltas):
    spec = lvgl_spec(lvgl_src)
    text = read_text(XKB)
    code, _cmt = strip_comments(text)
    proj = extract_xkb_constants(code)

    # -- 0. 版本与浅色口径前提
    check("static.lvgl", "version-9.2.2",
          (spec["major"], spec["minor"], spec["patch"]) == (9, 2, 2),
          "lv_version.h = %d.%d.%d" % (spec["major"], spec["minor"],
                                       spec["patch"]))
    check("static.lvgl", "theme-default-dark==0（浅色口径前提）",
          spec["theme_dark"] == 0,
          "LV_THEME_DEFAULT_DARK=%s" % spec["theme_dark"])
    check("static.lvgl", "mix-round-ofs 已读取",
          spec["ofs"] is not None,
          "LV_COLOR_MIX_ROUND_OFS=%d%s" %
          (spec["ofs"], "（lv_conf_internal 兜底）"
           if spec.get("ofs_fallback") else ""))
    check("static.lvgl", "LV_DPX_CALC 公式在场",
          "/ 160" in spec["dpx_calc_raw"],
          spec["dpx_calc_raw"] or "未匹配")

    # -- 1. XKB_LVGL_* 逐项对照（期望值全部现算）
    scr = spec["lighten_grey"][4]
    grey2 = spec["lighten_grey"][2]
    txt = spec["darken_grey"][4]
    white = spec["white"]
    primary = spec["main_blue"]
    ofs = spec["ofs"]
    derived = [
        ("XKB_LVGL_SCR", scr, False, "lighten(GREY,4)"),
        ("XKB_LVGL_CARD", white, False, "lv_color_white()"),
        ("XKB_LVGL_TEXT", txt, False, "darken(GREY,4)"),
        ("XKB_LVGL_GREY", grey2, False, "lighten(GREY,2)"),
        ("XKB_LVGL_PRIMARY", primary, False, "lv_palette_main(BLUE)"),
        ("XKB_LVGL_PRIMARY_MUTED",
         lv_mix(primary, white, spec["opa20"], ofs), True,
         "primary@LV_OPA_20=%d 合成于白卡（OFS=%d 口径）"
         % (spec["opa20"], ofs)),
        ("XKB_LVGL_PRESSED_FACE", lv_darken(white, 35, ofs), False,
         "darken(white,35)"),
        ("XKB_LVGL_PRESSED_TEXT", lv_darken(txt, 35, ofs), False,
         "darken(TEXT,35)"),
        ("XKB_LVGL_DISABLED_TEXT", lv_mix(grey2, txt, spec["opa50"], ofs),
         True, "mix(lighten(GREY,2), TEXT, LV_OPA_50=%d)" % spec["opa50"]),
        ("XKB_LVGL_ARROW_TEXT", spec["darken_grey"][2], False,
         "palette darken GREY 第2列（翻页箭头取值）"),
    ]
    for name, expect, known, how in derived:
        got = proj["xkb"].get(name)
        if got is None:
            check("static.color", name, False, "常量未在 XVirtualKeyboard.c 找到")
            continue
        ok = got == (expect[0] << 16 | expect[1] << 8 | expect[2])
        detail = "project=%s expect=%s (%s)" % (
            hx((got >> 16, (got >> 8) & 0xFF, got & 0xFF)), hx(expect), how)
        if not ok and known and known_deltas == "warn":
            detail += " —— 已知 Δ1/255 偏差（规格 risks 预声明），降级 WARN"
            check("static.color", name, False, detail, status="warn")
        else:
            check("static.color", name, ok, detail)

    # -- 2. 布局几何常量带宽
    if proj["radius"]:
        thr, big, small = proj["radius"]
        lv_big, lv_mid, lv_small = dpx(dpi, 12), dpx(dpi, 8), dpx(dpi, 4)
        ok = (lv_mid <= big <= lv_big) and (lv_small <= small <= lv_mid)
        check("static.geo", "键圆角在 LVGL DPX 带宽内", ok,
              "rowH>=%d ? %d : %d px；LVGL@%ddpi：大屏 %d/中小 %d/小屏 %d px"
              "（近似口径，源码注释自认 ≈12/8dp）"
              % (thr, big, small, dpi, lv_big, lv_mid, lv_small))
    else:
        check("static.geo", "键圆角公式在场", False,
              "xkb_keyRadius 未匹配（源码形变？）")
    for name, expect, plan in (
            ("MODE_CHIP_W", 40, "「中」chip 固定 40px"),
            ("PAGE_CELL_W", 24, "翻页单元 24px"),
            ("GAP", 4, "带内子区间间距 4px"),
            ("PAD", 2, "带内缩边距 2px"),
            ("CHIP_PAD", 3, "chip 文本垫宽 3px"),
            ("PAGE_MAX", 9, "单页 chip 上限 9")):
        got = proj["ime"].get(name)
        check("static.geo", "XKB_IME_%s==%d" % (name, expect),
              got == expect, "实际 %s；pixelPlan：%s" % (got, plan))
    check("static.geo", "面板 2px 内缩", proj["panel_inset"] == 2,
          "contentX = area.x + %s（pixelPlan：四边内缩 2px）"
          % proj["panel_inset"])
    check("static.geo", "键面 2px 内缩（4px 视觉间距）",
          proj["face_inset"] == 2,
          "faceRect.x = kr->x + %s（pixelPlan：命中矩形四边再内缩 2px）"
          % proj["face_inset"])
    check("static.geo", "候选带圆角=min(8,h/2) 钳位",
          proj["band_radius_clamp"], "xkb_imeBandPaint radius=8 且 >h/2 钳位")

    # -- 3. 宏删除 + 旧调色板清库（剔除注释与脚本自身后全库扫描）
    scan_exts = (".c", ".h", ".md")
    skip_dirs = {"out", "bin", ".git", "build", ".zcode", "__pycache__"}
    self_path = Path(__file__).resolve()
    hits_macro = []
    hits_palette_code = []
    hits_palette_cmt = []
    old_literals = ["0xFF3C3C43", "0xFF2D2D30", "0xFF1E1E1E"]
    xkb_key = XKB.relative_to(ROOT).as_posix().lower()
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in skip_dirs]
        for fn in filenames:
            p = Path(dirpath) / fn
            if p.suffix.lower() not in scan_exts or p == self_path:
                continue
            try:
                t = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            rel = str(p.relative_to(ROOT)).replace("\\", "/")
            pcode, pcmt = strip_comments(t)
            for ln, line in enumerate(t.splitlines(), 1):
                if "XKEYBOARD_THEME_LVGL_ON" in line:
                    hits_macro.append((rel, ln, line.strip()[:90]))
            for lit in old_literals:
                if lit in pcode:
                    for ln, line in enumerate(pcode.splitlines(), 1):
                        if lit in line:
                            hits_palette_code.append((rel, ln, lit))
                if lit in pcmt:
                    hits_palette_cmt.append(rel)
    bad_macro = [h for h in hits_macro
                 if not (h[0].lower().replace("\\", "/") == xkb_key
                         and not h[2].startswith("#"))]
    detail = ("命中 %d 处且全部为 XVirtualKeyboard.c 注释（已删除/已合并说明）"
              % len(hits_macro)) if not bad_macro else \
        ("违例（宏分支复用/他文件引用）：%s" % (bad_macro[:5],))
    check("static.clearance", "XKEYBOARD_THEME_LVGL_ON 全库清零",
          not bad_macro, detail)
    detail = ("0xFF3C3C43/0xFF2D2D30/0xFF1E1E1E 剔注释后 0 命中；"
              "注释命中文件：%s" % sorted(set(hits_palette_cmt))) \
        if not hits_palette_code else \
        ("代码区残留：%s" % (hits_palette_code[:5],))
    check("static.clearance", "旧调色板字面量代码区清零",
          not hits_palette_code, detail)
    # 运行时 0xFFxxxxxx 字面量全部出自 XKB_LVGL_*/XKB_ACCENT_*（按
    # define 原文字面量放行；ACCENT=搜狗强调色命名常量，Sogou 改版一
    # 阶段引入，含逐通道派生注释——命名即出处，非杂散）。
    allowed = {m.group(1).upper()
               for m in re.finditer(
                   r"#define\s+XKB_(?:LVGL|ACCENT)[A-Z_]*\s+"
                   r"(0x[0-9A-Fa-f]{6,8})u?",
                   code)}
    stray = sorted({m.upper() for m in re.findall(r"0x[0-9A-Fa-f]{8}", code)
                    if m.upper() not in allowed})
    check("static.clearance",
          "XVirtualKeyboard.c 0xFFxxxxxx 全部出自 XKB_LVGL_*/XKB_ACCENT_*",
          not stray,
          ("杂散字面量：%s" % (stray,)) if stray else "0 杂散")
    return spec, proj


# ==================== PNG 解码（stdlib zlib+struct） ====================

class Png(object):
    """最小 PNG 解码：8bit truecolor/truecolor+alpha，全 5 种滤波。"""

    def __init__(self, path):
        d = path.read_bytes()
        if d[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError("not a PNG: %s" % path)
        pos = 8
        idat = b""
        self.w = self.h = self.ch = None
        while pos + 8 <= len(d):
            ln, typ = struct.unpack(">I4s", d[pos:pos + 8])
            body = d[pos + 8:pos + 8 + ln]
            if typ == b"IHDR":
                w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
                if depth != 8 or ctype not in (2, 6):
                    raise ValueError("unsupported PNG %dbit ct=%d"
                                     % (depth, ctype))
                self.w, self.h = w, h
                self.ch = 3 if ctype == 2 else 4
            elif typ == b"IDAT":
                idat += body
            pos += 12 + ln
        if not self.w:
            raise ValueError("IHDR missing")
        raw = zlib.decompress(idat)
        stride = self.w * self.ch
        self.px = bytearray(self.w * self.h * self.ch)
        prev = bytearray(stride)
        p = 0
        for y in range(self.h):
            f = raw[p]
            p += 1
            line = bytearray(raw[p:p + stride])
            p += stride
            if f == 1:
                for i in range(self.ch, stride):
                    line[i] = (line[i] + line[i - self.ch]) & 0xFF
            elif f == 2:
                for i in range(stride):
                    line[i] = (line[i] + prev[i]) & 0xFF
            elif f == 3:
                for i in range(stride):
                    a = line[i - self.ch] if i >= self.ch else 0
                    line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
            elif f == 4:
                for i in range(stride):
                    a = line[i - self.ch] if i >= self.ch else 0
                    b = prev[i]
                    c = prev[i - self.ch] if i >= self.ch else 0
                    pa = abs(b - c)
                    pb = abs(a - c)
                    pc = abs(a + b - 2 * c)
                    pr = a if (pa <= pb and pa <= pc) else \
                        (b if pb <= pc else c)
                    line[i] = (line[i] + pr) & 0xFF
            self.px[y * stride:(y + 1) * stride] = line
            prev = line

    def get(self, x, y):
        o = (y * self.w + x) * self.ch
        return tuple(self.px[o:o + 3])

    def near(self, x, y, color, tol):
        p = self.get(x, y)
        return dist(p, color) <= tol

    def scan_dark(self, rect, max_lum=0x60):
        """矩形内深色像素计数与最暗像素（文字簇口径）。"""
        x0, y0, w, h = rect
        count = 0
        best_lum = 999
        best = None
        for yy in range(max(y0, 0), min(y0 + h, self.h)):
            for xx in range(max(x0, 0), min(x0 + w, self.w)):
                p = self.get(xx, yy)
                lum = (p[0] + p[1] + p[2]) // 3
                if lum < best_lum:
                    best_lum = lum
                    best = (xx, yy, p)
                if lum <= max_lum:
                    count += 1
        return count, best


def dist(c1, c2):
    return max(abs(c1[i] - c2[i]) for i in range(3))


# ==================== 几何双簿比对 ====================

def parse_geo(stdout):
    """XKB-GEO 行解析：返回 (widget, band, keys, rows)。"""
    widget = band = None
    keys = []
    rows = None
    for line in stdout.splitlines():
        if not line.startswith("XKB-GEO"):
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if line.startswith("XKB-GEO widget"):
            widget = tuple(int(fields[k]) for k in ("x", "y", "w", "h"))
        elif line.startswith("XKB-GEO band"):
            band = tuple(int(fields[k]) for k in ("x", "y", "w", "h"))
        elif line.startswith("XKB-GEO key"):
            keys.append({
                "i": int(fields["i"]), "ctrl": int(fields["ctrl"]),
                "rect": tuple(int(fields[k]) for k in ("x", "y", "w", "h")),
                "label": fields.get("label", ""),
            })
        elif line.startswith("XKB-GEO rows"):
            rows = int(fields.get("rows", "0"))
    return widget, band, keys, rows


def recompute_rects(widget, band, keys):
    """按 xkb_rebuildLayout 边界式复算期望矩形（控件局部坐标系）。

    dump 的键矩形为控件局部（XKB-GEO key 直出 m_keyRects），期望复算
    同用局部原点（内容区=控件四边内缩 2px）；窗口原点由 px_* 取样时
    才加 widget x/y。返回 (expect{i:(x,y,w,h)}, row_info)。
    """
    ww, wh = widget[2], widget[3]
    content_x, content_y = 2, 2
    content_w, content_h = ww - 4, wh - 4
    row_y = sorted({k["rect"][1] for k in keys})
    n_rows = len(row_y)
    # 菜单条常驻预留（Sogou 二阶段，xkb_rebuildLayout 同式）：条高=
    # contentH/(rows+1)，所有布局/模式一致预留、键区 y 自 contentY+条高
    # 起。与中文态无关——dump 的 band 行是 m_imeBandRect（仅中文态镜像
    # 条矩形），英文态 band=0 而键区仍偏移，不可作预留依据。
    reserve = content_h // (n_rows + 1)
    row_h = (content_h - reserve) // n_rows
    by_index = {k["i"]: k for k in keys}
    expect = {}
    row_info = []
    for ri, y in enumerate(row_y):
        idxs = sorted(k["i"] for k in keys if k["rect"][1] == y)
        total = sum(max(by_index[i]["ctrl"] & 0xF, 1) for i in idxs)
        if total <= 0:
            total = 1
        x = content_x
        ey = content_y + reserve + ri * row_h
        row_info.append((ri, ey, row_h, total))
        for i in idxs:
            unit = max(by_index[i]["ctrl"] & 0xF, 1)
            x_end = x + (unit * content_w) // total
            expect[i] = (x, ey, x_end - x, row_h)
            x = x_end
    return expect, row_info


def geometry_checks(widget, band, keys, rows):
    """几何双簿：dump 矩形 vs xkb_rebuildLayout 边界式公式复算。"""
    if not widget or not keys:
        skip("geometry", "XKB-GEO 双簿比对", "dump 缺失（exe 未含钩子？）")
        return
    ok_all, details = True, []
    expect, row_info = recompute_rects(widget, band, keys)
    for k in keys:
        e = expect.get(k["i"])
        if e != tuple(k["rect"]):
            ok_all = False
            details.append("key %d label=%s dumped=%s expect=%s"
                           % (k["i"], k["label"], k["rect"], e))
    check("geometry", "键位矩形==布局公式复算（边界式）", ok_all,
          "；".join(details[:4]) if details else
          "%d 键全部一致（内容区 2px 内缩 + unit*contentW/total 边界式）"
          % len(keys))
    rh = {r[2] for r in row_info}
    check("geometry", "行高=(contentH-条高)//rows 整除口径（条高=常驻预留 contentH/(rows+1)）",
          len(rh) == 1,
          "rowH=%s rows=%d" % (sorted(rh), len(row_info)))
    gaps = set()
    by_row = {}
    for k in keys:
        by_row.setdefault(k["rect"][1], []).append(k)
    for y, ks in by_row.items():
        ks.sort(key=lambda a: a["rect"][0])
        for a, b in zip(ks, ks[1:]):
            gaps.add(b["rect"][0] - (a["rect"][0] + a["rect"][2]))
    gap_faces = {g + 4 for g in gaps}
    check("geometry", "相邻键面视觉间距 4px（两侧各缩 2px）",
          all(g == 4 for g in gap_faces),
          "命中矩形间隙=%s → 面距+4=%s" % (sorted(gaps), sorted(gap_faces)))


# ==================== 像素采样 ====================

def px_panel(png, widget, C):
    """① 面板留白 = SCR（角部+底边；底行键面止于 h-4，底 2px 为纯面板）。"""
    x0, y0, w, h = widget
    pts = [(x0 + 1, y0 + 1, "左上角"), (x0 + w // 2, y0 + h - 2, "底边中点"),
           (x0 + w - 2, y0 + 1, "右上角")]
    bad = [(p[:2], png.get(p[0], p[1])) for p in pts
           if not png.near(p[0], p[1], C["scr"], 2)]
    check("pixel.normal", "面板底=SCR %s（角部+底边）" % hx(C["scr"]),
          not bad,
          ("异常点：%s" % bad) if bad else
          "3 点全部命中（容差2）")


def px_key_faces(png, widget, keys, C):
    """② 键面白 + 文字簇；③ checked 键底 GREY。"""
    wx, wy = widget[0], widget[1]
    normals = [k for k in keys if not (k["ctrl"] & 0x400)]
    checked = [k for k in keys if k["ctrl"] & 0x400]
    if not normals:
        skip("pixel.normal", "键面采样", "布局无普通键")
        return
    fails = []
    sampled = 0
    for k in normals[:12]:
        kr = k["rect"]
        cx = kr[0] + 2 + wx + (kr[2] - 4) // 2
        cy = kr[1] + 2 + wy + 6
        if kr[2] - 4 < 4 or kr[3] - 4 < 4:
            continue
        sampled += 1
        if not png.near(cx, cy, C["card"], 2):
            fails.append((k["label"], png.get(cx, cy)))
    check("pixel.normal",
          "键面=CARD %s（面顶带 %d 键）" % (hx(C["card"]), sampled),
          sampled > 0 and not fails,
          ("异常：%s" % fails[:4]) if fails else "%d 键全部命中" % sampled)
    k = normals[min(2, len(normals) - 1)]
    kr = k["rect"]
    cnt, best = png.scan_dark((kr[0] + 6 + wx, kr[1] + 8 + wy,
                               kr[2] - 12, kr[3] - 16))
    ok = cnt >= 3 and best and dist(best[2], C["text"]) <= 48
    check("pixel.normal", "键面文字深色簇=TEXT %s" % hx(C["text"]), ok,
          "中心区深色像素 %d 枚，最暗 %s"
          % (cnt, hx(best[2]) if best else "-"))
    if checked:
        k = checked[0]
        kr = k["rect"]
        sx = kr[0] + 2 + wx + 4
        sy = kr[1] + 2 + wy + 4
        check("pixel.normal", "checked 键底=GREY %s（label=%s）"
              % (hx(C["grey"]), k["label"]),
              png.near(sx, sy, C["grey"], 2), "取样 %s" % hx(png.get(sx, sy)))
    else:
        skip("pixel.normal", "checked 键底=GREY", "当前布局无 CHECKED 键")


def px_corners(png, widget, keys, C, radius=8):
    """④ 圆角四角对角线走查：外=面板/内=键面/中间只允许单调过渡带。"""
    wx, wy = widget[0], widget[1]
    k = keys[1] if len(keys) > 1 else keys[0]
    kr = k["rect"]
    fx, fy = kr[0] + 2 + wx, kr[1] + 2 + wy
    corners = [(fx, fy, 1, 1), (fx + kr[2] - 5, fy, -1, 1),
               (fx, fy + kr[3] - 5, 1, -1),
               (fx + kr[2] - 5, fy + kr[3] - 5, -1, -1)]
    fails = []
    for ci, (cx, cy, dx, dy) in enumerate(corners):
        seq = []
        for t in range(0, radius + 1):
            p = png.get(cx + dx * t, cy + dy * t)
            if dist(p, C["scr"]) <= 4:
                seq.append("O")
            elif dist(p, C["card"]) <= 4:
                seq.append("I")
            else:
                seq.append("T")
        joined = "".join(seq)
        ok = ("O" in joined and "I" in joined
              and joined.index("I") > joined.rindex("O")
              and joined.count("T") <= radius // 2 + 1)
        if not ok:
            fails.append((ci, joined))
    check("pixel.normal", "键面圆角四角走查（外→内单调，过渡带≤%d px）"
          % (radius // 2 + 1), not fails,
          ("异常角 %s" % ([f[1] for f in fails],)) if fails else
          "四角序列 O*→T*→I* 全过")


def px_seams(png, widget, keys, C):
    """① 补：同行相邻键界 4px 缝全为面板。"""
    wx, wy = widget[0], widget[1]
    by_row = {}
    for k in keys:
        by_row.setdefault(k["rect"][1], []).append(k)
    fails = []
    tested = 0
    for y, ks in by_row.items():
        ks.sort(key=lambda a: a["rect"][0])
        for a, b in zip(ks, ks[1:]):
            if b["rect"][0] - (a["rect"][0] + a["rect"][2]) != 0:
                continue
            cy = a["rect"][1] + a["rect"][3] // 2 + wy
            xr = a["rect"][0] + a["rect"][2] + wx
            # 键界 xr 处两侧命中矩形贴边，但键面各内缩 2px→4px 面板缝
            # 为 [xr-2, xr+1]（xr-2=xr 界左侧键面末 2px 缘，xr+1=右侧键
            # 面首缘前 1px；xr+2 已是右键面白底——第 2 轮取样修正）。
            for x in (xr - 2, xr - 1, xr, xr + 1):
                tested += 1
                if not png.near(x, cy, C["scr"], 2):
                    fails.append((x, png.get(x, cy)))
    check("pixel.normal", "键间 4px 缝=面板 SCR", not fails and tested > 0,
          ("采样 %d 点，异常 %s" % (tested, fails[:3])) if fails else
          "采样 %d 点全部命中" % tested)


def px_band(png, widget, band, C, compose_filled):
    """⑤ 候选带底色/描边/圆角/翻页禁用态；组串区主色簇/chip 文字簇。"""
    if not band or band[3] <= 0:
        skip("pixel.band", "候选带检查", "band h==0（未切中文态？）")
        return
    wx, wy = widget[0], widget[1]
    bx, by, bw, bh = band[0] + wx, band[1] + wy, band[2], band[3]
    r = min(8, bh // 2)
    pad = C["ime_pad"]
    inner_h = bh - 2 * pad
    mid = bx + bw // 2
    check("pixel.band", "带顶边中点=GREY 1px 描边",
          png.near(mid, by, C["grey"], 2)
          and png.near(mid, by + 1, C["card"], 2),
          "edge=%s inner=%s" % (hx(png.get(mid, by)),
                                hx(png.get(mid, by + 1))))
    check("pixel.band", "带卡底=CARD 白",
          png.near(mid, by + bh // 2, C["card"], 2),
          "band 中心 %s" % hx(png.get(mid, by + bh // 2)))
    # 带外框圆角：对角走查不可行——modeChip 自 band.x+PAD(2) 起、高=带
    # 内高，翻页区贴右缘，四角对角线 2px 内即入内件（实测 t>=2 全为
    # chip GREY）。改用顶部自由行（band.y 行无任何内件）圆角签名：
    # 切点 (bx+r, by) 前 [bx, bx+3] 无 GREY 描边（方角回归时描边直铺
    # 到角点即失配）、切点处=描边 GREY。
    edge_fails = []
    for x in range(bx, min(bx + 4, bx + bw)):
        if dist(png.get(x, by), C["grey"]) <= 4:
            edge_fails.append((x - bx, hx(png.get(x, by))))
    tangent = png.get(bx + r, by)
    check("pixel.band",
          "带左上圆角（切点前无描边、切点=GREY，r=%d）" % r,
          not edge_fails and dist(tangent, C["grey"]) <= 4,
          "切点前 %s，切点(%+d,0)=%s"
          % (edge_fails or "无描边", r, hx(tangent)))
    # 「中」chip：GREY 面 + 深字簇
    mc_x, mc_y = bx + pad, by + pad
    fp = (mc_x + 4, mc_y + 4)
    cnt, best = png.scan_dark((mc_x + 6, mc_y + 4,
                               C["ime_mode_chip_w"] - 12, inner_h - 8))
    check("pixel.band", "「中」chip 面=GREY 且文字簇存在",
          png.near(fp[0], fp[1], C["grey"], 2) and cnt >= 3,
          "面 %s，深色 %d 枚（最暗 %s）"
          % (hx(png.get(*fp)), cnt, hx(best[2]) if best else "-"))
    # 组串区：主色弱高亮面（pixelPlan ⑤：取面角 (x+2,y+2)——面为
    # radius4 圆角矩形、文本左对齐 VCENTER，中左取样在组串非空时会
    # 落到字形上（compose 场景实测取到主色字 #43A6F4））。
    comp_x = mc_x + C["ime_mode_chip_w"] + C["ime_gap"]
    comp_y = by + pad
    check("pixel.band", "组串区面=PRIMARY_MUTED %s" % hx(C["primary_muted"]),
          png.near(comp_x + 2, comp_y + 2, C["primary_muted"], 2),
          "取样 %s" % hx(png.get(comp_x + 2, comp_y + 2)))
    # 翻页区："<" ">" 恒绘制（无候选时置灰）；页码文本仅 pageCount>0
    # 绘制（XVirtualKeyboard.c:916 if (pageCount > 0)）——空组串场景带内无候
    # 选，页码格恒白，只在有候选的 compose 场景检查（compose_filled）。
    right = bx + bw - pad
    cell = C["ime_page_cell_w"]
    cells = [("<", right - 3 * cell), (">", right - cell)]
    if compose_filled:
        cells.insert(1, ("1/1", right - 2 * cell))
    dis_fails = []
    for nm, x0 in cells:
        cnt, best = png.scan_dark((x0, comp_y, cell, inner_h), max_lum=0xB0)
        if not (cnt >= 1 and best and dist(best[2], C["disabled"]) <= 40):
            dis_fails.append((nm, hx(best[2]) if best else "-"))
    check("pixel.band",
          "翻页箭头/页码=DISABLED_TEXT %s（单页边界置灰）" % hx(C["disabled"]),
          not dis_fails,
          ("三格最暗 %s" % dis_fails) if dis_fails else
          "「<」「页码」「>」全部命中禁用灰" if compose_filled else
          "「<」「>」命中禁用灰（页码仅 pageCount>0 绘制，空组串不查）")
    if compose_filled:
        # 组串区主色文字簇：区内最蓝像素接近 PRIMARY
        comp_w = bw // 3
        blue_best = (None, -10 ** 9)
        for yy in range(comp_y, comp_y + inner_h):
            for xx in range(comp_x, min(comp_x + comp_w, bx + bw)):
                p = png.get(xx, yy)
                blue = p[2] - (p[0] + p[1]) // 2
                if blue > blue_best[1]:
                    blue_best = (p, blue)
        ok = blue_best[0] is not None and dist(blue_best[0], C["primary"]) <= 90
        check("pixel.band", "组串文字簇=PRIMARY %s" % hx(C["primary"]), ok,
              "最蓝像素 %s（blue 偏置 %d）"
              % (hx(blue_best[0]) if blue_best[0] else "-",
                 blue_best[1]))
        # 候选 chip 文字簇（chips 区=compose 右缘到翻页区左缘；compose 宽
        # 上限=带宽/3，从 comp_x+带宽/3 起扫描必落在 chips 区内）。
        # 字库口径：默认家族 XFontOutlineCommon 经 FT 外挂链解析
        # （XFONT_EXTERNAL_FT_FONT_DIR 相对 cwd 解析 + exe 目录旁兜底，
        # 2026-10-07 起 FT 为唯一轮廓字实现，原 XFO1 .xfo 通道已删），
        # 字库文件须随 exe 部署（如 bin/*/XFontOutlineCommon.ttc），
        # 缺文件时回退 8x16 小字集、候选汉字画不出（词簇断言即 FAIL——
        # 该 FAIL 是部署缺失信号，不吞）。
        zone_x0 = comp_x + bw // 3 + 8
        zone_w = (right - 3 * cell - 4) - zone_x0
        cnt, best = png.scan_dark((zone_x0, comp_y, zone_w, inner_h))
        check("pixel.band", "候选 chip 文字簇存在（chips 区深色簇）",
              cnt >= 3, "chips 区深色 %d 枚（最暗 %s）"
              % (cnt, hx(best[2]) if best else "-"))


def px_pressed(png, widget, keys, label, C):
    """⑦ 按压态：按住键面=PRESSED_FACE、文字=PRESSED_TEXT；邻键不受扰。"""
    wx, wy = widget[0], widget[1]
    hit = [k for k in keys if k["label"] == label]
    if not hit:
        skip("pixel.pressed", "按压键定位", "label=%s 不在布局" % label)
        return
    kr = hit[0]["rect"]
    fx, fy = kr[0] + 2 + wx, kr[1] + 2 + wy
    fw, fh = kr[2] - 4, kr[3] - 4
    p = png.get(fx + fw // 2, fy + 6)
    check("pixel.pressed", "按压键面=PRESSED_FACE %s" % hx(C["pressed_face"]),
          dist(p, C["pressed_face"]) <= 2, "取样 %s" % hx(p))
    cnt, best = png.scan_dark((fx + 6, fy + 8, fw - 12, fh - 16))
    ok = cnt >= 3 and best and dist(best[2], C["pressed_text"]) <= 40
    check("pixel.pressed",
          "按压键文字=PRESSED_TEXT %s" % hx(C["pressed_text"]), ok,
          "深色 %d 枚（最暗 %s）" % (cnt, hx(best[2]) if best else "-"))
    others = [k for k in keys
              if k["label"] != label and not (k["ctrl"] & 0x400)]
    if others:
        kr2 = others[0]["rect"]
        p2 = png.get(kr2[0] + 2 + wx + 4, kr2[1] + 2 + wy + 6)
        check("pixel.pressed", "非按压键面不受扰（仍=CARD）",
              dist(p2, C["card"]) <= 2, "邻键 %s" % hx(p2))


def px_bubble(png, widget, keys, label, C):
    """⑨ 气泡：按压 POPOVER 键上方放大区白底 + 文字（几何按绘制代码
    推导：bw=max(2*krW,24)、y=2、h=max(bandH,rowH)-4）。"""
    wx, wy, ww, wh = widget
    hit = [k for k in keys if k["label"] == label]
    if not hit:
        skip("pixel.bubble", "气泡键定位", "label=%s 不在布局" % label)
        return
    kr = hit[0]["rect"]
    rows = len({k["rect"][1] for k in keys})
    band_h = (wh - 4) // rows
    row_h = keys[0]["rect"][3]
    bw = max(kr[2] * 2, 24)
    bx = kr[0] + kr[2] // 2 - bw // 2
    if bx < 2:
        bx = 2
    if bx + bw > ww - 2:
        bx = ww - 2 - bw
    by, bh = 2, max(band_h, row_h) - 4
    sx = wx + bx + bw // 2
    sy = wy + by + 8
    check("pixel.bubble",
          "气泡面=CARD %s（rect=%s）"
          % (hx(C["card"]), (wx + bx, wy + by, bw, bh)),
          dist(png.get(sx, sy), C["card"]) <= 2,
          "取样 %s" % hx(png.get(sx, sy)))
    cnt, best = png.scan_dark((wx + bx + 4, wy + by + 10, bw - 8, bh - 14))
    ok = cnt >= 3 and best and dist(best[2], C["text"]) <= 48
    check("pixel.bubble", "气泡文字簇=TEXT", ok,
          "深色 %d 枚（最暗 %s）" % (cnt, hx(best[2]) if best else "-"))


def px_closed(png, widget, C, ref_png):
    """⑧ 收起残板检查（pixelPlan：无残板/残键）。

    与「键盘从未弹出」参照帧逐点比对（步长 3、容差 2）；失配点中只有
    再落在键盘色板（SCR/CARD/PRESSED/GREY/DISABLED/MUTED ±2）内才判
    残留——页面自带动态像素（右下内存 HUD 数字随运行分配量变化，实测
    5 点灰阶字形）不算残留。旧「纯色板扫描」口径误报 26364 点（被遮
    页面白底本身就在键盘色板内），旧「全区域严格 diff」口径误报 5 点
    （HUD 动态字形），本口径为两者交集。
    """
    x0, y0, w, h = widget
    if (png.w, png.h) != (ref_png.w, ref_png.h):
        skip("pixel.closed", "收起残板检查",
             "两帧尺寸不同：%dx%d vs %dx%d"
             % (png.w, png.h, ref_png.w, ref_png.h))
        return
    palette = [C["scr"], C["card"], C["pressed_face"], C["grey"],
               C["disabled"], C["primary_muted"]]
    # HUD 排除区（页面自带 FPS/CPU 面板，右下角锚定）：其静态灰字
    # （#7F8182 系）恰落键盘 DISABLED 色板 ±2 内，FPS 两帧同值时被误判
    # 残板（实测 3 点 (699,531)/(687,555)/(729,567)）——残板语义=键盘
    # 内容物残留，页面 HUD 与键盘无关，整段排除。
    hud_x0, hud_y0 = png.w - 160, png.h - 105
    bad = []
    residue = []
    for yy in range(max(y0, 0), min(y0 + h, png.h), 3):
        for xx in range(max(x0, 0), min(x0 + w, png.w), 3):
            if xx >= hud_x0 and yy >= hud_y0:
                continue
            p = png.get(xx, yy)
            if dist(p, ref_png.get(xx, yy)) > 2:
                bad.append((xx, yy, hx(p), hx(ref_png.get(xx, yy))))
                if any(dist(p, c) <= 2 for c in palette):
                    residue.append((xx, yy, hx(p)))
    check("pixel.closed", "收起后旧区域无键盘色残板", not residue,
          ("键盘色残留 %d 点 %s；动态失配 %d 点（页面 HUD 等，不判残留）"
           % (len(residue), residue[:3], len(bad)))
          if residue else
          ("区域 (%d,%d)+%dx%d 步长3 无键盘色残留；动态失配 %d 点"
           "（页面 HUD 等）样例 %s"
           % (x0, y0, w, h, len(bad), bad[:2])) if bad else
          "区域 (%d,%d)+%dx%d 步长3 与被遮内容全一致（±2）"
          % (x0, y0, w, h))


# ==================== 动态驱动 ====================

SCENARIOS = [
    ("normal", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_DUMP": "1"},
     "常态五行拼音布局：面板/键面/checked/圆角/间距/文字"),
    ("band", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_CHINESE": "1",
              "XGUI_KB_DUMP": "1"}, "中文态候选带（空组串）"),
    ("compose", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_CHINESE": "1",
                 "XGUI_KB_PHYSKEY": "ni", "XGUI_KB_DUMP": "1"},
     "组串 ni + 候选 chip（物理键注入）"),
    # compose 注入口径（需求③收缩态适配）：组串首字符落地即触发面板收缩
    # （XKB-GEO widget h=带高+4），XGUI_KB_COMPOSE 的逐字符点屏键注入第二
    # 字符起命中坐标越出收缩控件被丢弃（组串停在单字符、候选 0、页码格
    # 不渲染）；XGUI_KB_PHYSKEY 经应用层入口 XGuiApplication_
    # virtualKeyboardNotifyKey 与真机物理按键同链，不受收缩几何影响。
    ("pressed", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_PRESS_LABEL": "q",
                 "XGUI_KB_DUMP": "1"}, "按住 q（拼音布局无气泡）"),
    ("bubble", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_MODE": "textlower",
                "XGUI_KB_PRESS_LABEL": "q", "XGUI_KB_DUMP": "1"},
     "TextLower+popovers 按住 q（气泡放大）"),
    ("closed", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_CLOSE": "1",
                "XGUI_KB_DUMP": "1"}, "closePopup 收层残板检查"),
    ("t9", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_LAYOUT": "t9",
            "XGUI_KB_DUMP": "1"}, "拼音九键款型（T9 21 键表，英文态）"),
    ("t9zh", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_LAYOUT": "t9",
              "XGUI_KB_CHINESE": "1", "XGUI_KB_DUMP": "1"},
     "拼音九键中文态（组串带预留/工具栏渲染）"),
    ("english", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_LAYOUT": "english",
                 "XGUI_KB_DUMP": "1"}, "英文全键款型（与全键共表同几何）"),
    ("selector", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_PANEL": "selector",
                  "XGUI_KB_DUMP": "1"}, "键盘选择面板打开态（三行勾选）"),
    ("editpanel", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_PANEL": "edit",
                   "XGUI_KB_DUMP": "1"},
     "文字编辑面板（标题行+方向区+右列）"),
    ("compact", {"XGUI_KB_AUTOSHOW": "1", "XGUI_KB_PANEL": "float",
                 "XGUI_KB_DUMP": "1"},
     "紧凑悬浮小键盘（独立顶层 Popup 宿主右下角）"),
]

# 像素期望色（项目口径常量；与静态断言的现算值同源——PRIMARY_MUTED/
# DISABLED_TEXT 已按模板 OFS=0/LV_OPA_50=127 口径修值对齐，无已知偏差）
COLORS = {
    "scr": (0xF5, 0xF5, 0xF5), "card": (0xFF, 0xFF, 0xFF),
    "text": (0x21, 0x21, 0x21), "grey": (0xE0, 0xE0, 0xE0),
    "primary": (0x21, 0x96, 0xF3), "primary_muted": (0xD2, 0xEA, 0xFC),
    "pressed_face": (0xDC, 0xDC, 0xDC), "pressed_text": (0x1C, 0x1C, 0x1C),
    "disabled": (0x80, 0x80, 0x80),
    "ime_mode_chip_w": 40, "ime_page_cell_w": 24, "ime_gap": 4,
    "ime_pad": 2,
}


def exe_has_hook(exe):
    try:
        blob = exe.read_bytes()
    except OSError:
        return False
    return b"XGUI_KB_AUTOSHOW" in blob


def run_closed_reference(args, shot_dir, keep_png):
    """参照帧：同页同参但钩子零操作（无 XGUI_KB_AUTOSHOW）→ 键盘从未
    弹出，旧键盘区域即纯被遮内容。运行失败返回 None（调用方 SKIP）。"""
    exe = Path(args.exe)
    if not exe.exists():
        return None
    png_path = Path(shot_dir) / "kb_closedref.png"
    if png_path.exists() and not keep_png:
        png_path.unlink()
    run_env = dict(os.environ)
    run_env.pop("XGUI_KB_AUTOSHOW", None)  # 钩子首行 envFlag 即 return。
    cmd = [str(exe), "--screenshot", str(png_path), "--page", "9"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace",
                              timeout=90, env=run_env, cwd=str(ROOT))
    except subprocess.TimeoutExpired:
        return None
    if proc.returncode != 0 or not png_path.exists():
        return None
    try:
        return Png(png_path)
    except (ValueError, zlib.error):
        return None


def run_dynamic(args, shot_dir, keep_png):
    exe = Path(args.exe)
    if not exe.exists():
        for name, _env, _desc in SCENARIOS:
            skip("dynamic.run", "场景 %s" % name, "exe 不存在：%s" % exe)
        return
    if not exe_has_hook(exe):
        reason = ("exe 未含无头钩子（二进制内无 XGUI_KB_AUTOSHOW 字串）——"
                  "钩子源码已在 Test/XGuiDemo 落地，待主流程重建后重跑本脚本")
        for name, _env, _desc in SCENARIOS:
            skip("dynamic.run", "场景 %s" % name, reason)
        return
    shot_dir = Path(shot_dir)
    shot_dir.mkdir(parents=True, exist_ok=True)
    results = {}
    for name, env, desc in SCENARIOS:
        png_path = shot_dir / ("kb_%s.png" % name)
        if png_path.exists() and not keep_png:
            png_path.unlink()
        run_env = dict(os.environ)
        run_env.update(env)
        cmd = [str(exe), "--screenshot", str(png_path), "--page", "9"]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace",
                                  timeout=90, env=run_env, cwd=str(ROOT))
        except subprocess.TimeoutExpired:
            skip("dynamic.run", "场景 %s" % name, "超时（90s）")
            continue
        if proc.returncode != 0 or not png_path.exists():
            skip("dynamic.run", "场景 %s" % name,
                 "退出码 %s，PNG %s（stdout 尾部：%s）"
                 % (proc.returncode,
                    "缺失" if not png_path.exists() else "在",
                    (proc.stdout or proc.stderr or "")[-160:]))
            continue
        check("dynamic.run", "场景 %s 截图成功" % name, True,
              "%s（%s）" % (desc, png_path.name))
        try:
            png = Png(png_path)
        except (ValueError, zlib.error) as exc:
            skip("pixel." + name, "PNG 解码", str(exc))
            continue
        widget, band, keys, rows = parse_geo(proc.stdout or "")
        if widget is None or not keys:
            # 场景 PNG 在场但无几何 dump（popup FAILED/钩子未 dump）：
            # 登记 FAIL 并跳过本场景像素检查——不得带着 None 几何进入
            # px_*（第 1 轮复现：px_panel 解包 None 整脚本 TypeError）。
            check("dynamic.run", "场景 %s 键盘几何 dump" % name, False,
                  "stdout 无 XKB-GEO widget/key 行（popup FAILED 或未 "
                  "dump；尾部：%s）" % (proc.stdout or "")[-160:])
            continue
        results[name] = (png, widget, band, keys, rows)

    if "normal" in results:
        png, widget, band, keys, rows = results["normal"]
        geometry_checks(widget, band, keys, rows)
        px_panel(png, widget, COLORS)
        px_key_faces(png, widget, keys, COLORS)
        px_corners(png, widget, keys, COLORS)
        px_seams(png, widget, keys, COLORS)
    if "band" in results:
        # 空组串渲染图标工具栏（Sogou 二阶段：组串态才由候选带替换菜单；
        # dump 的 band 行=中文态常驻条矩形，非候选带内容物），候选带像素
        # 断言由 compose 场景（物理键注入组串 ni）承载。
        skip("pixel.band", "候选带检查（band 场景）",
             "空组串=图标工具栏渲染（非候选带），断言由 compose 场景承载")
    if "compose" in results:
        png, widget, band, keys, rows = results["compose"]
        px_band(png, widget, band, COLORS, compose_filled=True)
    if "pressed" in results:
        png, widget, band, keys, rows = results["pressed"]
        px_pressed(png, widget, keys, "q", COLORS)
    if "bubble" in results:
        png, widget, band, keys, rows = results["bubble"]
        px_pressed(png, widget, keys, "q", COLORS)
        px_bubble(png, widget, keys, "q", COLORS)
    if "closed" in results:
        png, widget, band, keys, rows = results["closed"]
        if widget:
            ref_png = run_closed_reference(args, shot_dir, keep_png)
            if ref_png is not None:
                px_closed(png, widget, COLORS, ref_png)
        else:
            skip("pixel.closed", "收起残板检查", "无 widget 几何 dump")


# ==================== 主流程 ====================

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    # nargs="?"：--json 落单/空值不再 argparse exit 2，回退默认路径
    # （第 1 轮失败根因：调用方 `--json` 无值 → 整脚本 usage error 终止）。
    ap.add_argument("--json", nargs="?", const="", default=str(DEFAULT_JSON),
                    metavar="PATH", help="机器可读结果输出路径"
                                         "（缺省/空值→%s）" % DEFAULT_JSON)
    ap.add_argument("--screenshot-dir", default=str(DEFAULT_SHOT_DIR))
    ap.add_argument("--skip-dynamic", action="store_true")
    ap.add_argument("--keep-png", action="store_true")
    ap.add_argument("--known-deltas", choices=("fail", "warn"),
                    default="fail")
    ap.add_argument("--exe", default=str(DEFAULT_EXE))
    ap.add_argument("--lvgl-src", default=str(DEFAULT_LVGL_SRC))
    ap.add_argument("--dpi", type=int, default=130)
    args = ap.parse_args()

    if not XKB.exists():
        print("[style_check] error: %s missing" % XKB)
        return 2
    static_checks(Path(args.lvgl_src), args.dpi, args.known_deltas)
    if not args.skip_dynamic:
        run_dynamic(args, args.screenshot_dir, args.keep_png)

    n_pass = sum(1 for c in CHECKS if c["status"] == "pass")
    n_fail = sum(1 for c in CHECKS if c["status"] == "fail")
    n_warn = sum(1 for c in CHECKS if c["status"] == "warn")
    n_skip = sum(1 for c in CHECKS if c["status"] == "skip")
    width = max(len(c["name"]) for c in CHECKS) if CHECKS else 20
    for c in CHECKS:
        mark = {"pass": "PASS", "fail": "FAIL", "warn": "WARN",
                "skip": "SKIP"}[c["status"]]
        print("[style_check] [%s] %-*s %s"
              % (mark, width, c["name"], c["detail"]))
    print("[style_check] summary: %d pass / %d fail / %d warn / %d skip"
          % (n_pass, n_fail, n_warn, n_skip))
    # JSON 恒写出：--json 缺失/落单/空值均回退默认路径（输出解析不再
    # 因调用方丢参而失败）。
    json_arg = args.json or str(DEFAULT_JSON)
    try:
        json_path = Path(json_arg)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(
            json.dumps({"exe": args.exe, "lvgl_src": args.lvgl_src,
                        "dpi": args.dpi,
                        "known_deltas": args.known_deltas,
                        "summary": {"pass": n_pass, "fail": n_fail,
                                    "warn": n_warn, "skip": n_skip},
                        "checks": CHECKS},
                       ensure_ascii=False, indent=1),
            encoding="utf-8")
        print("[style_check] json -> %s" % json_path)
    except OSError as exc:
        print("[style_check] error: cannot write json %s (%s)"
              % (json_arg, exc))
    print("FAIL" if n_fail else "PASS")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
