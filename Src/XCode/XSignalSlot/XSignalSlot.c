#include"XSignalSlot.h"
#include"XMemory.h"
#include"XMap.h"
#include"XListSLinked.h"
#include"XListSLinkedAtomic.h"
#include"XObject.h"
static const bool XEquality_XConnection(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	return (pvPrevValue->receiver == pvNextValue->receiver) && (pvPrevValue->signal == pvNextValue->signal) && (pvPrevValue->slot_func == pvNextValue->slot_func) && (pvPrevValue->type == pvNextValue->type);
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
	manager->signalMap = XMap_Create(size_t, XSignal,XEquality_size_t,XLess_size_t);
	manager->bindSignalList = XListSLinkedAtomic_Create(XConnection*);
	manager->bindSignalList->m_equality = XEquality_ptr;
}
void XSignalSlot_deinit(XSignalSlot* manager)
{
	if (manager == NULL)
		return;
	//清除绑定的槽
	for_each_iterator(manager->signalMap, XMap, it)
	{
		XSignal* signal = XPair_second(XMap_iterator_data(&it));
		if (signal == NULL)
			continue;
		for_each_iterator(signal->connList, XListSLinkedAtomic,it)
		{
			XConnection* conn = XListSLinkedAtomic_iterator_data(&it);
			if (conn->receiver)
			{//在接收者中删除指向
				XListBase_remove_base(conn->receiver->m_signalSlot->bindSignalList, &conn);
			}
		}
		XListBase_delete_base(signal->connList);
	}
	XMapBase_delete_base(manager->signalMap);
	manager->signalMap = NULL;
	//清除链接的信号
	for_each_iterator(manager->bindSignalList, XListSLinkedAtomic, it)
	{
		XConnection* conn = XListSLinkedAtomic_iterator_data(&it);
		XSignal* signalObj = conn->signal;
		if (signalObj == NULL)
			continue;
		XListBase_remove_base(signalObj->connList, conn);
	}
	XListBase_delete_base(manager->bindSignalList);
	manager->bindSignalList = NULL;

	manager->obj = NULL;
}
void XSignalSlot_delete(XSignalSlot* manager)
{
	XSignalSlot_deinit(manager);
	XMemory_free(manager);
}
static const bool Equality_Connection(const XConnection* pvPrevValue, const XConnection* pvNextValue)
{
	return (pvPrevValue->receiver == pvNextValue->receiver) && (pvPrevValue->signal == pvNextValue->signal) && (pvPrevValue->slot_func == pvNextValue->slot_func);
}
XConnection* XSignalSlot_connect(XSignalSlot* manager,size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if(manager==NULL||slot_func==NULL)
		return NULL;
	if (receiver == NULL && ((type & XConnectionType_Queued) || (type & XConnectionType_BlockingQueued)))
		return NULL;//没有接收对象不允许用队列的方式
	XSignal* signalObj= XMapBase_value_base(manager->signalMap,&signal);
	if (signalObj == NULL)
	{
		XSignal insert = {.sender=manager->obj,.type= signal,.connList= XListSLinkedAtomic_Create(XConnection)};
		insert.connList->m_equality = XEquality_XConnection;
		XMap_insert_base(manager->signalMap, &signal, &insert);
		signalObj = XMapBase_value_base(manager->signalMap, &signal);
	}
	//判断是否重复添加
	XConnection conn = {.type=type,.signal= signalObj,.receiver=receiver,.slot_func=slot_func};
	for_each_iterator(signalObj->connList, XListSLinkedAtomic,it)
	{
		if (Equality_Connection(XListSLinkedAtomic_iterator_data(&it), &conn) && (type & XConnectionType_Unique))
			return NULL;//重复了
	}
	//添加
	XListBase_push_back_base(signalObj->connList,&conn);
	XConnection* ptr=XListBase_back_base(signalObj->connList);
	if (receiver)
	{//存在接收对象 添加绑定的信号
		XListBase_push_back_base(receiver->m_signalSlot->bindSignalList, &ptr);
	}
	return ptr;
}

void XSignalSlot_disconnect(XConnection* conn)
{
	if (conn==NULL)
		return;
	XSignal* signalObj = conn->signal;
	if (signalObj == NULL)
		return;
	XListBase_remove_base(signalObj->connList, conn);
	if (conn->receiver)
	{//存在接收对象
		XListBase_remove_base(conn->receiver->m_signalSlot->bindSignalList,&conn);
	}
}
//信号发射时，槽函数会立即被调用
static void Direct_emit(XConnection* conn, void* args)
{
	if(conn->slot_func)
		conn->slot_func(conn->receiver,args);
}
//槽函数会在接收者线程的事件循环回归控制时被调用。槽函数在接收者所属线程中执行。
static void Queued_emit(XConnection* conn, void* args)
{
	if (conn->receiver == NULL)
		return;
	//向接收者对象投递函数事件
	XObject_postEvent(conn->receiver, XEventFunc_create(conn->receiver,conn->slot_func,args));
}
////等待槽函数
//static void waitSlot()
//{
//
//}
//（槽函数在接收者线程执行），区别在于发送信号的线程会阻塞，直到槽函数执行完成后才继续。
static void BlockingQueued_emit(XConnection* conn, void* args)
{
	if (conn->receiver == NULL)
		return;
	//向接收者对象投递函数事件
	XObject_postEvent(conn->receiver, XEventFunc_create(conn->receiver, conn->slot_func, args));
}
//若接收者与发送信号的线程处于同一线程，则使用 Qt::DirectConnection（直接连接）；否则，使用 Qt::QueuedConnection（队列连接）。连接类型会在信号发射时动态确定。
static void Auto_emit(XConnection* conn, void* args)
{
	if (conn->receiver == NULL)
		return;
	XObject* send = conn->signal->sender;
	if (XObject_thread(send) == XObject_thread(conn->receiver))
		Direct_emit(conn, args);
	else
		Queued_emit(conn,args);
}
void XSignalSlot_emit(XSignalSlot* manager, size_t signal, void* args)
{
	if (manager == NULL)
		return;
	XSignal* signalObj = XMapBase_value_base(manager->signalMap, &signal);
	if (signalObj == NULL)
		return;//没有这个信号
	XConnection* conn = NULL;
	for (XListSLinkedAtomic_iterator it = XListSLinkedAtomic_begin(signalObj->connList), endIt = XListSLinkedAtomic_end(signalObj->connList); !XListSLinkedAtomic_iterator_equality(&it, &endIt); )
	{
		conn=XListSLinkedAtomic_iterator_data(&it);
		switch (conn->type)
		{
		case XConnectionType_Auto:Auto_emit(conn, args); break;
		case XConnectionType_Direct:Direct_emit(conn, args); break;
		case XConnectionType_Queued:Queued_emit(conn, args); break;
		case XConnectionType_BlockingQueued:BlockingQueued_emit(conn, args); break;
		}
		//是否是单次链接
		if ((conn->type)& XConnectionType_SingleShot)
		{//取消链接
			XListBase_erase_base(signalObj->connList, &it,&it);
		}
		else
		{
			XListSLinkedAtomic_iterator_add(signalObj->connList, &it);
		}
	}
}
