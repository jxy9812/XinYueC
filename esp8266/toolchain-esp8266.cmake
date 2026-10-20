# ESP8266 RTOS SDK 的 CMake 工具链文件。
#
# 与 ESP-IDF 无关：ESP-IDF 不支持 ESP8266，本目标走 Espressif 官方的
# xtensa-lx106-elf GCC。SDK 自身的 Makefile 只服务 Makefile 工程，本文件
# 让同一套 XinYueC 源文件可以直接由 CMake 编译成 Xtensa 目标文件。
#
# 用法：
#   cmake -S esp8266 -B build/esp8266 \
#         -DCMAKE_TOOLCHAIN_FILE=toolchain-esp8266.cmake \
#         -DXESP8266_SDK_PATH=/c/esp/esp8266

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR esp8266)

# ---- 工具链定位 --------------------------------------------------------
# XESP8266_TOOLCHAIN_ROOT 默认指向 SDK 同级的工具链目录，可覆盖。
# 默认值按平台给出：Windows 下 CMake 只接受含盘符的路径，
# Git Bash 风格的 /c/... 只有 MSYS 工具链认识。
if(WIN32)
    set(_XESP8266_DEFAULT_TOOLCHAIN "C:/esp/toolchain-xtensa")
else()
    set(_XESP8266_DEFAULT_TOOLCHAIN "/opt/xtensa-lx106-elf")
endif()
set(XESP8266_TOOLCHAIN_ROOT "${_XESP8266_DEFAULT_TOOLCHAIN}"
    CACHE PATH "xtensa-lx106-elf 工具链根目录")

set(TOOLCHAIN_BIN "${XESP8266_TOOLCHAIN_ROOT}/bin")

# Windows 上 CMake 只接受带 .exe 的完整路径；非 Windows 目标则用无后缀名。
if(WIN32)
    set(_TOOL_SUFFIX ".exe")
else()
    set(_TOOL_SUFFIX "")
endif()

if(NOT EXISTS "${TOOLCHAIN_BIN}/xtensa-lx106-elf-gcc${_TOOL_SUFFIX}")
    message(FATAL_ERROR
        "找不到 xtensa-lx106-elf-gcc：${TOOLCHAIN_BIN}\n"
        "请设置 -DXESP8266_TOOLCHAIN_ROOT=<工具链根目录>")
endif()

set(CMAKE_C_COMPILER   "${TOOLCHAIN_BIN}/xtensa-lx106-elf-gcc${_TOOL_SUFFIX}")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/xtensa-lx106-elf-g++${_TOOL_SUFFIX}")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_BIN}/xtensa-lx106-elf-gcc${_TOOL_SUFFIX}")
set(CMAKE_AR      "${TOOLCHAIN_BIN}/xtensa-lx106-elf-ar${_TOOL_SUFFIX}"     CACHE FILEPATH "")
set(CMAKE_RANLIB  "${TOOLCHAIN_BIN}/xtensa-lx106-elf-ranlib${_TOOL_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${TOOLCHAIN_BIN}/xtensa-lx106-elf-objcopy${_TOOL_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP "${TOOLCHAIN_BIN}/xtensa-lx106-elf-objdump${_TOOL_SUFFIX}" CACHE FILEPATH "")

# xtensa 的 -mlongcalls 是该 ABI 的必需项，不是可优化开关。
set(CMAKE_C_FLAGS_INIT "-mlongcalls -mtext-section-literals")

# 平台IO 只提供 xtensa-lx106-elf-ld.exe 与 -as.exe，而 collect2 按无前缀的
# ld.exe / as.exe 去找。编译期走 cc1 不受影响，一到链接就报
# "collect2: CreateProcess: No such file or directory"。-B 显式指定子程序目录。
set(CMAKE_EXE_LINKER_FLAGS_INIT "-B${TOOLCHAIN_BIN}")

# ESP8266 无操作系统层，CMake 的 find_* 与 try_run 都不可用。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
