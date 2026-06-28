/**
 * @file XHashFunc.h
 * @brief 高性能哈希函数集合。
 *
 * 此头文件定义了一系列常用的、经过实战检验的高性能哈希函数接口。
 * 所有64位哈希函数均返回 uint64_t 类型，确保在32位和64位平台上都能获得完整的哈希值。
 * 32位哈希函数返回 size_t 类型以保持兼容性。
 *
 * 这些哈希函数适用于各种场景，如哈希表、布隆过滤器、数据校验等，具有良好的分布性和速度。
 *
 * @note 本文件中的所有实现均为公共领域或在宽松许可证（如 MIT/BSD/Apache 2.0）下可用的算法。
 */

#ifndef XHASHFUNC_H
#define XHASHFUNC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= 经典哈希函数 ========================= */

/**
 * @brief MurmurHash3 (32-bit variant) 哈希函数。
 * Austin Appleby 开发的非加密哈希函数，速度快，分布均匀。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 通用哈希表、游戏开发、缓存系统
 * @优点: 极快的速度，优秀的雪崩效应，良好的分布性，对各种输入长度都表现良好
 * @缺点: 不是密码学安全的，存在已知的碰撞攻击（但对一般应用无影响）
 */
size_t XHash_murmur3_32(const void* key, size_t len);

/**
 * @brief FNV-1a (64-bit variant) 哈希函数。
 * Fowler–Noll–Vo 哈希的改进版本，实现简单，速度快。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 编译器、解释器、字符串哈希、短键值哈希
 * @优点: 实现极其简单，内存占用极小，代码可读性好，速度快
 * @缺点: 对于某些特定模式的数据分布性不佳，不适合长字符串或二进制数据
 */
uint64_t XHash_fnv1a_64(const void* key, size_t len);

/**
 * @brief DJB2 (Bernstein) 哈希函数。
 * Daniel J. Bernstein 开发的简单快速字符串哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 简单字符串哈希、教学示例、资源受限环境
 * @优点: 代码极其简洁（仅几行），计算速度快，内存占用为零
 * @缺点: 分布性一般，容易产生碰撞，不适合高质量哈希需求
 */
size_t XHash_djb2(const void* key, size_t len);

/**
 * @brief Pearson 哈希函数。
 * 使用256字节查找表的经典算法，适合短字符串。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 8位微控制器、嵌入式系统、短字符串（< 256字节）
 * @优点: 内存友好（仅256字节表），计算简单，适合硬件实现
 * @缺点: 需要预计算查找表，对长字符串效率低，分布性有限
 */
size_t XHash_pearson(const void* key, size_t len);

/**
 * @brief Jenkins Lookup3 哈希函数。
 * Bob Jenkins 开发的经典高质量哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 通用高质量哈希需求、需要可靠性的生产环境
 * @优点: 质量优秀，雪崩效应好，经过多年实战检验，可靠性高
 * @缺点: 速度比现代哈希函数慢，代码相对复杂
 */
size_t XHash_lookup3(const void* key, size_t len);

/* ========================= Google 系列 ========================= */

/**
 * @brief CityHash64 哈希函数。
 * Google 开发的高速哈希函数，擅长处理长字符串。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 大数据处理、数据库索引、长字符串哈希、Google生态系统
 * @优点: 对长字符串优化极佳，速度非常快，64位输出质量好
 * @缺点: 存在已知的安全漏洞（不适合安全敏感场景），对短字符串优势不明显
 */
uint64_t XHash_cityhash64(const void* key, size_t len);

/**
 * @brief FarmHash64 哈希函数。
 * Google 开发的 CityHash 继任者，更好的分布性和速度。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: Google 生态系统、现代高性能应用、替代 CityHash
 * @优点: 比 CityHash 更优的分布性，修复了安全问题，速度相当或更好
 * @缺点: 实现相对复杂，依赖较多的平台特定优化
 */
uint64_t XHash_farmhash64(const void* key, size_t len);

