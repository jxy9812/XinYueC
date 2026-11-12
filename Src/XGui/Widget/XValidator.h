/**
 * @file       XValidator.h
 * @brief      XValidator 输入校验抽象基类（对标 Qt 6.8 QValidator）。
 * @details    对齐依据（Qt 6.8.3 原文）：
 *             - `QValidator : public QObject`，`enum State { Invalid,
 *               Intermediate, Acceptable }`（gui/util/qvalidator.h:23-34）；
 *             - `virtual State validate(QString &, int &) const = 0` 与
 *               `virtual void fixup(QString &) const`（qvalidator.h:40-41）；
 *             - 基类 `QValidator::fixup(QString &)` 为空实现
 *               （qvalidator.cpp:250「the default does nothing」）；
 *             - `QRegularExpressionValidator::validate` 只重写 validate、
 *               **不重写 fixup**（qvalidator.cpp:994-1013）。
 *
 *             本库的 C 适配：QString& 用「UTF-8 堆串 char**（输入/输出）」
 *             承载，int& 用 int* 承载；状态经**返回值**输出（对标 Qt 的
 *             `State validate(...)` 返回值，即任务书所述的 out_state）。
 *             `char**` 的所有权契约与 XLineControl 的既有钩子逐字相同
 *             （XLineControl.h:131-150）：入参指向 XStrdup/XMalloc_System
 *             族分配的 NUL 结尾堆串；槽可就地修改，或 XFree_System 释放旧
 *             串后以新的 XStrdup 串替换；返回后缓冲所有权归调用方。
 *
 *             **_base 契约（§497 条款 1-7）**：`XValidator_validate_base` /
 *             `XValidator_fixup_base` 是 validate/fixup 两虚槽的唯一公开
 *             入口，函数体只做空指针/虚表检查与虚表分派，不写业务逻辑；
 *             基类缺省实现放在 static VXValidator_* 槽内经 class_init
 *             注册；派生槽内回落父类一律 XClass_Parent。
 *
 *             与 Qt 的一处**有意偏差**：Qt 把 validate 声明为纯虚（=0），
 *             本库按任务书要求给出基类缺省 `Acceptable`（与
 *             `XAbstractSpinBox` 的 Validate 基类槽同口径），使抽象基类
 *             可独立实例化用于「无校验」场景。
 *
 *             类生命周期走 XObject 体系（对标 Qt QValidator 继承 QObject），
 *             与 XCompleter/XButtonGroup 同款：支持栈上 init 与堆上
 *             create_ex；XObject 禁用拷贝/移动（Qt Q_DISABLE_COPY），故
 *             本类不提供 create_copy/create_move。
 *
 * @note       模块总开关 XGUI_ON 定义于 CXinYueConfig.h；=0 时裁剪全部
 *             公共 API。Widget 侧正则适配器由 XRegularExpression_ON 门控。
 * @author     XinYueC 团队
 */
#ifndef XVALIDATOR_H
#define XVALIDATOR_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XClass.h"

/* ==================== 状态枚举（对标 QValidator::State） ==================== */

/**
 * @brief      输入校验状态（对标 QValidator::State；数值与 Qt 一致）。
 * @details    Qt 6.8 gui/util/qvalidator.h:30-34：
 *             `enum State { Invalid, Intermediate, Acceptable }` ⇒ 0/1/2。
 *             本枚举是**全库单一定义**：XAbstractSpinBox.h 原有一份同名
 *             同值定义已迁到此处（XAbstractSpinBox.h 改为包含本头）。
 *             XLineControlValidatorState / XLineEditValidatorState /
 *             XRegularExpressionValidator_State 三份平行枚举逐值同口径
 *             （各自 0/1/2），适配链以数值转换连接，不新增第四份定义。
 */
typedef enum XValidatorState
{
    XValidatorState_Invalid = 0,       /**< 非法输入（编辑被拒/回滚）。 */
    XValidatorState_Intermediate = 1,  /**< 中间态（未完成但可修正）。 */
    XValidatorState_Acceptable = 2     /**< 可接受输入。 */
} XValidatorState;

