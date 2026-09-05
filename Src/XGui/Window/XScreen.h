/******************************************************************************
 * @file       XScreen.h
 * @brief      XScreen 屏幕信息类（对标 Qt 6.8 QScreen）。
 * @details    XScreen 继承 XObject，提供单台显示屏幕的完整信息面：名称、
 *             厂商、型号、序列号、颜色深度、几何/可用几何、物理尺寸、
 *             物理/逻辑 DPI、设备像素比、方向、刷新率、虚拟桌面（兄弟
 *             屏幕）查询，以及抓屏接口。9 个通知信号与 Qt QScreen 一一
 *             对应；本模块不依赖任何平台 API，所有属性均可程序化配置，
 *             未来由 XGuiApplication 自动填充。
 * @note       模块总开关 XSCREEN_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XScreen 公共 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSCREEN_H
#define XSCREEN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XString.h"
#include "XVector.h"
#include "XTypes.h"
#include "XMemory.h"

#if XSCREEN_ON

/** @brief XScreen 继承 XObject，不新增虚函数槽位。 */
XCLASS_DEFINE_BEGING(XScreen)
XCLASS_DEFINE_EXTEND_END(XScreen, XObject)

/**
 * @brief      屏幕方向枚举（对标 Qt 6.8 Qt::ScreenOrientation）。
 * @details    枚举值 1/2/4/8 与 Qt 完全一致，可据此按 2 的幂做旋转角度
 *             换算：angleBetween 使用 log2 差值查表得到 0/90/180/270。
 *             这些值不是位标志，不能按位组合。
 */
typedef enum XScreenOrientation
{
    XScreenOrientation_Primary = 0,          /**< 主方向占位值；查询接口把它解析为屏幕的 primaryOrientation()。 */
    XScreenOrientation_Portrait = 1,         /**< 竖屏方向（短边水平、长边竖直）。 */
    XScreenOrientation_Landscape = 2,        /**< 横屏方向（长边水平）。 */
    XScreenOrientation_InvertedPortrait = 4, /**< 倒置竖屏方向。 */
    XScreenOrientation_InvertedLandscape = 8 /**< 倒置横屏方向。 */
} XScreenOrientation;

/**
 * @brief      平台屏幕句柄（对标 Qt 6.8 QScreen::handle() 返回的
 *             QPlatformScreen*）。
 * @details    当前保持不透明：默认屏幕全部由程序化属性驱动，句柄为 NULL。
 *             未来 XGuiApplication/平台后端可填充具体实例并提供真抓屏、
 *             原生方向等平台能力；XScreen 只保存指针，不拥有、不释放它。
 */
typedef struct XScreenPlatform XScreenPlatform;

/**
 * @brief      窗口系统窗口/入口标识（对标 Qt 6.8 的 WId，即 quintptr）。
 * @details    用于 grabWindow() 指定要抓取内容的窗口；0 表示抓取整个屏幕。
 */
#ifndef XWINDOWID_DEFINED
#define XWINDOWID_DEFINED 1
typedef uintptr_t XWindowId;
#endif

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XScreenPrivate XScreenPrivate;

/**
 * @brief      XScreen 屏幕对象；m_class 必须是第一个成员且禁止手工修改。
 * @details    屏幕属性快照保存在 m_data 中；m_data 由 XScreen 拥有，
 *             调用方不得直接访问或释放。
 */
typedef struct XScreen
{
    XObject        m_class; /**< 第一个成员，由 XObject 管理，禁止手工修改。 */
    XScreenPrivate* m_data; /**< 私有属性快照，由 XScreen 拥有，仅供实现访问。 */
} XScreen;

/**
 * @brief      初始化 XScreen 类虚函数表并返回共享表指针。
 * @return     XScreen 类的共享 XVtable 指针。
 */
XVtable* XScreen_class_init(void);

/**
 * @brief      初始化空 XScreen（对标 QScreen 的默认构造语义）。
 * @details    初始属性：几何/可用几何为 (0,0,0,0)（可用几何未显式设置时
 *             跟随几何变化，与 QPlatformScreen::availableGeometry 缺省
 *             返回 geometry 一致）、颜色深度 32、物理尺寸 (0,0)、物理 DPI 0、
 *             逻辑 DPI 96、设备像素比 1.0、方向为 Primary、主方向按几何
 *             宽高推导（0x0 视为横屏）、刷新率 60、平台句柄 NULL。
 * @param      self 待初始化的对象指针；生命周期结束时必须成对调用
 *             XScreen_deinit_base。
 */
