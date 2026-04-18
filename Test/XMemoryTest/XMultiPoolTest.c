#include"XMemoryTest.h"
#include"XMultiPool.h"
#include"XThread.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//线程处理函数
static void threadfunc(XThread*thread, XVarList* list)
{
	XVarList_args_1(list,XMultiPool*, multi_pool);
	while (true)
	{
		// 3. 分配内存
		char* ptr1 = XMultiPool_malloc(multi_pool, 10);  // 从 64-byte 池分配
		char* ptr2 = XMultiPool_malloc(multi_pool, 200); // 从 256-byte 池分配
		char* ptr3 = XMultiPool_malloc(multi_pool, 5);   // 从 8-byte 池分配
		strcpy(ptr1, "ptr11");
		strcpy(ptr2, "ptr21");
		strcpy(ptr3, "ptr31");
		if (ptr1 && ptr2 && ptr3) {
			printf("%s\n %s\n %s\n", ptr1, ptr2, ptr3);
		}
		// 4. 释放内存
		XMultiPool_free(multi_pool, ptr1);
		XMultiPool_free(multi_pool, ptr2);
		XMultiPool_free(multi_pool, ptr3);
	}
}
//多级内存池测试
void XMultiPoolTest()
{
	
	// 1. 创建多级池
	XMultiPool* multi_pool = XMultiPool_create();
	if (!multi_pool) {
		printf("Failed to create multi-pool!\n");
		return -1;
	}
	//创建一个线程
	XThread* th = XThread_create_func(threadfunc,XVarList_Create(XVar(XMultiPool*, multi_pool)) );
	// 2. 创建并添加不同大小的子池（顺序无关，内部会自动排序）
	XFixedPool* pool_256 = XFixedPool_create(256, 10);
	XFixedPool* pool_8 = XFixedPool_create(8, 100);
	XFixedPool* pool_64 = XFixedPool_create(64, 50);
	//添加不同大小的池
	XMultiPool_add_pool(multi_pool, pool_256);
	XMultiPool_add_pool(multi_pool, pool_8);
	XMultiPool_add_pool(multi_pool, pool_64);
	XThread_start(th);
	while (true)
	{
		// 3. 分配内存
		char* ptr1 = XMultiPool_malloc(multi_pool, 10);  // 从 64-byte 池分配
		char* ptr2 = XMultiPool_malloc(multi_pool, 200); // 从 256-byte 池分配
		char* ptr3 = XMultiPool_malloc(multi_pool, 5);   // 从 8-byte 池分配
		strcpy(ptr1, "ptr1");
		strcpy(ptr2, "ptr2");
		strcpy(ptr3, "ptr3");
		if (ptr1 && ptr2 && ptr3) {
			printf("%s\n %s\n %s\n", ptr1, ptr2, ptr3);
		}
		// 4. 释放内存
		XMultiPool_free(multi_pool, ptr1);
		XMultiPool_free(multi_pool, ptr2);
		XMultiPool_free(multi_pool, ptr3);
	}




	// 5. 销毁多级池（会自动销毁所有子池）
	XMultiPool_delete(multi_pool);
}

void XMenu_XMultiPoolTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XMultiPool(多级内存池)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XMultiPoolTest);
    }
}