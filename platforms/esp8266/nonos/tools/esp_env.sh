# ============================================================================
# ESP8266 外部工具链路径配置
# ============================================================================
# 被 tools/build_fw_final.sh 与 tools/recompile_final_one.sh 共同 source。
#
# ## 查找顺序（优先级从高到低）
#   1) 环境变量显式指定（ESP_TOOLCHAIN / ESP_ARDUINO / ...）
#   2) 仓库内 platforms/esp8266/external/  —— 零外部依赖方案，clone 即用
#   3) 本机常见安装位置（C:/esp/... 自动探测）
#
# ## 零外部依赖（推荐）
#   把工具链与 Arduino 包放到仓库的 platforms/esp8266/external/ 下：
#       platforms/esp8266/external/toolchain-xtensa/bin/xtensa-lx106-elf-gcc.exe
#       platforms/esp8266/external/arduino8266/tools/sdk/{include,lib,ld,lwip2}
#       platforms/esp8266/external/arduino8266/cores/esp8266
#       platforms/esp8266/external/arduino8266/bootloaders/eboot/eboot.elf
#       platforms/esp8266/external/arduino8266/tools/elf2bin.py
#       platforms/esp8266/external/nonos_v305/include/     ← 已在仓库内（~149KB）
#   运行 platforms/esp8266/external/setup_deps.ps1 会检查并提示缺什么。
#
# 所有路径都【不要】带结尾斜杠。
# ============================================================================

# 仓库根（本文件在 <repo>/platforms/esp8266/nonos/tools/，上溯四级）
# 2026-10-24：模板迁入 platforms/ 后由三级变四级。层数必须与
# build_fw_final.sh / recompile_final_one.sh / build.py 三处同步：
# 改一处不改其余会让仓库根错位一档，症状是满屏"头文件缺失"（与真因无关）。
_ESP_ENV_SELF="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
_ESP_REPO_ROOT="$(cd "$_ESP_ENV_SELF/../../../.." && pwd)"
_ESP_BUNDLED="$_ESP_REPO_ROOT/platforms/esp8266/external"

# ---- 1. 交叉编译器（xtensa-lx106-elf-gcc 所在目录的上一级）----
# 目录结构要求：$ESP_TOOLCHAIN/bin/xtensa-lx106-elf-gcc.exe
if [ -n "${ESP_TOOLCHAIN+x}" ]; then
    _ESP_TOOLCHAIN_SRC="env"
elif [ -e "$_ESP_BUNDLED/toolchain-xtensa/bin/xtensa-lx106-elf-gcc.exe" ] || \
     [ -e "$_ESP_BUNDLED/toolchain-xtensa/bin/xtensa-lx106-elf-gcc" ]; then
    ESP_TOOLCHAIN="$_ESP_BUNDLED/toolchain-xtensa"
    _ESP_TOOLCHAIN_SRC="bundled"
else
    ESP_TOOLCHAIN=C:/esp/toolchain-xtensa
    _ESP_TOOLCHAIN_SRC="default"
fi

# ---- 2. Arduino ESP8266 包（NONOS SDK 库、core、eboot、elf2bin.py）----
# 目录结构要求：$ESP_ARDUINO/tools/sdk/{include,lib,ld,lwip2}
#               $ESP_ARDUINO/cores/esp8266
#               $ESP_ARDUINO/bootloaders/eboot/eboot.elf
#               $ESP_ARDUINO/tools/elf2bin.py
if [ -n "${ESP_ARDUINO+x}" ]; then
    _ESP_ARDUINO_SRC="env"
elif [ -e "$_ESP_BUNDLED/arduino8266/tools/sdk" ]; then
    ESP_ARDUINO="$_ESP_BUNDLED/arduino8266"
    _ESP_ARDUINO_SRC="bundled"
else
    ESP_ARDUINO=C:/esp/arduino8266
    _ESP_ARDUINO_SRC="default"
fi

# ---- 3. NONOS SDK 3.0.5 独立头文件包（user_interface.h / osapi.h 等）----
# 目录结构要求：$ESP_NONOS_V305/include/*.h
if [ -n "${ESP_NONOS_V305+x}" ]; then
    _ESP_NONOS_SRC="env"
