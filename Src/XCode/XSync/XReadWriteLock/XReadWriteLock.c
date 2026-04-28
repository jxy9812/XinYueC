#include "XReadLocker.h"
#include "XAtomic.h"
#include "XThreadData.h"
#include "XThread.h"
#include "XHashMap.h"
#include <stdlib.h>
static XReadWriteLock* global_lock=NULL;
static XHashMap* global_locks_map =NULL;
/**
 * @brief ThreadLocalLockState - 用于支持 XReadWriteLock 的递归模式
 */
typedef struct
{
	int32_t reader_count; // 当前线程持有的读锁计数
	int32_t writer_count; // 当前线程持有的写锁计数 (0 or >=1)
} ThreadLocalLockState;
static void recursive_locks_map_init()//    XHashMap* m_recursive_locks_map; // 键: XReadWriteLock*, 值: <threadId,ThreadLocalLockState>
{
	if (global_locks_map)return;
	global_lock = XReadWriteLock_create(XReadWriteLock_Spin);
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
//bool
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
//平台实现
size_t XReadWriteLock_platform_getTypeSize();
void XReadWriteLock_platform_init(XReadWriteLock* rwlock, XReadWriteLock_Type type);
void XReadWriteLock_platform_deinit(XReadWriteLock* lock);
void XReadWriteLock_platform_lockForRead(XReadWriteLock* lock);
bool XReadWriteLock_platform_tryLockForRead(XReadWriteLock* lock);
bool XReadWriteLock_platform_tryLockForReadTimeout(XReadWriteLock* lock, int32_t timeout);
void XReadWriteLock_platform_lockForWrite(XReadWriteLock* lock);
bool XReadWriteLock_platform_tryLockForWrite(XReadWriteLock* lock);
bool XReadWriteLock_platform_tryLockForWriteTimeout(XReadWriteLock* lock, int32_t timeout);
void XReadWriteLock_platform_unlock(XReadWriteLock* lock);
bool XReadWriteLock_platform_hasReadLock(XReadWriteLock* lock);
bool XReadWriteLock_platform_hasWriteLock(XReadWriteLock* lock);
typedef struct XReadWriteLock
{
	XReadWriteLock_Type type;
}XReadWriteLock;
//读写自旋锁
typedef struct XReadWriteSpinLock
{
	XReadWriteLock_Type type;
    XAtomic_size_t state; // 核心状态变量
}XReadWriteSpinLock;

// --- 非递归模式的内部函数 ---
static bool try_acquire_read_nonrecursive(XReadWriteSpinLock* rwlock) {
	size_t current_state = XAtomic_load_size_t(&rwlock->state);

	if (current_state & WRITER_ACTIVE_FLAG) {
		return false;
	}

	size_t expected = current_state;
	size_t desired = current_state + 1;
	return XAtomic_compare_exchange_strong_size_t(&rwlock->state, &expected, desired);
}

static bool try_acquire_write_nonrecursive(XReadWriteSpinLock* rwlock) {
	size_t current_state = XAtomic_load_size_t(&rwlock->state);

	if (current_state != 0) {
		return false;
	}

	size_t expected = 0;
	size_t desired = WRITER_ACTIVE_FLAG;
	return XAtomic_compare_exchange_strong_size_t(&rwlock->state, &expected, desired);
}

// --- 递归模式的内部函数 ---
static bool try_acquire_read_recursive(XReadWriteSpinLock* rwlock)
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

static bool try_acquire_write_recursive(XReadWriteSpinLock* rwlock) 
{
	// 1. 获取当前线程对此锁 (rwlock) 的私有状态。
	ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
	if (!tls) return false;

	// 2. 情况A: 当前线程已经是此锁的写者。
	//    这是典型的递归写场景。我们只需增加写锁计数，并返回成功。
	if (tls->writer_count > 0) {
		//tls->writer_count++;
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
	XReadWriteLock* rwlock = (XReadWriteLock*)XMemory_malloc(XReadWriteLock_getTypeSize(type));
	XReadWriteLock_init(rwlock, type);
	return rwlock;
}
void XReadWriteLock_init(XReadWriteLock* rwlock, XReadWriteLock_Type type) 
{
    if (!rwlock)return;
	if(type & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* lock = (XReadWriteSpinLock*)rwlock;
		XAtomic_init(lock->state, 0);
		if (type == XReadWriteLock_Spin)
			lock->type = XReadWriteLock_SpinNonRecursive;
		else
			lock->type = type;
		if (type& XReadWriteLock_Recursive)
			recursive_locks_map_init();
	}
	else
	{
		XReadWriteLock_platform_init(rwlock,type);
	}
}
void XReadWriteLock_deinit(XReadWriteLock* rwlock) 
{
	if (XReadWriteLock_type(rwlock) & XReadWriteLock_SpinRecursive)
	{
		XThreadData_local_lock_clear(rwlock);//清理全局数据
	}
	else
	{
		XReadWriteLock_platform_deinit(rwlock);
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

size_t XReadWriteLock_getTypeSize(XReadWriteLock_Type type)
{
	if(type& XReadWriteLock_Spin)
		return sizeof(XReadWriteSpinLock);
	return XReadWriteLock_platform_getTypeSize();
}
void XReadWriteLock_lockForRead(XReadWriteLock* lock)
{
	if (!lock) return;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		if (rwlock->type&XReadWriteLock_Recursive) {
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
	else
	{
		XReadWriteLock_platform_lockForRead(lock);
	}

}

bool XReadWriteLock_tryLockForRead(XReadWriteLock* lock)
{
	if (!lock) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		if (rwlock->type&XReadWriteLock_Recursive) {
			return try_acquire_read_recursive(rwlock);
		}
		else {
			return try_acquire_read_nonrecursive(rwlock);
		}
	}
	else
	{
		return XReadWriteLock_platform_tryLockForRead(lock);
	}
}

bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* lock, int32_t timeout_ms)
{
	if (!lock) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
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
	else
	{
		return XReadWriteLock_platform_tryLockForReadTimeout(lock,timeout_ms);
	}
	
}

// --- 写锁相关公共接口 ---
void XReadWriteLock_lockForWrite(XReadWriteLock* lock)
{
	if (!lock) return;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		if (rwlock->type &XReadWriteLock_Recursive) {
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
	else
	{
		XReadWriteLock_platform_lockForWrite(lock);
	}

	
}

bool XReadWriteLock_tryLockForWrite(XReadWriteLock* lock) 
{
	if (!lock) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		if (rwlock->type& XReadWriteLock_Recursive) 
		{
			return try_acquire_write_recursive(rwlock);
		}
		else 
		{
			return try_acquire_write_nonrecursive(rwlock);
		}
	}
	else
	{
		XReadWriteLock_platform_tryLockForWrite(lock);
	}

	
}

bool XReadWriteLock_tryLockForWriteTimeout(XReadWriteLock* lock, int32_t timeout_ms)
{
	if (!lock) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		if (!rwlock) return false;
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
	else
	{
		return XReadWriteLock_platform_tryLockForWriteTimeout(lock, timeout_ms);
	}


	
}

// --- 解锁接口 ---
void XReadWriteLock_unlock(XReadWriteLock* lock)
{
	if (!lock) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		// 检查锁是否为递归模式
		if (rwlock->type & XReadWriteLock_Recursive) {
			// ========== 递归模式解锁流程 ==========

			// 1. 获取当前线程对此锁的私有递归状态
			ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
			if (!tls) return; // 如果获取失败（理论上不应该发生），直接返回

			// 2. 情况A: 当前线程是此锁的写者
			if (tls->writer_count > 0) {
				// 减少本地的写锁计数
				tls->writer_count--;

				// 检查是否完全释放了所有写锁
				if (tls->writer_count == 0) {
					// 是的，当前线程彻底放弃了这把锁的写权限。
					// 现在需要更新全局状态：清除写者标志位。
					// 因为写锁是独占的，所以直接将 state 置为 0 即可。
					XAtomic_store_size_t(&rwlock->state, 0);
				}
				// 无论是否归零，都已处理完毕，直接返回
				return;
			}

			// 3. 情况B: 当前线程是此锁的读者
			if (tls->reader_count > 0) {
				// 减少本地的读锁计数
				tls->reader_count--;

				// 检查是否完全释放了所有读锁
				if (tls->reader_count == 0) {
					// 是的，当前线程彻底放弃了这把锁的读权限。
					// 现在需要更新全局的读者计数器。
					// 使用原子减操作，因为可能有多个读者同时在释放锁。
					XAtomic_fetch_sub_size_t(&rwlock->state, 1);
				}
				// 无论是否归零，都已处理完毕，直接返回
				return;
			}

			// 注意: 如果既不是写者也不是读者，说明解锁了一个未持有的锁，
			// 这是一个错误，但此处选择静默忽略。
		}
		else {
			// ========== 非递归模式解锁流程 ==========

			// 1. 加载当前的全局锁状态
			size_t current_state = XAtomic_load_size_t(&rwlock->state);

			// 2. 判断当前持有的是写锁还是读锁
			if (current_state & WRITER_ACTIVE_FLAG) {
				// 情况A: 持有的是写锁
				// 写锁是独占的，直接将全局状态清零即可。
				XAtomic_store_size_t(&rwlock->state, 0);
			}
			else {
				// 情况B: 持有的是读锁
				// 需要将全局的读者计数器减1。
				// 使用原子减操作，因为可能有多个读者同时在释放锁。
				XAtomic_fetch_sub_size_t(&rwlock->state, 1);
			}
		}
	}
	else
	{
		XReadWriteLock_platform_unlock(lock);
	}

	
}

bool XReadWriteLock_hasReadLock(XReadWriteLock* lock)
{
	if (!lock) return false;
	if (!lock || !(lock->type & XReadWriteLock_Recursive)) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
		return tls && (tls->reader_count > 0);
	}
	else
	{
		XReadWriteLock_platform_hasReadLock(lock);
	}
	
}

bool XReadWriteLock_hasWriteLock(XReadWriteLock* lock)
{
	if (!lock) return false;
	if (!lock || !(lock->type & XReadWriteLock_Recursive)) return false;
	if (XReadWriteLock_type(lock) & XReadWriteLock_Spin)
	{
		XReadWriteSpinLock* rwlock = (XReadWriteSpinLock*)lock;
		ThreadLocalLockState* tls = XThreadData_local_lock_state(rwlock);
		return tls && (tls->writer_count > 0);
	}
	else
	{
		XReadWriteLock_platform_hasWriteLock(lock);
	}
	
}