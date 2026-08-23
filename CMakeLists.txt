#请求CAMKE的最小构建版本
cmake_minimum_required(VERSION 3.5)
#设置项目名称
project(Xin_Yue_C VERSION 0.1 LANGUAGES C)
set(EXPORT_NAME XinYueC)
#set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O0  -Wall")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0  -Wall")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX "d")

# ML-DSA-87 使用 PQCP 中 mldsa-native 的单编译单元实现，默认关闭以保持
# 常规构建的体积和编译时间。开启后算法归 XCryptographic 所有，mbedTLS
# 只保留其余 PQCP 驱动。
option(XCRYPTOGRAPHIC_ENABLE_MLDSA87
    "启用 XCryptographic 的 ML-DSA-87 密钥生成、签名和验签" OFF)
if(XCRYPTOGRAPHIC_ENABLE_MLDSA87)
    add_compile_definitions(XCRYPTOGRAPHIC_MLDSA87_ON=1)
endif()

# GCC 14 将部分旧 C 代码诊断提升为错误。项目的容器继承接口通过宏
# 保持现有的基类调用约定，因此暂时将这些兼容性诊断降回警告，避免
# 升级编译器改变公开 API 和已有源码的调用方式。
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    add_compile_options(
        -Wno-error=incompatible-pointer-types
        -Wno-error=return-mismatch
        -Wno-error=implicit-function-declaration
        -Wno-error=int-conversion
    )
endif()

# Linux 平台需要_GNU_SOURCE 以使用 PTHREAD_MUTEX_NORMAL 等
if(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE)
endif()

#设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)

# ==================== GBK转换实现切换 ====================
# 启用文件读写方式实现GBK转换（适用于嵌入式），注释此行则使用平台系统API
#add_compile_definitions(XCHAR_USE_FILE_GBK)
# 映射表文件路径（干净格式，无注释，固定4字节/行，支持文件二分）
add_compile_definitions(XCHAR_COMPACT_PATH="${CMAKE_CURRENT_LIST_DIR}/Library/XCHAR_COMPACT.BIN")
include(CMake_Config.cmake)
#查找include目录下的所有*.hpp,*.h头文件并路径列表保存到变量中
FIND_INCLUDE_DIR(INCLUDE_SRC_LIST "Src")			#调用函数，指定参数
FIND_INCLUDE_DIR(INCLUDE_DRIVE_LIST "Drive")			#调用函数，指定参数
FIND_INCLUDE_DIR(INCLUDE_TEST_LIST "Test")			#调用函数，指定参数
#将INCLUDE中路径列表加入工程	
include_directories(${INCLUDE_SRC_LIST})
include_directories(${INCLUDE_DRIVE_LIST})
include_directories(${INCLUDE_TEST_LIST})
# 公共平台接口直接位于 Src/XPlatform 下。这里显式加入该目录，避免递归
# 头文件目录收集器只记录父目录而在 Visual Studio 构建中遗漏此叶子目录。
include_directories(${CMAKE_CURRENT_LIST_DIR}/Src/XPlatform)
#递归搜索目录下的文件添加到变量中
file(GLOB_RECURSE SRC_FILE "Src/*.c" )
file(GLOB_RECURSE TEST_FILE "Test/*.c" )
file(GLOB_RECURSE DRIVE_FILE "Drive/*.c")
add_library(${EXPORT_NAME}S  STATIC  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE})
add_library(${EXPORT_NAME}  SHARED  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE} ${PUBLIC_HDRS})

# ==================== 外部库（以静态库方式构建，合并到 XinYueC 动态库中）====================
# 注意：外部库（lwip/zlib/fatfs）内部引用了 XinYueC 核心符号（XMalloc_*、XCrc32_*、
# XRandomGenerator_*、XTimerData_* 等），与 XinYueC 存在双向依赖，无法独立为动态库。
# 因此保持为静态库，由 XinYueC 动态库统一链接。

