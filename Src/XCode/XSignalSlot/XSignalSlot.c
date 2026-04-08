#include"XSignalSlot.h"
#include"XMemory.h"
#include"XMap.h"
#include"XVector.h"
#include"XObject.h"
#include"XCoreApplication.h"
#include"XMutex.h"
#include"XThread.h"
#include"XSemaphore.h"
static const bool XEquality_XConnection(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	return (pvPrevValue->receiver == pvNextValue->receiver) && (pvPrevValue->signal == pvNextValue->signal) && (pvPrevValue->slot_func == pvNextValue->slot_func) && (pvPrevValue->type == pvNextValue->type);
}
static const int32_t XConnection_compare(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	if((pvPrevValue->receiver == pvNextValue->receiver) && (pvPrevValue->signal == pvNextValue->signal) && (pvPrevValue->slot_func == pvNextValue->slot_func) && (pvPrevValue->type == pvNextValue->type))
		return XCompare_Equality;
	return XCompare_Other;
}
XSignalSlot* XSignalSlot_create(XObject* obj)
{
	XSignalSlot* manager = XNew(XSignalSlot);
	XSignalSlot_init(manager,obj);
	return manager;
}

void XSignalSlot_init(XSignalSlot* manager, XObject* obj)
{
	if (manager == NULL)
		return NULL;
	manager->obj = obj;
	manager->signalMap = XMap_Create(size_t, XSignal,XCompare_size_t);
	manager->bindSignalList = XVector_Create(XConnection*);
	XContainerSetCompare(manager->bindSignalList, XCompare_ptr);
	// 初始化互斥锁
	manager->mutex = XMutex_create();
}
void XSignalSlot_deinit(XSignalSlot* manager)
{
	if (manager == NULL)
		return;
	// 加锁保护销毁过程
	XMutex_lock(manager->mutex);
	//清除绑定的槽
	for_each_iterator(manager->signalMap, XMap, it)
	{
		XSignal* signal = XPair_second(XMap_iterator_data(&it));
		if (signal == NULL)
			continue;
		for_each_iterator(signal->connList, XVector,it)
		{
			XConnection* conn = XVector_iterator_data(&it);
			if (conn->receiver)
			{//在接收者中删除指向
				XVector_remove_base(conn->receiver->m_signalSlot->bindSignalList, XVector_indexOf(conn->receiver->m_signalSlot->bindSignalList, &conn,0),1);
			}
		}
		XVector_delete_base(signal->connList);
	}
	XMapBase_delete_base(manager->signalMap);
	manager->signalMap = NULL;
	//清除链接的信号
	for_each_iterator(manager->bindSignalList, XVector, it)
	{
		XConnection* conn = (*(XConnection**)XVector_iterator_data(&it));
		XSignal* signalObj = conn->signal;
		if (signalObj == NULL)
			continue;
		XVector_remove_base(signalObj->connList, XVector_indexOf(signalObj->connList, conn,0),1);
	}
	XVector_delete_base(manager->bindSignalList);
	manager->bindSignalList = NULL;

	manager->obj = NULL;
	// 解锁并销毁互斥锁
	XMutex_unlock(manager->mutex);
	XMutex_delete(manager->mutex);
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
static const bool Equality_Connection(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	return ((pvNextValue->receiver) ? (pvPrevValue->receiver == pvNextValue->receiver) : true) && (pvPrevValue->signal == pvNextValue->signal) && ((pvNextValue->slot_func) ? (pvPrevValue->slot_func == pvNextValue->slot_func):true);
}
XConnection* XSignalSlot_connect(XSignalSlot* manager,size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if(manager==NULL||slot_func==NULL)
		return NULL;
	if (receiver == NULL && ((type & XConnectionType_Queued) || (type & XConnectionType_BlockingQueued)))
		return NULL;//没有接收对象不允许用队列的方式
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj= XMapBase_value_base(manager->signalMap,&signal);
	if (signalObj == NULL)
	{
		XSignal insert = {.sender=manager->obj,.type= signal,.connList= XVector_Create(XConnection)};
		//insert.connList->m_equality = XEquality_XConnection;
		XContainerSetCompare(insert.connList, XConnection_compare);
		XMap_insert_base(manager->signalMap, &signal, &insert);
		signalObj = XMapBase_value_base(manager->signalMap, &signal);
	}
	//判断是否重复添加
	XConnection conn = {.type=type,.signal= signalObj,.receiver=receiver,.slot_func=slot_func};
	for_each_iterator(signalObj->connList, XVector,it)
	{
		if (Equality_Connection(XVector_iterator_data(&it), &conn) && (type & XConnectionType_Unique))
		{
			XMutex_unlock(manager->mutex);  // 解锁
			return NULL;//重复了
		}
	}
	//添加
	XVector_push_back_base(signalObj->connList,&conn);
	XConnection* ptr=XVector_back_base(signalObj->connList);
	if (receiver&&receiver!= manager->obj)
	{//存在接收对象 添加绑定的信号
		if(receiver->m_signalSlot==NULL)
			receiver->m_signalSlot = XSignalSlot_create(receiver);
		XVector_push_back_base(receiver->m_signalSlot->bindSignalList, &ptr);
		//触发回调
		XObject_connectNotify_base(receiver,signal);
	}
	XMutex_unlock(manager->mutex);  // 解锁
	return ptr;
}
static bool disconnect_conn(XConnection* conn)
{
	if (conn == NULL)
		return false;
	XSignal* signalObj = conn->signal;
	if (signalObj == NULL)
		return false;
	if (conn->receiver)
	{//存在接收对象
		XVector_remove_base(conn->receiver->m_signalSlot->bindSignalList, XVector_indexOf(conn->receiver->m_signalSlot->bindSignalList ,&conn,0),1);
		//触发信号断开回调
		XObject_disconnectNotify_base(conn->receiver, conn->signal->type);
	}

	XVector_remove_base(signalObj->connList, XVector_indexOf(signalObj->connList,conn,0),0);
	return true;
}
bool XSignalSlot_disconnect(XSignalSlot* manager, size_t signal, XObject* receiver, XSlotFunc slot_func)
{
	if(manager==NULL||slot_func==NULL)
		return false;
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj = XMapBase_value_base(manager->signalMap, &signal);
	if (signalObj == NULL)
		return false;
	XMutex* mutex = NULL;
	//判断是否重复添加
	XConnection conn = { .signal = signalObj,.receiver = receiver,.slot_func = slot_func };
	for_each_iterator(signalObj->connList, XVector, it)
	{
		if (Equality_Connection(XVector_iterator_data(&it), &conn))
		{//找到了
			XConnection* conn = XVector_iterator_data(&it);
			if (conn->receiver && conn->receiver->m_signalSlot)
				mutex = conn->receiver->m_signalSlot->mutex;
			if(mutex!= manager->mutex)
				XMutex_lock(mutex);  // 加锁
			bool is_ok= disconnect_conn(conn);
			if (mutex != manager->mutex)
				XMutex_unlock(mutex);  // 解锁
			XMutex_unlock(manager->mutex);  // 解锁
			return is_ok;
		}
	}
	XMutex_unlock(manager->mutex);  // 解锁
	return false;
}

bool XSignalSlot_disconnect_conn(XConnection* conn)
{
	if (conn == NULL)
		return false;
	XMutex* mutexSend=NULL,*mutexReceiver = NULL;
	if (conn->signal && conn->signal->sender&& conn->signal->sender->m_signalSlot)
		mutexSend = conn->signal->sender->m_signalSlot->mutex;
	if (conn->receiver && conn->receiver->m_signalSlot)
		mutexReceiver = conn->signal->sender->m_signalSlot->mutex;
	if (mutexSend == mutexReceiver)
		mutexReceiver = NULL;
	XMutex_lock(mutexSend);  // 加锁
	XMutex_lock(mutexReceiver);  // 加锁
	bool is_ok = disconnect_conn(conn);
	XMutex_unlock(mutexSend);  // 解锁
	XMutex_unlock(mutexReceiver);  // 解锁
	return is_ok;
}
//信号发射时，槽函数会立即被调用
static void Direct_emit(XConnection* conn, void* args,  XAtomic_int32_t* ref_count)
{
	if (ref_count) 
		XAtomic_fetch_add_int32(ref_count, 1);  // 原子加1
	if(conn->slot_func)
		conn->slot_func(conn->receiver,args);
	if (ref_count)
		XAtomic_fetch_sub_int32(ref_count, 1);
}
//槽函数会在接收者线程的事件循环回归控制时被调用。槽函数在接收者所属线程中执行。
static void Queued_emit(XConnection* conn, void* args, XAtomic_int32_t* ref_count, int priority)
{
	if (conn->receiver == NULL)
		return;
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1);  // 原子加1
	//向接收者对象投递函数事件
	//XObject_postEvent(conn->receiver, XEventMetaCall_create(conn->signal->sender, conn->receiver,conn->slot_func,args, del,ref_count,NULL), priority);
	XCoreApplication_postEvent(conn->receiver, XEventMetaCall_create(conn->signal->sender, conn->slot_func, args, ref_count, NULL), priority);
}

