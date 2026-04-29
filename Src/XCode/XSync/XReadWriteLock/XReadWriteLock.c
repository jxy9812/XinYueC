#include "XReadLocker.h"
//#include "XThreadData.h"
#include "XThread.h"
#include "XHashMap.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XTimer.h"
#include <stdlib.h>

typedef struct XReadWriteLockPrivate
{
	XMutex* mutex;               // 辅助互斥锁，用于条件变量
	XWaitCondition* readCond;    // 等待读锁的条件变量
	XWaitCondition* writeCond;   // 等待写锁的条件变量
	XAtomic_size_t read_waiters; // 等待读锁的线程数
	XAtomic_size_t write_waiters; //等待写锁的线程数
}XReadWriteLockPrivate;
static void XReadWriteLockPrivate_init(XReadWriteLockPrivate* p);
static XReadWriteLockPrivate* XReadWriteLockPrivate_create()
{
	XReadWriteLockPrivate* p = (XReadWriteLockPrivate*)XMalloc(sizeof(XReadWriteLockPrivate));
	XReadWriteLockPrivate_init(p);
	return p;
}

static void XReadWriteLockPrivate_init(XReadWriteLockPrivate* p)
{
	if (!p)return;
	p->mutex = XMutex_create();
	p->readCond = XWaitCondition_create();
	p->writeCond = XWaitCondition_create();
	XAtomic_init(p->read_waiters,0);
	XAtomic_init(p->write_waiters, 0);
}
static void XReadWriteLockPrivate_deinit(XReadWriteLockPrivate* p)
{
	if (!p)return;
	if (p->mutex)
	{
		XMutex_delete(p->mutex);
		p->mutex = NULL;
	}
	if (p->readCond)
	{
		XWaitCondition_delete(p->readCond);
		p->readCond = NULL;
	}
	if (p->writeCond)
	{
		XWaitCondition_delete(p->writeCond);
		p->writeCond = NULL;
	}
	/*XAtomic_init(p->read_waiters, 0);
	XAtomic_init(p->write_waiters, 0);*/
}
static void XReadWriteLockPrivate_delete(XReadWriteLockPrivate* p)
{
	if (!p)return;
	XReadWriteLockPrivate_deinit(p);
	XFree(p);
}
static XReadWriteLock* global_lock=NULL;
static XHashMap* global_locks_map =NULL;
/**
 * @brief ThreadLocalLockState - 用于支持 XReadWriteLock 的递归模式
 */
typedef struct
{
	size_t reader_count; // 当前线程持有的读锁计数
	size_t writer_count; // 当前线程持有的写锁计数 (0 or >=1)
} ThreadLocalLockState;

static void recursive_locks_map_init()//    XHashMap* m_recursive_locks_map; // 键: XReadWriteLock*, 值: <threadId,ThreadLocalLockState>
{
	if (global_locks_map)return;
	global_lock = XReadWriteLock_create(XReadWriteLock_NonRecursive);
	global_locks_map = XHashMap_Create(XReadWriteLock*, XHashMap,ptr_compare);
	XContainerSetDataDeinitMethod(global_locks_map, XHashMap_deinit_base);
}
//本地当前的锁状态
static ThreadLocalLockState* XThreadData_local_lock_state(XReadWriteLock* rwlock)
{
	start:
	XReadWriteLock_lockForRead(global_lock);
	XHashMap* stateMap = (XHashMap*)XHashMap_value_base(global_locks_map,&rwlock);
	XHandle id = XThread_currentThreadId();
	ThreadLocalLockState* state = NULL;
	if (stateMap)
	{
		findSate:
		state =(ThreadLocalLockState*)XHashMap_value_base(stateMap, &id);
		XReadWriteLock_unlock(global_lock);
		if (!state)
		{
			ThreadLocalLockState s = { 0 };
			XReadWriteLock_lockForWrite(global_lock);
			XHashMap_insert_base(stateMap, &id,&s);
			goto findSate;
			//state = (ThreadLocalLockState*)XHashMap_value_base(stateMap, &id);
			//XReadWriteLock_unlock(global_lock);
		}
		return state;
	}
	else
	{
		XReadWriteLock_unlock(global_lock);
		XHashMap map;
		XHashMap_init(&map,sizeof(XHandle),sizeof(ThreadLocalLockState), XHashMap_murmur3_32,ptr_compare);
		XReadWriteLock_lockForWrite(global_lock);
		XHashMap_insert_base(global_locks_map, &rwlock, &map);
		XReadWriteLock_unlock(global_lock);
		goto start;
	}
}
static void XThreadData_local_lock_clear(XReadWriteLock* rwlock)
{
	XReadWriteLock_lockForWrite(global_lock);
	if (global_locks_map)return;
	XHashMap_remove_base(global_locks_map,&rwlock);
	XReadWriteLock_unlock(global_lock);
}
// --- 锁状态定义 (使用 size_t) ---
#if SIZE_MAX == UINT32_MAX
 // 32-bit platform: 
	// - 最高位 (bit 31) 作为写者标志 (1 bit)
	// - 低31位 (bits 0-30) 作为读者计数 (31 bits)
