#include"XSignalSlot.h"
#include"XMemory.h"
#include"XHashMap.h"
#include"XVector.h"
#include"XObject.h"
#include"XObject_Protected.h"
#include"XCoreApplication.h"
#include"XMutex.h"
#include"XThread.h"
#include"XSemaphore.h"
#include"XThreadData.h"
#include"XAtomic.h"
//全局连接互斥锁:所有信号-槽连接维护(connect/disconnect/deinit/emit)共用同一把锁,
//消除"发送者锁->接收者锁"的 AB-BA 死锁与析构锁序问题(对标 Qt 全局连接锁)
static XAtomic_Uintptr g_connMutex = { 0 };
static XMutex* XSignalSlot_connMutex(void)
{
	XMutex* m = (XMutex*)XAtomic_load_uintptr_t(&g_connMutex, XAtomic_MemoryOrder_Acquire);
	if (m)
		return m;
	XMutex* nm = XMutex_create(XLock_NonRecursive);
	if (nm)
	{
		uintptr_t expected = 0;
		if (XAtomic_compare_exchange_strong_uintptr_t(&g_connMutex, &expected, (uintptr_t)nm, XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Acquire))
			return nm;
		XMutex_delete(nm);  //竞态失败:另一线程已创建,释放本次冗余
		return (XMutex*)expected;
	}
	return NULL;
}

/* ==================== 活连接表（句柄悬垂防线） ====================
 * 对标 Qt 6.8 `QMetaObject::Connection` 的「句柄比裸指针安全」这一条可观测
 * 性质（qobjectdefs.h:645-668 的引用计数句柄）：Qt 里对已断开的 Connection
 * 再 disconnect 只是返回 false，绝不触碰已释放内存。C 侧句柄就是裸指针，
 * 断开后连接对象被释放 ⇒ 旧句柄就是悬垂指针（全仓数十处 XObject_disconnect_2
 * 都有重复断开风险）。
 *
 * 这里维护「**已分配但未释放**的连接对象」集合：分配时登记、真正释放时注销。
 * 所有接受句柄的 API（disconnect/retain/release/isValid）先查集合再解引用，
 * 因此已释放的旧指针只会得到 false，不会被当连接读。
 *
 * 线程模型：本表与连接表共用同一把全局连接锁（XSignalSlot_connMutex）；
 * 登记/注销点都在已持锁的路径上（connect1 / XConnection_dataDeinit），
 * 查询点在公共 API 内自行加锁——**不要**在已持锁区里再调公共 API（锁非递归）。
 *
 * 残余限制（已知并接受）：指针地址被 malloc 复用时，旧句柄会指向**新**连接
 * （ABA）。Qt 的引用计数句柄靠「ConnectionData 比句柄活得久」天然免疫；
 * C 无值类型句柄无法完全消除。需要长期保存句柄的调用方用 XConnection_retain
 * 持有引用即可避免（引用在位时对象不会被释放、地址不会被复用）。
 *
 * 生命周期：与全局连接锁 g_connMutex 同口径，**进程级单例**，首次连接时懒
 * 创建、进程退出时随地址空间回收（表内条目在连接释放时逐个注销，稳态大小 =
 * 当前活连接数）。这与 Qt 把连接表挂在 sender 上的做法不同，是多出来的
 * 一份指针集合开销（每活连接一个哈希项）。 */
static XMap* g_liveConnections = NULL;   /* <uintptr_t, uint8_t>，守护于 g_connMutex */

static bool XConnection_registryAdd(XConnection* conn)
{
	if (!conn)
		return false;
	if (!g_liveConnections)
	{
		g_liveConnections = XHashMap_Create(uintptr_t, uint8_t, Xuintptr_t_compare);
		if (!g_liveConnections)
			return false;
	}
	uintptr_t key = (uintptr_t)conn;
	uint8_t one = 1;
	return XMapBase_insert_base(g_liveConnections, &key, &one);
}

static void XConnection_registryRemove(XConnection* conn)
{
	if (!conn || !g_liveConnections)
		return;
	uintptr_t key = (uintptr_t)conn;
	XMapBase_remove_base(g_liveConnections, &key);
}