/**
 * @brief HighwayHash64 哈希函数。
 * Google 开发的强哈希函数，利用 SIMD 指令集加速。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 安全敏感应用、需要抗碰撞的场景、高性能计算
 * @优点: 密码学强度（接近MAC），SIMD 加速，速度极快（支持 AVX2/NEON），安全性高
 * @缺点: 需要现代CPU支持SIMD指令，在老平台上回退到较慢的实现
 */
uint64_t XHash_highwayhash64(const void* key, size_t len);

/* ========================= 现代高性能哈希 ========================= */

/**
 * @brief xxHash64 哈希函数。
 * Yann Collet (LZ4作者) 开发的极快非加密哈希算法。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 游戏引擎、实时系统、压缩算法、通用高性能场景
 * @优点: 目前最快的哈希之一，质量优秀，跨平台一致性好，广泛采用
 * @缺点: 相对较新（虽然已很成熟），在某些极端输入下可能不如专门优化的算法
 */
uint64_t XHash_xxhash64(const void* key, size_t len);

/**
 * @brief Wyhash 哈希函数。
 * 超简洁的高质量哈希函数，代码极小但性能优秀。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 嵌入式系统、代码大小敏感的应用、通用高性能需求
 * @优点: 代码量极小 (< 20 行核心逻辑)，速度极快，质量优秀，无依赖
 * @缺点: 相对较新，社区采用度不如 xxHash 广泛
 */
uint64_t XHash_wyhash(const void* key, size_t len);

/**
 * @brief t1ha2 哈希函数。
 * Fast Positive Hash 的推荐版本，平衡了质量和性能。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 数据库系统、网络协议、需要稳定性能的生产环境
 * @优点: 在各种输入长度下表现稳定，质量与速度平衡极佳，经过严格测试
 * @缺点: 实现相对复杂，代码量较大
 */
uint64_t XHash_t1ha2(const void* key, size_t len);

/**
 * @brief SpookyHash64 哈希函数。
 * Bob Jenkins 开发的高质量哈希函数，优秀的雪崩效应。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 需要强雪崩效应的应用、高质量哈希需求、Bob Jenkins 粉丝
 * @优点: 雪崩效应极佳，分布均匀，速度良好，由哈希算法权威开发
 * @缺点: 代码相对复杂，性能略逊于最现代的算法如 xxHash
 */
uint64_t XHash_spookyhash64(const void* key, size_t len);

/* ========================= 安全哈希函数 ========================= */

/**
 * @brief SipHash-2-4 哈希函数。
 * 密码学安全的哈希函数，抗碰撞攻击，防哈希DoS。
 *
 * @param in 待哈希数据的指针
 * @param inlen 数据的字节长度
 * @param k0 SipHash 128位密钥的低64位部分
 * @param k1 SipHash 128位密钥的高64位部分
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 网络服务、防止哈希DoS攻击、安全敏感应用、Rust/Python 默认哈希
 * @优点: 密码学安全，抗碰撞，密钥化设计，防时序攻击，标准化程度高
 * @缺点: 比非加密哈希慢很多（约5-10倍），不适合纯性能场景
 *
 * @note SipHash 是密钥化哈希函数，需要128位密钥（由k0和k1组成）。
 *       在生产环境中，密钥应该使用密码学安全的随机数生成器生成，
 *       并且每次程序启动时都应该使用不同的随机密钥以防止攻击。
 */
uint64_t XHash_siphash24(const uint8_t* in, size_t inlen, uint64_t k0, uint64_t k1);


/* ========================= 其他优秀哈希函数 ========================= */

/**
 * @brief MetroHash64 哈希函数。
 * Intel 开发的高质量哈希函数，优秀的分布性。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (size_t)
 *
 * @推荐用途: Intel 平台优化、高质量哈希需求、数据库系统
 * @优点: 优秀的分布性，Intel 平台优化，质量稳定
 * @缺点: 社区采用度不如主流算法，相对较新
 */
uint64_t XHash_metrohash64(const void* key, size_t len);

/**
 * @brief MumHash 哈希函数。
 * Vsevolod Solovyov 开发的高质量哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (size_t)
 *
 * @推荐用途: 高质量哈希需求、替代 SpookyHash
 * @优点: 质量优秀，速度良好，现代设计
 * @缺点: 相对小众，文档和社区支持有限
 */
