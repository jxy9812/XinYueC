#请求CAMKE的最小构建版本
cmake_minimum_required(VERSION 3.5)
#设置项目名称
project(Xin_Yue_C VERSION 0.1 LANGUAGES C)

# ==================== CMP0079 NEW（跨目录链接落地，单条精确开启） ====================
# 平台/第三方系统库的 target_link_libraries 落地统一在本文件（探测、
# Library/* 注册、链接三段同居一处），而 XinYueCS/XinYueC 创建于 Src/XCore
# 子目录——旧策略禁止跨目录改目标（min 3.5 下 CMP0079 默认 OLD，实测报
# "not built in this directory"）。单条开启 NEW：仅放宽跨目录
# target_link_libraries，其余策略语义不变（存量调用全部同目录，不受影响；
# 装有 CMake <3.13 的环境 POLICY(CMP0079) 探测为假，自动跳过——彼时按旧
# 布局回退即可，不影响同目录调用）。
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

# ---- 裁剪开关转发：命令行 -DX?_ON=0/1 转为全局编译定义 ----
# （否则 -D 仅入缓存为 UNINITIALIZED，不产生编译定义，"裁剪构建"实际全量编译。）
#
# 【枚举型开关必须显式映射 2026-10-26 根修】XGUI_BACKINGSTORE_RENDER_MODE
# 不是布尔量，而是三值枚举（PARTIAL=0 / DIRECT=1 / FULL=2，见 XGuiConfig.h）。
# 旧转发只认 ON/TRUE/OFF/FALSE，"DIRECT" 两侧都不匹配 ⇒ 原样透传为编译定义
# XGUI_BACKINGSTORE_RENDER_MODE=DIRECT。该宏在 #if 比较里展开成**未定义标识符**，
# 预处理器按 0 处理 ⇒ 与 PARTIAL(0) 相等 ⇒ 嵌入构建实际编出的是 PARTIAL
# （tile 攒批）而非 DIRECT！真机症状：后备缓冲按 min(w,160)×min(h,80) 分配，
# present 把 160×80 的图往 1024×600 面板直写 ⇒ 逐行错位 ⇒ 面板上出现
# 阶梯状黑块/黑条纹（昆仑通态 A33 真机实证：黑带 |x−2y|≤320 斜带、
# 非黑区域与正确帧逐位一致）。此处补齐枚举名映射，命令行 -D…=DIRECT 由此
# 真正落到数值 1。
option(XGUI_ON "XGui 模块总开关（默认开；-DXGUI_ON=0 关闭）" ON)
foreach(_xsw XGUI_ON XGPU_ON XIMAGEIOPLUGIN_ON XSVGICON_ON XPIXMAPCACHE_ON XMOVIE_ON
             XVIRTUALKEYBOARD_DESKTOP_ON
             XPLATFORM_FBDEV_ON XPLATFORM_DRM_ON XPLATFORM_FBINPUT_ON XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
             XGUI_BACKINGSTORE_BUFFER_COUNT)
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

# 渲染模式枚举名 → 数值（与 XGuiConfig.h 的 PARTIAL=0/DIRECT=1/FULL=2 对齐）。
# 单独处理：它是三值枚举，不能走上面的布尔映射（DIRECT 既非 ON 也非 OFF）。
if(DEFINED XGUI_BACKINGSTORE_RENDER_MODE)
    set(_xrm "${XGUI_BACKINGSTORE_RENDER_MODE}")
    if(_xrm STREQUAL "PARTIAL")
        set(_xrm 0)
    elseif(_xrm STREQUAL "DIRECT")
        set(_xrm 1)
    elseif(_xrm STREQUAL "FULL")
        set(_xrm 2)
    else()
        # 数值直给（0/1/2）或未识别值：数值原样透传；未识别的**标识符**
        # 会退化成预处理器里的未定义符号（按 0 = PARTIAL 生效），此处显式
        # 报错拒收——宁可构建失败，也不要静默编出与意图相反的模式。
        if(NOT _xrm MATCHES "^[0-9]+$")
            message(FATAL_ERROR
                "XGUI_BACKINGSTORE_RENDER_MODE 取值非法：'${_xrm}'。"
                "合法值：PARTIAL(0) / DIRECT(1) / FULL(2)（枚举名或数值）。")
        endif()
    endif()
    add_compile_definitions(XGUI_BACKINGSTORE_RENDER_MODE=${_xrm})
    unset(_xrm)
endif()

# ==================== fbdev 显示驱动模板（嵌入式 RGB565 直写铺路） ====================
# 新增源文件 Drive/Posix/Graphics/XPlatformFramebuffer_posix.c（及其 .h）经
# Drive 模块的 file(GLOB_RECURSE DRIVE_FILE) 自动入列——新增/删除源文件后
# 须触发重新配置（touch Drive/CMakeLists.txt 即可），glob 才会重扫。
# 功能受 XPLATFORM_FBDEV_ON 控制（默认 0，XPlatformDisplayDriver.h 内
# #ifndef 兜底；嵌入式目标以 -DXPLATFORM_FBDEV_ON=ON 开启）。默认关闭时该
# 文件经预处理守卫整段裁剪，桌面 X11 路径零变化；无需额外链接库（fbdev 经
# sys/ioctl.h + mmap 即可）。

# ==================== DRM/KMS 显示驱动（无 fbdev 现代嵌入式板） ====================
# Drive/Posix/Graphics/XPlatformFramebuffer_drm.c（及其 .h）同经 glob 自动
# 入列。功能受 XPLATFORM_DRM_ON 控制（默认 0；嵌入式目标以
# -DXPLATFORM_DRM_ON=ON 开启，注册表符号随 fbdev 编译单元，FBDEV 开关须同
# 开）。板上引导先试 DRM（/dev/dri/card0 dumb buffer 直写）失败再试 fbdev，
# 见 XGuiWindowDemo。零外链（内核 UAPI 就地声明，不经 libdrm）。