elif [ -e "$_ESP_BUNDLED/nonos_v305/include/user_interface.h" ]; then
    ESP_NONOS_V305="$_ESP_BUNDLED/nonos_v305"
    _ESP_NONOS_SRC="bundled"
else
    ESP_NONOS_V305=C:/esp/nonos_v305
    _ESP_NONOS_SRC="default"
fi

# ---- 4. extra_include（lwip2 与 nonos 头共存所需的中间层）----
# 实测：当前配方下【非必需】（-I$SDKINC 已提供等价头）。保留为可选覆盖点。
if [ -n "${ESP_EXTRA_INCLUDE+x}" ]; then
    _ESP_EXTRA_SRC="env"
elif [ -e "$_ESP_BUNDLED/extra_include" ]; then
    ESP_EXTRA_INCLUDE="$_ESP_BUNDLED/extra_include"
    _ESP_EXTRA_SRC="bundled"
else
    ESP_EXTRA_INCLUDE=C:/esp/esp8266/extra_include
    _ESP_EXTRA_SRC="default"
fi

# ---- 5. Python（构建管线调用 3 个 .py：rodata 归位、elf2bin）----
# 默认用 PATH 里的 python；若不在 PATH（如 PortableGit 的 bash），设
# ESP_PYTHON 为绝对路径，例如 ESP_PYTHON=D:/anaconda3/python.exe
# 解析顺序：
#   1) ESP_PYTHON 环境变量
#   2) PATH 里的 python / python3（PortableGit 的 bash 常常没有）
#   3) 常见 Windows 安装位置（Anaconda / Python.org / Arduino 自带）
# ⚠ 不解决这个会让脚本在 move_rodata_to_flash.py 处被 set -e 静默终止，
#   日志只停在 "== XESP8266_FREERTOS_V9_ON=... =="，极难定位。
#
# ⚠ 两个坑（2026-10-22 实测）：
#   1) PortableGit 的 bash 里 $HOME / $USERPROFILE 可能为空，不能靠它们拼路径；
#   2) Windows 自带的 C:/Users/<u>/AppData/Local/Microsoft/WindowsApps/python
#      是"应用商店占位符"，command -v 能查到、执行却失败（或弹商店）。
#      所以必须先验证 python 能真正跑起来（-c "import sys"），再采用。
if [ -z "${ESP_PYTHON+x}" ]; then
    ESP_PYTHON=""
    # 候选列表：先 PATH 里的（但要能真跑），再常见安装位置
    for _cand in python python3 python.exe; do
        if command -v "$_cand" >/dev/null 2>&1 && \
           "$_cand" -c "import sys" >/dev/null 2>&1; then
            ESP_PYTHON="$_cand"
            break
        fi
    done
    # 扫常见安装位置（覆盖 D: 与 C: 盘、Anaconda 与 Python.org）
    if [ -z "$ESP_PYTHON" ]; then
        for _cand in \
            "D:/anaconda3/python.exe" \
            "C:/anaconda3/python.exe" \
            "D:/ProgramData/anaconda3/python.exe" \
            "C:/ProgramData/anaconda3/python.exe" \
            "C:/Python311/python.exe" \
            "C:/Python312/python.exe" \
            "C:/Python310/python.exe" \
            "D:/Python311/python.exe" \
            "D:/Python312/python.exe"
        do
            if [ -x "$_cand" ] && "$_cand" -c "import sys" >/dev/null 2>&1; then
                ESP_PYTHON="$_cand"
                break
            fi
        done
    fi
    # 最后尝试通过 Windows 的 py 启动器
    if [ -z "$ESP_PYTHON" ] && command -v py >/dev/null 2>&1; then
        if py -c "import sys" >/dev/null 2>&1; then
            ESP_PYTHON="py"
        fi
    fi
    if [ -z "$ESP_PYTHON" ]; then
        echo "!! 找不到可用的 python。构建管线需要它跑 rodata 归位 / elf2bin。"
        echo "   请安装 Python 3，或设环境变量 ESP_PYTHON=<python 可执行文件路径>"
        ESP_PYTHON=python   # 保留原值，让后续报错更直观
    fi
fi

