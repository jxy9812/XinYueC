# ==================== mbedTLS 静态库 ====================
# XSsl SSL/TLS 后端，按三层库（crypto / x509 / TLS）组织，
# 仅编必需源码；后量子密码 pqcp 体积大（约2.3MB），默认关闭。
#
# mbedTLS 平台依赖在 XSsl_mbedtls.c 中桥接到 XinYueC：
# 随机数 -> XRandomGenerator，内存 -> XMemory(XMalloc_System/XMalloc_MultiPool)，
# 文件 -> XFile，网络 BIO -> XAbstractSocket。

option(MBEDTLS_ENABLE_PQCP "编译后量子密码 pqcp 驱动（嵌入式默认关）" OFF)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD
    "使用 XCryptographic 实现 PSA AEAD（GCM/CCM/ChaCha20-Poly1305）" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH
    "使用 XCryptographic 实现 PSA 摘要（单次和分段）" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_MD
    "使用 XCryptographic 实现 mbedTLS 通用 MD/HMAC 兼容层" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_HASH
    "使用 XCryptographic 实现并裁剪旧版 MD5/SHA/RIPEMD-160 直接算法" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_CIPHER
    "使用 XCryptographic 实现 mbedTLS 通用 cipher 兼容层（分组/CTR/AEAD）" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_MAC
    "使用 XCryptographic 实现 PSA HMAC 与 AES-CMAC（单次和分段）" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR
    "使用 XCryptographic 实现 PSA AES-CTR" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC
    "使用 XCryptographic 实现 PSA P-256/P-384/P-521/secp256k1/brainpoolP256r1/brainpoolP384r1/brainpoolP512r1 ECDH、相同曲线 ECDSA 和 X25519/X448" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER
    "使用 XCryptographic 实现 PSA AES-ECB/CBC/CFB/OFB" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS
    "使用 XCryptographic 实现 PSA AES-XTS（256/512 位组合密钥）" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_HMAC_DRBG
    "使用 XCryptographic HMAC 实现 mbedTLS HMAC-DRBG" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR_DRBG
    "使用 PSA/XCryptographic AES-ECB 实现 mbedTLS CTR-DRBG" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_LMS
    "使用 XCryptographic 实现 LMS/LM-OTS SHA-256 验签" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20
    "使用 XCryptographic 实现 PSA ChaCha20 流密码" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF
    "使用 XCryptographic 实现 PSA SHAKE128/256 XOF" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG
    "使用 XCryptographic 实现 PSA CCM*-无标签流密码" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_KDF
    "使用 XCryptographic 实现 PSA HKDF（提取/扩展）/TLS1.2 PRF/PBKDF2-HMAC/PBKDF2-AES-CMAC KDF" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_FFDH
    "使用 XCryptographic 实现 PSA 有限域 Diffie-Hellman (FFDH)" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_RSA
    "使用 XCryptographic 实现 PSA RSA 加解密/签名/密钥" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE
    "使用 XCryptographic 实现 PSA ECJPAKE（secp256r1/SHA-256）" ON)
option(MBEDTLS_USE_XCRYPTOGRAPHIC_KEY_WRAP
    "使用 XCryptographic 实现 PSA AES-KW/KWP（RFC 3394/5649）" ON)


# --- 公开头文件路径（仅构建时，安装时由目标工程自行提供）---
set(MBEDTLS_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/library>"
)

# --- 私有头文件路径（仅编译单元可见）---
# 适配层实现所需的私有头文件与 XCryptographic 路径。
set(MBEDTLS_PRIVATE_INCS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/psa"
    "${CMAKE_CURRENT_SOURCE_DIR}/library/legacy"
    "${CMAKE_CURRENT_SOURCE_DIR}/library/xcryptographic"
    "${PROJECT_SOURCE_DIR}/Src/XCode/XCryptographic"
)

# 1) mbedTLS 的 XCryptographic 后端兼容入口。
# 实现归 mbedTLS 目标所有，算法调用全部经过 XCryptographic.h，
# 因此 mbedTLS 可以脱离 XinYueC 主库单独替换/链接后端。
file(GLOB MBEDTLS_CRYPTO_SRCS
    "platform/*.c"
    "library/legacy/*.c"
    "library/xcryptographic/*.c"
)
if(MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE)
    # ECJPAKE arithmetic and protocol state live in XCryptographic. Keep the
    # PSA adapter, but do not compile the legacy mbedTLS algorithm source.
    list(FILTER MBEDTLS_CRYPTO_SRCS EXCLUDE REGEX "/ecjpake\\.c$")