# ---- XGuiRemote 编解码 NEON 加速（perf9 路4，2026-10-05） ----
# 仅对 XGuiRemoteCodec.c 追加 -mfpu=neon（ARM 交叉目标默认开；-DXGUI_REMOTE_NEON=0
# 显式关回）。ABI 恒 softfp 不变（工具链默认 -mfloat-abi=softfp，NEON 只改指令
# 集不改传参），现网 A33（Cortex-A7, /proc/cpuinfo 含 neon）可执行。源内经
# XGuiRemoteCodecNeon.h 以 __ARM_NEON__ 感知，桌面 x86/无 NEON 产物整头裁空
# 恒标量——输出逐字节等价由 verify/perf9 rle_bench NEON 版 0 mismatch 红线
# 自证（out/perf9/neon-bench.txt），未初始化安全由行内夹取+尾块标量回退保证。
# [2026-11-01 模块化拆分] set_source_files_properties 是目录作用域的：该源
# 文件已随 XGui 模块归 XinYueGui 目标编译，属性必须落在 Src/XGui/CMakeLists.txt
# （目标定义目录）才生效——留在根会静默失效（搬库时的坑，已随迁）。
option(XGUI_REMOTE_NEON
    "XGuiRemote 编解码热循环 NEON 加速（仅 ARM 交叉目标有意义）" ON)

set(EXPORT_NAME XinYueC)
# 仓库根目录锚（2026-11-01 模块化拆分）：仅根函数 xinyue_force_include_config
# 使用——函数体内的 CMAKE_CURRENT_LIST_DIR 取**调用者**目录（Src/XCore、
# Drive、Test 模块各不相同），定位根下 CXinYueConfig.h 必须用根锚定变量；
# 各模块自身文件一律用模块相对路径，不依赖本变量。
set(XINYUE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

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

# ==================== 禁止链接期相同代码折叠（ICF） ====================
# 框架的**信号身份 = 信号函数地址**（XSignal(signal) = (size_t)(signal)，
# XObject.h:416）：连接与发射两侧都以该地址为键。MSVC 的 /OPT:ICF 会把
# **结构相同**的函数折叠成同一地址——而各类的信号函数恰恰是模板化的同构
# 代码（"判空→emit(self, (size_t)自己, args)→返回自身地址"），
# 于是 XScreen_primaryOrientationChanged_signal 与
# XScreen_orientationChanged_signal 折叠成同一地址 → 两个信号共用一个
# 身份 → 连接 A 也会收到 B 的发射（探针实证：priOr 与 or 同步翻倍，
# rr 与 logDpi 同址），表现为「信号重复发射」类断言全红、同一处理器被
# 跑两遍（弹层重定位跑两次 → 落位时好时坏；重复重绘 → 切换时闪烁；
# 重复执行吃穿内存 → 池耗尽 → 崩栈）。
# 代价仅为少量代码体积（放弃折叠），换来信号身份的前提成立。
# ⚠ 勿当冗余删掉：症状是「偶发、看起来像随机」的行为缺陷，回归极难自证。
# ⚠ 同类陷阱：GCC/LTO 的 -fipa-icf 同样会折叠「取自身地址」的函数。若将来
# 开启 LTO，需同步加 -fno-ipa-icf（GCC）或等价手段。
if(MSVC)
    add_link_options(/OPT:NOICF)
endif()

# ==================== 源文件字符集 = UTF-8（MSVC /source-charset） ====================
# [2026-11-07 根修] 项目口径"源文件 UTF-8"（AGENTS.md），但 MSVC 默认按
# **活动码页**（简中系统 = GBK/936）解码无 BOM 的源文件——UTF-8 中文字面量
# 的字节被按 GBK 重新分组 ⇒ 运行期字符串就是 mojibake（实测：HsfTitleBar.c
# 的自动运行 chip 文案「自动·已停止」显示为「èªå¨Â·å²åæ­¢」——UTF-8
# 字节被 GBK 解码后再按 UTF-8 输出的双重乱码）。/utf-8 = /source-charset +
# /execution-charset 同设 UTF-8：源码按 UTF-8 读、窄字符串按 UTF-8 存，
# 与 GCC/Clang（默认 UTF-8）口径对齐，全部平台一致。
# 注意只加在 MSVC 分支（GCC/Clang 不认识 /utf-8，且默认即 UTF-8 无需处理）。
if(MSVC)
    add_compile_options(/utf-8)
endif()

#设置可执行文件的输出目录（可用 -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=... 覆盖，
#供 Release 门等旁路构建输出到独立目录，避免覆盖 bin/ 的既有产物）
#
# [2026-11-01] fbdev 变体构建输出隔离：fbdev 是**同机 native 变体配置**
#（XPLATFORM_FBDEV_ON=ON），与桌面默认配置共用 bin/ 时会互相覆盖二进制：
# 编完 fbdev 再跑桌面套件，跑到的是 fbdev 产物 ⇒ 冒出一整族与代码无关的
# 假失败（本轮实测 8 条：「resize 保留旧内容」「setBuffers 使用调用方提供
# 的帧缓冲」「平台后端 resize 后绘制设备有效」等 backing store 族断言全红，
# 而纯净树是绿的）。交叉构建防呆门只覆盖 CMAKE_CROSSCOMPILING（armel/
# rk3568 等），覆盖不到 fbdev 这个 native 变体 ⇒ 此处补同款隔离：fbdev
# 默认落 bin-fbdev/（显式 -DCMAKE_RUNTIME_OUTPUT_DIRECTORY 仍优先；门脚本
# 与部署流程用的都是桌面构建的 bin/，不受影响）。
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    if(XPLATFORM_FBDEV_ON)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin-fbdev)
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)
    endif()
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
# （[已移除 2026-10-07] 原 XFONT_BUILTIN_OUTLINE_ON=1 内嵌轮廓字库
# 定义：自研 XFO1 字库数据已删，轮廓字统一走 FT 外挂文件 + 位图回退。）
if(ANDROID)
    add_compile_definitions(XPLATFORM_FBDEV_ON=1)
    # 外挂 FT 轮廓字目录（2026-10-24）：安卓无文件系统可写工作目录，
    # 字体由 android_main.c 启动时从 APK assets 释放到应用私有目录
    # fonts/，此处注入该绝对路径供 FT 链查找。
    # 不注入时框架回退默认的 "../Library/XFont"（构建机路径，设备上不存
    # 在）→ 装载失败回退位图字库 XFont8x16（仅 ASCII）→ 中文整片缺失
    # （真机截图实证）。
    # 路径取 /data/data/<pkg>/files/fonts：Android 上 /data/data 是
    # /data/user/0 的稳定入口（跨多用户设备同样成立），且与
    # android_main.c 的释放目标同源（同一宏 XFONT_EXTERNAL_FT_FONT_DIR，
    # 两处分离过一次导致「释放到 A、FT 找 B」→ 探测落空）。
    add_compile_definitions(
        XFONT_EXTERNAL_FT_FONT_DIR="/data/data/com.xinyuec.xguidemo/files/fonts")
