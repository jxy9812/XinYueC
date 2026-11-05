# ==================== 高速过滤器（HighSpeedFilter）模板项目 ====================
# 一个 CMake 管两个自带 main 的可执行，目录三分：
#   common/  公共协议约定（常量/文档，无 .c）
#   server/  HsfServer —— XCoreApplication 控制台事件循环 + IIR 低通滤波 + TCP
#   client/  HsfClient —— XGuiApplication GUI 事件循环，发送采样/显示滤波结果
#
# 文件搜集：主库同款 file(GLOB_RECURSE)——新增/删除源文件后须触发重新
# 配置（改动本文件即可）。头文件目录同款走 CMake_Config.cmake 的
# FIND_INCLUDE_DIR（该宏在根 include(CMake_Config.cmake) 后全局可用）：
# 各自目录 + 公共 common 全部自动入列，裸文件名 include 直达。
#
# 集成方式：根 CMakeLists.txt add_subdirectory(HighSpeedFilter)（必须位于
# 主库 ${EXPORT_NAME}S 定义之后）。依赖宿主变量：EXPORT_NAME、
# XINYUE_UNIX_THREAD_LIBS、XINYUE_UNIX_EXTRA_LIBS（根 CMakeLists 定义）。
#
# 协议（文本行，换行结尾，详见 common/XhsfProtocol.h）：
#   d <浮点>   —— 提交一个采样          → 应答 r <滤波后值>
#   a <浮点>   —— 设置平滑系数(0..1]   → 应答 ok
#   z          —— 复位滤波器           → 应答 ok

# ---- 头文件目录自动采集（CMake_Config.cmake 同款 FIND_INCLUDE_DIR）----
FIND_INCLUDE_DIR(HSF_COMMON_INCLUDE_DIRS "common")
FIND_INCLUDE_DIR(HSF_SERVER_INCLUDE_DIRS "server")
FIND_INCLUDE_DIR(HSF_CLIENT_INCLUDE_DIRS "client")

# ---- 源文件自动搜集 ----
# 2026-11-03：新增 client/ui/HsfSettingsStore.c（界面设置持久化门面）——
# 改动本文件即触发 ninja 重跑 cmake，GLOB 重新求值收进新文件。
file(GLOB_RECURSE HSF_SERVER_FILE "${CMAKE_CURRENT_LIST_DIR}/server/*.c")
file(GLOB_RECURSE HSF_CLIENT_FILE "${CMAKE_CURRENT_LIST_DIR}/client/*.c")
# [2026-11-04 单测可链接化] 服务端 IO 站模块单独编成静态库：
# 原先它们只随 add_executable(HsfServer) 编译，Test/ 侧无法链接 ⇒
# HsfServerIoFlowMeter / HsfServerIoTempHum 等站模块零单测（端到端手测
# 才覆盖）。抽成库后，HsfServer/HsfServerFlow 与测试目标共享同一份实现。
file(GLOB_RECURSE HSF_SERVER_IO_FILE
    "${CMAKE_CURRENT_LIST_DIR}/server/io/*.c")
list(REMOVE_DUPLICATES HSF_SERVER_IO_FILE)

