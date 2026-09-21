#ifndef XTimeWheelGroup_H
#define XTimeWheelGroup_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XVector.h"
#include"XTimerGroupBase.h"
#include"XTimerData.h"
typedef struct XListSLinked XListSLinked;
typedef struct XListSNode XListSNode;
typedef struct XVector XVector;
typedef struct XTimeWheelGroup XTimeWheelGroup;
#define XTIMEWHEELGROUP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimeWheelGroup))       //XTimeGroupWheel虚函数表大小
XCLASS_DEFINE_BEGING(XTimeWheelGroup)
//XCLASS_DEFINE_ENUM(XTimeWheelGroup, Add_TimeWheel) = XCLASS_VTABLE_GET_SIZE(XTimerGroupBase),
XCLASS_DEFINE_EXTEND_END(XTimeWheelGroup, XTimerGroupBase)
//定时器轮组    -当前设计仅支持毫秒
typedef struct XTimeWheelGroup
{
	XTimerGroupBase m_class;//继承
	XVector m_timeWheel;//多时间轮	
	XAtomic_size_t m_count;//正在管理的定时器数量
	XAtomic_size_t m_activeProducers;//正在访问槽头的生产者数量，用于安全回收
	XAtomic_bool m_hasCancelledTimers;//存在已取消节点，消费者下一轮统一清扫
	XListSNode* m_retiredNodes;//等待无生产者时回收的节点
	//XMutex* m_mutex;
}XTimeWheelGroup;
XVtable* XTimeWheelGroup_class_init();
XTimeWheelGroup* XTimeWheelGroup_create_ex(XMemoryType memory,  uint16_t precision);
void XTimeWheelGroup_init(XTimeWheelGroup* group, uint16_t precision);
void XTimeWheelGroup_addTimeWheel(XTimeWheelGroup* group,size_t slotsCount);
void XTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group);
size_t XTimeWheelGroup_count(XTimeWheelGroup* group);
/**
 * @brief 查询组内最近一个未取消定时器的绝对到期时刻（无锁只读，§23.4 规划 5）。
 * @details 时间轮当前设计仅支持毫秒精度，到期刻度即 Unix 纪元毫秒；为与
 *          XHrTimerGroup_getNextExpireTime 同口径比较，此处换算为纳秒返回，
 *          供事件分发器把普通定时器并入阻塞等待的最近截止。可与时间轮消费
 *          线程并发调用：遍历持有 m_activeProducers 护栏，消费者对退休节点
 *          的回收在护栏计数非零时推迟，遍历不会读到已释放内存；并发清扫对
 *          链表的瞬时改写最多导致漏看个别节点（下一次查询自愈），对等待
 *          超时属可容忍的近似。
 * @param group 定时器轮组指针，可为 NULL。
 * @return 最近到期时刻（纳秒，Unix 纪元）；组为空或全部已取消返回 UINT64_MAX。
 */
uint64_t XTimeWheelGroup_getNextExpireTime(XTimeWheelGroup* group);
#define XTimeWheelGroup_addTimerMs_base				XTimerGroupBase_addTimerMs
#define XTimeWheelGroup_removeTimer_base			XTimerGroupBase_removeTimer_base
#define XTimeWheelGroup_timeRange					XTimerGroupBase_timeRange
#define XTimeWheelGroup_min_time					XTimerGroupBase_min_time
#define XTimeWheelGroup_max_time					XTimerGroupBase_max_time
#define XTimeWheelGroup_tick_base					XTimerGroupBase_tick_base
#define XTimeWheelGroup_handler_base				XTimerGroupBase_handler_base
#define XTimeWheelGroup_clear_base					XTimerGroupBase_clear_base
#define XTimeWheelGroup_deleteLater					XTimerGroupBase_deleteLater

//全局时间轮
XTimeWheelGroup*XTimeWheelGroup_global();
//全局是否存在
bool XTimeWheelGroup_GlobalExists(void);
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTimeWheelGroup_create
#define XTimeWheelGroup_create(...) XTimeWheelGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // !XTimeWheelGroup_H
