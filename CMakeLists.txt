#请求CAMKE的最小构建版本
cmake_minimum_required(VERSION 3.5)
#设置项目名称
project(Xin_Yue_C VERSION 0.1 LANGUAGES C)

# ---- 裁剪开关转发：命令行 -DX?_ON=0/1 转为全局编译定义 ----
# （否则 -D 仅入缓存为 UNINITIALIZED，不产生编译定义，"裁剪构建"实际全量编译。）
option(XGUI_ON "XGui 模块总开关（默认开；-DXGUI_ON=0 关闭）" ON)
foreach(_xsw XGUI_ON XGPU_ON XIMAGEIOPLUGIN_ON XSVGICON_ON XPIXMAPCACHE_ON XMOVIE_ON
             XPLATFORM_FBDEV_ON XPLATFORM_FBINPUT_ON XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
             XGUI_BACKINGSTORE_RENDER_MODE XGUI_BACKINGSTORE_BUFFER_COUNT)
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

# ---- XGuiRemote 编解码 NEON 加速（perf9 路4，2026-10-05） ----
# 仅对 XGuiRemoteCodec.c 追加 -mfpu=neon（ARM 交叉目标默认开；-DXGUI_REMOTE_NEON=0
# 显式关回）。ABI 恒 softfp 不变（工具链默认 -mfloat-abi=softfp，NEON 只改指令
# 集不改传参），现网 A33（Cortex-A7, /proc/cpuinfo 含 neon）可执行。源内经
# XGuiRemoteCodecNeon.h 以 __ARM_NEON__ 感知，桌面 x86/无 NEON 产物整头裁空
# 恒标量——输出逐字节等价由 verify/perf9 rle_bench NEON 版 0 mismatch 红线
# 自证（out/perf9/neon-bench.txt），未初始化安全由行内夹取+尾块标量回退保证。
option(XGUI_REMOTE_NEON
    "XGuiRemote 编解码热循环 NEON 加速（仅 ARM 交叉目标有意义）" ON)
if(XGUI_REMOTE_NEON AND CMAKE_C_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiRemoteCodec.c"
        PROPERTIES COMPILE_OPTIONS "-mfpu=neon")
endif()

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
# Clang 16+（含 NDK r29 的 Clang 21，安卓交叉编译）同步提升了同类
# 诊断，按同一策略降回警告。
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR
   (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14))
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

