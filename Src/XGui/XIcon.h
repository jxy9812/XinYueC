/******************************************************************************
 * @file       XIcon.h
 * @brief      XIcon 图标类（对标 Qt 6.8 QIcon）
 * @author     XinYueC 团队
 * @note       提供可缩放图标功能，支持多种模式（正常/禁用/激活/选中）和状态（开/关）
 ******************************************************************************/
#ifndef XICON_H
#define XICON_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XPixmap.h"
#include "XClass/XClass.h"
#include "XClass/XTypes.h"


/* ========== XIcon 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XIcon)
XCLASS_DEFINE_EXTEND_END(XIcon, XClass)

/* 前向声明 */
typedef struct XIconPrivate XIconPrivate;

/**
 * @brief      XIcon 图标模式枚举（对标 Qt 6.8 QIcon::Mode）
 */
typedef enum XIconMode
{
    XIconMode_Normal,    /**< 正常模式 */
    XIconMode_Disabled,  /**< 禁用模式 */
    XIconMode_Active,    /**< 激活模式 */
    XIconMode_Selected   /**< 选中模式 */
} XIconMode;

/**
 * @brief      XIcon 图标状态枚举（对标 Qt 6.8 QIcon::State）
 */
typedef enum XIconState
{
    XIconState_Off,      /**< 关闭状态 */
    XIconState_On        /**< 开启状态 */
} XIconState;

/**
 * @brief      XIcon 图标类结构体（对标 Qt 6.8 QIcon）
 * @note       提供可缩放图标功能，支持多分辨率、多模式、多状态
 */
typedef struct XIcon
{
    XClass        m_class;   /**< 继承的基类成员 */
    XIconPrivate*  m_data;    /**< 图标私有数据指针 */
}XIcon;

/**
 * @brief      初始化 XIcon 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XIcon_class_init();

/**
 * @brief      在堆上创建 XIcon 实例
 * @return     指向新创建的 XIcon 对象的指针，失败返回 NULL
 */
XIcon* XIcon_create();

/**
 * @brief      初始化 XIcon 实例（创建空图标）
 * @param self 待初始化的 XIcon 对象指针
 */
void XIcon_init(XIcon* self);

/**
 * @brief      使用 XPixmap 创建图标
 * @param self   待初始化的 XIcon 对象指针
 * @param pixmap 源 XPixmap 对象指针
 */
void XIcon_init_pixmap(XIcon* self, const XPixmap* pixmap);

/**
 * @brief      从文件加载图标
 * @param self     待初始化的 XIcon 对象指针
 * @param fileName 文件名
 */
void XIcon_init_file(XIcon* self, const char* fileName);

/**
 * @brief      使用图标引擎创建图标
 * @param self   待初始化的 XIcon 对象指针
 * @param engine 图标引擎指针
 */
void XIcon_init_engine(XIcon* self, void* engine);

/**
 * @brief      复制构造函数
 * @param self 目标 XIcon 对象指针
 * @param other 源 XIcon 对象指针
 */
void XIcon_copy(XIcon* self, const XIcon* other);

/**
 * @brief      移动构造函数
 * @param self 目标 XIcon 对象指针
 * @param other 源 XIcon 对象指针（移动后源对象变为空）
 */
void XIcon_move(XIcon* self, XIcon* other);

/**
 * @brief      释放 XIcon 资源
 * @param self 待释放的 XIcon 对象指针
 */
void XIcon_deinit(XIcon* self);

/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
void XIcon_copy_base(XIcon* dest, const XIcon* src);

/**
 * @brief      虚函数调度：移动
 * @param dest 目标对象指针
 * @param src  源对象指针（移动后源对象变为空）
 */
void XIcon_move_base(XIcon* dest, XIcon* src);

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
void XIcon_deinit_base(XIcon* self);

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
void XIcon_delete_base(XIcon* self);

/* ========== 查询方法 ========== */

/**
 * @brief      判断图标是否为 null
 * @param self 目标 XIcon 对象指针
 * @return 图标为 null 返回 true，否则返回 false
 */
bool XIcon_isNull(const XIcon* self);

/**
 * @brief      判断图标数据是否已分离
 * @param self 目标 XIcon 对象指针
 * @return 已分离返回 true
 */
bool XIcon_isDetached(const XIcon* self);

/**
 * @brief      分离数据（写时复制）
 * @param self 目标 XIcon 对象指针
 */
void XIcon_detach(XIcon* self);

/**
 * @brief      获取缓存键值
 * @param self 目标 XIcon 对象指针
 * @return 缓存键值（64 位）
 */
int64_t XIcon_cacheKey(const XIcon* self);

/* ========== 像素图获取 ========== */

/**
 * @brief      获取指定尺寸和模式的像素图
 * @param self   目标 XIcon 对象指针
 * @param width  目标宽度
 * @param height 目标高度
 * @param mode   图标模式（默认 Normal）
 * @param state  图标状态（默认 Off）
 * @param out    输出像素图指针
 */