/* 调用方必须持有全局连接锁。 */
static bool XConnection_registryContains(const XConnection* conn)
{
	if (!conn || !g_liveConnections)
		return false;
	uintptr_t key = (uintptr_t)conn;
	return XMapBase_value_base(g_liveConnections, &key) != NULL;
}

/* 引用归零的回收（调用方必须持有全局连接锁）：注销句柄 + 释放内存。 */
static void XConnection_freeLocked(XConnection* conn)
{
	if (!conn)
		return;
	XConnection_registryRemove(conn);
	XFree_System(conn);
}

// connList 以 XConnection* 指针(堆分配)存储,设置元素释放回调,
// 使得容器在删除/移除/清空元素时自动回收连接对象的堆内存
static void XConnection_dataDeinit(void* data)
{
	XConnection* conn = *(XConnection**)data;
	if (!conn)
		return;
	/* 断开即"这条连接不复存在"：把 signal/receiver 清空，令**保留中的**
	 * 句柄可以安全地继续被检查（否则 conn->signal 指向的信号随发送者析构
	 * 失效，任何解引用都是 UAF）。对象本身仍留在活表里，直到引用归零。 */
	conn->signal = NULL;
	conn->receiver = NULL;
	/* 发送者连接表持有的那 1 个引用在此释放。若调用方 XConnection_retain
	 * 过，引用计数仍 >0 ⇒ 连接对象保留（句柄只剩 isValid()==false），等最后
	 * 一次 XConnection_release 才真正释放。 */
	if (XAtomic_fetch_sub_int32(&conn->m_ref, 1, XAtomic_MemoryOrder_AcqRel) == 1)
		XConnection_freeLocked(conn);
}
static const bool XConnection_equal(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	return ((pvNextValue->receiver) ? (pvPrevValue->receiver == pvNextValue->receiver) : true) && (pvPrevValue->signal == pvNextValue->signal) && ((pvNextValue->slot_func1) ? (pvPrevValue->slot_func1 == pvNextValue->slot_func1):true);
}
XSignalSlot* XSignalSlot_create(XObject* obj)
{
	if (obj == NULL)
		return NULL;
	XSignalSlot* manager = XClass_Malloc(XSignalSlot);
	if (manager == NULL)
		return NULL;
	XSignalSlot_init(manager,obj);
	return manager;
}

