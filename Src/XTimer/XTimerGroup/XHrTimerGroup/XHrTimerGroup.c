#include "XHrTimerGroup.h"
#include "XMemory.h"
#include "XThreadData.h"
#include "XCoreApplication.h"
#include <string.h>

// --- 内部辅助函数声明 ---
static void VXHrTimerGroup_deinit(XHrTimerGroup* group);
static XHandle VXTimerGroupBase_addTimerMs(XHrTimerGroup* group, XTimerData data);
static XHandle VXTimerGroupBase_addTimerNs(XHrTimerGroup* group, XTimerData data);
static bool VXTimerGroupBase_removeTimer(XHrTimerGroup* group, XHandle handle);
static void VXHrTimerGroup_tick(XHrTimerGroup* group);

// --- 红黑树比较函数 ---
static int compare_expire_time_ns(const void* a, const void* b) {
    uint64_t time_a = ((const XHrTimerNodeData*)a)->m_expire_time_ns;
    uint64_t time_b = ((const XHrTimerNodeData*)b)->m_expire_time_ns;
    if (time_a < time_b) return -1;
    if (time_a > time_b) return 1;
    return 0;
}

static bool less_expire_time_ns(const void* a, const void* b) {
    return ((const XHrTimerNodeData*)a)->m_expire_time_ns < ((const XHrTimerNodeData*)b)->m_expire_time_ns;
}

static bool equal_expire_time_and_id(const void* node_data, const void* target) {
    const XHrTimerNodeData* node = (const XHrTimerNodeData*)node_data;
    const XHrTimerNodeData* target_node = (const XHrTimerNodeData*)target;
    return (node->m_expire_time_ns == target_node->m_expire_time_ns) &&
        (node->m_timer_data.timerId == target_node->m_timer_data.timerId);
}

// --- 内存管理 ---
static void delete_hr_timer_node_event(XVarList* argList) {
    XVarList_args_1(argList, XRBTreeNode*, node);
    if (node) {
        XRBTree_delete(node, NULL, NULL);
    }
}

static void delete_hr_timer_node(XHrTimerGroup* group, XRBTreeNode* node) {
    XEventFunc* event = XEventFunc_create(delete_hr_timer_node_event,
        XVarList_Create(XVar(XRBTreeNode*, node)),
        NULL);
    XCoreApplication_tryPostEvent(group, event, XEVENT_PRIORITY_LOWEST);
}

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
        return (uint64_t)XTimer_getCurrentTime() * 1000000ULL;
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
        VXTimerGroupBase_addTimerMs,
        VXTimerGroupBase_addTimerNs,
        VXTimerGroupBase_removeTimer,
        VXHrTimerGroup_tick,VXHrTimerGroup_tick
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHrTimerGroup_deinit);
    return XVTABLE_DEFAULT;
}

// --- 析构函数 ---
static void VXHrTimerGroup_deinit(XHrTimerGroup* group) {
    XMutex_lock(group->m_mutex);
    XRBTree_delete(group->m_rbtree_root, NULL, NULL);
    group->m_rbtree_root = NULL;
    group->m_min_node = NULL;
    XMutex_unlock(group->m_mutex);

    XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(group);
}

// --- 核心添加逻辑 ---
static XHandle internal_add_timer(XHrTimerGroup* group, uint64_t timeout_ns, uint64_t interval_ns, bool is_single_shot, XTimerCallback callback, void* user_data) {
    if (callback == NULL || ((interval_ns == 0) && (timeout_ns == 0))) {
        return NULL;
    }

    uint64_t current_time_ns = get_current_time_ns(group);
    uint64_t expire_time_ns = current_time_ns + timeout_ns;
    if (timeout_ns == 0) {
        expire_time_ns = current_time_ns + interval_ns;
    }

    XHrTimerNodeData node_data = { 0 };
    node_data.m_expire_time_ns = expire_time_ns;
    node_data.m_timer_data.timerId = 0;
    node_data.m_timer_data.m_timeout = timeout_ns;
    node_data.m_timer_data.m_interval = interval_ns;
    node_data.m_timer_data.m_isSingleShot = is_single_shot;
    node_data.m_timer_data.m_timerCallback = callback;
    node_data.m_timer_data.m_userData = user_data;

    XRBTreeNode* new_node = NULL;
    XMutex_lock(group->m_mutex);
    {
        new_node = XRBTree_insert(&group->m_rbtree_root, compare_expire_time_ns, less_expire_time_ns, &node_data, sizeof(XHrTimerNodeData));
        if (new_node) {
            XAtomic_fetch_add_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
            // --- 关键：插入后更新最小节点 ---
            // 如果新节点的到期时间小于或等于当前最小节点（或当前无最小节点），则更新
            if (group->m_min_node == NULL ||
                node_data.m_expire_time_ns <= ((XHrTimerNodeData*)XRBTree_getData(group->m_min_node))->m_expire_time_ns) {
                group->m_min_node = new_node;
            }
        }
    }
    XMutex_unlock(group->m_mutex);

    return (XHandle)new_node;
}