void XScreen_init(XScreen* self);

/**
 * @brief      使用默认内存类型在堆上创建 XScreen。
 * @return     新对象指针；失败返回 NULL，调用方用 XScreen_delete_base 释放。
 */
#define XScreen_create() XScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建 XScreen。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL，调用方用 XScreen_delete_base 释放。
 */
XScreen* XScreen_create_ex(XMemoryType memory);

/**
 * @brief      拷贝创建：深拷贝源对象属性快照与显式兄弟列表。
 * @param      other 源对象；可为 NULL。
 * @return     新对象指针；失败返回 NULL，调用方用 XScreen_delete_base 释放。
 * @note       信号连接、父子关系、注册表归属不随拷贝转移。
 */
XScreen* XScreen_create_copy(const XScreen* other);

/**
 * @brief      移动创建：转移源对象私有数据所有权。
 * @param      other 源对象；移动后其 m_data 置空，仍需 deinit_base。
 * @return     新对象指针；失败返回 NULL，调用方用 XScreen_delete_base 释放。
 */
XScreen* XScreen_create_move(XScreen* other);

/** @brief 通过 XClass 虚表释放 XScreen 资源（栈/外部存储对象使用）。 */
#define XScreen_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XScreen 对象。 */
#define XScreen_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 屏幕注册表（未来由 XGuiApplication 接管） ==================== */

/**
 * @brief      把屏幕注册到进程内屏幕注册表（对标 QGuiApplication 的
 *             allScreens() 语义）。
 * @details    注册表由 XScreen.c 内部静态维护，不持有屏幕所有权；重复
 *             注册同一对象是 no-op。屏幕销毁（deinit_base）时自动退表。
 * @param      screen 目标屏幕；可为 NULL。
 */
void XScreen_register(XScreen* screen);

/**
 * @brief      从屏幕注册表移除屏幕。
 * @details    若被移除屏幕当前是主屏幕，主屏幕自动清空。未注册的屏幕
 *             调用本函数是 no-op。
 * @param      screen 目标屏幕；可为 NULL。
 */
void XScreen_unregister(XScreen* screen);

/**
 * @brief      返回当前主屏幕（对标 QGuiApplication::primaryScreen）。
 * @return     主屏幕借用指针；未设置时返回 NULL。
 */
XScreen* XScreen_primaryScreen(void);

/**
 * @brief      设置主屏幕（对标 QGuiApplication 内部主屏幕语义）。
 * @param      screen 目标屏幕；可为 NULL 以清空主屏幕。
 */
void XScreen_setPrimary(XScreen* screen);

/**
 * @brief      返回当前注册的所有屏幕（对标 QGuiApplication::screens）。
 * @return     新建的 XVector，元素为 XScreen* 借用指针，按注册顺序排列；
 *             调用方用 XVector_delete_base 释放。
 */
XVector* XScreen_screens(void);

/* ==================== 平台句柄 ==================== */

/**
 * @brief      返回平台屏幕句柄（对标 QScreen::handle()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     平台句柄借用指针；未配置时为 NULL。
 */
XScreenPlatform* XScreen_handle(const XScreen* self);

/**
 * @brief      设置平台屏幕句柄。
 * @details    XScreen 只保存借用指针，不拥有、不释放句柄；调用方保证
 *             句柄存活时间不短于屏幕对象。
 * @param      self 目标屏幕；可为 NULL。
 * @param      handle 平台句柄；可为 NULL 清除。
 */
void XScreen_setHandle(XScreen* self, XScreenPlatform* handle);

/* ==================== 标识属性（对标 QScreen 相应只读属性） ==================== */

/**
 * @brief      返回屏幕名称副本（对标 QScreen::name()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     新建 XString；无名称时为空字符串，调用方负责释放。
 */
XString* XScreen_name(const XScreen* self);
/**
 * @brief      返回屏幕名称内部只读引用。
 * @param      self 目标屏幕；可为 NULL。
 * @return     内部 XString 借用指针；无名称时返回 NULL，不得释放或修改。
 */
const XString* XScreen_name_const(const XScreen* self);
/**
 * @brief      返回屏幕名称 UTF-8 兼容指针。
 * @param      self 目标屏幕；可为 NULL。
 * @return     UTF-8 字符串指针，由内部转换缓存持有，不得释放。
 */
