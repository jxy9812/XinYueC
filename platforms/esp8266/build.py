#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP8266 NONOS3 固件一键构建 / 烧录脚本
==========================================

用法（在仓库任意位置）::

    python platforms/esp8266/build.py              # 只构建
    python platforms/esp8266/build.py --flash      # 构建 + 烧录
    python platforms/esp8266/build.py --flash-only # 只烧录（跳过构建）
    python platforms/esp8266/build.py -c COM4      # 指定串口
    python platforms/esp8266/build.py --clean      # 先清掉所有中间产物再构建

特点：
  * 实时进度：构建阶段逐条打印被编译的 TU；烧录阶段显示百分比进度条
  * 自动定位工具链：优先用仓库内 platforms/esp8266/external/，其次环境变量，最后本机默认路径
  * 依赖自检：缺什么会明确告诉你缺在哪、怎么补
  * 跨平台：Windows / Linux / macOS 都能跑（Windows 走 git-bash，Linux 走原生 bash）

不需要 CMake —— 真正的编译由 platforms/esp8266/nonos/tools/build_fw_final.sh 驱动，
本脚本只负责调用 + 显示进度 + 烧录。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

# ----------------------------------------------------------------------------
# 路径
# ----------------------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent          # platforms/esp8266/
REPO_ROOT = SCRIPT_DIR.parent.parent                   # 仓库根（多一层 platforms/）
NONOS_DIR = SCRIPT_DIR / "nonos"                       # platforms/esp8266/nonos
TOOLS_DIR = NONOS_DIR / "tools"
BUILD_SCRIPT = TOOLS_DIR / "build_fw_final.sh"
BUILD_DIR = NONOS_DIR / "build"                       # 所有中间产物都在这
FW_BIN = NONOS_DIR / "fw_final.bin"
BUNDLED_DIR = SCRIPT_DIR / "external"                  # 外部依赖（原 platforms/esp8266/external）

# ----------------------------------------------------------------------------
# 终端着色（Windows 10+ 终端支持 ANSI）
# ----------------------------------------------------------------------------
if os.name == "nt":
    os.system("")  # 触发 VT100 支持
    # 让 Python 输出 UTF-8，否则中文在 GBK 控制台（cmd/PowerShell 传统编码）
    # 下全是乱码。VS 集成终端 / Windows Terminal 默认 UTF-8，可正常显示。
    for _s in (sys.stdout, sys.stderr):
        try:
            _s.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass

class C:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    CYAN = "\033[36m"
    GRAY = "\033[90m"


def _supports_color() -> bool:
    if os.environ.get("NO_COLOR"):
        return False
    return sys.stdout.isatty()


COLOR = _supports_color()


def c(text: str, color: str) -> str:
    if not COLOR:
        return text
    return f"{color}{text}{C.RESET}"


def info(msg: str) -> None:
    print(f"{c('==>', C.CYAN)} {msg}", flush=True)