# ==================== X11（Linux 原生窗口后端） =====================
# XPlatformNativeWindow_posix.c 需要 Xlib 头与运行库。检测到 X11 时注入
# XINYUE_C_HAS_X11=1 编译宏并公开链接 X11 库（静态库 PUBLIC 传播给可执行程序，
# 动态库 PRIVATE 仅自身链接阶段解析）；未检出时 Posix 原生窗口文件整段被
# 守卫裁剪，由 Drive/XPlatformNativeWindow_unsupported.c 兜底，XWindow 回落
# 虚拟 WId 行为，嵌入式构建不受影响。
if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND)
        add_compile_definitions(XINYUE_C_HAS_X11=1)
        include_directories(${X11_INCLUDE_DIRS})
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${X11_LIBRARIES})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${X11_LIBRARIES})
    endif()

    # XPlatformGraphics 的 GLX 后端只在同时检测到 X11 与 OpenGL 时启用。
    # Vulkan 单独按库存在性启用，保持图形模块可独立裁剪。
    find_package(OpenGL QUIET)
    if(X11_FOUND AND OPENGL_FOUND)
        add_compile_definitions(XINYUE_C_HAS_OPENGL=1)
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${OPENGL_gl_LIBRARY})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${OPENGL_gl_LIBRARY})
    endif()
    find_library(XINYUE_VULKAN_LIBRARY NAMES vulkan)
    if(XINYUE_VULKAN_LIBRARY)
        add_compile_definitions(XINYUE_C_HAS_VULKAN=1)
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_VULKAN_LIBRARY})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_VULKAN_LIBRARY})
    endif()

    # Linux AT-SPI 辅助功能后端使用 dbus-1；缺失开发包时自动回退 Drive
    # 存根，嵌入式配置不会引入 D-Bus 依赖。
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XINYUE_DBUS QUIET dbus-1)
        if(XINYUE_DBUS_FOUND)
            add_compile_definitions(XINYUE_C_HAS_DBUS=1)
            include_directories(${XINYUE_DBUS_INCLUDE_DIRS})
            link_directories(${XINYUE_DBUS_LIBRARY_DIRS})
            target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_DBUS_LIBRARIES})
            target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_DBUS_LIBRARIES})
        endif()
        pkg_check_modules(XINYUE_FONTCONFIG QUIET fontconfig)
        if(XINYUE_FONTCONFIG_FOUND)
            add_compile_definitions(XINYUE_C_HAS_FONTCONFIG=1)
            include_directories(${XINYUE_FONTCONFIG_INCLUDE_DIRS})
            link_directories(${XINYUE_FONTCONFIG_LIBRARY_DIRS})
            target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_FONTCONFIG_LIBRARIES})
            target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_FONTCONFIG_LIBRARIES})
        endif()
    endif()
endif()

# ==================== zlib（压缩库）====================
# 以子目录静态库方式集成，详见 Library/zlib/CMakeLists.txt
add_subdirectory(Library/zlib)
# ==================== FatFs（FAT/exFAT 文件系统）====================
# 以子目录静态库方式集成，详见 Library/Fatfs/CMakeLists.txt
add_subdirectory(Library/Fatfs)
# ==================== lwIP（轻量级 TCP/IP 协议栈）====================
# 以子目录静态库方式集成，详见 Library/lwip/CMakeLists.txt
add_subdirectory(Library/lwip)
# ==================== mbedTLS（XinYueC SSL/TLS 后端）====================
# 以子目录静态库方式集成，详见 Library/mbedtls/CMakeLists.txt
add_subdirectory(Library/mbedtls)
# ==================== PCRE2（Qt 6.8 正则表达式引擎）====================
add_subdirectory(Library/pcre2)
# ==================== SQLite（源码方式集成）====================
# SQLite 作为可替换的源码组件编译，适配器不依赖系统 libsqlite3。
add_subdirectory(Library/sqlite)

# 链接 mbedTLS 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE mbedtls)
target_link_libraries(${EXPORT_NAME} PRIVATE mbedtls)
# 链接 PCRE2 16-bit 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE pcre2)
target_link_libraries(${EXPORT_NAME} PRIVATE pcre2)
# 链接 SQLite 源码静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE sqlite)
target_link_libraries(${EXPORT_NAME} PRIVATE sqlite)
# 链接 lwIP 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE lwip)
target_link_libraries(${EXPORT_NAME} PRIVATE lwip)
# 链接 zlib 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE zlib)
target_link_libraries(${EXPORT_NAME} PRIVATE zlib)
# 链接 FatFs 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE fatfs)
target_link_libraries(${EXPORT_NAME} PRIVATE fatfs)
# Windows 平台依赖：
# - Advapi32：XSystem 后端调整关机权限；
# - User32：窗口类注册/句柄操作/消息泵（XPlatformNativeWindow_win32.c）；
# - Gdi32：DIB 后备缓冲 + BitBlt/SetDIBitsToDevice 上屏
#   （XPlatformBackingStore_win32.c / XPlatformNativeWindow_win32.c）。
# 静态库将依赖公开给最终可执行文件，动态库在自身链接阶段解析。
if(WIN32)
    # Vulkan 仅由 XPlatformGraphics_win32.c 的可选后端使用；
    # 本机未安装 Vulkan SDK 时不定义 XINYUE_C_HAS_VULKAN，
    # 不包含 vulkan.h 也不链接 Vulkan::Vulkan，避免无 Vulkan 环境编译失败。
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_compile_definitions(XINYUE_C_HAS_VULKAN=1)
        include_directories(${Vulkan_INCLUDE_DIRS})
        set(XINYUE_WIN32_VULKAN_VAR Vulkan::Vulkan)
        message(STATUS "Vulkan found: ${Vulkan_INCLUDE_DIRS}")
    endif()
    set(XINYUE_WIN32_LIBS advapi32 user32 gdi32 opengl32 imm32 shell32 ole32 uiautomationcore)
    if(XINYUE_WIN32_VULKAN_VAR)
        list(APPEND XINYUE_WIN32_LIBS ${XINYUE_WIN32_VULKAN_VAR})
    endif()
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_WIN32_LIBS})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_WIN32_LIBS})
endif()
# Windows(MSVC): mbedtls_platform_get_entropy() 调用 BCryptGenRandom（需 bcrypt.lib）。
# 已开启 MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG，TLS/PSA 运行时随机数走 XRandomGenerator，
# 但 platform_util.c 仍被编译并链接，故 Windows 上必须链接 bcrypt 解析符号。
# Linux 用 getrandom()/urandom，无需额外库。