# ==================== 交叉构建防呆（configure 期硬门） ====================
# 事故防线：交叉构建（armel 等 toolchain 配置）的运行时输出目录若指向仓库 bin/，
# ARM 产物将覆盖桌面 x86 门禁二进制（./bin/XGuiRegression_Test 等，历史事故重演）。
# 故 configure 期直接 FATAL_ERROR，并给出私有输出目录的正确姿势。
if(CMAKE_CROSSCOMPILING)
    get_filename_component(_XY_REPO_BIN_REAL "${CMAKE_CURRENT_LIST_DIR}/bin" REALPATH)
    get_filename_component(_XY_OUT_REAL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" REALPATH)
    if(_XY_OUT_REAL STREQUAL _XY_REPO_BIN_REAL)
        message(FATAL_ERROR
            "交叉构建(${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR})的 "
            "CMAKE_RUNTIME_OUTPUT_DIRECTORY 指向仓库 bin/：${_XY_OUT_REAL}\n"
            "ARM/交叉产物会覆盖桌面 x86 门禁二进制（历史事故）。请改用私有输出目录，例如：\n"
            "  cmake -B build-armel -DCMAKE_TOOLCHAIN_FILE=<tc>.cmake "
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/tmp/armel-out\n"
            "诊断/交叉构建一律私有输出目录，严禁写仓库 bin/。")
    endif()
    message(STATUS "交叉构建输出目录（已校验非仓库 bin/）: ${_XY_OUT_REAL}")
    unset(_XY_REPO_BIN_REAL)
    unset(_XY_OUT_REAL)
endif()

# ==================== GBK转换实现切换 ====================
# 启用文件读写方式实现GBK转换（适用于嵌入式），注释此行则使用平台系统API
#add_compile_definitions(XCHAR_USE_FILE_GBK)
# Android 的 bionic 无 iconv 实现，XChar_conf.h 自动检测已让安卓回落
# 文件映射表模式，无需（也不得）在此追加定义——与自动检测叠加会触发
# XChar_conf.h 的多模式 #error。
# Android：开启显示驱动契约（XPlatformDisplayDriver.h 整体受
# XPLATFORM_FBDEV_ON 门控）。安卓 NativeActivity 是与 fbdev 同构的
# 单屏直写模型，XPlatformWindowAndroid.c 经该契约报告 surface 能力。
# 同时把轮廓字库（GB2312 汉字全集，约 3MiB）编入目标：安卓无
# ../Library/XFont 外挂目录，XFontOutlineFace 的外挂文件加载不可达，
# 内嵌是项目文档标注的免文件系统部署路径（XFont_config.h）。
if(ANDROID)
    add_compile_definitions(XPLATFORM_FBDEV_ON=1 XFONT_BUILTIN_OUTLINE_ON=1)
endif()
# 强制 DPI（XGUI_FORCE_DPI，XGuiConfig.h 有 #ifndef 兜底 0）：命令行
# -DXGUI_FORCE_DPI=2.0 调试缩放链/小屏放大画面。CMake -D 只建变量不自动
# 变编译定义，此处显式转发（全平台，非安卓限定）。
if(DEFINED XGUI_FORCE_DPI AND NOT XGUI_FORCE_DPI STREQUAL "" AND NOT XGUI_FORCE_DPI STREQUAL "0")
    add_compile_definitions(XGUI_FORCE_DPI=${XGUI_FORCE_DPI})
endif()
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
# XDateEdit.c/XTimeEdit.c（XDateTimeEdit 同族子类控件，Src/XGui/Widget/）
# 经本 glob 自动入列——新增/删除源文件后须触发重新配置（改动本文件即可）。
file(GLOB_RECURSE SRC_FILE "Src/*.c" )
file(GLOB_RECURSE TEST_FILE "Test/*.c" )
# 拼音 IME 翻译单元显式登记（XKEYBOARD_IME_ON 门控）：GLOB 列表在
# 配置期固化，既有构建树不重配时 glob 感知不到新增文件（XPinyinEngine.c/
# XPinyinTable.c 不入列即 LNK2019，第 1 轮门禁实锅）；REMOVE_
# DUPLICATES 兜底——重配置后 glob 收编同名路径时去重，防双列入库。
# 词组库 TU（XPinyinPhrase.c，XKEYBOARD_IME_PHRASE_ON=
# XKEYBOARD_IME_ON&&XFILE_ON 门控）同批登记。词库资产
# （Library/VirtualKeyboard/phrases_zh.txt 及解析夹具）不拷贝不入构建树：
# 默认路径 ../Library/VirtualKeyboard 按 XFont 外挂字库同款三段解析——
# CTest WORKING_DIRECTORY=bin（下方 add_test）直跑命中 ../ 前缀；
# 非 bin 工作目录经 ../ 剥前缀重试；安装布局经 exe 目录兜底拼接。
list(APPEND SRC_FILE
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XPinyinEngine.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XPinyinTable.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XPinyinPhrase.c"
    # Qt Virtual Keyboard 对齐类族（XVIRTUALKEYBOARD_ON 门控）：显式登记
    # 不 glob（glob 在配置期固化，构建树不重配时感知不到新增文件——
    # XPinyinEngine.c LNK2019 同款教训）。
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardAbstractInputMethod.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardTrace.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardSelectionListModel.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardInputEngine.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardInputContext.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardSettings.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardDictionary.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardDictionaryManager.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardObserver.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardPlatformInputContext.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardPlainInputMethod.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/VirtualKeyboard/XVirtualKeyboardPinyinInputMethod.c")
# XGuiRemote 远程窗口模块（XGuiRemoteProto/Codec/Loopback/Server/Client）：
# 显式登记同因 glob 配置期固化——既有构建树不重配时 glob 感知不到
# Src/XGui/Remote/ 新增文件（XPinyinEngine.c LNK2019 同款教训）；
# REMOVE_DUPLICATES 兜底——重配置后 glob 收编同名路径时去重防双列入库。
list(APPEND SRC_FILE
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiRemoteProto.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiRemoteCodec.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiRemoteLoopback.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiRemoteAuth.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiRemoteUdpChannel.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiServer.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XGuiClient.c"
    "${CMAKE_CURRENT_LIST_DIR}/Src/XGui/Remote/XRemoteSessionBar.c")
list(REMOVE_DUPLICATES SRC_FILE)
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
# xgui_font_scan_tool 同理：自带 main 的独立离屏字库巡检工具（
# xgui_font_scan_tool 目标，接线见 XGuiGpu_Test 前注释），与主程序
# main.c 双 main 冲突，必须从 Test/*.c glob 列表剔除。
file(GLOB_RECURSE XGUI_FONT_SCAN_TOOL_FILE "Test/XGuiTest/xgui_font_scan_tool.c" )
if(XGUI_FONT_SCAN_TOOL_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_FONT_SCAN_TOOL_FILE})
endif()
# XGuiRemoteAuthTest 同理：自带 main 的访问口令认证矩阵测试（
# XGuiRemoteAuth_Test 目标，见 XGuiRegression_Test 后接线），不得卷入主程序。
file(GLOB_RECURSE XGUI_REMOTE_AUTH_TEST_FILE "Test/XGuiTest/XGuiRemoteAuthTest.c" )
if(XGUI_REMOTE_AUTH_TEST_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_REMOTE_AUTH_TEST_FILE})
endif()
# XGuiRemoteInputViewTest 同理：自带 main 的独立专项探针
# （XGuiRemoteInputView_Test 目标，mcgs 战役输入路 2026-10-04），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/Test/XGuiTest/XGuiRemoteInputViewTest.c")
# XGuiDialogMoveTest 同理：自带 main 的 fbdev 弹层拖动条带归位离屏测试
# （XGuiDialogMove_Test 目标，对话框拖动白块根修 2026-10-05 的回归锁；
# 须 -DXPLATFORM_FBDEV_ON=ON 构建才激活，见文件头注），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/Test/XGuiTest/XGuiDialogMoveTest.c")
# XGuiMirrorFidelityTest 同理：自带 main 的 fbdev 镜像保真回环离屏测试
# （XGuiMirrorFidelity_Test 目标，perf9 路2 白块时序口/换页脏区完整性/
#  热切换/FB_REQUEST 首帧实效的回归锁；须 -DXPLATFORM_FBDEV_ON=ON 构建
#  才激活，见文件头注），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/Test/XGuiTest/XGuiMirrorFidelityTest.c")
# XGuiRemoteAdaptTest 同理：自带 main 的 Xvfb 会话直跑测试（XGuiRemoteAdapt_
# Test 目标，perf9 路4 自适应降档阶梯单元+回环量化；见文件头注），不得
# 卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/Test/XGuiTest/XGuiRemoteAdaptTest.c")
file(GLOB_RECURSE DRIVE_FILE "Drive/*.c")
# 桌面（非交叉）构建排除嵌入式移植层（2026-10-05 回归门修复）：FreeRTOS
# portmacro.h/tasks.c 含 ARM CMSIS 内联汇编（mrs/msr basepri、isb），x86
# cc1 直接汇编报错；STM32 cmsis 头无器件宏同样 #error。历史上门禁 build/
# 目录的 glob 快照未含这两个目录（配置早于其入库），全量重配置即暴露。
# 交叉构建同理排除（perf9 上屏部署修复）：非 FreeRTOS 平台的交叉构建
# （如 armel/A33）glob 到 ARM_CM4F port 同样汇编失败（msr psp/basepri 为
# Cortex-M 专有寄存器）；仅当未来恢复 XINYUE_PLATFORM_FREERTOS 分支时
# 才放开由其自管源集。
if(NOT CMAKE_CROSSCOMPILING OR NOT XINYUE_PLATFORM_FREERTOS)
    list(FILTER DRIVE_FILE EXCLUDE REGEX ".*/FreeRTOS/.*")
    list(FILTER DRIVE_FILE EXCLUDE REGEX ".*/STM32/.*")
