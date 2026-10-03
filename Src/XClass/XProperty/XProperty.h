/**
 * @file       XProperty.h
 * @brief      声明式属性公开 API（对标 Qt 6.8 QPropertyData&lt;T&gt; / QProperty&lt;T&gt;）。
 * @details    数据驱动界面的核心设施：属性 = 值（XVariant 承载）+ 绑定 + 观察者
 *             链表。业务代码只写数据/绑定，界面通过观察者或 notify 信号自动跟随
 *             （UI = f(state)）。语义要点（均与 Qt 对齐）：
 *             - 绑定惰性求值：setBinding 只标脏，属性下次被读取时重算并写回；
 *             - 依赖自动捕获：求值函数执行期间经 value_const 读到的每个属性都
 *               成为依赖（通过 XThreadData::m_bindingEvalStack 识别求值上下文），
 *               每次求值前先解除旧依赖再重新捕获（支持条件依赖）；
 *             - 级联通知：依赖变化时把依赖它的绑定标脏并立即通知其目标属性的
 *               观察者（观察者回调里重读属性即得新值）；
 *             - 显式赋值断开绑定：setValue 先移除既有绑定（与值是否相等无关），
 *               再比较写入，值相等不通知；
 *             - 绑定循环检测：求值中的绑定被再次读取时记 BindingLoop 错误并
 *               返回现值，绝不重入。
 *             XProperty 为 XClass 派生值对象；XPropertyData 为可嵌入的属性数据
 *             载体（XObjectBindableProperty/XPropertyAlias 复用同一载体）。
 *             实现只依赖 XinYueC 抽象层，禁止调用平台 API。
 */
#ifndef XPROPERTY_H
#define XPROPERTY_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XVariant.h"
#include "XPropertyBinding.h"
#include "XPropertyObserver.h"

typedef struct XBindableInterface XBindableInterface; /**< 可绑定行为接口（定义见 XBindable.h）。 */

/* ==================== 属性数据载体（对标 QUntypedPropertyData/QPropertyData<T>） ==================== */

/**
 * @brief 属性数据载体：值 + 观察者链表 + 绑定 + 行为接口。
 * @note 可嵌入其它结构体（XObjectBindableProperty 要求 m_data 为首成员以便
 *       接口回调回推容器指针）。字段仅供模块内部使用，调用方禁止直接修改；
 *       读写一律走下方函数。
 */
typedef struct XPropertyData
{
    XVariant m_value;                        /**< 属性值（对象拥有；初始为 NULL 变体）。 */
    XPropertyObserver* m_firstObserver;      /**< 观察者链表头（节点归各自创建方所有）。仅供内部使用。 */
    XPropertyBinding* m_binding;             /**< 当前绑定（对象持有引用）；无绑定为 NULL。仅供内部使用。 */
    const XBindableInterface* m_interface;   /**< 行为接口（由属性类型决定：独立属性/对象属性）。仅供内部使用。 */
} XPropertyData;

/**
 * @brief 初始化属性数据载体（默认无绑定、无观察者、NULL 变体、独立属性接口）。
 * @param self 目标属性数据指针，不能为 NULL。
 * @return 无返回值。
 */
void XPropertyData_init(XPropertyData* self);

/**
 * @brief 初始化属性数据载体并设置初值（对标 QProperty(const T &initialValue) 的存储部分）。
 * @param self         目标属性数据指针，不能为 NULL。
 * @param initialValue 初值变体；借用，可为 NULL（等价 init）。内部深拷贝。
 * @return 无返回值。
 */
void XPropertyData_init_ex(XPropertyData* self, const XVariant* initialValue);

/**
 * @brief 反初始化属性数据载体；解挂全部观察者、解除绑定并释放值。
 * @param self 目标属性数据指针；NULL 安全。
 * @return 无返回值。观察者节点内存仍归各创建方；与 init 成对使用。
 */
void XPropertyData_deinit(XPropertyData* self);