const char* XScreen_name_2(const XScreen* self);
/**
 * @brief      设置屏幕名称。
 * @param      self 目标屏幕；可为 NULL。
 * @param      name 名称；可为 NULL 清空。
 */
void XScreen_setName(XScreen* self, const XString* name);
/**
 * @brief      设置 UTF-8 屏幕名称的兼容重载。
 * @param      self 目标屏幕；可为 NULL。
 * @param      name UTF-8 编码的名称；可为 NULL 清空。
 */
void XScreen_setName_2(XScreen* self, const char* name);

/**
 * @brief      返回屏幕厂商副本（对标 QScreen::manufacturer()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     新建 XString；未设置时为空字符串，调用方负责释放。
 */
XString* XScreen_manufacturer(const XScreen* self);
/** @brief 返回厂商内部只读引用；未设置返回 NULL。 @param self 目标屏幕；可为 NULL。 */
const XString* XScreen_manufacturer_const(const XScreen* self);
/** @brief 返回厂商 UTF-8 兼容指针，不得释放。 @param self 目标屏幕；可为 NULL。 */
const char* XScreen_manufacturer_2(const XScreen* self);
/**
 * @brief      设置屏幕厂商。
 * @param      self 目标屏幕；可为 NULL。
 * @param      manufacturer 厂商名；可为 NULL 清空。
 */
void XScreen_setManufacturer(XScreen* self, const XString* manufacturer);
/**
 * @brief      设置 UTF-8 厂商名的兼容重载。
 * @param      self 目标屏幕；可为 NULL。
 * @param      manufacturer UTF-8 编码的厂商名；可为 NULL 清空。
 */
void XScreen_setManufacturer_2(XScreen* self, const char* manufacturer);

/**
 * @brief      返回屏幕型号副本（对标 QScreen::model()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     新建 XString；未设置时为空字符串，调用方负责释放。
 */
XString* XScreen_model(const XScreen* self);
/** @brief 返回型号内部只读引用；未设置返回 NULL。 @param self 目标屏幕；可为 NULL。 */
const XString* XScreen_model_const(const XScreen* self);
/** @brief 返回型号 UTF-8 兼容指针，不得释放。 @param self 目标屏幕；可为 NULL。 */
const char* XScreen_model_2(const XScreen* self);
/**
 * @brief      设置屏幕型号。
 * @param      self 目标屏幕；可为 NULL。
 * @param      model 型号名；可为 NULL 清空。
 */
void XScreen_setModel(XScreen* self, const XString* model);
/**
 * @brief      设置 UTF-8 型号名的兼容重载。
 * @param      self 目标屏幕；可为 NULL。
 * @param      model UTF-8 编码的型号名；可为 NULL 清空。
 */
void XScreen_setModel_2(XScreen* self, const char* model);

/**
 * @brief      返回屏幕序列号副本（对标 QScreen::serialNumber()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     新建 XString；未设置时为空字符串，调用方负责释放。
 */
XString* XScreen_serialNumber(const XScreen* self);
/** @brief 返回序列号内部只读引用；未设置返回 NULL。 @param self 目标屏幕；可为 NULL。 */
const XString* XScreen_serialNumber_const(const XScreen* self);
/** @brief 返回序列号 UTF-8 兼容指针，不得释放。 @param self 目标屏幕；可为 NULL。 */
const char* XScreen_serialNumber_2(const XScreen* self);
/**
 * @brief      设置屏幕序列号。
 * @param      self 目标屏幕；可为 NULL。
 * @param      serialNumber 序列号；可为 NULL 清空。
 */
void XScreen_setSerialNumber(XScreen* self, const XString* serialNumber);
/**
 * @brief      设置 UTF-8 序列号的兼容重载。
 * @param      self 目标屏幕；可为 NULL。
 * @param      serialNumber UTF-8 编码的序列号；可为 NULL 清空。
 */
void XScreen_setSerialNumber_2(XScreen* self, const char* serialNumber);

/**
 * @brief      返回屏幕颜色深度（对标 QScreen::depth()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     每像素位数；默认 32，未配置时为初始值。
 */
int XScreen_depth(const XScreen* self);
/**
 * @brief      设置屏幕颜色深度。
 * @param      self 目标屏幕；可为 NULL。
 * @param      depth 每像素位数。
 */
void XScreen_setDepth(XScreen* self, int depth);

