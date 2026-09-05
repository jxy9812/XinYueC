/**
 * @file       XAbstractButton_Protected.h
 * @brief      XAbstractButton 保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明对标 Qt 6.8 QAbstractButton protected API 的
 *             保护回调类型、默认钩子与内部类型识别入口；虚函数表槽位
 *             枚举按项目惯例位于公共头文件 XAbstractButton.h。普通应用
 *             代码不应直接包含或调用本文件中的接口；具体按钮子类和
 *             XAbstractButton.c 必须显式包含本文件，不能依赖公共头文件
 *             的间接声明。
 *             本文件不依赖 Win32、POSIX、Qt 或其他平台 API，按钮事件
 *             由 XWidget 事件体系处理。
 * @note       本文件依赖 XAbstractButton.h；模块总开关
 *             XABSTRACTBUTTON_ON=0 时，以下保护接口全部裁剪。
 * @author     XinYueC 团队
 */
#ifndef XABSTRACTBUTTON_PROTECTED_H
#define XABSTRACTBUTTON_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractButton.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON

/* ==================== 保护回调类型（对标 QAbstractButton，槽位枚举见 XAbstractButton.h） ==================== */

/**
 * @brief      选中状态已由外部设置后的保护回调类型。
 * @details    具体按钮类可在虚函数表中重载该回调，用于同步自定义选中
 *             状态；回调不负责释放 self，也不取得对象所有权。
 * @param      self 被调用的按钮对象；由框架借用，回调期间不可为 NULL。
 * @return     无返回值。
 */
typedef void (*XAbstractButtonCheckStateSetSlot)(XAbstractButton* self);

/**
 * @brief      计算下一个选中状态的保护回调类型。
 * @details    具体按钮类可重载该回调实现三态或其他自定义状态机；普通
 *             二态按钮使用基类默认的反转逻辑。
 * @param      self 被调用的按钮对象；由框架借用，回调期间不可为 NULL。
 * @return     无返回值。
 */
typedef void (*XAbstractButtonNextCheckStateSlot)(XAbstractButton* self);

/**
 * @brief      判断局部坐标是否命中按钮的保护回调类型。
 * @details    坐标使用按钮自己的局部坐标系；具体按钮类可将矩形命中
 *             改为圆角、图标或其他形状的命中规则。
 * @param      self 被查询的按钮对象；由框架借用，回调期间不可为 NULL。
 * @param      pos 待测试的局部坐标；由框架借用，不会被修改。
 * @return     坐标落在可点击区域内返回 true，否则返回 false。
 */
typedef bool (*XAbstractButtonHitButtonSlot)(const XAbstractButton* self,
                                             const XPoint* pos);

/**
 * @brief      可视内容变更后的保护回调类型。
 * @details    具体按钮类可在虚函数表中重载该回调，用于在文本/图标等
 *             内容变化后刷新自己的尺寸提示或外观缓存；回调不负责释放
 *             self，也不取得对象所有权。
 * @param      self 被调用的按钮对象；由框架借用，回调期间不可为 NULL。
 * @return     无返回值。
 */
typedef void (*XAbstractButtonContentChangedSlot)(XAbstractButton* self);

/* ==================== 派生类登记与类型判断 ==================== */

/**
 * @brief      登记一个具体按钮类的虚函数表。
 * @details    XAbstractButton_isInstance 使用登记表识别 XAbstractButton
 *             派生对象；具体按钮类应在自身 class_init 完成虚表后调用。
 *             vtable 只保存借用指针，不复制也不释放虚表。
 * @param      vtable 已初始化的派生类虚函数表；可为 NULL，NULL 时不登记。
 * @return     无返回值；无效虚表不会改变已有登记。
 * @note       虚表应具有静态或至少覆盖所有对象使用期的生命周期，并在
 *             GUI 线程完成登记。
 */
void XAbstractButton_registerClass(XVtable* vtable);

/**
 * @brief      判断对象是否为 XAbstractButton 或其已登记的派生类实例。
 * @param      object 待判断对象的借用指针；可为 NULL。
 * @return     object 非 NULL 且其虚表属于 XAbstractButton 体系时返回 true；
 *             NULL、未初始化对象或其他 XObject 返回 false。
 */
bool XAbstractButton_isInstance(const XObject* object);

