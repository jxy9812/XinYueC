/**
 * @file XRandomGenerator.h
 * @brief 随机数生成器（对齐Qt 6.8 QRandomGenerator）
 * 
 * QRandomGenerator提供高质量的随机数生成功能：
 * - 系统随机数生成器（/dev/urandom或Windows CryptoAPI）
 * - 伪随机数生成器
 * - 支持种子初始化和范围生成
 * 
 * 与Qt 6.8 QRandomGenerator API对齐
 */

#ifndef XRANDOMGENERATOR_H
#define XRANDOMGENERATOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============== 类型定义 =============== */

/**
 * @brief 随机数结果类型（对应Qt的result_type）
 */
typedef uint32_t XRandomGenerator_result_type;

/**
 * @brief 随机数生成器对象（不透明指针）
 */
typedef struct XRandomGenerator XRandomGenerator;

/* =============== 构造/析构函数 =============== */

/**
 * @brief 创建随机数生成器（使用默认种子1）
 * @return 生成器对象指针，失败返回NULL
 * @note 对应Qt: QRandomGenerator(quint32 seedValue = 1)
 */
XRandomGenerator* XRandomGenerator_create(void);

/**
 * @brief 使用种子创建随机数生成器
 * @param seedValue 种子值
 * @return 生成器对象指针，失败返回NULL
 * @note 对应Qt: QRandomGenerator(quint32 seedValue = 1)
 */
XRandomGenerator* XRandomGenerator_createWithSeed(uint32_t seedValue);

/**
 * @brief 使用种子数组创建随机数生成器
 * @param seedBuffer 种子数组
 * @param len 种子数量
 * @return 生成器对象指针，失败返回NULL
 * @note 对应Qt: QRandomGenerator(const quint32 *seedBuffer, qsizetype len)
 */
XRandomGenerator* XRandomGenerator_createWithSeeds(const uint32_t* seedBuffer, size_t len);

/**
 * @brief 使用种子范围创建随机数生成器
 * @param begin 种子数组起始指针
 * @param end 种子数组结束指针
 * @return 生成器对象指针，失败返回NULL
 * @note 对应Qt: QRandomGenerator(const quint32 *begin, const quint32 *end)
 */
XRandomGenerator* XRandomGenerator_createWithRange(const uint32_t* begin, const uint32_t* end);

/**
 * @brief 复制随机数生成器
 * @param other 源生成器
 * @return 新生成器对象指针，失败返回NULL
 * @note 对应Qt: QRandomGenerator(const QRandomGenerator &other)
 */
XRandomGenerator* XRandomGenerator_copy(const XRandomGenerator* other);

/**
 * @brief 删除随机数生成器
 * @param generator 生成器对象指针
 */
void XRandomGenerator_delete(XRandomGenerator* generator);

/* =============== 种子设置 =============== */

/**
 * @brief 重新设置种子
 * @param generator 生成器对象指针
 * @param seed 种子值（默认1）
 * @note 对应Qt: void seed(quint32 seed = 1)
 */
void XRandomGenerator_seed(XRandomGenerator* generator, uint32_t seed);

/**
 * @brief 使用种子数组重新设置种子
 * @param generator 生成器对象指针
 * @param seedBuffer 种子数组
 * @param len 种子数量
 */
void XRandomGenerator_seedWithArray(XRandomGenerator* generator, const uint32_t* seedBuffer, size_t len);

/* =============== 随机数生成 =============== */

/**
 * @brief 生成32位随机数
 * @param generator 生成器对象指针
 * @return 32位随机数
 * @note 对应Qt: quint32 generate()
 */
uint32_t XRandomGenerator_generate(XRandomGenerator* generator);

/**
 * @brief 生成64位随机数
 * @param generator 生成器对象指针
 * @return 64位随机数
 * @note 对应Qt: quint64 generate64()
 */
uint64_t XRandomGenerator_generate64(XRandomGenerator* generator);

/**
 * @brief 生成随机浮点数 [0, 1)
 * @param generator 生成器对象指针
 * @return 浮点数
 * @note 对应Qt: double generateDouble()
 */
double XRandomGenerator_generateDouble(XRandomGenerator* generator);

/**
 * @brief 填充缓冲区为随机数（32位）
 * @param generator 生成器对象指针
 * @param buffer 输出缓冲区
 * @param count 元素数量
 * @note 对应Qt: void fillRange(UInt *buffer, qsizetype count)
 */
void XRandomGenerator_fillRange32(XRandomGenerator* generator, uint32_t* buffer, size_t count);

/**
 * @brief 填充缓冲区为随机数（64位）
 * @param generator 生成器对象指针
 * @param buffer 输出缓冲区
 * @param count 元素数量
 */
void XRandomGenerator_fillRange64(XRandomGenerator* generator, uint64_t* buffer, size_t count);

/**
 * @brief 跳过指定数量的随机数
 * @param generator 生成器对象指针
 * @param z 跳过数量
 * @note 对应Qt: void discard(unsigned long long z)
 */
void XRandomGenerator_discard(XRandomGenerator* generator, unsigned long long z);

/**
 * @brief 获取下一个随机数（类似Qt的operator()）
 * @param generator 生成器对象指针
 * @return 随机数
 * @note 对应Qt: result_type operator()()
 */
XRandomGenerator_result_type XRandomGenerator_call(XRandomGenerator* generator);

/* =============== bounded() 函数族 - 范围随机数 =============== */

/**
 * @brief 生成 [0, highest) 范围内的随机数（32位无符号）
 * @param generator 生成器对象指针
 * @param highest 上界（不包含）
 * @return 随机数
 * @note 对应Qt: quint32 bounded(quint32 highest)
 */