void XSignalSlot_init(XSignalSlot* manager, XObject* obj)
{
	if (manager == NULL)
		return;
	manager->obj = obj;
	manager->signalMap = XHashMap_Create(size_t, XSignal,Xsize_t_compare);
	manager->bindSignalList = XVector_Create(XConnection*);
	XContainerSetCompare(manager->bindSignalList, Xuintptr_t_compare);
	// 初始化互斥锁
	manager->mutex = XSignalSlot_connMutex();
}
void XSignalSlot_deinit(XSignalSlot* manager)
{
	if (manager == NULL)
		return;
	// 加锁保护销毁过程
	XMutex_lock(manager->mutex);
	//清除绑定的槽(本对象作为发送者)
	for_each_iterator(manager->signalMap, XHashMap, it)
	{
		XSignal* signal = XPair_second(XHashMap_iterator_data(&it));
		if (signal == NULL)
			continue;
		for_each_iterator(signal->connList, XVector, cit)
		{
			XConnection* conn = *(XConnection**)XVector_iterator_data(&cit);
			if (conn && conn->receiver && !(conn->type & XConnectionType_RawReceiver) && conn->receiver->m_signalSlot)
			{//在接收者中删除指向(RawReceiver 的 receiver 非 XObject,无簿记可清)
				XSignalSlot* recvSS = conn->receiver->m_signalSlot;
				XVector_remove_base(recvSS->bindSignalList, XVector_indexOf(recvSS->bindSignalList, &conn,0),1);
			}
		}
		XClassDelete(signal->connList);  // 释放连接堆内存
		signal->connList = NULL;
	}
	XClassDelete(manager->signalMap);
	manager->signalMap = NULL;
	//清除链接的信号(本对象作为接收者)
	for_each_iterator(manager->bindSignalList, XVector, it)
	{
		XConnection* conn = (*(XConnection**)XVector_iterator_data(&it));
		if(conn==NULL)
			continue;
		XSignal* signalObj = conn->signal;
		if (signalObj == NULL)
			continue;
		//从发送者的连接列表中移除(会释放连接堆内存)
		XVector_remove_base(signalObj->connList, XVector_indexOf(signalObj->connList, &conn,0),1);
	}
	XClassDelete(manager->bindSignalList);
	manager->bindSignalList = NULL;

	manager->obj = NULL;
	// 解锁(互斥锁为全局共享,不在此销毁)
	XMutex_unlock(manager->mutex);
	manager->mutex = NULL;
}
void XSignalSlot_delete(XSignalSlot* manager)
{
	XSignalSlot_deinit(manager);
	if (manager)
		XFree_System(manager);
}
bool XSignalSlot_isSignalConnected(const XSignalSlot* manager, size_t signal)
{
	return XSignalSlot_receivers(manager,signal);
}
int XSignalSlot_receivers(const XSignalSlot* manager, size_t signal)
{
	if(!manager|| XMapBase_isEmpty_base(manager->signalMap))return 0;
	int size = 0;
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj = XMapBase_value_base(manager->signalMap, &signal);
	if (signalObj == NULL)goto end;
	size=XVector_size_base(signalObj->connList);
end:
	XMutex_unlock(manager->mutex);  // 解锁
	return size;
}
XConnection* XSignalSlot_connect1(XSignalSlot* manager,size_t signal, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
{
	if(manager==NULL||slot_func==NULL)
		return NULL;
	/* Qt 6.8：连接一个"不存在的信号"（signal==nullptr）打
	 * `QObject::connect: No such signal` 并返回无效连接。X 的信号标识是信号
	 * 函数地址，任何非 0 地址都无法从函数指针反查合法性（无 moc 元数据），
	 * 故只守 0 这个确定的非法值——旧实现会给 0 号"信号"静默建桶，桶永不
	 * 被发射，调用方以为连上了。（按名字/签名校验需要每个信号登记签名表，
	 * 属新子系统，见 gaps 的 deferred 说明。） */
	if (signal == 0)
	{
		XERROR_PRINTF("XObject::connect: No such signal (id=0)\n");
		return NULL;
	}
	if (receiver == NULL && ((type & 0x3) == XConnectionType_Queued || (type & 0x3) == XConnectionType_BlockingQueued))
		return NULL;//没有接收对象不允许用队列的方式
	if ((type & XConnectionType_RawReceiver) && ((type & 0x3) != XConnectionType_Direct))
		return NULL;//RawReceiver 只允许 Direct(队列投递需访问 receiver->m_*)
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj= XMapBase_value_base(manager->signalMap,&signal);
	if (signalObj == NULL)
	{
		XSignal insert = {.sender=manager->obj,.type= signal,.connList= XVector_Create(XConnection*)};
		XContainerSetCompare(insert.connList, Xuintptr_t_compare);
		XContainerSetDataDeinitMethod(insert.connList, XConnection_dataDeinit);
		if (!XMapBase_insert_base(manager->signalMap, &signal, &insert))
		{//插入失败(如内存不足):新建的connList无人接管,必须在此释放,否则泄漏
			if (insert.connList)
				XClassDelete(insert.connList);
			XMutex_unlock(manager->mutex);  // 解锁
			return NULL;
		}
		signalObj = XMapBase_value_base(manager->signalMap, &signal);
	}
	if (signalObj == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁
		return NULL;
	}
	//判断是否重复添加
	XConnection connTemp = {.type=type,.signal= signalObj,.receiver=receiver,.slot_func1=slot_func};
	for_each_iterator(signalObj->connList, XVector,it)
	{
		if (XConnection_equal(*(XConnection**)XVector_iterator_data(&it), &connTemp) && (type & XConnectionType_Unique))
		{
			XMutex_unlock(manager->mutex);  // 解锁
			return NULL;//重复了
		}
	}
	//添加(堆分配连接对象,保证返回的句柄地址稳定,不会被容器扩容/搬移失效)
	XConnection* conn = XNew(XConnection);
	if (conn == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁
		return NULL;
	}
	conn->type = type;
	conn->signal = signalObj;
	conn->receiver = receiver;
	conn->slot_func1 = slot_func;
	/* 引用计数：1 = 发送者连接表持有的那一份（对标 Qt 里 ConnectionData
	 * 由 sender 的连接表持有；调用方要长期保存句柄自行 retain）。 */
	XAtomic_init(conn->m_ref, 1);
	if (!XConnection_registryAdd(conn))
	{	/* 活表登记失败（OOM）时不建立连接：否则断开后旧句柄无法识别。 */
		XFree_System(conn);
		XMutex_unlock(manager->mutex);  // 解锁
		return NULL;
	}
	if (!XVector_push_back_1_base(signalObj->connList,&conn))
	{	/* 入表失败：连接对象无人持有 ⇒ 当场注销并释放（旧实现静默泄漏）。 */
		XConnection_registryRemove(conn);
		XFree_System(conn);
		XMutex_unlock(manager->mutex);  // 解锁
		return NULL;
	}
	bool needNotify = false;
	size_t notifySignal = 0;
	/* [2026-11-11 对齐 Qt] connectNotify/disconnectNotify 通知的是**信号所属
	 * 对象**（sender），不是接收者。Qt 6.8.3 原文（qobject.cpp:3469-3470）：
	 * "This virtual function is called when something has been connected
	 *  to \a signal in this object." —— this 就是发声的那个对象；本机 Qt 6.8
	 * 探针实测 connect 后命中对象名 "sender" 1 次、接收者 0 次（disconnect
	 * 同理，Qt 在 qobject.cpp:1083/3430 都是在 **sender** 上调用
	 * disconnectNotify）。旧实现把通知打在 receiver 上，子类按 Qt 写法重载
	 * connectNotify 想统计"我的信号被谁连了"时永远收不到。 */
	if (manager->obj)
	{
		needNotify = true;
		notifySignal = signal;
	}
	/* receiver 簿记：RawReceiver 除外（receiver 非 XObject，不可写其
	 * m_signalSlot）。ContextReceiver 需要簿记——它的 receiver 就是 Qt 的
	 * context，连接随 context 析构自动断开正是靠这条链。 */
	if (receiver && receiver != manager->obj && !(type & XConnectionType_RawReceiver))
	{
		if(receiver->m_signalSlot==NULL)
			receiver->m_signalSlot = XSignalSlot_create(receiver);
		if (receiver->m_signalSlot)
		{
			if (!XVector_push_back_1_base(receiver->m_signalSlot->bindSignalList, &conn))
			{	/* 接收者侧登记失败：连接不完整（context 仍活时也无法反注册）⇒
				 * 回滚发送者侧，按失败返回，避免半条连接。 */
				XVector_remove_base(signalObj->connList,
					XVector_indexOf(signalObj->connList, &conn, 0), 1);
				XMutex_unlock(manager->mutex);
				return NULL;
			}
		}
	}
	XMutex_unlock(manager->mutex);  // 解锁
	if (needNotify)
		XObject_connectNotify_base(manager->obj, notifySignal);
	return conn;
}
XConnection* XSignalSlot_connect2(XSignalSlot* manager, size_t signal, XSlotFunc2 slot_func)
{
	return XSignalSlot_connect1(manager, signal,NULL,slot_func,XConnectionType_Auto);
}
//断开并释放连接;通过出参返回通知信息,由调用方在解锁后触发回调,
//避免子类重载disconnectNotify重入信号槽系统时与非递归锁死锁。
//通知对象按 Qt 6.8 口径是**信号所属对象**(signalObj->sender)而非接收者,
//见 XSignalSlot_connect1 里 connectNotify 的说明。
static bool XConnection_disconnectInternal(XConnection* conn, XObject** outNotifyObject, size_t* outNotifySignal)
{
	if (outNotifyObject)
		*outNotifyObject = NULL;  // 默认无需通知
	if (conn == NULL)
		return false;
	XSignal* signalObj = conn->signal;
	if (signalObj == NULL)
		return false;
	//必须在释放连接前捕获通知信息(下方移除会释放conn堆内存)
	if (outNotifyObject && outNotifySignal)
	{
		*outNotifyObject = signalObj->sender;
		*outNotifySignal = signalObj->type;
	}
	if (conn->receiver && !(conn->type & XConnectionType_RawReceiver))
	{//存在接收对象(RawReceiver 的 receiver 非 XObject,只做发送者侧移除)
		if (conn->receiver->m_signalSlot)
		{
			int idx = XVector_indexOf(conn->receiver->m_signalSlot->bindSignalList, &conn, 0);
			if (idx >= 0)
				XVector_remove_base(conn->receiver->m_signalSlot->bindSignalList, idx, 1);
		}
	}
	//从发送者连接列表移除(会释放连接堆内存)
	XVector_remove_base(signalObj->connList, XVector_indexOf(signalObj->connList, &conn, 0), 1);
	return true;
}
bool XSignalSlot_disconnect1(XSignalSlot* manager, size_t signal, XObject* receiver, XSlotFunc1 slot_func1)
{
	if(manager==NULL||slot_func1==NULL)
		return false;
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj = XMapBase_value_base(manager->signalMap, &signal);
	if (signalObj == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁(修复:原代码此处漏解锁导致死锁)
		return false;
	}
	//查找匹配的连接
	XConnection connTemp = { .signal = signalObj,.receiver = receiver,.slot_func1 = slot_func1 };
	for_each_iterator(signalObj->connList, XVector, it)
	{
		XConnection* cur = *(XConnection**)XVector_iterator_data(&it);
		if (cur && XConnection_equal(cur, &connTemp))
		{//找到了
			XObject* notifyObject = NULL;
			size_t notifySignal = 0;
			bool is_ok= XConnection_disconnectInternal(cur, &notifyObject, &notifySignal);
			XMutex_unlock(manager->mutex);  // 解锁
			if (is_ok && notifyObject)
				XObject_disconnectNotify_base(notifyObject, notifySignal);
			return is_ok;
		}
	}
	XMutex_unlock(manager->mutex);  // 解锁
	return false;
}

bool XSignalSlot_disconnect2(XConnection* conn)
{
	if (conn == NULL)
		return false;
	XMutex* mutex = XSignalSlot_connMutex();
	XMutex_lock(mutex);  // 全局连接锁
	/* 句柄安全闸：只解引用**活表里**的连接。已断开并释放的旧句柄在这里
	 * 直接返回 false（对标 Qt 对失效 Connection 再 disconnect 的语义），
	 * 不触碰可能已被复用的内存。 */
	if (!XConnection_registryContains(conn))
	{
		XMutex_unlock(mutex);
		return false;
	}
	XObject* notifyObject = NULL;
	size_t notifySignal = 0;
	bool is_ok = XConnection_disconnectInternal(conn, &notifyObject, &notifySignal);
	XMutex_unlock(mutex);
	if (is_ok && notifyObject)
		XObject_disconnectNotify_base(notifyObject, notifySignal);
	return is_ok;
}
//信号发射时，槽函数会立即被调用
static void XConnection_directEmit(XConnection* conn, void* args,  XAtomic_Int32* ref_count)
{
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子加1
	if(conn->slot_func1)
	{
		/* ContextReceiver（Qt 的 context 重载）：receiver 只做生命周期/线程
		 * 亲和簿记，不是槽的首参 ⇒ 走 slot_func2（首参=发送者），
		 * 也不 pushSender（context 不是"正在被调用的接收者"）。 */
		if(conn->receiver && !(conn->type & XConnectionType_ContextReceiver))
		{
			//用每线程发送者栈替代每对象 m_sender,消除跨线程竞争与嵌套发射错乱
			XThreadData_pushSender(conn->receiver, conn->signal->sender, conn->signal->type);
			conn->slot_func1(conn->receiver, args);
			XThreadData_popSender();
		}
		else if (conn->slot_func2)
		{
			conn->slot_func2(conn->signal->sender,args);
		}
	}
	if (ref_count)
		XAtomic_fetch_sub_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed);
}
//槽函数会在接收者线程的事件循环回归控制时被调用。槽函数在接收者所属线程中执行。
static void XConnection_queuedEmit(XConnection* conn, void* args, XAtomic_Int32* ref_count, int priority)
{
	if (conn->receiver == NULL)
		return;
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子加1
	//向接收者对象投递函数事件
	XMetaCallEvent* event = XMetaCallEvent_create(conn->signal->sender, conn->slot_func1, conn->signal->type, args, ref_count, NULL);
	if (event == NULL)
	{//事件创建失败,回退引用计数,若为最后引用则释放参数
		if (ref_count && XAtomic_fetch_sub_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 1)
		{
			if (args) XVarList_delete(args);
			XAtomic_delete(ref_count);
		}
		return;
	}
	/* ContextReceiver 的队列派发：事件目标仍是 context（在它的线程执行），
	 * 但槽要按"首参=发送者"调用 ⇒ 打上 functor 标志（见 XMetaCallEvent_handler）。 */
	if (conn->type & XConnectionType_ContextReceiver)
		event->m_isFunctor = true;
	XCoreApplication_postEvent(conn->receiver, event, priority);
}

