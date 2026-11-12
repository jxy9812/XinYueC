#include "XPrintf.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XThread.h"
#include "XSetBase.h"
#include "XCoreApplication.h"
#include "XSignalSlot.h"
#include "XThreadData.h"
#include "XMapBase.h"
#include "XHashMap.h"
#include "XTimer.h"
#include "XStack.h"
#include "XVariant.h"
#include "XAbstractEventDispatcher.h"
#include "XVector.h"
/* 正则版 findChild/findChildren 的实现依赖完整类型（对标 Qt：qobject.h:41
 * 只前向声明 QRegularExpression，qobject.cpp 才 include
 * qregularexpression.h；本文件同构）。 */
#include "XRegularExpression.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static void VXObject_deinit(XObject* object);
static bool VXObject_event(XObject* self, XEvent* e);
static bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event);
static void VXObject_timerEvent(XObject* timer, XTimerEvent* event);
static void VXObject_copy(XClass* object, const XClass* src);
static void VXObject_move(XClass* object, XClass* src);

void VXObject_deinit(XObject* object)
{
	/* 【threadData 提前取快照 2026-11-08】原实现在函数**末尾**才
	 * `XAtomic_load(&object->m_threadData)` 再 deref。但 deinit 中段会
	 * 级联删除子对象、发 destroyed 信号、清信号槽——任何一环若因既有
	 * 双删/堆踩踏把本对象内存写坏，末尾读到的就是垃圾指针：
	 * 实测 HsfClient 退出恒 AV，崩溃指令是 `mov eax,[rax+50h]`（即
	 * XThreadData_deref 里读 data->m_ref），rax=0x000000060000000C
	 * 这种"两个 32 位整数拼成"的脏值。
	 * 在本函数**开头**（对象内存必然完好）就把 td 取出存进局部变量，
	 * 末尾直接用局部变量 deref——把"读脏内存"的时间窗口压到零。
	 * 【2026-11-08 修订】槽位清空（store NULL）必须留到**末尾**：deinit
	 * 中段的 XObject_thread()/XObject_eventDispatcher() 都要经
	 * m_threadData 取线程亲和性，用于 XCoreApplication_removePostedEvents
	 * 守卫与定时器注销。若在开头就把槽位清空，XObject_thread() 恒返回
	 * NULL → removePostedEvents 被整段跳过，对象的 posted 事件留在队列里
	 * 带悬垂 m_receiver；下一次 processEvents 解引用
	 * m_receiver->m_is_deleting_children（[rax+1Ch] bit3）即 AV（实测
	 * XGuiRegression_Test 稳定 segfault 于 test_layout_updategeometry_qt_align）。
	 * 故：只提前"读快照"，不提前"清槽位"；清槽位与 deref 一并留到末尾。 */
	XThreadData* deinitTd = NULL;
	if (!object || object->m_was_deleted)
		return;

	deinitTd = (XThreadData*)XAtomic_load_uintptr_t(
		(XAtomic_Uintptr*)&object->m_threadData, XAtomic_MemoryOrder_Acquire);

	object->m_was_deleted = true;
	object->m_block_sig = 0;
	if (XObject_thread(object) == XThread_currentThread())
		XCoreApplication_removePostedEvents(object, XEVENT_TYPE_NONE);
	/* 析构期 sender() 会话清理（对标 Qt ~QObject 里把 currentSender 的
	 * receiver 置空）：本对象可能是某个**外层发射**的接收者，若它现在被销毁，
	 * 发送者栈上那一帧会留下已释放的 receiver 地址；同地址被新对象复用时，
	 * 新对象在槽里调 sender() 会命中别人的旧帧。必须在发 destroyed 之前清，
	 * 否则 destroyed 的槽里若再查 sender() 也会命中墓碑帧。 */
	XThreadData_clearSenderFrames(object);
	/* 注销本对象在事件分发器上的全部定时器（对标 Qt ~QObject 经
	 * QAbstractEventDispatcherPrivate::removeTimerInfo 的对象级清理）：
	 * 定时器回调在计时线程异步触发，若对象先亡而轮表项残留，回调将
	 * 向已释放内存 postEvent（实测堆 UAF 崩溃）。分发器必须取本对象
	 * threadData 所属线程的（XObject_eventDispatcher，与
	 * startTimer/killTimer 同源）——XThread 为每个工作线程建独立
	 * dispatcher，XCoreApplication_eventDispatcher 只返回主线程的，
	 * 跨线程对象会注销错表、真表残留悬垂指针。 */
	{
		XAbstractEventDispatcher* disp = XObject_eventDispatcher(object);
		if (disp)
			XAbstractEventDispatcher_unregisterTimers_base(disp, object);
	}
	XObject_destroyed_signal(object);

	object->m_is_deleting_children = true;
	if(object->m_children)
	{
		/* [2026-11-05 快照式析构] 原实现边遍历边删：XClassDelete(child) 会
		 * 跑到 child 的 VXObject_deinit，那里可能（a）把本对象当 parent 再摘
		 * 一次链、（b）触发延迟删除事件的回收、（c）重入 setParent 往
		 * m_children 里塞新条目。三条都会让 m_children 的**元素个数与内存
		 * 在循环中途变化**，于是：
		 *   · XContainerDataAddr 每轮重取 → 可能拿到重分配后的新基址；
		 *   · 更糟的是槽位被别的 child 覆盖 → 同一 child 被删两次，
		 *     第二次读到 0xDDDDDDDD 填充即 AV。
		 * 实测症状就是**退出时随机崩**（hsf-io-flowmeter 30 次崩 3 次，
		 * 栈恒为 VXObject_deinit → XClass_delete_base → XClass_deinit_base）。
		 * 改成先把指针搬到局部数组、把容器清空，再逐个删——循环期间
		 * 父容器不再被任何人看到，重入也伤不到它。 */
		const size_t n = XContainerSize(object->m_children);
		XObject** snap = n ? (XObject**)XMemory_malloc(sizeof(XObject*) * n,
		                                              XCLASS_DEFAULT_MEMORY_TYPE)
		                   : NULL;
		size_t i;
		for (i = 0; i < n; ++i) {
			XObject** childPtr = (XObject**)XContainerDataAddr(object->m_children);
			snap[i] = childPtr[i];
			childPtr[i] = NULL;
		}
		/* 先把表清空：重入 setParent 塞进来的新 child 会活在**新**表里，
		 * 不会再被下面的遍历误伤。 */
		XClassDelete(object->m_children);
		object->m_children = NULL;
		for (i = 0; i < n; ++i) {
			XObject* child = snap[i];
			size_t j;
			bool seen = false;
			if (!child) continue;
			/* 同一指针重复登记（历史 setParent 的 indexOf 漏查）：只删一次。 */
			for (j = 0; j < i; ++j) {
				if (snap[j] == child) { seen = true; break; }
			}
			if (seen) continue;
			object->m_currentChildBeingDeleted = child;
			child->m_parent = NULL;
			if (Class_IsHeap(child))
				XClassDelete(child);
			else
				XClassDeinit(child);
		}
		if (snap) XMemory_free(snap, XCLASS_DEFAULT_MEMORY_TYPE);
	}
	object->m_currentChildBeingDeleted = NULL;
	object->m_is_deleting_children = false;

	if (object->m_parent) {
		XObject* parent = object->m_parent;
		object->m_parent = NULL;
		if (parent->m_children && !parent->m_is_deleting_children) {
			int index = XVector_indexOf(parent->m_children, &object, 0);
			if (index != -1) {
				XVector_remove_base(parent->m_children, index, 1);
				/* [2026-11-05 重复登记清尾] 万一历史 setParent 留下了同一指针
				 * 的多份登记（indexOf 漏查/重入 setParent 都会导致），只删第一个
				 * 会在表里留下**悬垂指针**：父析构时对该地址再 XClassDelete 一次，
				 * 读到 0xDDDDDDDD 填充即 AV（实测 hsf-io-flowmeter 退出随机崩）。
				 * 这里把同一指针的其余登记一并清掉，保证"对象死了表里就没有它"。 */
				for (;;) {
					int again = XVector_indexOf(parent->m_children, &object, 0);
					if (again == -1) break;
					XVector_remove_base(parent->m_children, again, 1);
				}
			}
			if (index != -1 && object->m_send_child_events && parent->m_receive_child_events) {
				XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object);
				if (event) {
					XCoreApplication_sendEvent(parent, (XEvent*)event);
					XClassDelete((XEvent*)event);
				}
			}
		}
	}

	/* 反注册事件过滤器双向登记（防悬挂引用）：本对象作为被观察者时，从各
	 * 过滤器的回登记表 m_watchedObjects 中移除自己；本对象作为过滤器时，从
	 * 各被观察对象的 m_filters 中移除自己。两侧对称维护后，m_filters 中的
	 * 过滤器指针恒为存活对象，事件分发（XCoreApplication_notify）不再触碰
	 * 已释放内存——此前过滤器先亡会在被观察对象表中残留悬挂指针。 */
	if (object->m_filters)
	{
		for (size_t i = 0; i < XContainerSize(object->m_filters); ++i)
		{
			XObject** filterPtr = (XObject**)XContainerDataAddr(object->m_filters);
			XObject* filter = filterPtr[i];
			if (filter && filter->m_watchedObjects)
			{
				int index = XVector_indexOf(filter->m_watchedObjects, &object, 0);
				if (index != -1)
					XVector_remove_base(filter->m_watchedObjects, index, 1);
			}
		}
		XClassDelete(object->m_filters);
		object->m_filters = NULL;
	}
	if (object->m_watchedObjects)
	{
		for (size_t i = 0; i < XContainerSize(object->m_watchedObjects); ++i)
		{
			XObject** watchedPtr = (XObject**)XContainerDataAddr(object->m_watchedObjects);
			XObject* watched = watchedPtr[i];
			if (watched && watched->m_filters)
			{
				int index = XVector_indexOf(watched->m_filters, &object, 0);
				if (index != -1)
					XVector_remove_base(watched->m_filters, index, 1);
			}
		}
		XClassDelete(object->m_watchedObjects);
		object->m_watchedObjects = NULL;
	}
	if (object->m_object_name)
	{
		XClassDelete(object->m_object_name);
		object->m_object_name = NULL;
	}
	//释放信号与槽
	if (object->m_signalSlot)
	{
		XSignalSlot_delete(object->m_signalSlot);
		object->m_signalSlot = NULL;
	}
	if (object->m_dynamicPropertyNames)
	{
		for (int i = 0; i < (int)XContainerSize(object->m_dynamicPropertyNames); i++) {
			XString** namePtr = (XString**)XContainerDataAddr(object->m_dynamicPropertyNames);
			if (namePtr[i]) {
				XClassDelete(namePtr[i]);
			}
		}
		XClassDelete(object->m_dynamicPropertyNames);
		object->m_dynamicPropertyNames = NULL;
	}
	if (object->m_dynamicPropertyValues)
	{
		XVariant** values = (XVariant**)XContainerDataAddr(object->m_dynamicPropertyValues);
		for (size_t i = 0; i < XContainerSize(object->m_dynamicPropertyValues); ++i) {
			if (values[i])
				XClassDelete(values[i]);
		}
		XClassDelete(object->m_dynamicPropertyValues);
		object->m_dynamicPropertyValues = NULL;
	}
	// Qt 6.8: ~QObjectPrivate() - 释放线程亲和性引用 (对标 thisThreadData->deref())
	// 放在所有子对象/信号槽清理之后: 清理过程可能仍需通过 threadData 访问当前线程。
	// XObject_init 时 ref, moveToThread 时平衡 (ref 新/deref 旧), 析构时此处 deref 收尾。
	// 快照在函数开头已取（见上方说明）：这里只用局部变量，不再读对象内存。
	// 清槽位留在此处（deinit 中段仍需 m_threadData 判线程亲和性，见开头说明）。
	if (deinitTd) {
		XAtomic_store_uintptr_t(&object->m_threadData, (uintptr_t)NULL,
			XAtomic_MemoryOrder_Release);
		XThreadData_deref(deinitTd);
	}
}
/* ==================== ThreadChange 定时器迁移（对标 Qt 6.8 QObject::event） ====================
 *
 * Qt 原文（qobject.cpp:1422-1443）：
 *   case QEvent::ThreadChange: {
 *       QThreadData *threadData = d->threadData.loadRelaxed();
 *       QAbstractEventDispatcher *eventDispatcher = threadData->eventDispatcher.loadRelaxed();
 *       if (eventDispatcher) {
 *           QList<...TimerInfoV2> timers = eventDispatcher->timersForObject(this);
 *           if (!timers.isEmpty()) {
 *               const bool res = eventDispatcher->unregisterTimers(this);
 *               // do not to release our timer ids back to the pool (since the timer ids are moving to a new thread).
 *               auto reRegisterTimers = [this, timers = std::move(timers)]() {
 *                   ...eventDispatcher->registerTimer(ti.timerId, ti.interval, ti.timerType, this); };
 *               QMetaObject::invokeMethod(this, std::move(reRegisterTimers), Qt::QueuedConnection);
 *           }
 *       }
 *       break;
 *   }
 *
 * 三件事必须同批做，缺一条定时器就丢：
 *   ① **收集**：`timersForObject` 快照（interval/timerId/timerType）；
 *   ② **摘除但保留 id**：`XAbstractEventDispatcher_detachTimers`（X 的 id 就是
 *      `XFd` 槽位，「不归还池子」= 不 `XFd_free`）；
 *   ③ **在新线程重注册**：Qt 用 QueuedConnection 的 invokeMethod 把这一步推到
 *      目标线程的事件循环里——本框架用 `XCoreApplication_postEvent` 投递一个
 *      `XEVENT_TYPE_FUNC_RUN` 事件到**对象自身**：事件在 ThreadChange 期间入队
 *      （对象仍是旧线程），随后 `XObject_setThreadData_helper` 会把它随对象一起
 *      迁移到目标线程的投递队列，待目标线程跑事件循环时才兑现（时机与 Qt 的
 *      QueuedConnection 完全一致，且不需要跨线程直接碰目标调度器的内部表）。
 */

