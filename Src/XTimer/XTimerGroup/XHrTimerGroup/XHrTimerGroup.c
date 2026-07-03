#include "XHrTimerGroup.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XThreadData.h"
#include "XCoreApplication.h"
#include <string.h>

// --- 内部辅助函数声明 ---
static void VXHrTimerGroup_deinit(XHrTimerGroup* group);
static XHandle VXTimerGroupBase_addTimerNs(XHrTimerGroup* group, XTimerData data);
static bool VXTimerGroupBase_removeTimer(XHrTimerGroup* group, XHandle handle);
static void VXHrTimerGroup_tick(XHrTimerGroup* group);
static void VXHrTimerGroup_clear(XHrTimerGroup* group);

static inline XRBTreeNode* create_hr_timer_node(const void* pvData, const size_t dataTypeSize)
{
    XRBTreeNode* node = XMalloc_MultiPool(XRBTree_typeSize() + dataTypeSize);
    if (!node)return NULL;
    XRBTree_init(node, XRBTree_typeSize(), pvData, dataTypeSize);
    return node;
}
static inline void delete_hr_timer_node(XHrTimerGroup* group, XRBTreeNode* node) {
    if (node) {
        XFree_MultiPool(node); // 或者您项目中对应的简单 free 函数
    }
    /*XEventFunc* event = XEventFunc_create(delete_hr_timer_node_event,
        XVarList_Create(XVar(XRBTreeNode*, node)),
        NULL);
    XCoreApplication_tryPostEvent(group, event, XEVENT_PRIORITY_LOWEST);*/
}

// --- 红黑树比较函数 ---
static int32_t compare_expire_time_ns(const XHrTimerNodeData* a, const XHrTimerNodeData* b) {
    uint64_t time_a = ((const XHrTimerNodeData*)a)->m_expire_time_ns;
    uint64_t time_b = ((const XHrTimerNodeData*)b)->m_expire_time_ns;
    if (time_a < time_b) return XCompare_Less;
    if (time_a > time_b) return XCompare_Greater;
    return XCompare_Equality;
}

static int32_t less_expire_time_ns(XCompare compare, const void* a, const void* b) {
    (void)compare;
    return compare(a, b);
}

// --- 内存管理 ---
//static void delete_hr_timer_node_event(XVarList* argList) {
//    XVarList_args_1(argList, XRBTreeNode*, node);
//    if (node) {
//        XRBTree_delete(node, NULL, NULL);
//    }
//}


// --- 辅助函数：更新最小节点指针 (已修正) ---
/**
 * @brief 在树发生变化后，更新 m_min_node 指针。
 * 如果树为空，则 m_min_node 为 NULL。
 */
static void update_min_node(XHrTimerGroup* group) {
    if (group->m_rbtree_root == NULL) {
        group->m_min_node = NULL;
        return;
    }

    // 从根节点开始，一直向左走，直到最左叶子节点
    // 使用 XBTreeNode_GetLChild 宏
    XRBTreeNode* current = group->m_rbtree_root;
    while (XBTreeNode_GetLChild(current) != NULL) {
        current = (XRBTreeNode*)XBTreeNode_GetLChild(current);
    }
    group->m_min_node = current;
}

// --- 辅助函数：获取当前纳秒时间 ---
static inline uint64_t get_current_time_ns(XHrTimerGroup* group) {
    XHighResTimeFunc high_res_func = group->m_class.m_high_res_time_func;
    if (high_res_func) {
        return high_res_func();
    }
    else {
        // 回退到原有的毫秒时间源并转换
        return (uint64_t)XDateTime_currentMSecsSinceEpoch() * 1000000ULL;
    }
}

// --- 虚函数表初始化 ---
XVtable* XHrTimerGroup_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XHRTIMERGROUP_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXTimerGroupBase_addTimerNs,   // Add_TimerNs
        VXTimerGroupBase_removeTimer,  // Remove_Timer
        VXHrTimerGroup_tick,           // Tick
        VXHrTimerGroup_tick,           // Handler（复用 Tick 逻辑，避免重复定义）
        VXHrTimerGroup_clear           // Clear
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHrTimerGroup_deinit);
    return XVTABLE_DEFAULT;
}

