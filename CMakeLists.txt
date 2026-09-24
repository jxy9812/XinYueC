#请求CAMKE的最小构建版本
cmake_minimum_required(VERSION 3.5)
#设置项目名称
project(Xin_Yue_C VERSION 0.1 LANGUAGES C)

# ---- 裁剪开关转发：命令行 -DX?_ON=0/1 转为全局编译定义 ----
# （否则 -D 仅入缓存为 UNINITIALIZED，不产生编译定义，"裁剪构建"实际全量编译。）
option(XGUI_ON "XGui 模块总开关（默认开；-DXGUI_ON=0 关闭）" ON)
foreach(_xsw XGUI_ON XGPU_ON XIMAGEIOPLUGIN_ON XSVGICON_ON XPIXMAPCACHE_ON XMOVIE_ON XPLATFORM_FBDEV_ON)
    if(DEFINED ${_xsw})
        set(_xval "${${_xsw}}")
        if(_xval STREQUAL "ON" OR _xval STREQUAL "TRUE")
            set(_xval 1)
        elseif(_xval STREQUAL "OFF" OR _xval STREQUAL "FALSE")
            set(_xval 0)
        endif()
        add_compile_definitions(${_xsw}=${_xval})
        unset(_xval)
    endif()
endforeach()

# ==================== fbdev 显示驱动模板（嵌入式 RGB565 直写铺路） ====================
# 新增源文件 Drive/Posix/Graphics/XPlatformFramebuffer_posix.c（及其 .h）经上方
# file(GLOB_RECURSE DRIVE_FILE "Drive/*.c") 自动入列——新增/删除源文件后须触发
# 重新配置（touch 本文件即可），glob 才会重扫。功能受 XPLATFORM_FBDEV_ON 控制
# （默认 0，XPlatformDisplayDriver.h 内 #ifndef 兜底；嵌入式目标以
# -DXPLATFORM_FBDEV_ON=ON 开启）。默认关闭时该文件经预处理守卫整段裁剪，
# 桌面 X11 路径零变化；无需额外链接库（fbdev 经 sys/ioctl.h + mmap 即可）。
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

#设置可执行文件的输出目录（可用 -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=... 覆盖，
#供 Release 门等旁路构建输出到独立目录，避免覆盖 bin/ 的既有产物）
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)
endif()

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
# XGuiDemo 是自带 main() 的独立演示可执行（见下方 XGuiWindowDemo_Test 目标），
# 不能被卷进 Static/Dynamic 主程序（会与 main.c 重复定义 main）。
file(GLOB_RECURSE XGUI_DEMO_FILE "Test/XGuiDemo/*.c" )
if(XGUI_DEMO_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_DEMO_FILE})
endif()
# XGuiApiTest 同理：自带 main 的独立 API 测试套件（XGuiApi_Test 目标，
# 接线见文件尾注释），不得卷入主程序。
file(GLOB_RECURSE XGUI_API_TEST_FILE "Test/XGuiApiTest/*.c" )
if(XGUI_API_TEST_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_API_TEST_FILE})
endif()
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
        # RandR（屏幕枚举/几何变化，XPlatformNativeWindow_posix.c 屏幕段）：
        # 优先 dev 安装的 Xrandr 链接名；仅有运行库（libXrandr.so.2）时按
        # 版本名兜底（函数声明内联在平台文件中，无需 libxrandr-dev 头）。
        # 未检出时不注入 XINYUE_C_HAS_XRANDR，屏幕枚举回落 XScreenOfDisplay。
        find_library(XINYUE_XRANDR_LIBRARY NAMES Xrandr)
        if(NOT XINYUE_XRANDR_LIBRARY)
            find_library(XINYUE_XRANDR_LIBRARY NAMES libXrandr.so.2 Xrandr.so.2)
        endif()
        if(XINYUE_XRANDR_LIBRARY)
            add_compile_definitions(XINYUE_C_HAS_XRANDR=1)
            target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_XRANDR_LIBRARY})
            target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_XRANDR_LIBRARY})
        endif()
        # XI2（触摸/平滑滚轮，XPlatformNativeWindow_posix.c XI2 段）：
        # 优先 dev 链接名 Xi；仅有运行库（libXi.so.6）时按版本名兜底
        #（结构与函数原型内联在平台文件中，无需 libxi-dev 头）。
        # 未检出时不注入 XINYUE_C_HAS_XI2，输入回落核心协议。
        find_library(XINYUE_XI_LIBRARY NAMES Xi)
        if(NOT XINYUE_XI_LIBRARY)
            find_library(XINYUE_XI_LIBRARY NAMES libXi.so.6 Xi.so.6)
        endif()
        # 头文件（XI2.h 协议常量，xorgproto 提供）必须与运行库同时存在：
        # 只有运行库无头（无 libxi-dev/xorgproto）时编译不过，回落核心协议。
        include(CheckIncludeFiles)
        check_include_files("X11/extensions/XI2.h" XINYUE_XI2_HEADER)
        if(XINYUE_XI_LIBRARY AND XINYUE_XI2_HEADER)
            add_compile_definitions(XINYUE_C_HAS_XI2=1)
            target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_XI_LIBRARY})
            target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_XI_LIBRARY})
        endif()
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

