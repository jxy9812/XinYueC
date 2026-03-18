// XHostInfo.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#ifndef XHOSTINFO_H
#define XHOSTINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
#include "XHostAddress.h"
#include "XObject.h"

/**
 * @brief 主机信息查询结果状态。
 *
 * 描述 DNS 查询的最终结果状态。
 */
typedef enum XHostInfo_Error {
    XHostInfo_NoError = 0,      ///< 查询成功，地址列表有效。
    XHostInfo_HostNotFound = 1, ///< 主机名无法解析（NXDOMAIN 或类似错误）。
    XHostInfo_UnknownError = 2  ///< 发生未知错误（如网络不可达、超时等）。
} XHostInfo_Error;

// 前向声明私有数据结构
struct XHostInfoPrivate;

/**
 * @brief 表示主机名解析结果。
 *
 * XHostInfo 封装了从主机名到 IP 地址列表的映射结果。
 * 它是值语义对象，支持拷贝，并通过 PIMPL 模式隐藏平台相关细节。
 *
 * @note 此类不进行 DNS 缓存，每次查询均为实时请求。
 * @note 所有公共 API 均为线程安全（内部使用互斥锁保护）。
 */
typedef struct XHostInfo {
    XClass m_class;                     ///< 基类虚表指针
    struct XHostInfoPrivate* d;         ///< 私有数据指针（PIMPL）
} XHostInfo;

// ==================== 构造与析构 ====================

/**
 * @brief 创建一个空的 XHostInfo 实例。
 * @return 新分配的 XHostInfo 实例，需调用 XHostInfo_delete() 释放。
 */
XHostInfo* XHostInfo_create(void);

/**
 * @brief 拷贝构造函数。
 * @param other 源 XHostInfo 对象。
 * @return 新分配的副本。
 */
XHostInfo* XHostInfo_create_copy(const XHostInfo* other);

/**
 * @brief 销毁 XHostInfo 实例。
 * @param info 要销毁的实例。
 */
void XHostInfo_delete(XHostInfo* info);

// ==================== 属性访问器 ====================

/**
 * @brief 获取被查询的主机名。
 * @param info XHostInfo 实例。
 * @return 主机名字符串（若未设置则返回空字符串）。
 */
const char* XHostInfo_hostName(const XHostInfo* info);

/**
 * @brief 设置主机名。
 * @param info XHostInfo 实例。
 * @param name 主机名（会被复制）。
 */
void XHostInfo_setHostName(XHostInfo* info, const char* name);

/**
 * @brief 获取解析得到的 IP 地址列表。
 * @param info XHostInfo 实例。
 * @param count 输出参数，返回地址数量。
 * @return 指向 XHostAddress 数组的指针，若无地址则返回 NULL。
 */
const XHostAddress* XHostInfo_addresses(const XHostInfo* info, int* count);

/**
 * @brief 设置 IP 地址列表。
 * @param info XHostInfo 实例。
 * @param addrs 地址数组。
 * @param count 地址数量。
 */
void XHostInfo_setAddresses(XHostInfo* info, const XHostAddress* addrs, int count);

/**
 * @brief 获取查询错误状态。
 * @param info XHostInfo 实例。
 * @return 错误码。
 */
XHostInfo_Error XHostInfo_error(const XHostInfo* info);

/**
 * @brief 设置错误状态。
 * @param info XHostInfo 实例。
 * @param error 错误码。
 */
void XHostInfo_setError(XHostInfo* info, XHostInfo_Error error);

/**
 * @brief 获取人类可读的错误描述。
 * @param info XHostInfo 实例。
 * @return 错误字符串（若无则返回空字符串）。
 */
const char* XHostInfo_errorString(const XHostInfo* info);

/**
 * @brief 设置错误描述。
 * @param info XHostInfo 实例。
 * @param str 错误描述（会被复制）。
 */
void XHostInfo_setErrorString(XHostInfo* info, const char* str);

/**
 * @brief 获取本次异步查询的唯一 ID。
 * @param info XHostInfo 实例。
 * @return lookup ID（由 XHostInfo_lookupHost 返回）。
 */
int XHostInfo_lookupId(const XHostInfo* info);

/**
 * @brief 设置 lookup ID。
 * @param info XHostInfo 实例。
 * @param id ID 值。
 */
void XHostInfo_setLookupId(XHostInfo* info, int id);

// ==================== 静态工具函数 ====================

/**
 * @brief 同步执行 DNS 主机名查询。
 *
 * 此函数会阻塞调用线程直到查询完成或失败。
 *
 * @param name 要解析的主机名（如 "www.example.com"）。
 * @return 包含查询结果的 XHostInfo 实例。
 */
XHostInfo* XHostInfo_fromName(const char* name);

/**
 * @brief 获取本机的主机名。
 *
 * @return 动态分配的主机名字符串（需调用 XMemory_free 释放），失败返回 NULL。
 */
char* XHostInfo_localHostName(void);

/**
 * @brief 获取本机的域名（若系统支持）。
 *
 * @note 在大多数 POSIX 系统上，此函数可能无法可靠获取域名。
 * @return 动态分配的域名字符串（需调用 XMemory_free 释放），失败或不支持返回 NULL。
 */
char* XHostInfo_localDomainName(void);

// ==================== 异步查询接口 ====================

/**
 * @brief 异步 DNS 查询完成回调函数类型。
 *
 * 当 DNS 查询完成后，此回调将在**主线程的事件循环中被调用**。
 *
 * @param result 查询结果（所有权转移给回调函数，需手动 delete）。
 * @param userData 用户自定义数据（透传自 XHostInfo_lookupHost）。
 */
typedef void (*XHostInfo_Callback)(XHostInfo* result, void* userData);

/**
 * @brief 异步执行 DNS 主机名查询（通用回调版本）。
 *
 * 查询在后台线程执行，完成后通过你的事件系统将结果投递到主线程，
 * 并调用指定的回调函数。
 *
 * @param name 要解析的主机名。
 * @param callback 查询完成后的回调函数。
 * @param userData 用户数据（透传给回调）。
 * @return 唯一的 lookup ID（可用于 XHostInfo_abortHostLookup 中止查询）。
 */
int XHostInfo_lookupHost(const char* name, XHostInfo_Callback callback, void* userData);

/**
 * @brief 异步执行 DNS 主机名查询（QObject 信号槽版本）。
 *
 * 完全模拟 Qt 的 QHostInfo::lookupHost 接口。
 * 查询完成后，会向指定的 receiver 对象发射一个信号。
 *
 * @param name 要解析的主机名。
 * @param receiver 接收信号的对象（必须是 XObject 子类）。
 * @param member 信号签名（如 "hostResolved(XHostInfo*)"）。
 * @return 唯一的 lookup ID。
 */
int XHostInfo_lookupHost_toObject(const char* name, XObject* receiver, size_t member);

/**
 * @brief 中止正在进行的异步 DNS 查询。
 *
 * @param lookupId 由 XHostInfo_lookupHost 返回的 ID。
 */
void XHostInfo_abortHostLookup(int lookupId);

#ifdef __cplusplus
}
#endif
#endif // XHOSTINFO_H