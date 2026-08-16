/**
 * @file       XDeviceFile.h
 * @brief      文件设备（XDeviceFile）—— XDevice 的统一文件设备，类别名为 "file"。
 * @details    XDeviceFile 继承 XDevice，文件平台实现在各平台 Drive 目录中；
 *             调用方不需要直接使用 XFileSystem_open/read/...，而是统一通过
 *             XDevice_open(XDeviceType_File, ...) 或
 *             XDevice_openClass("file", ...) 拿到 XFd 后操作。
 *             目前文件设备仍可复用 XFileSystem 抽象层，后续将替换为各平台实现。
 */
#ifndef XDEVICEFILE_H
#define XDEVICEFILE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XDevice.h"

/* 前向声明：文件打开选项的路径使用 XString。 */
struct XString;
typedef struct XString XString;

/* ============================================================================
 * 文件设备打开选项
 * ============================================================================ */
/**
 * @brief 文件设备的打开选项（扩展 XDeviceOpenOptions）。
 * @details 第一个成员 m_base 是 XDeviceOpenOptions 基础选项；Open 虚函数把本结构体
 *          向上转型为 const XDeviceOpenOptions* 读取基础字段，再按需向下转型取路径等
 *          文件专有字段。打开选项只在该次 Open 调用期间有效。
 */
typedef struct XDeviceFileOpenOptions
{
    XDeviceOpenOptions m_base;      /**< 第一个成员，基础打开选项。 */
    const XString* m_path;          /**< 文件路径；借用指针，不能为 NULL。 */
    uint64_t m_bufferSize;          /**< 请求的读写缓冲字节数；0 表示设备默认。 */
} XDeviceFileOpenOptions;

/* ============================================================================
 * 类虚函数表定义
 * ============================================================================ */
/** @brief XDeviceFile 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceFile)
XCLASS_DEFINE_EXTEND_END(XDeviceFile, XDevice)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */
/**
 * @brief 文件设备类。
 * @details 基类 XDevice 必须作为第一个成员。文件实际句柄保存在每个打开操作的
 *          内部上下文（XDeviceFileCtx）中，不放在本类对象上，因此本类对象可
 *          作为共享注册单例，同时支持多路并发打开。
 */
typedef struct XDeviceFile
{
    XDevice m_base; /**< 第一个成员，基类 XDevice，由 XClass 管理，禁止手工修改。 */
} XDeviceFile;

/* ============================================================================
 * 类生命周期
 * ============================================================================ */
/**
 * @brief 初始化 XDeviceFile 类的共享虚函数表。
 * @return 指向共享虚函数表的指针；失败返回 NULL。
 * @note   由 XClass 虚函数表机制调用，通常不需要外部直接调用。
 */
XVtable* XDeviceFile_class_init(void);

/**
 * @brief 初始化一个已分配的 XDeviceFile 设备对象。
 * @param self 待初始化的文件设备对象；不能为 NULL，需保证至少 sizeof(XDeviceFile) 字节。
 * @note   初始化后 m_capabilities 为 Read|Write|Seek|Flush|Resize。
 */
void XDeviceFile_init(XDeviceFile* self);

/**
 * @brief 在堆上创建文件设备对象。
 * @return 新创建的文件设备对象；失败返回 NULL。
 * @note   该方法主要供单元测试与动态创建使用；对外注册使用 XDeviceFile_register。
 */
XDeviceFile* XDeviceFile_create(void);

/* ============================================================================
 * 注册
 * ============================================================================ */
/**
 * @brief 把内置文件设备注册为类别名 "file"。
 * @details 注册后即可通过 XDevice_open(XDeviceType_File, ...) 或
 *          XDevice_openClass("file", ...) 打开文件设备。
 * @return 注册成功返回 true；函数幂等，重复调用也返回 true。
 * @note   内部使用静态单例，不产生堆分配；重复调用不会重复注册。
 */
bool XDeviceFile_register(void);

#ifdef __cplusplus
}
#endif
#endif /* XDEVICEFILE_H */