/** @brief ThreadChange 迁移载荷：待重注册的定时器快照 + 目标对象。 */
typedef struct XObjectTimerMigration
{
    XObject* object;    /**< 借用：接收者自身（重注册的归属对象） */
    XVector* timers;    /**< 拥有：元素为 XAbstractEventDispatcher_TimerInfoV2 */
} XObjectTimerMigration;

/** @brief 载荷回收：事件被兑现**或**被丢弃（对象先亡）时都经 argsDel 走到这里。 */
static void XObject_timerMigration_free(XVarList* args)
{
    if (!args) return;
    {
        XVarList_args_1(args, XObjectTimerMigration*, ctx);
        if (!ctx) return;
        if (ctx->timers)
            XClassDelete(ctx->timers);
        XMemory_free((void*)ctx, XCLASS_DEFAULT_MEMORY_TYPE);
    }
}

/** @brief 兑现：在**目标线程**用显式 id 把定时器重新布防。 */
static void XObject_timerMigration_reregister(XVarList* args)
{
    XAbstractEventDispatcher* disp;
    if (!args) return;
    {
        XVarList_args_1(args, XObjectTimerMigration*, ctx);
        size_t i, n;
        if (!ctx || !ctx->object || !ctx->timers) return;
        /* 与 XObject_startTimer 同源取调度器（该对象此刻所在线程的），保证
         * 「重注册后的定时器」与「本线程 startTimer 起的定时器」落在同一张表。 */
        disp = XObject_eventDispatcher(ctx->object);
        if (!disp) return;
        n = XContainerSize(ctx->timers);
        for (i = 0; i < n; ++i) {
            const XAbstractEventDispatcher_TimerInfoV2* info =
                (const XAbstractEventDispatcher_TimerInfoV2*)XVector_at_base(ctx->timers, i);
            if (!info) continue;
            if (info->timerId == XTIMER_INVALID_ID) continue;
            XAbstractEventDispatcher_registerTimer_base(disp, info->timerId, info->interval,
                                                        info->timerType, ctx->object);
        }
    }
}

/**
 * @brief 把「稍后按显式 id 重注册」排进对象的投递队列。
 * @param self 接收者（ThreadChange 期间仍是旧线程）。
 * @param timers 定时器快照（**成功即转移所有权**；失败仍归调用方）。
 * @return true 已入队（ownership 已转走）。
 * @note `XCoreApplication_postEvent` 无返回值；其失败路径会就地删除事件，
 *       argsDel 随之回收 ctx 与快照。此时调用方仍会摘除旧调度器上的定时器——
 *       与修复前「定时器留在旧线程」相比不再更差，且只在投递内存不足时发生。
 */
static bool XObject_postTimerMigration(XObject* self, XVector* timers)
{
    XObjectTimerMigration* ctx;
    XVarList* args;
    XEventFunc* ev;
    if (!self || !timers) return false;
    ctx = (XObjectTimerMigration*)XMemory_malloc(sizeof(XObjectTimerMigration),
                                                 XCLASS_DEFAULT_MEMORY_TYPE);
    if (!ctx) return false;
    ctx->object = self;
    ctx->timers = timers;
    args = XVarList_Create(XVar(XObjectTimerMigration*, ctx));
    if (!args) {
        XMemory_free(ctx, XCLASS_DEFAULT_MEMORY_TYPE);
        return false;
    }
    ev = XEventFunc_create(XObject_timerMigration_reregister, args,
                           XObject_timerMigration_free);
    if (!ev) {
        XVarList_delete(args);   /* argsDel 尚未挂上：列表自身回收，ctx 由下方释放 */
        XMemory_free(ctx, XCLASS_DEFAULT_MEMORY_TYPE);
        return false;
    }
    XCoreApplication_postEvent(self, (XEvent*)ev, XEVENT_PRIORITY_NORMAL);
    return true;
}

bool VXObject_event(XObject* self, XEvent* e)
{
	if (!self || !e) {
		return false; // 安全防护：空指针直接忽略
	}
	/* 返回值严格对齐 Qt 6.8 `QObject::event`（qobject.cpp）：Timer / Child* /
	 * DeferredDelete / MetaCall / ThreadChange 分支处理完 `return true`；
	 * `default` 分支里 **User 及以上** 交 customEvent 后 `break`（→ true），
	 * 其余一律 `return false`。
	 *
	 * 旧实现在函数末尾 `return e->m_accepted;` —— 那是把「事件被 accept 过」
	 * 当成「event() 认领了事件」。Qt 里两者无关：accepted 缺省为 true
	 * （qcoreevent.h:344），而未识别类型的 event() 返回 false。若继续按旧写法，
	 * accepted 缺省翻成 true 后所有未识别事件都会被误判为"已处理"（父链不再
	 * 收到、drag/手势回退链路改变）。 */
	switch (e->m_type)
	{
		case XEVENT_TYPE_TIMER: XObject_timerEvent_base(self, (XTimerEvent*)e); return true;
		case XEVENT_TYPE_CHILD_ADDED:
		case XEVENT_TYPE_CHILD_POLISHED:
		case XEVENT_TYPE_CHILD_REMOVED:
			/* Qt 6.8 QObject::event：Child 系列事件先派发 **childEvent 虚函数**，
			 * 再走默认处理。修复前这里只做 XChildEvent_handler（仅 accept），
			 * XObject_childEvent_base 全仓 0 处引用 ⇒ 子类重载 childEvent
			 * 永远收不到（Qt 实测 new QObject(&parent) 触发 1 次 ChildAdded，
			 * delete 触发 1 次 ChildRemoved）。 */
			XObject_childEvent_base(self, (XChildEvent*)e);
			XChildEvent_handler(e, self);
			return true;
		case XEVENT_TYPE_META_CALL: XMetaCallEvent_handler(e, self); return true;
		case XEVENT_TYPE_FUNC_RUN: XEventFunc_handler( e); return true;
		case XEVENT_TYPE_DEFERRED_DELETE: XDeferredDeleteEvent_handler((XDeferredDeleteEvent*)e, self); return true;
		case XEVENT_TYPE_THREAD_CHANGE: {
			/* Qt 6.8 QObject::event 的 ThreadChange 分支（qobject.cpp:1422-1443）：
			 * 对象换线程前把**活定时器**从旧调度器摘除（id 不归还池子），并在
			 * 新线程用显式 id 重注册——详见本函数上方的机制说明。修复前这里只
			 * `XEvent_accept` + return：定时器留在旧线程的调度器上，旧线程一旦
			 * 停转就不再触发，迁移后 killTimer 也打不到旧设备。 */
			XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
			if (disp) {
				XVector* timers = XAbstractEventDispatcher_timersForObject_base(disp, self);
				if (timers && XContainerSize(timers) > 0) {
					if (XObject_postTimerMigration(self, timers))
						(void)XAbstractEventDispatcher_detachTimers(disp, self);
					else
						XClassDelete(timers);   /* 投递失败：定时器保持原样，不摘 */
				} else if (timers) {
					XClassDelete(timers);       /* 没有活定时器：空快照直接回收 */
				}
			}
			XEvent_accept(e);
			return true;
		}
		default:
			/* Qt 6.8 QObject::event 的 default 分支：
			 * `if (e->type() >= QEvent::User) { customEvent(e); break; } return false;`
			 * （qobject.h:114 声明 customEvent；Qt 实测 User+7 事件被派发 1 次）。
			 * 修复前本函数无 default 分支，XObject_customEvent_base 全仓 0 处
			 * 引用 ⇒ 用户重载 customEvent 永远收不到。 */
			if (e->m_type >= XEVENT_TYPE_USER && e->m_type <= XEVENT_TYPE_MAX_USER) {
				XObject_customEvent_base(self, e);
				return true;
			}
			return false;
	}
}

bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event)
{
	return false;
}
void VXObject_timerEvent(XObject* self, XTimerEvent* event)
{
	XEvent_accept((XEvent*)event);
}

/* Qt 6.8：QObject 不可拷贝（qobject.h 里 `Q_DISABLE_COPY(QObject)`，拷贝构造
 * 与赋值运算符均被删除）——对象树、事件过滤器、信号槽连接表达的是**对象身份**，
 * 浅拷贝会产生"两个对象共用同一份子表/连接表" ⇒ 双释放。C 侧没有编译期删除
 * 拷贝的手段，故在两个虚函数槽位上显式报错并保持目标对象不变：
 * 修复前继承的是 XClass 的空实现，`XClassCopy(&b, &a)` 会**静默什么都不做**，
 * 调用方以为拿到了副本。 */
static void VXObject_copy(XClass* object, const XClass* src)
{
	(void)object;
	(void)src;
	XERROR_PRINTF("XObject::copy: QObject 不可拷贝（对标 Qt Q_DISABLE_COPY(QObject)）\n");
}

static void VXObject_move(XClass* object, XClass* src)
{
	(void)object;
	(void)src;
	XERROR_PRINTF("XObject::move: QObject 不可移动构造（对标 Qt Q_DISABLE_COPY(QObject)）\n");
}

