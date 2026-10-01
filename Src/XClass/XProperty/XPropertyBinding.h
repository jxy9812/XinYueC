/**
 * @file       XPropertyBinding.h
 * @brief      属性绑定公开 API（对标 Qt 6.8 QUntypedPropertyBinding / QPropertyBinding /
 *             QPropertyBindingError / QPropertyBindingSourceLocation）。
 * @details    声明式属性绑定系统的绑定对象。绑定把一个求值函数（C 函数指针）
 *             附着到属性（见 XProperty.h 的 XPropertyData）上：求值函数执行期间
 *             通过 XPropertyData_value_const 读取的每一个属性都会被自动登记为依赖，
 *             任一依赖变化时绑定被标记失效，属性下次被读取时惰性重算并写回，
 *             写回后沿观察者链表级联通知（对标 Qt 的惰性求值 + 脏标记语义）。
 *             Qt 用 std::function 承载表达式；本库为纯 C，表达式即函数指针 +
 *             用户数据指针。绑定对象为引用计数堆对象（对标
 *             QPropertyBindingPrivatePtr 的隐式共享），作为 setBinding 参数传入
 *             属性后所有权转移给属性，调用方如需保留句柄应先 XPropertyBinding_ref。
 *             实现只依赖 XinYueC 抽象层，禁止调用平台 API。
 */
#ifndef XPROPERTYBINDING_H
#define XPROPERTYBINDING_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XVariant.h"
#include "XString.h"

typedef struct XPropertyData XPropertyData;   /**< 属性数据载体（定义见 XProperty.h） */

/* ==================== 绑定错误（对标 QPropertyBindingError） ==================== */

/**
 * @brief 绑定错误类型枚举（对标 QPropertyBindingError::Type）。
 */
typedef enum XPropertyBindingErrorType
{
    XPropertyBindingError_NoError = 0,            /**< 无错误。 */
    XPropertyBindingError_BindingLoop = 1,        /**< 绑定循环：绑定直接或间接读取了自身目标属性。 */
    XPropertyBindingError_EvaluationLoopError = 2 /**< 求值循环：求值重入（观察者回调触发嵌套求值回到本绑定）。 */
    ,
    XPropertyBindingError_InvalidBindingType = 3, /**< 绑定结果类型与属性不兼容（预留；XVariant 动态类型下默认不触发）。 */
    XPropertyBindingError_Unknown = 4             /**< 未知错误（求值函数返回失败且未设置具体错误等）。 */
} XPropertyBindingErrorType;

XCLASS_DEFINE_BEGING(XPropertyBindingError)
XCLASS_DEFINE_EXTEND_END(XPropertyBindingError, XClass)

/**
 * @brief 绑定错误对象（对标 QPropertyBindingError）。
 * @note 可嵌入或堆创建；m_description 由对象拥有，deinit 时释放。
 */
typedef struct XPropertyBindingError
{
    XClass m_class;                     /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XPropertyBindingErrorType m_type;   /**< 错误类型；默认 XPropertyBindingError_NoError。 */
    XString* m_description;             /**< 错误描述（对象拥有；可为 NULL）；UTF-16 内部存储。 */
} XPropertyBindingError;

/**
 * @brief 初始化 XPropertyBindingError 类的虚函数表。
 * @return 指向初始化完成的 XVtable 的指针。
 */
XVtable* XPropertyBindingError_class_init(void);

/**
 * @brief 初始化绑定错误对象（栈对象用；对标 QPropertyBindingError() 默认构造）。
 * @param self 目标错误对象指针，不能为 NULL。
 * @return 无返回值。
 */
void XPropertyBindingError_init(XPropertyBindingError* self);

/**
 * @brief 初始化绑定错误对象并设置类型与描述（XString 主版本；对标 QPropertyBindingError(type, description)）。
 * @param self        目标错误对象指针，不能为 NULL。
 * @param type        错误类型。
 * @param description 错误描述；借用，可为 NULL（无描述）。函数内部深拷贝，不取得所有权。
 * @return 无返回值；分配失败时描述保持为空。
 */
void XPropertyBindingError_init_ex(XPropertyBindingError* self, XPropertyBindingErrorType type, const XString* description);

/**
 * @brief 初始化绑定错误对象并设置类型与 UTF-8 描述（对标 Qt 的 QString 重载；_2 为 UTF-8 兼容版本）。
 * @param self        目标错误对象指针，不能为 NULL。
 * @param type        错误类型。
 * @param description UTF-8 编码描述字符串；借用，可为 NULL（无描述），按 UTF-8 解码。
 * @return 无返回值；分配失败时描述保持为空。
 */
void XPropertyBindingError_init_ex_2(XPropertyBindingError* self, XPropertyBindingErrorType type, const char* description);

/** @brief 反初始化绑定错误对象；与 init 成对使用，释放内部描述字符串。 */
#define XPropertyBindingError_deinit_base XClass_deinit_base
/** @brief 删除堆上绑定错误对象；内部先反初始化再释放结构体内存。 */
#define XPropertyBindingError_delete_base XClass_delete_base