/* ==================== 几何与尺寸 ==================== */

/**
 * @brief      返回屏幕几何（对标 QScreen::geometry()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     屏幕矩形；NULL 返回 (0,0,0,0)。
 */
XRect XScreen_geometry(const XScreen* self);
/**
 * @brief      设置屏幕几何（对标 QScreen 内部 geometry 更新语义）。
 * @details    值变化时发射 geometryChanged；物理 DPI 由几何与物理尺寸
 *             计算得出，若变化则同时发射 physicalDotsPerInchChanged；
 *             主方向未显式锁定且宽高比例改变时自动重新推导并发射
 *             primaryOrientationChanged；注册表内/兄弟集合内屏幕的
 *             虚拟几何变化会发射 virtualGeometryChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      geometry 新几何；可为 NULL 表示不操作。
 */
void XScreen_setGeometry(XScreen* self, const XRect* geometry);

/**
 * @brief      返回屏幕像素尺寸（对标 QScreen::size()，等于 geometry 的
 *             宽高）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     像素尺寸；NULL 返回 (0,0)。
 */
XSize XScreen_size(const XScreen* self);

/**
 * @brief      返回物理尺寸（对标 QScreen::physicalSize()，单位毫米）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     物理尺寸；NULL 返回 (0,0)。
 */
XSizeF XScreen_physicalSize(const XScreen* self);
/**
 * @brief      设置物理尺寸（毫米）。
 * @details    值变化时发射 physicalSizeChanged；若由此导致物理 DPI 变化，
 *             同时发射 physicalDotsPerInchChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      size 物理尺寸；可为 NULL 表示不操作。
 */
void XScreen_setPhysicalSize(XScreen* self, const XSizeF* size);

/**
 * @brief      返回水平物理 DPI（对标 QScreen::physicalDotsPerInchX()）。
 * @details    由几何宽 / 物理宽 x 25.4 计算；物理宽度非正时返回 0。
 * @param      self 目标屏幕；可为 NULL。
 * @return     水平物理 DPI（设备无关点/英寸）。
 */
float XScreen_physicalDotsPerInchX(const XScreen* self);
/**
 * @brief      返回垂直物理 DPI（对标 QScreen::physicalDotsPerInchY()）。
 * @details    由几何高 / 物理高 x 25.4 计算；物理高度非正时返回 0。
 * @param      self 目标屏幕；可为 NULL。
 * @return     垂直物理 DPI。
 */
float XScreen_physicalDotsPerInchY(const XScreen* self);
/**
 * @brief      返回平均物理 DPI（对标 QScreen::physicalDotsPerInch()）。
 * @details    Qt 约定取 X/Y 平均值。
 * @param      self 目标屏幕；可为 NULL。
 * @return     平均物理 DPI。
 */
float XScreen_physicalDotsPerInch(const XScreen* self);

/**
 * @brief      返回水平逻辑 DPI（对标 QScreen::logicalDotsPerInchX()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     水平逻辑 DPI；默认 96。
 */
float XScreen_logicalDotsPerInchX(const XScreen* self);
/**
 * @brief      返回垂直逻辑 DPI（对标 QScreen::logicalDotsPerInchY()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     垂直逻辑 DPI；默认 96。
 */
float XScreen_logicalDotsPerInchY(const XScreen* self);
/**
 * @brief      返回平均逻辑 DPI（对标 QScreen::logicalDotsPerInch()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     平均逻辑 DPI。
 */
float XScreen_logicalDotsPerInch(const XScreen* self);
/**
 * @brief      同时设置水平/垂直逻辑 DPI。
 * @details    任一值变化即发射 logicalDotsPerInchChanged（参数为平均值，
 *             与 Qt 信号参数一致）。
 * @param      self 目标屏幕；可为 NULL。
 * @param      x 水平逻辑 DPI。
 * @param      y 垂直逻辑 DPI。
 */
void XScreen_setLogicalDotsPerInch(XScreen* self, float x, float y);

/**
 * @brief      返回设备像素比（对标 QScreen::devicePixelRatio()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     设备像素比；默认 1.0。
 */
float XScreen_devicePixelRatio(const XScreen* self);
/**
 * @brief      设置设备像素比。
 * @details    Qt 中该属性 NOTIFY 为 physicalDotsPerInchChanged，本实现
 *             保持一致：值变化时发射 physicalDotsPerInchChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      ratio 设备像素比；建议 ≥1.0。
 */
