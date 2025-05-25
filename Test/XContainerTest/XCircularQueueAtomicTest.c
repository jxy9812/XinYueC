#include"XDataStructTest.h"
#if DEMOTEST
#include"XCircularQueueAtomic.h"
#ifdef WIN32
#include"windows.h"
// 线程函数 1：输出 "Thread 1 is running"
static DWORD WINAPI ThreadReceive(LPVOID lpParam)
{
	XCircularQueueAtomic* queue = lpParam;
	//int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };

	for (size_t i = 0; i < 100000; i++)
	{
		int n = i;
		while(!XCircularQueueAtomic_push(queue, &n));
		//Sleep(100);
	}

	return 0;
}

static void threadTest(XCircularQueueAtomic* queue)
{
	HANDLE hThread1;
	DWORD threadId1;
	// 创建线程 1
	hThread1 = CreateThread(NULL, 0, ThreadReceive, queue, 0, &threadId1);
	if (hThread1 == NULL) {
		printf("CreateThread1 failed with error %d\n", GetLastError());
		return 1;
	}


	// 等待两个线程结束
	//WaitForSingleObject(hThread1, INFINITE);
	// 关闭线程句柄
	//CloseHandle(hThread1);
}
#endif // WIN32


void XCircularQueueAtomicTest()
{
#if XCircularQueueAtomic_ON
	printf("循环队列 测试\n");
	XCircularQueueAtomic* queue = XCircularQueueAtomic_New(int,1000);
	
	threadTest(queue);
	int index = 0;
	int value;
	while (true)
	{
		if (XCircularQueueAtomic_receive(queue, &value))
		{

			printf("index:%d %d size:%d\n",index++,value ,XCircularQueueAtomic_size(queue));
			//XCircularQueueAtomic_pop(queue);
		}
	}
	
	XCircularQueueAtomic_free(queue);
	printf("循环队列 空\n");
	
#else
	IS_ON_DEBUG(XCircularQueueAtomic_ON);
#endif
}

#endif