/* ==================== 保护虚函数与分派入口（对标 QAbstractButton protected API） ==================== */

/**
 * @brief      选中状态已由公共 API 设置后的默认保护钩子。
 * @details    基类默认实现不执行操作；具体按钮可重载该钩子以同步复选框
 *             或单选按钮的附加状态。该函数是默认实现，不负责虚表分派。
 * @param      self 已初始化的按钮对象；由调用方借用，不可为 NULL。
 * @return     无返回值。
 */
void XAbstractButton_checkStateSet(XAbstractButton* self);

/**
 * @brief      通过当前按钮虚表分派 checkStateSet 保护钩子。
 * @details    该入口只查询和调用虚表，不包含业务逻辑；子类重载后若需要
 *             基类行为，应使用 XClass_Parent 访问父类槽位，不能递归调用
 *             自身的 _base 入口。
 * @param      self 已初始化的按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；没有有效虚表或槽位时不执行操作。
 */
void XAbstractButton_checkStateSet_base(XAbstractButton* self);

/**
 * @brief      计算下一个选中状态的默认保护钩子。
 * @details    基类对普通二态可选中按钮执行 checked 反转；不可选中按钮
 *             不改变状态。三态或单选按钮可在派生类中重载。
 * @param      self 已初始化的按钮对象；由调用方借用，不可为 NULL。
 * @return     无返回值。
 */
void XAbstractButton_nextCheckState(XAbstractButton* self);

/**
 * @brief      通过当前按钮虚表分派 nextCheckState 保护钩子。
 * @details    该入口只查询和调用虚表，不包含业务逻辑；子类重载后若需要
 *             基类行为，应使用 XClass_Parent 访问父类槽位，不能递归调用
 *             自身的 _base 入口。
 * @param      self 已初始化的按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；没有有效虚表或槽位时不执行操作。
 */
void XAbstractButton_nextCheckState_base(XAbstractButton* self);

/**
 * @brief      判断局部坐标是否命中按钮的默认保护钩子。
 * @details    默认把整个 XWidget 局部矩形视为可点击区域；具体按钮可
 *             重载以实现圆角、图标或自定义形状命中。
 * @param      self 按钮对象的借用指针；可为 NULL，NULL 时返回 false。
 * @param      pos 待测试的局部坐标借用指针；可为 NULL，NULL 时返回 false。
 * @return     坐标位于按钮局部矩形内返回 true，否则返回 false。
 */
bool XAbstractButton_hitButton(const XAbstractButton* self,
                               const XPoint* pos);

/**
 * @brief      通过当前按钮虚表分派 hitButton 保护钩子。
 * @details    该入口只查询和调用虚表，不包含业务逻辑；子类重载后若需要
 *             基类行为，应使用 XClass_Parent 访问父类槽位，不能递归调用
 *             自身的 _base 入口。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @param      pos 待测试的局部坐标借用指针；可为 NULL。
 * @return     当前虚表命中钩子返回的结果；self、pos、虚表或槽位无效时
 *             返回 false。
 */
bool XAbstractButton_hitButton_base(const XAbstractButton* self,
                                    const XPoint* pos);

/**
 * @brief      文本/图标等可视内容变更后的默认保护钩子。
 * @details    基类默认实现不执行操作；具体按钮可重载该钩子以刷新自己的
 *             尺寸提示或外观缓存（例如按钮文本/图标变化后重算 sizeHint
 *             存储位）。基类会在 setText/setIcon/setIconSize 等公共入口
 *             内容真正变化后、刷新几何之前分派该钩子。
 * @param      self 已初始化的按钮对象；由调用方借用，不可为 NULL。
 * @return     无返回值。
 */
void XAbstractButton_contentChanged(XAbstractButton* self);

/**
 * @brief      通过当前按钮虚表分派 contentChanged 保护钩子。
 * @details    该入口只查询和调用虚表，不包含业务逻辑；子类重载后若需要
 *             基类行为，应使用 XClass_Parent 访问父类槽位，不能递归调用
 *             自身的 _base 入口。
 * @param      self 已初始化的按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；没有有效虚表或槽位时不执行操作。
 */
void XAbstractButton_contentChanged_base(XAbstractButton* self);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON */

#ifdef __cplusplus
}
#endif

#endif /* XABSTRACTBUTTON_PROTECTED_H */
