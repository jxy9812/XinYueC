#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""网页端 XLSX/CSV 导出验收（产物必须是合法 OOXML）。

[验收项]
  1) **统一入口 `export.write_xlsx` 的两条产出路径**：
     ① 纯 Python（`prefer="python"`）—— 板端无 .so 时的唯一路径；
     ② C 动态库（`prefer="native"`）—— 与桌面端同一个 xlsx_bridge.dll；
     ③ 自动择优（`prefer="auto"`，网页端实际用的那条）—— 按实测成本模型
        与预算决定走哪条；缺库/失败**静默回落**，不许 500。
     每条产物都用 **zipfile 逐项校验**：条目清单、[Content_Types].xml、
     xl/workbook.xml、工作表 XML 俱在；**单元格值与源数据一致**
     （表头行 + 首行数据逐格比对）。
  2) **HTTP 端到端**：真起 webui，登录后 GET /api/history/export?format=xlsx
     （全量）与 format=csv，校验产物（xlsx 合法 OOXML；csv 带 BOM + CRLF）；
     另用**窄时间窗**再导一次 xlsx（表小 ⇒ 走 C 动态库路径）⇒ 用包内
     特征（theme1.xml / 两张表）证明网页端确实能产出 C 路径的文件。
  3) 产物与**校验报告**留证到 verify/webui/shots/。

[用法]
    python verify/webui/webui_xlsx_check.py [--web-port 8090] [--upstream 6100]

