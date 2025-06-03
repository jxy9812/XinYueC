#include"XCommunicatorBase.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
static void XCommunicatorBase_free(XCommunicatorBase* comm);
XVtable* XCommunicatorBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCOMMUNICATORBASE_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
		
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free, XCommunicatorBase_free);
#if SHOWCONTAINERSIZE
	printf("XIODeviceBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void XCommunicatorBase_free(XCommunicatorBase* comm)
{
	XIODeviceBase_free_base(comm->m_io);
	XMemory_free(comm);
}
