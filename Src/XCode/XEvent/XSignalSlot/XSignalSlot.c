#include"XSignalSlot.h"
#include"XMemory.h"
#include"XHashMap.h"
#include"XVector.h"
#include"XObject.h"
#include"XCoreApplication.h"
#include"XMutex.h"
#include"XThread.h"
#include"XSemaphore.h"
#include"XThreadData.h"
#include"XAtomic.h"
//全局连接互斥锁:所有信号-槽连接维护(connect/disconnect/deinit/emit)共用同一把锁,
//消除"发送者锁->接收者锁"的 AB-BA 死锁与析构锁序问题(对标 Qt 全局连接锁)
static XAtomic_uintptr_t g_connMutex = { 0 };
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
// connList 以 XConnection* 指针(堆分配)存储,设置元素释放回调,
// 使得容器在删除/移除/清空元素时自动回收连接对象的堆内存
static void XConnection_dataDeinit(void* data)
{
	XConnection* conn = *(XConnection**)data;
	if (conn)
		XFree_System(conn);
}
static const bool Equality_Connection(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	return ((pvNextValue->receiver) ? (pvPrevValue->receiver == pvNextValue->receiver) : true) && (pvPrevValue->signal == pvNextValue->signal) && ((pvNextValue->slot_func1) ? (pvPrevValue->slot_func1 == pvNextValue->slot_func1):true);
}
XSignalSlot* XSignalSlot_create(XObject* obj)
{
	if (obj == NULL)
		return NULL;
	XSignalSlot* manager = XNew(XSignalSlot);
	if (manager == NULL)
		return NULL;
	XSignalSlot_init(manager,obj);
	Set_Class_MemoryFree(obj, XFree_System);
	return manager;
}

