/**
 * @file       XProperty_Protected.h
 * @brief      属性绑定模块内部机制（对标 Qt qpropertyprivate.h）。
 * @details    仅供 XProperty 模块内部实现（XProperty.c/XPropertyBinding.c/
 *             XPropertyObserver.c/XBindable.c/XPropertyAlias.c/
 *             XObjectBindableProperty.c）使用，不对外公开；外部代码禁止
 *             include 本头文件。包含绑定对象的完整内部结构与依赖捕获/
 *             惰性求值/级联通知的原语。
 */
#ifndef XPROPERTY_PROTECTED_H
#define XPROPERTY_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include "XProperty.h"
#include "XPropertyObserver.h"
#include "XPropertyBinding.h"
#include "XBindable.h"

/* ==================== 绑定对象内部结构（对标 QPropertyBindingPrivate） ==================== */

/**
 * @brief 绑定对象内部结构（对标 QPropertyBindingPrivate；公开侧为不透明句柄）。
 * @note 引用计数堆对象：create 时计 1；XPropertyData_setBinding 接管该引用，
 *       替换/清除/属性销毁时 unref；共享句柄经 XPropertyBinding_ref/unref 管理。
 */
struct XPropertyBinding
{
    XAtomic_int32_t m_ref;                    /**< 引用计数（对标 QPropertyBindingPrivatePtr）。 */
    XPropertyData* m_propertyData;            /**< 写入目标属性数据；未挂接为 NULL（对标 propertyData）。 */
    XPropertyBindingEvalFunc m_eval;          /**< 求值函数；create_eval 形式使用，否则 NULL。 */
    void* m_evalUser;                         /**< 求值函数用户数据（借用）。 */
    XPropertyBindingValueUpdater m_updater;   /**< 定制更新器；NULL 表示使用默认更新器。 */
    XPropertyObserver* m_dependencies;        /**< 依赖节点链表头（单向；节点挂在被依赖属性的观察者链表上）。 */
    XPropertyBindingSourceLocation m_location; /**< 创建时登记的源码位置。 */
    XPropertyBindingError m_error;            /**< 最近错误（嵌入 XClass 对象；对象拥有描述字符串）。 */
    XVariant m_result;                        /**< 最近求值结果缓存（定制更新器经 XBinding_result_const 读取）。 */
    bool m_dirty;                             /**< 失效标记：true 表示下次读取须重算。 */
    bool m_evaluating;                        /**< 重入保护：本绑定求值函数正在执行。 */
};

/**
 * @brief 查询绑定最近一次求值结果（默认更新器/定制更新器内部使用）。
 * @param binding 目标绑定；不能为 NULL。
 * @return 结果变体借用指针；禁止释放或修改。
 */
const XVariant* XBinding_result_const(XPropertyBinding* binding);

/**
 * @brief 惰性求值绑定（内部；由 XPropertyData_value_const 在脏时调用）。
 * @param binding 目标绑定；NULL 安全。
 * @return 无返回值。流程：求值栈入栈（记 XThreadData::m_bindingEvalStack）→
 *         解除旧依赖 → 清脏标记并求值 → 出栈 → 经更新器比较写回并通知。
 *         求值栈入栈失败时记录错误日志并保持脏标记，等待下次读取重试；
 *         栈扫描发现本绑定已在求值中（绑定循环）或求值函数重入时记录
 *         BindingLoop/EvaluationLoopError 并放弃本次求值。
 */
void XBinding_evaluate(XPropertyBinding* binding);

/**
 * @brief 查询当前正在求值的绑定（求值栈顶；对标 QBindingStatus::currentlyEvaluatingBinding）。
 * @return 绑定指针；无求值上下文返回 NULL。依赖捕获据此登记依赖。
 */
XPropertyBinding* XBinding_currentEvalBinding(void);

/**
 * @brief 解除并释放绑定的全部依赖节点（从各属性的观察者链表摘下并释放节点；
 *        依赖在每次求值中重新捕获）。
 * @param binding 目标绑定；NULL 安全。
 * @return 无返回值。绑定解挂/销毁/每次求值前调用。
 */
