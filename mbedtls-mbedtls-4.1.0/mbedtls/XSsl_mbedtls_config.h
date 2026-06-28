/**
 * @file XSsl_mbedtls_config.h
 * @brief mbedTLS配置文件 - 针对嵌入式系统优化
 * 
 * 此配置文件针对资源受限的嵌入式系统进行了优化：
 * - 减少内存占用
 * - 禁用不必要的功能
 * - 支持硬件加速
 * 
 * 使用方式：
 * 1. 在编译时定义 MBEDTLS_CONFIG_FILE="XSsl_mbedtls_config.h"
 * 2. 或者直接修改 mbedTLS 的 mbedtls_config.h
 */

#ifndef XSSL_MBEDTLS_CONFIG_H
#define XSSL_MBEDTLS_CONFIG_H

/* =========================================================================
 * 一、系统与平台配置
 * ========================================================================= */

/**
 * @brief 平台配置
 * 根据目标平台选择合适的配置
 */

/* 支持的平台选择（取消注释以启用） */
// #define XSSL_PLATFORM_FREERTOS      /* FreeRTOS */
// #define XSSL_PLATFORM_RTTHREAD      /* RT-Thread */
// #define XSSL_PLATFORM_ZEPHYR        /* Zephyr */
// #define XSSL_PLATFORM_BAREMETAL     /* 裸机 */
// #define XSSL_PLATFORM_LINUX         /* Linux/POSIX */
// #define XSSL_PLATFORM_WINDOWS       /* Windows */

/* =========================================================================
 * 二、mbedTLS核心配置
 * ========================================================================= */

/**
 * @brief mbedTLS版本检查
 */
#include <mbedtls/version.h>

/**
 * @brief 平台抽象层
 */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS

/**
 * @brief 自定义内存分配函数
 * 使用XinYueC的内存管理
 */
#define MBEDTLS_PLATFORM_STD_CALLOC  XMalloc_System
#define MBEDTLS_PLATFORM_STD_FREE    XFree_System

/**
 * @brief 禁用标准库函数（嵌入式优化）
 */
// #define MBEDTLS_NO_PLATFORM_ENTROPY  /* 如果没有硬件随机源，注释掉 */

/* =========================================================================
 * 三、熵源与随机数配置
 * ========================================================================= */

#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/**
 * @brief 硬件随机数支持
 * 如果MCU有硬件TRNG，启用此选项
 */
// #define MBEDTLS_ENTROPY_HARDWARE_ALT

/**
 * @brief 熵源配置
 */
#define MBEDTLS_ENTROPY_MAX_SOURCES 2  /* 减少熵源数量 */

/* =========================================================================
 * 四、SSL/TLS协议配置
 * ========================================================================= */

#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C           /* 客户端支持 */
// #define MBEDTLS_SSL_SRV_C         /* 服务端支持（按需启用） */
#define MBEDTLS_SSL_DTLS            /* DTLS支持 */

/**
 * @brief TLS版本配置
 * 建议只启用TLS 1.2，禁用旧版本以节省空间
 */
#define MBEDTLS_SSL_PROTO_TLS1_2
// #define MBEDTLS_SSL_PROTO_TLS1_3  /* TLS 1.3（需要mbedTLS 3.x） */
// #define MBEDTLS_SSL_PROTO_TLS1    /* TLS 1.0（不安全，禁用） */
// #define MBEDTLS_SSL_PROTO_TLS1_1  /* TLS 1.1（不安全，禁用） */
// #define MBEDTLS_SSL_PROTO_DTLS    /* DTLS（按需启用） */

/**
 * @brief SSL选项
 */
#define MBEDTLS_SSL_MAX_CONTENT_LEN  16384  /* 最大TLS记录长度 */
// #define MBEDTLS_SSL_IN_CONTENT_LEN  4096  /* 减小输入缓冲区 */
// #define MBEDTLS_SSL_OUT_CONTENT_LEN 4096  /* 减小输出缓冲区 */

/* =========================================================================
 * 五、加密算法配置
 * ========================================================================= */

/**
 * @brief 对称加密算法
 * 只启用常用的安全算法
 */

/* AES - 最常用的对称加密 */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES      /* 使用ROM中的AES表（节省RAM） */
// #define MBEDTLS_AES_FEWER_TABLES  /* 进一步减少表大小 */

/* GCM模式 - TLS 1.2推荐 */
#define MBEDTLS_GCM_C

/* CCM模式 */
#define MBEDTLS_CCM_C

/* ChaCha20-Poly1305 - 现代高效算法 */
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C

/* CBC模式（兼容旧系统） */
#define MBEDTLS_CIPHER_MODE_CBC

/* 流加密（不推荐，按需启用） */
// #define MBEDTLS_ARC4_C
// #define MBEDTLS_BLOWFISH_C
// #define MBEDTLS_CAMELLIA_C
// #define MBEDTLS_DES_C