XVtable* XObject_class_init()
{
	XVTABLE_INIT_DEFAULT(XObject)
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXObject_event ,VXObject_eventFilter,
	NULL,NULL,NULL,NULL,VXObject_timerEvent };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXObject_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXObject_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXObject_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XObject);
	return XVTABLE_DEFAULT;
}

XObject* XObject_create_ex(XMemoryType memory)
{
	XObject* object = XMemory_malloc(sizeof(XObject), memory);
	if (!object)
		return NULL;
	XObject_init(object);
	Set_Class_Memory(object, memory); Set_Class_IsHeap(object, true);
	return object;
}

/* Qt 6.8：`explicit QObject(QObject *parent = nullptr)` 的 C 映射。
 * Qt 的构造函数在对象初始化末尾调 setParent；跨线程父对象时 Qt 同样
 * "对象建出来但不挂父"（setParent 内部告警后 return）。 */
XObject* XObject_createWithParent_ex(XMemoryType memory, XObject* parent)
{
	XObject* object = XObject_create_ex(memory);
	if (object && parent)
		XObject_setParent(object, parent);
	return object;
}

void XObject_init(XObject* object)
{
	if (object == NULL)
		return;
	memset(((XClass*)object)+1,0,sizeof(XObject)-sizeof(XClass));
	XClass_init(object);
	XClassGetVtable(object) = XObject_class_init();
	XAtomic_init(object->m_threadData, 0);
	XThreadData* threadData = XThreadData_current();
	if (threadData) {
		XThreadData_ref(threadData);
		XAtomic_store_uintptr_t(&object->m_threadData, (uintptr_t)threadData,
			XAtomic_MemoryOrder_Release);
	}
	object->m_send_child_events = true;
	object->m_receive_child_events = true;
	XAtomic_init(object->m_posted_events, 0);
}

const XString* XObject_objectName(const XObject* self)
{
	if (!self)return NULL;
	return self->m_object_name;
}

void XObject_setObjectName(XObject* self, const XString* name)
{
	if (!self)return;
	/* NULL = 清空（对标 Qt `setObjectName(QString())`：本机实测清空后
	 * objectName().isNull()==true，且**发**一次 objectNameChanged）。
	 * 旧实现把 NULL 当 no-op（C 的空指针惯例），与 Qt 相反。 */
	if (!name || XString_length_base(name) == 0) {
		if (!self->m_object_name)
			return;                 /* 本来就是无名：Qt 同值不发信号 */
		/* 先落地再发信号（Qt 的 setObjectName 是 `objectName = name; emit ...`：
		 * 槽里读 objectName() 已经是新值）。X 的"清空"= 删除名称对象。 */
		XClassDelete((XClass*)self->m_object_name);
		self->m_object_name = NULL;
		{
			XString empty;
			XString_init(&empty);
			XObject_objectNameChanged_signal(self, &empty);
			XClassDeinit(&empty);
		}
		return;
	}
	if (!self->m_object_name) {
		self->m_object_name = XString_create();
		if (!self->m_object_name)
			return;
	}
	if(XString_compare(self->m_object_name, name)!=XCompare_Equality)
	{
		XString_assign(self->m_object_name, name);
		XObject_objectNameChanged_signal(self, self->m_object_name);
	}
}

bool XObject_isSignalConnected(const XObject* self, size_t signal)
{
	if (!self) return false;
	return XSignalSlot_isSignalConnected(self->m_signalSlot,signal);
}

int XObject_receivers(const XObject* self, size_t signal)
{
	if (!self) return 0;
	return XSignalSlot_receivers(self->m_signalSlot, signal);
}

void XObject_setParent(XObject* object, XObject* parent)
{
	// 健壮性检查：空对象或已删除的对象不应再设置父对象
	if (!object || object->m_was_deleted) {
		return;
	}

	// 如果新父对象就是当前父对象，直接返回
	if (object->m_parent == parent) {
		return;
	}
	if (object == parent)
		return;
	for (XObject* ancestor = parent; ancestor; ancestor = ancestor->m_parent) {
		if (ancestor == object)
			return;
	}

	XThreadData* objectData = XObject_threadData(object);
	if (XThreadData_current() != objectData) {
		/* X 侧附加守卫（Qt 的 setParent 只查"新旧父线程是否一致"，没有这一条）：
		 * 对象的事件/定时器都挂在创建线程上，从别的线程改父子关系会让
		 * m_children 在无锁状态下被两线程同时改。
		 * 位置与 Qt 一致：在**最前**——对象树状态一个字节都没动就拒绝。 */
		XPrintf("XObject::setParent: Current thread is not the object's thread\n");
		return;
	}

	XObject* prev_parent = object->m_parent;
	if (prev_parent && prev_parent->m_children) {
		int index = XVector_indexOf(prev_parent->m_children, &object, 0);
		if (index != -1 && prev_parent->m_is_deleting_children) {
			XObject** children = (XObject**)XContainerDataAddr(prev_parent->m_children);
			children[index] = NULL;
		} else if (index != -1) {
			XVector_remove_base(prev_parent->m_children, index, 1);
			if (object->m_send_child_events && prev_parent->m_receive_child_events) {
				XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object);
				if (event) {
					XCoreApplication_sendEvent(prev_parent, (XEvent*)event);
					XClassDelete((XEvent*)event);
				}
			}
		}
	}

	if (object->m_receive_parent_events) {
		XEvent* event = XEvent_create(XEVENT_TYPE_PARENT_ABOUT_TO_CHANGE);
		if (event) {
			XCoreApplication_sendEvent(object, event);
			XClassDelete(event);
		}
	}

	// 设置新的父对象
	object->m_parent = parent;

	// 将自己添加到新父对象的 children 列表中
	if (parent) {
		/* Qt 6.8 `QObjectPrivate::setParent_helper`（qobject.cpp:2269-2275）：
		 * **先落地 parent、再判线程**，失败时 `parent = nullptr; return;`——
		 * 也就是说旧父已经被摘掉（ChildRemoved 与 ParentAboutToChange 都已经
		 * 发过），对象最终是**无父**状态，且不再发 ChildAdded / ParentChange。
		 * 修复前这段校验在摘旧父之前：对象保留原父、一条信号都不发，与 Qt 相反
		 * （Qt 本机实测：跨线程 setParent 后 parent()==nullptr、旧父 children -1）。 */
		if (XObject_threadData(object) != XObject_threadData(parent)) {
			/* Qt 6.8 原文（qobject.cpp:2272）："QObject::setParent: Cannot set
			 * parent, new parent is in a different thread"。 */
			XPrintf("XObject::setParent: Cannot set parent, new parent is in a different thread\n");
			object->m_parent = NULL;
			return;
		}
		if (!parent->m_children) {
			parent->m_children = XVector_create(sizeof(XObject*));
		}
		if (parent->m_children) {
			XVector_push_back_1_base(parent->m_children, &object);
		}
		if (object->m_send_child_events && parent->m_receive_child_events) {
			XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_ADDED, object);
			if (event) {
				XCoreApplication_sendEvent(parent, (XEvent*)event);
				XClassDelete((XEvent*)event);
			}
		}
	}

	// Qt 6.8: 发送 ParentChange 事件给自身
	if (object->m_receive_parent_events) {
		XEvent* event = XEvent_create(XEVENT_TYPE_PARENT_CHANGE);
		if (event) {
			XCoreApplication_sendEvent(object, event);
			XClassDelete(event);
		}
	}
}

const XVector* XObject_children(const XObject* self)
{
	if(!self)return NULL;
	return self->m_children;
}

// Qt 6.8: QStyleSheetStyle::previousSiblingNode - 父容器子表顺序查找上一个相邻兄弟
// (供 QSS 兄弟组合符 '+'/'~' 匹配使用; 子表空位不跳过, 与 Qt children() 相邻语义一致)
XObject* XObject_previousSibling(const XObject* self)
{
	if (!self || !self->m_parent || !self->m_parent->m_children)
		return NULL;
	int64_t index = XVector_indexOf(self->m_parent->m_children, &self, 0);
	if (index <= 0)
		return NULL;
	XObject** children = (XObject**)XContainerDataAddr(self->m_parent->m_children);
	return children[index - 1];
}

// 【XObject_nextSibling 已按台账 P2-01 删除（2026-11-13）】：全仓 0 调用
// （QSS '+'/'~' 只调 previousSibling，回归测试亦无引用）；旧头注释
// 「QSS 与回归测试都用它」与事实相反，订正见 XObject.h 的删除记。

bool XObject_isWidgetType(const XObject* self)
{
	if (!self)return false;
	return self->m_is_widget;
}

bool XObject_isQuickItemType(const XObject* self)
{
	if (!self) return false;
	return self->m_is_quick_item;
}

bool XObject_isWindowType(const XObject* self)
{
	if (!self)return false;
	return self->m_is_window;
}
// Qt 6.8: `QObject *QObject::parent() const` —— 只读，故取 const 入参
XObject* XObject_parent(const XObject* object)
{
	if (object)
		return object->m_parent;
	return NULL;
}

// Qt 6.8: moveToThread_helper - 递归发送 ThreadChange 事件给对象及其所有子对象
// (对标 QObjectPrivate::moveToThread_helper)
static void XObject_moveToThread_helper(XObject* object)
{
	if (!object) return;

	// 发送 ThreadChange 事件 (对标 Qt 6.8 QEvent::ThreadChange)
	XEvent* e = XEvent_create(XEVENT_TYPE_THREAD_CHANGE);
	if (e) {
		XCoreApplication_sendEvent(object, e);
		XClassDelete(e);
	}

	// 递归处理所有子对象
	if (object->m_children) {
		for_each_iterator(object->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			if (child) {
				XObject_moveToThread_helper(child);
			}
		}
	}
}

static size_t XObject_setThreadData_helper(XObject* object,
	XThreadData* currentData, XThreadData* targetData)
{
	if (!object) return 0;

	size_t moved = 0;
	if (currentData != targetData) {
		XPostEvent* events = (XPostEvent*)XContainerDataAddr(&currentData->m_postEventList);
		size_t eventCount = XContainerSize(&currentData->m_postEventList);
		for (size_t i = 0; i < eventCount; ++i) {
			if (events[i].m_event && events[i].m_receiver == object) {
				XPostEvent event = events[i];
				if (XVector_push_back_1_base(&targetData->m_postEventList, &event)) {
					events[i].m_event = NULL;
					++moved;
				} else
					XThreadData_discardPostedEvent(&events[i]);
			}
		}

		XVector** activeLists = (XVector**)XContainerDataAddr(
			&currentData->m_activePostEventLists);
		size_t activeCount = XContainerSize(&currentData->m_activePostEventLists);
		for (size_t i = 0; i < activeCount; ++i) {
			XVector* active = activeLists[i];
			if (!active) continue;
			XPostEvent* activeEvents = (XPostEvent*)XContainerDataAddr(active);
			size_t activeEventCount = XContainerSize(active);
			for (size_t j = 0; j < activeEventCount; ++j) {
				if (activeEvents[j].m_event && activeEvents[j].m_receiver == object) {
					XPostEvent event = activeEvents[j];
					if (XVector_push_back_1_base(&targetData->m_postEventList, &event)) {
						activeEvents[j].m_event = NULL;
						++moved;
					} else
						XThreadData_discardPostedEvent(&activeEvents[j]);
				}
			}
		}
	}

	XThreadData_ref(targetData);
	XThreadData* oldData = (XThreadData*)XAtomic_exchange_uintptr_t(
		&object->m_threadData, (uintptr_t)targetData, XAtomic_MemoryOrder_AcqRel);
	XThreadData_deref(oldData);
	if (object->m_children) {
		for_each_iterator(object->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			moved += XObject_setThreadData_helper(child, currentData, targetData);
		}
	}
	return moved;
}