//（槽函数在接收者线程执行），区别在于发送信号的线程会阻塞，直到槽函数执行完成后才继续。
static void BlockingQueued_emit(XConnection* conn, void* args, XAtomic_int32_t* ref_count, int priority)
{
	if (conn->receiver == NULL)
		return;
	//当前发送线程和接收线程是同一个不允许，会有死锁问题
	if (XThread_currentThread()==conn->receiver->m_thread)
	{
		// 相同线程：直接报错或返回，避免死锁
		XPrintf("BlockingQueued: 发送线程与接收者线程相同，可能导致死锁\n");
		return;  // 或触发断言：XASSERT(false, "线程相同错误");
	}
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1);  // 原子加1
	//使用信号量进行同步
	XSemaphore* sem = XSemaphore_create(1,1);
	if (!sem)
		return;
	//向接收者对象投递信号事件
	//XObject_postEvent(conn->receiver, XEventMetaCall_create(conn->signal->sender, conn->receiver, conn->slot_func, args, del, ref_count, sem), priority);
	XCoreApplication_postEvent(conn->receiver, XEventMetaCall_create(conn->signal->sender, conn->slot_func, args, ref_count, NULL), priority);
	XSemaphore_acquire(sem,1);//阻塞等待其他线程处理
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
		if (XObject_thread(conn->signal->sender) == XObject_thread(conn->receiver))
			Direct_emit(conn, args, ref_count);
		else
			Queued_emit(conn, args,ref_count, priority);
	}
}

