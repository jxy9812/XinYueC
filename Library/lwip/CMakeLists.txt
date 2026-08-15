# ==================== lwIP 轻量级 TCP/IP 协议栈 ====================
# 轻量级 TCP/IP 协议栈，适用于嵌入式/资源受限环境。
# 包含 XinYueC 网络适配层（XNetwork_lwip.c / XNetwork_lwip_platform.h）。

# --- 公开头文件路径（传播给链接方）---
set(LWIP_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

# 收集所有源文件
file(GLOB_RECURSE LWIP_SRCS "*.c")

# 创建静态库
add_library(lwip STATIC ${LWIP_SRCS})
set_target_properties(lwip PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 传播头文件路径
target_include_directories(lwip PUBLIC ${LWIP_PUBLIC_INCS})
target_include_directories(lwip PRIVATE
    "${PROJECT_SOURCE_DIR}/Src/XCode/XAlgorithm/XCrc"
)