endif()
# 强制 DPI（XGUI_FORCE_DPI，XGuiConfig.h 有 #ifndef 兜底 0）：命令行
# -DXGUI_FORCE_DPI=2.0 调试缩放链/小屏放大画面。CMake -D 只建变量不自动
# 变编译定义，此处显式转发（全平台，非安卓限定）。
if(DEFINED XGUI_FORCE_DPI AND NOT XGUI_FORCE_DPI STREQUAL "" AND NOT XGUI_FORCE_DPI STREQUAL "0")
    add_compile_definitions(XGUI_FORCE_DPI=${XGUI_FORCE_DPI})
endif()
# 映射表文件路径（干净格式，无注释，固定4字节/行，支持文件二分）
add_compile_definitions(XCHAR_COMPACT_PATH="${CMAKE_CURRENT_LIST_DIR}/Library/XCHAR_COMPACT.BIN")
# XUpgrade 产品版本基线（XUpgradeIdentity.h 契约：桌面目标由构建注入真实
# 版本，未注入回退 "0.0.0"=版本未知、引擎只允许手动检查）。量产恢复为按
# 项目版本三段式派生：
#   add_compile_definitions(XUPGRADE_PRODUCT_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
# 当前为演示口径：出厂版本 1.2.0（配合服务器真实 1.3.0 二进制包做自升验收；
# 1.3.0 负载 exe 由同树临时抬版本构建）。单一来源变量：编译定义与 Win32
# VERSIONINFO 资源（demo 属性页文件/产品版本）同源注入。
set(XUPGRADE_PRODUCT_VERSION "1.2.0")
string(REPLACE "." "," XUPGRADE_PRODUCT_VERSION_RC "${XUPGRADE_PRODUCT_VERSION}")
add_compile_definitions(XUPGRADE_PRODUCT_VERSION="${XUPGRADE_PRODUCT_VERSION}")
include(CMake_Config.cmake)

# ==================== 模块化拆分（2026-11-01） ====================
# 源文件搜集与头目录自采已下放各模块（模块自己的头文件自己包含）：
#   Src/XCore/CMakeLists.txt  核心框架模块（XinYueCore；最小系统裁剪单元，
#                        只含自己的头/源），抬升 INCLUDE_XCORE_LIST_LIFT
#   Drive/CMakeLists.txt  平台组合模块（主库 XinYueCS/XinYueC = Drive 源树
#                        + 挂 XinYueCore/XGuiLayer；只含自己的头/源），抬升
#                        INCLUDE_DRIVE_LIST
#   Src/XGui/CMakeLists.txt  GUI 框架模块（XinYueGui，2026-11-01 回迁 Src/
#                        与 XCore 平级；头目录走链接图传递，不抬升）
#   Src/Industry/CMakeLists.txt  工业设备层模块（XinYueIndustry，2026-11-01
#                        迁入 Src/ 与 XCore/XGui 平级；自带测试可执行）
#   HighSpeedFilter/CMakeLists.txt（模板/演示项目，自带 main，留仓库根）
#   Test/CMakeLists.txt  测试/工具/demo 可执行模块（TEST_FILE 及全部 exe）
# 根保留：全局配置/编译定义、平台依赖探测与**链接落地**（探测段只探测+
# 注入编译定义+注入目录；链接在接线段对 Drive 模块的主库目标兑现，跨目录
# 改目标经 CMP0079 NEW）、第三方子目录、模块接线与安装收尾。
# 目录级 include_directories 在接线段按模块抬升的清单落地（先于 Library/
# 等子目录注册，保持原「根目录级快照」语义——Library/zlib 的 TU 引用
# XMemory.h/XCrc.h，依赖该目录级路径）。

# ==================== 强制包含配置头（各模块共用） ====================
# [原 449-464 迁此 2026-11-01] 函数只在其定义作用域及子目录可见——XCore、
# Drive、Test 都是根的子目录，放根全部可见；定义在任一模块内则其余模块
# 不可见。
# [修路径] 函数体内的 CMAKE_CURRENT_LIST_DIR 取**调用者**目录，拆分后从
# 模块调用会解析成 "…/Test/Src/CXinYueConfig.h" 错路径，改用根锚定
# 变量 XINYUE_SOURCE_DIR。
# ESP8266 移植给大量 .h/.c 补了 #if MODULE_ON 文件顶部守卫，开关默认值在
# CXinYueConfig.h（及其拉入的模块 config）。桌面原先没有强制包含，TU 首行
# 的 #if 因宏未定义按 0 求值，整文件被裁空（实测 mbedtls 引用
# XCryptographic 类型失败）。与 ESP8266 的 -include ESP8266Minimal.h 对齐，
# 对核心库目标强制包含 CXinYueConfig.h；第三方纯 C 库不受影响。
function(xinyue_force_include_config target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            "/FI${XINYUE_SOURCE_DIR}/Src/CXinYueConfig.h")
    else()
        target_compile_options(${target} PRIVATE
            "-include" "${XINYUE_SOURCE_DIR}/Src/CXinYueConfig.h")
    endif()
