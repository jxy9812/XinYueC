/******************************************************************************
 * @file       XPicture.h
 * @brief      XPicture 绘图指令记录与回放类（对标 Qt 6.8 QPicture）
 * @details    实现只依赖 XinYueC 抽象层，不直接调用 Win32、POSIX、Qt
 *             或其他平台 API；指令流为固定小端序，可跨主机架构序列化。
 * @author     XinYueC 团队
 * @note       提供绘图指令的录制和回放功能，支持矢量图形序列化
 ******************************************************************************/
#ifndef XPICTURE_H
#define XPICTURE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XGeometry.h"
#include "XTypes.h"
#include "XString.h"
#include "XPainter_config.h"

#if XPAINTDEVICE_ON
typedef struct XPaintDevice XPaintDevice; /* 前向声明。 */
#endif

typedef struct XIODevice XIODevice;

/* XPicture 持有紧凑的便携命令流；XPainter 仅保存回调，不依赖窗口工具包。 */
typedef struct XImage XImage;
typedef struct XPainter XPainter;
typedef struct XImageTransform XImageTransform;
typedef struct XFont XFont;
typedef struct XPainterGradient XPainterGradient;
struct XPainterPath;

/* 流常量属于 XGui C ABI；无论主机架构如何，字节流始终采用小端序。 */
#define XPICTURE_STREAM_VERSION 1u
#define XPICTURE_HEADER_SIZE 40u
#define XPICTURE_RECORD_HEADER_SIZE 8u
#define XPICTURE_MAGIC_SIZE 8u

