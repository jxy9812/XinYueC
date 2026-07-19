# ==================== mbedTLS 静态库 ====================
# XSsl SSL/TLS 后端，按三层库（crypto / x509 / TLS）组织，
# 仅编必需源码；后量子密码 pqcp 体积大（约2.3MB），默认关闭。
#
# mbedTLS 平台依赖在 XSsl_mbedtls.c 中桥接到 XinYueC：
# 随机数 -> XRandomGenerator，内存 -> XMemory(XMalloc_System/XMalloc_MultiPool)，
# 文件 -> XFile，网络 BIO -> XAbstractSocket。

option(MBEDTLS_ENABLE_PQCP "编译后量子密码 pqcp 驱动（嵌入式默认关）" OFF)

# --- 公开头文件路径（仅构建时，安装时由目标工程自行提供）---
set(MBEDTLS_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/library>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/core>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/include>"
)

# --- 私有头文件路径（仅编译单元可见）---
set(MBEDTLS_PRIVATE_INCS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/utilities"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/extras"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/dispatch"
)

# 1) 加密核心（tf-psa-crypto，独立库）+ 经典算法 builtin 驱动
file(GLOB MBEDTLS_CRYPTO_SRCS
    "platform/*.c"    
    "tf-psa-crypto/core/*.c"
    "tf-psa-crypto/utilities/*.c"
    "tf-psa-crypto/platform/*.c"
    "tf-psa-crypto/extras/*.c"
    "tf-psa-crypto/drivers/builtin/src/*.c"
)

# 2) X.509 + TLS（顶层 library）
file(GLOB MBEDTLS_TLS_SRCS "library/*.c")

# 3) 后量子密码（可选，默认关）
if(MBEDTLS_ENABLE_PQCP)
    list(APPEND MBEDTLS_PUBLIC_INCS
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/drivers/pqcp/include>"
    )
    file(GLOB_RECURSE MBEDTLS_PQCP_SRCS "tf-psa-crypto/drivers/pqcp/*.c")
endif()

# 创建静态库
add_library(mbedtls STATIC
    ${MBEDTLS_CRYPTO_SRCS}
    ${MBEDTLS_TLS_SRCS}
    ${MBEDTLS_PQCP_SRCS}
)
set_target_properties(mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 传播头文件路径
target_include_directories(mbedtls PUBLIC ${MBEDTLS_PUBLIC_INCS})
target_include_directories(mbedtls PRIVATE ${MBEDTLS_PRIVATE_INCS})

# 启用 XSsl mbedTLS 后端（传播给链接方）
target_compile_definitions(mbedtls PUBLIC XSSL_USE_MBEDTLS)

# Windows(MSVC): mbedtls_platform_get_entropy() 调用 BCryptGenRandom（需 bcrypt.lib）。
# 已开启 MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG，TLS/PSA 运行时随机数走 XRandomGenerator；
# 但 platform_util.c 仍被编译并链接，故 Windows 上必须链接 bcrypt 解析符号。
# Linux 用 getrandom()/urandom，无需额外库。
if(WIN32)
    target_link_libraries(mbedtls PUBLIC bcrypt)
endif()