endfunction()

# ==================== 静态归档环依赖收口（模块化拆分遗留）====================
# [2026-11-01] 框架链（XinYueCore / XinYueIndustry / XinYueGui）与第三方
# 归档（Library/*）之间存在**双向**符号依赖，静态链接下单遍扫描无法解析：
#   ① zlib ↔ Core：vendored zlib 的 zutil.h 用框架 CRC（XCrc32_update），
#      而 Core 的 XZipWriter/XZipReader 又调 zlib；
#   ② Gui → Core：Gui 归档成员引用 XAction_create 等 Core 符号；
#   ③ Core → Gui：Core 侧工具亦引用 Gui 符号。
# 拆库前所有核心对象同在 ${EXPORT_NAME}S 一个归档内，靠「双列主库」惯例
# （链接行首尾各列一次）解决；拆库后该惯例失效——CMake 把传递依赖排在直接
# 项之后，框架链不再位于行尾，后扫到的归档无法回溯满足先前归档成员的引用
# （实测 undefined reference: XAction_create / XCrc32_update）。
# 收口：把相关归档按**文件路径**放进 LINK_GROUP:RESCAN（链接器
# --start-group/--end-group，组内反复重扫直到无新符号，与顺序无关）。
# 用 $<TARGET_FILE:> 而非目标名：目标名会带上接口依赖，与既有目标级环依赖
# （XinYueCore↔XinYueCS、XinYueIndustry↔XinYueCS）叠加成 "Cyclic
# dependencies" 生成期错误；文件路径只建立文件级依赖，不成环。
# 线程/数学库（-lm -lpthread）置于组后：--as-needed 下共享库只在「处理到
# 它时确有未决符号」才被标记为 needed，而数学符号的引用来自组内 Core 归档
# 成员（XChar.c/XStringUtils.c），故 -lm 必须排在组之后。
function(xinyue_link_rescan_group target)
    # 平台闸：LINK_GROUP:RESCAN 依赖平台模块声明 CMAKE_<LANG>_LINK_GROUP_USING_
    # FEATURE_RESCAN（仅 Platform/GNU.cmake、Linux.cmake 等 GNU 系声明）。
    # Windows-MSVC.cmake 不声明——MSVC link.exe 本就多遍重扫归档，单遍扫描
    # 问题不存在；MSVC 工具链下套组会在 generate 期报
    # "Feature 'RESCAN' ... is not supported for the 'C' link language"
    # ⇒ configure 直接失败（2026-11-01 远端合并回归根因，复现于 x64-Debug）。
    if(MSVC)
        return()
    endif()
    set(_archives "")
    foreach(_arc IN ITEMS XinYueCore XinYueIndustry XinYueGui
                          zlib mbedtls lwip pcre2 sqlite freetype fatfs xxhash)
        if(TARGET ${_arc})
            list(APPEND _archives "$<TARGET_FILE:${_arc}>")
        endif()
    endforeach()
    if(_archives)
        string(REPLACE ";" "," _group "${_archives}")
        target_link_libraries(${target}
            "$<LINK_GROUP:RESCAN,${_group}>"
            ${XINYUE_UNIX_THREAD_LIBS})
    endif()
endfunction()

# ==================== X11（Linux 原生窗口后端，探测+定义注入） =====================
# XPlatformNativeWindow_posix.c 需要 Xlib 头与运行库。检测到 X11 时注入
# XINYUE_C_HAS_X11=1 编译宏并链接 X11 库（链接在下方"平台/第三方链接落地"
# 段兑现：静态库 PUBLIC 传播给可执行程序，动态库 PRIVATE 仅自身链接阶段
# 解析）；未检出时
# Posix 原生窗口文件整段被守卫裁剪，由 Drive/XPlatformNativeWindow_unsupported.c
# 兜底，XWindow 回落虚拟 WId 行为，嵌入式构建不受影响。
# Android 排除（整段）：安卓交叉构建（NDK）在 Linux 宿主上会把宿主
# X11/OpenGL/pkg-config 依赖（dbus/fontconfig 等）误探进来——头文件
# 侥幸能编、链接的是宿主 x86_64 库必炸；Windows 宿主因探不到而隐式
# 跳过，本条件把该事实显式化。安卓图形/输入/字体一律走 Drive/Android
# 平台层（字体=APK assets 外挂目录），不吃本段任何宏。
# [接线契约] XGui 模块（XGpuRenderBackend.c 等消费 XINYUE_C_HAS_*）在
# add_subdirectory(Src/XGui) 时快照本段定义——探测必须先于全部模块接线。
if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(X11)
    if(X11_FOUND)
        add_compile_definitions(XINYUE_C_HAS_X11=1)
        include_directories(${X11_INCLUDE_DIRS})
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
        endif()
        # DPMS（显示器电源管理，Drive/Posix/Graphics/XPlatformPower_posix.c）：
        # DPMSForceLevel 位于 libXext（**不是** libXss），本机通常只有运行库
        # libXext.so.6 而无开发链接，按 RandR/XI2 同款两段式兜底；协议头
        # dpms.h 缺失，最小原型内联在平台文件中（纪律同 RandR/XI2 段）。
        # 未检出则不注入 XINYUE_C_HAS_DPMS，DPMS 段整段不编译（链接不引
        # 用未解析符号），熄屏能力自动回落为「本平台不支持」。
        find_library(XINYUE_XEXT_LIBRARY NAMES Xext)
        if(NOT XINYUE_XEXT_LIBRARY)
            find_library(XINYUE_XEXT_LIBRARY NAMES libXext.so.6 Xext.so.6)
        endif()
        if(XINYUE_XEXT_LIBRARY)
            add_compile_definitions(XINYUE_C_HAS_DPMS=1)
        endif()
    endif()

    # XPlatformGraphics 的 GLX 后端只在同时检测到 X11 与 OpenGL 时启用。
    # Vulkan 单独按库存在性启用，保持图形模块可独立裁剪。
    find_package(OpenGL QUIET)
    if(X11_FOUND AND OPENGL_FOUND)
        add_compile_definitions(XINYUE_C_HAS_OPENGL=1)
    endif()
    # Android 排除：NDK sysroot 带 libvulkan 存根会误启用 X11 窗口 surface
    # 路径（XPlatformGraphics_posix.c 的 VK_USE_PLATFORM_XLIB_KHR 需要 Xlib）。
    # 安卓经 VK_KHR_android_surface 建 surface，属 Drive/Android 平台层，待接。
    if(NOT ANDROID)
        find_library(XINYUE_VULKAN_LIBRARY NAMES vulkan)
    endif()
    if(XINYUE_VULKAN_LIBRARY)
        add_compile_definitions(XINYUE_C_HAS_VULKAN=1)
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
        endif()
        pkg_check_modules(XINYUE_FONTCONFIG QUIET fontconfig)
        if(XINYUE_FONTCONFIG_FOUND)
            add_compile_definitions(XINYUE_C_HAS_FONTCONFIG=1)
            include_directories(${XINYUE_FONTCONFIG_INCLUDE_DIRS})
            link_directories(${XINYUE_FONTCONFIG_LIBRARY_DIRS})
        endif()
    endif()