endif()
# android_main.c 是 demo APK 的 NativeActivity 壳接线（引用 demo 入口
# xgui_window_demo_main，定义 ANativeActivity_onCreate），归 xguidemo
# 目标所有（该目标已显式登记此文件）；留在框架库里会让动态库
# --no-undefined 链接因找不到 demo 入口失败，且与壳目标重复编译。
if(ANDROID)
    list(REMOVE_ITEM DRIVE_FILE "${CMAKE_CURRENT_LIST_DIR}/Drive/Android/android_main.c")
endif()
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
    # Android 排除：NDK sysroot 带 libvulkan 存根会误启用 X11 窗口 surface
    # 路径（XPlatformGraphics_posix.c 的 VK_USE_PLATFORM_XLIB_KHR 需要 Xlib）。
    # 安卓经 VK_KHR_android_surface 建 surface，属 Drive/Android 平台层，待接。
    if(NOT ANDROID)
        find_library(XINYUE_VULKAN_LIBRARY NAMES vulkan)
    endif()
    if(XINYUE_VULKAN_LIBRARY)
        add_compile_definitions(XINYUE_C_HAS_VULKAN=1)
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_VULKAN_LIBRARY})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_VULKAN_LIBRARY})
    endif()

    # Linux AT-SPI 辅助功能后端使用 dbus-1；缺失开发包时自动回退 Drive
    # 存根，嵌入式配置不会引入 D-Bus 依赖。
    # fbdev 交叉构建（XPLATFORM_FBDEV_ON）排除：pkg-config 会在交叉
    # 环境误抓宿主 x86_64 库传给 ARM 链接器（libdbus-1.so 格式不识别），
    # 且无窗口系统的 fbdev 上 AT-SPI/fontconfig 均无意义。
    # 【memhunt fbdev 通道 2026-10-06】排除收窄到交叉构建（NOT
    # CMAKE_CROSSCOMPILING 放行）：桌面 fbdev 构建仍编译 X11 原生窗口
    # 段（XPlatformNativeWindow_posix.c fcitx5 DBus 输入法前端无条件
    # #include <dbus/dbus.h>），跳过发现会让该段 fatal error: dbus/dbus.h；
    # 嵌入式交叉 fbdev 行为不变（仍排除）。
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND AND (NOT XPLATFORM_FBDEV_ON OR NOT CMAKE_CROSSCOMPILING))
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

