# ==================== XCore（核心框架模块） ====================
# Src 核心子目录统一收进本目录（XClass/XCode/XContainer/XData/XDevice/
# XIO/XMemory/XPlatform/XProtocol/XTimer/XTui），自成静态库 XinYueCore——
# 裁剪最小系统时只需包含本目录（+ 平台 Drive 模块）。
# 主库组合（XinYueCS/XinYueC = Drive 源树 + 挂接本库）见
# Drive/CMakeLists.txt（平台组合模块）；消费目标链接行零改动。
#
# XGui/Industry/Library 同款模块写法：file(GLOB_RECURSE) 配置期固化，新增/
# 删除源文件后须触发重新配置（改动本文件即可）。
# 模块只含自己的头文件与源文件：头目录 FIND_INCLUDE_DIR(".") 自采（XGui
# 模块同款），不越界引用其他模块。
# CXinYueConfig.h 居上级 Src 根：强制包含走根函数（绝对路径），上级目录入
# PUBLIC 供 "XGui/XGuiConfig.h" 反向包含与 config 裸名 include。

# ---- 头文件目录自动采集（本模块自管）----
FIND_INCLUDE_DIR(INCLUDE_XCORE_LIST ".")

# ---- 抬升根作用域（XGui 模块历史抬升的同款机制，经 PARENT_SCOPE）----
# 上级 Src 根 + 本模块叶子目录：先注册的 Library/*（zlib TU 引用
# XMemory.h → CXinYueConfig.h → "XGui/XGuiConfig.h" 的整条反向包含链）与
# 后续模块的目录级快照依赖这两级路径。
set(INCLUDE_XCORE_LIST_LIFT
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ${INCLUDE_XCORE_LIST}
    PARENT_SCOPE)

# 本目录由根最先 add_subdirectory，目录级快照必须自给（根的抬升落地晚于
# 本目录注册）。
include_directories(${INCLUDE_XCORE_LIST})

# ---- 源文件自动搜集 ----
file(GLOB_RECURSE XCORE_FILE "${CMAKE_CURRENT_LIST_DIR}/*.c")
list(REMOVE_DUPLICATES XCORE_FILE)

add_library(XinYueCore STATIC ${XCORE_FILE})
# POSITION_INDEPENDENT_CODE：本库随 XinYueC 动态库（PRIVATE 挂接）编入
# .so，XGui 模块同款要求。
set_target_properties(XinYueCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
# 模块自身目录（裸名 include）+ 上级 Src 根（CXinYueConfig.h 的
# "XGui/XGuiConfig.h" 反向包含口径）+ 各子目录叶子。
target_include_directories(XinYueCore PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${INCLUDE_XCORE_LIST})
# ESP8266 移植 #if MODULE_ON 文件顶部守卫同款纪律：与库 TU 同口径强制包含
# CXinYueConfig.h（函数在根 CMakeLists 定义，本目录是其子目录可见）。
xinyue_force_include_config(XinYueCore)

# ---- 外部库使用需求（本模块 TU 直接 #include 的外部头） ----
# [2026-11-01 摸底+全链编译实证] zlib（XByteArray/XZipReader/XZipWriter/
# XUpgradePackage/XUpgradeEngine 等 6 处）、lwip（opt.h/sys.h，NO_SYS/
# sys_prot 桥）、pcre2（pcre2_xin_memory.h）、sqlite3、xxhash、mbedtls
# （XConsoleShell_XSsh.c 的 psa/crypto.h；XCryptographic/XSsl/XUpgrade 链
# 上同源）。PUBLIC：消费端链接行本就显式列出这些库（重复列出无害）。
# FatFs 无本模块消费者（仅注释提及）。平台库（X11/DBus/libusb/Vulkan/
# Win32/Android）在根落地。
target_link_libraries(XinYueCore PUBLIC
    zlib lwip pcre2 sqlite xxhash mbedtls)

# 最小系统裁剪：本库随主库 XinYueCS 口径独立安装
install(TARGETS XinYueCore
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
 )
