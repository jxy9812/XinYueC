#include "XReadLocker.h"
#include "XRecursiveLockState.h"
#include "XThread.h"
#include "XHashMap.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XTimer.h"
#include <stdlib.h>
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
//读写自旋锁
typedef struct XReadWriteLock
{
	XLock_Type type;
	XAtomic_size_t state; // : false=unlocked, true=locked
	XAtomic_size_t read_waiters; // 等待读锁的线程数
	XAtomic_size_t write_waiters; //等待写锁的线程数
	char m_d[];//扩展数据
}XReadWriteLock;
//用户等待休眠 这里声明的结构实际是直接存储对象而不是指针
//typedef struct WaitPrivate
//{
//	XMutex* mutex;               // 辅助互斥锁，用于条件变量
//	XWaitCondition* readCond;    // 等待读锁的条件变量
//	XWaitCondition* writeCond;   // 等待写锁的条件变量
//	char m_d[];//扩展数据
//}WaitPrivate;
typedef struct WaitPrivate  WaitPrivate;
#define GetWaitPrivate(lock)	((WaitPrivate*)lock->m_d)
#define GetMutex(lock)           GetWaitPrivate(lock)
#define GetReadCond(lock)       (((uint8_t*)GetWaitPrivate(lock))+XMutex_typetSize(XLock_NonRecursive))
#define GetWriteCond(lock)      (((uint8_t*)GetReadCond(lock))+XWaitCondition_typeSize())
static size_t XMutex_WaitPrivate_size()
{
	return XMutex_typetSize(XLock_NonRecursive)+2*XWaitCondition_typeSize();
}

// --- 非递归模式的内部函数 ---
static bool try_acquire_read_nonrecursive(XReadWriteLock* rwlock) 
{
	size_t current_state = XAtomic_load_size_t(&rwlock->state, XAtomic_MemoryOrder_Relaxed);

	if (current_state & WRITER_ACTIVE_FLAG) {
		return false;
	}

	size_t expected = current_state;
	size_t desired = current_state + 1;
	return XAtomic_compare_exchange_strong_size_t(&rwlock->state, &expected, desired, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);
}

static bool try_acquire_write_nonrecursive(XReadWriteLock* rwlock) 
{
	size_t current_state = XAtomic_load_size_t(&rwlock->state, XAtomic_MemoryOrder_Relaxed);

	if (current_state != 0) {
		return false;
	}

	size_t expected = 0;
	size_t desired = WRITER_ACTIVE_FLAG;
	return XAtomic_compare_exchange_strong_size_t(&rwlock->state, &expected, desired, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);
}