/* ==================== 虚函数表（新增 Validate/Fixup 两槽） ==================== */
XCLASS_DEFINE_BEGING(XValidator)
XCLASS_DEFINE_ENUM(XValidator, Validate) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XValidator, Fixup),
XCLASS_DEFINE_END(XValidator)

/**
 * @brief      XValidator 抽象基类对象；m_base 必须是第一个成员。
 * @details    本基类不携带状态（对标 QValidatorPrivate 的 locale 等属性
 *             本批不建）；派生类把自有字段**尾部追加**在 m_base 之后。
 */
typedef struct XValidator
{
    XObject m_base;    /**< 基类成员；必须是第一个，由 XClass 管理。 */
} XValidator;

/* ==================== 生命周期 ==================== */

/**
 * @brief      获取 XValidator 虚函数表（首次调用时构建单例虚表）。
 * @details    继承 XObject 虚表，注册 Validate/Fixup 两槽的基类缺省实现。
 * @return     虚函数表单例指针（库内部管理，借用；不得释放）。
 */
XVtable* XValidator_class_init(void);
/**
 * @brief      初始化 XValidator（对标 QValidator(parent) 构造）。
 * @details    目标可为未初始化内存；XObject_init 完成基类部分，随后把虚表
 *             绑到 XValidator。init 显式清零（MSVC /RTC1 口径）。
 * @param      self 目标对象；可为 NULL（无操作）。
 * @param      parent 父对象（借用）；可为 NULL。
 * @return     无返回值。
 */
void XValidator_init(XValidator* self, XObject* parent);
/** @brief 创建堆对象便捷宏：以默认内存类型调用 XValidator_create_ex。 */
#define XValidator_create(parent) \
    XValidator_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
/**
 * @brief      创建 XValidator 堆对象。
 * @param      memory 内存分配器类型（XMemoryType）。
 * @param      parent 父对象（借用）；可为 NULL。
 * @return     新建对象（调用方以 XClassDelete 释放）；失败返回 NULL。
 */
XValidator* XValidator_create_ex(XMemoryType memory, XObject* parent);

/* ==================== 唯一公开虚入口（*_base） ==================== */

/**
 * @brief      validate 虚槽的唯一公开入口（对标 QValidator::validate）。
 * @details    只做空指针/虚表检查 + 虚表分派（§497 条款 1）；基类缺省
 *             返回 XValidatorState_Acceptable。状态经返回值输出
 *             （Qt 的 `State validate(QString&, int&) const` 返回值）。
 * @param      self 校验器对象；NULL 时返回 Invalid。
 * @param      input 输入/输出：UTF-8 堆串地址（借用指针的指针）；槽可就地
 *             修改或替换缓冲（替换用 XStrdup，旧串由槽自行 XFree_System）。
 *             不得为 NULL 时解引用，调用方负责最终 XFree_System。
 * @param      pos 输入/输出：光标位置（UTF-8 字节偏移）；可为 NULL。
 * @return     XValidatorState 状态值；self 无效时返回 Invalid。
 * @warning    本入口不解引用 input 的内容之外的任何对象；self 必须是由
 *             XValidator_init/create_ex 构造的 XClass 对象。
 */
XValidatorState XValidator_validate_base(XValidator* self, char** input,
                                         int* pos);
/**
 * @brief      fixup 虚槽的唯一公开入口（对标 QValidator::fixup）。
 * @details    只做空指针/虚表/空槽检查 + 虚表分派（§497 条款 1、5）；
 *             基类缺省为空实现（qvalidator.cpp:250）。
 * @param      self 校验器对象；NULL 时无操作。
 * @param      input 输入/输出：UTF-8 堆串地址；契约同 validate_base。
 * @return     无返回值。
 */
void XValidator_fixup_base(XValidator* self, char** input);

/* ==================== Widget 侧正则适配器（§validator 专项） ==================== */

