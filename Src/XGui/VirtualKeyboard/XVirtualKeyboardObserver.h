/**
 * @file       XVirtualKeyboardObserver.h
 * @brief      XVirtualKeyboardObserver 键盘布局观察器公开 API（对标 Qt
 *             6.8 QVirtualKeyboardObserver，qvirtualkeyboardobserver.h
 *             :16-35）。
 * @details    create_ex 公开（Qt 构造即公开）；layout() 返回新建
 *             XVariant（当前键盘布局描述符）；layoutChanged 信号；
 *             invalidateLayout 由默认面板在布局/模式切换时经保护头调
 *             用。偏离标注：Qt layout() 返回 QML 布局对象 JSON，本版
 *             为 XVariant 描述符（XVariantType_String，格式
 *             "<layoutType>/<locale>/<inputMode>"；无 QML 层）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDOBSERVER_H
#define XVIRTUALKEYBOARDOBSERVER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"
#include "XVariant.h"

#if XVIRTUALKEYBOARD_ON

/** @brief 声明 XVirtualKeyboardObserver 虚函数枚举：继承 XObject（无
 *         新增槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardObserver)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardObserver, XObject)

/**
 * @brief      键盘布局观察器对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存布局描述符缓存。
 */
typedef struct XVirtualKeyboardObserver
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardObserver;

/** @brief 初始化类虚函数表并返回共享表指针。 */
XVtable* XVirtualKeyboardObserver_class_init(void);

/**
 * @brief      进程单例（InputContext.keyboardObserver 访问面返回该单
 *             例；Qt 为可实例化对象——本实现同样公开 create_ex，进程
 *             级通知面收敛单例）。
 * @return     单例借用指针；不得释放。
 */
XVirtualKeyboardObserver* XVirtualKeyboardObserver_instance(void);

/**
 * @brief      使用默认内存类型创建独立观察器（对标 Qt 公开构造）。
 * @return     新对象指针；失败返回 NULL。
 */
#define XVirtualKeyboardObserver_create() \
    XVirtualKeyboardObserver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型创建独立观察器。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XVirtualKeyboardObserver* XVirtualKeyboardObserver_create_ex(
        XMemoryType memory);


/**
 * @brief      返回当前键盘布局描述符（对标 layout()）。
 * @details    新建 XVariant*（XVariantType_String，UTF-8 描述符
 *             "<layoutType>/<locale>/<inputMode>"，如
 *             "main/zh_CN/3"；layoutType ∈ main/symbols/digits/
 *             numbers/dialpad）。调用方用 XClassDelete 释放。
 * @param      self 观察器借用指针；NULL 返回 NULL。
 * @return     新建 XVariant*；调用方释放。
 */
XVariant* XVirtualKeyboardObserver_layout(
        const XVirtualKeyboardObserver* self);

/** @brief layoutChanged() 信号标识（布局描述符变化）。 */
void* XVirtualKeyboardObserver_layoutChanged_signal(
        XVirtualKeyboardObserver* self);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDOBSERVER_H */