/**
 * @brief 读取属性值（对标 QProperty::value()；求值后返回借用指针）。
 * @param data 目标属性数据指针。
 * @return 内部值借用指针；NULL 返回 NULL。若绑定已失效会先触发惰性重算；
 *         若处于某绑定求值上下文中，本属性会被登记为该绑定的依赖。
 * @note 借用指针在下一次写入/重算前有效；禁止释放或修改。
 */
const XVariant* XPropertyData_value_const(XPropertyData* data);

/**
 * @brief 读取属性值并深拷贝到输出变体。
 * @param data 目标属性数据指针。
 * @param out  输出变体；可为未初始化对象（内部兜底初始化），调用方负责 deinit。
 * @return 拷贝成功返回 true；参数非法或分配失败返回 false（out 保持有效空状态）。
 */
bool XPropertyData_value(XPropertyData* data, XVariant* out);

/**
 * @brief 写入属性值（对标 QProperty::setValue；显式赋值会先移除既有绑定）。
 * @param data 目标属性数据指针；NULL 不执行任何操作。
 * @param v    新值变体；借用，可为 NULL（按 NULL 变体处理）。内部深拷贝。
 * @return 无返回值。值与现值相等时只移除绑定不通知；变化时写回并沿观察者
 *         链表通知、调用接口的值变化钩子（对象属性由此发出 notify 信号）。
 */
void XPropertyData_setValue(XPropertyData* data, const XVariant* v);

/**
 * @brief 写入属性值（移动语义；先移除既有绑定）。
 * @param data 目标属性数据指针；NULL 不执行任何操作。
 * @param v    新值变体；资源所有权转移到属性，调用方之后仍需按其分配方式
 *             deinit（此时为空对象），不得再使用其中的数据。
 * @return 无返回值。值语义与 XPropertyData_setValue 相同。
 */
void XPropertyData_setValue_move(XPropertyData* data, XVariant* v);

/**
 * @brief 查询是否存在绑定（对标 QProperty::hasBinding）。
 * @param data 目标属性数据指针。
 * @return 存在有效绑定返回 true；否则 false。
 */
bool XPropertyData_hasBinding(const XPropertyData* data);

/**
 * @brief 查询当前绑定（对标 QProperty::binding()）。
 * @param data 目标属性数据指针。
 * @return 绑定借用指针；无绑定返回 NULL。借用指针禁止 unref；如需共享先
 *         XPropertyBinding_ref。
 */
XPropertyBinding* XPropertyData_binding_const(const XPropertyData* data);

/**
 * @brief 设置绑定（对标 QProperty::setBinding(const QUntypedPropertyBinding&)；接管绑定所有权）。
 * @param data    目标属性数据指针；NULL 不执行任何操作。
 * @param binding 绑定对象；所有权转移给属性（传入后调用方不得再 unref），
 *                可为 NULL（等价于清除绑定，对标 setBinding(QUntypedPropertyBinding())）。
 * @return 无返回值。只标脏不立即求值；替换/清除旧绑定时旧绑定依赖随之解挂。
 */
void XPropertyData_setBinding(XPropertyData* data, XPropertyBinding* binding);

/**
 * @brief 用求值函数设置绑定（对标 QProperty::setBinding(Functor) 便捷形式）。
 * @param data     目标属性数据指针；NULL 返回 NULL。
 * @param eval     求值函数；不能为 NULL。
 * @param user     求值函数用户数据；可为 NULL，仅借用，生命周期须覆盖绑定。
 * @param location 源码位置；可为 NULL。
 * @return 新建绑定指针；归属性所有，禁止 unref；分配失败返回 NULL（属性不变）。
 */
XPropertyBinding* XPropertyData_setBinding_eval(XPropertyData* data, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location);

/**
 * @brief 取出绑定并解除挂接（对标 QProperty::takeBinding）。
 * @param data 目标属性数据指针。
 * @return 绑定指针（所有权移交调用方，用完 XPropertyBinding_unref）；无绑定返回 NULL。
 */
XPropertyBinding* XPropertyData_takeBinding(XPropertyData* data);