void XScreen_setDevicePixelRatio(XScreen* self, float ratio);

/**
 * @brief      返回可用几何（对标 QScreen::availableGeometry()）。
 * @details    未显式设置时返回几何本身（与 QPlatformScreen 缺省实现一致）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     可用矩形。
 */
XRect XScreen_availableGeometry(const XScreen* self);
/**
 * @brief      设置可用几何。
 * @details    首次设置后可用几何独立于几何；值变化时发射
 *             availableGeometryChanged，并触发兄弟集合的可用虚拟几何
 *             复查。
 * @param      self 目标屏幕；可为 NULL。
 * @param      geometry 可用矩形；可为 NULL 表示不操作。
 */
void XScreen_setAvailableGeometry(XScreen* self, const XRect* geometry);

/**
 * @brief      返回可用尺寸（对标 QScreen::availableSize()，等于
 *             availableGeometry 的宽高）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     可用尺寸。
 */
XSize XScreen_availableSize(const XScreen* self);

/* ==================== 虚拟桌面（兄弟屏幕） ==================== */

/**
 * @brief      返回兄弟屏幕列表（对标 QScreen::virtualSiblings()）。
 * @details    若设置了显式兄弟列表，返回该列表副本；否则返回“自身 +
 *             注册表中的全部其它屏幕”。返回的是新建 XVector，元素为
 *             XScreen* 借用指针，调用方用 XVector_delete_base 释放。
 * @param      self 目标屏幕；可为 NULL。
 * @return     新建兄弟列表；NULL 入参返回空列表。
 */
XVector* XScreen_virtualSiblings(const XScreen* self);
/**
 * @brief      设置显式兄弟屏幕列表。
 * @details    列表元素为借用指针，XScreen 不拥有它们。设置 NULL 或
 *             count<=0 时清除显式列表并恢复默认语义。兄弟集合的虚拟
 *             几何若变化会发射 virtualGeometryChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      siblings 兄弟屏幕数组；可为 NULL。
 * @param      count 数组元素个数；非正数视为清除。
 */
void XScreen_setVirtualSiblings(XScreen* self, XScreen* const* siblings, int count);

/**
 * @brief      返回包含给定点的兄弟屏幕（对标 QScreen::virtualSiblingAt()）。
 * @details    遍历 virtualSiblings()（与 Qt 一致，坐标相对虚拟桌面），
 *             返回第一个 geometry 包含该点的屏幕。
 * @param      self 目标屏幕；可为 NULL。
 * @param      point 查询点。
 * @return     命中的兄弟屏幕借用指针；未命中或入参为 NULL 时返回 NULL。
 */
XScreen* XScreen_virtualSiblingAt(const XScreen* self, XPoint point);

/**
 * @brief      返回虚拟桌面尺寸（对标 QScreen::virtualSize()，等于
 *             virtualGeometry 的宽高）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     虚拟桌面尺寸。
 */
XSize XScreen_virtualSize(const XScreen* self);
/**
 * @brief      返回虚拟桌面几何（对标 QScreen::virtualGeometry()，即兄弟
 *             几何的并集）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     虚拟桌面矩形；无兄弟时返回 (0,0,0,0)。
 */
XRect XScreen_virtualGeometry(const XScreen* self);
/**
 * @brief      返回可用虚拟桌面尺寸（对标 QScreen::availableVirtualSize()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     可用虚拟桌面尺寸。
 */
XSize XScreen_availableVirtualSize(const XScreen* self);
/**
 * @brief      返回可用虚拟桌面几何（对标 QScreen::availableVirtualGeometry()，
 *             即兄弟可用几何的并集）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     可用虚拟桌面矩形；无兄弟时返回 (0,0,0,0)。
 */
XRect XScreen_availableVirtualGeometry(const XScreen* self);

/* ==================== 方向与刷新率 ==================== */

/**
 * @brief      返回主方向（对标 QScreen::primaryOrientation()）。
 * @details    默认由几何推导：宽 ≥ 高为 Landscape，否则 Portrait；显示
 *             方向变化导致几何变化时随之更新，直到调用
 *             setPrimaryOrientation 显式锁定。
 * @param      self 目标屏幕；可为 NULL。
 * @return     主方向。
 */
XScreenOrientation XScreen_primaryOrientation(const XScreen* self);
/**
 * @brief      显式设置主方向并锁定（不再随几何自动推导）。
 * @details    值变化时发射 primaryOrientationChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      orientation 新主方向；传入 Primary 视为无效并忽略。
 */
