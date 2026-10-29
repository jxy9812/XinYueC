/**
 * @file       XIoChannel.h
 * @brief      工业通道抽象基类公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场的"测点/通道"概念）。
 *             XIoChannel 是 Industry/XChannel 通道接口族（XDigitalInput/
 *             XDigitalOutput/XAnalogInput/XAnalogOutput）的公共瘦基类：
 *             只承载通道名、数据有效标志、最近错误码与 errorOccurred 信号，
 *             不含任何传输语义。通道对象是"通道句柄"性质的对象：**禁止
 *             拷贝/移动语义**（不提供 create_copy/create_move，也不允许对
 *             通道对象使用 XClassCopy/XClassMove/memcpy），生命周期只有
 *             栈上 init + XClassDeinit 与堆上 create + XClassDelete 两种。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他
 *             平台 API；具体传输（GPIO/Modbus/CAN 等）由 Industry/XChannel
 *             下的适配器子类承载，设备类只依赖本族接口头文件。
 */
#ifndef XIOCHANNEL_H
#define XIOCHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/**
 * @brief 通道层通用错误码。
 * @details 适配器把平台/协议原生错误映射到本枚举；原生错误码可由适配器
 *          自行记录（如经日志输出）。0 恒表示无错误。
 */
typedef enum XIoChannelError {
    XIoChannelError_None = 0,        /**< 无错误。 */
    XIoChannelError_InvalidArgument, /**< 参数为 NULL 或非法。 */
    XIoChannelError_NotAttached,     /**< 通道尚未挂接底层传输（未绑定 GPIO/未 attach 主站等）。 */
    XIoChannelError_Busy,            /**< 底层传输忙碌，本次请求未被受理。 */
    XIoChannelError_Timeout,         /**< 底层传输应答超时。 */
    XIoChannelError_Unsupported,     /**< 当前适配器或后端不支持该能力。 */
    XIoChannelError_Hardware,        /**< 底层硬件/协议返回错误。 */
    XIoChannelError_Unknown          /**< 未分类错误。 */
} XIoChannelError;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XIoChannel)
XCLASS_DEFINE_EXTEND_END(XIoChannel, XObject)

/**
 * @brief XIoChannel 通道抽象瘦基类。
 * @details 四个通道接口（数字/模拟 × 输入/输出）的共同父类。只承载"身份 +
 *          质量 + 错误"三件公共状态与一个公共信号；传输行为全部由接口
 *          子类的虚槽与信号承载。成员禁止直接访问，一律经 API 操作。
 */
typedef struct XIoChannel {
    XObject m_class;          /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    const char* m_tag;        /**< 通道名（借用，不拥有；如 "DI0"、"温度AI1"）。生命周期由调用方保证，通常传入字符串字面量。可为 NULL。 */
    uint32_t m_valid : 1;     /**< 数据有效标志：1=通道数据可信（已挂接且通信正常），0=不可信（未挂接/通信失败）。 */
    uint32_t m_reserved : 31; /**< 保留位，必须为 0。 */
    int m_lastError;          /**< 最近一次错误码，取值见 XIoChannelError；0=无错误。仅供实现读取，设置经保护接口。 */
} XIoChannel;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XIoChannel 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XIoChannel_class_init(void);

/**
 * @brief 栈上初始化通道基类。
 * @param point 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；通道子类的 init 在设置自身虚表前调用本函数。
 */
void XIoChannel_init(XIoChannel* point);

/**
 * @brief 堆上创建通道基类对象。
 * @return 新通道对象；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete 释放。
 * @note 基类本身可独立实例化（测试/占位用途），但读写行为为空实现。
 */
XIoChannel* XIoChannel_create(void);

/* ==================== 功能函数 ==================== */

/**
 * @brief 设置通道名。
 * @param point 目标通道；传入 NULL 时函数不执行任何操作。
 * @param tag 通道名；借用语义，本函数不拷贝，调用方须保证其生命周期覆盖
 *            通道对象（推荐传字符串字面量）；传 NULL 表示清除通道名。
 * @return 无。设置不发射任何信号。
 */
void XIoChannel_setTag(XIoChannel* point, const char* tag);

/**
 * @brief 读取通道名。
 * @param point 目标通道；传入 NULL 返回 NULL。
 * @return 内部保存的通道名借用指针；禁止释放或修改。未设置时返回 NULL。
 */
const char* XIoChannel_tag_const(const XIoChannel* point);

/**
 * @brief 判断通道数据是否有效。
 * @param point 目标通道；传入 NULL 返回 false。
 * @return 通道已挂接且通信正常返回 true；未挂接、通信失败或参数非法返回 false。
 * @note 有效标志由适配器在挂接/通信成败时经保护接口维护，业务层只读。
 */
bool XIoChannel_isValid(const XIoChannel* point);

/**
 * @brief 读取通道最近一次错误码。
 * @param point 目标通道；传入 NULL 返回 XIoChannelError_InvalidArgument。
 * @return 最近一次错误码（XIoChannelError 枚举值）；无错误返回 XIoChannelError_None。
 */
int XIoChannel_lastError(const XIoChannel* point);

/* ==================== 信号 ==================== */

/**
 * @brief errorOccurred 信号：通道发生错误时发射。
 * @param point 发射信号的通道；经 XSignal() 宏取地址连接，禁止直调。
 * @param error 本次错误码（XIoChannelError 枚举值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 同一错误反复发生时适配器可重复发射；业务层按需做去重/限频。
 */
void* XIoChannel_errorOccurred_signal(XIoChannel* point, int error);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOCHANNEL_H */