//（槽函数在接收者线程执行），区别在于发送信号的线程会阻塞，直到槽函数执行完成后才继续。
static void XConnection_blockingQueuedEmit(XConnection* conn, void* args, XAtomic_Int32* ref_count, int priority)
{
	if (conn->receiver == NULL)
		return;
	//当前发送线程和接收线程是同一个不允许，会有死锁问题
	if (XThread_currentThread() == XObject_thread(conn->receiver))
	{
		// 相同线程：直接报错或返回，避免死锁
		XPrintf("BlockingQueued: 发送线程与接收者线程相同，可能导致死锁\n");
		return;  // 或触发断言：XASSERT(false, "线程相同错误");
	}
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子加1
	//使用信号量进行同步(初始资源为0,阻塞等待接收者线程执行完槽函数后释放)
	XSemaphore* sem = XSemaphore_create(0,1);
	if (!sem)
	{//信号量创建失败,回退引用计数
		if (ref_count && XAtomic_fetch_sub_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 1)
		{
			if (args) XVarList_delete(args);
			XAtomic_delete(ref_count);
		}
		return;
	}
	//向接收者对象投递信号事件(传入信号量,槽函数执行完后由事件handler释放,从而唤醒发送线程)
	XMetaCallEvent* event = XMetaCallEvent_create(conn->signal->sender, conn->slot_func1, conn->signal->type, args, ref_count, sem);
	if (event == NULL)
	{
		if (ref_count && XAtomic_fetch_sub_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 1)
		{
			if (args) XVarList_delete(args);
			XAtomic_delete(ref_count);
		}
		XSemaphore_delete(sem);
		return;
	}
	if (conn->type & XConnectionType_ContextReceiver)
		event->m_isFunctor = true;   /* 同 queuedEmit：context 不当槽首参 */
	XCoreApplication_postEvent(conn->receiver, event, priority);
	XSemaphore_acquire(sem,1);//阻塞等待接收者线程处理完成
	XSemaphore_delete(sem);//释放信号量
}
//若接收者与发送信号的线程处于同一线程，则使用 Qt::DirectConnection（直接连接）；否则，使用 Qt::QueuedConnection（队列连接）。连接类型会在信号发射时动态确定。
static void XConnection_autoEmit(XConnection* conn, void* args, XAtomic_Int32* ref_count, int priority)
{
	if (conn->receiver == NULL)
	{
		XConnection_directEmit(conn, args, ref_count);
	}
	else
	{
		if (XThread_currentThread() == XObject_thread(conn->receiver))
			XConnection_directEmit(conn, args, ref_count);
		else
			XConnection_queuedEmit(conn, args,ref_count, priority);
	}
}