bool XObject_moveToThread(XObject* object, XThread* target_thread)
{
	// 健壮性检查
	if (!object || object->m_was_deleted) {
		return false;
	}

	// Qt compares the QThread stored in threadData, including NULL affinity.
	if (XObject_thread(object) == target_thread) {
		return true;
	}

	// Qt 6.8: 有父对象的对象不能移动线程
	if (object->m_parent != NULL) {
		/* Qt 实测：返回 false 且告警 "QObject::moveToThread: Cannot move
		 * objects with a parent"（本机 probe 输出）。修复前静默返回，
		 * 现场排障看不出原因。 */
		XPrintf("XObject::moveToThread: Cannot move objects with a parent\n");
		return false;
	}
	if (object->m_is_widget) {
		/* Qt：QWidget 只能留在 GUI 线程（QWidget 重载 moveToThread 直接
		 * 告警拒绝），X 侧以 m_is_widget 标记同口径拒绝。 */
		XPrintf("XObject::moveToThread: Widgets cannot be moved to a new thread\n");
		return false;
	}

	XThreadData* currentData = XThreadData_current();
#if XTHREAD_ON
    XThreadData* targetData = target_thread ? XThreadData_get2(target_thread) : NULL;
#else
    XThreadData* targetData = NULL;
#endif
	XThreadData* thisData = XObject_threadData(object);
	if (!currentData || !thisData)
		return false;

	// Qt permits an affinity-less object to be pulled into the current thread.
	if (!thisData->m_thread && currentData == targetData)
		currentData = thisData;
	else if (thisData != currentData) {
		/* Qt 同样拒绝从非对象所属线程发起移动，并告警
		 * "Current thread is not the object's thread"（qobject.cpp
		 * moveToThread_helper 的 currentThread != selfThread 分支）。 */
		XPrintf("XObject::moveToThread: Current thread is not the object's thread\n");
		return false;
	}

	bool ownsTargetData = false;
	if (!targetData) {
		targetData = XThreadData_create(NULL);
		if (!targetData)
			return false;
		ownsTargetData = true;
	}

	// Qt 6.8: 递归发送 ThreadChange 事件给对象及其所有子对象
	// (对标 QObjectPrivate::moveToThread_helper)
	XObject_moveToThread_helper(object);
	XThreadData_ref(currentData);
	XThreadData* first = (uintptr_t)currentData < (uintptr_t)targetData ? currentData : targetData;
	XThreadData* second = first == currentData ? targetData : currentData;
	XMutex_lock(first->m_mutex);
	XMutex_lock(second->m_mutex);
	size_t moved = XObject_setThreadData_helper(object, currentData, targetData);
	if (moved > 0)
		XAtomic_store_bool(&targetData->m_canWait, false, XAtomic_MemoryOrder_Release);
	XMutex_unlock(second->m_mutex);
	XMutex_unlock(first->m_mutex);
	XThreadData_deref(currentData);
	if (ownsTargetData)
		XThreadData_deref(targetData);

	if (moved > 0 && targetData->m_eventDispatcher)
		XAbstractEventDispatcher_wakeUp_base(targetData->m_eventDispatcher);

	return true;
}

bool XObject_signalsBlocked(const XObject* self)
{
	if(!self)return false;
	return self->m_block_sig;
}

bool XObject_blockSignals(XObject* self, bool block)
{
	if (!self)return false;
	bool state = XObject_signalsBlocked(self);
	self->m_block_sig = block;
	return state;
}

/* ==================== QSignalBlocker（C 版 RAII） ====================
 * Qt 6.8 qobject.h:482-565：`QSignalBlocker b(obj);` 构造时 blockSignals(true)
 * 并记住**构造前**的状态，析构时恢复该状态（不是无条件 false）。C 没有
 * 析构，故提供成对的 init/destroy；嵌套守卫靠"记住前态"自然成立。
 * `_ctor`/`_dtor` 是 Qt 命名别名（保留给既有调用点），行为完全一致。
 * 中途改主意三函数 reblock/unblock/dismiss 逐条对标
 * qobject.h:548-565（本机 Qt 6.8 探针 /tmp/probe_qobject_regex.cpp 实测）：
 *   - unblock() 恢复 m_wasBlocked（进入前状态）并置 inhibit ⇒ dtor 不动对象；
 *   - reblock() 无条件 blockSignals(true) 并清 inhibit ⇒ dtor 再按前态恢复；
 *   - dismiss() 只清 m_object ⇒ 对象保持当前状态、dtor no-op。
 * 结构体字段 m_object / m_wasBlocked / m_inhibited 与 Qt 的
 * m_o / m_blocked / m_inhibited 一一对应。 */
void XSignalBlocker_init(XSignalBlocker* self, XObject* object)
{
	if (!self)
		return;
	self->m_object = object;
	self->m_inhibited = false;   /* 对标 Qt 构造函数 m_inhibited(false) */
	if (!object) {
		self->m_wasBlocked = false;
		return;
	}
	self->m_wasBlocked = XObject_blockSignals(object, true);
}

void XSignalBlocker_destroy(XSignalBlocker* self)
{
	if (!self || !self->m_object)
		return;
	/* Qt：`if (m_o && !m_inhibited) m_o->blockSignals(m_blocked);`
	 * inhibit（曾 unblock）时不动对象；m_object 一律清空以支持重复 destroy。 */
	if (!self->m_inhibited)
		XObject_blockSignals(self->m_object, self->m_wasBlocked);
	self->m_object = NULL;
	self->m_inhibited = false;
}

void XSignalBlocker_reblock(XSignalBlocker* self)
{
	/* 对标 Qt qobject.h:548-553：无条件重新阻塞，并清 inhibit 位。
	 * 不恢复前态——后续 destroy 才按 m_wasBlocked 恢复。 */
	if (!self)
		return;
	if (self->m_object)
		XObject_blockSignals(self->m_object, true);
	self->m_inhibited = false;
}

void XSignalBlocker_unblock(XSignalBlocker* self)
{
	/* 对标 Qt qobject.h:555-560：恢复**进入前**的阻塞状态（不是无条件
	 * false），并置 inhibit 位使 destroy 不再重复恢复。 */
	if (!self)
		return;
	if (self->m_object)
		XObject_blockSignals(self->m_object, self->m_wasBlocked);
	self->m_inhibited = true;
}

void XSignalBlocker_dismiss(XSignalBlocker* self)
{
	/* 对标 Qt qobject.h:562-565：只清 m_o（不碰 inhibit 位），
	 * 对象保持当前阻塞状态，所有后续入口对对象无副作用。 */
	if (!self)
		return;
	self->m_object = NULL;
}

void XSignalBlocker_ctor(XSignalBlocker* self, XObject* object)
{
	XSignalBlocker_init(self, object);
}

void XSignalBlocker_dtor(XSignalBlocker* self)
{
	XSignalBlocker_destroy(self);
}
/* Qt 6.8：QObject::startTimer/killTimer 只允许在**对象所属线程**调用。
 * Qt 实测：跨线程 startTimer 返回 0 并告警 "Timers cannot be started from
 * another thread"；跨线程 killTimer 告警 "Timers cannot be stopped from
 * another thread" 并拒绝执行。修复前 X 侧无任何检查，跨线程会直接改别人
 * 线程的定时器表（数据竞争）。 */
static bool XObject_timerThreadCheck(const XObject* self, const char* message)
{
	XThreadData* own = XObject_threadData(self);
	if (own && own != XThreadData_current()) {
		XPrintf("XObject: %s\n", message);
		return false;
	}
	return true;
}

XTimerId XObject_startTimer(XObject* self, uint64_t interval, XTimerType timerType)
{
	/* Qt 6.8：毫秒口径 0 合法（"尽快触发"，Qt 实测 startTimer(0)=1）；
	 * X 调度器以纳秒注册且拒绝 0（XAbstractEventDispatcher.c 的
	 * `interval == 0` 分支），故 0 毫秒按 1 纳秒下限注册。
	 * 24 天夹取是 **X 侧防溢出保护**（避免 64 位毫秒×1e6 溢出成纳秒后
	 * 回绕），仅影响 ≥24 天的间隔——Qt 6.8 的 startTimer 无此夹取
	 * （qobject.cpp:1873-1899，台账 K-01 订正）。 */
	const uint64_t maxMs = 24ULL * 60ULL * 60ULL * 1000ULL;
	if (interval == 0)
		return XObject_startTimer_ns(self, 1, timerType);
	if (interval > maxMs)
		interval = maxMs;
	return XObject_startTimer_ns(self, interval * 1000000ULL, timerType);
}
XTimerId XObject_startTimer_ns(XObject* self, uint64_t interval_ns, XTimerType timerType)
{
	if (!self) return XTIMER_INVALID_ID;
	if (!XObject_timerThreadCheck(self, "Timers cannot be started from another thread"))
		return XTIMER_INVALID_ID;
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
	if (!disp) return XTIMER_INVALID_ID;
	if (interval_ns == 0) interval_ns = 1; /* Qt：0 间隔尽快触发 */
	return XAbstractEventDispatcher_registerTimer(disp, interval_ns, timerType, self);
}
void XObject_killTimer(XObject* self, XTimerId timerId)
{
	if (!self) return;
	/* Qt 6.8：跨线程 killTimer 告警并拒绝（本机实测）。 */
	if (!XObject_timerThreadCheck(self, "Timers cannot be stopped from another thread"))
		return;
	/* Qt 6.8 原文（qobject.cpp:1926-1936）：
	 *   `if (id > Qt::TimerId::Invalid) {
	 *        int at = d->extraData ? d->extraData->runningTimers.indexOf(id) : -1;
	 *        if (at == -1) {   // timer isn't owned by this object
	 *            qWarning("QObject::killTimer(): Error: timer id %d is not valid for object ...");
	 *            return; } }`
	 * 即**先查归属再注销**：误传 id 不得静默杀掉另一个对象的定时器。
	 * X 的归属事实在 `XFd::object`（registerTimer 写入，moveToThread 时保持不变），
	 * 由 `XAbstractEventDispatcher_unregisterTimerForObject` 校验。 */
	if (timerId == XTIMER_INVALID_ID) return;
	{
		XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
		/* 悬空调试残留「if (timerId == 1)」已删（staged diff 可证系调试
		 * 中间态）：它把下一行守卫短路成自己的条件体，使 timerId!=1 时
		 * 无条件走到 unregisterTimer_base——disp 为 NULL 即触发
		 * ArgIsNULL 报错（回归门禁实测 ×4）。恢复守卫语义。 */
		if (!disp) return;
		if (!XAbstractEventDispatcher_unregisterTimerForObject(disp, timerId, self)) {
			XPrintf("XObject::killTimer(): Error: timer id %lld is not valid for object, timer has not been killed\n",
				(long long)timerId);
		}
	}
}
// Qt 6.8: threadData.loadRelaxed() (对标 QObjectPrivate::threadData)
XThreadData* XObject_threadData(const XObject* object)
{
	if (!object)
		return NULL;
	return (XThreadData*)XAtomic_load_uintptr_t(
		(XAtomic_Uintptr*)&object->m_threadData, XAtomic_MemoryOrder_Relaxed);
}