void XIcon_pixmap(const XIcon* self, int width, int height, XIconMode mode, XIconState state, XPixmap* out);

/**
 * @brief      获取指定尺寸（正方形）的像素图
 * @param self   目标 XIcon 对象指针
 * @param extent 边长
 * @param mode   图标模式
 * @param state  图标状态
 * @param out    输出像素图指针
 */
void XIcon_pixmapExtent(const XIcon* self, int extent, XIconMode mode, XIconState state, XPixmap* out);

/**
 * @brief      获取带设备像素比的像素图
 * @param self             目标 XIcon 对象指针
 * @param width            目标宽度
 * @param height           目标高度
 * @param devicePixelRatio 设备像素比
 * @param mode             图标模式
 * @param state            图标状态
 * @param out              输出像素图指针
 */
void XIcon_pixmapRatio(const XIcon* self, int width, int height, float devicePixelRatio,
                       XIconMode mode, XIconState state, XPixmap* out);

/**
 * @brief      获取图标实际渲染尺寸
 * @param self   目标 XIcon 对象指针
 * @param width  目标宽度
 * @param height 目标高度
 * @param mode   图标模式
 * @param state  图标状态
 * @param out    输出尺寸结构体指针
 */
void XIcon_actualSize(const XIcon* self, int width, int height, XIconMode mode, XIconState state, XSize* out);

/* ========== 绘制方法 ========== */

/**
 * @brief      绘制图标（使用矩形区域）
 * @param self      目标 XIcon 对象指针
 * @param painter   QPainter 指针
 * @param x         目标 x 坐标
 * @param y         目标 y 坐标
 * @param w         目标宽度
 * @param h         目标高度
 * @param alignment 对齐方式
 * @param mode      图标模式
 * @param state     图标状态
 */
void XIcon_paint(const XIcon* self, void* painter, int x, int y, int w, int h,
                 uint32_t alignment, XIconMode mode, XIconState state);

/* ========== 添加资源 ========== */

/**
 * @brief      添加像素图到图标
 * @param self   目标 XIcon 对象指针
 * @param pixmap 待添加的像素图指针
 * @param mode   图标模式
 * @param state  图标状态
 */
void XIcon_addPixmap(XIcon* self, const XPixmap* pixmap, XIconMode mode, XIconState state);

/**
 * @brief      添加文件到图标
 * @param self     目标 XIcon 对象指针
 * @param fileName 文件名
 * @param width    目标宽度（0 表示使用原始尺寸）
 * @param height   目标高度（0 表示使用原始尺寸）
 * @param mode     图标模式
 * @param state    图标状态
 */
void XIcon_addFile(XIcon* self, const char* fileName, int width, int height,
                   XIconMode mode, XIconState state);

/**
 * @brief      获取可用尺寸列表
 * @param self  目标 XIcon 对象指针
 * @param mode  图标模式
 * @param state 图标状态
 * @param out   输出尺寸列表指针
 */
void XIcon_availableSizes(const XIcon* self, XIconMode mode, XIconState state, void* out);

/* ========== 掩码 ========== */

/**
 * @brief      设置图标是否为掩码
 * @param self   目标 XIcon 对象指针
 * @param isMask 是否为掩码
 */
void XIcon_setIsMask(XIcon* self, bool isMask);

/**
 * @brief      判断图标是否为掩码
 * @param self 目标 XIcon 对象指针
 * @return 是掩码返回 true
 */
bool XIcon_isMask(const XIcon* self);

/* ========== 主题相关静态方法 ========== */

/**
 * @brief      从主题获取图标
 * @param name     图标名称
 * @param fallback 后备图标指针（可为 NULL）
 * @param out      输出图标指针
 */
void XIcon_fromTheme(const char* name, const XIcon* fallback, XIcon* out);

/**
 * @brief      判断主题是否存在指定图标
 * @param name 图标名称
 * @return 存在返回 true
 */
bool XIcon_hasThemeIcon(const char* name);

/**
 * @brief      获取主题搜索路径列表
 * @return 搜索路径字符串数组（XStringList*）
 */
void* XIcon_themeSearchPaths();

/**
 * @brief      设置主题搜索路径
 * @param paths 搜索路径列表指针
 */
void XIcon_setThemeSearchPaths(void* paths);

/**
 * @brief      获取主题名称
 * @return 主题名称字符串
 */
const char* XIcon_themeName();

/**
 * @brief      设置主题名称
 * @param name 主题名称
 */
void XIcon_setThemeName(const char* name);

/**
 * @brief      获取后备主题名称
 * @return 后备主题名称字符串
 */
const char* XIcon_fallbackThemeName();

/**
 * @brief      设置后备主题名称
 * @param name 后备主题名称
 */
void XIcon_setFallbackThemeName(const char* name);

#ifdef __cplusplus
}
#endif
#endif /* XICON_H */