void XScreen_setPrimaryOrientation(XScreen* self, XScreenOrientation orientation);

/**
 * @brief      返回当前显示方向（对标 QScreen::orientation()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     当前方向；默认 Primary（未配置）。
 */
XScreenOrientation XScreen_orientation(const XScreen* self);
/**
 * @brief      设置当前显示方向。
 * @details    值变化时发射 orientationChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      orientation 当前方向。
 */
void XScreen_setOrientation(XScreen* self, XScreenOrientation orientation);

/**
 * @brief      返回原生方向（对标 QScreen::nativeOrientation()）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     原生方向；默认 Primary（平台未提供时与 Qt 一致）。
 */
XScreenOrientation XScreen_nativeOrientation(const XScreen* self);
/**
 * @brief      设置原生方向。
 * @param      self 目标屏幕；可为 NULL。
 * @param      orientation 原生方向；可为 Primary 表示未知。
 */
void XScreen_setNativeOrientation(XScreen* self, XScreenOrientation orientation);

/**
 * @brief      返回刷新率（对标 QScreen::refreshRate()，单位 Hz）。
 * @param      self 目标屏幕；可为 NULL。
 * @return     刷新率；默认 60。
 */
float XScreen_refreshRate(const XScreen* self);
/**
 * @brief      设置刷新率。
 * @details    值变化时发射 refreshRateChanged。
 * @param      self 目标屏幕；可为 NULL。
 * @param      refreshRate 刷新率（Hz）。
 */
void XScreen_setRefreshRate(XScreen* self, float refreshRate);

/* ==================== 方向换算（对标 QScreen 同名接口） ==================== */

/**
 * @brief      计算从方向 a 旋转到方向 b 所需的角度（对标
 *             QScreen::angleBetween()）。
 * @details    Primary 先解析为 primaryOrientation()；结果固定为
 *             0/90/180/270。算法与 Qt 6.8.3 一致：对 1/2/4/8 取 log2，
 *             差值模 4 查 {0,90,180,270} 表。入参非法时返回 0。
 * @param      self 目标屏幕；可为 NULL（Primary 无法解析时按 Landscape
 *             处理）。
 * @param      a 起始方向。
 * @param      b 目标方向。
 * @return     旋转角度（度）。
 */
int XScreen_angleBetween(const XScreen* self, XScreenOrientation a,
                         XScreenOrientation b);
/**
 * @brief      计算从方向 a 坐标系统一到方向 b 坐标系的变换矩阵（对标
 *             QScreen::transformBetween()）。
 * @details    矩阵语义与 Qt 6.8.3 QPlatformScreen::transformBetween 完全
 *             一致：先按 target 平移再旋转（QTransform 的 translate +
 *             rotate）。例如 a=Landscape、b=Portrait、target=(0,0,w,h) 时
 *             角度为 90，映射 (0,0)->(w,0)、(h,w)->(0,h)，即
 *             x'=w-y、y'=x。结果按 XImageTransform 字段返回
 *             （x'=m11*x+m21*y+dx，y'=m12*x+m22*y+dy，透视项取 0/0/1）。
 * @param      self 目标屏幕；可为 NULL。
 * @param      a 起始方向。
 * @param      b 目标方向。
 * @param      target 目标矩形（用于确定平移量）；可为 NULL 按 (0,0,0,0)。
 * @return     齐次 2D 仿射矩阵。
 */
XImageTransform XScreen_transformBetween(const XScreen* self,
                                         XScreenOrientation a,
                                         XScreenOrientation b,
                                         const XRect* target);
/**
 * @brief      在两种方向间映射矩形（对标 QScreen::mapBetween()）。
 * @details    Primary 先解析；方向同面（同为竖屏或同为横屏）时返回原矩形，
 *             竖屏/横屏互换时交换 x/y 与宽高，与 Qt 6.8.3 一致。
 * @param      self 目标屏幕；可为 NULL。
 * @param      a 起始方向。
 * @param      b 目标方向。
 * @param      rect 待映射矩形；可为 NULL 按 (0,0,0,0)。
 * @return     映射后的矩形。
 */
XRect XScreen_mapBetween(const XScreen* self, XScreenOrientation a,
                         XScreenOrientation b, const XRect* rect);
