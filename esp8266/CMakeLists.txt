# ESP8266 最小控制台固件构建。
#
# 目标：NodeMCU ESP8266 + CH340 串口控制台，保留 XCoreApplication 事件循环
# 与 XConsoleShell，裁掉图形/网络/协议栈，产出可被 esptool 直接烧录的 .bin。
#
# 与桌面构建的关键差异：
#   1. 不复用仓库根 CMakeLists.txt——那份带 X11/OpenGL/Vulkan/fontconfig 的
#      桌面探测逻辑在裸机上没有意义。本文件只做源文件收集与链接。
#   2. 裁剪开关不在命令行逐个 -D，而是由 Drive/Espressif/ESP8266/ESP8266Minimal.h
#      统一强制包含，矩阵与理由集中在一处维护。
#   3. 平台检测靠 -D__ESP8266__ 与 -D__FreeRTOS__ 两个宏：前者让 Drive/Espressif/ESP8266
#      的文件参与编译，后者让 CXinYueConfig.h 把平台判为 FreeRTOS。
#
# 配置：
#   cmake -S esp8266 -B build/esp8266 \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/esp8266/toolchain-esp8266.cmake \
#         -DXESP8266_SDK_PATH=/c/esp/esp8266 -DXESP8266_TOOLCHAIN_ROOT=/c/esp/esp8266/toolchain