/**
 * @brief 哈希算法
 */
#define MBEDTLS_SHA256_C            /* SHA-256（必需） */
#define MBEDTLS_SHA224_C            /* SHA-224 */
// #define MBEDTLS_SHA384_C          /* SHA-384（按需启用） */
// #define MBEDTLS_SHA512_C          /* SHA-512（按需启用） */
// #define MBEDTLS_SHA1_C            /* SHA-1（不安全，禁用） */
// #define MBEDTLS_MD5_C             /* MD5（不安全，禁用） */
// #define MBEDTLS_RIPEMD160_C

#define MBEDTLS_MD_C                /* MD抽象层 */

/**
 * @brief HMAC
 */
#define MBEDTLS_HKDF_C              /* HKDF（TLS 1.3需要） */

/* =========================================================================
 * 六、非对称加密配置
 * ========================================================================= */

/**
 * @brief RSA
 */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15           /* PKCS#1 v1.5 */
#define MBEDTLS_PKCS1_V21           /* PKCS#1 v2.1 (RSA-PSS) */

/**
 * @brief ECC - 椭圆曲线加密
 * 推荐用于嵌入式系统（更小的密钥，相同的安全性）
 */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C              /* ECDH密钥交换 */
#define MBEDTLS_ECDSA_C             /* ECDSA签名 */
#define MBEDTLS_ECDH_LEGACY_CONTEXT /* 兼容旧API */

/**
 * @brief 椭圆曲线选择
 * 只启用常用的安全曲线
 */
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED  /* P-256（最常用） */
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED  /* P-384 */
// #define MBEDTLS_ECP_DP_SECP521R1_ENABLED  /* P-521 */
// #define MBEDTLS_ECP_DP_SECP192R1_ENABLED  /* P-192（不推荐） */
// #define MBEDTLS_ECP_DP_SECP224R1_ENABLED  /* P-224 */
// #define MBEDTLS_ECP_DP_SECP192K1_ENABLED
// #define MBEDTLS_ECP_DP_SECP224K1_ENABLED
// #define MBEDTLS_ECP_DP_SECP256K1_ENABLED  /* secp256k1（比特币用） */
// #define MBEDTLS_ECP_DP_BP256R1_ENABLED
// #define MBEDTLS_ECP_DP_BP384R1_ENABLED
// #define MBEDTLS_ECP_DP_BP512R1_ENABLED
// #define MBEDTLS_ECP_DP_CURVE25519_ENABLED  /* Curve25519 */
// #define MBEDTLS_ECP_DP_CURVE448_ENABLED

/**
 * @brief DH密钥交换
 */
// #define MBEDTLS_DHM_C             /* DHM（传统，可禁用以节省空间） */

/**
 * @brief 公钥抽象层
 */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C          /* 解析公钥 */
#define MBEDTLS_PK_WRITE_C          /* 写入公钥 */

/* =========================================================================
 * 七、证书与X.509配置
 * ========================================================================= */

#define MBEDTLS_X509_USE_C          /* X.509使用 */
#define MBEDTLS_X509_CRT_PARSE_C    /* 解析证书 */
// #define MBEDTLS_X509_CRL_PARSE_C  /* CRL解析（按需启用） */
// #define MBEDTLS_X509_CSR_PARSE_C  /* CSR解析 */
// #define MBEDTLS_X509_CREATE_C     /* 创建证书 */
// #define MBEDTLS_X509_CRT_WRITE_C  /* 写入证书 */
// #define MBEDTLS_X509_CSR_WRITE_C  /* 写入CSR */

/**
 * @brief 证书解析选项
 */
#define MBEDTLS_X509_MAX_INTERMEDIATE_CA  5  /* 中间CA最大数量 */
#define MBEDTLS_X509_MAX_FILE_PATH_LEN    256

/**
 * @brief 证书时间检查
 * 如果系统没有实时时钟，可以禁用
 */
#define MBEDTLS_X509_CHECK_TIME_VALIDITY
// #define MBEDTLS_X509_REMOVE_TIME_CHECKS  /* 禁用时间检查 */

/* =========================================================================
 * 八、密码套件配置
 * ========================================================================= */

/**
 * @brief 启用的密码套件
 * 只启用安全的现代密码套件
 */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED

/* RSA密钥交换（无前向保密，不推荐） */
// #define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED

/* PSK（预共享密钥） */
// #define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
// #define MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED

/* DHE（传统） */
// #define MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED

/* =========================================================================
 * 九、网络与IO配置
 * ========================================================================= */

/**
 * @brief 网络接口
 * 嵌入式系统通常使用自定义网络接口
 */
// #define MBEDTLS_NET_C             /* 标准socket（嵌入式禁用） */

/**
 * @brief 自定义IO回调
 */
#define MBEDTLS_SSL_EXPORT_KEYS    /* 导出密钥（用于自定义处理） */

/* =========================================================================
 * 十、调试与错误处理
 * ========================================================================= */

