// XSocketDescriptor.h
#ifndef XSOCKETDESCRIPTOR_H
#define XSOCKETDESCRIPTOR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 跨平台套接字描述符（值语义，不透明结构体）
 *
 * - POSIX: 存储 int fd
 * - Windows: 存储 HANDLE (as intptr_t)
 * - 无效值: -1（Windows 还需排除 0）
 */
typedef struct XSocketDescriptor {
    intptr_t value;
} XSocketDescriptor;

/**
 * @brief 返回无效描述符（value = -1）
 */
XSocketDescriptor XSocketDescriptor_Invalid(void);

/**
 * @brief 判断是否有效
 */
bool XSocketDescriptor_isValid(XSocketDescriptor sd);

/**
 * @brief 从整数创建描述符（兼容 Qt 的 qintptr）
 */
XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value);

int32_t XSocketDescriptor_compare(const XSocketDescriptor* str1, const XSocketDescriptor* str2);
/**
 * @brief 转换回整数（用于日志、调试，禁止用于逻辑判断）
 */
intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd);
#endif // XSOCKETDESCRIPTOR_H