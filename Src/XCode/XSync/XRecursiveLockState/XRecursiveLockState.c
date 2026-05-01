#include "XRecursiveLockState.h"
#include "XReadWriteLock.h" // 用于保护全局哈希表
#include "XHashMap.h"
#include "XThread.h" // 用于 XThread_currentThreadId()
#include "XMemory.h" // 用于内存分配

// ========== 全局变量 ==========
static XReadWriteLock* global_lock = NULL;
static XHashMap* global_locks_map = NULL; // 键: void* (锁对象指针), 值: XHashMap (线程ID -> XRecursiveLockState)

// ========== 初始化函数 ==========
void XRecursiveLockState_init() 
{
	if (global_locks_map)return;
	global_lock = XReadWriteLock_create(XLock_Spin);
	global_locks_map = XHashMap_Create(XReadWriteLock*, XHashMap, ptr_compare);
	XContainerSetDataDeinitMethod(global_locks_map, XHashMap_deinit_base);
}

// ========== 获取状态函数 ==========
XRecursiveLockState* XRecursiveLockState_get(void* lock_obj) 
{
start:
	XReadWriteLock_lockForRead(global_lock);
	XHashMap* stateMap = (XHashMap*)XHashMap_value_base(global_locks_map, &lock_obj);
	XHandle id = XThread_currentThreadId();
	XRecursiveLockState* state = NULL;
	if (stateMap)
	{
	findSate:
		state = (XRecursiveLockState*)XHashMap_value_base(stateMap, &id);
		XReadWriteLock_unlock(global_lock);
		if (!state)
		{
			XRecursiveLockState s = { 0 };
			XReadWriteLock_lockForWrite(global_lock);
			XHashMap_insert_base(stateMap, &id, &s);
			goto findSate;
		}
		return state;
	}
	else
	{
		XReadWriteLock_unlock(global_lock);
		XHashMap map;
		XHashMap_init(&map, sizeof(XHandle), sizeof(XRecursiveLockState), XHashMap_murmur3_32, ptr_compare);
		XReadWriteLock_lockForWrite(global_lock);
		XHashMap_insert_base(global_locks_map, &lock_obj, &map);
		XReadWriteLock_unlock(global_lock);
		goto start;
	}
}

// ========== 清除状态函数 ==========
void XRecursiveLockState_clear(void* lock_obj) {
	XReadWriteLock_lockForWrite(global_lock);
	if (global_locks_map)return;
	XHashMap_remove_base(global_locks_map, &lock_obj);
	XReadWriteLock_unlock(global_lock);
}