/**
 * @brief 调试支持
 */
#ifdef DEBUG
#define MBEDTLS_DEBUG_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_DEBUG_LEVEL  2     /* 调试级别 0-4 */
#else
// #define MBEDTLS_DEBUG_C
#define MBEDTLS_ERROR_C
#endif

/**
 * @brief 错误信息字符串
 * 禁用可节省约10KB空间
 */
// #define MBEDTLS_ERROR_STRERROR_DUMMY  /* 使用简化错误信息 */

/* =========================================================================
 * 十一、安全选项
 * ========================================================================= */

/**
 * @brief 安全加固选项
 */
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES
#define MBEDTLS_REMOVE_ARC4_CIPHERSUITES
#define MBEDTLS_REMOVE_3DES_CIPHERSUITES

/**
 * @brief 内存安全
 */
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C  /* 内存池分配器 */
#define MBEDTLS_MEMORY_ALIGN           4  /* 内存对齐 */

/**
 * @brief 时序攻击防护
 */
#define MBEDTLS_RSA_NO_CRT            /* 禁用CRT（减少时序攻击风险） */
#define MBEDTLS_SSL_ALL_ALERT_MESSAGES

/* =========================================================================
 * 十二、硬件加速配置
 * ========================================================================= */

/**
 * @brief 硬件加密加速
 * 如果MCU有硬件加密模块，启用相应选项
 */
// #define MBEDTLS_AES_ALT          /* 自定义AES实现 */
// #define MBEDTLS_SHA256_ALT       /* 自定义SHA256实现 */
// #define MBEDTLS_ECP_ALT          /* 自定义ECP实现 */

/**
 * @brief STM32硬件加密示例
 */
#ifdef XSSL_PLATFORM_STM32
    #define MBEDTLS_AES_ALT
    #define MBEDTLS_SHA256_ALT
    // 使用STM32 CRYP外设
#endif

/**
 * @brief ESP32硬件加密示例
 */
#ifdef XSSL_PLATFORM_ESP32
    // ESP-IDF已内置硬件加速支持
#endif

/* =========================================================================
 * 十三、内存优化配置
 * ========================================================================= */

/**
 * @brief 内存缓冲区大小
 * 根据可用RAM调整
 */
// #define MBEDTLS_MEMORY_BUFFER_ALLOC_SIZE  65536  /* 64KB内存池 */

/**
 * @brief SSL缓冲区大小
 */
#define MBEDTLS_SSL_IN_CONTENT_LEN   4096   /* 输入缓冲区 */
#define MBEDTLS_SSL_OUT_CONTENT_LEN  4096   /* 输出缓冲区 */
#define MBEDTLS_SSL_MAX_FRAG_LEN     4096   /* 最大片段长度 */

/**
 * @brief 减少内存占用
 */
#define MBEDTLS_MPI_MAX_SIZE         256    /* 大整数最大字节数 */
#define MBEDTLS_ECP_MAX_BITS         384    /* ECC最大位数 */

/* =========================================================================
 * 十四、功能裁剪
 * ========================================================================= */

/**
 * @brief 禁用不需要的功能
 */
// #define MBEDTLS_SELF_TEST_C      /* 自测试（生产环境禁用） */
// #define MBEDTLS_VERSION_C        /* 版本检查 */
// #define MBEDTLS_COMPATIBILITY_V2_1  /* 兼容旧API */

/* =========================================================================
 * 十五、FreeRTOS适配
 * ========================================================================= */

#ifdef XSSL_PLATFORM_FREERTOS
    #include "FreeRTOS.h"
    #include "semphr.h"
    
    /* 线程支持 */
    #define MBEDTLS_THREADING_C
    #define MBEDTLS_THREADING_ALT
    
    /* 互斥锁类型 */
    typedef SemaphoreHandle_t mbedtls_threading_mutex_t;
#endif

/* =========================================================================
 * 十六、RT-Thread适配
 * ========================================================================= */

#ifdef XSSL_PLATFORM_RTTHREAD
    #include <rtthread.h>
    #include <rtdevice.h>
    
    #define MBEDTLS_THREADING_C
    #define MBEDTLS_THREADING_ALT
    
    typedef struct rt_mutex* mbedtls_threading_mutex_t;
#endif

/* =========================================================================
 * 十七、裸机适配
 * ========================================================================= */

#ifdef XSSL_PLATFORM_BAREMETAL
    /* 无线程支持 */
    // #define MBEDTLS_THREADING_C
    
    /* 使用简单定时器 */
    // #define MBEDTLS_TIMING_C
#endif

/* =========================================================================
 * 十八、包含mbedTLS默认配置
 * ========================================================================= */

/**
 * @brief 包含mbedTLS默认配置
 * 注意：此文件应在mbedTLS包含路径之前
 */
// #include "check_config.h"

#endif /* XSSL_MBEDTLS_CONFIG_H */