#define WRITER_ACTIVE_FLAG      (((size_t)1) << 31)
#define READER_COUNT_MASK       (WRITER_ACTIVE_FLAG - 1) // 0x7FFFFFFF
#define MAX_READERS             READER_COUNT_MASK        // 2,147,483,647
#elif SIZE_MAX == UINT64_MAX
	// 64-bit platform: 
	// - 最高位 (bit 63) 作为写者标志 (1 bit)
	// - 低63位 (bits 0-62) 作为读者计数 (63 bits)
#define WRITER_ACTIVE_FLAG      (((size_t)1) << 63)
#define READER_COUNT_MASK       (WRITER_ACTIVE_FLAG - 1) // 0x7FFFFFFFFFFFFFFF
#define MAX_READERS             READER_COUNT_MASK        // 9,223,372,036,854,775,807
#else
#error "Unsupported platform word size."
#endif

// --- 非递归模式的内部函数 ---
static bool try_acquire_read_nonrecursive(XReadWriteLock* rwlock) {
	size_t current_state = XAtomic_load_size_t(&rwlock->state);

	if (current_state & WRITER_ACTIVE_FLAG) {
		return false;
	}

	size_t expected = current_state;
	size_t desired = current_state + 1;
	return XAtomic_compare_exchange_strong_size_t(&rwlock->state, &expected, desired);
}

static bool try_acquire_write_nonrecursive(XReadWriteLock* rwlock) {
	size_t current_state = XAtomic_load_size_t(&rwlock->state);

	if (current_state != 0) {
		return false;
	}

	size_t expected = 0;
	size_t desired = WRITER_ACTIVE_FLAG;
	return XAtomic_compare_exchange_strong_size_t(&rwlock->state, &expected, desired);
}

// --- 递归模式的内部函数 ---
static bool try_acquire_read_recursive(XReadWriteLock* rwlock)
{
	// 1. 获取当前线程对此锁 (rwlock) 的私有状态。
	ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
	if (!tls) return false; // 如果获取状态失败（如内存不足），则直接返回失败。

	// 2. 情况A: 当前线程已经是此锁的写者。
	//    根据读写锁的规则，写者可以降级为读者（即在持有写锁期间也可以读）。
	//    这里我们简单地增加读锁计数，并返回成功。
	if (tls->writer_count > 0) {
		tls->reader_count++;
		return true;
	}

	// 3. 情况B: 当前线程已经是此锁的读者。
	//    这是典型的递归读场景。我们只需增加读锁计数，并返回成功。
	if (tls->reader_count > 0) {
		tls->reader_count++;
		return true;
	}

	// 4. 情况C: 当前线程对此锁没有任何持有。
	//    我们需要像非递归锁一样，尝试从全局状态中真正获取一次读锁。
	if (try_acquire_read_nonrecursive(rwlock)) {
		// 如果成功获取了读锁，将此线程对此锁的读计数初始化为1。
		tls->reader_count = 1;
		return true;
	}

	// 5. 全局获取也失败了，说明有其他线程持有写锁，返回失败。
	return false;
}

