#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""网页端导出对话框的**浏览器实跑**验收（格式选项真兑现）。

[为什么要有这一发] 后端端点能产出合法文件 ≠ 前端对话框的选项真兑现。
本脚本用 Edge（--headless=new + CDP）真开网页、**点「导出数据」菜单里的
格式项**、等浏览器真把文件下载下来，再逐项校验：

    ① 「导出 Excel 工作簿（.xlsx）」→ 下载物是合法 OOXML（zipfile 逐项
       校验：条目清单 / [Content_Types].xml / xl/workbook.xml / 工作表 XML
       / 表头行与首行数据）；
    ② 「导出 CSV（全部命中行）」→ 下载物带 UTF-8 BOM + CRLF 行尾。

[流程]
    后端登录取 token → `?authtoken=<token>#/page/9`（历史数据页）
    → 点「导出数据」→ 点菜单项 → 等下载目录出现文件 → 校验。

[用法]
    python verify/webui/webui_export_browser_check.py [--web-port 8090]

[退出码] 0 = 两个格式都下到且校验通过；1 = 有检查项失败；2 = 前置失败
（找不到 Edge / 登录失败 / 页面没起来）。
"""

import argparse
import glob
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
SHOTS = os.path.join(REPO, "verify", "webui", "shots")

sys.path.insert(0, HERE)
import cdp_client                                          # noqa: E402

#: msedge.exe 候选位置（同 web_sweep_edge.py 的口径）。
EDGE_CANDIDATES = (
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    r"C:/Program Files/Google/Chrome/Application/chrome.exe",
)

#: 历史数据页的页号（pages_registry：主界面0 … 历史数据9）。
HISTORY_PAGE = 9

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


def find_edge():
    for path in EDGE_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


def free_port():
    import socket
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def http_json(url, payload=None, timeout=20.0):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", "replace"))


# ----------------------------------------------------------------------
#                          产物校验（与后端脚本同口径）
# ----------------------------------------------------------------------

def validate_xlsx_file(path, label):
    with open(path, "rb") as fh:
        data = fh.read()
    try:
        zf = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile as exc:
        check("%s 是合法 zip 包" % label, False, str(exc))
        return None
    names = zf.namelist()
    note("%s 包内条目：%s" % (label, ", ".join(names)))
    check("%s 含 [Content_Types].xml" % label,
          "[Content_Types].xml" in names, "")
    check("%s 含 xl/workbook.xml" % label, "xl/workbook.xml" in names, "")
    sheets = [n for n in names if n.startswith("xl/worksheets/")
              and n.endswith(".xml") and "_rels" not in n]
    check("%s 含工作表 XML" % label, bool(sheets), str(sheets))
    blob = ""
    for name in names:
        if name.startswith("xl/worksheets/") or name.endswith("sharedStrings.xml"):
            blob += zf.read(name).decode("utf-8", "replace")
    check("%s 含表头值「时间」" % label, "时间" in blob, "")
    wb = zf.read("xl/workbook.xml").decode("utf-8", "replace")
    check("%s 工作簿含表名「历史数据」" % label, "历史数据" in wb, "")
    note("%s 大小 %d 字节；走 %s"
         % (label, len(data),
            "C 动态库路径（含 theme1.xml）" if "xl/theme/theme1.xml" in names
            else "纯 Python 路径"))
    # [数据行数] 供与 CSV 比对：XLSX 曾有 12000 行内存闸（超出会截断）⇒
    # 同一筛选下 CSV 行数会更多。流式写之后两者必须**相等**。
    rows = blob.count("<row ")
    note("%s 工作表行数 %d（含表头）" % (label, rows))
    return names, max(0, rows - 1)


def validate_csv_file(path, label):
    with open(path, "rb") as fh:
        data = fh.read()
    check("%s 带 UTF-8 BOM" % label, data[:3] == b"\xef\xbb\xbf",
          repr(data[:4]))
    check("%s 行分隔是 CRLF" % label, b"\r\n" in data, "")
    text = data.decode("utf-8-sig", "replace")
    lines = [ln for ln in text.split("\r\n") if ln.strip()]
    check("%s 表头首列是「时间」" % label,
          bool(lines) and lines[0].startswith("时间"), repr(text[:40]))
    note("%s 大小 %d 字节 / 数据行 %d" % (label, len(data), max(0, len(lines) - 1)))
    return len(lines) - 1


# ----------------------------------------------------------------------
#                            浏览器驱动
# ----------------------------------------------------------------------

def click_export_menu(cdp, menu_text):
    """点「导出数据」→ 再点菜单里含 menu_text 的那一项。"""
    opened = cdp.evaluate(
        "(function(){var bs=Array.prototype.slice.call("
        "document.querySelectorAll('button'));"
        "var b=bs.filter(function(x){return (x.textContent||'').indexOf("
        "'导出数据')>=0;})[0];"
        "if(!b){return 'no-button';} b.click(); return 'clicked';})()")
    if opened != "clicked":
        return False, "找不到「导出数据」按钮（%s）" % opened
    time.sleep(0.8)
    picked = cdp.evaluate(
        "(function(){var its=Array.prototype.slice.call("
        "document.querySelectorAll('button.menu-item'));"
        "var it=its.filter(function(x){return (x.textContent||'').indexOf("
        "%s)>=0;})[0];"
        "if(!it){return 'no-item';} it.click(); return 'picked';})()"
        % json.dumps(menu_text))
    return picked == "picked", "菜单项点击结果：%s" % picked


def wait_download(download_dir, suffix, timeout_s=90.0):
    """等下载目录里出现指定后缀的文件（返回路径）。"""
    deadline = time.time() + timeout_s
    seen = set()
    while time.time() < deadline:
        for path in glob.glob(os.path.join(download_dir, "*" + suffix)):
            if path.endswith(".crdownload"):
                continue
            if os.path.getsize(path) > 0:
                return path
        for path in glob.glob(os.path.join(download_dir, "*")):
            seen.add(os.path.basename(path))
        time.sleep(0.5)
    note("下载目录现有文件：%s" % ", ".join(sorted(seen)) or "（空）")
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--web-port", type=int, default=8090)
    args = ap.parse_args()

    edge = find_edge()
    if edge is None:
        print("[export_browser] 找不到浏览器（%s）" % (EDGE_CANDIDATES,),
              file=sys.stderr)
        return 2

    base = "http://127.0.0.1:%d" % args.web_port
    try:
        login = http_json(base + "/api/login",
                          {"user": "root", "password": "root"})
    except Exception as exc:                          # noqa: BLE001
        print("[export_browser] 登录失败：%s" % exc, file=sys.stderr)
        return 2
    token = login.get("token")
    if not token:
        print("[export_browser] 登录应答无 token", file=sys.stderr)
        return 2
    note("后端登录成功（token 已取）")

    profile = tempfile.mkdtemp(prefix="edge_export_")
    download_dir = tempfile.mkdtemp(prefix="edge_dl_")
    port = free_port()
    url = "%s/?authtoken=%s#/page/%d" % (base, token, HISTORY_PAGE)
    cmd = [edge, "--headless=new", "--disable-gpu", "--no-first-run",
           "--user-data-dir=" + profile,
           "--remote-debugging-port=%d" % port,
           "--window-size=1280,800", url]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    cdp = None
    try:
        ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
        cdp = cdp_client.Cdp(ws_url, timeout=60.0)
        cdp.call("Page.enable")
        cdp.call("Runtime.enable")
        # 下载落盘到临时目录（blob + a[download] 触发）。
        try:
            cdp.call("Browser.setDownloadBehavior",
                     {"behavior": "allow", "downloadPath": download_dir,
                      "eventsEnabled": True})
        except Exception:                           # noqa: BLE001
            cdp.call("Page.setDownloadBehavior",
                     {"behavior": "allow", "downloadPath": download_dir})
        # 等页面挂载（历史页有内容 ⇒ 导出按钮在）。
        deadline = time.time() + 25.0
        ready = False
        while time.time() < deadline:
            n = cdp.evaluate(
                "(document.getElementById('page-host')||{}).childElementCount")
            if isinstance(n, (int, float)) and n > 0:
                ready = True
                break
            time.sleep(0.4)
        check("历史数据页已挂载", ready, url)
        time.sleep(2.0)          # 等首批数据与按钮启用

        # ---- ① 导出 Excel 工作簿 ----
        ok, detail = click_export_menu(cdp, "导出 Excel")
        check("点开「导出数据」菜单并选「导出 Excel 工作簿」", ok, detail)
        path = wait_download(download_dir, ".xlsx")
        check("浏览器下载到 .xlsx", path is not None,
              path or "下载目录：%s" % download_dir)
        xlsx_rows = None
        if path:
            _names, xlsx_rows = validate_xlsx_file(path, "浏览器下载的 .xlsx")
            out = os.path.join(SHOTS, "webui_browser_export.xlsx")
            shutil.copyfile(path, out)
            note("产物留证：%s" % out)

        # ---- ② 导出 CSV ----
        ok, detail = click_export_menu(cdp, "导出 CSV")
        check("点开「导出数据」菜单并选「导出 CSV」", ok, detail)
        path = wait_download(download_dir, ".csv")
        check("浏览器下载到 .csv", path is not None,
              path or "下载目录：%s" % download_dir)
        csv_rows = None
        if path:
            csv_rows = validate_csv_file(path, "浏览器下载的 .csv")
            out = os.path.join(SHOTS, "webui_browser_export.csv")
            shutil.copyfile(path, out)
            note("产物留证：%s" % out)

        # ---- ③ XLSX 与 CSV 行数一致（XLSX 不再有 12000 行内存闸）----
        # [为什么锁这条] 旧实现：XLSX 攒内存 ⇒ 12000 行上限（超出**截断**）
        # ⇒ 同一筛选下 CSV 导出的行比 XLSX 多 ⇒ 用户拿到的是**少了一截**
        # 的 Excel（且 Excel 里看不出被截）。流式写之后两者必须相等。
        if xlsx_rows is not None and csv_rows is not None:
            check("XLSX 与 CSV 数据行数一致（XLSX 不再截断）",
                  xlsx_rows == csv_rows,
                  "xlsx=%d csv=%d" % (xlsx_rows, csv_rows))
    except Exception as exc:                        # noqa: BLE001
        check("浏览器实跑", False, "%s: %s" % (type(exc).__name__, exc))
    finally:
        if cdp is not None:
            try:
                cdp.close()
            except Exception:                       # noqa: BLE001
                pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True,
                           env=dict(os.environ, MSYS_NO_PATHCONV="1"))
        shutil.rmtree(profile, ignore_errors=True)
        shutil.rmtree(download_dir, ignore_errors=True)

    summary = ("全部通过" if not FAILURES
               else "失败 %d 项：%s" % (len(FAILURES), "; ".join(FAILURES)))
    print("\n结果：%s" % summary)
    REPORT.append("")
    REPORT.append("结果：%s" % summary)
    if not os.path.isdir(SHOTS):
        os.makedirs(SHOTS, exist_ok=True)
    report_path = os.path.join(SHOTS, "webui_browser_export_check.txt")
    with open(report_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(REPORT) + "\n")
    print("校验报告留证：%s" % report_path)
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