# ==================== pcap 链接项探测（UNIX 分支历史口径无脑链接） ====================
# UNIX 可执行目标历史上无条件链接 pcap（物理网卡 lwIP 模式 LWIP_USE_PHYSICAL
# 的依赖，源码默认注释关闭，符号零引用）。桌面通常装有 libpcap；交叉 sysroot
# （如 MCGS 官方 SDK）多无 pcap。探测缺失时告警并跳过该链接项——不动源码
# 逻辑（pcap.h 仅在 LWIP_USE_PHYSICAL 手动开启时才被包含，跳过链接不影响
# 任何现有功能）。FIND_ROOT_PATH_MODE_LIBRARY=ONLY 限定探测只搜 sysroot，
# 不会误抓宿主 x86 的 libpcap。
set(XINYUE_UNIX_EXTRA_LIBS "")
if(UNIX)
    find_library(XINYUE_PCAP_LIBRARY NAMES pcap)
    if(XINYUE_PCAP_LIBRARY)
        list(APPEND XINYUE_UNIX_EXTRA_LIBS pcap)
    else()
        message(STATUS "libpcap 未检出（交叉 sysroot 常见）：UNIX 目标跳过 pcap 链接项")
    endif()
endif()

# ---- librt（仅 Linux）：glibc < 2.34 时代 shm_open/clock_gettime 等驻留
#      librt；老 binutils 的 "DSO missing from command line" 严格语义要求
#      显式列入链接行，否则引用方（如 XDeviceFile_posix_api 的 shm_open）
#      链接失败。桌面/交叉（如 MCGS SDK glibc 2.26）均适用；macOS/BSD
#      无 librt，经系统名判断隔离。 ====
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND XINYUE_UNIX_EXTRA_LIBS rt)
endif()