// --- 删除定时器 (已修正作用域和指针访问) ---
static bool VXTimerGroupBase_removeTimer(XHrTimerGroup* group, XHandle handle) {
    XRBTreeNode* node_to_remove = (XRBTreeNode*)handle;
    if (!node_to_remove) return false;

    XHrTimerNodeData* node_data = (XHrTimerNodeData*)XRBTree_getData(node_to_remove);
    if (!node_data) return false;

    // --- 修正：将 found 变量声明移到此处 ---
    XRBTreeNode* found = NULL;

    XMutex_lock(group->m_mutex);
    {
        // 检查被删除的节点是否是最小节点
        bool was_min_node = (node_to_remove == group->m_min_node);

        found = XRBTree_remove(&group->m_rbtree_root, compare_expire_time_ns, equal_expire_time_and_id, node_data, sizeof(XHrTimerNodeData));
        if (found) {
            XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);

            // --- 关键：如果删除的是最小节点，则需要重新计算 ---
            if (was_min_node) {
                update_min_node(group);
            }

            delete_hr_timer_node(group, found);
        }
    }
    XMutex_unlock(group->m_mutex);

    return (found != NULL);
}

// --- 核心滴答处理函数 (最终修正版) ---
static void VXHrTimerGroup_tick(XHrTimerGroup* group) {
    uint64_t current_time_ns = get_current_time_ns(group);

    XRBTreeNode* expired_list_head = NULL;
    XRBTreeNode* expired_list_tail = NULL;

    XMutex_lock(group->m_mutex);
    {
        // --- O(1) 访问最小节点 ---
        while (group->m_min_node != NULL) {
            XHrTimerNodeData* min_data = (XHrTimerNodeData*)XRBTree_getData(group->m_min_node);
            if (min_data->m_expire_time_ns <= current_time_ns) {
                // --- 关键修正：使用 XRBTree_removeNode 分离节点，并正确处理其返回值 ---
                XRBTreeNode* detached_node = XRBTree_removeNode(
                    &group->m_rbtree_root,
                    compare_expire_time_ns,
                    equal_expire_time_and_id,
                    group->m_min_node,
                    sizeof(XHrTimerNodeData)
                );

                // XRBTree_removeNode 可能会因为内部数据交换而返回一个不同的节点指针。
                // 我们需要操作的是这个返回的节点。
                if (detached_node == NULL) {
                    // 理论上不应该发生，但安全起见
                    break;
                }

                if (expired_list_head == NULL) {
                    expired_list_head = detached_node;
                    expired_list_tail = detached_node;
                }
                else {
                    XBTreeNode_SetRChild(expired_list_tail, detached_node);
                    expired_list_tail = detached_node;
                }
                // 断开原右子树链接
                XBTreeNode_SetRChild(detached_node, NULL);

                // --- 移除后立即更新最小节点 ---
                update_min_node(group);
            }
            else {
                break; // 没有更多过期的定时器
            }
        }
    }
    XMutex_unlock(group->m_mutex);

    // 处理所有过期的定时器
    XRBTreeNode* current = expired_list_head;
    while (current) {
        XRBTreeNode* next = (XRBTreeNode*)XBTreeNode_GetRChild(current);
        XHrTimerNodeData* timer_data = (XHrTimerNodeData*)XRBTree_getData(current);

        XTimerData_out(&timer_data->m_timer_data);

        // 如果是周期性定时器，重新加入
        if (!timer_data->m_timer_data.m_isSingleShot && timer_data->m_timer_data.m_interval > 0) {
            timer_data->m_expire_time_ns = current_time_ns + timer_data->m_timer_data.m_interval;

            XMutex_lock(group->m_mutex);
            XRBTreeNode* reinserted = XRBTree_insert(&group->m_rbtree_root, compare_expire_time_ns, less_expire_time_ns, timer_data, sizeof(XHrTimerNodeData));
            if (reinserted) {
                // --- 重新插入后，检查是否成为新的最小节点 ---
                if (group->m_min_node == NULL ||
                    timer_data->m_expire_time_ns <= ((XHrTimerNodeData*)XRBTree_getData(group->m_min_node))->m_expire_time_ns) {
                    group->m_min_node = reinserted;
                }
            }
            XMutex_unlock(group->m_mutex);
        }
        else {
            delete_hr_timer_node(group, current);
            XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
        }

        current = next;
    }
}

// --- 虚函数：为了兼容性保留 ---
static XHandle VXTimerGroupBase_addTimerMs(XHrTimerGroup* group, XTimerData data) {
    if (!group) return NULL;
    return internal_add_timer(group, data.m_timeout * 1000000ULL, data.m_interval * 1000000ULL, data.m_isSingleShot, data.m_timerCallback, data.m_userData);
}
static XHandle VXTimerGroupBase_addTimerNs(XHrTimerGroup* group, XTimerData data) {
    if (!group) return NULL;
    return internal_add_timer(group, data.m_timeout, data.m_interval, data.m_isSingleShot, data.m_timerCallback, data.m_userData);
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
    group->m_mutex = XMutex_create(XLock_Spin);

    ((XTimerGroupBase*)group)->m_max_time = UINT64_MAX;
    ((XTimerGroupBase*)group)->m_min_time = precision_ns;
    ((XTimerGroupBase*)group)->m_high_res_time_func = NULL;
}