// --- 析构函数 ---
static void VXHrTimerGroup_deinit(XHrTimerGroup* group) {
    // 先调用clear清理树中所有节点
    XHrTimerGroup_clear_base(group);
    if(group->m_mutex)
    {
        XMutex_delete(group->m_mutex);
        group->m_mutex = NULL;
    }
    XClass_Deinit_Parent(XObject,group);
}
// --- 删除定时器 (已修正作用域和指针访问) ---
static bool VXTimerGroupBase_removeTimer(XHrTimerGroup* group, XHandle handle) {
    XRBTreeNode* node_to_remove = (XRBTreeNode*)handle;
    if (!node_to_remove) return false;

    XHrTimerNodeData* node_data = (XHrTimerNodeData*)XRBTree_getData(node_to_remove);
    if (!node_data) return false;

    bool removed_successfully = false;
    XMutex_lock(group->m_mutex);
    {
        //基本可以断定是在回调函数中被调用了，tick后续还要访问该节点所以不能释放做一个标记释放交给tick函数
        if (node_data->m_is_detached) {
            node_data->m_is_was_deleted = true;
            removed_successfully = true;
        }
        else {
            // 情况2: 节点仍在树中，正常移除
            XRBTreeNode* removed_node = XRBTree_removeNode(
                &group->m_rbtree_root,
                node_to_remove,
                sizeof(XHrTimerNodeData)
            );

            if (removed_node) {
                ((XHrTimerNodeData*)XRBTree_getData(removed_node))->m_is_detached = true;

                if (node_to_remove == group->m_min_node) {
                    update_min_node(group);
                }

                XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
                removed_successfully = true;
                delete_hr_timer_node(group, removed_node);
            }
        }
    }
    XMutex_unlock(group->m_mutex);

    return removed_successfully;
}

static void VXHrTimerGroup_tick(XHrTimerGroup* group) {
    uint64_t current_time_ns = get_current_time_ns(group);

    // --- 第一阶段：加锁，分离所有过期节点 ---
    XRBTreeNode* expired_list_head = NULL;
    XRBTreeNode* expired_list_tail = NULL;

    XMutex_lock(group->m_mutex);
    {
        // 循环：只要最小节点存在且已过期，就将其移除
        while (group->m_min_node != NULL) {
            XHrTimerNodeData* min_data = (XHrTimerNodeData*)XRBTree_getData(group->m_min_node);

            // 如果最小节点都未过期，则全部未过期，跳出循环
            if (min_data->m_expire_time_ns > current_time_ns) {
                break;
            }

            // 从红黑树中移除当前最小节点
            XRBTreeNode* detached_node = XRBTree_removeNode(
                &group->m_rbtree_root,
                group->m_min_node, // 直接使用缓存的最小节点
                sizeof(XHrTimerNodeData)
            );

            if (detached_node) {
                // 标记此节点已从树中分离
                XHrTimerNodeData* detached_data = (XHrTimerNodeData*)XRBTree_getData(detached_node);
                detached_data->m_is_detached = true;

                // 将分离出的节点加入到过期链表 (使用右子节点指针作为链表next)
                if (expired_list_head == NULL) {
                    expired_list_head = detached_node;
                    expired_list_tail = detached_node;
                }
                else {
                    XBTreeNode_SetRChild(expired_list_tail, detached_node);
                    expired_list_tail = detached_node;
                }
                // 链表尾部置空
                XBTreeNode_SetRChild(detached_node, NULL);

                // 更新计数
                XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);

                // 【关键修复】每次移除后都安全地更新 m_min_node 缓存！
                update_min_node(group);
            }
            else {
                // 安全兜底：如果移除失败，强制更新缓存以避免死循环
                update_min_node(group);
                break;
            }
        }
    }
    XMutex_unlock(group->m_mutex);

    // --- 第二阶段：无锁，遍历过期链表并执行回调 ---
    XRBTreeNode* current_expired = expired_list_head;
    while (current_expired != NULL) {
        XHrTimerNodeData* data = (XHrTimerNodeData*)XRBTree_getData(current_expired);
        XRBTreeNode* next_expired = (XRBTreeNode*)XBTreeNode_GetRChild(current_expired);

        // 执行用户回调
        if (data->m_timer_data.m_timerCallback) {
            data->m_timer_data.m_timerCallback(data->m_timer_data.m_userData, &data->m_timer_data);
        }

        // --- 第三阶段：决定是释放还是重新调度 ---
        if (data->m_timer_data.m_isSingleShot || data->m_is_was_deleted) {
            // 单次定时器或已被外部 remove 的定时器：释放内存
            delete_hr_timer_node(group, current_expired);
        }
                else {
            // 周期性定时器：重新计算下次到期时间
            data->m_expire_time_ns = current_time_ns + data->m_timer_data.m_interval;

            // 重新加锁以安全地插入回红黑树
            XMutex_lock(group->m_mutex);
            {
                data->m_is_detached = false; // 重新插入前清除标记（在锁内执行）
                XRBTree_insertNode(&group->m_rbtree_root, compare_expire_time_ns, less_expire_time_ns,current_expired);
                XAtomic_fetch_add_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
                update_min_node(group);
            }
            XMutex_unlock(group->m_mutex);
        }

        current_expired = next_expired;
    }
}