void XSignalSlot_init(XSignalSlot* manager, XObject* obj)
{
	if (manager == NULL)
		return;
	manager->obj = obj;
	manager->signalMap = XHashMap_Create(size_t, XSignal,size_t_compare);
	manager->bindSignalList = XVector_Create(XConnection*);
	XContainerSetCompare(manager->bindSignalList, uintptr_t_compare);
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
			if (conn && conn->receiver && conn->receiver->m_signalSlot)
			{//在接收者中删除指向
				XSignalSlot* recvSS = conn->receiver->m_signalSlot;
				XVector_remove_base(recvSS->bindSignalList, XVector_indexOf(recvSS->bindSignalList, &conn,0),1);
			}
		}
		XVector_delete_base(signal->connList);  // 释放连接堆内存
		signal->connList = NULL;
	}
	XMapBase_delete_base(manager->signalMap);
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
	XVector_delete_base(manager->bindSignalList);
	manager->bindSignalList = NULL;

	manager->obj = NULL;
	// 解锁(互斥锁为全局共享,不在此销毁)
	XMutex_unlock(manager->mutex);
	manager->mutex = NULL;
}
void XSignalSlot_delete(XSignalSlot* manager)
{
	XSignalSlot_deinit(manager);
	XDelete(manager);
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
	if (receiver == NULL && ((type & XConnectionType_Queued) || (type & XConnectionType_BlockingQueued)))
		return NULL;//没有接收对象不允许用队列的方式
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj= XMapBase_value_base(manager->signalMap,&signal);
	if (signalObj == NULL)
	{
		XSignal insert = {.sender=manager->obj,.type= signal,.connList= XVector_Create(XConnection*)};
		XContainerSetCompare(insert.connList, uintptr_t_compare);
		XContainerSetDataDeinitMethod(insert.connList, XConnection_dataDeinit);
		XMapBase_insert_base(manager->signalMap, &signal, &insert);
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
		if (Equality_Connection(*(XConnection**)XVector_iterator_data(&it), &connTemp) && (type & XConnectionType_Unique))
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
	XVector_push_back_1_base(signalObj->connList,&conn);
	bool needNotify = false;
	XObject* notifyReceiver = NULL;
	size_t notifySignal = 0;
	if (receiver&&receiver!= manager->obj)
	{//存在接收对象 添加绑定的信号
		if(receiver->m_signalSlot==NULL)
			receiver->m_signalSlot = XSignalSlot_create(receiver);
		if (receiver->m_signalSlot)
			XVector_push_back_1_base(receiver->m_signalSlot->bindSignalList, &conn);
		//记录通知信息,解锁后再触发回调,避免子类重载重入信号槽系统导致非递归锁死锁
		needNotify = true;
		notifyReceiver = receiver;
		notifySignal = signal;
	}
	XMutex_unlock(manager->mutex);  // 解锁
	if (needNotify)
		XObject_connectNotify_base(notifyReceiver, notifySignal);
	return conn;
}
XConnection* XSignalSlot_connect2(XSignalSlot* manager, size_t signal, XSlotFunc2 slot_func)
{
	return XSignalSlot_connect1(manager, signal,NULL,slot_func,XConnectionType_Auto);
}
//断开并释放连接;通过出参返回通知信息,由调用方在解锁后触发回调,
//避免子类重载disconnectNotify重入信号槽系统时与非递归锁死锁
static bool disconnect_conn(XConnection* conn, XObject** outNotifyReceiver, size_t* outNotifySignal)
{
	if (outNotifyReceiver)
		*outNotifyReceiver = NULL;  // 默认无需通知
	if (conn == NULL)
		return false;
	XSignal* signalObj = conn->signal;
	if (signalObj == NULL)
		return false;
	if (conn->receiver)
	{//存在接收对象
		if (conn->receiver->m_signalSlot)
		{
			//必须在释放连接前捕获通知信息(下方移除会释放conn堆内存)
			if (outNotifyReceiver && outNotifySignal)
			{
				*outNotifyReceiver = conn->receiver;
				*outNotifySignal = signalObj->type;
			}
			XVector_remove_base(conn->receiver->m_signalSlot->bindSignalList, XVector_indexOf(conn->receiver->m_signalSlot->bindSignalList ,&conn,0),1);
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
		if (cur && Equality_Connection(cur, &connTemp))
		{//找到了
			XObject* notifyReceiver = NULL;
			size_t notifySignal = 0;
			bool is_ok= disconnect_conn(cur, &notifyReceiver, &notifySignal);
			XMutex_unlock(manager->mutex);  // 解锁
			if (is_ok && notifyReceiver)
				XObject_disconnectNotify_base(notifyReceiver, notifySignal);
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
	XObject* notifyReceiver = NULL;
	size_t notifySignal = 0;
	bool is_ok = disconnect_conn(conn, &notifyReceiver, &notifySignal);
	XMutex_unlock(mutex);
	if (is_ok && notifyReceiver)
		XObject_disconnectNotify_base(notifyReceiver, notifySignal);
	return is_ok;
}
//信号发射时，槽函数会立即被调用
static void Direct_emit(XConnection* conn, void* args,  XAtomic_int32_t* ref_count)
{
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子加1
	if(conn->slot_func1)
	{
		if(conn->receiver)
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
static void Queued_emit(XConnection* conn, void* args, XAtomic_int32_t* ref_count, int priority)
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
	XCoreApplication_postEvent(conn->receiver, event, priority);
}

//（槽函数在接收者线程执行），区别在于发送信号的线程会阻塞，直到槽函数执行完成后才继续。
static void BlockingQueued_emit(XConnection* conn, void* args, XAtomic_int32_t* ref_count, int priority)
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
	XCoreApplication_postEvent(conn->receiver, event, priority);
	XSemaphore_acquire(sem,1);//阻塞等待接收者线程处理完成
	XSemaphore_delete(sem);//释放信号量
}
//若接收者与发送信号的线程处于同一线程，则使用 Qt::DirectConnection（直接连接）；否则，使用 Qt::QueuedConnection（队列连接）。连接类型会在信号发射时动态确定。
static void Auto_emit(XConnection* conn, void* args, XAtomic_int32_t* ref_count, int priority)
{
	if (conn->receiver == NULL)
	{
		Direct_emit(conn, args, ref_count);
	}
	else
	{
		if (XThread_currentThread() == XObject_thread(conn->receiver))
			Direct_emit(conn, args, ref_count);
		else
			Queued_emit(conn, args,ref_count, priority);
	}
}

//释放本emit对参数持有的引用;若为最后引用则释放参数与引用计数
static void emit_release_ref(XVarList* args, XAtomic_int32_t* ref_count)
{
	if (ref_count && XAtomic_fetch_sub_int32(ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 1)
	{
		if (args)
			XVarList_delete(args);
		XAtomic_delete(ref_count);
	}
}
static void emit(XSignalSlot* manager, size_t signal, XVarList* args, XAtomic_int32_t* ref_count, int priority)
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
		emit_release_ref(args, ref_count);  // 没有连接的信号:释放本emit引用
		return;
	}
	//拷贝连接列表快照,避免在调用槽函数时持锁导致重入死锁(锁为非递归)
	//快照中signal指向栈上稳定副本,避免map扩容导致signalObj指针失效
	XSignal senderSignal = { .sender = manager->obj,.type = signal,.connList = NULL };
	XVector* snapshot = XVector_Create(XConnection);
	if (snapshot == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁
		emit_release_ref(args, ref_count);
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
		if (conn->receiver && conn->receiver->m_signalSlot)
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
		case XConnectionType_Auto:Auto_emit(conn, args,ref_count,priority); break;
		case XConnectionType_Direct:Direct_emit(conn, args,ref_count); break;
		case XConnectionType_Queued:Queued_emit(conn, args,ref_count,priority); break;
		case XConnectionType_BlockingQueued:BlockingQueued_emit(conn, args,ref_count, priority); break;
		}
	}
	XVector_delete_base(snapshot);
	emit_release_ref(args, ref_count);  // 释放本emit引用
}
void XSignalSlot_emit(XSignalSlot* manager, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, int priority)
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
		ref_count = XAtomic_create(int32_t);

	emit(manager, signal, args, ref_count, priority);
}