/**
 * @brief      判断方向是否为竖屏（对标 QScreen::isPortrait()）。
 * @details    Portrait/InvertedPortrait 为竖屏；Primary 解析为
 *             primaryOrientation() 后判断。
 * @param      self 目标屏幕；可为 NULL。
 * @param      orientation 待判断方向。
 * @return     竖屏返回 true；非法或无法解析返回 false。
 */
bool XScreen_isPortrait(const XScreen* self, XScreenOrientation orientation);
/**
 * @brief      判断方向是否为横屏（对标 QScreen::isLandscape()）。
 * @details    Landscape/InvertedLandscape 为横屏；Primary 解析为
 *             primaryOrientation() 后判断。
 * @param      self 目标屏幕；可为 NULL。
 * @param      orientation 待判断方向。
 * @return     横屏返回 true；非法或无法解析返回 false。
 */
bool XScreen_isLandscape(const XScreen* self, XScreenOrientation orientation);

/* ==================== 抓屏 ==================== */

/**
 * @brief      抓取指定窗口/屏幕区域像素（对标 QScreen::grabWindow()）。
 * @details    在 X11/Win32 原生窗口后端可用时返回真实抓屏结果；无显示服务器
 *             或嵌入式平台不可抓取时返回空像素图（XPixmap_isNull 为 true）。
 *             window 为 0 表示整屏。
 * @param      self 目标屏幕；可为 NULL。
 * @param      window 目标窗口标识（对标 Qt WId）；0 表示整屏。
 * @param      x 抓取区域左上角 X 偏移（设备无关像素）。
 * @param      y 抓取区域左上角 Y 偏移。
 * @param      w 抓取宽度；负值表示到窗口右边界。
 * @param      h 抓取高度；负值表示到窗口下边界。
 * @return     新建 XPixmap；分配失败返回 NULL，调用方用
 *             XPixmap_delete_base 释放。
 */
XPixmap* XScreen_grabWindow(XScreen* self, XWindowId window,
                            int x, int y, int w, int h);

/* ==================== 通知信号（对标 QScreen 全部 9 个信号） ==================== */

/** @brief 几何变化信号（对标 QScreen::geometryChanged）。 @param self 目标屏幕。 @param geometry 新几何。 */
void* XScreen_geometryChanged_signal(XScreen* self, const XRect* geometry);
/** @brief 可用几何变化信号（对标 QScreen::availableGeometryChanged）。 @param self 目标屏幕。 @param geometry 新可用几何。 */
void* XScreen_availableGeometryChanged_signal(XScreen* self, const XRect* geometry);
/** @brief 物理尺寸变化信号（对标 QScreen::physicalSizeChanged）。 @param self 目标屏幕。 @param size 新物理尺寸。 */
void* XScreen_physicalSizeChanged_signal(XScreen* self, const XSizeF* size);
/** @brief 物理 DPI 变化信号（对标 QScreen::physicalDotsPerInchChanged）。 @param self 目标屏幕。 @param dpi 平均物理 DPI。 */
void* XScreen_physicalDotsPerInchChanged_signal(XScreen* self, float dpi);
/** @brief 逻辑 DPI 变化信号（对标 QScreen::logicalDotsPerInchChanged）。 @param self 目标屏幕。 @param dpi 平均逻辑 DPI。 */
void* XScreen_logicalDotsPerInchChanged_signal(XScreen* self, float dpi);
/** @brief 虚拟几何变化信号（对标 QScreen::virtualGeometryChanged）。 @param self 目标屏幕。 @param rect 新虚拟几何。 */
void* XScreen_virtualGeometryChanged_signal(XScreen* self, const XRect* rect);
/** @brief 主方向变化信号（对标 QScreen::primaryOrientationChanged）。 @param self 目标屏幕。 @param orientation 新主方向。 */
void* XScreen_primaryOrientationChanged_signal(XScreen* self, XScreenOrientation orientation);
/** @brief 方向变化信号（对标 QScreen::orientationChanged）。 @param self 目标屏幕。 @param orientation 新方向。 */
void* XScreen_orientationChanged_signal(XScreen* self, XScreenOrientation orientation);
/** @brief 刷新率变化信号（对标 QScreen::refreshRateChanged）。 @param self 目标屏幕。 @param refreshRate 新刷新率。 */
void* XScreen_refreshRateChanged_signal(XScreen* self, float refreshRate);

#endif /* XSCREEN_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSCREEN_H */