endif()

# POSIX USB Host 后端使用 libusb-1.0。该探测独立于 Linux 桌面图形依赖，
# 因而同样覆盖 macOS、BSD 和裁剪后的嵌入式 Linux 系统。
# Android 排除：同上段理由——宿主 libusb（x86_64）不可链入安卓 .so；
# 安卓 USB 走平台层实现。Windows 宿主探不到本就隐式跳过。
if(UNIX AND NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XINYUE_LIBUSB QUIET libusb-1.0)
    endif()
    if(XINYUE_LIBUSB_FOUND)
        add_compile_definitions(XINYUE_C_HAS_LIBUSB=1)
        include_directories(${XINYUE_LIBUSB_INCLUDE_DIRS})
        link_directories(${XINYUE_LIBUSB_LIBRARY_DIRS})
    else()
        find_path(XINYUE_LIBUSB_INCLUDE_DIR libusb-1.0/libusb.h)
        find_library(XINYUE_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
        if(XINYUE_LIBUSB_INCLUDE_DIR AND XINYUE_LIBUSB_LIBRARY)
            add_compile_definitions(XINYUE_C_HAS_LIBUSB=1)
            include_directories(${XINYUE_LIBUSB_INCLUDE_DIR})
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
# 【模块契约】XINYUE_UNIX_EXTRA_LIBS / XINYUE_UNIX_THREAD_LIBS 必须在根定义
# （Industry/HighSpeedFilter/Test 模块的可执行目标链接行都引用）。
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

# ==================== Windows 平台依赖（探测 + 库清单组装） ====================
# - Advapi32：XSystem 后端调整关机权限；
# - User32：窗口类注册/句柄操作/消息泵（XPlatformNativeWindow_win32.c）；
# - Gdi32：DIB 后备缓冲 + BitBlt/SetDIBitsToDevice 上屏
#   （XPlatformBackingStore_win32.c / XPlatformNativeWindow_win32.c）。
# 链接在下方"平台/第三方链接落地"段兑现：静态库将依赖公开给最终可执行
# 文件，动态库在自身链接阶段解析。
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
    # wlanapi：Native WiFi（XDeviceWiFi_win32.c 的无线枚举/扫描/连接通路）。
    set(XINYUE_WIN32_LIBS advapi32 user32 gdi32 opengl32 imm32 shell32 ole32 uiautomationcore setupapi winusb cfgmgr32 hid wlanapi)
    if(XINYUE_WIN32_VULKAN_VAR)
        list(APPEND XINYUE_WIN32_LIBS ${XINYUE_WIN32_VULKAN_VAR})
    endif()
endif()

# ==================== 平台模板（platforms/）====================
# ⚠ 2026-10-22 起，平台模板**不作为本根工程的子目录**（2026-10-24 迁入
#   platforms/ 后结论不变）。
#   实测把 ESP8266 模板 add_subdirectory 进来后，VS 的启动目标列表里会出现
#   两个 esp8266_nonos_fw.slnx（VS 的 CMake 扩展对"同一工程被独立打开 + 又被
#   父工程纳入"这种双份 .sln 识别不了），两个都点不动、无法切换执行。
#   结论：platforms/esp8266/ 保持【独立子工程】，用下面的方式单独打开。
#
#   独立打开（推荐，VS 一键可用）：
#       cmake -S platforms/esp8266 -B build_esp
#       cmake --build build_esp                  # 构建固件
#       cmake --build build_esp --target flash   # 烧录
#       cmake --build build_esp --target monitor # 串口日志
#   VS 里：File > Open > Folder... 选 platforms/esp8266/，或直接 Open
#   build_esp\ 里的 .sln。
#
#   底层构建其实不依赖 CMake：直接
#       bash platforms/esp8266/nonos/tools/build_fw_final.sh
#   即可产出 platforms/esp8266/nonos/fw_final.bin（CMake 只是编排层）。
#
#   ⚠ platforms/android/ 只是打包脚本，没有 CMakeLists——安卓的编译由
#   Drive/Src/XCore/Test 模块承担（xguidemo 目标在 Test 模块），打包另跑
#   platforms/android/build_apk.sh。项目结构与边界见 platforms/README.md。

# ==================== FreeType 包含前置（先于全部模块接线） ====================
# 轮廓字 FT 模块（XFONT_FT_ON，见 XFont_config.h）的包含前置：
# 根工程经 fontconfig 检测把 -I/usr/include/freetype2（系统 FreeType，
# 本机 26.2.20 与捆绑 2.13 不同源）目录级注入，而目录级 include 先于
# target 传播 include 命中——不前置的话 XPainter.c/XFontFt.c 的
# <ft2build.h> 会吃到系统头、链的却是捆绑 .a（跨版本混编）。构建目录在
# 前：生成的最小 ftmodule.h（只注册 smooth 渲染器）必须先于捆绑全量
# ftmodule.h 命中，否则未编译字体驱动的模块声明落成链接缺符号。
# [拆分注意] 路径注入必须在 add_subdirectory(Src/XCore)/add_subdirectory(Drive)
# **之前**：模块子目录的目录级快照才含这两条前置路径。
include_directories(BEFORE
    "${CMAKE_BINARY_DIR}/Library/freetype/include"
    "${CMAKE_SOURCE_DIR}/Library/freetype/include")

# ==================== 模块接线 ====================
# 顺序契约（动了会炸，见各注释）：
#   1. Src/XCore 最先（核心库）→ Drive（主库组合，挂 XCore/XGuiLayer）；
#      各模块自采头目录并抬升根作用域，后续模块经链接图拿框架头。
#   2. 根 include_directories 落地抬升清单：Library/*（zlib TU 引用
#      XMemory.h/XCrc.h）、XGui（TU 经 CXinYueConfig.h 反向含框架头）等
#      子目录注册时快照到 XCore/Drive 头目录。
#   3. XGui 在平台探测定义之后（XINYUE_C_HAS_* 对 XinYueGui 生效——模块
#      头注释的接线契约）。
#   4. 平台/第三方链接落地段在 Library/* 注册之后：对 Drive 模块目标
#      （XinYueCS/XinYueC）兑现（CMP0079 NEW 跨目录）。
#   5. XGuiLayer 在 add_subdirectory(Src/XGui) 之后定义：Drive 模块以纯
#      目标名预挂 XGuiLayer（generate 期解析），此处接通 XinYueGui。
#   6. enable_testing() 在根：顶层 ctest 可见 Test 模块注册的全部用例。

# 工业设备层根目录（Src/Industry）：CXinYueConfig.h 反向包含
# Industry/XIndustry_config.h，需要能解析该头。
# [快照语义实证 2026-11-01] 目录级 INCLUDE_DIRECTORIES 是 add_subdirectory
# 时刻的快照、之后对根的追加不下发已注册子目录——必须先于
# add_subdirectory(Src/XCore) 入根（核心/主库 TUs 全量 /FI CXinYueConfig.h，
# 快照缺这条即 C1083 XIndustry_config.h）。
# [清理 2026-11-01] 原同位置的仓库根 -I 兜底已删：全仓唯一根前缀包含
# "XGui/XGuiConfig.h" 由 Src 根解析（XCore 模块抬升的 /.. 项；XGui 居
# Src/XGui 后仓库根下已无 XGui/ 目录），仓库根 -I 零消费。
include_directories(${XINYUE_SOURCE_DIR}/Src/Industry)

add_subdirectory(Src/XCore)
# 抬升落地：XCore 模块自采的头目录 + 上级 Src 根（CXinYueConfig.h →
# "XGui/XGuiConfig.h"/"Industry/XIndustry_config.h" 反向包含链，先注册的
# Library/* 与后续模块的目录级快照依赖）。
include_directories(${INCLUDE_XCORE_LIST_LIFT})
# 抬升落地：XGui 模块自持的头目录导出清单（Src/XGui/XGuiHeaders.cmake）。
# [2026-11-01 模块化拆分收口] 落地时点必须在 Drive/Library/* 目标注册**之前**
# ——目录级 INCLUDE_DIRECTORIES 是 add_subdirectory 时刻的快照，先注册者拿
# 不到后落的根清单（Src 曾因快照语义 C1083 实证）。XGui 模块自身排在
# Library/* 之后（它要 PUBLIC 链 zlib，zlib 目标须先存在），其
# PARENT_SCOPE 抬升来不及给先注册的目标，故改为「模块提供导出清单、根在
# 消费者注册前提前落地」。具体链路与判据见 Src/XGui/XGuiHeaders.cmake 头注
# （裸名 "XGuiConfig.h" 的解析点：XGuiConfig.h → XImageCodec_config.h）。
include(${XINYUE_SOURCE_DIR}/Src/XGui/XGuiHeaders.cmake)
include_directories(${XGUI_HEADER_EXPORT_DIRS})
add_subdirectory(Drive)
# 抬升落地：Drive 模块自采的 Drive 头目录。
include_directories(${INCLUDE_DRIVE_LIST})

# ==================== 外部库（以静态库方式构建，合并到 XinYueC 动态库中）====================
# 注意：外部库（lwip/zlib/fatfs）内部引用了 XinYueC 核心符号（XMalloc_*、XCrc32_*、
# XRandomGenerator_*、XTimerData_* 等），与 XinYueC 存在双向依赖，无法独立为动态库。
# 因此保持为静态库，由 XinYueC 动态库统一链接。

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
# ==================== FreeType（字体光栅化库，最小集合）====================
# 以子目录静态库方式集成，详见 Library/freetype/CMakeLists.txt
add_subdirectory(Library/freetype)
# ==================== xxHash（非密码学哈希库）====================
# 上游 xxHash（Yann Collet，BSD-2-Clause），XCryptographicHash_XxHash64 的实现。
# 以子目录静态库方式集成，详见 Library/xxHash/CMakeLists.txt
add_subdirectory(Library/xxHash)

# ==================== 平台/第三方系统库链接落地 ====================
# [2026-11-01 自原 Src 层上提] 探测段、Library/* 注册、链接三段同居本文件；
# XinYueCS/XinYueC 创建于 Drive 子目录，跨目录改目标经 CMP0079 NEW
# （见文件头）。静态库 PUBLIC 传播给可执行程序，动态库 PRIVATE 仅自身
# 链接阶段解析。XinYueCore 自身 TU 直接消费的外部库（zlib/lwip/pcre2/
# sqlite/xxhash/mbedtls）使用需求在 XCore 模块自带，此处不重复承担。

# ---- X11（Linux 原生窗口后端） ----
if(X11_FOUND)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${X11_LIBRARIES})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${X11_LIBRARIES})
    # RandR（屏幕枚举/几何变化）：根已按 dev 链接名/版本名两段式探测。
    if(XINYUE_XRANDR_LIBRARY)
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_XRANDR_LIBRARY})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_XRANDR_LIBRARY})
    endif()
    # XI2（触摸/平滑滚轮）：头（xorgproto）与运行库同时存在才链。
    if(XINYUE_XI_LIBRARY AND XINYUE_XI2_HEADER)
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_XI_LIBRARY})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_XI_LIBRARY})
    endif()
    # DPMS（显示器电源管理）：libXext（不是 libXss）。
    if(XINYUE_XEXT_LIBRARY)
        target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_XEXT_LIBRARY})
        target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_XEXT_LIBRARY})
    endif()