void VXHrTimerGroup_clear(XHrTimerGroup* group)
{
    if(XAtomic_load_size_t(&group->m_count, XAtomic_MemoryOrder_Relaxed))
    {
        XMutex_lock(group->m_mutex);
        if(group->m_rbtree_root)
        {
            XRBTree_delete(group->m_rbtree_root, NULL, NULL);
            group->m_rbtree_root = NULL;
            group->m_min_node = NULL;
            XAtomic_store_size_t(&group->m_count, 0, XAtomic_MemoryOrder_Relaxed);
        }
        XMutex_unlock(group->m_mutex);
    }
}

static XHandle VXTimerGroupBase_addTimerNs(XHrTimerGroup* group, XTimerData data) {
    if (!group) return NULL;
    if (data.m_timerCallback == NULL ) {
        return NULL;
    }

    uint64_t current_time_ns = get_current_time_ns(group);
    uint64_t expire_time_ns = current_time_ns + data.m_timeout;
    if (data.m_timeout == 0) {
        expire_time_ns = current_time_ns + data.m_interval;
    }

    XHrTimerNodeData node_data = { 0 };
    memcpy(&node_data.m_timer_data,&data,sizeof(XTimerData));
    node_data.m_expire_time_ns = expire_time_ns;
    //node_data.m_timer_data.timerId = 0;
    //node_data.m_timer_data.m_timeout = data.m_timeout;
    //node_data.m_timer_data.m_interval = data.m_interval;
    //node_data.m_timer_data.m_isSingleShot = is_single_shot;
    //node_data.m_timer_data.m_timerCallback = callback;
    //node_data.m_timer_data.m_userData = user_data;
    node_data.m_is_detached = false;

    XRBTreeNode* new_node = NULL;
    XMutex_lock(group->m_mutex);
    {
        new_node = create_hr_timer_node(&node_data, sizeof(XHrTimerNodeData));
        if (!new_node) {
            XMutex_unlock(group->m_mutex);
            return NULL;
        }
        new_node = XRBTree_insertNode(&group->m_rbtree_root, compare_expire_time_ns, less_expire_time_ns, new_node);
        if (new_node) {
            XAtomic_fetch_add_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
            update_min_node(group);
        }
    }
    XMutex_unlock(group->m_mutex);

    return (XHandle)new_node;
}
// --- 对外构造函数 ---
XHrTimerGroup* XHrTimerGroup_create(uint64_t precision_ns) {
    if (precision_ns == 0) return NULL;

    XHrTimerGroup* group = XMalloc_System(sizeof(XHrTimerGroup));
    if (group == NULL) return NULL;

    XHrTimerGroup_init(group, precision_ns);
    Set_Class_MemoryFree(group, XFree_System);
    return group;
}

void XHrTimerGroup_init(XHrTimerGroup* group, uint64_t precision_ns) {
    if (group == NULL || precision_ns == 0) return;

    XTimerGroupBase_init((XTimerGroupBase*)group, precision_ns);
    XClassGetVtable(group) = XHrTimerGroup_class_init();

    group->m_rbtree_root = NULL;
    group->m_min_node = NULL; // 初始化最小节点指针
    XAtomic_init(group->m_count, 0);
    //group->m_mutex = XMutex_create(XLock_Spin);
    group->m_mutex = NULL;
    ((XTimerGroupBase*)group)->m_max_time = UINT64_MAX;
    ((XTimerGroupBase*)group)->m_min_time = precision_ns;
    ((XTimerGroupBase*)group)->m_high_res_time_func = NULL;
    //group->m_pending_delete_list = NULL;
}

uint64_t XHrTimerGroup_getNextExpireTime(XHrTimerGroup* group)
{
    if (!group) {
        return UINT64_MAX;
    }

    uint64_t next_expire_time = UINT64_MAX;

    // 加锁以确保在读取 m_min_node 时其状态是一致的
    XMutex_lock(group->m_mutex);
    {
        if (group->m_min_node != NULL) {
            XHrTimerNodeData* min_data = (XHrTimerNodeData*)XRBTree_getData(group->m_min_node);
            if (min_data) {
                next_expire_time = min_data->m_expire_time_ns;
            }
        }
    }
    XMutex_unlock(group->m_mutex);
    //XPrintf("时间:%lld\n", next_expire_time);
    /*uint64_t current_time_ns = get_current_time_ns(group);
    if (next_expire_time - current_time_ns > 30 * 1000000)
    {
        XPrintf("时间异常\n");

    }*/
    return next_expire_time;
}

size_t XHrTimerGroup_count(XHrTimerGroup* group)
{
    return group ? XAtomic_load_size_t(&group->m_count, XAtomic_MemoryOrder_Relaxed) : 0;
}