static void emit(XSignalSlot* manager, size_t signal, void* args, XAtomic_int32_t* ref_count, int priority)
{
	if (manager == NULL)
		return;
	XMutex_lock(manager->mutex);  // 加锁
	XSignal* signalObj = XMapBase_value_base(manager->signalMap, &signal);
	if (signalObj == NULL)
	{
		XMutex_unlock(manager->mutex);  // 解锁
		return;//没有连接的信号
	}
	XConnection* conn = NULL;
	if (ref_count)
		XAtomic_fetch_add_int32(ref_count, 1);  // 使用原子存储++
	for (XVector_iterator it = XVector_begin(signalObj->connList), endIt = XVector_end(signalObj->connList); !XVector_iterator_equality(&it, &endIt); )
	{
		conn = XVector_iterator_data(&it);
		switch (conn->type)
		{
		case XConnectionType_Auto:Auto_emit(conn, args,ref_count,priority); break;
		case XConnectionType_Direct:Direct_emit(conn, args,ref_count); break;
		case XConnectionType_Queued:Queued_emit(conn, args,ref_count,priority); break;
		case XConnectionType_BlockingQueued:BlockingQueued_emit(conn, args,ref_count, priority); break;
		}
		//是否是单次链接
		if ((conn->type) & XConnectionType_SingleShot)
		{//取消链接
			XVector_erase_base(signalObj->connList, &it, &it);
		}
		else
		{
			XVector_iterator_add(signalObj->connList, &it);
		}
	}
	if (ref_count && XAtomic_fetch_sub_int32(ref_count,1) == 1)
	{//该释放了
		if (args)
			XVarList_delete(args);
		XAtomic_delete(ref_count);
	}
	XMutex_unlock(manager->mutex);  // 解锁
}
void XSignalSlot_emit(XSignalSlot* manager, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, int priority)
{
	if (manager == NULL)
	{
		if (args && del)
			del(args);
		if (ref_count)
			XAtomic_delete(ref_count);
		return;
	}
	if (del && !ref_count)
		ref_count = XAtomic_create(int32_t);
	if (del)
		args->del = del;
	emit(manager, signal, args, ref_count, priority);
}

void XSignalSlot_emit_queue(XSignalSlot* manager, size_t signal, void* args, void(*del)(void*), XAtomic_int32_t* ref_count, XEventPriority priority)
{
	if (manager == NULL)
	{
		if (args && del)
			del(args);
		if(ref_count)
			XAtomic_delete(ref_count);
		return;
	}
	if (del&&!ref_count)
		ref_count = XAtomic_create(int32_t);
	//XCoreApplication_postSendSignal(emit, manager, signal, argList, del,ref_count, priority);
}