endif()

# XPlatformGraphics 的 GLX 后端：X11 与 OpenGL 同时检出才启用。
if(X11_FOUND AND OPENGL_FOUND)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${OPENGL_gl_LIBRARY})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${OPENGL_gl_LIBRARY})
endif()
# XPlatformGraphics 的 EGL/GLES 后端（Drive/Posix/Graphics/
# XPlatformGraphics_egl.c）：与 GLX 后端等价并列，由运行期开关
# XGUI_GL_BACKEND=egl|glx 选择（默认 EGL 优先）。
# 存在理由：嵌入式 BSP 常只提供 EGL/GLES 用户态库而不提供 GLX DRI
# 驱动（RK3568 厂商 BSP 即如此），此时 GLX 路径会静默回退软件光栅，
# 而 EGL 路径能拿到厂商 GPU。EGL 不可用时自动回落 GLX，行为不变。
# 交叉构建注意：Debian multiarch 把 EGL 放在 <sysroot>/lib/<arch>/
# 而非 <sysroot>/usr/lib/<arch>/，且 CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
# 为 ONLY ⇒ find_package(EGL) 找不到库；显式补候选路径再探测。
if(X11_FOUND)
    find_package(EGL QUIET)
    if(NOT EGL_FOUND)
        foreach(_egl_dir
                ${CMAKE_SYSROOT}/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                ${CMAKE_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                ${CMAKE_SYSROOT}/lib
                ${CMAKE_SYSROOT}/usr/lib)
            find_library(XINYUE_EGL_LIBRARY NAMES EGL
                         PATHS ${_egl_dir} NO_DEFAULT_PATH)
            if(XINYUE_EGL_LIBRARY)
                set(EGL_FOUND TRUE)
                set(EGL_INCLUDE_DIR ${CMAKE_SYSROOT}/usr/include)
                set(EGL_LIBRARIES ${XINYUE_EGL_LIBRARY})
                break()
            endif()
        endforeach()
    endif()
    if(EGL_FOUND)
        add_compile_definitions(XINYUE_C_HAS_EGL=1)
        if(TARGET EGL::EGL)
            target_link_libraries(${EXPORT_NAME}S PUBLIC EGL::EGL)
            target_link_libraries(${EXPORT_NAME} PRIVATE EGL::EGL)
        else()
            target_link_libraries(${EXPORT_NAME}S PUBLIC ${EGL_LIBRARIES})
            target_link_libraries(${EXPORT_NAME} PRIVATE ${EGL_LIBRARIES})
        endif()
    endif()
endif()
# Vulkan（UNIX）：按库存在性启用（Android 排除已在探测段处理）。
if(XINYUE_VULKAN_LIBRARY)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_VULKAN_LIBRARY})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_VULKAN_LIBRARY})
endif()