# POSIX USB Host 后端使用 libusb-1.0。该探测独立于 Linux 桌面图形依赖，
# 因而同样覆盖 macOS、BSD 和裁剪后的嵌入式 Linux 系统。
if(UNIX)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XINYUE_LIBUSB QUIET libusb-1.0)
    endif()
    if(XINYUE_LIBUSB_FOUND)
        add_compile_definitions(XINYUE_C_HAS_LIBUSB=1)
        include_directories(${XINYUE_LIBUSB_INCLUDE_DIRS})
        link_directories(${XINYUE_LIBUSB_LIBRARY_DIRS})
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_LIBUSB_LIBRARIES})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_LIBUSB_LIBRARIES})
    else()
        find_path(XINYUE_LIBUSB_INCLUDE_DIR libusb-1.0/libusb.h)
        find_library(XINYUE_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
        if(XINYUE_LIBUSB_INCLUDE_DIR AND XINYUE_LIBUSB_LIBRARY)
            add_compile_definitions(XINYUE_C_HAS_LIBUSB=1)
            include_directories(${XINYUE_LIBUSB_INCLUDE_DIR})
            target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_LIBUSB_LIBRARY})
            target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_LIBUSB_LIBRARY})
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
# ==================== TinyUSB（嵌入式 USB 协议栈）====================
# 以子目录静态库方式集成，详见 Library/tinyusb/CMakeLists.txt
# 默认不启用，仅嵌入式构建通过 XINYUE_C_HAS_TINYUSB 开启。
add_subdirectory(Library/tinyusb)

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
    set(XINYUE_WIN32_LIBS advapi32 user32 gdi32 opengl32 imm32 shell32 ole32 uiautomationcore setupapi winusb cfgmgr32 hid)
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
# XGUI_ON=0（嵌入式全裁剪）时与回归测试同口径排除：demo（main.c）与
# Test/*.c 控件测试源引用 GUI 类型，裁剪构建无法编译。
if(XGUI_ON)
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
endif() # XGUI_ON（demo 可执行排除）

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
# XGUI_ON=0（嵌入式全裁剪）时 GUI 类型全部消失，回归测试(引用 GUI 类型)
# 与 GUI 控件测试源一并排除。
if(XGUI_ON)
add_executable(XGuiRegression_Test
    "./xgui_regression_test.c"
    "./Test/XGuiTest/XLineEditTest.c"
    "./Test/XGuiTest/XSliderTest.c"
    "./Test/XGuiTest/XSpinBoxTest.c"
    "./Test/XGuiTest/XGroupBoxTest.c"
    "./Test/XGuiTest/XProgressBarTest.c"
    "./Test/XGuiTest/XDialTest.c"
    "./Test/XGuiTest/XComboBoxTest.c"
    "./Test/XGuiTest/XTabBarTest.c"
    "./Test/XGuiTest/XLcdNumberTest.c"
    "./Test/XGuiTest/XScrollBarTest.c"
    "./Test/XGuiTest/XStackedWidgetTest.c"
    "./Test/XGuiTest/XButtonGroupTest.c"
    "./Test/XGuiTest/XTouchMultiPointTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON

# ==================== XLineControl 控制器验收测试 ====================
if(XGUI_ON)
add_executable(XLineControl_Acceptance_Test
    "./xgui_linecontrol_acceptance_test.c"
    "./Test/XGuiTest/XLineControlAcceptance.c"
)
if(UNIX)
    target_link_libraries(XLineControl_Acceptance_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XLineControl_Acceptance_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
add_test(NAME XLineControlAcceptance COMMAND XLineControl_Acceptance_Test)

endif() # XGUI_ON
enable_testing()
# 测试以相对路径访问仓库资源（assets/、../Library/XFont 等），其直跑口径
# 是仓库 bin/ 目录（./bin/XGuiRegression_Test）；CTest 显式对齐该工作目录，
# 避免构建目录层级（如 out/build/x64-Debug）破坏 ../ 回退。
add_test(NAME XGuiRegression COMMAND XGuiRegression_Test
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/bin")
# CTest 在受控运行环境下会以 ptrace 方式启动子进程；LeakSanitizer 无法在该
# 环境工作。地址越界检查仍由 ASan 保留，泄漏检查在可直接运行的环境单独执行。
set_tests_properties(XGuiRegression PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")
# GPU 后端下的完整回归变体：无 GL 的环境由后端自动回退软件（断言按
# 实际会话状态跳过降级项），有 GL 时逐用例覆盖 GPU 快速路径。
add_test(NAME XGuiRegressionGpu COMMAND XGuiRegression_Test
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/bin")
set_tests_properties(XGuiRegressionGpu PROPERTIES
    ENVIRONMENT "XGUI_RENDER_BACKEND=gpu;XGUI_GPU_SYNC=1;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")

# ==================== Linux X11 XDND 跨客户端端到端测试 ====================
# 用独立 Xlib 客户端模拟拖放源，验证 XDND 状态应答、Selection 数据传输和
# XDropEvent 负载。仅 Linux 且已找到 X11 时构建，嵌入式裁剪不引入平台依赖。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    add_executable(XGuiXdnd_Test
        "./xgui_xdnd_test.c"
    )
    target_link_libraries(XGuiXdnd_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${X11_LIBRARIES} ${EXPORT_NAME}S)
endif()

# ==================== XGui 统一可视化控件测试程序 ====================
# 桌面原生窗口控件验收入口：XWindow + XGuiApplication + XBackingStore 走完整的
# 「平台原生窗口（X11/Win32）-> 事件注入 -> 重绘 -> 上屏」链路。
# 运行：无参数常驻，带参数可传入自动退出秒数（供无窗口管理器环境截图）。
if(XGUI_ON)
add_executable(XGuiWindowDemo_Test
    "./Test/XGuiDemo/xgui_window_demo.c"
    "./Test/XGuiDemo/xgui_demo_page_views.c"
    "./Test/XGuiDemo/xgui_demo_page_dialogs.c"
    "./Test/XGuiDemo/xgui_demo_page_advanced.c"
    "./Test/XGuiDemo/xgui_demo_page_effects.c"
    "./Test/XGuiDemo/xgui_demo_apitest_buttons.c"
    "./Test/XGuiDemo/xgui_demo_apitest_labels.c"
    "./Test/XGuiDemo/xgui_demo_apitest_input.c"
    "./Test/XGuiDemo/xgui_demo_apitest_text.c"
    "./Test/XGuiDemo/xgui_demo_apitest_views.c"
    "./Test/XGuiDemo/xgui_demo_apitest_containers.c"
    "./Test/XGuiDemo/xgui_demo_apitest_dialogs.c"
    "./Test/XGuiDemo/xgui_demo_apitest_menus.c"
    "./Test/XGuiDemo/xgui_demo_apitest_core.c"
)
if(UNIX)
    target_link_libraries(XGuiWindowDemo_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiWindowDemo_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON

# ==================== XGui GPU 阶段 1 冒烟测试 ====================
add_executable(XGuiGpu_Test
    "./xgui_gpu_test.c"
)
if(UNIX)
    target_link_libraries(XGuiGpu_Test ${EXPORT_NAME}S m pthread pcap mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiGpu_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
add_test(NAME XGuiGpu COMMAND XGuiGpu_Test)
set_tests_properties(XGuiGpu PROPERTIES
    ENVIRONMENT "XGUI_RENDER_BACKEND=gpu;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")

#安装
include(CMake_Install.cmake)