/**
 * @brief 订阅属性变化（观察者由调用方持有；对标 QUntypedBindable::subscribe 的嵌入用法）。
 * @param data     目标属性数据指针；NULL 返回 false。
 * @param observer 观察者节点；节点归调用方所有（可嵌入），须先或事后用
 *                 setCallback/setHandler 设置行为；属性销毁时自动解挂。
 * @return 挂接成功返回 true。已挂接节点会先解挂再重挂（支持改投）。
 */
bool XPropertyData_subscribe(XPropertyData* data, XPropertyObserver* observer);

/**
 * @brief 以堆分配观察者订阅值变化（对标 QUntypedBindable::onValueChanged）。
 * @param data 目标属性数据指针。
 * @param callback 值变化回调；可为 NULL（挂接但通知跳过，无意义）。
 * @param user     回调用户数据；可为 NULL，仅借用。
 * @return 新建观察者指针，归调用方所有：注销用 XPropertyObserver_delete
 *         （或处理器返回 true）；NULL 表示参数非法或分配失败。
 */
XPropertyObserver* XPropertyData_onValueChanged(XPropertyData* data, XPropertyObserverCallback callback, void* user);

/**
 * @brief 绕过绑定直读值（对标 QPropertyData::valueBypassingBindings；逃生舱）。
 * @param data 目标属性数据指针。
 * @return 内部值借用指针；不触发求值、不捕获依赖。仅限内部实现与性能特殊路径。
 */
const XVariant* XPropertyData_valueBypassingBindings_const(const XPropertyData* data);

/**
 * @brief 绕过绑定直写值（对标 QPropertyData::setValueBypassingBinding；逃生舱）。
 * @param data 目标属性数据指针；NULL 不执行任何操作。
 * @param v    新值变体；借用，可为 NULL。不比较、不通知、不断开绑定。
 * @return 无返回值。调用方须自行保证后续一致性（通常随后手动通知）。
 */
void XPropertyData_setValueBypassingBinding(XPropertyData* data, const XVariant* v);

/* ==================== 独立属性对象（对标 QProperty<T>） ==================== */

/**
 * @brief XProperty 虚函数表（继承 XClass，不新增虚槽；重载 copy/move/deinit）。
 */
XCLASS_DEFINE_BEGING(XProperty)
XCLASS_DEFINE_EXTEND_END(XProperty, XClass)

/**
 * @brief 独立属性对象（对标 QProperty&lt;T&gt;；值经 XVariant 承载）。
 */
typedef struct XProperty
{
    XClass m_class;       /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XPropertyData m_data; /**< 属性数据载体（值/绑定/观察者/接口）。 */
} XProperty;

/**
 * @brief 初始化独立属性（NULL 变体、无绑定；对标 QProperty() 默认构造）。
 * @param self 目标属性指针，不能为 NULL。
 * @return 无返回值。
 */
void XProperty_init(XProperty* self);

/**
 * @brief 初始化独立属性并设置初值（对标 QProperty(const T &initialValue)）。
 * @param self         目标属性指针，不能为 NULL。
 * @param initialValue 初值变体；借用，可为 NULL（等价 init）。内部深拷贝。
 * @return 无返回值。
 */
void XProperty_init_ex(XProperty* self, const XVariant* initialValue);


/**
 * @brief 堆上创建独立属性（对标 QProperty()）。
 * @return 新属性指针；分配失败返回 NULL。用 XClassDelete 释放。
 */
XProperty* XProperty_create(void);

/**
 * @brief 堆上创建独立属性并设置初值（对标 QProperty(const T&)）。
 * @param initialValue 初值变体；借用，可为 NULL。内部深拷贝。
 * @return 新属性指针；分配失败返回 NULL。用 XClassDelete 释放。
 */
XProperty* XProperty_create_ex(const XVariant* initialValue);

/**
 * @brief 拷贝创建独立属性（对标 Qt：QProperty 禁止拷贝；此处按库规范提供
 *        仅拷贝值的弱化语义，绑定与观察者不随拷贝转移）。
 * @param other 源属性；不能为 NULL。
 * @return 新属性指针；分配失败返回 NULL。用 XClassDelete 释放。
 */
XProperty* XProperty_create_copy(const XProperty* other);