# Linux AT-SPI 后端 dbus-1 与 fontconfig（fbdev 交叉构建排除已在探测段处理）。
if(XINYUE_DBUS_FOUND)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_DBUS_LIBRARIES})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_DBUS_LIBRARIES})
endif()
if(XINYUE_FONTCONFIG_FOUND)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_FONTCONFIG_LIBRARIES})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_FONTCONFIG_LIBRARIES})
endif()

# POSIX USB Host 后端 libusb-1.0（pkg-config 与 find_path/find_library 两
# 段探测在根，链接按同一变量兑现）。
if(XINYUE_LIBUSB_FOUND)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_LIBUSB_LIBRARIES})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_LIBUSB_LIBRARIES})
elseif(XINYUE_LIBUSB_INCLUDE_DIR AND XINYUE_LIBUSB_LIBRARY)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_LIBUSB_LIBRARY})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_LIBUSB_LIBRARY})
endif()

# [2026-10-26 armel 交叉构建固化] 32 位 ARM（gnueabi soft-float）的
# 64 位原子操作（XAtomic_GCC 的 __atomic_store_8 等）需链 libatomic——
# 仅交叉编译时追加。非交叉桌面/安卓构建不受影响。
# 必须挂静态库 PUBLIC 依赖，消费端链接行才落在库后可解析（link_libraries
# 前置会导致 undefined __atomic_store_8，1018 战役实证）。
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    target_link_libraries(${EXPORT_NAME}S PUBLIC atomic)
    # [2026-11-01] 共享库同样要链：共享库内的 64 位原子符号若不显式链
    # libatomic，其 .so 会带着未定义原子符号出库（undefined reference to
    # __atomic_load_8@LIBATOMIC_1.0）——消费端（XinYueC_Dynamic 等）链接
    # 该 .so 时连带解析失败。设备侧运行时库 /root/xguidemo/lib/
    # libatomic.so.1 已在位（10-31 实证），此处只补链接期需求。
    target_link_libraries(${EXPORT_NAME} PRIVATE atomic)