//释放本emit对参数持有的引用;若为最后引用则释放参数与引用计数
static void XSignalSlot_emitReleaseRef(XVarList* args, XAtomic_Int32* ref_count)
{
	if (ref_count && XAtomic_fetch_sub_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 1)
	{
		if (args)
			XVarList_delete(args);
		XAtomic_delete(ref_count);
	}
}
static void XSignalSlot_emitLocked(XSignalSlot* manager, size_t signal, XVarList* args, XAtomic_Int32* ref_count, int priority)
{
	if (manager == NULL)
		return;
	//本emit获取一次参数引用,确保遍历调用槽函数期间参数不会被提前释放
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed);
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj = XMapBase_value_base(manager->signalMap, &signal);
	if (signalObj == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁
		XSignalSlot_emitReleaseRef(args, ref_count);  // 没有连接的信号:释放本emit引用
		return;
	}
	//拷贝连接列表快照,避免在调用槽函数时持锁导致重入死锁(锁为非递归)
	//快照中signal指向栈上稳定副本,避免map扩容导致signalObj指针失效
	XSignal senderSignal = { .sender = manager->obj,.type = signal,.connList = NULL };
	XVector* snapshot = XVector_Create(XConnection);
	if (snapshot == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁
		XSignalSlot_emitReleaseRef(args, ref_count);
		return;
	}
	for_each_iterator(signalObj->connList, XVector, it)
	{
		XConnection* src = *(XConnection**)XVector_iterator_data(&it);
		if (src == NULL)
			continue;
		XConnection connCopy = *src;
		connCopy.signal = &senderSignal;
		XVector_push_back_1_base(snapshot, &connCopy);
	}
	//移除单次连接:用基于索引的遍历+remove,避免erase将迭代器置空导致只能移除首个单次连接
	for (int64_t i = 0; i < (int64_t)XVector_size_base(signalObj->connList); )
	{
		XConnection* conn = *(XConnection**)XVector_at_base(signalObj->connList, i);
		if (conn == NULL || !(conn->type & XConnectionType_SingleShot))
		{
			i++;
			continue;
		}
		//从接收者绑定表中移除指向(全局连接锁已持有,自连接时即本锁,无需重复加锁)
		if (conn->receiver && !(conn->type & XConnectionType_RawReceiver) && conn->receiver->m_signalSlot)
		{
			XSignalSlot* recvSS = conn->receiver->m_signalSlot;
			XVector_remove_base(recvSS->bindSignalList, XVector_indexOf(recvSS->bindSignalList, &conn, 0), 1);
		}
		//从发送者连接列表移除(触发dataDeinit释放连接堆内存),后续元素左移,索引i不变指向下一个
		XVector_remove_base(signalObj->connList, i, 1);
	}
	XMutex_unlock(manager->mutex);  // 解锁后再调用槽函数,避免重入死锁
	//遍历快照调用槽函数
	for (XVector_iterator it = XVector_begin(snapshot), endIt = XVector_end(snapshot); !XVector_iterator_equality(&it, &endIt); XVector_iterator_add(snapshot, &it))
	{
		XConnection* conn = XVector_iterator_data(&it);
		switch (conn->type & 0x3)  // 屏蔽Unique/SingleShot等标志位,取基础连接类型
		{
		case XConnectionType_Auto:XConnection_autoEmit(conn, args,ref_count,priority); break;
		case XConnectionType_Direct:XConnection_directEmit(conn, args,ref_count); break;
		case XConnectionType_Queued:XConnection_queuedEmit(conn, args,ref_count,priority); break;
		case XConnectionType_BlockingQueued:XConnection_blockingQueuedEmit(conn, args,ref_count, priority); break;
		}
	}
	XClassDelete(snapshot);
	XSignalSlot_emitReleaseRef(args, ref_count);  // 释放本emit引用
}
void XSignalSlot_emit(XSignalSlot* manager, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_Int32* ref_count, int priority)
{
	if (args)
		args->argsDel = del;
	if (manager == NULL)
	{
		if (args)
			XVarList_delete(args);
		if (ref_count)
			XAtomic_delete(ref_count);
		return;
	}
	//有参数时必须使用引用计数,以便队列连接能安全共享参数(避免发送方提前释放导致接收者使用悬垂参数)
	if (args && !ref_count)
		ref_count = XAtomic_create(Int32);
	if (args && !ref_count)
	{
		//引用计数创建失败:无计数时所有释放路径都不会回收参数,先释放参数再返回防止泄漏
		XVarList_delete(args);
		return;
	}

	XSignalSlot_emitLocked(manager, signal, args, ref_count, priority);
}