uint64_t XHash_mumhash(const void* key, size_t len);

/**
 * @brief FastHash64 哈希函数。
 * 简单快速的64位哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (size_t)
 *
 * @推荐用途: 简单快速的64位哈希需求、教学示例
 * @优点: 实现简单，速度较快，64位输出
 * @缺点: 质量一般，分布性不如专业算法
 */
uint64_t XHash_fasthash64(const void* key, size_t len);

/**
 * @brief Thomas Wang 64-bit 哈希函数。
 * 经典的整数哈希函数，优秀的位分布。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (size_t)
 *
 * @推荐用途: 整数键哈希、哈希表索引计算
 * @优点: 对整数输入优化极佳，位分布优秀，速度极快
 * @缺点: 主要针对整数设计，对字符串等复杂数据效果一般
 */
uint64_t XHash_thomaswang64(const void* key, size_t len);

/**
 * @brief OneAtATime 哈希函数。
 * Bob Jenkins 开发的简单哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 简单哈希需求、教学、轻量级应用
 * @优点: 代码简单，内存占用小，易于理解和实现
 * @缺点: 性能和质量都一般，不适合高性能或高质量要求
 */
size_t XHash_oneatatime(const void* key, size_t len);

/**
 * @brief SuperFastHash 哈希函数。
 * Paul Hsieh 开发的快速哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 快速字符串哈希、游戏开发
 * @优点: 速度很快，特别是对字符串，实现相对简单
 * @缺点: 存在已知的质量问题（某些输入分布不佳），不是64位
 */
size_t XHash_superfasthash(const void* key, size_t len);

/**
 * @brief ELFHash 哈希函数。
 * Unix ELF 格式使用的哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: Unix 系统编程、符号表哈希
 * @优点: 在 Unix 系统中有历史意义，对 C 标识符效果好
 * @缺点: 现代应用中很少使用，性能和质量都一般
 */
size_t XHash_elfhash(const void* key, size_t len);

/**
 * @brief APHash 哈希函数。
 * Arash Partow 开发的哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 字符串哈希、通用哈希需求
 * @优点: 分布性相对较好，实现简单
 * @缺点: 性能一般，不如现代算法
 */
size_t XHash_aphash(const void* key, size_t len);

/**
 * @brief JSHash 哈希函数。
 * Justin Sobel 开发的哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 字符串哈希、简单应用
 * @优点: 实现简单，代码清晰
 * @缺点: 性能和质量都一般，现代应用中较少使用
 */
size_t XHash_jshash(const void* key, size_t len);

/**
 * @brief RSHash 哈希函数。
 * Robert Sedgewick 开发的哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 教学、算法研究、简单应用
 * @优点: 由著名计算机科学家开发，理论基础好
 * @缺点: 实际性能一般，现代应用中较少使用
 */
size_t XHash_rshash(const void* key, size_t len);

/**
 * @brief PJWHash 哈希函数。
 * Peter J. Weinberger 开发的经典哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 编译器、链接器、经典系统软件
 * @优点: 历史悠久，经过时间检验，在编译器中有广泛应用
 * @缺点: 32位限制，现代应用中性能不如新算法
 */
size_t XHash_pjwhash(const void* key, size_t len);

/**
 * @brief BKDRHash 哈希函数。
 * 常用的字符串哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 中文字符串哈希、简单字符串处理
 * @优点: 对中文字符处理较好，实现简单
 * @缺点: 性能一般，主要在中文社区使用
 */
size_t XHash_bkdrhash(const void* key, size_t len);

/**
 * @brief SDBMHash 哈希函数。
 * SDBM 数据库使用的哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 32位哈希值 (size_t)
 *
 * @推荐用途: 数据库系统、简单哈希需求
 * @优点: 在 SDBM 数据库中经过长期使用验证
 * @缺点: 性能和质量都一般，现代应用中较少使用
 */
size_t XHash_sdbmhash(const void* key, size_t len);

#ifdef __cplusplus
}
#endif

#endif // XHASHFUNC_H