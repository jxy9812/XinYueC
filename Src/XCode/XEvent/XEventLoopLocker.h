// Copyright (C) 2026 Your Name. MIT License.
#ifndef XEVENTLOOPLOCKER_H
#define XEVENTLOOPLOCKER_H

#include "XObject.h"
#include "XEventLoop.h"
#include "XThread.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief XEventLoopLocker - 防止事件循环意外退出的 RAII 锁
	 *
	 * 作用：在对象生命周期内，阻止关联的事件循环被 exit() 退出。
	 * 常用于长时间操作期间（如模态对话框、网络请求）防止外部调用 quit() 导致提前退出。
	 *
	 * 支持三种锁定目标：
	 *   - 特定 XEventLoop 对象
	 *   - 特定 XThread 的事件循环
	 *   - 全局应用的主事件循环（默认）
	 */
	typedef struct XEventLoopLocker XEventLoopLocker;

	/**
	 * @brief 创建一个锁定全局应用事件循环的 locker（默认行为）
	 */
	XEventLoopLocker* XEventLoopLocker_create(void);

	/**
	 * @brief 创建一个锁定指定事件循环的 locker
	 */
	XEventLoopLocker* XEventLoopLocker_createForLoop(XEventLoop* loop);

	/**
	 * @brief 创建一个锁定指定线程事件循环的 locker
	 */
	XEventLoopLocker* XEventLoopLocker_createForThread(XThread* thread);

	/**
	 * @brief 析构函数（释放锁）
	 */
	void XEventLoopLocker_destroy(XEventLoopLocker* self);

	/**
	 * @brief 移动构造（转移所有权）
	 * 调用后 other 不再有效，不应再使用。
	 */
	XEventLoopLocker* XEventLoopLocker_move(XEventLoopLocker* other);

	/**
	 * @brief 交换两个 locker 的内容
	 */
	void XEventLoopLocker_swap(XEventLoopLocker* a, XEventLoopLocker* b);

	// 禁止拷贝（C 中通过不提供 copy 函数实现）

#ifdef __cplusplus
}
#endif

#endif // XEVENTLOOPLOCKER_H