/**
 * @brief 在堆上创建绑定错误对象（默认 NoError；对标 QPropertyBindingError()）。
 * @return 新对象指针；分配失败返回 NULL。用 XPropertyBindingError_delete_base 释放。
 */
XPropertyBindingError* XPropertyBindingError_create(void);

/**
 * @brief 在堆上创建绑定错误对象并设置类型与描述（XString 主版本）。
 * @param type        错误类型。
 * @param description 错误描述；借用，可为 NULL。函数内部深拷贝。
 * @return 新对象指针；分配失败返回 NULL。用 XPropertyBindingError_delete_base 释放。
 */
XPropertyBindingError* XPropertyBindingError_create_ex(XPropertyBindingErrorType type, const XString* description);

/**
 * @brief 在堆上创建绑定错误对象并设置类型与 UTF-8 描述（_2 为 UTF-8 兼容版本）。
 * @param type        错误类型。
 * @param description UTF-8 编码描述；借用，可为 NULL，按 UTF-8 解码。
 * @return 新对象指针；分配失败返回 NULL。用 XPropertyBindingError_delete_base 释放。
 */
XPropertyBindingError* XPropertyBindingError_create_ex_2(XPropertyBindingErrorType type, const char* description);

/**
 * @brief 拷贝创建绑定错误对象（深拷贝描述字符串）。
 * @param other 源对象；不能为 NULL。
 * @return 新对象指针；分配失败返回 NULL。用 XPropertyBindingError_delete_base 释放。
 */
XPropertyBindingError* XPropertyBindingError_create_copy(const XPropertyBindingError* other);

/**
 * @brief 重置为无错误状态（对标 QPropertyBindingError 赋值 NoError 的效果）。
 * @param self 目标错误对象指针；NULL 不执行任何操作。
 * @return 无返回值。类型复位为 NoError，描述字符串释放并置空。
 */
void XPropertyBindingError_clear(XPropertyBindingError* self);

/**
 * @brief 查询错误类型（对标 QPropertyBindingError::type()）。
 * @param self 目标错误对象指针；NULL 返回 XPropertyBindingError_Unknown。
 * @return 错误类型。
 */
XPropertyBindingErrorType XPropertyBindingError_type(const XPropertyBindingError* self);

/**
 * @brief 查询错误描述（对标 QPropertyBindingError::description()）。
 * @param self 目标错误对象指针。
 * @return 描述字符串借用指针；未设置或 NULL 返回 NULL。借用指针禁止释放或修改。
 */
const XString* XPropertyBindingError_description_const(const XPropertyBindingError* self);

/* ==================== 源位置（对标 QPropertyBindingSourceLocation） ==================== */

/**
 * @brief 绑定源码位置（对标 QPropertyBindingSourceLocation；用于诊断信息）。
 * @note 全部为借用静态字符串（如 __FILE__/__func__ 字面量），结构体不拥有；
 *       通常经 XPropertyBindingSourceLocation_Here 宏就地构造。
 */
typedef struct XPropertyBindingSourceLocation
{
    const char* m_fileName;     /**< 源文件名（借用字面量；对标 fileName）。 */
    uint32_t m_line;            /**< 行号（对标 line）。 */
    uint32_t m_column;          /**< 列号（对标 column；C 侧通常填 0）。 */
    const char* m_functionName; /**< 函数名（借用字面量；对标 functionName）。 */
} XPropertyBindingSourceLocation;

/**
 * @brief 初始化源码位置为全空（对标 QPropertyBindingSourceLocation() 默认构造）。
 * @param self 目标位置结构体指针，不能为 NULL。
 * @return 无返回值。
 */
void XPropertyBindingSourceLocation_init(XPropertyBindingSourceLocation* self);

/**
 * @brief 就地构造源码位置（对标 Q_PROPERTY 绑定宏自动填充的 location 参数）。
 * @note 展开为复合字面量，可作为 XPropertyBinding_create 系列的 location 实参。
 */
#define XPropertyBindingSourceLocation_Here() \
    ((XPropertyBindingSourceLocation){__FILE__, (uint32_t)__LINE__, 0, __func__})

/* ==================== 绑定对象（对标 QUntypedPropertyBinding / QPropertyBinding） ==================== */

/**
 * @brief 绑定对象（对标 QUntypedPropertyBinding/QPropertyBinding；C 无模板，值经 XVariant 承载）。
 * @note 不透明类型：只能经指针使用。引用计数堆对象：create 返回计 1 的句柄；
 *       传入 XPropertyData_setBinding 等后所有权转移给属性；若需共享，先
 *       XPropertyBinding_ref 增加引用，用完 XPropertyBinding_unref 释放。
 */
typedef struct XPropertyBinding XPropertyBinding;

/**
 * @brief 求值函数类型（对标 QPropertyBinding&lt;T&gt;(std::function&lt;T(const T&)&gt;) 的表达式）。
 * @param user 创建时传入的用户数据指针；可为 NULL。
 * @param out  结果输出变体；进入时已初始化为 NULL 变体，函数内用
 *             XVariant_init(out, &value, sizeof(value), 类型) 写入结果（分配失败返回 false）。
 * @return 求值成功返回 true；返回 false 表示失败（应先经 XPropertyBinding_setError
 *         设置错误，否则按 Unknown 处理），属性值保持不变。
 * @note 求值期间通过 XPropertyData_value_const 读取属性会自动登记为依赖；
 *       不要在求值函数里写任何属性。
 */