# ============================================================================
# 导出：构建脚本使用的派生变量
# ============================================================================
TC="$ESP_TOOLCHAIN/bin"
ARD="$ESP_ARDUINO"
SDK="$ESP_ARDUINO/tools/sdk"
CORE="$ESP_ARDUINO/cores/esp8266"
SDKINC="$SDK/include"
NONOSINC="$ESP_NONOS_V305/include"
EXTRAINC="$ESP_EXTRA_INCLUDE"
EB2BIN="$ESP_ARDUINO/tools/elf2bin.py"
EBOOT="$ESP_ARDUINO/bootloaders/eboot/eboot.elf"
PY="$ESP_PYTHON"

# ============================================================================
# 自检：缺什么直接报出来，不要让构建跑到一半才失败
# ============================================================================
_esp_check_paths() {
    _missing=0
    for _p in \
        "$TC/xtensa-lx106-elf-gcc.exe" \
        "$SDK/lib" \
        "$SDKINC" \
        "$CORE" \
        "$EBOOT" \
        "$EB2BIN"
    do
        if [ ! -e "$_p" ]; then
            echo "!! 缺少：$_p"
            _missing=1
        fi
    done
    if [ "$_missing" = "1" ]; then
        echo ""
        echo "=============================================================="
        echo "外部工具链路径不完整。请按下面之一处理："
        echo "  1) 零外部依赖方案（推荐）：把工具链与 Arduino 包放到仓库"
        echo "     platforms/esp8266/external/ 下，然后运行"
        echo "       powershell -ExecutionPolicy Bypass -File platforms/esp8266/external/setup_deps.ps1"
        echo "     该脚本会检查并提示缺什么。"
        echo "  2) 设环境变量后再构建，例如："
        echo "       export ESP_TOOLCHAIN=/path/to/toolchain-xtensa"
        echo "       export ESP_ARDUINO=/path/to/arduino8266"
        echo "       export ESP_NONOS_V305=/path/to/nonos_v305"
        echo "  3) 参考 README「换电脑：外部依赖清单」一节"
        echo "=============================================================="
        echo ""
        echo "当前解析到的路径："
        echo "  ESP_TOOLCHAIN   = $ESP_TOOLCHAIN   (来源: ${_ESP_TOOLCHAIN_SRC:-?})"
        echo "  ESP_ARDUINO     = $ESP_ARDUINO     (来源: ${_ESP_ARDUINO_SRC:-?})"
        echo "  ESP_NONOS_V305  = $ESP_NONOS_V305  (来源: ${_ESP_NONOS_SRC:-?})"
        return 1
    fi
    # python 可用性（构建管线必需，缺了会在 rodata 归位处静默失败）
    if ! "$PY" -c "import sys" >/dev/null 2>&1; then
        echo "!! python 不可用：$PY"
        echo "   构建管线的 move_rodata_to_flash.py / fix_rodata_pos.py / elf2bin.py 需要它。"
        echo "   请安装 Python 3 或设 ESP_PYTHON=<路径>。"
        return 1
    fi
    # 非致命警告
    [ -e "$NONOSINC/user_interface.h" ] || \
        echo "!! 警告：$NONOSINC/user_interface.h 不存在（user_interface.h 可能解析失败）"
    [ -e "$EXTRAINC" ] || \
        echo "!! 提示：extra_include 不存在：$EXTRAINC（当前配方下非必需，可忽略）"
    return 0
}

# ============================================================================
# 可选：打印解析结果（ESP_ENV_VERBOSE=1 时）
# ============================================================================
if [ "${ESP_ENV_VERBOSE:-0}" = "1" ]; then
    echo "== ESP 依赖解析 =="
    echo "  ESP_TOOLCHAIN   = $ESP_TOOLCHAIN   (来源: ${_ESP_TOOLCHAIN_SRC:-?})"
    echo "  ESP_ARDUINO     = $ESP_ARDUINO     (来源: ${_ESP_ARDUINO_SRC:-?})"
    echo "  ESP_NONOS_V305  = $ESP_NONOS_V305  (来源: ${_ESP_NONOS_SRC:-?})"
    echo "  ESP_EXTRA_INCLUDE = $ESP_EXTRA_INCLUDE (来源: ${_ESP_EXTRA_SRC:-?})"
    echo "  ESP_PYTHON      = $ESP_PYTHON"
fi
