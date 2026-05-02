#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XThread.h"
#include"XTimer.h"
#include"XAtomic.h"
static void threadFunc(XThread* thread, XVarList* list)
{
	XVarList_args_1(list, XAtomic_int32_t*, rt);
	XPrintf("子线程:%p 引用:%d\n", XThread_currentThread(), XAtomic_load_int32(rt, XAtomic_MemoryOrder_Relaxed));
	XTimer* timer = XTimer_create();
	//XPrintf("XTimer:thread:%p\n",((XObject*)timer)->m_thread);
	XTimer_setInterval(timer, 20);

	XTimer_setSingleShot(timer, true);
	XTimer_setAutoDelete(timer, true);
	XTimer_setTimerType(timer, XTimerType_PreciseTimer);
	XObject_connect1(timer, XSignal(XTimer_timeout_signal), thread, XThread_quit, XConnectionType_Auto);
	XTimer_start_base(timer);

	XThread_exec(thread);
	//XTimer_deleteLater(timer);
	//XClass_delete_base(timer);
	int value = XAtomic_fetch_sub_int32(rt, 1, XAtomic_MemoryOrder_Relaxed);
	if (value <= 1)
	{
		XCoreApplication* app = xApp;
		XEventLoop * l = NULL;
		while (true)
		{
			XThread* t = ((XObject*)app)->m_thread;
			if (!t)continue;
			l = t->m_loop;
			if (!l)continue;
			break;
		}

		while (l->m_state != XEventLoop_Running);
		XCoreApplication_quit();
	}
	
	//XThread_deleteLater(thread);
}
void XThreadTest()
{
	XAtomic_int32_t* rt= XAtomic_create(int32_t);
	while (true)
	{
		//XPrintf("主线程:%p\n", XThread_currentThread());
		for (size_t i = 0; i < 16; i++)
		{
			XAtomic_fetch_add_int32(rt, 1, XAtomic_MemoryOrder_Relaxed);
			XThread* th = XThread_create_func(threadFunc, XVarList_Create(XVar(XAtomic_int32_t*, rt)));
			XObject_connect1(th, XSignal(XThread_finished_signal), th, XThread_deleteLater, XConnectionType_Auto);
			//XPrintf("XThread:%p XVector* children:%p\n", th, ((XObject*)th)->children);
			if (!XThread_start(th))
			{
				XThread_deleteLater(th);
				int value = XAtomic_fetch_sub_int32(rt, 1, XAtomic_MemoryOrder_Relaxed);
				/*if (value <= 1 && i == 9)
					continue;*/
				
			}
		}
		XCoreApplication_exec();
		//XThread_deleteLater(th);

		XCoreApplication_processEvents(XEventLoop_AllEvents);
	}
}

void XMenu_XThreadTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XThread(线程)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XThreadTest);
	}
}