/**
 * @brief 读取属性值（求值后借用指针；对标 QProperty::value()）。
 * @param self 目标属性指针。
 * @return 内部值借用指针；NULL 返回 NULL。借用指针禁止释放或修改。
 */
const XVariant* XProperty_value_const(XProperty* self);

/**
 * @brief 读取属性值并深拷贝到输出变体。
 * @param self 目标属性指针。
 * @param out  输出变体；可为未初始化对象，调用方负责 deinit。
 * @return 拷贝成功返回 true；失败返回 false。
 */
bool XProperty_value(XProperty* self, XVariant* out);

/**
 * @brief 写入属性值（对标 QProperty::setValue；先移除既有绑定）。
 * @param self 目标属性指针；NULL 不执行任何操作。
 * @param v    新值变体；借用，可为 NULL。内部深拷贝。
 * @return 无返回值。
 */
void XProperty_setValue(XProperty* self, const XVariant* v);

/**
 * @brief 写入属性值（移动语义；先移除既有绑定）。
 * @param self 目标属性指针；NULL 不执行任何操作。
 * @param v    新值变体；资源所有权转移到属性；调用方仍需 deinit 空源对象。
 * @return 无返回值。
 */
void XProperty_setValue_move(XProperty* self, XVariant* v);

/** @brief 查询是否存在绑定（对标 QProperty::hasBinding）。 */
bool XProperty_hasBinding(const XProperty* self);

/** @brief 查询当前绑定借用指针（对标 QProperty::binding()）；无绑定返回 NULL，禁止 unref。 */
XPropertyBinding* XProperty_binding_const(const XProperty* self);

/**
 * @brief 设置绑定（对标 QProperty::setBinding；接管绑定所有权，传入后禁止 unref）。
 * @param self    目标属性指针；NULL 不执行任何操作。
 * @param binding 绑定对象；可为 NULL（清除绑定）。
 * @return 无返回值。
 */
void XProperty_setBinding(XProperty* self, XPropertyBinding* binding);

/**
 * @brief 用求值函数设置绑定（对标 QProperty::setBinding(Functor)）。
 * @param self     目标属性指针；NULL 返回 NULL。
 * @param eval     求值函数；不能为 NULL。
 * @param user     求值函数用户数据；可为 NULL，仅借用。
 * @param location 源码位置；可为 NULL。
 * @return 新建绑定指针；归属性所有，禁止 unref；失败返回 NULL。
 */
XPropertyBinding* XProperty_setBinding_eval(XProperty* self, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location);

/**
 * @brief 取出绑定（对标 QProperty::takeBinding）；所有权移交调用方，无绑定返回 NULL。
 */
XPropertyBinding* XProperty_takeBinding(XProperty* self);

/**
 * @brief 订阅属性变化（观察者归调用方持有；对标 QUntypedBindable::subscribe）。
 * @param self     目标属性指针；NULL 返回 false。
 * @param observer 观察者节点；节点归调用方所有（可嵌入），属性销毁时自动解挂。
 * @return 挂接成功返回 true。
 */
bool XProperty_subscribe(XProperty* self, XPropertyObserver* observer);

/**
 * @brief 以堆分配观察者订阅值变化（对标 QUntypedBindable::onValueChanged）。
 * @param self 目标属性指针。
 * @param callback 值变化回调；可为 NULL。
 * @param user     回调用户数据；可为 NULL，仅借用。
 * @return 新建观察者指针，归调用方所有，用 XPropertyObserver_delete 注销；失败返回 NULL。
 */
XPropertyObserver* XProperty_onValueChanged(XProperty* self, XPropertyObserverCallback callback, void* user);

/** @brief 绕过绑定直读值（对标 valueBypassingBindings；逃生舱，不求值不捕获）。 */
const XVariant* XProperty_valueBypassingBindings_const(const XProperty* self);

/** @brief 绕过绑定直写值（对标 setValueBypassingBinding；逃生舱，不比较不通知）。 */
void XProperty_setValueBypassingBinding(XProperty* self, const XVariant* v);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPROPERTY_H */
