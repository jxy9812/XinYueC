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
#include "XGeometry.h"
#include "XTypes.h"
#include "XString.h"

typedef struct XIODevice XIODevice;

/* XPicture owns a small, portable command stream.  XPainter deliberately
 * contains callbacks only; it has no dependency on a windowing toolkit. */
typedef struct XImage XImage;
typedef struct XPainter XPainter;

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
XPicture* XPicture_create_ex(XMemoryType memory);

/** @brief 由已有记录创建深拷贝对象。 */
XPicture* XPicture_create_copy(const XPicture* other, XMemoryType memory);

/** @brief 创建对象并转移已有记录的所有权。 */
XPicture* XPicture_create_move(XPicture* other, XMemoryType memory);

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

/**
 * @brief      移动构造函数
 * @param self 目标 XPicture 对象指针
 * @param other 源 XPicture 对象指针（移动后源对象变为空）
 */

/**
 * @brief      释放 XPicture 资源
 * @param self 待释放的 XPicture 对象指针
 */
/** @brief 交换两个记录对象的数据所有权。 */
void XPicture_swap(XPicture* self, XPicture* other);

/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
/**
 * @brief 通过 XClass 虚表复制绘图记录。
 * @param self 目标绘图记录对象指针。
 * @param other 源绘图记录对象指针。
 */
#define XPicture_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/**
 * @brief 通过 XClass 虚表移动绘图记录。
 * @param self 目标绘图记录对象指针。
 * @param other 源绘图记录对象指针；移动后源对象为空。
 */
#define XPicture_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放绘图记录。 @param self 待释放的绘图记录指针。 */
#define XPicture_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
/** @brief 删除堆上的绘图记录对象。 @param self 待删除的绘图记录指针。 */
#define XPicture_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 查询方法 ========== */

/**
 * @brief      判断绘图记录是否为 null
 * @param self 目标 XPicture 对象指针
 * @return 为 null 返回 true
 */
bool XPicture_isNull(const XPicture* self);

/** @brief 返回绘图设备类型（对标 QPicture::devType）。 */
int XPicture_devType(const XPicture* self);

/** @brief 返回底层绘图引擎；纯 C 记录无独立引擎时返回 NULL。 */
void* XPicture_paintEngine(const XPicture* self);

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
 * @param self    目标 XPicture 对象指针；回放期间只读
 * @param painter XPainter 指针；NULL 仅对空记录有效
 * @return 回放成功返回 true
 */
bool XPicture_play(const XPicture* self, XPainter* painter);

/* Record commands into the portable stream.  All functions return false on
 * overflow, allocation failure, malformed existing data, or invalid input. */
/**
 * @brief 记录绘制直线命令。
 * @param self 目标图片对象指针。
 * @param x1 起点 X 坐标。
 * @param y1 起点 Y 坐标。
 * @param x2 终点 X 坐标。
 * @param y2 终点 Y 坐标。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawLine(XPicture* self, int x1, int y1, int x2, int y2);
/**
 * @brief 记录填充矩形命令。
 * @param self 目标图片对象指针。
 * @param rect 要填充的矩形。
 * @param color 填充颜色，使用 ARGB32 表示。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordFillRect(XPicture* self, const XRect* rect, uint32_t color);
/**
 * @brief 记录绘制图像命令。
 * @param self 目标图片对象指针。
 * @param image 待绘制的源图像。
 * @param x 目标左上角 X 坐标。
 * @param y 目标左上角 Y 坐标。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawImage(XPicture* self, const XImage* image, int x, int y);
/**
 * @brief 记录保存绘制状态命令。
 * @param self 目标图片对象指针。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSave(XPicture* self);
/**
 * @brief 记录恢复绘制状态命令。
 * @param self 目标图片对象指针。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordRestore(XPicture* self);
/**
 * @brief 清除图片中的全部绘制命令。
 * @param self 目标图片对象指针。
 */
void XPicture_clearCommands(XPicture* self);
/**
 * @brief 检查图片数据是否为当前命令流格式。
 * @param self 图片对象指针。
 * @return 数据有效返回 true，否则返回 false。
 */
bool XPicture_isValidStream(const XPicture* self);

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载绘图记录
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 加载成功返回 true
 */
bool XPicture_load(XPicture* self, const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名加载图片的兼容重载。
 * @param self 目标图片对象指针。
 * @param fileName UTF-8 编码的文件名。
 * @return 加载成功返回 true，失败返回 false。
 */
bool XPicture_load_2(XPicture* self, const char* fileName);

/**
 * @brief 从 XIODevice 读取绘图记录。
 * @param self 目标图片对象指针。
 * @param device 输入设备指针，由调用方持有。
 * @return 读取成功返回 true，否则返回 false。
 */
bool XPicture_load_device(XPicture* self, XIODevice* device);

/**
 * @brief      保存绘图记录到文件
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 保存成功返回 true
 */
bool XPicture_save(const XPicture* self, const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名保存图片的兼容重载。
 * @param self 源图片对象指针。
 * @param fileName UTF-8 编码的目标文件名。
 * @return 保存成功返回 true，失败返回 false。
 */
bool XPicture_save_2(const XPicture* self, const char* fileName);

/**
 * @brief 将绘图记录写入 XIODevice。
 * @param self 源图片对象指针。
 * @param device 输出设备指针，由调用方持有。
 * @return 写入成功返回 true，否则返回 false。
 */
bool XPicture_save_device(const XPicture* self, XIODevice* device);

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

/* XClass create API default-memory wrappers. */
#undef XPicture_create
#define XPicture_create() XPicture_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#define XPicture_create_copy_default(other) XPicture_create_copy((other), XCLASS_DEFAULT_MEMORY_TYPE)
#define XPicture_create_move_default(other) XPicture_create_move((other), XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XPICTURE_H */