uint32_t XRandomGenerator_boundedU32(XRandomGenerator* generator, uint32_t highest);

/**
 * @brief 生成 [lowest, highest) 范围内的随机数（32位无符号）
 * @param generator 生成器对象指针
 * @param lowest 下界（包含）
 * @param highest 上界（不包含）
 * @return 随机数
 * @note 对应Qt: quint32 bounded(quint32 lowest, quint32 highest)
 */
uint32_t XRandomGenerator_boundedU32Range(XRandomGenerator* generator, uint32_t lowest, uint32_t highest);

/**
 * @brief 生成 [0, highest) 范围内的随机数（64位无符号）
 * @param generator 生成器对象指针
 * @param highest 上界（不包含）
 * @return 随机数
 * @note 对应Qt: quint64 bounded(quint64 highest)
 */
uint64_t XRandomGenerator_boundedU64(XRandomGenerator* generator, uint64_t highest);

/**
 * @brief 生成 [lowest, highest) 范围内的随机数（64位无符号）
 * @param generator 生成器对象指针
 * @param lowest 下界（包含）
 * @param highest 上界（不包含）
 * @return 随机数
 * @note 对应Qt: quint64 bounded(quint64 lowest, quint64 highest)
 */
uint64_t XRandomGenerator_boundedU64Range(XRandomGenerator* generator, uint64_t lowest, uint64_t highest);

/**
 * @brief 生成 [0, highest) 范围内的随机数（32位有符号）
 * @param generator 生成器对象指针
 * @param highest 上界（不包含），必须为正数
 * @return 随机数
 * @note 对应Qt: int bounded(int highest)
 */
int32_t XRandomGenerator_boundedI32(XRandomGenerator* generator, int32_t highest);

/**
 * @brief 生成 [lowest, highest) 范围内的随机数（32位有符号）
 * @param generator 生成器对象指针
 * @param lowest 下界（包含）
 * @param highest 上界（不包含），必须大于lowest
 * @return 随机数
 * @note 对应Qt: int bounded(int lowest, int highest)
 */
int32_t XRandomGenerator_boundedI32Range(XRandomGenerator* generator, int32_t lowest, int32_t highest);

/**
 * @brief 生成 [0, highest) 范围内的随机数（64位有符号）
 * @param generator 生成器对象指针
 * @param highest 上界（不包含），必须为正数
 * @return 随机数
 * @note 对应Qt: qint64 bounded(qint64 highest)
 */
int64_t XRandomGenerator_boundedI64(XRandomGenerator* generator, int64_t highest);

/**
 * @brief 生成 [lowest, highest) 范围内的随机数（64位有符号）
 * @param generator 生成器对象指针
 * @param lowest 下界（包含）
 * @param highest 上界（不包含），必须大于lowest
 * @return 随机数
 * @note 对应Qt: qint64 bounded(qint64 lowest, qint64 highest)
 */
int64_t XRandomGenerator_boundedI64Range(XRandomGenerator* generator, int64_t lowest, int64_t highest);

/**
 * @brief 生成 [0, highest) 范围内的随机浮点数
 * @param generator 生成器对象指针
 * @param highest 上界（不包含）
 * @return 随机数
 * @note 对应Qt: double bounded(double highest)
 */
double XRandomGenerator_boundedDouble(XRandomGenerator* generator, double highest);

/* =============== 静态函数 =============== */

/**
 * @brief 获取最小可能值
 * @return 最小值（0）
 * @note 对应Qt: static constexpr result_type min()
 */
XRandomGenerator_result_type XRandomGenerator_min(void);

/**
 * @brief 获取最大可能值
 * @return 最大值（UINT32_MAX）
 * @note 对应Qt: static constexpr result_type max()
 */
XRandomGenerator_result_type XRandomGenerator_max(void);

/**
 * @brief 获取全局随机数生成器（线程安全）
 * @return 全局生成器指针（无需删除）
 * @note 对应Qt: static QRandomGenerator *global()
 */
XRandomGenerator* XRandomGenerator_global(void);

/**
 * @brief 获取系统密码学安全随机数生成器（线程安全）
 * @return 系统生成器指针（无需删除）
 * @note 对应Qt: static QRandomGenerator *system()
 */
XRandomGenerator* XRandomGenerator_system(void);

/**
 * @brief 创建一个安全种子的随机数生成器
 * @return 新生成器对象指针（需要调用者删除）
 * @note 对应Qt: static QRandomGenerator securelySeeded()
 */
XRandomGenerator* XRandomGenerator_securelySeeded(void);

/* =============== 便捷静态函数（使用全局生成器） =============== */

/**
 * @brief 生成32位随机数（使用全局生成器）
 * @return 随机数
 */
uint32_t XRandomGenerator_random(void);

/**
 * @brief 生成 [0, highest) 范围内的随机数（使用全局生成器）
 * @param highest 上界（不包含）
 * @return 随机数
 */
uint32_t XRandomGenerator_randomBounded(uint32_t highest);

/**
 * @brief 生成64位随机数（使用全局生成器）
 * @return 64位随机数
 */
uint64_t XRandomGenerator_random64(void);

/**
 * @brief 生成随机浮点数 [0, 1)（使用全局生成器）
 * @return 浮点数
 */
double XRandomGenerator_randomDouble(void);

/**
 * @brief 使用安全随机数填充缓冲区
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小（字节）
 * @return 成功返回true
 */
bool XRandomGenerator_fillSecure(void* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XRANDOMGENERATOR_H */