# ==================== 静态链接的可执行程序 ====================
# 链接 XinYueCS 静态库，生成的可执行文件不依赖外部动态库（除系统库外）
add_executable(${EXPORT_NAME}_Static
${TEST_FILE}
"./main.c"
)
set_target_properties(${EXPORT_NAME}_Static PROPERTIES
    OUTPUT_NAME "${EXPORT_NAME}_Static"
)
# Windows(MSVC): 数学函数内置在 C 运行时，线程使用 Drive/windows 的 Win32 实现，无需链接 m/pthread
# Unix(Linux/macOS/BSD): 需链接数学库 m 与线程库 pthread（对应 Drive/Posix 实现，源码已用
#                         __linux__/__APPLE__/__BSD__ 宏守护，仅在这些平台编译）
if(UNIX)
    target_link_libraries(${EXPORT_NAME}_Static ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
else()
    target_link_libraries(${EXPORT_NAME}_Static ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
endif()

# ==================== 动态链接的可执行程序 ====================
# 链接 XinYueC 动态库，生成的可执行文件运行时需附带 XinYueC.dll（Windows）或 libXinYueC.so（Linux）
add_executable(${EXPORT_NAME}_Dynamic
${TEST_FILE}
"./main.c"
)
set_target_properties(${EXPORT_NAME}_Dynamic PROPERTIES
    OUTPUT_NAME "${EXPORT_NAME}_Dynamic"
)
if(UNIX)
    target_link_libraries(${EXPORT_NAME}_Dynamic ${EXPORT_NAME} m pthread pcap mbedtls lwip zlib fatfs pcre2 sqlite)
else()
    target_link_libraries(${EXPORT_NAME}_Dynamic ${EXPORT_NAME} mbedtls lwip zlib fatfs pcre2 sqlite)
endif()

# ==================== XFtp 端到端真服务器联调测试 ====================
# 独立可执行文件 ftp_e2e_test.exe，搭配 ftp_test_server.py 跑完整 FTP 协议测试
add_executable(FtpE2E_Test
    "./ftp_e2e_test.c"
)
if(UNIX)
    target_link_libraries(FtpE2E_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(FtpE2E_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XGui 对齐回归测试 ====================
add_executable(XGuiRegression_Test
    "./xgui_regression_test.c"
)
if(UNIX)
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== Linux X11 XDND 跨客户端端到端测试 ====================
# 用独立 Xlib 客户端模拟拖放源，验证 XDND 状态应答、Selection 数据传输和
# XDropEvent 负载。仅 Linux 且已找到 X11 时构建，嵌入式裁剪不引入平台依赖。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    add_executable(XGuiXdnd_Test
        "./xgui_xdnd_test.c"
    )
    target_link_libraries(XGuiXdnd_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${X11_LIBRARIES} ${EXPORT_NAME}S)
endif()

# ==================== XGui 第一个真实窗口程序 ====================
# 桌面原生窗口验收样例：XWindow + XGuiApplication + XBackingStore 走完整的
# 「平台原生窗口（X11/Win32）-> 事件注入 -> 重绘 -> 上屏」链路。
# 运行：无参数常驻，带参数可传入自动退出秒数（供无窗口管理器环境截图）。
add_executable(XGuiWindowDemo_Test
    "./xgui_window_demo.c"
)
if(UNIX)
    target_link_libraries(XGuiWindowDemo_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiWindowDemo_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

#安装
include(CMake_Install.cmake)