endif()

# ---- 第三方静态库链接（接线方式同 zlib：PRIVATE 依赖经 LINK_ONLY 传给
#      最终可执行文件） ----
# 链接 FreeType 静态库（轮廓字 FT 模块 XFONT_FT_ON，见 XFont_config.h；
# 包含路径由上方 include_directories(BEFORE) 前置防系统头混编）
target_link_libraries(${EXPORT_NAME}S PRIVATE freetype)
target_link_libraries(${EXPORT_NAME} PRIVATE freetype)
# 链接 mbedTLS 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE mbedtls)
target_link_libraries(${EXPORT_NAME} PRIVATE mbedtls)
# 链接 PCRE2 16-bit 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE pcre2)
target_link_libraries(${EXPORT_NAME} PRIVATE pcre2)
# 链接 SQLite 源码静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE sqlite)
target_link_libraries(${EXPORT_NAME} PRIVATE sqlite)
# 链接 xxHash 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE xxhash)
target_link_libraries(${EXPORT_NAME} PRIVATE xxhash)
# 链接 lwIP 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE lwip)
target_link_libraries(${EXPORT_NAME} PRIVATE lwip)
# 链接 zlib 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE zlib)
target_link_libraries(${EXPORT_NAME} PRIVATE zlib)
# 链接 FatFs 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE fatfs)
target_link_libraries(${EXPORT_NAME} PRIVATE fatfs)

# ---- Windows 平台依赖 ----
# - Advapi32：XSystem 后端调整关机权限；
# - User32：窗口类注册/句柄操作/消息泵（XPlatformNativeWindow_win32.c）；
# - Gdi32：DIB 后备缓冲 + BitBlt/SetDIBitsToDevice 上屏。
# 库清单（含 Vulkan SDK/vendored 两形态择一）在探测段组装
# （XINYUE_WIN32_LIBS），静态库将依赖公开给最终可执行文件，动态库在自身
# 链接阶段解析。
if(WIN32)
    target_link_libraries(${EXPORT_NAME}S PUBLIC ${XINYUE_WIN32_LIBS})
    target_link_libraries(${EXPORT_NAME} PRIVATE ${XINYUE_WIN32_LIBS})
endif()

# ---- Android 平台依赖（bionic 系统库） ----
# - log：平台层诊断输出（__android_log_print）；
# - android：ANativeWindow/NativeActivity（XPlatformWindowAndroid.c、
#   android_main.c）。动态库为 --no-undefined 语义，必须在本体链接
#   阶段解析；静态库 PUBLIC 传给最终载体（libxguidemo.so）。
if(ANDROID)
    target_link_libraries(${EXPORT_NAME}S PUBLIC android log)
    target_link_libraries(${EXPORT_NAME} PRIVATE android log)
endif()

# ==================== XGui（GUI 框架模块）====================
# XinYueGui 静态库；平台探测定义已注入（契约见 Src/XGui/CMakeLists.txt 头注释）。
# XGui 头目录不抬升：传递全走链接图（XinYueCS PUBLIC XGuiLayer → XinYueGui
# 的 PUBLIC 头目录），反向包含链走上级 Src 根（XCore 模块抬升）。
add_subdirectory(Src/XGui)

# XGuiLayer 接口胶水：Drive 模块已对 XinYueCS/XinYueC 预挂 PUBLIC/PRIVATE
# XGuiLayer（彼时 XinYueGui 未定义，纯目标名 generate 期解析）；此处接通。
# 消费目标链接 ${EXPORT_NAME}S 即随链获得 XinYueGui 及其头目录——XGui 拆库
# 后测试/demo 链接行零改动。XGUI_ON=0/嵌入式：XinYueGui 不建，XGuiLayer
# 恒存在但为空 INTERFACE，零链接残留。
add_library(XGuiLayer INTERFACE)
if(TARGET XinYueGui)
    target_link_libraries(XGuiLayer INTERFACE XinYueGui)
endif()

# ==================== Industry（工业设备层）====================
# 以子目录静态库方式集成，详见 Src/Industry/CMakeLists.txt
# 通道接口（XChannel：XIoChannel + DI/DO/AI/AO + 适配器）与工业设备
# （XSolenoidValve 电磁阀）；测试可执行 XIndustryValve_Test 随子目录注册。
add_subdirectory(Src/Industry)
# 抬升落地：Industry 模块自持的头目录清单（XChannel/XSolenoidValve 供
# 裸名 include）。[2026-11-01 模块化拆分收口] 落地点必须早于
# add_subdirectory(Test)（目录属性是 add_subdirectory 时刻的快照，先注册
# 的目标拿不到后落的根清单）：Test 模块的 XinYueC_Dynamic 链共享库
# ${EXPORT_NAME}，而共享库对本库是 PRIVATE 链接（不能改 PUBLIC：会把未
# 导出的 XinYueIndustry 带进 install(EXPORT) 导出集），故其消费者拿不到
# 本库 PUBLIC 头目录 → Test/XIndustryTest/*.c 的 #include "XIoChannel.h"
# 解析失败。判据详见 Src/Industry/CMakeLists.txt 头注。
include_directories(${INCLUDE_INDUSTRY_LIST_LIFT})

# ==================== HighSpeedFilter（高速过滤器模板项目）====================
# HsfServer/HsfClient 双可执行，依赖主库 ${EXPORT_NAME}S（必须在主库定义
# 之后接线——模块头注释的契约）。
add_subdirectory(HighSpeedFilter)

enable_testing()

# ==================== Test（测试/工具/demo 可执行模块）====================
# TEST_FILE 收集（含自带 main 独立可执行的剔除清单）、主程序双 exe、全部
# 专项探针/回归测试、add_test/CTest 注册、XUpgradePack、xguidemo 安卓 APK
# 壳与 exe 安装，全部在 Test/CMakeLists.txt。
add_subdirectory(Test)

#安装
include(CMake_Install.cmake)
