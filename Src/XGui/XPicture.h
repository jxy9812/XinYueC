/******************************************************************************
 * @file       XPicture.h
 * @brief      XPicture 绘图指令记录与回放类（对标 Qt 6.8 QPicture）
 * @author     XinYueC 团队
 * @note       提供绘图指令的录制和回放功能，支持矢量图形序列化
 ******************************************************************************/
#ifndef XPICTURE_H
#define XPICTURE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XData/XGeometry.h"
#include "XTypes.h"

/* XPicture owns a small, portable command stream.  XPainter deliberately
 * contains callbacks only; it has no dependency on a windowing toolkit. */
typedef struct XImage XImage;
typedef struct XPainter XPainter;

typedef bool (*XPainter_drawLine)(XPainter* painter, int x1, int y1,
                                  int x2, int y2);
typedef bool (*XPainter_fillRect)(XPainter* painter, const XRect* rect,
                                  uint32_t color);
typedef bool (*XPainter_drawImage)(XPainter* painter, const XImage* image,
                                   int x, int y);
typedef bool (*XPainter_save)(XPainter* painter);
typedef bool (*XPainter_restore)(XPainter* painter);

struct XPainter
{
    void* userData;
    XPainter_drawLine drawLine;
    XPainter_fillRect fillRect;
    XPainter_drawImage drawImage;
    XPainter_save save;
    XPainter_restore restore;
};

void XPainter_init(XPainter* self, void* userData);

/* Stream constants are part of the XGui C ABI.  The byte stream is always
 * little-endian, regardless of the host architecture. */
#define XPICTURE_STREAM_VERSION 1u
#define XPICTURE_HEADER_SIZE 40u
#define XPICTURE_RECORD_HEADER_SIZE 8u
#define XPICTURE_MAGIC_SIZE 8u

typedef enum XPictureOpcode
{
    XPictureOpcode_DrawLine = 1,
    XPictureOpcode_FillRect = 2,
    XPictureOpcode_DrawImage = 3,
    XPictureOpcode_Save = 4,
    XPictureOpcode_Restore = 5
} XPictureOpcode;

/* ========== XPicture 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XPicture)
XCLASS_DEFINE_EXTEND_END(XPicture, XClass)

/* 前向声明 */
typedef struct XPicturePrivate XPicturePrivate;

/**
 * @brief      XPicture 绘图记录类结构体（对标 Qt 6.8 QPicture）
 * @note       继承自 XClass，记录 QPainter 绘图指令并支持回放和序列化
 */
typedef struct XPicture
{
    XClass              m_class;   /**< 继承的基类成员 */
    XPicturePrivate*     m_data;    /**< 私有数据指针 */
}XPicture;

/**
 * @brief      初始化 XPicture 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XPicture_class_init();

/**
 * @brief      在堆上创建 XPicture 实例
 * @return     指向新创建的 XPicture 对象的指针，失败返回 NULL
 */
XPicture* XPicture_create();

/**
 * @brief      初始化 XPicture 实例
 * @param self          待初始化的 XPicture 对象指针
 * @param formatVersion 格式版本号（-1 表示使用默认版本）
 */
void XPicture_init(XPicture* self, int formatVersion);

/**
 * @brief      复制构造函数
 * @param self 目标 XPicture 对象指针
 * @param other 源 XPicture 对象指针
 */
void XPicture_copy(XPicture* self, const XPicture* other);

/**
 * @brief      移动构造函数
 * @param self 目标 XPicture 对象指针
 * @param other 源 XPicture 对象指针（移动后源对象变为空）
 */
void XPicture_move(XPicture* self, XPicture* other);

/**
 * @brief      释放 XPicture 资源
 * @param self 待释放的 XPicture 对象指针
 */
void XPicture_deinit(XPicture* self);

/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
void XPicture_copy_base(XPicture* dest, const XPicture* src);

/**
 * @brief      虚函数调度：移动
 * @param dest 目标对象指针
 * @param src  源对象指针（移动后源对象变为空）
 */
void XPicture_move_base(XPicture* dest, XPicture* src);

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
void XPicture_deinit_base(XPicture* self);

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
void XPicture_delete_base(XPicture* self);

/* ========== 查询方法 ========== */

/**
 * @brief      判断绘图记录是否为 null
 * @param self 目标 XPicture 对象指针
 * @return 为 null 返回 true
 */
bool XPicture_isNull(const XPicture* self);

/**
 * @brief      获取绘图记录数据大小
 * @param self 目标 XPicture 对象指针
 * @return 数据大小（字节）
 */
uint32_t XPicture_size(const XPicture* self);

/**
 * @brief      获取绘图记录数据指针
 * @param self 目标 XPicture 对象指针
 * @return 数据指针
 */
const char* XPicture_data(const XPicture* self);

/**
 * @brief      设置绘图记录数据
 * @param self 目标 XPicture 对象指针
 * @param data 数据指针
 * @param size 数据大小
 */
void XPicture_setData(XPicture* self, const char* data, uint32_t size);

/**
 * @brief      获取边界矩形
 * @param self 目标 XPicture 对象指针
 * @param out  输出矩形结构体指针
 */
void XPicture_boundingRect(const XPicture* self, XRect* out);

/**
 * @brief      设置边界矩形
 * @param self 目标 XPicture 对象指针
 * @param rect 矩形结构体指针
 */
void XPicture_setBoundingRect(XPicture* self, const XRect* rect);

/* ========== 操作方法 ========== */

/**
 * @brief      回放绘图指令到指定的纯 C XPainter
 * @param self    目标 XPicture 对象指针
 * @param painter XPainter 指针；NULL 仅对空记录有效
 * @return 回放成功返回 true
 */
bool XPicture_play(XPicture* self, XPainter* painter);

/* Record commands into the portable stream.  All functions return false on
 * overflow, allocation failure, malformed existing data, or invalid input. */
bool XPicture_recordDrawLine(XPicture* self, int x1, int y1, int x2, int y2);
bool XPicture_recordFillRect(XPicture* self, const XRect* rect, uint32_t color);
bool XPicture_recordDrawImage(XPicture* self, const XImage* image, int x, int y);
bool XPicture_recordSave(XPicture* self);
bool XPicture_recordRestore(XPicture* self);
void XPicture_clearCommands(XPicture* self);
bool XPicture_isValidStream(const XPicture* self);

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载绘图记录
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 加载成功返回 true
 */
bool XPicture_load(XPicture* self, const char* fileName);

/**
 * @brief      保存绘图记录到文件
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 保存成功返回 true
 */
bool XPicture_save(const XPicture* self, const char* fileName);

/* ========== 数据分离 ========== */

/**
 * @brief      分离数据（写时复制）
 * @param self 目标 XPicture 对象指针
 */
void XPicture_detach(XPicture* self);

/**
 * @brief      判断数据是否已分离
 * @param self 目标 XPicture 对象指针
 * @return 已分离返回 true
 */
bool XPicture_isDetached(const XPicture* self);

#ifdef __cplusplus
}
#endif
#endif /* XPICTURE_H */

