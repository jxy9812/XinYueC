cmake_minimum_required(VERSION 3.21)
project(android_apk NONE)

# ============================================================================
# Android APK — CMake 编排层（按「应用目录」驱动，本层不认识具体 app）
# ============================================================================
# 与 platforms/esp8266/CMakeLists.txt 同一形态：真正的编译不在这里。
#   - 原生库（如 libxguidemo.so / libxhsfclient.so）由**仓库根 CMakeLists.txt**
#     的对应目标编译（NDK 工具链 + 应用自己的源码）。
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
#
# 【应用目录】一个「安卓应用」= 一个目录，内含两个文件：
#   AndroidManifest.xml —— 应用身份唯一事实源（应用 id / 原生库名 / 应用名）
#   app.env             —— 构建侧信息（CMake 目标 / 构建目录前缀 / 产物名）
#   应用目录随应用走：平台层自带的模板应用（XGuiDemo）用本目录；其它应用
#   用 -DANDROID_APP_DIR=<自己的目录>（如 HighSpeedFilter/client/android）。
#   应用 id（package）由本层从清单派生并传给根工程 → 根工程据此注入
#   XFONT_EXTERNAL_FT_FONT_DIR（字体释放目录必须与应用私有目录一致）。
# ============================================================================

set(ANDROID_SELF_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(REPO_ROOT "${ANDROID_SELF_DIR}/../.." ABSOLUTE)

# ---- 可 -D 覆盖的参数 ----
set(ANDROID_ABI_NAME "x86_64" CACHE STRING "目标 ABI: x86_64 | arm64-v8a")
set(ANDROID_API_LEVEL "24" CACHE STRING "minSdkVersion / target platform")
set(ANDROID_NDK_ROOT "D:/android-ndk/android-ndk-r29" CACHE PATH "Android NDK 根目录")
set(ANDROID_APP_DIR "" CACHE STRING
    "安卓应用目录（含 AndroidManifest.xml 与 app.env）；相对路径按**仓库根**解析，如 HighSpeedFilter/client/android")
# 相对路径按**仓库根**解析（与打包脚本 build_apk.sh 的基准一致）——不要用
# CACHE PATH：那种类型会被 CMake 自动绝对化，基准是顶层源目录，与本层的
# 基准不一致（实测 ../../ 会被多退一级）。故这里用 STRING + 自己绝对化。
if(ANDROID_APP_DIR STREQUAL "")
    set(ANDROID_APP_DIR "${ANDROID_SELF_DIR}")   # 缺省=平台层自带的模板应用
endif()
get_filename_component(ANDROID_APP_DIR "${ANDROID_APP_DIR}" ABSOLUTE
                       BASE_DIR "${REPO_ROOT}")

set_property(CACHE ANDROID_ABI_NAME PROPERTY STRINGS x86_64 arm64-v8a)

# ---- 读应用描述（app.env）与清单（应用 id 的唯一事实源）----
set(ANDROID_APP_ENV "${ANDROID_APP_DIR}/app.env")
set(ANDROID_APP_MANIFEST "${ANDROID_APP_DIR}/AndroidManifest.xml")
if(NOT EXISTS "${ANDROID_APP_ENV}")
    message(FATAL_ERROR
        "找不到应用描述：${ANDROID_APP_ENV}\n"
        "  应用目录须含 app.env（构建侧信息）与 AndroidManifest.xml（应用身份）。")
endif()
if(NOT EXISTS "${ANDROID_APP_MANIFEST}")
    message(FATAL_ERROR "找不到应用清单：${ANDROID_APP_MANIFEST}")
endif()
file(STRINGS "${ANDROID_APP_ENV}" _app_env_lines)
foreach(_line IN LISTS _app_env_lines)
    if(_line MATCHES "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*(.+)$")
        set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()
foreach(_need APP_TARGET APP_BUILD_PREFIX APP_APK_NAME)
    if(NOT DEFINED ${_need} OR "${${_need}}" STREQUAL "")
        message(FATAL_ERROR "app.env 缺 ${_need}：${ANDROID_APP_ENV}")
    endif()
endforeach()
# 应用 id：清单里的 package（唯一事实源）→ 同时是字体释放目录的来源。
file(READ "${ANDROID_APP_MANIFEST}" _manifest_text)
string(REGEX MATCH "package=\"([^\"]+)\"" _pkg_match "${_manifest_text}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "清单里读不到 package：${ANDROID_APP_MANIFEST}")
endif()
set(ANDROID_APP_PKG "${CMAKE_MATCH_1}")
message(STATUS "android app: target=${APP_TARGET} pkg=${ANDROID_APP_PKG} "
               "apk=${APP_APK_NAME} dir=${ANDROID_APP_DIR}")

# ---- 构建目录：与 app.env 的前缀一致（每个应用一棵独立的树）----
# ⚠ 字体目录宏 XFONT_EXTERNAL_FT_FONT_DIR 是**构建树级**常量（被静态库
#   内的 XFontFt.c 消费），一棵树只能服务一个应用 id——两个应用共用一棵树
#   会「字库释放到 A、FT 去 B 找」→ 中文整片缺失（真机事故记录见
#   platforms/android/README.md）。故 demo 与 HSF 客户端各用各的目录。
if(ANDROID_ABI_NAME STREQUAL "arm64-v8a")
    set(_android_abi_dir "arm64")
else()
    set(_android_abi_dir "${ANDROID_ABI_NAME}")
endif()
set(ANDROID_BUILD_DIR "${REPO_ROOT}/out/${APP_BUILD_PREFIX}-${_android_abi_dir}"
    CACHE PATH "原生库构建目录")

# ---- 工具链自检：缺什么直接报，不要跑一半才失败 ----
set(_android_toolchain "${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${_android_toolchain}")
    message(FATAL_ERROR
        "找不到 NDK 工具链：${_android_toolchain}\n"
        "  请用 -DANDROID_NDK_ROOT=<NDK 路径> 指定，或设环境变量 ANDROID_NDK。")
endif()

# ---- 原生库：配置 + 编译根工程的应用目标 ----
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
            # 应用 id → 根工程注入字体目录宏（/data/data/<pkg>/files/fonts），
            # 必须与本应用的清单 package 一致（此处即从清单派生）。
            -DANDROID_APP_PKG=${ANDROID_APP_PKG}
            # 显式锚定产物目录=本构建目录（.so 落 out/<前缀>-<abi>/，打包
            # 脚本按此找）。不传则机器本地 CMake_Config.cmake 可能把
            # CMAKE_RUNTIME_OUTPUT_DIRECTORY 改指仓库 bin/，被根工程的
            # 交叉构建守卫拒绝（防交叉产物覆盖桌面门禁二进制）。
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${ANDROID_BUILD_DIR}
            # 嵌套 configure 必须显式带上 make 程序路径：本层的 ninja 来自
            # VS 自带目录（不在 PATH 上），不传则嵌套 CMake 报
            # "unable to find a build program corresponding to Ninja"。
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    COMMAND ${CMAKE_COMMAND} --build "${ANDROID_BUILD_DIR}" --target ${APP_TARGET}
    WORKING_DIRECTORY "${REPO_ROOT}"
    BYPRODUCTS "${ANDROID_BUILD_DIR}/lib${APP_TARGET}.so"
    COMMENT "Android 原生库：配置并编译 ${APP_TARGET} (${ANDROID_ABI_NAME})"
    VERBATIM)

# ---- 打包：优先 bash 版，退回 PowerShell 版 ----
# bash 版支持 x86_64 / arm64-v8a / both；PowerShell 版当前只处理单 ABI。
# 两者都按「应用目录」参数工作，本层不传 app 名——应用身份由目录内文件决定。
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
    set(APK_CMD "${BASH_EXE}" "${ANDROID_SELF_DIR}/build_apk.sh" "${ANDROID_ABI_NAME}" "${ANDROID_APP_DIR}")
    set(APK_TOOL_DESC "build_apk.sh (bash)")
elseif(POWERSHELL_EXE)
    set(APK_CMD "${POWERSHELL_EXE}" -ExecutionPolicy Bypass
                -File "${ANDROID_SELF_DIR}/pack_apk.ps1" -Abi "${ANDROID_ABI_NAME}" -AppDir "${ANDROID_APP_DIR}")
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
        BYPRODUCTS "${ANDROID_SELF_DIR}/out/${APP_APK_NAME}"
        COMMENT "打包 APK（${APK_TOOL_DESC}）"
        VERBATIM)
endif()

# ---- 安装到设备（adb）----
find_program(ADB_EXE adb
    HINTS
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
                "${ANDROID_SELF_DIR}/out/${APP_APK_NAME}"
        # 组件名 = 清单包 + 完整 Activity 类名（Java 包与清单包是两个概念，
        # 简写 ".XGuiNativeActivity" 只在类名与清单包同前缀时成立）。
        COMMAND "${ADB_EXE}" ${_adb_serial_args} shell am start
                -n ${ANDROID_APP_PKG}/com.xinyuec.android.XGuiNativeActivity
        DEPENDS apk
        COMMENT "安装并启动 ${ANDROID_APP_PKG}（设备：${ANDROID_SERIAL}）"
        VERBATIM)
endif()

# ---- 一键默认目标 ----
if(APK_CMD)
    add_custom_target(all-apk ALL
        COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target apk
        COMMENT "默认目标：编译 + 打包"
        VERBATIM)
endif()
