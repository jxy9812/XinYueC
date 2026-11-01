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
file(GLOB_RECURSE HSF_SERVER_FILE "${CMAKE_CURRENT_LIST_DIR}/server/*.c")
file(GLOB_RECURSE HSF_CLIENT_FILE "${CMAKE_CURRENT_LIST_DIR}/client/*.c")

add_executable(HsfServer ${HSF_SERVER_FILE})
add_executable(HsfClient ${HSF_CLIENT_FILE})

# 头文件路径：各自目录 + 公共 common（宏在当前作用域直接出结果变量）。
target_include_directories(HsfServer PRIVATE
    ${HSF_COMMON_INCLUDE_DIRS}
    ${HSF_SERVER_INCLUDE_DIRS})
target_include_directories(HsfClient PRIVATE
    ${HSF_COMMON_INCLUDE_DIRS}
    ${HSF_CLIENT_INCLUDE_DIRS})

if(UNIX)
    target_link_libraries(HsfServer
        ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    # 客户端走 GUI 栈：链接序对齐 XGuiWindowDemo_Test（sqlite/freetype 同配置）。
    target_link_libraries(HsfClient
        ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
else()
    target_link_libraries(HsfServer
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
    target_link_libraries(HsfClient
        mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
endif()