/* ==================== 连接句柄（对标 Qt 6.8 QMetaObject::Connection） ====================
 * 三个入口都先查「活连接表」再解引用：已释放的旧句柄只会得到 false。
 * 查表与引用计数变更都在全局连接锁内完成（锁非递归 ⇒ 这里不能再调
 * 已持锁的内部函数）。 */
/* 句柄工厂：Qt 的 Connection 由 connect 返回，X 侧不复制连接逻辑，逐字转调
 * 裸指针入口（单一事实源）。无效信号 id 的告警/拒绝在 XSignalSlot_connect1
 * 内统一处理，这里自然继承同一口径。 */
XConnectionHandle* XConnectionHandle_create(XObject* sender, size_t signal,
                                            XObject* receiver, XSlotFunc1 slot_func,
                                            XConnectionType type)
{
	return XObject_connect_1(sender, signal, receiver, slot_func, type);
}

XConnectionHandle* XConnectionHandle_create2(XObject* sender, size_t signal,
                                             XSlotFunc2 slot_func)
{
	return XObject_connect_2(sender, signal, slot_func);
}

bool XConnection_isValid(const XConnection* conn)
{
	XMutex* mutex = NULL;
	bool valid = false;
	if (!conn)
		return false;
	mutex = XSignalSlot_connMutex();
	if (!mutex)
		return false;
	XMutex_lock(mutex);
	valid = XConnection_registryContains(conn) && conn->signal != NULL;
	XMutex_unlock(mutex);
	return valid;
}

