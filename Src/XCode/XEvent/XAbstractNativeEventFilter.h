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
    XClass m_class; ///< 虚函数表（必须由子类初始化）
} XAbstractNativeEventFilter;

// =============== 构造/析构 ====================
XVtable* XAbstractNativeEventFilter_class_init(void);
void XAbstractNativeEventFilter_init(XAbstractNativeEventFilter* self);
#define XAbstractNativeEventFilter_deinit_base    XClass_deinit_base
#define XAbstractNativeEventFilter_delete_base    XClass_delete_base

// =============== 虚函数多态入口 ===============
/**
 * @brief 调用 nativeEventFilter 虚函数
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