cmake_minimum_required(VERSION 3.16)
project(XinYueC_ESP8266 VERSION 0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 归一化成不含 ".." 的绝对路径：file(GLOB_RECURSE) 的匹配模式里带 ".."
# 会匹配不到，导致 Src/Drive 的子目录静默丢失（表现为满屏 "No such file"）。
get_filename_component(XINYUE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(XINYUE_ROOT "${XINYUE_ROOT}" CACHE PATH "XinYueC 仓库根目录")

# ---- SDK 与工具链位置 --------------------------------------------------
set(XESP8266_SDK_PATH "" CACHE PATH "ESP8266 RTOS SDK 根目录")
set(XESP8266_TOOLCHAIN_ROOT "" CACHE PATH "xtensa-lx106-elf 工具链根目录")
if(NOT XESP8266_SDK_PATH)
    message(FATAL_ERROR "必须指定 -DXESP8266_SDK_PATH=<ESP8266 RTOS SDK 目录>")
endif()

set(SDK_INCLUDE_DIRS
    "${XESP8266_SDK_PATH}/include"
    "${XESP8266_SDK_PATH}/extra_include"
    "${XESP8266_SDK_PATH}/driver_lib/include"
    "${XESP8266_SDK_PATH}/include/espressif"
    "${XESP8266_SDK_PATH}/include/lwip"
    "${XESP8266_SDK_PATH}/include/lwip/ipv4"
    "${XESP8266_SDK_PATH}/include/lwip/ipv6"
    "${XESP8266_SDK_PATH}/include/nopoll"
    "${XESP8266_SDK_PATH}/include/spiffs"
    "${XESP8266_SDK_PATH}/include/ssl"
    "${XESP8266_SDK_PATH}/include/json"
    "${XESP8266_SDK_PATH}/include/openssl")

# SDK 链接顺序与 builder/frameworks/esp8266-rtos-sdk.py 的 LIBS 保持一致：
# main/hal 提供 newlib syscall 与 os_printf，espconn/lwip 供 SDK 自身网络栈
# 引用，缺一不可，即便本产品并不使用网络。
# freertos 有意不在列表里：内核改用仓库 vendored 的 V9.0.0 源码自建，
# 移植层为 Drive/FreeRTOS/portable/GCC/XTENSA_LX106。SDK 预编译的
# libfreertos.a 是 V7.5.2 ABI，两套内核符号不能同时进入最终固件。
set(SDK_LIBS
    cirom crypto espconn espnow freertos gcc
    json hal lwip main mesh mirom net80211 nopoll mbedtls
    # -nostdlib 之后必须显式补 libc 与 libgcc：SDK 的预编译库会引用
    # malloc/free/strrchr 等 newlib 符号，PlatformIO 原构建脚本靠 Arduino
    # 核心的链接脚本兜底，直接复用参数会漏掉。
    c gcc
    phy pp pwm smartconfig spiffs ssl wpa wps)

# 板载 Flash 为 4MB，而 SDK 自带的 eagle.app.v6.ld 按 512KB 布局只给 irom0
# 368KB（0x5C000）。本控制台固件实测需要约 381KB，装不下。改用按 4MB 布局
# 生成的变体：irom0 从 Flash 偏移 0x20000 起，系统参数区在 0x3fb000（脚本
# 注释第 4 条），故可用 len = 0x3DB000（约 3.9MB），余量充足。
set(LDSCRIPT "${CMAKE_CURRENT_LIST_DIR}/ld/eagle.app.v6.4m.ld")

# ---- 编译定义 ----------------------------------------------------------
# __ESP8266__ ：Drive/Espressif/ESP8266 下所有平台文件的编译守卫。
# __FreeRTOS__ ：CXinYueConfig.h 的平台自动检测，据此走 XPLATFORM_FREERTOS。
# __ets__ / ICACHE_FLASH / F_CPU ：SDK 头文件与驱动库的既有约定。
add_compile_definitions(
    __ESP8266__
    __FreeRTOS__
    __ets__
    ICACHE_FLASH
    F_CPU=80000000)

# 裁剪矩阵：必须早于任何 Config.h 被预处理，故用 -include 而非普通头文件。
add_compile_options(-include "${XINYUE_ROOT}/Drive/Espressif/ESP8266/ESP8266Minimal.h")

add_compile_options(
    -Os
    -std=gnu99
    -U__STRICT_ANSI__
    -ffunction-sections
    -fdata-sections
    # SDK 默认带 -Werror/-Wundef；本工程有大量历史告警，只保留真正致命的。
    -Wno-error
    -Wno-unused-parameter
    -Wno-implicit-function-declaration)

# ---- 包含路径 ----------------------------------------------------------
# 统一收集后用 target_include_directories 一次性下发。不要改回
# add_compile_options(-I ...)：经 Ninja 生成时多个 -I 会被折叠成单个 -I 加
# 一串路径，而 GCC 的 -I 只吃一个参数，结果只有第一个目录生效。
set(XINYUE_INCLUDE_DIRS
    "${XINYUE_ROOT}/Src"
    "${XINYUE_ROOT}/Src/XPlatform"
    "${XINYUE_ROOT}/Drive"
    )

# Src 与 Drive 的所有子目录都进 include path，源文件之间才能按短名互相
# 包含（仓库根 CMakeLists 用 FIND_INCLUDE_DIR 做同样的事，这里是等价实现）。
foreach(_tree Src Drive)
    if(_tree STREQUAL "Library")
        file(GLOB _ld "${XINYUE_ROOT}/Library/xxHash")
        list(APPEND XINYUE_INCLUDE_DIRS "${XINYUE_ROOT}/Library/xxHash" "${XINYUE_ROOT}/Library/zlib")
        continue()
    endif()
    # LIST_DIRECTORIES true 是必需的：GLOB_RECURSE 默认只返回文件，不返回目录，
    # 少了它 IS_DIRECTORY 永远不成立，结果一个子目录都加不进来。
    file(GLOB_RECURSE _dirs LIST_DIRECTORIES true
         RELATIVE "${XINYUE_ROOT}" "${XINYUE_ROOT}/${_tree}/*")
    foreach(_d ${_dirs})
        if(IS_DIRECTORY "${XINYUE_ROOT}/${_d}")
            list(APPEND XINYUE_INCLUDE_DIRS "${XINYUE_ROOT}/${_d}")
        endif()
    endforeach()
endforeach()
# Library/ 下内建第三方源码的公开头（xxHash / zlib）
list(APPEND XINYUE_INCLUDE_DIRS
    "${XINYUE_ROOT}/Library/xxHash"
    "${XINYUE_ROOT}/Library/zlib")
list(APPEND XINYUE_INCLUDE_DIRS ${SDK_INCLUDE_DIRS})

# 排除仓库 vendored 的 FreeRTOS 头文件树，内核与移植层改用 SDK 的 V7.5.2：
#   - 留着 Drive/FreeRTOS/include 会让 "FreeRTOS.h" 解析到 V9.0.0，
#     与链接的 libfreertos.a（V7.5.2）ABI 不一致；
#   - 留着 Drive/FreeRTOS/portable 会让 "portmacro.h" 解析到 ARM_CM4F，
#     在 Xtensa 上直接报 "unknown opcode or format name 'dsb'"。
# Drive/FreeRTOS/Core 与 Sync 保留：那是 XinYueC 自己的 XThread / XSemaphore
# 适配层，与内核版本无关。
list(FILTER XINYUE_INCLUDE_DIRS EXCLUDE REGEX "/Drive/FreeRTOS/(include|portable)(/|$)")

# SDK 的 FreeRTOS 头按短名可包含（include/freertos 下就是 FreeRTOS.h 等），
# 插到最前确保 #include "FreeRTOS.h" 命中 V7.5.2。
list(INSERT XINYUE_INCLUDE_DIRS 0 "${XESP8266_SDK_PATH}/include/freertos")

# ---- 源文件收集 --------------------------------------------------------

file(GLOB_RECURSE SRC_LIST "${XINYUE_ROOT}/Src/*.c")

# Library/ 是仓库内建的第三方源码（xxHash 等）。XHashMap/XString 等核心容器
# 直接调用 XCryptographicHash_XxHash64，而该实现落在 xxHash 上，因此这一目录
# 必须参与编译——也正因如此 XCryptographic_ON 事实上不可裁。
# 只取 xxHash：Library/ 下还有 freetype 等桌面向依赖，它们依赖各自构建
# 系统注入的宏（如 freetype 的 FT_ERR_PREFIX），在独立工具链下不能整目录编。
file(GLOB LIBRARY_LIST "${XINYUE_ROOT}/Library/xxHash/*.c" "${XINYUE_ROOT}/Library/zlib/*.c")

# Drive/Espressif/ESP8266 只放芯片专属实现（串口后端、XSystem、裁剪档案）。
# 跨芯片共用的部分复用 Src/ 里既有的 XSerialPort / XConsoleShell 适配层，
# 不在本仓库另建一套。
file(GLOB_RECURSE ESP_DRIVE_LIST "${XINYUE_ROOT}/Drive/Espressif/ESP8266/*.c")

# FreeRTOS：内核与移植层都改用 SDK 自带的 V7.5.2（libfreertos.a +
# include/freertos），不编译仓库 vendored 的 V9.0.0。
#
# 原因（实测结论）：SDK 的预编译库 libmain/liblwip/libespconn/libssl 等全部
# 是针对 V7.5.2 ABI 编译的，所需符号（malloc/free/pvPortZalloc/zalloc/
# xTaskGenericCreate/xTimerCreate/vTaskStepTick/...）全部定义在 libfreertos.a。
# 仓库的 V9.0.0 与它是互斥的：同时链接会产生 42 处重复定义（pxCurrentTCB、
# vTaskDelay、队列系列等核心符号冲突），而"补 V7 兼容层"实际等于把 V7.5.2 的
# 内核与移植层重新实现一遍。因此本平台直接采用 SDK 内核。
#
# 由此还带来一个好处：SDK 的 V7.5.2 自带 timers.c，因此 configUSE_TIMERS
# 可以打开，libmain 依赖的 xTimerCreate / xTimerGenericCommand 不再缺失。
set(FREERTOS_KERNEL_LIST "")

# 公共层：Core/Sync 里的 XThread/XSemaphore/XMutex/XWaitCondition 实现基于
# FreeRTOS 公开 API，跨架构可复用。
file(GLOB FREERTOS_DRIVE_LIST
     "${XINYUE_ROOT}/Drive/FreeRTOS/Core/*.c"
     "${XINYUE_ROOT}/Drive/FreeRTOS/Sync/*.c")

# 桌面/平台无关的其余 Drive 文件（如 Unsupported 存根）保持参与编译：
# 它们各自带 __linux__/_WIN32 守卫，在 ESP8266 下整段裁空。
# Unsupported/Device/XDeviceFile_unsupported.c 要排除：它提供同名的一组
# XDeviceFile_legacy* 存根，与本平台的 XDeviceFile_esp8266.c 重复定义，
# 而 --whole-archive 会把两者都拉进最终链接。
file(GLOB_RECURSE OTHER_DRIVE_LIST "${XINYUE_ROOT}/Drive/Posix/*.c"
     "${XINYUE_ROOT}/Drive/windows/*.c"
     "${XINYUE_ROOT}/Drive/Unsupported/*.c"
     "${XINYUE_ROOT}/Drive/Compiler/gcc/*.c")
list(FILTER OTHER_DRIVE_LIST EXCLUDE REGEX
     "X(DeviceFile|System)_unsupported\.c$")

# 先打包成静态库再链接。
#
# Windows 的命令行长度上限约 32KB，而本工程有约 1500 个目标文件。collect2 把
# 整条 ld 命令拼出来后再 CreateProcess，一旦超限就报
# "collect2: CreateProcess: No such file or directory"——错误信息具有误导性，
# 看起来像找不到链接器，实际是命令行超长。打成 .a 之后，链接命令行里只剩
# 一个库文件加 user_main.o，长度降到可接受范围。
add_library(xinyuec_core STATIC
    ${SRC_LIST}
    ${LIBRARY_LIST}
    ${ESP_DRIVE_LIST}
    ${FREERTOS_KERNEL_LIST}
    ${FREERTOS_DRIVE_LIST}
    ${OTHER_DRIVE_LIST})

# 静态库里的符号必须被整个拉进来：--gc-sections 会丢掉未被 exe 直接引用的
# 目标文件，而本工程大量符号是经虚表/静态注册表间接引用的。
target_link_options(xinyuec_core PRIVATE
    -Wl,--whole-archive)

target_include_directories(xinyuec_core PUBLIC ${XINYUE_INCLUDE_DIRS})
target_compile_options(xinyuec_core PUBLIC -include
    "${XINYUE_ROOT}/Drive/Espressif/ESP8266/ESP8266Minimal.h")

add_executable(xinyuec_console "${CMAKE_CURRENT_LIST_DIR}/user/user_main.c")

target_include_directories(xinyuec_console PRIVATE ${XINYUE_INCLUDE_DIRS})
target_link_directories(xinyuec_console PRIVATE
    "${XESP8266_SDK_PATH}/lib"
    "${XESP8266_TOOLCHAIN_ROOT}/lib/gcc/xtensa-lx106-elf/10.3.0")
target_link_libraries(xinyuec_console PRIVATE xinyuec_core
    # SDK 各库之间存在循环引用（libespconn <-> liblwip、libssl <-> libmbedtls 等），
    # 必须整组重复扫描才能收敛。
    "-Wl,--start-group" ${SDK_LIBS} "-Wl,--end-group"
    # __ashrdi3 / __atomic_* 等编译器内建只在工具链的 libgcc 里；SDK 自带的
    # libgcc.a 是 L106 裁剪版，缺这些符号，因此用全路径显式指定工具链那份。
    "${XESP8266_TOOLCHAIN_ROOT}/lib/gcc/xtensa-lx106-elf/10.3.0/libgcc.a")

target_link_options(xinyuec_console PRIVATE
    # collect2 按无前缀名找 ld/as，平台IO 只带 xtensa-lx106-elf- 前缀版本
    -B${XESP8266_TOOLCHAIN_ROOT}/bin
    -nostdlib
    -Wl,--no-check-sections
    -Wl,-static
    -Wl,--gc-sections
    # SHELL: 前缀阻止 CMake 给路径加转义引号。否则 ld 收到的是带引号的文件名，
    # 报 "cannot open linker script file ...: Invalid argument"。
    "SHELL:-T ${LDSCRIPT}"
    -u call_user_start
    # 不加 -u _printf_float / -u _scanf_float：那是 PlatformIO 为 ESP32
    # 构建脚本准备的，SDK 自带的 libmain/libc 不含这两个符号。
)

# ---- 产物 --------------------------------------------------------------
set_target_properties(xinyuec_console PROPERTIES SUFFIX ".elf")