void XBinding_detachDependencies(XPropertyBinding* binding);

/**
 * @brief 查询绑定是否正在求值（绑定循环检测判据：任何循环必经过正在求值的绑定）。
 * @param binding 目标绑定；NULL 返回 false。
 * @return 正在求值返回 true。
 */
bool XBinding_isOnEvalStack(XPropertyBinding* binding);

/* ==================== 观察者链表内部操作 ==================== */

/**
 * @brief 把观察者节点链入属性数据的观察者链表头（内部；不检查重复挂接）。
 * @param node     目标节点；不能为 NULL，须处于未挂接状态。
 * @param data     目标属性数据；不能为 NULL。
 * @return 无返回值。
 */
void XPropertyObserver_attach(XPropertyObserver* node, XPropertyData* data);

/**
 * @brief 把观察者节点从其属性观察者链表解挂（内部；幂等）。
 * @param node 目标节点；NULL 安全。
 * @return 无返回值。解挂后清空 m_propertyData/m_prev/m_next。
 */
void XPropertyObserver_unlink(XPropertyObserver* node);

/**
 * @brief 创建绑定依赖节点并挂到被依赖属性上（内部；对标 QPropertyObserver 依赖路径）。
 * @param data    被依赖的属性数据；不能为 NULL。
 * @param binding 属主绑定；不能为 NULL。
 * @return 无返回值。分配失败时跳过登记（XERROR_PRINTF 记录），
 *         该依赖在本轮求值中缺失，可能漏更新。
 */
void XPropertyObserver_attachDependency(XPropertyData* data, XPropertyBinding* binding);

/* ==================== 属性数据内部操作 ==================== */

/**
 * @brief 沿观察者链表通知并级联（内部）。
 * @param data 目标属性数据；NULL 安全。
 * @return 无返回值。普通回调直接调用；变更处理器收到当前值指针、返回 true
 *         时解挂节点；依赖节点把属主绑定标脏（首次）并递归通知其目标属性
 *         观察者；别名锚点跳过。
 */
void XPropertyData_notifyObservers(XPropertyData* data);

/**
 * @brief 比较写入并通知（内部核心写路径；不断开绑定、不做显式赋值语义）。
 * @param data 目标属性数据；NULL 安全。
 * @param v    新值变体；借用，可为 NULL。内部深拷贝。
 * @return 值发生实际变化并已写回返回 true；相等或失败返回 false（不通知）。
 */
bool XPropertyData_writeAndNotify(XPropertyData* data, const XVariant* v);

/**
 * @brief 默认绑定更新器流程（内部；对标 QPropertyBindingPrivate 默认 wrapper）。
 * @param data   目标属性数据；NULL 安全。
 * @param result 求值结果；借用。
 * @return 值发生实际变化返回 true。
 */
bool XPropertyData_updateFromBinding(XPropertyData* data, const XVariant* result);

/**
 * @brief 解除属性当前绑定：解挂旧依赖、清空双向指针并 unref（内部）。
 * @param data 目标属性数据；NULL 安全。
 * @return 无返回值。
 */
void XPropertyData_removeBindingInternal(XPropertyData* data);

/**
 * @brief 把当前属性登记为指定绑定的依赖（内部；求值上下文中由 value_const 调用）。
 * @param data    被依赖属性数据；NULL 安全。
 * @param binding 正在求值的绑定；NULL 安全。
 * @return 无返回值。同一绑定对同一属性的重复依赖按已存在节点去重。
 */
void XPropertyData_captureDependency(XPropertyData* data, XPropertyBinding* binding);

/* ==================== 行为接口实例 ==================== */

/**
 * @brief 独立属性（XProperty）的行为接口实例。
 * @return 接口借用指针；值变化钩子为空操作。
 */
const XBindableInterface* XProperty_interface(void);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPROPERTY_PROTECTED_H */
