# ==================== XGui（GUI 框架模块）====================
# Library/ 同款模块写法：本目录自 Src/XGui 拆出（2026-10-31，自成静态库
# XinYueGui；2026-11-01 回迁 Src/ 与 XCore 平级——模块边界不变，仍独立
# 目标，Src 主库 glob 已按 .*/Src/XGui/.* 剔除防双列）；文件搜集主库同款
# file(GLOB_RECURSE)——新增/删除源文件后须触发重新配置（改动本文件即可）。
#
# 头文件目录在本文件内自采（FIND_INCLUDE_DIR 宏由根
# include(CMake_Config.cmake) 定义，add_subdirectory 前已全局可用；
# add_subdirectory 于根平台依赖块（X11/DBus/fontconfig 的
# add_compile_definitions）之后执行，XINYUE_C_HAS_X11 等定义对本目标生效）。
#
# 依赖方向（已验证）：XGui → XCore/XContainer/Drive 平台（均在主库
# XinYueCS）+ freetype；反向无非 GUI 代码引用 XGui 符号。链接链：
# 消费目标 → XinYueGui → XinYueCS（根 CMakeLists 里
# target_link_libraries(XinYueCS PUBLIC XinYueGui) 声明传递，消费者零改动）。
#
# 嵌入式（F407 等 -DXGUI_ON=0）不建本库：XGui 各 .c 按 XGUI_ON 整体编译
# 为空，独立成库只会多一个空目标；消费目标已按 NOT XINYUE_EMBEDDED 守卫。

# ---- 头文件目录自动采集（本模块自管）----
FIND_INCLUDE_DIR(XGUI_INCLUDE_DIRS ".")

file(GLOB_RECURSE XGUI_FILE "${CMAKE_CURRENT_LIST_DIR}/*.c")

if(NOT XINYUE_EMBEDDED)
    add_library(XinYueGui STATIC ${XGUI_FILE})
    set_target_properties(XinYueGui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # 模块自身目录（XGuiConfig.h 等裸名 include）+ 上级 Src 根（CXinYueConfig.h
    # 的 #include "XGui/XGuiConfig.h" 相对路径口径；XGui 居 Src/ 后 /.. 恰为
    # Src 根，"XGui/..." 前缀包含同口径覆盖）。
    target_include_directories(XinYueGui PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${XGUI_INCLUDE_DIRS})

    # zlib（拆分补线 2026-11-01）：XImageCodecPng/Svg 与 XGuiRemoteCodec 的
    # TU 直接 #include "zlib.h"——拆库前这些 TU 在主库 XinYueCS 里，经主库
    # PRIVATE zlib 链接的使用需求拿头路径；独立成库后必须自带该使用需求。
    # PUBLIC：装出/消费端链接行本就显式列 zlib（重复列出无害）。
    target_link_libraries(XinYueGui PUBLIC zlib)

    # ---- XGuiRemote 编解码 NEON 加速（perf9 路4，2026-10-05） ----
    # [2026-11-01 随模块化拆分自根迁入] set_source_files_properties 是目录
    # 作用域的：本文件由 XinYueGui（本目录创建）编译，属性必须落在目标定义
    # 目录——留在根 CMakeLists 会指向失效的 Src/XGui 路径且对本目标不生效
    # （搬库时的坑，迁此收口）。仅对 XGuiRemoteCodec.c 追加 -mfpu=neon
    # （ARM 交叉目标默认开；-DXGUI_REMOTE_NEON=0 显式关回，option 在根）。
    # ABI 恒 softfp 不变；仅 ARM32 加此参（aarch64 NEON 为标配且 gcc 拒绝
    # -mfpu，RK3568 实测）。
    if(XGUI_REMOTE_NEON AND CMAKE_C_COMPILER_ID STREQUAL "GNU"
            AND CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
        set_source_files_properties(
            "${CMAKE_CURRENT_LIST_DIR}/Remote/XGuiRemoteCodec.c"
            PROPERTIES COMPILE_OPTIONS "-mfpu=neon")
    endif()
endif()

# [历史 2026-10-31 → 2026-11-01 移除抬升] 原将模块头目录抬回父作用域
# （INCLUDE_XGUI_AFTER_LIST），设计假设"目录属性生成期解析，根随后
# include_directories 对先注册的 Library 目标同样生效"——实测为误：目录级
# INCLUDE_DIRECTORIES 是 add_subdirectory 时刻的快照，先注册者拿不到后落的
# 根清单（Src 曾因快照语义 C1083 实证）。现 XGui 头目录的传递全走链接图
# （XinYueCS PUBLIC XGuiLayer → 本库 PUBLIC 头目录），"XGui/XGuiConfig.h"
# 反向包含链走上级 Src 根（XCore 模块抬升），本模块不再抬升。