static bool try_acquire_write_recursive(XReadWriteLock* rwlock) 
{
	// 1. 获取当前线程对此锁 (rwlock) 的私有状态。
	ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
	if (!tls) return false;

	// 2. 情况A: 当前线程已经是此锁的写者。
	//    这是典型的递归写场景。我们只需增加写锁计数，并返回成功。
	if (tls->writer_count > 0) {
		tls->writer_count++;
		return true;
	}

	// 3. 情况B: 当前线程是此锁的读者（但不是写者）。
	//    这里明确禁止"读锁升级到写锁"。
	//    原因：如果允许升级，当多个读者同时尝试升级时，会导致死锁。
	//    因此，函数直接返回失败。
	if (tls->reader_count > 0) {
		return false; // 不支持升级！
	}

	// 4. 情况C: 当前线程对此锁没有任何持有。
	//    我们需要像非递归锁一样，尝试从全局状态中真正获取一次写锁。
	if (try_acquire_write_nonrecursive(rwlock)) {
		// 如果成功获取了写锁，将此线程对此锁的写计数初始化为1。
		tls->writer_count = 1;
		return true;
	}

	// 5. 全局获取也失败了，说明有其他线程持有读锁或写锁，返回失败。
	return false;
}

XReadWriteLock* XReadWriteLock_create(XReadWriteLock_Type type)
{
	XReadWriteLock* rwlock = (XReadWriteLock*)XMemory_malloc(sizeof(XReadWriteLock));
	XReadWriteLock_init(rwlock, type);
	return rwlock;
}
void XReadWriteLock_init(XReadWriteLock* rwlock, XReadWriteLock_Type type) 
{
    if (!rwlock)return;
	XAtomic_init(rwlock->state, 0);
	if (type == XReadWriteLock_Spin)
		rwlock->type = XReadWriteLock_SpinNonRecursive;
	else
		rwlock->type = type;
	if (type & XReadWriteLock_Recursive)
		recursive_locks_map_init();

	if(type & XReadWriteLock_Spin)
	{
		rwlock->m_d = NULL;
	}
	else
	{
		rwlock->m_d = XReadWriteLockPrivate_create();
	}
}
void XReadWriteLock_deinit(XReadWriteLock* rwlock) 
{
	if (XReadWriteLock_type(rwlock) & XReadWriteLock_Recursive)
	{
		XThreadData_local_lock_clear(rwlock);//清理全局数据
	}
	if (rwlock->m_d)
	{
		XReadWriteLockPrivate_delete(rwlock->m_d);
		rwlock->m_d = NULL;
	}
}

void XReadWriteLock_delete(XReadWriteLock* rwlock) 
{
	if (rwlock) {
		XReadWriteLock_deinit(rwlock);
		XMemory_free(rwlock);
	}
}
XReadWriteLock_Type XReadWriteLock_type(XReadWriteLock* rwlock)
{
	return rwlock ? rwlock->type : XReadWriteLock_NonRecursive;
}
// ========== Spin 模式专用接口 ==========
static void XReadWriteLock_lockForRead_Spin(XReadWriteLock* rwlock)
{
	if (rwlock->type & XReadWriteLock_Recursive) {
		while (!try_acquire_read_recursive(rwlock)) {
			XThread_yieldCurrentThread();
		}
	}
	else {
		while (!try_acquire_read_nonrecursive(rwlock)) {
			XThread_yieldCurrentThread();
		}
	}
}

static void XReadWriteLock_lockForWrite_Spin(XReadWriteLock* rwlock)
{
	if (rwlock->type & XReadWriteLock_Recursive) {
		while (!try_acquire_write_recursive(rwlock)) {
			XThread_yieldCurrentThread();
		}
	}
	else {
		while (!try_acquire_write_nonrecursive(rwlock)) {
			XThread_yieldCurrentThread();
		}
	}
}

// ========== Non-Spin 模式专用接口 ==========
static void XReadWriteLock_lockForRead_NonSpin(XReadWriteLock* rwlock)
{
	// 第一阶段：快速路径
	if (XReadWriteLock_tryLockForRead(rwlock)) {
		return;
	}

	// 第二阶段：慢速路径
	XAtomic_fetch_add_size_t(&rwlock->m_d->read_waiters, 1);
	while (true) {
		XMutex_lock(rwlock->m_d->mutex);

		// 写者优先：如果有任何写者在等待，则新来的读者必须等待
		size_t current_write_waiters = XAtomic_load_size_t(&rwlock->m_d->write_waiters);
		if (current_write_waiters > 0) {
			XWaitCondition_wait(rwlock->m_d->readCond, rwlock->m_d->mutex, -1);
			XMutex_unlock(rwlock->m_d->mutex);
			continue;
		}

		if (XReadWriteLock_tryLockForRead(rwlock)) {
			XMutex_unlock(rwlock->m_d->mutex);
			break;
		}

		XWaitCondition_wait(rwlock->m_d->readCond, rwlock->m_d->mutex, -1);
		XMutex_unlock(rwlock->m_d->mutex);
	}
	XAtomic_fetch_sub_size_t(&rwlock->m_d->read_waiters, 1);
}

