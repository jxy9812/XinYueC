#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XThread.h"
#include"XTimer.h"
static void threadFunc(XThread* thread, XVarList* list)
{
	XPrintf("子线程:id:%d\n",XThread_currentThreadId());
	XTimer* timer = XTimer_create();
	XTimer_setInterval(timer, 3);
	//XTimer_setTimeout(timer, 5000);
	XTimer_setSingleShot(timer, true);
	XTimer_setAutoDelete(timer, true);
	XTimer_setTimerType(timer, XTimerType_PreciseTimer);
	XObject_connect1(timer, XSignal(XTimer_timeout_signal), thread, XThread_quit, XConnectionType_Auto);
	XTimer_start_base(timer);
	
	XThread_exec(thread);
	XTimer_delete_base(timer);
	XCoreApplication_processEvents(XEventLoop_AllEvents);
	XCoreApplication_quit();
	XThread_deleteLater(thread);
}
void XThreadTest()
{
	while (true)
	{
		XPrintf("主线程:id:%d\n", XThread_currentThreadId());
		XThread* th = XThread_create_func(threadFunc, NULL);
		//XPrintf("XThread:%p XVector* children:%p\n", th,((XObject*)th)->children);
		XThread_start(th);
		//XPrintf("XThread:%p XVector* children:%p\n", th, ((XObject*)th)->children);
		//while (true);
		XCoreApplication_exec();
		XThread_deleteLater(th);

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