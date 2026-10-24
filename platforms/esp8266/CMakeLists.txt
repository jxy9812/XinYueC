cmake_minimum_required(VERSION 3.16)
project(esp8266_nonos_fw NONE)

# ============================================================================
# ESP8266 NONOS3 固件 — CMake 编排层
# ============================================================================
# 真正的编译由 tools/build_fw_final.sh 驱动（bash 编排：275 个框架源 +
# lwip + SDK 库链接 + elf2bin + rodata 归位；python 后处理）。
# 本 CMakeLists 提供三件事：
#   1. `cmake --build .`           → 一键构建固件（fw_final.bin）
#   2. `cmake --build . --target flash` → 烧录（esptool，COM 口可配）
#   3. IDE（VS/CLion）可直接打开本目录获得构建入口
# 固件本体不经过 CMake 编译器检测（xtensa 工具链在 bash 管线内部调用），
# 因此 project(...) 声明为 NONE。
# ============================================================================

set(NONOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nonos")
set(FW_BIN "${NONOS_DIR}/fw_final.bin")

# ---- 串口与工具（可 -D 覆盖）----
# 串口：默认 auto = 让 esptool 自动探测（板子在 COM2/COM4 间漂移）；
# 需要锁定时 cmake -DFW_COM_PORT=COM4 ...
set(FW_COM_PORT "auto" CACHE STRING "esptool 烧录串口（auto=自动探测）")
set(FW_BAUD "115200" CACHE STRING "esptool 烧录波特率（115200 最稳，实测 28s/450KB）")

# ---- bash：PortableGit / Git for Windows / MSYS2 均可 ----
# 注意用 OPTIONAL 而非 REQUIRED：本文件可能被根工程 add_subdirectory() 纳入，
# 那时桌面开发者机器上不一定有 bash/python。配置阶段不报错，等到真正构建
# fw 目标时才检查并给出明确提示（见下方 add_custom_target 里的守卫）。
find_program(BASH_EXE bash
    HINTS
        "C:/Program Files/WorkBuddy/resources/vendor/PortableGit/bin"
        "C:/Program Files/Git/bin"
        "C:/msys64/usr/bin"
    OPTIONAL)
find_program(PY_EXE python
    HINTS "D:/anaconda3"
    OPTIONAL)

if(NOT BASH_EXE OR NOT PY_EXE)
    message(WARNING
        "esp8266: 未找到 bash 或 python，fw/flash 目标将无法构建。\n"
        "  bash  : ${BASH_EXE}\n"
        "  python: ${PY_EXE}\n"
        "  请安装 Git for Windows / MSYS2 与 Python，或用 -DBASH_EXE=... -DPY_EXE=... 指定。")
endif()

# ---- fw：一键构建（增量，脚本内部按 mtime 跳过未变文件）----
# ⚠ 必须经 bash -c 显式把 python 目录塞进 PATH：
#   构建管线里调用了 python（move_rodata_to_flash.py / fix_rodata_pos.py /
#   elf2bin.py），而 PortableGit 的 bash 默认 PATH 里没有 python —— 直接
#   `bash tools/build_fw_final.sh` 会在 rodata 归位那步静默失败（脚本 set -e
#   直接退出，日志只停在 "== filelist: ... =="，极易误判为"编译卡住"）。
if(PY_EXE)
    get_filename_component(PY_DIR "${PY_EXE}" DIRECTORY)
else()
    set(PY_DIR "")
endif()

# fw 目标是否作为默认构建的一部分：
#   - 独立打开 esp8266/ 时（顶层工程）→ 默认参与 ALL，行为与以前一致
#   - 被根工程 add_subdirectory 纳入时 → 不参与 ALL，避免"全部生成"去跑
#     10 分钟的固件编译；需要时在 VS 里单独生成 fw 目标即可
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_fw_all ALL)
else()
    set(_fw_all "")
endif()

add_custom_target(fw ${_fw_all}
    COMMAND "${BASH_EXE}" -c
            "export PATH='${PY_DIR}':$PATH; exec bash tools/build_fw_final.sh"
    WORKING_DIRECTORY "${NONOS_DIR}"
    BYPRODUCTS "${FW_BIN}"
    COMMENT "ESP8266 NONOS3 firmware build"
    VERBATIM)

# ---- flash：烧录（板子需进下载态或用 RTS/DTR 自动复位）----
find_program(ESPTOOL_EXE esptool
    HINTS "D:/anaconda3/Scripts"
    OPTIONAL)
if(NOT ESPTOOL_EXE)
    set(ESPTOOL_CMD "${PY_EXE}" -m esptool)
else()
    set(ESPTOOL_CMD "${ESPTOOL_EXE}")
endif()

# ⚠ 端口：板子（CH340）会在 COM2/COM4 之间漂移。若 FW_COM_PORT 指向一个
#   不存在的口，esptool 5.x 会报一个完全误导的错误
#   「Must be pairs of an address and the binary filename」。
#   故这里默认不传 --port，让 esptool 自动探测；需要锁定端口时用
#   cmake -DFW_COM_PORT=COM4 显式覆盖（此时才追加 --port）。
if(FW_COM_PORT STREQUAL "auto")
    set(FW_PORT_ARGS "")
else()
    set(FW_PORT_ARGS --port "${FW_COM_PORT}")
endif()

add_custom_target(flash
    COMMAND "${PY_EXE}" -m esptool --chip esp8266 ${FW_PORT_ARGS}
            --baud "${FW_BAUD}" write-flash
            --flash_mode dout --flash_freq 40m --flash_size 4MB
            0x0 "${FW_BIN}"
    DEPENDS fw
    COMMENT "Flash fw_final.bin -> ${FW_COM_PORT}"
    VERBATIM)

# ---- monitor：串口日志（74880，板端输出波特率）----
# 用仓库外的 capture.py（C:/esp/esp8266/esptool/，自带复位+抓取）。
# ⚠ 它必须拿到真实 COM 号，故本目标要求显式 -DFW_COM_PORT=COM2/COM4。
find_program(CAPTURE_PY capture.py
    HINTS "C:/esp/esp8266/esptool"
    OPTIONAL)
if(FW_COM_PORT STREQUAL "auto")
    set(MONITOR_COM "COM2")
else()
    set(MONITOR_COM "${FW_COM_PORT}")
endif()
add_custom_target(monitor
    COMMAND "${PY_EXE}" "${CAPTURE_PY}" "${MONITOR_COM}" 74880 60 monitor.log
    COMMENT "Monitor UART ${MONITOR_COM} 60s -> monitor.log"
    VERBATIM)
