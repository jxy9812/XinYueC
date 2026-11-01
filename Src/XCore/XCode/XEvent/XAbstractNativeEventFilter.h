/**
 * @file       XAbstractNativeEventFilter.h
 * @brief      原生事件过滤器抽象接口公开 API。
 * @details    对齐 Qt QAbstractNativeEventFilter；子类实现 nativeEventFilter
 *             以拦截原生平台事件。实现只依赖 XinYueC 抽象层，禁止直接调用
 *             Win32、POSIX、Qt 或其他平台 API。
 */
#ifndef XABSTRACTNATIVEEVENTFILTER_H
#define XABSTRACTNATIVEEVENTFILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XByteArray.h"
#include "XClass.h"
#include <stdint.h>
#include <stdbool.h>

// =============== 虚函数表枚举 ==================
XCLASS_DEFINE_BEGING(XAbstractNativeEventFilter)
XCLASS_DEFINE_ENUM(XAbstractNativeEventFilter, NativeEventFilter) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_END(XAbstractNativeEventFilter)

// =============== 结构体 =======================
/**
 * @brief 原生事件过滤器抽象基类（对标 QAbstractNativeEventFilter）
 *
 * 注意：此类型不继承 XObject，仅为接口。
 */
typedef struct XAbstractNativeEventFilter 
{
    XClass m_class; ///< 第一个成员（虚函数表），由 XClass 管理，禁止手工修改
} XAbstractNativeEventFilter;

// =============== 构造/析构 ====================
XVtable* XAbstractNativeEventFilter_class_init(void);
void XAbstractNativeEventFilter_init(XAbstractNativeEventFilter* self);

// =============== 虚函数多态入口 ===============
/**
 * @brief 调用 nativeEventFilter 虚函数，把原生平台事件分发给过滤器。
 * @param self 过滤器自身指针；传入 NULL 时函数直接返回 false，不执行任何操作。
 * @param eventType 事件类型字节串（如 "windows_generic_MSG"）；只借用，函数不取得所有权。
 * @param message 原生平台消息指针（如 MSG* / XEvent*）；只透传给虚函数，不校验、不释放。
 * @param result 输出参数，由调用方提供存储空间；可为 NULL；虚函数可写入处理结果，未写入时保持原值。
 * @return true 表示事件已被过滤器处理，调用方应停止继续传递；false 表示未处理（self 为 NULL 或
 *         虚函数默认实现返回 false），事件继续按默认流程传递。
 */
bool XAbstractNativeEventFilter_nativeEventFilter_base(
        XAbstractNativeEventFilter* self,
        const XByteArray* eventType,
        void* message,
        int64_t* result);

#ifdef __cplusplus
}
#endif
#endif // XABSTRACTNATIVEEVENTFILTER_H