typedef bool (*XPropertyBindingEvalFunc)(void* user, XVariant* out);

/**
 * @brief 定制值更新器类型（对标 QPropertyBindingValueUpdater）。
 * @param data    目标属性数据。
 * @param binding 所属绑定；最近一次求值结果经内部结果缓存（见 XProperty_Protected.h）读取。
 * @return 属性值发生实际变化返回 true，未变化返回 false。
 * @note 高级接口：普通场景用 create_eval 即可，默认更新器自动完成比较/写入/通知。
 */
typedef bool (*XPropertyBindingValueUpdater)(XPropertyData* data, XPropertyBinding* binding);

/**
 * @brief 绑定函数描述（对标 QPropertyBindingFunction； Qt 中另含 wrapper 槽位，C 侧合并进更新器）。
 */
typedef struct XPropertyBindingFunction
{
    XPropertyBindingValueUpdater m_updater; /**< 定制更新器；不能为 NULL。 */
} XPropertyBindingFunction;

/**
 * @brief 创建绑定（定制更新器形式；对标 QUntypedPropertyBinding(metaType, f, location)）。
 * @param fn       绑定函数描述；m_updater 不能为 NULL。
 * @param location 源码位置；可为 NULL（无诊断信息）。结构体按值拷贝保存，字符串借用。
 * @return 新绑定指针（引用计数 1，调用方持有）；分配失败返回 NULL。
 *         传入属性后所有权转移，详见类型说明。
 */
XPropertyBinding* XPropertyBinding_create(XPropertyBindingFunction fn, const XPropertyBindingSourceLocation* location);

/**
 * @brief 创建绑定（求值函数形式；对标 QPropertyBinding&lt;T&gt;(f, location)，最常用）。
 * @param eval     求值函数；不能为 NULL。
 * @param user     传给求值函数的用户数据；可为 NULL，仅借用，生命周期由调用方保证
 *                 不短于绑定（推荐指向属性/对象等被观察结构）。
 * @param location 源码位置；可为 NULL。结构体按值拷贝保存。
 * @return 新绑定指针（引用计数 1，调用方持有）；分配失败返回 NULL。
 */
XPropertyBinding* XPropertyBinding_create_eval(XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location);

/**
 * @brief 增加引用计数（对标 QPropertyBinding 隐式共享拷贝）。
 * @param self 目标绑定；NULL 安全（直接返回 NULL）。
 * @return 同一绑定指针（便于链式使用）。
 */
XPropertyBinding* XPropertyBinding_ref(XPropertyBinding* self);

/**
 * @brief 减少引用计数；减到 0 时解绑属性、释放依赖节点与错误信息并销毁绑定对象。
 * @param self 目标绑定；NULL 安全。
 * @return 无返回值。调用后不得再使用 self。
 */
void XPropertyBinding_unref(XPropertyBinding* self);

/**
 * @brief 查询绑定是否有效（对标 QUntypedPropertyBinding::isValid()）。
 * @param self 目标绑定；NULL 返回 false。
 * @return 非空绑定返回 true。空绑定对应 Qt 的默认构造（无效）绑定。
 */
bool XPropertyBinding_isValid(const XPropertyBinding* self);

/**
 * @brief 查询最近一次求值的错误（对标 QUntypedPropertyBinding::error()）。
 * @param self 目标绑定。
 * @return 内部错误对象借用指针；NULL 或无错误时类型为 NoError。借用指针禁止释放。
 */
const XPropertyBindingError* XPropertyBinding_error_const(const XPropertyBinding* self);

/**
 * @brief 查询创建时登记的源码位置（对标 QUntypedPropertyBinding::sourceLocation()）。
 * @param self 目标绑定。
 * @return 源码位置值拷贝；NULL 返回全空位置。
 */
XPropertyBindingSourceLocation XPropertyBinding_sourceLocation(const XPropertyBinding* self);

/**
 * @brief 由求值函数或外部代码设置绑定错误（对标 QPropertyBindingPrivate::setError）。
 * @param self        目标绑定；NULL 不执行任何操作。
 * @param type        错误类型。
 * @param description 错误描述；借用 XString，可为 NULL。内部深拷贝。
 * @return 无返回值。覆盖此前错误；错误不影响脏标记，仅影响 error() 查询结果。
 */
void XPropertyBinding_setError(XPropertyBinding* self, XPropertyBindingErrorType type, const XString* description);

/**
 * @brief 由求值函数或外部代码设置绑定错误（_2 为 UTF-8 兼容描述版本）。
 * @param self        目标绑定；NULL 不执行任何操作。
 * @param type        错误类型。
 * @param description UTF-8 编码描述；借用，可为 NULL，按 UTF-8 解码。内部深拷贝。
 * @return 无返回值。
 */
void XPropertyBinding_setError_2(XPropertyBinding* self, XPropertyBindingErrorType type, const char* description);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPROPERTYBINDING_H */