def ok(msg: str) -> None:
    print(f"{c('  OK', C.GREEN)} {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"{c('  !!', C.YELLOW)} {msg}", flush=True)


def err(msg: str) -> None:
    print(f"{c('  XX', C.RED)} {msg}", file=sys.stderr, flush=True)


def banner(title: str) -> None:
    line = "=" * 68
    print(c(line, C.GRAY))
    print(c(f"  {title}", C.BOLD))
    print(c(line, C.GRAY))


# ----------------------------------------------------------------------------
# 工具定位
# ----------------------------------------------------------------------------
def find_bash() -> str | None:
    """找一个可用的 bash。Windows 需要 git-bash / MSYS2 / WSL。"""
    env = os.environ.get("ESP_BASH")
    if env and Path(env).exists():
        return env
    found = shutil.which("bash")
    if found:
        return found
    candidates = [
        r"C:\Program Files\WorkBuddy\resources\vendor\PortableGit\bin\bash.exe",
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files (x86)\Git\bin\bash.exe",
        r"C:\msys64\usr\bin\bash.exe",
        "/bin/bash",
        "/usr/bin/bash",
    ]
    for cpath in candidates:
        if Path(cpath).exists():
            return cpath
    return None


def find_esptool_python() -> str:
    """找一个带 esptool 的 python 解释器。"""
    env = os.environ.get("ESP_PYTHON")
    if env and Path(env).exists():
        return env
    for exe in [sys.executable, "python", "python3"]:
        if not exe:
            continue
        try:
            r = subprocess.run([exe, "-c", "import esptool"],
                               capture_output=True, timeout=20)
            if r.returncode == 0:
                return exe
        except Exception:
            continue
    return sys.executable


def check_deps() -> bool:
    """构建前的依赖自检（只查最关键的：bash / 工具链 / SDK / python）。"""
    banner("依赖检查")
    missing = False

    bash = find_bash()
    if bash:
        ok(f"bash        : {bash}")
    else:
        err("bash        : 未找到（需 Git for Windows / MSYS2 / WSL）")
        missing = True

    # 工具链 / SDK 位置解析：与 tools/esp_env.sh 同规则
    toolchain = os.environ.get("ESP_TOOLCHAIN")
    if not toolchain:
        bundled = BUNDLED_DIR / "toolchain-xtensa"
        toolchain = str(bundled) if (bundled / "bin").exists() else "C:/esp/toolchain-xtensa"
    gcc = Path(toolchain) / "bin" / "xtensa-lx106-elf-gcc.exe"
    if not gcc.exists():
        gcc = Path(toolchain) / "bin" / "xtensa-lx106-elf-gcc"
    if gcc.exists():
        ok(f"工具链      : {toolchain}")
    else:
        err(f"工具链      : 未找到（期望 {toolchain}/bin/xtensa-lx106-elf-gcc[.exe]）")
        err("             → 放到 platforms/esp8266/external/toolchain-xtensa/ 或设 ESP_TOOLCHAIN")
        missing = True

    arduino = os.environ.get("ESP_ARDUINO")
    if not arduino:
        bundled = BUNDLED_DIR / "arduino8266"
        arduino = str(bundled) if (bundled / "tools" / "sdk").exists() else "C:/esp/arduino8266"
    eboot = Path(arduino) / "bootloaders" / "eboot" / "eboot.elf"
    if eboot.exists():
        ok(f"Arduino SDK : {arduino}")
    else:
        err(f"Arduino SDK : 未找到（期望 {arduino}/bootloaders/eboot/eboot.elf）")
        err("             → 放到 platforms/esp8266/external/arduino8266/ 或设 ESP_ARDUINO")
        missing = True

    py = find_esptool_python()
    ok(f"python      : {py}")

    if missing:
        print()
        warn("依赖不完整。可运行：powershell -File platforms/esp8266/external/setup_deps.ps1 查看获取指引")
    return not missing


# ----------------------------------------------------------------------------
# 构建
# ----------------------------------------------------------------------------
# 构建脚本里会打印的行
RE_TU = re.compile(r"^\s*(CC|AS)\s+(.+?)\s*$")
RE_OBJS = re.compile(r"==\s*objs:\s*(\d+)")
RE_LINK = re.compile(r"==\s*link OK\s*==")
RE_ERR = re.compile(r"(error:|undefined reference|collect2\.exe: error|!!\s)")
RE_FILELIST = re.compile(r"==\s*filelist:\s*(\d+)\s+files")
RE_V9SYMS = re.compile(r"V9 task-symbol hits.*?:\s*(\d+)")


class BuildProgress:
    """构建进度。

    框架 TU（275 个）由 recompile_final_one.sh 并行编译，成功时**不打印**任何
    输出（只有失败才打 ERROR），所以不能靠解析日志数进度 —— 改为直接监控
    .o 文件数量增长，准确且不受并行输出交错影响。

    本地 TU（13 个）会打印 "CC xxx" / "AS xxx"，逐条显示文件名。
    """

    # (子目录, 该目录应有的 .o 数, 显示名) —— 相对 BUILD_DIR
    PHASES = [
        ("_obj", 275, "框架 TU"),
        ("_lwip", 24, "lwip"),
        ("obj", 13, "模板 TU"),
    ]

    def __init__(self):
        self.compiled_local = 0
        self.started = False
        self._tty = sys.stdout.isatty()

    def start(self):
        self.started = True
        info("开始编译（约 288 个 TU，首次全量约 10–20 分钟，增量几秒）")

    def _count(self, dirname: str) -> int:
        d = BUILD_DIR / dirname
        if not d.is_dir():
            return 0
        return sum(1 for f in d.glob("*.o"))

    def snapshot(self) -> tuple[int, int, list[str]]:
        """返回 (已完成, 总数, 各阶段描述)。"""
        done = 0
        total = 0
        desc = []
        for dirname, expect, label in self.PHASES:
            n = self._count(dirname)
            done += min(n, expect)
            total += expect
            desc.append(f"{label} {min(n, expect)}/{expect}")
        return done, total, desc

    def draw(self) -> None:
        if not self._tty:
            return
        done, total, desc = self.snapshot()
        pct = int(done * 100 / max(total, 1))
        bar_w = 28
        filled = int(bar_w * pct / 100)
        bar = "█" * filled + "░" * (bar_w - filled)
        sys.stdout.write(f"\r  [{bar}] {pct:3d}%  " + "  ".join(desc) + "   ")
        sys.stdout.flush()

    def clear(self) -> None:
        if self._tty:
            sys.stdout.write("\r" + " " * 110 + "\r")
            sys.stdout.flush()

    def on_line(self, line: str) -> None:
        m = RE_TU.match(line)
        if m:
            self.compiled_local += 1
            kind, src = m.group(1), m.group(2)
            self.clear()
            print(f"  {c('[' + str(self.compiled_local).rjust(3) + ']', C.GRAY)} "
                  f"{c(kind, C.GRAY)} {src}", flush=True)
            return

        m = RE_OBJS.search(line)
        if m:
            self.total_hint = int(m.group(1))
            return

        if RE_LINK.search(line):
            self.clear()
            ok("链接完成")
            return

        if RE_ERR.search(line):
            self.clear()
            err(line.strip()[:160])
            return

        # 其余关键行原样透出（filelist / V9 符号核验 / size 等）
        if any(k in line for k in ("== filelist:", "undefined symbols",
                                   "V9 task-symbol", "allocator forwarding",
                                   "merged .rodata")):
            self.clear()
            print("  " + line[:150], flush=True)


def purge_build_artifacts() -> None:
    """彻底清理所有构建产物（nonos/build/ 整个目录），下次构建为全量。"""
    banner("彻底清理构建产物")
    total = 0
    if BUILD_DIR.exists():
        for root, _, files in os.walk(BUILD_DIR):
            for f in files:
                try:
                    total += os.path.getsize(os.path.join(root, f))
                except OSError:
                    pass
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        ok(f"已删除 {BUILD_DIR.relative_to(REPO_ROOT)}/")

    # 旧版本残留（在 build/ 引入之前散在 nonos/ 根目录的）
    for pat in ["local.eagle.*.ld", "lwip_esp_side.o", "fw_final.elf",
                "fw_final.map", "nm_final.txt", "undefined_final.txt",
                "_obj", "obj", "_lwip", "_esp"]:
        for f in NONOS_DIR.glob(pat):
            try:
                if f.is_dir():
                    shutil.rmtree(f, ignore_errors=True)
                else:
                    total += f.stat().st_size
                    f.unlink()
                ok(f"已删除 nonos/{f.name}")
            except OSError:
                pass

    # 注意：fw_final.bin 不删 —— 它是随仓库走的"可直接烧录"产物，
    #       删了若随后构建失败就没有可用固件了。
    print(f"  合计释放约 {total / 1024 / 1024:.1f} MB")


def run_build(clean: bool = False) -> int:
    banner("构建 ESP8266 固件")

    bash = find_bash()
    if not bash:
        err("找不到 bash，构建中止")
        return 2

    if clean:
        info("清理中间产物 …")
        for d in [BUILD_DIR / "_obj", BUILD_DIR / "obj", BUILD_DIR / "_lwip",
                  BUILD_DIR / "_esp"]:
            if d.exists():
                shutil.rmtree(d, ignore_errors=True)
                ok(f"已删除 {d.relative_to(REPO_ROOT)}/")
        for f in BUILD_DIR.glob("local.eagle.*.ld"):
            f.unlink(missing_ok=True)
        # obj/ 是构建脚本写 .o 的地方，删掉后必须重建，否则 gcc 报
        # "Fatal error: can't create obj/xxx.o: No such file or directory"
        (BUILD_DIR / "obj").mkdir(parents=True, exist_ok=True)

    # 让脚本里的 python 也能被找到（Windows 下 bash 的 PATH 常缺 python）
    py = find_esptool_python()
    py_dir = str(Path(py).parent)

    env = os.environ.copy()
    env["ESP_PYTHON"] = py
    if os.name == "nt":
        env["PATH"] = py_dir + os.pathsep + env.get("PATH", "")

    cmd = [bash, "-c", "cd '{0}' && bash tools/build_fw_final.sh".format(
        NONOS_DIR.as_posix())]
    info("执行：" + " ".join(cmd[:2]) + " ... tools/build_fw_final.sh")

    prog = BuildProgress()
    prog.start()
    t0 = time.time()
    tail: list[str] = []
    rc = 1

    # Windows 下 git-bash 输出的编码可能是 GBK/UTF-8 混杂，逐行容错解码
    proc = subprocess.Popen(
        cmd, cwd=str(NONOS_DIR), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        bufsize=1, universal_newlines=True, encoding="utf-8", errors="replace",
    )
    assert proc.stdout is not None

    stop = threading.Event()

    def ticker():
        """周期性刷新进度条（框架 TU 并行编译时日志没有输出，靠 .o 数量监控）。"""
        while not stop.wait(0.4):
            prog.draw()

    th = threading.Thread(target=ticker, daemon=True)
    th.start()

    for line in proc.stdout:
        line = line.rstrip("\n").rstrip("\r")
        if not line:
            continue
        tail.append(line)
        if len(tail) > 200:
            tail.pop(0)
        prog.on_line(line)
    proc.wait()
    rc = proc.returncode
    stop.set()
    th.join(timeout=1)
    prog.clear()
    dt = time.time() - t0
    if rc == 0 and FW_BIN.exists():
        size = FW_BIN.stat().st_size
        age = time.time() - FW_BIN.stat().st_mtime
        if age > 600:
            warn(f"固件时间戳较旧（{age / 60:.0f} 分钟前），可能不是本次构建产物")
        ok(f"构建成功：{FW_BIN.relative_to(REPO_ROOT)}  "
           f"({size:,} 字节，用时 {dt:.0f}s)")
        return 0

    err(f"构建失败（退出码 {rc}，用时 {dt:.0f}s）")
    if tail:
        print()
        print(c("---- 末尾输出 ----", C.GRAY))
        for l in tail[-30:]:
            print(c("  " + l[:150], C.GRAY))
    return rc or 1


# ----------------------------------------------------------------------------
# 烧录
# ----------------------------------------------------------------------------
RE_WRITE = re.compile(r"Writing at (0x[0-9a-fA-F]+)\s*(.*?)\s*(\d+\.\d)%")
RE_HASH = re.compile(r"Hash of data verified")
RE_WROTE = re.compile(r"Wrote (\d+) bytes")


def detect_com_ports() -> list[str]:
    """列出存在的串口（Windows 用 pyserial）。"""
    try:
        from serial.tools import list_ports
        return [p.device for p in list_ports.comports()]
    except Exception:
        return []


def pick_com(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    ports = detect_com_ports()
    if not ports:
        return None
    # CH340 / CP2102 / USB-Serial 是本板常用的 USB-UART
    for p in ports:
        if any(k in p.upper() for k in ("COM", "USB")):
            return p
    return ports[0]


def run_flash(com: str | None, baud: int = 115200) -> int:
    banner("烧录固件")

    if not FW_BIN.exists():
        err(f"固件不存在：{FW_BIN}")
        return 2

    ports = detect_com_ports()
    if ports:
        ok("检测到串口：" + ", ".join(ports))
    target = pick_com(com)
    if not target:
        err("没有可用串口。用 --port COMx 指定")
        return 2
    if ports and target not in ports:
        warn(f"{target} 当前不在已插设备列表 {ports} 中，继续尝试")

    py = find_esptool_python()
    # esptool 5.x 用连字符式选项（--flash-mode/--flash-freq/--flash-size）；
    # 5.6 起下划线形式已废弃。
    cmd = [py, "-m", "esptool", "--chip", "esp8266", "--port", target,
           "--baud", str(baud), "write-flash",
           "--flash-mode", "dout", "--flash-freq", "40m", "--flash-size", "4MB",
           "0x0", str(FW_BIN)]
    info(f"esptool --port {target} --baud {baud}")

    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        bufsize=1, universal_newlines=True, encoding="utf-8", errors="replace",
    )
    assert proc.stdout is not None
    last_pct = -1
    for line in proc.stdout:
        line = line.rstrip("\n").rstrip("\r")
        m = RE_WRITE.search(line)
        if m:
            pct = int(float(m.group(3)))
            bar_w = 30
            filled = int(bar_w * pct / 100)
            bar = "█" * filled + "░" * (bar_w - filled)
            sys.stdout.write(f"\r  [{bar}] {pct:3d}%")
            sys.stdout.flush()
            last_pct = pct
            continue
        if RE_HASH.search(line):
            sys.stdout.write("\r" + " " * 60 + "\r")
            ok("数据校验通过（Hash of data verified）")
            continue
        m = RE_WROTE.search(line)
        if m:
            ok(f"已写入 {int(m.group(1)):,} 字节")
            continue
        if line.strip():
            print("  " + line[:150], flush=True)
    proc.wait()

    if proc.returncode == 0:
        ok("烧录完成")
        return 0
    err(f"烧录失败（退出码 {proc.returncode}）")
    return proc.returncode or 1


# ----------------------------------------------------------------------------
# 入口
# ----------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description="ESP8266 NONOS3 固件一键构建 / 烧录",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("不需要 CMake")[-1] if "不需要 CMake" in __doc__ else None,
    )
    ap.add_argument("--flash", action="store_true", help="构建后烧录")
    ap.add_argument("--flash-only", action="store_true", help="只烧录，不构建")
    ap.add_argument("--port", "-c", help="串口（如 COM4）；默认自动探测")
    ap.add_argument("--baud", type=int, default=115200, help="烧录波特率（默认 115200）")
    ap.add_argument("--clean", action="store_true",
                    help="构建前清理中间产物（_obj/ obj/ _lwip/ _esp/）")
    ap.add_argument("--purge", action="store_true",
                    help="彻底清理：除 --clean 外，还删掉链接脚本/中间 .o/链接产物"
                         "（下次构建为全量，约 10–20 分钟）")
    args = ap.parse_args()

    print()
    banner("ESP8266 NONOS3 固件工具")
    print(f"  仓库根    : {REPO_ROOT}")
    print(f"  模板目录  : {NONOS_DIR}")
    print()

    # --purge 是 --clean 的超集：清得更彻底（含生成的链接脚本/中间 .o）。
    # --flash-only 单独用（只烧录不构建），此时不做任何清理。
    if args.purge and not args.flash_only:
        purge_build_artifacts()
        print()

    if args.flash_only:
        return run_flash(args.port, args.baud)

    if not check_deps():
        return 2

    rc = run_build(clean=args.clean)
    if rc != 0:
        return rc

    if args.flash:
        print()
        return run_flash(args.port, args.baud)

    print()
    info("下一步：加 --flash 可直接烧录，或用 --flash-only 只烧录")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print()
        warn("已中断")
        sys.exit(130)