typedef enum XPictureOpcode
{
    XPictureOpcode_DrawLine = 1, /**< 直线段绘制（两浮点端点，16 字节载荷）。 */
    XPictureOpcode_FillRect = 2, /**< 矩形填充（矩形+ARGB32 颜色，20 字节载荷）。 */
    XPictureOpcode_DrawImage = 3, /**< 图像绘制（目标矩形与源区域载荷）。 */
    XPictureOpcode_Save = 4, /**< 保存绘制器状态（无载荷）。 */
    XPictureOpcode_Restore = 5, /**< 恢复绘制器状态（无载荷）。 */
    XPictureOpcode_DrawShape = 6, /**< 形状绘制（椭圆/圆弧/扇形/弦/圆角矩形）。 */
    XPictureOpcode_DrawPolyline = 7, /**< 折线绘制（顶点序列载荷）。 */
    XPictureOpcode_DrawPolygon = 8, /**< 多边形绘制（闭合顶点序列载荷）。 */
    XPictureOpcode_DrawPoints = 9, /**< 点集绘制（点序列载荷）。 */
    XPictureOpcode_DrawPath = 10, /**< 路径绘制（填充/描边由载荷标志控制）。 */
    XPictureOpcode_SetPen = 11, /**< 完整画笔状态（对标 QPicture PdcSetPen）。 */
    XPictureOpcode_SetOpacity = 12, /**< 绘制器不透明度（对标 PdcSetOpacity）。 */
    XPictureOpcode_SetCompositionMode = 13, /**< 合成模式（对标 PdcSetCompositionMode）。 */
    XPictureOpcode_SetRenderHints = 14, /**< 渲染提示位（对标 PdcSetRenderHint）。 */
    XPictureOpcode_SetBrushOrigin = 15, /**< 画刷原点（对标 PdcSetBrushOrigin）。 */
    XPictureOpcode_SetTransform = 16, /**< 世界变换矩阵（对标 PdcSetWMatrix）。 */
    XPictureOpcode_SetBackgroundColor = 17, /**< 背景颜色（对标 PdcSetBkColor）。 */
    XPictureOpcode_SetBackgroundMode = 18, /**< 背景模式（对标 PdcSetBkMode）。 */
    XPictureOpcode_SetBrush = 19, /**< 基础画刷样式与颜色（对标 PdcSetBrush）。 */
    XPictureOpcode_SetWindow = 20, /**< 逻辑窗口矩形（对标 QPainter::setWindow）。 */
    XPictureOpcode_SetViewport = 21, /**< 设备视口矩形（对标 QPainter::setViewport）。 */
    XPictureOpcode_SetViewTransformEnabled = 22, /**< 视图变换启用状态。 */
    XPictureOpcode_SetClipEnabled = 23, /**< 裁剪启用状态（对标 PdcSetClipEnabled）。 */
    XPictureOpcode_SetClipRect = 24, /**< 矩形裁剪（对标 PdcSetClipRegion 的矩形子集）。 */
    XPictureOpcode_SetClipRegion = 25, /**< 区域裁剪（对标 PdcSetClipRegion）。 */
    XPictureOpcode_DrawTiledPixmap = 26, /**< 平铺像素图（对标 PdcDrawTiledPixmap）。 */
    XPictureOpcode_DrawPixmap = 27, /**< 矩形像素图（对标 PdcDrawPixmap）。 */
    XPictureOpcode_SetFont = 28, /**< 字体状态（对标 PdcSetFont）。 */
    XPictureOpcode_DrawPoint = 29, /**< 单点绘制（对标 PdcDrawPoint）。 */
    XPictureOpcode_DrawText = 30, /**< 文本绘制（对标 PdcDrawTextItem）。 */
    XPictureOpcode_SetBrushGradient = 31 /**< 渐变画刷（对标 QBrush/QGradient）。 */
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
    XClass              m_class;   /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XPicturePrivate*     m_data;    /**< 私有数据指针，由记录对象拥有，仅供实现使用。 */
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

/** @brief 交换两个记录对象的数据所有权。 */
void XPicture_swap(XPicture* self, XPicture* other);

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

/* 命令记录协议（record* 家族、clearCommands、isValidStream）属 Qt 私有面
   对位（QAbstractPicture / QPicturePrivate），已按「可见性下沉」迁到
   XPicture_Protected.h；调用方须显式 include 该保护面头文件。 */

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载绘图记录
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 加载成功返回 true；文件无法打开时返回 false 并将图片置为空；文件已打开
 *         但流格式无效时返回 false，同时保留已读的非空数据供 size()/data() 查询。
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
 * @return 读取成功返回 true，否则返回 false。与 Qt 6.8 一致，设备读取到的非空
 *         畸形流会保留在对象中，仅通过返回值和 XPicture_isValidStream() 报告失败；
 *         空流或读取失败会得到空图片。
 */
bool XPicture_load_3(XPicture* self, XIODevice* device);

/**
 * @brief      保存绘图记录到文件
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 文件名有效且设备成功打开时返回 true；Qt 6.8 不检查后续设备写入结果，
 *         因此短写或写入错误不会改变返回值。
 */
bool XPicture_save(const XPicture* self, const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名保存图片的兼容重载。
 * @param self 源图片对象指针。
 * @param fileName UTF-8 编码的目标文件名。
 * @return 文件名有效且设备成功打开时返回 true；设备写入短写或错误不会改变返回值，
 *         参数无效或打开失败时返回 false。
 */
bool XPicture_save_2(const XPicture* self, const char* fileName);

/**
 * @brief 将绘图记录写入设备。
 * @param self 目标绘图记录对象指针。
 * @param device 接收字节流的设备指针。
 * @return 记录和设备指针有效时返回 true；Qt 6.8 不检查设备 write 的短写或错误返回，
 *         本接口同样不检查，也不会额外调用 flush；参数无效时返回 false。
 */
bool XPicture_save_3(const XPicture* self, XIODevice* device);

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

#if XPAINTDEVICE_ON
/**
 * @brief      返回绘制设备描述（XPaintDevice；边界矩形即设备尺寸）。
 * @param      self 目标图片指针。
 * @return     绘制设备借用指针；无效返回 NULL。
 */
XPaintDevice* XPicture_paintDevice(XPicture* self);
#endif /* XPAINTDEVICE_ON */

#ifdef __cplusplus
}
#endif

/* XClass 创建 API 的默认内存类型封装。 */
#undef XPicture_create
#define XPicture_create() XPicture_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#define XPicture_create_copy_default(other) XPicture_create_copy((other), XCLASS_DEFAULT_MEMORY_TYPE)
#define XPicture_create_move_default(other) XPicture_create_move((other), XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XGUI_ON */

#endif /* XPICTURE_H */