// Qt 6.8: QObject::thread() -> d_func()->threadData.loadRelaxed()->thread.loadAcquire()
XThread* XObject_thread(const XObject* object)
{
	if (!object)
		return NULL;
	XThreadData* td = XObject_threadData(object);
	return td ? td->m_thread : NULL;
}

// Qt 6.8: QAbstractEventDispatcher via threadData->eventDispatcher
XAbstractEventDispatcher* XObject_eventDispatcher(const XObject* object)
{
	if (!object)
		return NULL;
	XThreadData* td = XObject_threadData(object);
	if (td && td->m_eventDispatcher)
		return td->m_eventDispatcher;
	return XCoreApplication_eventDispatcher();
}

XConnection* XObject_connect(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
{
	if(object==NULL || slot_func == NULL)
		return NULL;
	if(object->m_signalSlot==NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_connect1(object->m_signalSlot,signal,receiver,slot_func,type);
}

XConnection* XObject_connect_2(XObject* object, size_t signal, XSlotFunc2 slot_func)
{
	if (object == NULL || slot_func == NULL)
		return NULL;
	if (object->m_signalSlot == NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_connect2(object->m_signalSlot, signal, slot_func);
}

XConnection* XObject_connect_context(XObject* object, size_t signal, XObject* context,
	XSlotFunc2 slot_func, XConnectionType type)
{
	if (object == NULL || context == NULL || slot_func == NULL)
		return NULL;
	/* Qt 6.8（qobject.h:226-271）：**不检查** sender/context 是否同线程——
	 * 跨线程连接本来就是合法用法（Auto 在跨线程时退化为 Queued，槽在
	 * context 所属线程执行；Qt 的 qobject.cpp 里 connect 路径没有
	 * "different thread" 告警，那三条告警分别属于 setParent /
	 * installEventFilter / createChildren）。本实现同口径，线程判定交给
	 * XConnection_autoEmit。 */
	if (object->m_signalSlot == NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	/* ContextReceiver：receiver 只做生命周期/线程亲和簿记，槽按 slot_func2
	 * 调用（首参=发送者），见 XConnection_directEmit / XMetaCallEvent。 */
	return XSignalSlot_connect1(object->m_signalSlot, signal, context, slot_func,
		(XConnectionType)(type | XConnectionType_ContextReceiver));
}

/* ==================== disconnect 的匹配条件 ====================
 * 对标 Qt 6.8 `QObject::disconnect(sender, signal, receiver, member)`
 * （qobject.h:284-293 的四个重载）：signal / receiver / member 三个过滤条件
 * 都可为空，空即"任意"。本仓库用 0 / NULL 表达：
 *   signal==0        ⇒ 任意信号；
 *   receiver==NULL   ⇒ 任意接收者；
 *   slot_func1==NULL ⇒ 任意槽。 */
typedef struct XObjectDisconnectFilter
{
	bool anySignal;      /* signal==0（Qt 的 signal==nullptr） */
	size_t signal;       /* 有效信号标识 */
	XObject* receiver;   /* 可为 NULL */
	XSlotFunc1 slot_func1; /* 可为 NULL */
} XObjectDisconnectFilter;

static bool XObject_connectionMatches(const XConnection* conn,
	const XObjectDisconnectFilter* filter)
{
	if (!conn || !filter)
		return false;
	if (filter->receiver && conn->receiver != filter->receiver)
		return false;
	/* 与 XSignalSlot 的 XConnection_equal 同口径：槽函数地址按 XSlotFunc1
	 * 解释比较（union 同址，connect_2 存的是 XSlotFunc2）。 */
	if (filter->slot_func1 && conn->slot_func1 != filter->slot_func1)
		return false;
	return true;
}

/* 在发送者连接表里找**第一条**匹配的连接（调用方必须持有 manager->mutex）。
 * 只取一条是有意的：断开会同步触发 disconnectNotify，子类重载里可能删掉别的
 * 连接；每次重新查找可保证后续断开用的是当轮仍然有效的指针。 */
static XConnection* XObject_findFirstMatchingConnection(XSignalSlot* manager,
	const XObjectDisconnectFilter* filter)
{
	if (!manager || !manager->signalMap || !filter)
		return NULL;
	for_each_iterator(((XHashMap*)manager->signalMap), XHashMap, it) {
		XSignal* signal = (XSignal*)XPair_second(XHashMap_iterator_data(&it));
		if (!signal || !signal->connList)
			continue;
		if (!filter->anySignal && signal->type != filter->signal)
			continue;
		for_each_iterator(signal->connList, XVector, cit) {
			XConnection* conn = *(XConnection**)XVector_iterator_data(&cit);
			if (XObject_connectionMatches(conn, filter))
				return conn;
		}
	}
	return NULL;
}

bool XObject_disconnect(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func1)
{
	XSignalSlot* manager = NULL;
	int removed = 0;
	if (!object || !object->m_signalSlot)
		return false;
	manager = object->m_signalSlot;

	if (signal != 0 && slot_func1 != NULL) {
		/* 常用形态：三个条件全指定。重复调用 XSignalSlot_disconnect1 直到
		 * 没有匹配——Qt 实测同一 (signal,receiver,method) 连三次、一次
		 * disconnect 后槽触发 0 次（旧实现找到第一条就 return，会残留
		 * 两条继续触发）。 */
		while (XSignalSlot_disconnect1(manager, signal, receiver, slot_func1))
			++removed;
		return removed > 0;
	}

	if (!manager->mutex)
		return false;
	{
		XObjectDisconnectFilter filter;
		filter.anySignal = (signal == 0);
		filter.signal = signal;
		filter.receiver = receiver;
		filter.slot_func1 = slot_func1;
		for (;;) {
			XConnection* victim = NULL;
			XMutex_lock(manager->mutex);
			victim = XObject_findFirstMatchingConnection(manager, &filter);
			XMutex_unlock(manager->mutex);
			if (!victim)
				break;
			/* disconnect2 内部持全局连接锁，并会向接收者发 disconnectNotify */
			if (!XSignalSlot_disconnect2(victim))
				break;
			++removed;
		}
	}
	return removed > 0;
}

bool XObject_disconnect_2(XConnection* conn)
{
	if ( conn == NULL)
		return false;
	return XSignalSlot_disconnect2(conn);
}

static void XObject_scheduleDeferredDelete(XObject* object, bool isDelete)
{
	if (!object) return;

	XThreadData* td = XThreadData_lockPostEventList(object);
	if (!td) return;

	if (object->m_delete_later_called) {
		/* 已有待处理动作：**升级**为更重的那个。旧实现直接返回 ⇒
		 * `deinitLater(obj); deleteLater(obj);` 的第二句被吞，对象只被
		 * 反初始化、内存永不释放（deinitLater 是 Qt 没有的 X 扩展，
		 * XTcpServer 有 2 处调用，这条路径是真实可达的）。 */
		if (isDelete)
			object->m_deferred_delete_is_release = 1;
		XMutex_unlock(td->m_mutex);
		return;
	}
	object->m_delete_later_called = true;
	object->m_deferred_delete_is_release = isDelete ? 1 : 0;

	int loopLevel = 0;
	int scopeLevel = 0;
	if (td == XThreadData_current()) {
		/* 对标 Qt 6.8 qobject.cpp:2459-2486：
		 *   loopLevel = objectThreadData->loopLevel;
		 *   scopeLevel = objectThreadData->scopeLevel;
		 *   // scopeLevel==0 而 loopLevel!=0 ⇒ 取 1（非规范代码路径的兜底）
		 *   if (scopeLevel == 0 && loopLevel != 0) scopeLevel = 1;
		 * 旧注释曾把最后这条当"X 自定义规则"，实际 Qt 6.8.3 源里逐字存在
		 * （qobject.cpp:2478-2482，注释为 "If the scope level is 0 while
		 * loopLevel != 0, we are called from a non-conformant code path..."）。 */
		loopLevel = (int)XAtomic_load_size_t(&td->m_loopLevel, XAtomic_MemoryOrder_Acquire);
		scopeLevel = td->m_scopeLevel;
		if (scopeLevel == 0 && loopLevel != 0)
			scopeLevel = 1;
	}
	XMutex_unlock(td->m_mutex);

	XDeferredDeleteEvent* event = XDeferredDeleteEvent_create(
		isDelete, loopLevel, scopeLevel);
	if (!event) {
		XThreadData* current = XThreadData_lockPostEventList(object);
		if (current) {
			object->m_delete_later_called = false;
			object->m_deferred_delete_is_release = 0;
			XMutex_unlock(current->m_mutex);
		}
		return;
	}
	XCoreApplication_postEvent(object, (XEvent*)event, XEVENT_PRIORITY_LOWEST);
}

void XObject_deinitLater(XObject* object)
{
	XObject_scheduleDeferredDelete(object, false);
}

void XObject_deleteLater(XObject* object)
{
	XObject_scheduleDeferredDelete(object, true);
}

void XObject_emitSignal(XObject* object, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_Int32* ref_count, XEventPriority priority)
{
	if (!object)
		return;
	/* Qt 6.8 QObject::blockSignals：阻塞期间**所有信号都不派发**（Qt 实测
	 * blockSignals(true) 后槽 0 次触发），唯一例外是 destroyed()——Qt 文档
	 * 明确 "destroyed() is emitted even if signals are blocked"（Qt 实测
	 * 被阻塞对象析构时 destroyed 仍触发 1 次）。本框架的 destroyed 信号在
	 * VXObject_deinit 里清 m_block_sig 之后才发，这里再按信号标识放行一次，
	 * 保证手工发射 destroyed 也不会被拦。
	 * 修复前 m_block_sig 只在 blockSignals/signalsBlocked 里读写，发射路径
	 * （XSignalSlot_emit）从不看它 ⇒ blockSignals 完全空转。 */
	if (object->m_block_sig && signal != (size_t)XObject_destroyed_signal) {
		/* 参数所有权与 XSignalSlot_emit 的"无管理器"路径完全同口径：
		 * 先登记释放回调再交给 XVarList_delete，引用计数同理回收。 */
		if (args) {
			args->argsDel = del;
			XVarList_delete(args);
		}
		if (ref_count)
			XAtomic_delete(ref_count);
		return;
	}
	if (object->m_signalSlot) {
		XSignalSlot_emit(object->m_signalSlot, signal, args, del, ref_count, priority);
		return;
	}
	/* 本对象从未建过连接表：沿用 XSignalSlot_emit 的"无管理器"清理路径，
	 * 别把 args/ref_count 漏在调用方已经放弃所有权之后。 */
	if (args) {
		args->argsDel = del;
		XVarList_delete(args);
	}
	if (ref_count)
		XAtomic_delete(ref_count);
}

size_t XObject_signalIdOf(void* (*fn)(XObject*))
{
	/* 以 NULL 调用信号函数，取其返回值——即信号函数在**其定义模块内**的
	 * 真实地址（各 signal 实现约定：self==NULL 时返回 `(void*)(size_t)(自身名)`）。
	 *
	 * 跨 DLL 边界的必要性（2026-10-25 根修）：调用方（可执行文件）侧
	 * `&fn` 得到的是 MSVC 增量链接生成的导入跳板地址，与动态库发射侧使用
	 * 的真实函数地址不等 → signalMap 查不中 → 槽函数静默不触发（表现为
	 * Test 菜单动作全部失效）。经本次调用，跳板被穿透、进入定义模块执行，
	 * 其返回值即定义模块真址，与发射侧一致。
	 *
	 * 安全性：NULL 调用时各信号函数在 if(self)/if(!self) 守卫内短路，
	 * 不会分配参数（XVarList_Create 等实参构造位于 if 函数体内，惰性求值），
	 * 故无副作用、无泄漏。
	 */
	if (!fn)
		return 0;
	return (size_t)fn(NULL);
}

void* XObject_destroyed_signal(XObject* object)
{
	/* Qt 6.8: `void destroyed(QObject *obj = nullptr)` —— 槽能拿到**正在销毁的
	 * 对象**（本机实测：槽参数 == 被 delete 的指针）。X 的槽签名是
	 * `(XObject* receiver, XVarList* args)`，故把对象作为唯一参数放进 args。
	 * 参数是借用指针：argsDel 不设，列表析构不会去删这个对象（真正的释放由
	 * 延迟删除事件/调用方的 XClassDelete 负责）。 */
	XObject* payload = object;
	XVarList* args = NULL;
	/* 没有连接表的对象（绝大多数在析构期）不必造参数列表：XEmitSignal 本来
	 * 就不会发射，这里省掉每次析构一次分配。 */
	if (object && object->m_signalSlot)
		args = XVarList_Create(XVar(XObject*, payload));
	/* 参数列表建不出来（OOM）时退化为旧行为（无参数），不能让 destroyed
	 * 因为分配失败而完全不发——析构通知比参数更重要。 */
	XEmitSignal(object, XObject_destroyed_signal, args, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}
static void XObject_objectNameChanged_signal_del(struct XVarList* list)
{
	XVarList_args_1(list, XString*, objectName);
	if (objectName)
		XClassDelete((XClass*)objectName);
}
void* XObject_objectNameChanged_signal(XObject* object, const XString* objectName)
{
	/* [信号函数 NULL 契约] XObject_signalIdOf 以 `self=NULL` 调用信号函数
	 * 采集信号 ID（返回值=定义模块内真址，见 XObject_signalIdOf 注释）。
	 * 此时调用方只传了一个实参，`objectName` 是寄存器/栈残值——**不是**
	 * NULL 约定，可能非空。故必须在触碰 `objectName` 之前短路返回自身地址：
	 * 2026-11-11 对齐测试 XObject.objectNameNullClears 首次 XSignal(...) 连接
	 * 即按残值 0x8 进入 XString_create_copy 段错误（X11 平台插件只报
	 * `[xpwn] X11 async error ignored`，看不出是哪个信号函数）。 */
	if (!object)
		return (void*)(size_t)XObject_objectNameChanged_signal;
	if(objectName)
	{
		XString* name = XString_create_copy(objectName);
		XVarList* args = XVarList_Create(XVar(XString*, name));
		if (!args)
		{
			if (name)
				XClassDelete((XClass*)name);
			return (void*)(size_t)XObject_objectNameChanged_signal;
		}
		if (object->m_signalSlot)
			XObject_emitSignal(object, (size_t)XObject_objectNameChanged_signal,
				args, XObject_objectNameChanged_signal_del, NULL, XEVENT_PRIORITY_NORMAL);
		else
		{
			XVarList_setArgsDel(args, XObject_objectNameChanged_signal_del);
			XVarList_delete(args);
		}
	}
	else
	{
		if (object->m_signalSlot)
			XObject_emitSignal(object, (size_t)XObject_objectNameChanged_signal,
				NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
	}
	return (void*)(size_t)XObject_objectNameChanged_signal;
}

void XObject_installEventFilter(XObject* self, XObject* filterObj)
{
	if (!self || !filterObj)return;

	// Qt 6.8: 线程亲和性检查 - 过滤器和被过滤对象必须在同一线程
	if (XObject_threadData(self) != XObject_threadData(filterObj)) {
		/* Qt 实测输出 "QObject::installEventFilter(): Cannot filter events
		 * for objects in a different thread." 且不安装；修复前此处静默
		 * return，排障时看不出"过滤器没生效"的原因。 */
		XPrintf("XObject::installEventFilter(): Cannot filter events for objects in a different thread.\n");
		return;
	}

	// 回登记表须先就绪：过滤器记录自己观察的对象，供过滤器先亡时反注册
	if (!filterObj->m_watchedObjects) {
		filterObj->m_watchedObjects = XVector_create(sizeof(XObject*));
		if (!filterObj->m_watchedObjects)return;
	}

	XVector* filters = self->m_filters;
	if (!filters)
	{
		self->m_filters = XVector_create(sizeof(XObject*));
		filters = self->m_filters;
		if (!filters)return;
	}

	// Qt 6.8: 如果已存在，先移除再 prepend（最后安装的最先调用）
	int idx = XVector_indexOf(filters, &filterObj, 0);
	if (idx != -1)
		XVector_remove_base(filters, idx, 1);
	if (!XVector_insert_1_base(filters, 0, &filterObj, 1)) // prepend
		return;
	/* 双向登记：把被观察对象写入过滤器回登记表；写失败则回滚本次安装，
	 * 保持 m_filters 与 m_watchedObjects 两侧登记一致 */
	if (XVector_indexOf(filterObj->m_watchedObjects, &self, 0) == -1
		&& !XVector_push_back_1_base(filterObj->m_watchedObjects, &self))
		XVector_remove_base(filters, 0, 1);
}

void XObject_removeEventFilter(XObject* self, XObject* obj)
{
	if (!self || !obj)return;
	XVector* filters = self->m_filters;
	if (!filters) return;
	int index = XVector_indexOf(filters, &obj, 0);
	if (index != -1)
	{
		XVector_remove_base(filters, index, 1);
		// 同步回删过滤器回登记表中的本对象，保持 m_filters/m_watchedObjects 双向一致
		if (obj->m_watchedObjects)
		{
			int backIndex = XVector_indexOf(obj->m_watchedObjects, &self, 0);
			if (backIndex != -1)
				XVector_remove_base(obj->m_watchedObjects, backIndex, 1);
		}
	}
}

static bool XObject_nameMatches(const XObject* object, const XString* name)
{
	if (!object) return false;
	/* Qt 6.8 findChild/findChildren 的名字过滤：**名字为空即不过滤**。
	 *
	 * 本机 Qt 6.8.0 offscreen 实测的精确口径（比 Qt 文档更细）：
	 *   - `findChild<QObject*>(QString())`（null）       → 第一个子对象（不论有无名字）
	 *   - `findChild<QObject*>(QString(""))`（非 null 空串）→ 第一个**无名**子对象
	 * 即 Qt 靠「null vs 空串」区分"不过滤"与"只匹配无名对象"。
	 * XString 没有 null 态（AGENTS.md 已知限制 #21：空串的 XString_toUtf8
	 * 返回 NULL，isNull 无法表达），C 侧拿不到这个区分 ⇒ 本项目按 Qt
	 * **文档**口径 "If name is empty, all objects are matched"，NULL 与空
	 * XString 一律不过滤。这是有意偏差，由 XObject.findChild 用例锁定。 */
	if (!name || XString_length_base(name) == 0)
		return true;
	if (!object->m_object_name)
		return false;
	return XString_equals(object->m_object_name, name, XChar_CaseSensitive);
}

static XObject* XObject_findChild_recursive(const XObject* parent, const XString* name,
	XFindChildOption options)
{
	if (!parent || !parent->m_children) return NULL;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (XObject_nameMatches(child, name))
			return child;
	}
	if (options & XFindChildOption_ChildrenRecursively) {
		for_each_iterator(parent->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			XObject* match = XObject_findChild_recursive(child, name, options);
			if (match) return match;
		}
	}
	return NULL;
}

XObject* XObject_findChild(const XObject* self, const XString* name, XFindChildOption options)
{
	return XObject_findChild_recursive(self, name, options);
}

static void XObject_findChildren_recursive(const XObject* parent, const XString* name,
	XFindChildOption options, XObjectList* result)
{
	/* 遍历顺序对齐 Qt：**深度优先前序**（命中就追加，然后递归该子树）。
	 * 本机 Qt 6.8.0 offscreen 实测：根下 c1、c2，c1 下挂 gc 时
	 * `findChildren<QObject*>(QString())` 的顺序是 `c1, gc, c2`。
	 * 与 findChild（单个）的「先直接子对象、再递归」浅层优先**不同**。 */
	if (!parent || !parent->m_children || !result) return;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (!child) continue;
		if (XObject_nameMatches(child, name))
			XVector_push_back_1_base(result, &child);
		if (options & XFindChildOption_ChildrenRecursively)
			XObject_findChildren_recursive(child, name, options, result);
	}
}

XObjectList* XObject_findChildren(const XObject* self, const XString* name, XFindChildOption options)
{
	if (!self) return NULL;
	XObjectList* result = XVector_Create(XObject*);
	XObject_findChildren_recursive(self, name, options, result);
	return result;
}

/* ==================== 按类型的 findChild/findChildren（对标 Qt 6.8 findChild<T>） ==================== */

static bool XObject_classMatches(const XObject* object, const XVtable* classVtable)
{
	/* classVtable == NULL ⇒ 不按类型过滤（Qt 的 findChild<QObject*>） */
	return classVtable == NULL || XObject_isA_class(object, classVtable);
}

static XObject* XObject_findChild_class_recursive(const XObject* parent, const XString* name,
	const XVtable* classVtable, XFindChildOption options)
{
	if (!parent || !parent->m_children) return NULL;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (XObject_nameMatches(child, name) && XObject_classMatches(child, classVtable))
			return child;
	}
	if (options & XFindChildOption_ChildrenRecursively) {
		for_each_iterator(parent->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			XObject* match = XObject_findChild_class_recursive(child, name, classVtable, options);
			if (match) return match;
		}
	}
	return NULL;
}

XObject* XObject_findChild_class(const XObject* self, const XString* name,
	const XVtable* classVtable, XFindChildOption options)
{
	return XObject_findChild_class_recursive(self, name, classVtable, options);
}

static void XObject_findChildren_class_recursive(const XObject* parent, const XString* name,
	const XVtable* classVtable, XFindChildOption options, XObjectList* result)
{
	if (!parent || !parent->m_children || !result) return;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (!child) continue;
		if (XObject_nameMatches(child, name) && XObject_classMatches(child, classVtable))
			XVector_push_back_1_base(result, &child);
		if (options & XFindChildOption_ChildrenRecursively)
			XObject_findChildren_class_recursive(child, name, classVtable, options, result);
	}
}

XObjectList* XObject_findChildren_class(const XObject* self, const XString* name,
	const XVtable* classVtable, XFindChildOption options)
{
	XObjectList* result = NULL;
	if (!self) return NULL;
	result = XVector_Create(XObject*);
	if (!result) return NULL;
	XObject_findChildren_class_recursive(self, name, classVtable, options, result);
	return result;
}

/* ==================== 按正则表达式的 findChild/findChildren ====================
 * 对标 Qt 6.8 qobject.h:186-198 的
 * `findChildren<T>(const QRegularExpression &re, Qt::FindChildOptions)`，
 * 实现口径取自 qobject.cpp:2152-2166：
 *   for (QObject *obj : parent->children()) {
 *       if (mo.cast(obj)) {
 *           QRegularExpressionMatch m = re.match(obj->objectName());
 *           if (m.hasMatch()) list->append(obj);
 *       }
 *       if (options & FindChildrenRecursively) recurse(obj);
 *   }
 * 要点（本机 Qt 6.8.0 offscreen 探针 /tmp/probe_qobject_regex.cpp 实测）：
 *   - 主题是 objectName()，**未设名字按空串**：空模式 "" 命中全部（含无名
 *     对象），"a*" 也命中无名对象，".+" 不命中无名对象；
 *   - `match()` 是**非锚定**搜索："e" 命中 "deep"，"^d" 只命中以 d 开头的；
 *   - **无效模式不命中任何对象**（Qt 打印 "called on an invalid
 *     QRegularExpression object" 并返回空表）；C 侧 NULL 表达式同此口径；
 *   - 列表顺序是**深度优先前序**（命中就追加，再递归子树）。
 * 单个对象的正则重载 Qt 没有（qobject.cpp 只有 qt_qFindChildren_helper 的
 * 正则版），X 侧按 findChild 家族口径补全，见 XObject.h 的说明。 */
#if XRegularExpression_ON
static bool XObject_nameMatchesRegex(const XObject* object, const XRegularExpression* expression)
{
	XRegularExpressionMatch* match = NULL;
	bool matched = false;
	if (!object || !expression)
		return false;
	/* subject 传 NULL ⇒ 框架按空串匹配（与 Qt objectName() 未设名同口径） */
	match = XRegularExpression_match(expression, object->m_object_name, 0,
		XRegularExpression_NormalMatch, XRegularExpression_NoMatchOption);
	if (!match)
		return false;
	matched = XRegularExpressionMatch_hasMatch(match);
	XClassDelete((XClass*)match);
	return matched;
}

/* classVtable == NULL ⇒ 不按类型过滤（findChildren<QObject*> 等价） */
static XObject* XObject_findChild_regex_recursive(const XObject* parent,
	const XVtable* classVtable, const XRegularExpression* expression, XFindChildOption options)
{
	if (!parent || !parent->m_children) return NULL;
	/* 浅层优先（与 XObject_findChild 同序）：先扫直接子对象，再递归各子树 */
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (child && XObject_classMatches(child, classVtable)
			&& XObject_nameMatchesRegex(child, expression))
			return child;
	}
	if (options & XFindChildOption_ChildrenRecursively) {
		for_each_iterator(parent->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			XObject* found = XObject_findChild_regex_recursive(child, classVtable,
				expression, options);
			if (found) return found;
		}
	}
	return NULL;
}

static void XObject_findChildren_regex_recursive(const XObject* parent,
	const XVtable* classVtable, const XRegularExpression* expression,
	XFindChildOption options, XObjectList* result)
{
	/* 深度优先前序（与 XObject_findChildren 同序）：命中就追加，再递归子树。 */
	if (!parent || !parent->m_children || !result) return;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (!child) continue;
		if (XObject_classMatches(child, classVtable)
			&& XObject_nameMatchesRegex(child, expression))
			XVector_push_back_1_base(result, &child);
		if (options & XFindChildOption_ChildrenRecursively)
			XObject_findChildren_regex_recursive(child, classVtable, expression, options, result);
	}
}