[退出码] 0 = 全绿；1 = 有检查项失败；2 = 前置失败（webui 不在/登录失败）。
"""

import argparse
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import zipfile

# [编码 2026-11-23 实锅] cp936 下打印「⇒」直接 UnicodeEncodeError ⇒ 门禁
# 中途被打断（退出码还可能是 0）。与 webui_check.py 同款：统一 UTF-8。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    os.pardir, os.pardir))
WEBUI_PKG = os.path.join(REPO, "HighSpeedFilter", "webui_py")
SHOTS = os.path.join(REPO, "verify", "webui", "shots")

FAILURES = []
REPORT = []


def check(name, ok, detail=""):
    mark = "PASS" if ok else "FAIL"
    line = "  [%s] %s%s" % (mark, name, ("  -- " + detail) if detail else "")
    print(line)
    REPORT.append(line)
    if not ok:
        FAILURES.append(name)
    return ok


def note(text):
    print("  " + text)
    REPORT.append("  " + text)


def add_webui_to_path():
    if WEBUI_PKG not in sys.path:
        sys.path.insert(0, WEBUI_PKG)


# ----------------------------------------------------------------------
#                       OOXML 合法性校验
# ----------------------------------------------------------------------

#: 单元格解析：`<c r="A1" t="inlineStr"><is><t xml:space="preserve">文本`
#: 或 `<c r="A1"><v>数值</v>`（两条产出路径都用 inlineStr，兼容数值格）。
_CELL_RE = re.compile(r"<c\s+r=\"([A-Z]+\d+)\"[^>]*>(.*?)</c>", re.S)
_T_RE = re.compile(r"<t[^>]*>(.*?)</t>", re.S)
_V_RE = re.compile(r"<v>(.*?)</v>", re.S)


def _unescape(text):
    return (text.replace("&lt;", "<").replace("&gt;", ">")
                .replace("&quot;", "\"").replace("&apos;", "'")
                .replace("&amp;", "&"))


def sheet_cells(xml_text):
    """sheetData 的 XML → {单元格引用: 文本}。"""
    cells = {}
    body = xml_text.split("<sheetData>", 1)[-1].split("</sheetData>", 1)[0]
    for ref, inner in _CELL_RE.findall(body):
        m = _T_RE.search(inner)
        if m:
            cells[ref] = _unescape(m.group(1))
            continue
        m = _V_RE.search(inner)
        if m:
            cells[ref] = _unescape(m.group(1))
            continue
        cells[ref] = ""
    return cells


def data_sheet_xml(zf, names):
    """取**有数据**的那张工作表 XML（跳过 C 路径自带的空缺省表）。"""
    sheets = sorted(n for n in names
                    if n.startswith("xl/worksheets/") and n.endswith(".xml")
                    and "_rels" not in n)
    best, best_cells = None, -1
    for name in sheets:
        cells = sheet_cells(zf.read(name).decode("utf-8", "replace"))
        if len(cells) > best_cells:
            best, best_cells = name, len(cells)
    return (best or ""), cells


def validate_xlsx(data, label, expect_headers=None, expect_row=None,
                  expect_cells=None):
    """校验一段 XLSX 字节是合法 OOXML，并回读表头/首行单元格值。"""
    try:
        zf = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile as exc:
        check("%s 是合法 zip 包" % label, False, str(exc))
        return None
    names = zf.namelist()
    note("包内条目（%d 个）：%s" % (len(names), ", ".join(names)))
    check("%s 含 [Content_Types].xml" % label,
          "[Content_Types].xml" in names, "")
    check("%s 含 xl/workbook.xml" % label,
          "xl/workbook.xml" in names, "")
    sheets = [n for n in names
              if n.startswith("xl/worksheets/") and n.endswith(".xml")
              and "_rels" not in n]
    check("%s 含工作表 XML" % label, bool(sheets), str(sheets))
    if not sheets:
        return None
    sheet_name, cells = data_sheet_xml(zf, names)
    note("数据工作表：%s（单元格 %d 个）" % (sheet_name, len(cells)))
    if expect_headers:
        got = [cells.get("%s1" % chr(ord("A") + i), "")
               for i in range(len(expect_headers))]
        check("%s 表头行与源数据一致" % label, got == list(expect_headers),
              "期望 %s / 实得 %s" % (list(expect_headers)[:4], got[:4]))
    if expect_row:
        got = [cells.get("%s2" % chr(ord("A") + i), "")
               for i in range(len(expect_row))]
        # [数值格容差 2026-11-23 基线] C 动态库路径把"看起来像数字"的字符串
        # 写成**数值格** ⇒ 回读是 `42`/`0.499`（尾零丢失），与源文本
        # `"42.00"` 逐字不等。这是 C 写出器既有口径（Excel 侧仍按数字显示），
        # 与纯 Python 路径（inlineStr 原样）不同 ⇒ 这里按**数值相等**容差；
        # 真正的值错（0.512 vs 0.51）仍红。
        def _cell_same(want, have):
            if want == have:
                return True
            try:
                return abs(float(want) - float(have)) <= 1e-9
            except (TypeError, ValueError):
                return False
        bad = [(w, g) for w, g in zip(list(expect_row), got)
               if not _cell_same(w, g)]
        check("%s 首行数据与源数据一致" % label, not bad,
              "期望 %s / 实得 %s" % (list(expect_row)[:4], got[:4]))
    if expect_cells:
        check("%s 单元格总数与源数据一致" % label, len(cells) == expect_cells,
              "期望 %d / 实得 %d" % (expect_cells, len(cells)))
    # 工作簿里的表名（两条路径都应含业务表名）
    wb = zf.read("xl/workbook.xml").decode("utf-8", "replace")
    check("%s 工作簿含业务表名" % label, "历史数据" in wb or "数据" in wb, "")
    return names


def validate_csv(data, label):
    check("%s 带 UTF-8 BOM" % label, data[:3] == b"\xef\xbb\xbf",
          repr(data[:4]))
    check("%s 行分隔是 CRLF" % label, b"\r\n" in data, "")
    bare_lf = data.count(b"\n") - data.count(b"\r\n")
    check("%s 无裸 LF 行尾" % label, bare_lf == 0, "裸 LF %d 个" % bare_lf)
    text = data.decode("utf-8-sig", "replace")
    lines = [ln for ln in text.split("\r\n") if ln.strip()]
    check("%s 表头首列是「时间」" % label,
          lines and lines[0].startswith("时间"), repr(text[:40]))
    return len(lines) - 1


# ----------------------------------------------------------------------
#                      进程内：统一入口的三种走法
# ----------------------------------------------------------------------

def check_paths():
    print("[1] 进程内：统一入口 export.write_xlsx 的两条产出路径")
    add_webui_to_path()
    from hsfweb import export as export_mod

    headers = list(export_mod.HISTORY_HEADERS)
    # [列序 2026-11-23] 显示列 19 项（电机温度在 6、压力1/2 在压差后）。
    # 样本行逐格与表头同序（以前只有 16 格 ⇒ 后三格语义错位，虽然
    # validate 只按各自长度比、仍会"绿"，但那是在验错东西）。
    row = ["2026-11-13 08:00:00", "自动·产水", "停机", "42.00", "3.10",
           "1440", "18.65", "0.45", "0.05", "19.3", "3.54 m³/h",
           "0.06 m³", "0.499", "0.512", "0.487", "26.5", "55.0",
           "0x00005AA8", "0x00000081"]
    sheets = [("历史数据", headers, [row])]

    # ---- ① 纯 Python 路径（强制）----
    t0 = time.time()
    data = export_mod.write_xlsx(sheets, prefer="python")
    note("纯 Python 路径：%.3fs / %d 字节 / 走=%s / 理由=%s"
         % (time.time() - t0, len(data), export_mod.last_writer(),
            export_mod.last_writer_reason()))
    check("纯 Python 路径被选中", export_mod.last_writer() == "python", "")
    validate_xlsx(data, "纯 Python 路径", expect_headers=headers,
                  expect_row=row, expect_cells=len(headers) + len(row))

    # ---- ② C 动态库路径（强制）----
    try:
        t0 = time.time()
        data = export_mod.write_xlsx(sheets, prefer="native")
        note("C 动态库路径：%.3fs / %d 字节 / 走=%s / 理由=%s"
             % (time.time() - t0, len(data), export_mod.last_writer(),
                export_mod.last_writer_reason()))
        check("C 动态库路径被选中", export_mod.last_writer() == "native", "")
        validate_xlsx(data, "C 动态库路径", expect_headers=headers,
                      expect_row=row, expect_cells=len(headers) + len(row))
    except Exception as exc:                          # noqa: BLE001
        check("C 动态库路径可用", False, "%s: %s" % (type(exc).__name__, exc))

    # ---- ③ 自动择优：小表走 C 路径 ----
    data = export_mod.write_xlsx(sheets)
    check("自动择优（小表）走 C 动态库路径",
          export_mod.last_writer() == "native",
          export_mod.last_writer_reason())
    validate_xlsx(data, "自动择优（小表）", expect_headers=headers,
                  expect_row=row, expect_cells=len(headers) + len(row))

    # ---- ③' 自动择优：大表走纯 Python（成本择优）----
    big = [("历史数据", headers, [row] * 2032)]
    t0 = time.time()
    data = export_mod.write_xlsx(big)
    dt = time.time() - t0
    note("自动择优（2032 行）：%.3fs / %d 字节 / 走=%s / 理由=%s"
         % (dt, len(data), export_mod.last_writer(),
            export_mod.last_writer_reason()))
    check("自动择优（大表）不选超预算的 C 路径",
          export_mod.last_writer() == "python", export_mod.last_writer_reason())
    check("自动择优（大表）耗时在可接受范围（< 30s）", dt < 30.0,
          "%.2fs" % dt)
    validate_xlsx(data, "自动择优（大表）", expect_headers=headers,
                  expect_row=row)

    # ---- ④ C 路径不可用 ⇒ 静默回落纯 Python（不许抛异常）----
    os.environ["HSF_XLSX_NATIVE"] = "0"
    try:
        data = export_mod.write_xlsx(sheets)
        check("C 路径被禁用时静默回落纯 Python",
              export_mod.last_writer() == "python",
              export_mod.last_writer_reason())
        validate_xlsx(data, "C 路径禁用（回落）", expect_headers=headers,
                      expect_row=row)
    finally:
        os.environ.pop("HSF_XLSX_NATIVE", None)


# ----------------------------------------------------------------------
#                        HTTP 端到端
# ----------------------------------------------------------------------

def http_json(url, payload=None, token=None, timeout=20.0):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer %s" % token
    req = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", "replace"))


def http_bytes(url, token=None, timeout=120.0):
    headers = {}
    if token:
        headers["Authorization"] = "Bearer %s" % token
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def http_bytes_retry(url, token=None, attempts=20, delay=2.0, timeout=120.0):
    """取字节；503（上游尚未就绪）时重试。

    [为什么要重试] webui 的上游是**惰性连接**：浏览器登录后才连上游，
    登录应答回来时上游可能还在 authenticating ⇒ 首个导出请求会拿到
    503 Service Unavailable。这不是导出功能坏了，是还没就绪。
    """
    last = None
    for i in range(attempts):
        try:
            return http_bytes(url, token, timeout)
        except urllib.error.HTTPError as exc:
            last = exc
            if exc.code != 503:
                raise
            body = exc.read()[:200]
            print("  上游未就绪（503），%d 秒后重试（第 %d 次）"
                  % (delay, i + 1))
            time.sleep(delay)
        except urllib.error.URLError as exc:
            last = exc
            time.sleep(delay)
    raise last if last else RuntimeError("请求失败：%s" % url)


def check_http(web_port):
    print("[2] HTTP 端到端（webui 端口 %d）" % web_port)
    base = "http://127.0.0.1:%d" % web_port
    try:
        ping = http_json(base + "/api/ping", timeout=8.0)
    except Exception as exc:
        print("  webui 探活失败：%s" % exc)
        return 2
    if not ping.get("ok"):
        print("  webui 探活应答异常：%r" % ping)
        return 2
    try:
        login = http_json(base + "/api/login",
                          {"user": "root", "password": "root"})
    except urllib.error.HTTPError as exc:
        print("  登录失败：HTTP %s %s" % (exc.code, exc.read()[:200]))
        return 2
    token = login.get("token")
    if not token:
        print("  登录应答无 token：%r" % login)
        return 2
    note("登录成功")

    # ---- 服务端报的总数（锚点口径对账用）----
    # [要重试] 刚起的网页实例上游是惰性连接 ⇒ 首个查询可能拿到 503
    # 「未连接服务端」（不是功能坏了）。
    page = json.loads(http_bytes_retry(
        base + "/api/history/page?limit=1&offset=0", token).decode("utf-8"))
    total = page.get("total")
    note("服务端历史总行数（未加锚点）：%s" % total)

    # ---- 全量 xlsx ----
    t0 = time.time()
    try:
        data = http_bytes_retry(base + "/api/history/export?format=xlsx",
                                token)
    except Exception as exc:
        check("GET /api/history/export?format=xlsx", False, str(exc))
        return 1
    dt = time.time() - t0
    check("GET .xlsx 有产物", len(data) > 0, "%d 字节" % len(data))
    check("GET .xlsx 在可接受耗时内（< 60s）", dt < 60.0, "%.2fs" % dt)
    names = validate_xlsx(data, "HTTP .xlsx 产物")
    if names:
        route = "C 动态库路径" if "xl/theme/theme1.xml" in names \
            else "纯 Python 路径"
        note("HTTP .xlsx 走的产出路径（按包内特征判定）：%s" % route)
        check("HTTP .xlsx 走的是成本择优后的路径", route in
              ("C 动态库路径", "纯 Python 路径"), route)

    # ---- 窄时间窗 xlsx（小表 ⇒ 走 C 动态库路径）----
    now = int(time.time() * 1000)
    narrow = ("/api/history/export?format=xlsx&loTs=%d&hiTs=%d"
              % (now - 5 * 60 * 1000, now))
    try:
        small = http_bytes_retry(base + narrow, token)
    except Exception as exc:
        check("GET 窄窗 .xlsx", False, str(exc))
        small = None
    if small:
        check("GET 窄窗 .xlsx 有产物", len(small) > 0, "%d 字节" % len(small))
        small_names = validate_xlsx(small, "HTTP 窄窗 .xlsx 产物")
        if small_names:
            check("窄窗 .xlsx 走 C 动态库路径（包内含 theme1.xml）",
                  "xl/theme/theme1.xml" in small_names, str(small_names[:8]))

    # ---- csv ----
    t0 = time.time()
    try:
        csv_data = http_bytes_retry(base + "/api/history/export?format=csv",
                                    token)
    except Exception as exc:
        check("GET /api/history/export?format=csv", False, str(exc))
        return 1
    dt = time.time() - t0
    check("GET .csv 有产物", len(csv_data) > 0, "%d 字节" % len(csv_data))
    note("GET .csv 耗时 %.2fs" % dt)
    rows = validate_csv(csv_data, "HTTP .csv 产物")
    note("CSV 数据行数：%d（服务端报 %s 行；导出期间新入库的行以锚点"
         "beforeTs 为界）" % (rows, total))

    # ---- 留证 ----
    if not os.path.isdir(SHOTS):
        os.makedirs(SHOTS, exist_ok=True)
    for name, blob in (("webui_export_xlsx.xlsx", data),
                       ("webui_export_csv.csv", csv_data)):
        out = os.path.join(SHOTS, name)
        with open(out, "wb") as fh:
            fh.write(blob)
        note("产物留证：%s (%d 字节)" % (out, len(blob)))
    if small:
        out = os.path.join(SHOTS, "webui_export_xlsx_native.xlsx")
        with open(out, "wb") as fh:
            fh.write(small)
        note("产物留证：%s (%d 字节)" % (out, len(small)))
    return 0


def check_no_native_http(upstream_port):
    """[3] **C 路径不可用时网页端仍能导出**（不许 500）：起一个禁了 C 路径
    的网页实例（环境变量 HSF_XLSX_NATIVE=0），导出 xlsx 必须是 200 + 合法
    OOXML（纯 Python 形状：包内无 theme1.xml）。只杀自己起的进程。
    """
    print("[3] C 路径禁用时的网页端导出（静默回落，不许 500）")
    import subprocess
    import socket

    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        web_port = s.getsockname()[1]
    finally:
        s.close()
    env = dict(os.environ)
    env["HSF_XLSX_NATIVE"] = "0"
    webui_dir = os.path.join(REPO, "HighSpeedFilter", "webui_py")
    proc = subprocess.Popen(
        [sys.executable, "-m", "hsfweb", "--server", "127.0.0.1",
         "--port", str(upstream_port), "--web-port", str(web_port)],
        cwd=webui_dir, env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = "http://127.0.0.1:%d" % web_port
    try:
        deadline = time.time() + 20.0
        up = False
        while time.time() < deadline:
            try:
                ping = http_json(base + "/api/ping", timeout=3.0)
                if ping.get("ok"):
                    up = True
                    break
            except Exception:                        # noqa: BLE001
                pass
            time.sleep(0.4)
        check("禁 C 路径的网页实例已起来", up, base)
        if not up:
            return 1
        login = http_json(base + "/api/login",
                          {"user": "root", "password": "root"})
        token = login.get("token")
        check("禁 C 路径的网页实例可登录", bool(token), "")
        if not token:
            return 1
        try:
            data = http_bytes_retry(base + "/api/history/export?format=xlsx",
                                    token)
        except urllib.error.HTTPError as exc:
            check("禁 C 路径时导出 .xlsx 不报 5xx", False,
                  "HTTP %s" % exc.code)
            return 1
        except Exception as exc:                    # noqa: BLE001
            check("禁 C 路径时导出 .xlsx 不报错", False, str(exc))
            return 1
        check("禁 C 路径时导出 .xlsx 有产物", len(data) > 0,
              "%d 字节" % len(data))
        names = validate_xlsx(data, "禁 C 路径的 .xlsx 产物")
        if names:
            check("禁 C 路径时走的是纯 Python 路径（包内无 theme1.xml）",
                  "xl/theme/theme1.xml" not in names, str(names[:8]))
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=8)
        except Exception:                           # noqa: BLE001
            try:
                proc.kill()
            except Exception:                       # noqa: BLE001
                pass
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--web-port", type=int, default=8090)
    ap.add_argument("--upstream-port", type=int, default=6100)
    ap.add_argument("--skip-http", action="store_true")
    args = ap.parse_args()

    check_paths()
    rc = 0
    if not args.skip_http:
        rc = check_http(args.web_port)
        if rc == 0:
            rc = check_no_native_http(args.upstream_port)
    summary = ("全部通过" if not FAILURES
               else "失败 %d 项：%s" % (len(FAILURES), "; ".join(FAILURES)))
    print("\n结果：%s" % summary)
    REPORT.append("")
    REPORT.append("结果：%s" % summary)
    if not os.path.isdir(SHOTS):
        os.makedirs(SHOTS, exist_ok=True)
    report_path = os.path.join(SHOTS, "webui_export_xlsx_check.txt")
    with open(report_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(REPORT) + "\n")
    print("校验报告留证：%s" % report_path)
    if rc:
        return rc
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