add_library(XHsfServerIoLib STATIC ${HSF_SERVER_IO_FILE})
set_target_properties(XHsfServerIoLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(XHsfServerIoLib PUBLIC
    ${HSF_COMMON_INCLUDE_DIRS}
    ${HSF_SERVER_INCLUDE_DIRS})

# 可执行只编 server/ 顶层（main/App/Io 编排/Settings/旧 XhsfServer），
# io/ 那几个站模块由 XHsfServerIoLib 提供——从源列表里剔掉，避免重复定义。
set(HSF_SERVER_MAIN_FILE ${HSF_SERVER_FILE})
list(FILTER HSF_SERVER_MAIN_FILE EXCLUDE REGEX "/server/io/")

add_executable(HsfServer ${HSF_SERVER_MAIN_FILE})
add_executable(HsfClient ${HSF_CLIENT_FILE})

# [2026-11-04 电磁流量计联调] 同源码的第二个服务端可执行，输出名
# HsfServerFlow.exe —— 现场常有「另一路在测其他功能」同时跑 HsfServer.exe
# 的情形，两者的 bin\HsfServer.exe 会互相占用（LNK1168 无法写入）且抢
# 同一个串口。本目标让流量计联调可以独立起一份、独立端口，互不干扰。
#
# [关键] 路 B 的启用是**编译期宏**，必须只对本目标生效：HsfServerConfig.h
# 的默认值是 0（关闭），这里用 PRIVATE define 打开。若把默认值改成 1，
# 所有从本树编出来的 HsfServer.exe 都会去抢 COM18（2026-11-04 实锅：
# 另一会话的 6100 服务端因此抢走串口，两边互相踢，打开错误码=3）。
add_executable(HsfServerFlow ${HSF_SERVER_MAIN_FILE})
set_target_properties(HsfServerFlow PROPERTIES OUTPUT_NAME "HsfServerFlow")
target_compile_definitions(HsfServerFlow PRIVATE HSF_BUS_B_ENABLED=1)

# 头文件路径：各自目录 + 公共 common（宏在当前作用域直接出结果变量）。
target_include_directories(HsfServer PRIVATE
    ${HSF_COMMON_INCLUDE_DIRS}
    ${HSF_SERVER_INCLUDE_DIRS})
target_include_directories(HsfServerFlow PRIVATE
    ${HSF_COMMON_INCLUDE_DIRS}
    ${HSF_SERVER_INCLUDE_DIRS})
target_include_directories(HsfClient PRIVATE
    ${HSF_COMMON_INCLUDE_DIRS}
    ${HSF_CLIENT_INCLUDE_DIRS})

if(UNIX)
    target_link_libraries(HsfServer
        XHsfServerIoLib
        ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    target_link_libraries(HsfServerFlow
        XHsfServerIoLib
        ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    # 客户端走 GUI 栈：链接序对齐 XGuiWindowDemo_Test（sqlite/freetype 同配置）。
    target_link_libraries(HsfClient
        ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
else()
    target_link_libraries(HsfServer
        XHsfServerIoLib
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    target_link_libraries(HsfServerFlow
        XHsfServerIoLib
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    target_link_libraries(HsfClient
        mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
endif()

# ==================== 静态归档环依赖收口（模块化拆分遗留）====================
# 判据与机制见根 CMakeLists 的 xinyue_link_rescan_group()：框架链与第三方
# 归档间存在双向符号依赖（zlib↔Core、Gui↔Core），静态链接单遍扫描无法解析
# （HsfServer/HsfClient 实测 undefined reference: atan2/floor/log10 等数学
# 符号与 Core 符号），需 LINK_GROUP:RESCAN 组内反复重扫。
xinyue_link_rescan_group(HsfServer)
xinyue_link_rescan_group(XHsfServerIoLib)
xinyue_link_rescan_group(HsfServerFlow)
xinyue_link_rescan_group(HsfClient)

# ==================== xlsx_bridge（Python 客户端 Excel 导出动态库）====================
# 高速过滤器 Python 客户端（client_py/）导出 Excel 用：把 XExcel（XCore/XData）
# 折成 6 个纯 C 函数（xlsx_create/write_str/write_num/add_sheet/set_col_width/
# save/delete），client_py 用 ctypes 加载 hsfsrv_client/bin/xlsx_bridge.dll。
# 链接集与 XDataTest 同口径（Core 静态库 + Library 外部库）+ RESCAN 收口。
# 源文件不存在（如未拉全）时跳过，不影响本文件其他目标。
set(XLSX_BRIDGE_SRC "${CMAKE_CURRENT_LIST_DIR}/client_py/tools/xlsx_bridge/xlsx_bridge.c")
if(EXISTS "${XLSX_BRIDGE_SRC}")
    add_library(xlsx_bridge SHARED "${XLSX_BRIDGE_SRC}")
    set_target_properties(xlsx_bridge PROPERTIES
        PREFIX "" OUTPUT_NAME "xlsx_bridge"
        POSITION_INDEPENDENT_CODE ON)
    if(UNIX)
        target_link_libraries(xlsx_bridge
            ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
            mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    else()
        target_link_libraries(xlsx_bridge
            mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    endif()
    xinyue_link_rescan_group(xlsx_bridge)
    add_custom_command(TARGET xlsx_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_CURRENT_LIST_DIR}/client_py/hsfsrv_client/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:xlsx_bridge>"
                "${CMAKE_CURRENT_LIST_DIR}/client_py/hsfsrv_client/bin/"
        COMMENT "拷贝 xlsx_bridge 动态库 -> client_py/hsfsrv_client/bin/")
endif()