# ---- m/pthread 链接项：Android（bionic）下 pthread、数学库全部内置于
#      libc，NDK 已不提供 libpthread 存根库，链 -lpthread 直接报
#      "unable to find library"。桌面/服务器 UNIX 仍按原口径链。====
if(ANDROID)
    set(XINYUE_UNIX_THREAD_LIBS "")
else()
    # 不带引号：展开须是 m/pthread 两个列表项（CMake 逐项转 -lm/-lpthread）；
    # 带引号字符串会被当成单项进链接行，"pthread" 落成裸文件名致 ld 找不到。
    set(XINYUE_UNIX_THREAD_LIBS m pthread)
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
    # Vulkan：优先 SDK（find_package）；无 SDK 时回退仓库内 vendored
    # Vulkan-Headers + 由系统 vulkan-1.dll 导出生成的导入库
    # （Tools/make_vulkan_lib.bat 从 System32 与 SysWOW64 的 vulkan-1.dll
    # 的 dumpbin /exports 分别生成 x64/x86 的 vulkan-1.def → lib /def）。两形态都只
    # 影响 XINYUE_C_HAS_VULKAN 的定义与否——未启用时不包含 vulkan.h，
    # Drive/Posix 的 Vulkan 驱动文件整体守卫为空，行为同旧版。
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_compile_definitions(XINYUE_C_HAS_VULKAN=1)
        include_directories(${Vulkan_INCLUDE_DIRS})
        set(XINYUE_WIN32_VULKAN_VAR Vulkan::Vulkan)
        message(STATUS "Vulkan found (SDK): ${Vulkan_INCLUDE_DIRS}")
    else()
        set(XINYUE_VULKAN_VENDORED_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/Tools/windows/vulkan/headers/Vulkan-Headers-1.3.290/include/vulkan/vulkan.h")
        # 导入库按目标架构选择：导入库的机器类型必须与目标一致，
        # 否则链接报 LNK4272 且全部 vk* 符号无法解析。
        # x64 <- System32 的 64 位 vulkan-1.dll；x86 <- SysWOW64 的 32 位
        # （x86 额外带 stdcall 跳板层，见 Tools/windows/vulkan/make_vulkan_lib.bat）。
        # 两份预编译库随仓库分发，同平台设备无需本地重新生成。
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(XINYUE_VULKAN_PREBUILT_SUBDIR x64-msvc)
        else()
            set(XINYUE_VULKAN_PREBUILT_SUBDIR x86-msvc)
        endif()
        set(XINYUE_VULKAN_VENDORED_LIB
            "${CMAKE_CURRENT_SOURCE_DIR}/Tools/windows/vulkan/importlib/${XINYUE_VULKAN_PREBUILT_SUBDIR}/vulkan-1.lib")
        if(EXISTS "${XINYUE_VULKAN_VENDORED_HEADER}" AND
           EXISTS "${XINYUE_VULKAN_VENDORED_LIB}")
            add_compile_definitions(XINYUE_C_HAS_VULKAN=1)
            include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Tools/windows/vulkan/headers/Vulkan-Headers-1.3.290/include")
            set(XINYUE_WIN32_VULKAN_VAR "${XINYUE_VULKAN_VENDORED_LIB}")
            message(STATUS "Vulkan found (vendored headers + loader import lib)")
        else()
            message(STATUS "Vulkan NOT enabled: no SDK and no vendored "
                           "headers/importlib (see Tools/windows/vulkan/make_vulkan_lib.bat)")
        endif()
    endif()
    set(XINYUE_WIN32_LIBS advapi32 user32 gdi32 opengl32 imm32 shell32 ole32 uiautomationcore setupapi winusb cfgmgr32 hid)
    if(XINYUE_WIN32_VULKAN_VAR)
        list(APPEND XINYUE_WIN32_LIBS ${XINYUE_WIN32_VULKAN_VAR})
    endif()
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_WIN32_LIBS})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_WIN32_LIBS})
endif()
# Android 平台依赖（bionic 系统库）：
# - log：平台层诊断输出（__android_log_print）；
# - android：ANativeWindow/NativeActivity（XPlatformWindowAndroid.c、
#   android_main.c）。动态库为 --no-undefined 语义，必须在本体链接
#   阶段解析；静态库 PUBLIC 传给最终载体（libxguidemo.so）。
if(ANDROID)
    target_link_libraries(${EXPORT_NAME}S PUBLIC android log)
    target_link_libraries(${EXPORT_NAME} PRIVATE android log)
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
    target_link_libraries(${EXPORT_NAME}_Static ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
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
    target_link_libraries(${EXPORT_NAME}_Dynamic ${EXPORT_NAME} ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite)
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
    target_link_libraries(FtpE2E_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
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
    "./Test/XGuiTest/XKeyboardTest.c"
    "./Test/XGuiTest/XPinyinT9Test.c"
    "./Test/XGuiTest/XTouchMultiPointTest.c"
    "./Test/XGuiTest/XGuiRemoteTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON

# ==================== XGuiRemote 访问口令认证矩阵测试 ====================
# 独立可执行(自带 main): 回环对 + 127.0.0.1 真 TCP(MITM 抓包)跑 XGuiRemote.md
# §3.8 认证矩阵——匿名基线/无口令快速失败/错口令拒绝/对口令准入/运行期设清
# (存量会话不断)/nonce 逐连接随机/失败上限可配/口令明文不过网。
if(XGUI_ON)
add_executable(XGuiRemoteAuth_Test
    "./Test/XGuiTest/XGuiRemoteAuthTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteAuth_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteAuth_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON（认证矩阵测试）

# ==================== XGuiRemote 自适应降档阶梯测试(perf9 路4) ====================
# 独立可执行(自带 main): (a) XGuiRemoteAdapt.h 纯状态机单元(阶梯映射/迟滞/
# 换档重置); (b) 回环量化——latency 档 60fps 持续伤害流下 XGUI_REMOTE_ADAPT_
# BUDGET_MS=1 强制超预算, 断言交付批率沉底 ~15fps; --adapt-baseline 反证组
# (关阶梯≈60fps)。需显示环境, 由 Xvfb 会话直跑口径执行(同认证矩阵测试)。
if(XGUI_ON)
add_executable(XGuiRemoteAdapt_Test
    "./Test/XGuiTest/XGuiRemoteAdaptTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteAdapt_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteAdapt_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON（自适应降档阶梯测试）

# ==================== fbdev 弹层拖动条带归位测试 ====================
# 独立可执行(自带 main)：伪 fbdev 显示驱动（RAM 帧缓冲，双缓冲+行距
# 垫高）离屏跑「弹层出现→拖动→拖回→松手」全帧合成模型断言——对话框
# 拖动白块根修（2026-10-05）的回归锁。须 fbdev 构建才激活（驱动契约
# 与 XWindowDecoration fbdev 分支均受 XPLATFORM_FBDEV_ON 门控）；桌面
# 默认构建编出 SKIP 壳（恒 0 退出，不扰门禁）。BUFFER_COUNT=2 镜像真
# 机 DIRECT+2 双缓冲差带账本路径。
if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiDialogMove_Test
    "./Test/XGuiTest/XGuiDialogMoveTest.c"
)
if(UNIX)
    target_link_libraries(XGuiDialogMove_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiDialogMove_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（弹层拖动条带归位测试）

# ==================== fbdev 键盘紧凑悬浮拖移测试 ====================
# 独立可执行(自带 main)：伪 fbdev 显示驱动离屏跑「停靠弹出→悬浮切换→
# 工具栏空白拖移→拖回→松手」全帧合成模型断言——键盘拖动残影+逐 MOVE
# 重绘风暴（昆仑通态 A33 真机 2026-10-06，FPS 5.9）的回归锁。构建组合
# 同 XGuiDialogMove_Test（FBDEV+BUFFER_COUNT=2+RGB16 真机 DIRECT+2 trio）。
if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiKeyboardDrag_Test
    "./Test/XGuiTest/XGuiKeyboardDragTest.c"
)
if(UNIX)
    target_link_libraries(XGuiKeyboardDrag_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiKeyboardDrag_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（键盘紧凑悬浮拖移测试）

if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiMirrorFidelity_Test
    "./Test/XGuiTest/XGuiMirrorFidelityTest.c"
)
if(UNIX)
    target_link_libraries(XGuiMirrorFidelity_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiMirrorFidelity_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（镜像保真回环测试）

if(XGUI_ON)
add_executable(XLineControl_Acceptance_Test
    "./xgui_linecontrol_acceptance_test.c"
    "./Test/XGuiTest/XLineControlAcceptance.c"
)
if(UNIX)
    target_link_libraries(XLineControl_Acceptance_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
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

# ==================== XGuiRemote 输入链路 + 视图适配缩放专项探针 ====================
# mcgs 战役输入路(2026-10-04)独立可执行: 回环零网络拓扑下自证客户端 FIT
# 信箱缩放、输入逆映射、键盘/滚轮链与服务端注入多顶层命中。不进 CTest
# (需图形显示, 由 Xvfb 会话直跑口径执行)。
if(XGUI_ON)
add_executable(XGuiRemoteInputView_Test
    "./Test/XGuiTest/XGuiRemoteInputViewTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteInputView_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteInputView_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON

# ==================== Linux X11 XDND 跨客户端端到端测试 ====================
# 用独立 Xlib 客户端模拟拖放源，验证 XDND 状态应答、Selection 数据传输和
# XDropEvent 负载。仅 Linux 且已找到 X11 时构建，嵌入式裁剪不引入平台依赖。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    add_executable(XGuiXdnd_Test
        "./xgui_xdnd_test.c"
    )
    target_link_libraries(XGuiXdnd_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${X11_LIBRARIES} ${EXPORT_NAME}S)
endif()

# ==================== XGui 统一可视化控件测试程序 ====================
# 桌面原生窗口控件验收入口：XWindow + XGuiApplication + XBackingStore 走完整的
# 「平台原生窗口（X11/Win32）-> 事件注入 -> 重绘 -> 上屏」链路。
# 运行：无参数常驻，带参数可传入自动退出秒数（供无窗口管理器环境截图）。
# 安卓排除：demo 由 xguidemo 共享库（NativeActivity 壳）承载，本可执行
# 是桌面窗口系统专用；安卓下 main 已改名 xgui_window_demo_main 且诊断
# 依赖壳里的 XGuiDemo_debugLog，独立可执行无入口可链。
if(XGUI_ON AND NOT ANDROID)
add_executable(XGuiWindowDemo_Test
    "./Test/XGuiDemo/xgui_window_demo.c"
    "./Test/XGuiDemo/xgui_demo_splitter.c"
    "./Test/XGuiDemo/xgui_demo_page_views.c"
    "./Test/XGuiDemo/xgui_demo_page_dialogs.c"
    "./Test/XGuiDemo/xgui_demo_page_advanced.c"
    "./Test/XGuiDemo/xgui_demo_page_keyboard.c"
    "./Test/XGuiDemo/xgui_demo_page_effects.c"
    "./Test/XGuiDemo/xgui_demo_page_remote_server.c"
    "./Test/XGuiDemo/xgui_demo_page_remote_client.c"
    "./Test/XGuiDemo/xgui_demo_page_network.c"
    "./Test/XGuiDemo/xgui_demo_page_overlay_settings.c"
    "./Test/XGuiDemo/xgui_demo_page_qrcode.c"
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
    target_link_libraries(XGuiWindowDemo_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiWindowDemo_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XGuiRemote 远程窗口双进程演示 ====================
# 单文件双角色（xgui_demo_remote.c 自带 main）：--server 起最小控件树 +
# XGuiServer 共存镜像（默认端口 46000），--client 另进程连接显示；
# --profile performance|resource 运行期切档，--tls 走 XSslSocket 加密档。
# 安卓排除同 XGuiWindowDemo_Test：demo 为桌面窗口系统专用独立可执行。
if(XGUI_ON AND NOT ANDROID)
add_executable(XGuiRemoteDemo_Test
    "./Test/XGuiDemo/xgui_demo_remote.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteDemo_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteDemo_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON AND NOT ANDROID（XGuiRemoteDemo_Test）
endif() # XGUI_ON AND NOT ANDROID

# ==================== XGuiWindowDemo 安卓 APK 壳（共享库） ====================
# NativeActivity 壳：android_main.c 生命周期接线 + 平台层（窗口/输入）。
# demo main 重命名为 xgui_window_demo_main（__ANDROID__ 下）并入本目标，
# 导出 ANativeActivity_onCreate。打包脚本（Tools/android/build_apk.sh）
# 以 libxguidemo.so 为载体组 APK。
if(XGUI_ON AND ANDROID)
    add_library(xguidemo SHARED
        "./Drive/Android/android_main.c"
        "./Test/XGuiDemo/xgui_window_demo.c"
        "./Test/XGuiDemo/xgui_demo_page_views.c"
        "./Test/XGuiDemo/xgui_demo_page_dialogs.c"
        "./Test/XGuiDemo/xgui_demo_page_advanced.c"
        "./Test/XGuiDemo/xgui_demo_page_keyboard.c"
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
    target_link_libraries(xguidemo ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
    target_link_libraries(xguidemo android log)
endif()

# ==================== XGui 字库离屏巡检工具 ====================
# 全字库离屏扫描（ASCII + GB2312 汉字，16px 轮廓 XFont -> 32x32 内存
# XImage，走与窗口后备缓冲一致的 XPainterDevice_Image SW-AA 字形管线）：
# 三道判据（零墨/墨量塌陷/横画叠带）筛坏字，产出 out/scan_bad_1.txt 与
# out/scan_bad/*.png 样图；退出码 = min(坏字数, 250)。仅新增文件与目标，
# 不触碰现有源码逻辑。仅桌面可执行（无窗口依赖，任何平台均可构建）。
add_executable(xgui_font_scan_tool
    "./Test/XGuiTest/xgui_font_scan_tool.c"
)
if(UNIX)
    target_link_libraries(xgui_font_scan_tool ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(xgui_font_scan_tool ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XGui GPU 阶段 1 冒烟测试 ====================
add_executable(XGuiGpu_Test
    "./xgui_gpu_test.c"
)
if(UNIX)
    target_link_libraries(XGuiGpu_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiGpu_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
add_test(NAME XGuiGpu COMMAND XGuiGpu_Test)
set_tests_properties(XGuiGpu PROPERTIES
    ENVIRONMENT "XGUI_RENDER_BACKEND=gpu;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")

#安装
include(CMake_Install.cmake)
