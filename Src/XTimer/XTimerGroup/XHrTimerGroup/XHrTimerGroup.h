#ifndef XHrTimerGroup_H
#define XHrTimerGroup_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XTimerGroupBase.h"
#include "XRedBlackTree.h"

// 前向声明
typedef struct XHrTimerGroup XHrTimerGroup;

// 虚函数表大小定义
#define XHRTIMERGROUP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XHrTimerGroup))

XCLASS_DEFINE_BEGING(XHrTimerGroup)
XCLASS_DEFINE_EXTEND_END(XHrTimerGroup, XTimerGroupBase)

/**
 * @brief 红黑树定时器节点数据结构
 */
typedef struct XHrTimerNodeData 
{
    bool m_is_detached;        ///< 标记此节点是否已从红黑树中分离
    bool m_is_was_deleted;     //已经被标记为删除
    uint64_t m_expire_time_ns; ///< 绝对到期时间（纳秒）
    XTimerData m_timer_data;   ///< 定时器参数 (内部时间单位: 纳秒)
} XHrTimerNodeData;

/**
 * @brief 高精度/大范围定时器组 (基于红黑树)
 */
typedef struct XHrTimerGroup 
{
    XTimerGroupBase m_class;         ///< 继承自 XTimerGroupBase
    XRBTreeNode* m_rbtree_root;      ///< 红黑树根节点
    XRBTreeNode* m_min_node;         ///< 指向到期时间最小的节点，用于O(1)查找
    XAtomic_size_t m_count;          ///< 正在管理的定时器数量
    XMutex* m_mutex;                 ///< 互斥锁，保证线程安全
} XHrTimerGroup;

// --- 公共 API ---

// 标准构造函数，精度单位为纳秒
XVtable* XHrTimerGroup_class_init(void);
XHrTimerGroup* XHrTimerGroup_create(uint64_t precision_ns);
void XHrTimerGroup_init(XHrTimerGroup* group, uint64_t precision_ns);
// 虚函数调用宏 (保持不变)
#define XHrTimerGroup_setHighResTimeFunc    XTimerGroupBase_setHighResTimeFunc
#define XHrTimerGroup_addTimerMs_base       XTimerGroupBase_addTimerMs_base
#define XHrTimerGroup_addTimerNs_base       XTimerGroupBase_addTimerNs_base
#define XHrTimerGroup_removeTimer_base      XTimerGroupBase_removeTimer_base
#define XHrTimerGroup_timeRange             XTimerGroupBase_timeRange
#define XHrTimerGroup_min_time              XTimerGroupBase_min_time
#define XHrTimerGroup_max_time              XTimerGroupBase_max_time
#define XHrTimerGroup_tick_base             XTimerGroupBase_tick_base
#define XHrTimerGroup_handler_base          XTimerGroupBase_handler_base
#define XHrTimerGroup_clear_base            XTimerGroupBase_clear_base
#define XHrTimerGroup_deleteLater           XTimerGroupBase_deleteLater

#ifdef __cplusplus
}
#endif

#endif // !XHrTimerGroup_H