bool XConnection_retain(XConnection* conn)
{
	XMutex* mutex = NULL;
	if (!conn)
		return false;
	mutex = XSignalSlot_connMutex();
	if (!mutex)
		return false;
	XMutex_lock(mutex);
	if (!XConnection_registryContains(conn))
	{
		XMutex_unlock(mutex);
		return false;
	}
	XAtomic_fetch_add_int32(&conn->m_ref, 1, XAtomic_MemoryOrder_Relaxed);
	XMutex_unlock(mutex);
	return true;
}

bool XConnection_release(XConnection* conn)
{
	XMutex* mutex = NULL;
	int32_t left = 0;
	if (!conn)
		return false;
	mutex = XSignalSlot_connMutex();
	if (!mutex)
		return false;
	XMutex_lock(mutex);
	if (!XConnection_registryContains(conn))
	{	/* 已释放的旧指针：只返回 false，不解引用。 */
		XMutex_unlock(mutex);
		return false;
	}
	/* 调用方只能释放**自己 retain 的那一份**（Qt 的 Connection 析构同理：
	 * 发送者连接表那一份永远由 disconnect / ~QObject 释放）。
	 * C 侧句柄是裸指针，分不清"哪一份"，但可按可判定状态拒绝误用：
	 * 连接**仍有效**（signal != NULL）且引用计数只剩 1 ⇒ 这一份必然是
	 * 发送者的；此时若减到 0 会当场 XFree_System，而连接指针仍挂在
	 * signalObj->connList 里 ⇒ 下一次发射读已释放内存（UAF）。
	 * 断开之后 signal 被清空，剩下的引用必然是调用方 retain 的，照常释放。 */
	if (conn->signal != NULL &&
		XAtomic_load_int32(&conn->m_ref, XAtomic_MemoryOrder_Acquire) <= 1)
	{
		XMutex_unlock(mutex);
		XERROR_PRINTF("XConnection::release: refusing to release the sender's reference "
			"(retain the handle before releasing it)\n");
		return false;
	}
	left = XAtomic_fetch_sub_int32(&conn->m_ref, 1, XAtomic_MemoryOrder_AcqRel) - 1;
	if (left <= 0)
	{
		XConnection_freeLocked(conn);
		XMutex_unlock(mutex);
		return true;
	}
	XMutex_unlock(mutex);
	return false;
}

bool XConnection_disconnect(XConnection* conn)
{
	return XSignalSlot_disconnect2(conn);
}
