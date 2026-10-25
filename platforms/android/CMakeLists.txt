cmake_minimum_required(VERSION 3.21)
project(android_apk NONE)

# ============================================================================
# Android APK — CMake 编排层
# ============================================================================
# 与 platforms/esp8266/CMakeLists.txt 同一形态：真正的编译不在这里。
#   - 原生库 libxguidemo.so 由**仓库根 CMakeLists.txt** 的 `xguidemo` 目标
#     编译（NDK 工具链 + Drive/Android/** + Test/XGuiDemo/**）。
#   - 本文件只负责"配置根工程 → 编译该目标 → 调打包脚本"这条流水线。
#
# 用法：
#   cmake -S platforms/android -B build_android
#   cmake --build build_android                  # 编译 + 打包 APK
#   cmake --build build_android --target lib     # 只编译 .so
#   cmake --build build_android --target apk     # 只打包（.so 已存在时）
#   cmake --build build_android --target install-apk
#
# ⚠ 不要把仓库根 add_subdirectory() 进来。根工程含桌面目标，且会让 VS 出现
#   两个同名 .sln 且都点不动（与 ESP8266 模板同一个坑，见 platforms/README.md）。
#   这里改用 add_custom_target 显式调用 cmake，等价于命令行操作。
# ============================================================================

