/**
 * @file XSharedData.h
 * @brief 隐式共享（Copy-On-Write）数据块
 * @details 包含原子引用计数和数据指针，支持 COW 机制的容器通过此结构体管理共享数据。
 *          可配合 XContainer 使用，也可独立用于其他需要 COW 的场景。
 */
#if !defined(XSharedData_H)
#define XSharedData_H

#include "XAtomic.h"
#include "XMemory.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 隐式共享数据块结构体
 */
typedef struct XSharedData {
    XAtomic_int32_t refCount;  ///< 原子引用计数（初始为1）
    XALIGNAS(XALIGN_PTR_SIZE) char data[];
} XSharedData;

/**
 * @brief 创建并初始化 XSharedData 块
 * @param dataPtr 数据区指针
 * @return XSharedData* 失败返回 NULL
 */
XSharedData* XSharedData_create(void* dataPtr, size_t  dataSize);

/**
 * @brief 增加引用计数
 */
void XSharedData_addRef(XSharedData* sd);

/**
 * @brief 减少引用计数，减到0则释放 XSharedData 块（不释放 data 指向的资源）
 * @return true 已释放，false 还有引用
 */
bool XSharedData_release(XSharedData* sd);

/**
 * @brief 减少引用计数，减到0时调用 dataDeleter 释放 data 并释放 XSharedData 块
 * @param sd XSharedData 指针
 * @param dataDeleter data 释放回调（可为 NULL，此时不释放 data）
 * @return true 已释放（XSharedData 已不可用），false 还有引用
 */
bool XSharedData_release_with(XSharedData* sd, void (*dataDeleter)(void* data,void*arg),void* arg);

/**
 * @brief 判断数据是否被共享（引用计数 > 1）
 */
bool XSharedData_isShared(const XSharedData* sd);

/**
 * @brief 获取引用计数
 */
int32_t XSharedData_refCount(const XSharedData* sd);

#ifdef __cplusplus
}
#endif

#endif // XSharedData_H