// --- 递归模式的内部函数 ---
static bool try_acquire_read_recursive(XReadWriteLock* rwlock)
{
	// 1. 获取当前线程对此锁 (rwlock) 的私有状态。
	XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
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
	XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
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

XReadWriteLock* XReadWriteLock_create(XLock_Type type)
{
	XReadWriteLock* rwlock = (XReadWriteLock*)XMemory_malloc(XReadWriteLock_typetSize(type));
	XReadWriteLock_init(rwlock, type);
	return rwlock;
}
size_t XReadWriteLock_typetSize(XLock_Type type)
{
	size_t size = sizeof(XReadWriteLock);
	if (!(type & XLock_Spin))
		size += XMutex_WaitPrivate_size();
	return size;
}
void XReadWriteLock_init(XReadWriteLock* rwlock, XLock_Type type)
{
    if (!rwlock)return;
	memset(rwlock, 0, XReadWriteLock_typetSize(type));
	//rwlock->type = type;

	if (type == XLock_Spin)
		rwlock->type = XLock_SpinNonRecursive;
	else
		rwlock->type = type;

	if (type & XLock_Recursive)
		XRecursiveLockState_init();
	if (!(type & XLock_Spin))
	{
		XMutex_init(GetMutex(rwlock), XLock_NonRecursive);
		XWaitCondition_init(GetReadCond(rwlock));
		XWaitCondition_init(GetWriteCond(rwlock));
	}
}
void XReadWriteLock_deinit(XReadWriteLock* rwlock) 
{
	if (XReadWriteLock_type(rwlock) & XLock_Recursive)
	{
		XRecursiveLockState_clear(rwlock);//清理全局数据
	}
	if (!(rwlock->type & XLock_Spin))
	{
		XMutex_deinit(GetMutex(rwlock));
		XWaitCondition_deinit(GetReadCond(rwlock));
		XWaitCondition_deinit(GetWriteCond(rwlock));
	}
}

void XReadWriteLock_delete(XReadWriteLock* rwlock) 
{
	if (rwlock) {
		XReadWriteLock_deinit(rwlock);
		XMemory_free(rwlock);
	}
}
XLock_Type XReadWriteLock_type(XReadWriteLock* rwlock)
{
	return rwlock ? rwlock->type : XLock_NonRecursive;
}
// ========== Spin 模式专用接口 ==========
static void XReadWriteLock_lockForRead_Spin(XReadWriteLock* rwlock)
{
	if (rwlock->type & XLock_Recursive) {
		// --- 修复: 使用分层自旋策略 ---
		const int MAX_BUSY_WAITS = 100; // 可根据实际情况调整
		int busy_wait_count = 0;

		while (!try_acquire_read_recursive(rwlock)) {
			if (busy_wait_count < MAX_BUSY_WAITS) {
				// 第一层: 忙等待 (可以在此处插入平台相关的 pause 指令以进一步优化)
				busy_wait_count++;
				// 对于简单实现，我们只是继续循环，不调用任何系统函数。
				// 在真实环境中，这里应该是一个非常轻量的操作。
			}
			else {
				// 第二层: 让出CPU
				XThread_yieldCurrentThread();
				busy_wait_count = 0; // 重置计数器
			}
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
	if (rwlock->type & XLock_Recursive) {
		// --- 修复: 使用分层自旋策略 ---
		const int MAX_BUSY_WAITS = 100; // 可根据实际情况调整
		int busy_wait_count = 0;

		while (!try_acquire_write_recursive(rwlock)) {
			if (busy_wait_count < MAX_BUSY_WAITS) {
				// 第一层: 忙等待
				busy_wait_count++;
			}
			else {
				// 第二层: 让出CPU
				XThread_yieldCurrentThread();
				busy_wait_count = 0; // 重置计数器
			}
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
	// --- 第一步：处理递归状态 ---
	if (rwlock->type & XLock_Recursive) {
		XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
		if (tls && (tls->reader_count > 0 || tls->writer_count > 0)) {
			tls->reader_count++;
			return;
		}
	}

	// 第二步：快速路径
	if (XReadWriteLock_tryLockForRead(rwlock)) {
		return;
	}

	// --- 第三步：进入慢速路径 ---
	// 所有非自旋锁都增加 read_waiters
	XAtomic_fetch_add_size_t(&rwlock->read_waiters, 1, XAtomic_MemoryOrder_Relaxed);

	while (true) {
		XMutex_lock(GetMutex(rwlock));

		// --- 写者优先：检查是否有写者在等待 ---
		bool should_wait_for_writers = (XAtomic_load_size_t(&rwlock->write_waiters, XAtomic_MemoryOrder_Relaxed) > 0);

		if (should_wait_for_writers) {
			XWaitCondition_wait(GetReadCond(rwlock), GetMutex(rwlock), -1);
			XMutex_unlock(GetMutex(rwlock));
			continue;
		}

		if (XReadWriteLock_tryLockForRead(rwlock)) {
			XMutex_unlock(GetMutex(rwlock));
			break;
		}

		XWaitCondition_wait(GetReadCond(rwlock), GetMutex(rwlock), -1);
		XMutex_unlock(GetMutex(rwlock));
	}

	// --- 第四步：清理 ---
	XAtomic_fetch_sub_size_t(&rwlock->read_waiters, 1, XAtomic_MemoryOrder_Relaxed);
}

static void XReadWriteLock_lockForWrite_NonSpin(XReadWriteLock* rwlock)
{
	// --- 第一步：处理递归写锁 ---
	if (rwlock->type & XLock_Recursive) {
		XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
		if (tls && tls->writer_count > 0) {
			tls->writer_count++;
			return;
		}
		// 注意：从读锁升级到写锁在此设计中是被禁止的。
	}

	// --- 第二步：尝试快速获取写锁 ---
	if (XReadWriteLock_tryLockForWrite(rwlock)) {
		return;
	}

	// --- 第三步：进入慢速路径 ---
	// 所有非自旋锁都增加 write_waiters
	XAtomic_fetch_add_size_t(&rwlock->write_waiters, 1, XAtomic_MemoryOrder_Relaxed);

	while (true) {
		XMutex_lock(GetMutex(rwlock));

		if (XReadWriteLock_tryLockForWrite(rwlock)) {
			XMutex_unlock(GetMutex(rwlock));
			break;
		}

		XWaitCondition_wait(GetWriteCond(rwlock), GetMutex(rwlock), -1);
		XMutex_unlock(GetMutex(rwlock));
	}

	// --- 第四步：清理 ---
	XAtomic_fetch_sub_size_t(&rwlock->write_waiters, 1, XAtomic_MemoryOrder_Relaxed);
}
void XReadWriteLock_lockForRead(XReadWriteLock* rwlock)
{
	if (!rwlock) return;
	if (XReadWriteLock_type(rwlock) & XLock_Spin) {
		XReadWriteLock_lockForRead_Spin(rwlock);
	}
	else {
		XReadWriteLock_lockForRead_NonSpin(rwlock);
	}
}

bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (rwlock->type & XLock_Recursive) {
		return try_acquire_read_recursive(rwlock);
	}
	else {
		return try_acquire_read_nonrecursive(rwlock);
	}
}

bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* rwlock, int32_t timeout_ms)
{
	if (!rwlock) return false;

	// --- 第一步：处理递归读锁 ---
	if (rwlock->type & XLock_Recursive) {
		XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
		if (tls && (tls->reader_count > 0 || tls->writer_count > 0)) {
			tls->reader_count++;
			return true;
		}
	}

	// --- 第二步：尝试快速获取读锁 ---
	if (XReadWriteLock_tryLockForRead(rwlock)) {
		return true;
	}

	if (timeout_ms == 0) {
		return false;
	}

	// --- 第三步：进入慢速路径（带超时等待）---
	XAtomic_fetch_add_size_t(&rwlock->read_waiters, 1, XAtomic_MemoryOrder_Relaxed);

	bool acquired = false;
	while (!acquired) {
		XMutex_lock(GetMutex(rwlock));

		// --- 写者优先 ---
		bool should_wait_for_writers = (XAtomic_load_size_t(&rwlock->write_waiters, XAtomic_MemoryOrder_Relaxed) > 0);

		if (should_wait_for_writers) {
			bool was_signaled = XWaitCondition_wait(GetReadCond(rwlock), GetMutex(rwlock), timeout_ms);
			XMutex_unlock(GetMutex(rwlock));
			if (!was_signaled) { // 超时
				break;
			}
			continue;
		}

		if (XReadWriteLock_tryLockForRead(rwlock)) {
			acquired = true;
			XMutex_unlock(GetMutex(rwlock));
			break;
		}

		bool was_signaled = XWaitCondition_wait(GetReadCond(rwlock), GetMutex(rwlock), timeout_ms);
		XMutex_unlock(GetMutex(rwlock));
		if (!was_signaled) { // 超时
			break;
		}
	}

	// --- 第四步：清理 ---
	XAtomic_fetch_sub_size_t(&rwlock->read_waiters, 1, XAtomic_MemoryOrder_Relaxed);
	return acquired;
}

// --- 写锁相关公共接口 ---
void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock)
{
	if (!rwlock) return;
	if (XReadWriteLock_type(rwlock) & XLock_Spin) {
		XReadWriteLock_lockForWrite_Spin(rwlock);
	}
	else {
		XReadWriteLock_lockForWrite_NonSpin(rwlock);
	}
}

bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (rwlock->type & XLock_Recursive)
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

	// --- 自旋模式处理 ---
	if (XReadWriteLock_type(rwlock) & XLock_Spin) {
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

	// --- 非自旋模式处理 ---
	else {
		if (timeout_ms < 0) {
			XReadWriteLock_lockForWrite_NonSpin(rwlock);
			return true;
		}
		if (timeout_ms == 0) {
			return XReadWriteLock_tryLockForWrite(rwlock);
		}

		if (XReadWriteLock_tryLockForWrite(rwlock)) {
			return true;
		}

		// --- 进入慢速路径 ---
		XAtomic_fetch_add_size_t(&rwlock->write_waiters, 1, XAtomic_MemoryOrder_Relaxed);

		size_t start_time = XTimer_getCurrentTime();
		bool result = false;

		while (true) {
			XMutex_lock(GetMutex(rwlock));

			if (XReadWriteLock_tryLockForWrite(rwlock)) {
				result = true;
				XMutex_unlock(GetMutex(rwlock));
				break;
			}

			size_t current_time = XTimer_getCurrentTime();
			int32_t elapsed = (int32_t)(current_time - start_time);
			int32_t remaining = timeout_ms - elapsed;
			if (remaining <= 0) {
				XMutex_unlock(GetMutex(rwlock));
				break;
			}

			bool was_signaled = XWaitCondition_wait(GetWriteCond(rwlock), GetMutex(rwlock), remaining);
			XMutex_unlock(GetMutex(rwlock));

			if (!was_signaled) {
				break;
			}
		}

		// --- 清理 ---
		XAtomic_fetch_sub_size_t(&rwlock->write_waiters, 1, XAtomic_MemoryOrder_Relaxed);
		return result;
	}
}
// ========== 核心状态更新逻辑 (Spin/Non-Spin 共用) ==========
static void unlock_state_update(XReadWriteLock* rwlock)
{
	if (rwlock->type & XLock_Recursive) {
		XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
		if (!tls) return;

		if (tls->writer_count > 0) {
			tls->writer_count--;
			if (tls->writer_count == 0) {
				// 修复点: 使用 Release 内存序
				// 这确保了 'tls->writer_count = 0' 的操作
				// 在此 store 操作之前完成，并且对其他线程可见。
				XAtomic_store_size_t(&rwlock->state, 0, XAtomic_MemoryOrder_Release);
			}
		}
		else if (tls->reader_count > 0) {
			tls->reader_count--;
			if (tls->reader_count == 0) {
				// 对于读者，我们执行原子减法。
				// Acquire-Release 语义通常由 compare_exchange 或 fetch_add/sub 提供。
				// 这里保持原有逻辑，但为了对称性，可以考虑其内存序。
				// 原有的 fetch_sub(Relaxed) 在此处通常是安全的，
				// 因为读者的释放不涉及像写者那样的独占权转移。
				XAtomic_fetch_sub_size_t(&rwlock->state, 1, XAtomic_MemoryOrder_Relaxed);
			}
		}
	}
	else {
		// 非递归模式，保持原样
		size_t current_state = XAtomic_load_size_t(&rwlock->state, XAtomic_MemoryOrder_Relaxed);
		if (current_state & WRITER_ACTIVE_FLAG) {
			XAtomic_store_size_t(&rwlock->state, 0, XAtomic_MemoryOrder_Relaxed);
		}
		else {
			XAtomic_fetch_sub_size_t(&rwlock->state, 1, XAtomic_MemoryOrder_Relaxed);
		}
	}
}
// --- 解锁接口 ---
void XReadWriteLock_unlock(XReadWriteLock* rwlock)
{
	if (!rwlock) return;

	// 1. 更新核心状态
	unlock_state_update(rwlock);

	// 2. 非自旋模式的唤醒逻辑
	if (!(XReadWriteLock_type(rwlock) & XLock_Spin)) {
		XMutex_lock(GetMutex(rwlock));

		// --- 关键修复：直接读取公共的 write_waiters ---
		size_t current_write_waiters = XAtomic_load_size_t(&rwlock->write_waiters, XAtomic_MemoryOrder_Relaxed);

		if (current_write_waiters > 0) {
			// 有写者在等待，优先唤醒一个写者
			XWaitCondition_wakeOne(GetWriteCond(rwlock));
		}
		else {
			// 否则，唤醒所有等待的读者
			XWaitCondition_wakeAll(GetReadCond(rwlock));
		}

		XMutex_unlock(GetMutex(rwlock));
	}
}

bool XReadWriteLock_hasReadLock(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (!rwlock || !(rwlock->type & XLock_Recursive)) return false;
	XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
	return tls && (tls->reader_count > 0);
	
}

bool XReadWriteLock_hasWriteLock(XReadWriteLock* rwlock)
{
	if (!rwlock) return false;
	if (!rwlock || !(rwlock->type & XLock_Recursive)) return false;
	XRecursiveLockState* tls = XRecursiveLockState_get(rwlock);
	return tls && (tls->writer_count > 0);
}