XObjectList* XObject_findChildren_class_regex(const XObject* self, const XVtable* classVtable,
	const XRegularExpression* expression, XFindChildOption options)
{
	XObjectList* result = NULL;
	if (!self) return NULL;
	result = XVector_Create(XObject*);
	if (!result) return NULL;
	XObject_findChildren_regex_recursive(self, classVtable, expression, options, result);
	return result;
}

XObjectList* XObject_findChildren_regex(const XObject* self,
	const XRegularExpression* expression, XFindChildOption options)
{
	return XObject_findChildren_class_regex(self, NULL, expression, options);
}

XObject* XObject_findChild_regex(const XObject* self,
	const XRegularExpression* expression, XFindChildOption options)
{
	return XObject_findChild_regex_recursive(self, NULL, expression, options);
}

XObject* XObject_findChild_class_regex(const XObject* self, const XVtable* classVtable,
	const XRegularExpression* expression, XFindChildOption options)
{
	return XObject_findChild_regex_recursive(self, classVtable, expression, options);
}
#endif /* XRegularExpression_ON */


bool XObject_event_base(XObject* self, XEvent* e)
{
		if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXObject_Event, bool(*)(XObject*, XEvent*))(self,e);
}

bool XObject_eventFilter_base(XObject* self, XObject* watched, XEvent* event)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXObject_EventFilter, bool(*)(XObject*, XObject *, XEvent*))(self, watched, event);
}