#if XRegularExpression_ON
/**
 * @brief      XCore 现成正则校验器的前向声明。
 * @details    完整定义在 XCore/XCode/XRegularExpression/
 *             XRegularExpressionValidator.h；本头只保存借用指针，避免
 *             XGui（上层）反向污染 XCore（下层）。
 */
struct XRegularExpressionValidator;

XCLASS_DEFINE_BEGING(XRegularExpressionValidatorAdapter)
XCLASS_DEFINE_EXTEND_END(XRegularExpressionValidatorAdapter, XValidator)

/**
 * @brief      把框架现成的 XRegularExpressionValidator 适配为 XValidator 派生。
 * @details    **为什么是 Widget 侧适配器而不是改 XCore 侧继承**：
 *             XValidator 是 XGui/Widget 层的类（对标 QtGui 的 QValidator），
 *             而 XRegularExpressionValidator 位于 XCore/XCode/XRegularExpression
 *             （值类型、继承 XClass）。让 XCore 的头文件包含 Widget 头会造成
 *             分层倒置；XCore 又是独立编译的底层库，包含路径也不保证可解析。
 *             适配器只持**借用**指针，validate 转发既有
 *             `XRegularExpressionValidator_validate_utf8`（铁律 4「框架有的
 *             绝不手搓」），fixup **不重写** ⇒ 走基类空实现，与 Qt
 *             QRegularExpressionValidator 不重写 fixup 完全一致
 *             （qvalidator.cpp:250 / :994-1013）。
 *             生命周期：正则校验器由调用方拥有，适配器销毁不解引用、不释放
 *             它；借用期须覆盖适配器的使用期（与三控件 setValidator 口径同）。
 * @note       m_regexValidator 尾部追加在 m_base 之后；init 显式清零。
 */
typedef struct XRegularExpressionValidatorAdapter
{
    XValidator m_base;                                  /**< 基类；第一个成员。 */
    const struct XRegularExpressionValidator* m_regexValidator;
                                                       /**< 借用；可为 NULL。 */
} XRegularExpressionValidatorAdapter;

/** @brief 获取适配器虚函数表（继承 XValidator，只重载 Validate）。 */
XVtable* XRegularExpressionValidatorAdapter_class_init(void);
/**
 * @brief      初始化适配器并绑定正则校验器（借用）。
 * @param      self 目标对象；可为 NULL。
 * @param      regex 正则校验器借用指针；可为 NULL（等价基类缺省 Acceptable）。
 * @return     无返回值。
 */
void XRegularExpressionValidatorAdapter_init(
        XRegularExpressionValidatorAdapter* self,
        const struct XRegularExpressionValidator* regex);
/** @brief 创建堆对象便捷宏（默认内存类型）。 */
#define XRegularExpressionValidatorAdapter_create(regex) \
    XRegularExpressionValidatorAdapter_create_ex( \
        XCLASS_DEFAULT_MEMORY_TYPE, (regex))
/**
 * @brief      创建适配器堆对象。
 * @param      memory 内存分配器类型。
 * @param      regex 正则校验器借用指针；可为 NULL。
 * @return     新建对象（调用方以 XClassDelete 释放）；失败返回 NULL。
 */
XRegularExpressionValidatorAdapter*
XRegularExpressionValidatorAdapter_create_ex(
        XMemoryType memory, const struct XRegularExpressionValidator* regex);
/**
 * @brief      更换借用的正则校验器（对标 QRegularExpressionValidator::
 *             setRegularExpression 的借用版）。
 * @param      self 适配器；可为 NULL。
 * @param      regex 新的正则校验器借用指针；NULL 表示退化为基类缺省。
 * @return     无返回值。
 */
void XRegularExpressionValidatorAdapter_setRegularExpression(
        XRegularExpressionValidatorAdapter* self,
        const struct XRegularExpressionValidator* regex);
/**
 * @brief      查询借用的正则校验器（借用，不得释放；未设置为 NULL）。
 */
const struct XRegularExpressionValidator*
XRegularExpressionValidatorAdapter_regularExpression_const(
        const XRegularExpressionValidatorAdapter* self);
#endif /* XRegularExpression_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVALIDATOR_H */