static void XReadWriteLock_lockForWrite_NonSpin(XReadWriteLock* rwlock)
{
	if (XReadWriteLock_tryLockForWrite(rwlock)) {
		return;
	}

	XAtomic_fetch_add_size_t(&rwlock->m_d->write_waiters, 1);
	while (true) {
		XMutex_lock(rwlock->m_d->mutex);

		if (XReadWriteLock_tryLockForWrite(rwlock)) {
			XMutex_unlock(rwlock->m_d->mutex);
			break;
		}

		XWaitCondition_wait(rwlock->m_d->writeCond, rwlock->m_d->mutex, -1);
		XMutex_unlock(rwlock->m_d->mutex);
	}
	XAtomic_fetch_sub_size_t(&rwlock->m_d->write_waiters, 1);
}
void XReadWriteLock_lockForRead(XReadWriteLock* rwlock)
{
	if (!rwlock) return;
	if (XReadWriteLock_type(rwlock) & XReadWriteLock_Spin) {
		XReadWriteLock_lockForRead_Spin(rwlock);
	}
	else {
		XReadWriteLock_lockForRead_NonSpin(rwlock);
	}
}

bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (rwlock->type & XReadWriteLock_Recursive) {
		return try_acquire_read_recursive(rwlock);
	}
	else {
		return try_acquire_read_nonrecursive(rwlock);
	}
}

bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* rwlock, int32_t timeout_ms)
{
	if (!rwlock) return false;

	if (XReadWriteLock_type(rwlock) & XReadWriteLock_Spin) {
		if (timeout_ms <= 0) {
			return XReadWriteLock_tryLockForRead(rwlock);
		}
		const int32_t max_spins = timeout_ms * 1000;
		for (int32_t i = 0; i < max_spins; ++i) {
			if (XReadWriteLock_tryLockForRead(rwlock)) {
				return true;
			}
		}
		return false;
	}
	else {
		if (timeout_ms <= 0) {
			XReadWriteLock_lockForRead_NonSpin(rwlock);
			return true;
		}

		if (XReadWriteLock_tryLockForRead(rwlock)) {
			return true;
		}

		size_t start_time = XTimerBase_getCurrentTime();
		XAtomic_fetch_add_size_t(&rwlock->m_d->read_waiters, 1);
		bool result = false;
		while (true) {
			XMutex_lock(rwlock->m_d->mutex);

			size_t current_write_waiters = XAtomic_load_size_t(&rwlock->m_d->write_waiters);
			if (current_write_waiters > 0) {
				// 有写者在等，读者不能插队
				size_t current_time = XTimerBase_getCurrentTime();
				int32_t elapsed = (int32_t)(current_time - start_time);
				int32_t remaining = timeout_ms - elapsed;
				if (remaining <= 0) {
					XMutex_unlock(rwlock->m_d->mutex);
					break;
				}
				XWaitCondition_wait(rwlock->m_d->readCond, rwlock->m_d->mutex, remaining);
				XMutex_unlock(rwlock->m_d->mutex);
				continue;
			}

			if (XReadWriteLock_tryLockForRead(rwlock)) {
				result = true;
				XMutex_unlock(rwlock->m_d->mutex);
				break;
			}

			size_t current_time = XTimerBase_getCurrentTime();
			int32_t elapsed = (int32_t)(current_time - start_time);
			int32_t remaining = timeout_ms - elapsed;
			if (remaining <= 0) {
				XMutex_unlock(rwlock->m_d->mutex);
				break;
			}

			XWaitCondition_wait(rwlock->m_d->readCond, rwlock->m_d->mutex, remaining);
			XMutex_unlock(rwlock->m_d->mutex);
		}
		XAtomic_fetch_sub_size_t(&rwlock->m_d->read_waiters, 1);
		return result;
	}
}

// --- 写锁相关公共接口 ---
void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock)
{
	if (!rwlock) return;
	if (XReadWriteLock_type(rwlock) & XReadWriteLock_Spin) {
		XReadWriteLock_lockForWrite_Spin(rwlock);
	}
	else {
		XReadWriteLock_lockForWrite_NonSpin(rwlock);
	}
}

bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (rwlock->type & XReadWriteLock_Recursive)
	{
		return try_acquire_write_recursive(rwlock);
	}
	else
	{
		return try_acquire_write_nonrecursive(rwlock);
	}
	
}

bool XReadWriteLock_tryLockForWriteTimeout(XReadWriteLock* rwlock, int32_t timeout_ms)
{
	if (!rwlock) return false;

	if (XReadWriteLock_type(rwlock) & XReadWriteLock_Spin) {
		if (timeout_ms <= 0) {
			return XReadWriteLock_tryLockForWrite(rwlock);
		}
		const int32_t max_spins = timeout_ms * 1000;
		for (int32_t i = 0; i < max_spins; ++i) {
			if (XReadWriteLock_tryLockForWrite(rwlock)) {
				return true;
			}
		}
		return false;
	}
	else {
		if (timeout_ms <= 0) {
			XReadWriteLock_lockForWrite_NonSpin(rwlock);
			return true;
		}

		if (XReadWriteLock_tryLockForWrite(rwlock)) {
			return true;
		}

		size_t start_time = XTimerBase_getCurrentTime();
		XAtomic_fetch_add_size_t(&rwlock->m_d->write_waiters, 1);
		bool result = false;
		while (true) {
			XMutex_lock(rwlock->m_d->mutex);

			if (XReadWriteLock_tryLockForWrite(rwlock)) {
				result = true;
				XMutex_unlock(rwlock->m_d->mutex);
				break;
			}

			size_t current_time = XTimerBase_getCurrentTime();
			int32_t elapsed = (int32_t)(current_time - start_time);
			int32_t remaining = timeout_ms - elapsed;
			if (remaining <= 0) {
				XMutex_unlock(rwlock->m_d->mutex);
				break;
			}

			XWaitCondition_wait(rwlock->m_d->writeCond, rwlock->m_d->mutex, remaining);
			XMutex_unlock(rwlock->m_d->mutex);
		}
		XAtomic_fetch_sub_size_t(&rwlock->m_d->write_waiters, 1);
		return result;
	}
}
// ========== 核心状态更新逻辑 (Spin/Non-Spin 共用) ==========
static void unlock_state_update(XReadWriteLock* rwlock)
{
	if (rwlock->type & XReadWriteLock_Recursive) {
		ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
		if (!tls) return;

		if (tls->writer_count > 0) {
			tls->writer_count--;
			if (tls->writer_count == 0) {
				XAtomic_store_size_t(&rwlock->state, 0);
			}
		}
		else if (tls->reader_count > 0) {
			tls->reader_count--;
			if (tls->reader_count == 0) {
				XAtomic_fetch_sub_size_t(&rwlock->state, 1);
			}
		}
	}
	else {
		size_t current_state = XAtomic_load_size_t(&rwlock->state);
		if (current_state & WRITER_ACTIVE_FLAG) {
			XAtomic_store_size_t(&rwlock->state, 0);
		}
		else {
			XAtomic_fetch_sub_size_t(&rwlock->state, 1);
		}
	}
}
// --- 解锁接口 ---
void XReadWriteLock_unlock(XReadWriteLock* rwlock)
{
	if (!rwlock) return;

	// 1. 首先，统一更新锁的核心状态 (state)
	unlock_state_update(rwlock);

	// 2. 如果是非自旋模式，则执行唤醒逻辑
	if (!(XReadWriteLock_type(rwlock) & XReadWriteLock_Spin)) {
		XMutex_lock(rwlock->m_d->mutex);
		size_t current_write_waiters = XAtomic_load_size_t(&rwlock->m_d->write_waiters);
		if (current_write_waiters > 0) {
			XWaitCondition_wakeOne(rwlock->m_d->writeCond);
		}
		else {
			XWaitCondition_wakeAll(rwlock->m_d->readCond);
		}
		XMutex_unlock(rwlock->m_d->mutex);
	}
}

bool XReadWriteLock_hasReadLock(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (!rwlock || !(rwlock->type & XReadWriteLock_Recursive)) return false;
	ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
	return tls && (tls->reader_count > 0);
	
}

bool XReadWriteLock_hasWriteLock(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (!rwlock || !(rwlock->type & XReadWriteLock_Recursive)) return false;
	ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
	return tls && (tls->writer_count > 0);
}