void XObject_timerEvent_base(XObject* self, XTimerEvent* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")|| !XClassGetVirtualFunc(self, EXObject_TimerEvent,bool))
		return ;
	XClassGetVirtualFunc(self, EXObject_TimerEvent, void(*)(XObject*, XTimerEvent*))(self, event);
}

void XObject_childEvent_base(XObject* self, XChildEvent* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_ChildEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_ChildEvent, void(*)(XObject*, XChildEvent*))(self, event);
}

void XObject_customEvent_base(XObject* self, XEvent* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_CustomEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_CustomEvent, void(*)(XObject*, XEvent*))(self, event);
}

void XObject_connectNotify_base(XObject* self, size_t signal)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_ConnectNotify, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_ConnectNotify, void(*)(XObject*, size_t))(self, signal);
}

void XObject_disconnectNotify_base(XObject * self, size_t signal)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_DisconnectNotify, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_DisconnectNotify, void(*)(XObject*, size_t))(self, signal);
}

XObject* XObject_sender(const XObject* self)
{
	//从当前线程的发送者栈中查找 receiver==self 的最近一次发射,返回其发送者
	//(取代每对象 m_sender,消除跨线程竞争与嵌套发射错乱)
	return XThreadData_currentSender((XObject*)self);
}

int XObject_senderSignalIndex(const XObject* self)
{
	// Qt 6.8: 从当前线程的发送者栈中查找 receiver==self 的最近一次发射,返回其信号索引
	// (对标 QObject::senderSignalIndex)
	return (int)XThreadData_currentSenderSignalIndex((XObject*)self);
}

/* 发送动态属性变更事件（对标 Qt 的 `QCoreApplication::sendEvent(this,
 * &QDynamicPropertyChangeEvent(name))`）。只在属性**真的变化**时发：
 * 本机 Qt 6.8 探针（QObject 子类重载 event() 计数 QEvent::DynamicPropertyChange）：
 *   新增 → 1 次；改值 → 1 次；**同值再写 → 0 次**；删除已有 → 1 次；
 *   删除不存在 → 0 次。
 * nameUtf8 必须非 NULL。事件对象同步派发，用完即删。 */
static void XObject_notifyDynamicPropertyChanged(XObject* self, const char* nameUtf8)
{
	XDynamicPropertyChangeEvent* event = NULL;
	if (!self || !nameUtf8)
		return;
	event = XDynamicPropertyChangeEvent_create(nameUtf8);
	if (!event)
		return;
	XCoreApplication_sendEvent(self, (XEvent*)event);
	XClassDelete((XClass*)event);
}

/* 删除一条动态属性（Qt 6.8 **没有** QObject::removeProperty：删除的唯一
 * 公开入口是 `setProperty(name, QVariant())`，见 qobject.h:332-336 与
 * 本机 Qt 探针"写入无效值 ⇒ dynamicPropertyNames() 由 1 条变 0 条"）。
 * 本函数是 setProperty 的 NULL 分支实现，static — 2026-11-11 API 收敛批次
 * 删除了公开的 XObject_removeProperty（Qt 无此名）。语义与旧入口逐条一致：
 *   - 属性不存在 ⇒ 静默返回（不发事件）；
 *   - 删除**存在**的属性 ⇒ 发一次 DynamicPropertyChange（Qt 实测）。 */
static void XObject_removeDynamicProperty(XObject* self, const XString* name)
{
	bool removed = false;
	if (!self || !name || !self->m_dynamicPropertyNames) return;

	for (int i = 0; i < (int)XContainerSize(self->m_dynamicPropertyNames); i++) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		if (namePtr[i] && XString_compare(namePtr[i], name) == XCompare_Equality) {
			XVariant** values = (XVariant**)XContainerDataAddr(self->m_dynamicPropertyValues);
			if (values[i])
				XClassDelete(values[i]);
			XClassDelete(namePtr[i]);
			XVector_remove_base(self->m_dynamicPropertyNames, i, 1);
			XVector_remove_base(self->m_dynamicPropertyValues, i, 1);
			removed = true;
			break;
		}
	}
	/* Qt 实测：删除**存在**的属性发 1 次 DynamicPropertyChange，删除不存在的不发。 */
	if (removed)
		XObject_notifyDynamicPropertyChanged(self, XString_toUtf8(name));
}

// Qt 6.8: 动态属性系统 (对标 QObject::setProperty / property / dynamicPropertyNames)
// 与 Qt 同口径：**总是**接管 value 所有权；返回值 = 是否命中静态属性（本仓库
// 尚无静态属性层 ⇒ 动态写入恒 false，见 XObject.h 的契约说明）。
bool XObject_setProperty(XObject* self, const XString* name, XVariant* value)
{
	/* Qt 6.8：`setProperty(name, QVariant())` 等价于**删除**该动态属性
	 * （本机实测：写入无效值后 dynamicPropertyNames().size() 由 1 变 0、
	 * property(name) 失效，返回值仍为 false）。修复前 NULL 会留下一条
	 * "值为 NULL" 的条目，读侧无法区分"没设过"和"设成空"。
	 * 这是删除动态属性的**唯一入口**（Qt 6.8 没有 QObject::removeProperty，
	 * 本仓库 2026-11-11 已删除同义的冗余入口 XObject_removeProperty）。 */
	if (!value) {
		if (self && name)
			XObject_removeDynamicProperty(self, name);
		return false;
	}
	/* 所有权已归本函数：任何提前返回都必须释放 value，否则调用方（按
	 * Qt 口径不再释放）会漏。 */
	if (!self || !name) {
		XClassDelete((XClass*)value);
		return false;
	}

	// 懒初始化动态属性存储
	if (!self->m_dynamicPropertyNames) {
		self->m_dynamicPropertyNames = XVector_Create(XString*);
		self->m_dynamicPropertyValues = XVector_Create(XVariant*);
		if (!self->m_dynamicPropertyNames || !self->m_dynamicPropertyValues) {
			if (self->m_dynamicPropertyNames) XClassDelete(self->m_dynamicPropertyNames);
			if (self->m_dynamicPropertyValues) XClassDelete(self->m_dynamicPropertyValues);
			self->m_dynamicPropertyNames = NULL;
			self->m_dynamicPropertyValues = NULL;
			XClassDelete((XClass*)value);
			return false;
		}
	}

	// 查找是否已有同名属性
	int idx = -1;
	for (int i = 0; i < (int)XContainerSize(self->m_dynamicPropertyNames); i++) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		if (namePtr[i] && XString_compare(namePtr[i], name) == XCompare_Equality) {
			idx = i;
			break;
		}
	}

	if (idx >= 0) {
		// 已存在: 更新值（同值也替换，但**不发**变更事件——Qt 实测同值 0 次）
		XVariant** valPtr = (XVariant**)XContainerDataAddr(self->m_dynamicPropertyValues);
		bool changed = true;
		if (valPtr[idx]
			&& XVariant_type(valPtr[idx]) == XVariant_type(value)
			&& XVariant_dataSize(valPtr[idx]) == XVariant_dataSize(value)
			&& XVariant_compare(valPtr[idx], value) == 0)
			changed = false;
		if (valPtr[idx])
			XClassDelete(valPtr[idx]);
		valPtr[idx] = value;
		if (changed)
			XObject_notifyDynamicPropertyChanged(self, XString_toUtf8(name));
	} else {
		XString* key = XString_create_copy(name);
		if (!key) {
			XClassDelete((XClass*)value);
			return false;
		}
		if (!XVector_push_back_1_base(self->m_dynamicPropertyNames, &key)) {
			XClassDelete(key);
			XClassDelete((XClass*)value);
			return false;
		}
		if (!XVector_push_back_1_base(self->m_dynamicPropertyValues, &value)) {
			XVector_remove_base(self->m_dynamicPropertyNames,
				(int64_t)XContainerSize(self->m_dynamicPropertyNames) - 1, 1);
			XClassDelete(key);
			XClassDelete((XClass*)value);
			return false;
		}
		XObject_notifyDynamicPropertyChanged(self, XString_toUtf8(name));
	}

	/* 静态属性层未实现（deferred）⇒ 动态写入恒返回 false，与 Qt 一致。 */
	return false;
}