set(ANDROID_SELF_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(REPO_ROOT "${ANDROID_SELF_DIR}/../.." ABSOLUTE)

# ---- 可 -D 覆盖的参数 ----
set(ANDROID_ABI_NAME "x86_64" CACHE STRING "目标 ABI: x86_64 | arm64-v8a")
set(ANDROID_API_LEVEL "24" CACHE STRING "minSdkVersion / target platform")
set(ANDROID_NDK_ROOT "D:/android-ndk/android-ndk-r29" CACHE PATH "Android NDK 根目录")

set_property(CACHE ANDROID_ABI_NAME PROPERTY STRINGS x86_64 arm64-v8a)

# ---- 构建目录：与 build_apk.sh 约定一致 ----
# 脚本按 out/android-<abi_dir>/ 找 .so；arm64-v8a 的目录名缩写为 arm64
# （CMake 侧习惯），打包侧再映射回 APK 的 arm64-v8a。两边必须一致，
# 否则脚本报"缺 libxguidemo.so"。
if(ANDROID_ABI_NAME STREQUAL "arm64-v8a")
    set(_android_abi_dir "arm64")
else()
    set(_android_abi_dir "${ANDROID_ABI_NAME}")
endif()
set(ANDROID_BUILD_DIR "${REPO_ROOT}/out/android-${_android_abi_dir}" CACHE PATH "原生库构建目录")

# ---- 工具链自检：缺什么直接报，不要跑一半才失败 ----
set(_android_toolchain "${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${_android_toolchain}")
    message(FATAL_ERROR
        "找不到 NDK 工具链：${_android_toolchain}\n"
        "  请用 -DANDROID_NDK_ROOT=<NDK 路径> 指定，或设环境变量 ANDROID_NDK。")
endif()

# ---- 原生库：配置 + 编译根工程的 xguidemo 目标 ----
# ⚠ 同一构建目录不能同时跑两个 ninja（会报 failed recompaction:
#   Permission denied）。桌面构建与安卓构建请串行。
add_custom_target(lib
    COMMAND ${CMAKE_COMMAND}
            -S "${REPO_ROOT}"
            -B "${ANDROID_BUILD_DIR}"
            -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${_android_toolchain}
            -DANDROID_ABI=${ANDROID_ABI_NAME}
            -DANDROID_PLATFORM=android-${ANDROID_API_LEVEL}
            # 显式锚定产物目录=本构建目录（.so 落 out/android-<abi>/，打包
            # 脚本按此找）。不传则机器本地 CMake_Config.cmake 可能把
            # CMAKE_RUNTIME_OUTPUT_DIRECTORY 改指仓库 bin/，被根工程的
            # 交叉构建守卫拒绝（防交叉产物覆盖桌面门禁二进制）。
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${ANDROID_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND} --build "${ANDROID_BUILD_DIR}" --target xguidemo
    WORKING_DIRECTORY "${REPO_ROOT}"
    BYPRODUCTS "${ANDROID_BUILD_DIR}/libxguidemo.so"
    COMMENT "Android 原生库：配置并编译 xguidemo (${ANDROID_ABI_NAME})"
    VERBATIM)

# ---- 打包：优先 bash 版，退回 PowerShell 版 ----
# bash 版支持 x86_64 / arm64-v8a / both；PowerShell 版当前只处理单 ABI。
find_program(BASH_EXE bash
    HINTS
        "C:/Program Files/WorkBuddy/resources/vendor/PortableGit/bin"
        "C:/Program Files/Git/bin"
        "C:/msys64/usr/bin"
    OPTIONAL)
find_program(POWERSHELL_EXE powershell
    HINTS "C:/WINDOWS/System32/WindowsPowerShell/v1.0"
    OPTIONAL)

if(BASH_EXE)
    set(APK_CMD "${BASH_EXE}" "${ANDROID_SELF_DIR}/build_apk.sh" "${ANDROID_ABI_NAME}")
    set(APK_TOOL_DESC "build_apk.sh (bash)")
elseif(POWERSHELL_EXE)
    set(APK_CMD "${POWERSHELL_EXE}" -ExecutionPolicy Bypass
                -File "${ANDROID_SELF_DIR}/pack_apk.ps1" -Abi "${ANDROID_ABI_NAME}")
    set(APK_TOOL_DESC "pack_apk.ps1 (powershell)")
else()
    set(APK_CMD "")
    message(WARNING
        "android: 未找到 bash 或 powershell，apk/install-apk 目标不可用。\n"
        "  请安装 Git for Windows 或使用 Windows PowerShell。")
endif()

if(APK_CMD)
    add_custom_target(apk
        COMMAND ${APK_CMD}
        WORKING_DIRECTORY "${REPO_ROOT}"
        DEPENDS lib
        BYPRODUCTS "${ANDROID_SELF_DIR}/out/XGuiDemo-debug.apk"
        COMMENT "打包 APK（${APK_TOOL_DESC}）"
        VERBATIM)
endif()

# ---- 安装到设备（adb）----
find_program(ADB_EXE adb
    HINTS
        "C:/Users/jxy/AppData/Local/Android/Sdk/platform-tools"
        "$ENV{ANDROID_SDK}/platform-tools"
        "$ENV{ANDROID_HOME}/platform-tools"
    OPTIONAL)

# 设备选择：多设备同时在线时 adb 会报 "more than one device/emulator"，
# 必须显式 -s。默认 127.0.0.1:5555（本机模拟器），可用 -DANDROID_SERIAL=...
# 覆盖；留空则让 adb 自己挑（仅单设备时可用）。
set(ANDROID_SERIAL "127.0.0.1:5555" CACHE STRING "adb 目标设备（空=让 adb 自动选择）")
if(ANDROID_SERIAL)
    set(_adb_serial_args -s "${ANDROID_SERIAL}")
else()
    set(_adb_serial_args "")
endif()

if(APK_CMD AND ADB_EXE)
    add_custom_target(install-apk
        COMMAND "${ADB_EXE}" ${_adb_serial_args} install -r
                "${ANDROID_SELF_DIR}/out/XGuiDemo-debug.apk"
        COMMAND "${ADB_EXE}" ${_adb_serial_args} shell am start
                -n com.xinyuec.xguidemo/com.xinyuec.android.XGuiNativeActivity
        DEPENDS apk
        COMMENT "安装并启动 XGuiDemo（设备：${ANDROID_SERIAL}）"
        VERBATIM)
endif()

# ---- 一键默认目标 ----
if(APK_CMD)
    add_custom_target(all-apk ALL
        COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target apk
        COMMENT "默认目标：编译 + 打包"
        VERBATIM)
endif()