endif()
file(STRINGS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/psa/crypto_config.h"
    MBEDTLS_CONFIG_LMS_PRIVATE_DEFINE
    REGEX "^[ \t]*#define[ \t]+MBEDTLS_LMS_PRIVATE([ \t]|$)")
if(MBEDTLS_USE_XCRYPTOGRAPHIC_LMS AND NOT MBEDTLS_CONFIG_LMS_PRIVATE_DEFINE)
    # 当前启用的 LMS 配置只包含公开验签。若显式启用私钥状态签名，保留
    # mbedTLS 的实验性私钥实现，避免因仅替换公开验签而缺失符号。
    list(FILTER MBEDTLS_CRYPTO_SRCS EXCLUDE REGEX "/(lms|lmots)\\.c$")
    list(APPEND MBEDTLS_CRYPTO_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/XSsl_mbedtls_lms.c")
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_HASH
   AND MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_MD
   AND MBEDTLS_USE_XCRYPTOGRAPHIC_HASH
   AND MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
    # 通用 MD/HMAC、PSA 哈希与 XOF 均已路由到 XCryptographic，
    # 不再编译旧版 MD5/SHA-1/SHA-2/SHA-3/RIPEMD-160 直接算法源码。
    list(FILTER MBEDTLS_CRYPTO_SRCS EXCLUDE REGEX
        "/(md5|sha1|sha256|sha512|sha3|ripemd160)\.c$")
endif()

if(MBEDTLS_USE_XCRYPTOGRAPHIC_RSA)
    # PSA RSA 算法路径与密钥导入/生成/导出已路由到 XCryptographic。
    # 旧版 mbedtls_rsa_* 数值运算仅剩未路由的兼容分支引用，先排除
    # 以验证路由完整性。
    list(FILTER MBEDTLS_CRYPTO_SRCS EXCLUDE REGEX
        "/(rsa|rsa_alt_helpers)\.c$")
endif()

if(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    # 临时：排除旧 ECP/ECDSA 数学源，枚举剩余旧 API 引用。
    list(FILTER MBEDTLS_CRYPTO_SRCS EXCLUDE REGEX
        "/(ecp\.c|ecp_curves\.c|ecp_curves_new\.c|ecdsa\.c)$")
endif()

# 2) X.509 + TLS（顶层 library）
file(GLOB MBEDTLS_TLS_SRCS "library/*.c")

# 3) 后量子密码（可选，默认关）
if(MBEDTLS_ENABLE_PQCP)
    # PQCP 的旧 PSA 驱动已移除；ML-DSA 由 XCryptographic 的可选实现负责。
    set(MBEDTLS_PQCP_SRCS)
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
if(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_HASH=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_MD)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_MD=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_HASH)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_HASH=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_CIPHER)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_CIPHER=1)
endif()

if(MBEDTLS_USE_XCRYPTOGRAPHIC_MAC)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_MAC=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_CTR=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_ECC=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_XTS=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_HMAC_DRBG)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_HMAC_DRBG=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR_DRBG)
    # This changes the public CTR-DRBG context layout, so consumers must see
    # the same definition as the library itself.
    target_compile_definitions(mbedtls PUBLIC MBEDTLS_CTR_DRBG_USE_PSA_CRYPTO=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_LMS)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_LMS=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_XOF=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_KDF)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_KDF=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_FFDH)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_FFDH=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_RSA)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_RSA=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE=1)
endif()
if(MBEDTLS_USE_XCRYPTOGRAPHIC_KEY_WRAP)
    target_compile_definitions(mbedtls PRIVATE MBEDTLS_USE_XCRYPTOGRAPHIC_KEY_WRAP=1)
endif()

# Windows(MSVC): mbedtls_platform_get_entropy() 调用 BCryptGenRandom（需 bcrypt.lib）。
# 已开启 MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG，TLS/PSA 运行时随机数走 XRandomGenerator；
# 但 platform_util.c 仍被编译并链接，故 Windows 上必须链接 bcrypt 解析符号。
# Linux 用 getrandom()/urandom，无需额外库。
if(WIN32)
    target_link_libraries(mbedtls PUBLIC bcrypt)
endif()