/* 动态属性读取（借用）：Qt 6.8 `QObject::property()` 的 C 映射。Qt 按值返回
 * QVariant 拷贝，本实现返回对象内部的借用指针，故按仓库「_const = 借用」约定
 * 命名。2026-11-11 API 收敛批次删除了同实同义的历史入口 XObject_property，
 * 本函数是唯一读取入口。 */
XVariant* XObject_property_const(const XObject* self, const XString* name)
{
	if (!self || !name || !self->m_dynamicPropertyNames) return NULL;

	XVariant* result = NULL;
	for (int i = 0; i < (int)XContainerSize(self->m_dynamicPropertyNames); i++) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		if (namePtr[i] && XString_compare(namePtr[i], name) == XCompare_Equality) {
			XVariant** valPtr = (XVariant**)XContainerDataAddr(self->m_dynamicPropertyValues);
			result = valPtr[i];
			break;
		}
	}
	return result;
}

/* 名字副本元素析构：XVector<XString*> 的 dataDeinit 回调收到的是元素槽位地址。 */
static void XObject_dynamicNameDeinit(void* data)
{
	XString* name = data ? *(XString**)data : NULL;
	if (name)
		XClassDelete((XClass*)name);
}

XVector* XObject_dynamicPropertyNames(const XObject* self)
{
	/* 对标 Qt 6.8 `QList<QByteArray> dynamicPropertyNames() const`：按值返回
	 * ⇒ **拷贝**，调用方拥有。修复前直接返回内部指针（且名字不带 _const），
	 * 调用方既不知情也不敢释放，写进去就等于改对象的属性表。 */
	XVector* copy = NULL;
	size_t n = 0;
	size_t i = 0;
	if (!self) return NULL;
	copy = XVector_Create(XString*);
	if (!copy) return NULL;
	/* 元素随容器一起释放：调用方只需 XClassDelete 返回的 vector。 */
	XContainerSetDataDeinitMethod(copy, XObject_dynamicNameDeinit);
	if (!self->m_dynamicPropertyNames)
		return copy;
	n = XContainerSize(self->m_dynamicPropertyNames);
	for (i = 0; i < n; ++i) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		XString* name = namePtr[i] ? XString_create_copy(namePtr[i]) : NULL;
		if (!name || !XVector_push_back_1_base(copy, &name)) {
			if (name) XClassDelete((XClass*)name);
			XClassDelete(copy);
			return NULL;
		}
	}
	return copy;
}

const XVector* XObject_dynamicPropertyNames_const(const XObject* self)
{
	if (!self || !self->m_dynamicPropertyNames) return NULL;
	return self->m_dynamicPropertyNames;
}

/* 【XObject_removeProperty 已删除（2026-11-11 API 收敛批次）】
 * Qt 6.8 没有 QObject::removeProperty；删除动态属性统一走
 * `XObject_setProperty(self, name, NULL)`（NULL 分支 ⇒
 * XObject_removeDynamicProperty，见本文件上方的静态实现）。
 * 全仓库调用点已迁移：XToolButton.c / XWidget.c / XMenu.c / XFileDialog.c
 * 与 Test/QtAlign/XObjectAlignTest.c。 */

/* 按值读取（Qt `property()` 的返回值语义）：返回独立副本，调用方拥有。 */
XVariant* XObject_property_copy(const XObject* self, const XString* name)
{
	XVariant* borrowed = XObject_property_const(self, name);
	if (!borrowed)
		return NULL;
	return XVariant_create_copy(borrowed);
}

/* ==================== dump 辅助 ==================== */

/* 取对象虚函数表登记的类名（对标 Qt 的 metaObject()->className()）。
 * 名称未编译进构建（XCLASS_VTABLE_NAME_ON=0）时回落 "XObject"。 */
static const char* XObject_className_impl(const XObject* self)
{
	XObject* mutableSelf = (XObject*)self;
	const char* name = NULL;
	if (!self)
		return "XObject";
	name = XVTABLE_GET_NAME(XClassGetVtable(mutableSelf));
	return (name && name[0]) ? name : "XObject";
}

const char* XObject_className(const XObject* self)
{
	return XObject_className_impl(self);
}

bool XObject_inherits(const XObject* self, const char* className)
{
	const XVtable* vtable = NULL;
	if (!self || !className || !className[0])
		return false;
	vtable = XClassGetVtable((XObject*)self);
	/* Qt 6.8 `QObject::inherits`：从自身类沿 superClass() 逐级比对类名
	 * （qobject.h:348 的内联实现 = `qt_metacast(classname) != nullptr`；
	 * 元对象侧接口 qobjectdefs.h:236-239 的
	 * `QMetaObject::inherits(const QMetaObject*)`）。X 的 superClass()
	 * 等价物是 vtable 的 m_parent（见 XVtable.h）。 */
	for (; vtable; vtable = XVtable_parent((XVtable*)vtable))
	{
		const char* name = XVTABLE_GET_NAME((XVtable*)vtable);
		if (name && strcmp(name, className) == 0)
			return true;
	}
	return false;
}

/* 【XObject_isA 已按台账 P2-20 删除（2026-11-13）】：纯别名（原实现逐字
 * 转调 XObject_inherits），Qt 6.8 无 isA 名字；调用点经 XObject.h 的
 * 过渡别名 `#define XObject_isA XObject_inherits` 兜底，主线程终轮替换
 * 后删除。 */

bool XObject_isA_class(const XObject* self, const XVtable* classVtable)
{
	const XVtable* vtable = NULL;
	if (!self || !classVtable)
		return false;
	/* 对标 Qt 的 `T::staticMetaObject.cast(obj)`：沿父链比对**类表指针**。
	 * 类表是每类一份单例（class_init 的静态指针），故"同类的不同实例"
	 * 与"派生类实例"都会命中，而精确类型相等不会。 */
	vtable = XClassGetVtable((XObject*)self);
	for (; vtable; vtable = XVtable_parent((XVtable*)vtable))
	{
		if (vtable == classVtable)
			return true;
	}
	return false;
}

static const char* XObject_nameUtf8(const XObject* self)
{
	const char* utf8 = NULL;
	if (!self || !self->m_object_name)
		return "";
	/* XString_toUtf8 对空串返回 NULL（XString.c:511 `isEmpty → NULL`），
	 * 直接交给 "%s" 是 UB，这里统一折成空串。 */
	utf8 = XString_toUtf8(self->m_object_name);
	return utf8 ? utf8 : "";
}

static void XObject_dumpObjectTree_helper(const XObject* self, int depth)
{
	int i = 0;
	size_t n = 0;
	if (!self)
		return;
	/* Qt 6.8 实测格式：每层 4 空格缩进 + `<类名>::<objectName>` + 一个空格
	 * （Qt 的尾随空格来自 flags 占位）；无名对象输出 `<类名>::`。 */
	for (i = 0; i < depth; ++i)
		XPrintf("    ");
	XPrintf("%s::%s \n", XObject_className(self), XObject_nameUtf8(self));

	if (!self->m_children)
		return;
	n = XContainerSize(self->m_children);
	for (i = 0; i < (int)n; ++i) {
		XObject** childPtr = (XObject**)XContainerDataAddr(self->m_children);
		if (childPtr[i])
			XObject_dumpObjectTree_helper(childPtr[i], depth + 1);
	}
}

// Qt 6.8: dumpObjectTree - 递归打印对象树 (对标 QObject::dumpObjectTree)
void XObject_dumpObjectTree(const XObject* self)
{
	/* 层级改为**参数**传递：修复前用函数级 static depth，非线程安全
	 * （两个线程同时 dump 会互相污染缩进），且与 Qt 的"按层级缩进"不可比。 */
	XObject_dumpObjectTree_helper(self, 0);
}

static void XObject_dumpOutConnections(const XObject* self)
{
	XSignalSlot* manager = self ? self->m_signalSlot : NULL;
	bool printed = false;
	if (!manager || !manager->signalMap) {
		XPrintf("        <None>\n");
		return;
	}
	if (manager->mutex) XMutex_lock(manager->mutex);
	for_each_iterator(((XHashMap*)manager->signalMap), XHashMap, it) {
		XSignal* signal = (XSignal*)XPair_second(XHashMap_iterator_data(&it));
		if (!signal || !signal->connList)
			continue;
		for_each_iterator(signal->connList, XVector, cit) {
			XConnection* conn = *(XConnection**)XVector_iterator_data(&cit);
			if (!conn)
				continue;
			/* Qt 打印 `signal: <签名>` + 下一行 `-> <接收者>`；X 的信号标识是
			 * 信号函数地址，没有签名表，以地址代替。 */
			XPrintf("        signal 0x%zx\n", signal->type);
			if (conn->receiver)
				XPrintf("          -> %s::%s\n", XObject_className(conn->receiver),
					XObject_nameUtf8(conn->receiver));
			else
				XPrintf("          -> <functor or function pointer>\n");
			printed = true;
		}
	}
	if (manager->mutex) XMutex_unlock(manager->mutex);
	if (!printed)
		XPrintf("        <None>\n");
}

static void XObject_dumpInConnections(const XObject* self)
{
	XSignalSlot* manager = self ? self->m_signalSlot : NULL;
	bool printed = false;
	if (!manager || !manager->bindSignalList) {
		XPrintf("        <None>\n");
		return;
	}
	if (manager->mutex) XMutex_lock(manager->mutex);
	for_each_iterator(manager->bindSignalList, XVector, it) {
		XConnection* conn = *(XConnection**)XVector_iterator_data(&it);
		XObject* sender = (conn && conn->signal) ? conn->signal->sender : NULL;
		if (!sender)
			continue;
		/* Qt 输出 `          <-- <发送者类名>::<发送者名> <未知槽>` */
		XPrintf("          <-- %s::%s\n", XObject_className(sender),
			XObject_nameUtf8(sender));
		printed = true;
	}
	if (manager->mutex) XMutex_unlock(manager->mutex);
	if (!printed)
		XPrintf("        <None>\n");
}

// Qt 6.8: dumpObjectInfo - 打印对象详细信息 (对标 QObject::dumpObjectInfo)
void XObject_dumpObjectInfo(const XObject* self)
{
	if (!self) return;
	/* Qt 6.8 实测输出：
	 *   OBJECT QObject::root
	 *     SIGNALS OUT
	 *           <None>
	 *     SIGNALS IN
	 *           <None>
	 * 修复前打印的是 THREAD/PARENT/CHILDREN/EVENT FILTERS/POSTED EVENTS/FLAGS
	 * 自造字段——Qt 该 API 的本职（连接表）完全缺失，且类名硬编码。 */
	XPrintf("OBJECT %s::%s\n", XObject_className(self), XObject_nameUtf8(self));
	XPrintf("  SIGNALS OUT\n");
	XObject_dumpOutConnections(self);
	XPrintf("  SIGNALS IN\n");
	XObject_dumpInConnections(self);
}
