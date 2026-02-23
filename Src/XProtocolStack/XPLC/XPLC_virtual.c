#include"XPLC.h"
#include"XHashMap.h"
#include"XQueueBase.h"
#include"XIODevice.h"
#include"XPLCTask.h"
static bool VXPLC_addOutIODevice(XPLC* plc, int32_t id, XIODevice* io);
static bool VXPLC_addInIODevice(XPLC* plc, int32_t id, XIODevice* io);
static bool VXPLC_removeOutId(XPLC* plc, int32_t id);
static bool VXPLC_removeInId(XPLC* plc, int32_t id);
static bool VXPLC_removeIODevice(XPLC* plc, XIODevice* io);
static void VXPLC_poll(XPLC* plc);
static void VXIODevice_deinit(XPLC* task);
XVtable* XPLC_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XPLC))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
		VXPLC_addOutIODevice,VXPLC_addInIODevice,
		VXPLC_removeOutId,VXPLC_removeInId,
		VXPLC_removeIODevice
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIODevice_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXPLC_poll);
#if SHOWCONTAINERSIZE
	printf("XPLCTask size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

bool VXPLC_addOutIODevice(XPLC* plc, int32_t id, XIODevice* io)
{
	if(io==NULL)
		return false;
	XIODevice* findIo=XMapBase_value_base(plc->m_outIO, &id);
	if (findIo != NULL)
		XIODevice_delete_base(findIo);
	XMapBase_insert_base(plc->m_outIO,&id,&io);
	return true;
}

bool VXPLC_addInIODevice(XPLC* plc, int32_t id, XIODevice* io)
{
	if (io == NULL)
		return false;
	XIODevice* findIo = XMapBase_value_base(plc->m_inIO, &id);
	if (findIo != NULL)
		XIODevice_delete_base(findIo);
	XMapBase_insert_base(plc->m_inIO, &id, &io);
	return true;
}

bool VXPLC_removeOutId(XPLC* plc, int32_t id)
{
	XIODevice* findIo = XMapBase_value_base(plc->m_outIO, &id);
	if (findIo != NULL)
		XIODevice_delete_base(findIo);
	XMapBase_remove_base(plc->m_outIO, &id);
	return true;
}

bool VXPLC_removeInId(XPLC* plc, int32_t id)
{
	XIODevice* findIo = XMapBase_value_base(plc->m_inIO, &id);
	if (findIo != NULL)
		XIODevice_delete_base(findIo);
	XMapBase_remove_base(plc->m_inIO, &id);
	return true;
}

bool VXPLC_removeIODevice(XPLC* plc, XIODevice* io)
{
	int32_t id;
	bool isFind = false;
	for_each_iterator(plc->m_inIO, XHashMap, it)
	{
		XPair* node = XHashMap_iterator_data(&it);
		if (XPair_Second(node, XIODevice*) == io)
		{
			id = XPair_First(node, int32_t);
			isFind = true;
			break;
		}
	}
	if (isFind)
	{
		XIODevice_delete_base(io);
		XMapBase_remove_base(plc->m_inIO, &id);
		return true;
	}
	for_each_iterator(plc->m_outIO, XHashMap, it)
	{
		XPair* node = XHashMap_iterator_data(&it);
		if (XPair_Second(node, XIODevice*) == io)
		{
			id = XPair_First(node, int32_t);
			isFind = true;
			break;
		}
	}
	if (isFind)
	{
		XIODevice_delete_base(io);
		XMapBase_remove_base(plc->m_outIO, &id);
		return true;
	}
}

void VXPLC_poll(XPLC* plc)
{
	XPLCTask* task;
	if (plc->m_taskQueue)
	{
		//printf("查询任务队列中\n");
		if (XQueueBase_receive_base(plc->m_taskQueue, &task))
		{
			while (task->m_lastTaskState!= XPLCT_State_ExitTask)
			{
				for_each_iterator(plc->m_inIO, XHashMap, it)
				{
					XPair* node = XHashMap_iterator_data(&it);
					XIODevice_poll_base(XPair_Second(node, XIODevice*));
				}
				XPLCTask_poll_base(task);
				if (plc->m_delay_ms_cb != NULL && plc->m_delay_ms != 0)
					plc->m_delay_ms_cb(plc->m_delay_ms);
			}
		}
	}
}

void VXIODevice_deinit(XPLC* plc)
{
	for_each_iterator(plc->m_inIO, XHashMap, it)
	{
		XPair* node = XHashMap_iterator_data(&it);
		XIODevice_delete_base(XPair_Second(node, XIODevice*));
	}
	XContainerObject_delete_base(plc->m_inIO);
	plc->m_inIO = NULL;
	for_each_iterator(plc->m_outIO, XHashMap, it)
	{
		XPair* node = XHashMap_iterator_data(&it);
		XIODevice_delete_base(XPair_Second(node, XIODevice*));
	}
	XContainerObject_delete_base(plc->m_outIO);
	plc->m_outIO = NULL;
}
