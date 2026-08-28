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
#include "XClass.h"
#include "XTypes.h"
#include "XString.h"


/* ========== XIcon 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XIcon)
XCLASS_DEFINE_EXTEND_END(XIcon, XClass)

/* 前向声明 */
typedef struct XIconPrivate XIconPrivate;
typedef struct XStringList XStringList;
typedef struct XIconEngine XIconEngine;

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

/** @brief Qt 6.8 QIcon::ThemeIcon 的可移植枚举映射。 */
typedef enum XIconThemeIcon
{
    XIconThemeIcon_DocumentNew, /**< 新建文档。 */
    XIconThemeIcon_DocumentOpen, /**< 打开文档。 */
    XIconThemeIcon_DocumentSave, /**< 保存文档。 */
    XIconThemeIcon_DocumentSaveAs, /**< 文档另存为。 */
    XIconThemeIcon_DocumentPrint, /**< 打印文档。 */
    XIconThemeIcon_DocumentClose, /**< 关闭文档。 */
    XIconThemeIcon_DocumentProperties, /**< 文档属性。 */
    XIconThemeIcon_DocumentPreview, /**< 文档预览。 */
    XIconThemeIcon_DocumentEdit, /**< 编辑文档。 */
    XIconThemeIcon_DocumentView, /**< 查看文档。 */
    XIconThemeIcon_DocumentReload, /**< 重新加载文档。 */
    XIconThemeIcon_Folder, /**< 文件夹。 */
    XIconThemeIcon_FolderOpen, /**< 打开的文件夹。 */
    XIconThemeIcon_FolderNew, /**< 新建文件夹。 */
    XIconThemeIcon_User, /**< 用户。 */
    XIconThemeIcon_UserGroup, /**< 用户组。 */
    XIconThemeIcon_UserAdd, /**< 添加用户。 */
    XIconThemeIcon_UserRemove, /**< 删除用户。 */
    XIconThemeIcon_GoPrevious, /**< 上一项。 */
    XIconThemeIcon_GoNext, /**< 下一项。 */
    XIconThemeIcon_GoUp, /**< 向上。 */
    XIconThemeIcon_GoDown, /**< 向下。 */
    XIconThemeIcon_MediaPlay, /**< 播放。 */
    XIconThemeIcon_MediaPause, /**< 暂停。 */
    XIconThemeIcon_MediaStop, /**< 停止。 */
    XIconThemeIcon_MediaRecord, /**< 录制。 */
    XIconThemeIcon_MediaSeekForward, /**< 快进。 */
    XIconThemeIcon_MediaSeekBackward, /**< 快退。 */
    XIconThemeIcon_MediaSkipForward, /**< 跳过下一项。 */
    XIconThemeIcon_MediaSkipBackward, /**< 跳过上一项。 */
    XIconThemeIcon_MediaEject, /**< 弹出媒体。 */
    XIconThemeIcon_ViewRefresh, /**< 刷新视图。 */
    XIconThemeIcon_ViewList, /**< 列表视图。 */
    XIconThemeIcon_ViewGrid, /**< 网格视图。 */
    XIconThemeIcon_ViewDetails, /**< 详细视图。 */
    XIconThemeIcon_ViewSidebar, /**< 侧边栏视图。 */
    XIconThemeIcon_ViewFullscreen, /**< 全屏视图。 */
    XIconThemeIcon_ViewRestore, /**< 恢复视图。 */
    XIconThemeIcon_WindowClose, /**< 关闭窗口。 */
    XIconThemeIcon_WindowMinimize, /**< 最小化窗口。 */
    XIconThemeIcon_WindowMaximize, /**< 最大化窗口。 */
    XIconThemeIcon_WindowRestore, /**< 恢复窗口。 */
    XIconThemeIcon_ApplicationExit, /**< 退出应用程序。 */
    XIconThemeIcon_Help, /**< 帮助。 */
    XIconThemeIcon_HelpAbout, /**< 关于帮助。 */
    XIconThemeIcon_PreferencesDesktop, /**< 桌面首选项。 */
    XIconThemeIcon_Invalid = -1 /**< 无效枚举值。 */
} XIconThemeIcon;

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
XIcon* XIcon_create_ex(XMemoryType memory);

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
void XIcon_init_file(XIcon* self, const XString* fileName);

/**
 * @brief 使用 UTF-8 文件名初始化图标的兼容重载。
 * @param self 待初始化的 XIcon 对象指针。
 * @param fileName UTF-8 编码的文件名。
 */
void XIcon_init_file_2(XIcon* self, const char* fileName);

/**
 * @brief      使用图标引擎创建图标
 * @param self   待初始化的 XIcon 对象指针
 * @param engine 图标引擎指针
 */
void XIcon_init_engine(XIcon* self, XIconEngine* engine);

/**
 * @brief      复制构造函数
 * @param self 目标 XIcon 对象指针
 * @param other 源 XIcon 对象指针
 */

/**
 * @brief      移动构造函数
 * @param self 目标 XIcon 对象指针
 * @param other 源 XIcon 对象指针（移动后源对象变为空）
 */

/**
 * @brief      释放 XIcon 资源
 * @param self 待释放的 XIcon 对象指针
 */
/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
/**
 * @brief 通过 XClass 虚表复制图标。
 * @param self 目标图标对象指针。
 * @param other 源图标对象指针。
 */
#define XIcon_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/**
 * @brief 通过 XClass 虚表移动图标。
 * @param self 目标图标对象指针。
 * @param other 源图标对象指针；移动后源对象为空。
 */
#define XIcon_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放图标资源。 @param self 待释放的图标对象指针。 */
#define XIcon_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
/** @brief 删除堆上的图标对象。 @param self 待删除的图标对象指针。 */
#define XIcon_delete_base(self) XClass_delete_base((XClass*)(self))

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
 * @brief      获取带设备像素比的像素图，按 Qt 的 DPR 优先策略选择资源
 * @param self             目标 XIcon 对象指针
 * @param width            目标逻辑宽度
 * @param height           目标逻辑高度
 * @param devicePixelRatio 设备像素比；输出会覆盖 width×height×DPR 的物理尺寸
 * @param mode             图标模式
 * @param state            图标状态
 * @param out              输出像素图指针
 */
void XIcon_pixmapRatio(const XIcon* self, int width, int height, float devicePixelRatio,
                       XIconMode mode, XIconState state, XPixmap* out);

/**
 * @brief      获取图标实际渲染逻辑尺寸；内部按 DPR=1 搜索资源
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
void XIcon_addFile(XIcon* self, const XString* fileName, int width, int height,
                   XIconMode mode, XIconState state);
/**
 * @brief 使用 UTF-8 文件名添加图标资源的兼容重载。
 * @param self 目标图标对象指针。
 * @param fileName UTF-8 编码的图标文件名。
 * @param width 目标宽度，0 表示使用原始尺寸。
 * @param height 目标高度，0 表示使用原始尺寸。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 */
void XIcon_addFile_2(XIcon* self, const char* fileName, int width, int height,
                     XIconMode mode, XIconState state);

/**
 * @brief      获取可用逻辑尺寸列表；@2x 等资源按设备像素比归一后去重
 * @param self  目标 XIcon 对象指针
 * @param mode  图标模式
 * @param state 图标状态
 * @param out   输出 XVector 指针，元素类型必须为 XSize；调用者负责先初始化向量
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

/**
 * @brief 获取图标名称。
 * @note 对于文件图标返回初始化时的文件名，对于主题图标返回主题名称；
 *       动态添加像素图的图标没有名称时返回空字符串。返回值由 XIcon 持有。
 */
XString* XIcon_name(const XIcon* self);

/** @brief 获取图标名称内部引用，对标 QString 返回值的 const 引用语义。 */
const XString* XIcon_name_const(const XIcon* self);

/** @brief 获取图标名称的 UTF-8 兼容版本；返回值由 XIcon 持有。 */
const char* XIcon_name_2(const XIcon* self);

/* ========== 主题相关静态方法 ========== */

/**
 * @brief      从主题获取图标
 * @param name     图标名称
 * @param fallback 后备图标指针（可为 NULL）
 * @param out      输出图标指针
 */
void XIcon_fromTheme(const XString* name, const XIcon* fallback, XIcon* out);

/**
 * @brief 使用 UTF-8 主题名称获取图标的兼容重载。
 * @param name UTF-8 编码的主题图标名称。
 * @param fallback 后备图标指针，可为 NULL。
 * @param out 输出图标对象指针，旧内容会被释放。
 */
void XIcon_fromTheme_2(const char* name, const XIcon* fallback, XIcon* out);

/**
 * @brief      判断主题是否存在指定图标
 * @param name 图标名称
 * @return 存在返回 true
 */
bool XIcon_hasThemeIcon(const XString* name);

/**
 * @brief 使用 UTF-8 名称查询主题图标的兼容重载。
 * @param name UTF-8 编码的主题图标名称。
 * @return 存在返回 true，否则返回 false。
 */
bool XIcon_hasThemeIcon_2(const char* name);

/**
 * @brief 根据 ThemeIcon 枚举创建主题图标。
 * @param icon 主题图标枚举值。
 * @param fallback 后备图标指针，可为 NULL。
 * @param out 输出图标对象指针，旧内容会被释放。
 */
void XIcon_fromThemeIcon(XIconThemeIcon icon, const XIcon* fallback, XIcon* out);

/**
 * @brief 查询 ThemeIcon 枚举对应的主题图标是否存在。
 * @param icon 主题图标枚举值。
 * @return 存在返回 true，否则返回 false。
 */
bool XIcon_hasThemeIconType(XIconThemeIcon icon);

/**
 * @brief 获取 ThemeIcon 对应的主题名称。
 * @param icon 主题图标枚举值。
 * @return 新建的 XString；调用者负责释放，未知枚举返回空字符串。
 */
XString* XIcon_themeIconName(XIconThemeIcon icon);

/**
 * @brief      获取主题搜索路径列表
 * @return 搜索路径字符串数组（XStringList*）
 */
XStringList* XIcon_themeSearchPaths();

/**
 * @brief 获取主题搜索路径列表的副本。
 * @return 新建的 XStringList；调用者负责释放。
 */
XStringList* XIcon_themeSearchPaths_2();

/**
 * @brief      设置主题搜索路径
 * @param paths 搜索路径列表指针
 */
void XIcon_setThemeSearchPaths(const XStringList* paths);

/**
 * @brief 设置主题搜索路径列表的兼容入口。
 * @param paths XStringList 路径列表；函数复制列表内容。
 */
void XIcon_setThemeSearchPaths_2(const XStringList* paths);

/**
 * @brief 获取主题后备搜索路径列表（返回新列表，调用者负责释放）。
 */
XStringList* XIcon_fallbackSearchPaths();

/**
 * @brief 获取后备主题搜索路径列表的副本。
 * @return 新建的 XStringList；调用者负责释放。
 */
XStringList* XIcon_fallbackSearchPaths_2();

/**
 * @brief 设置主题后备搜索路径列表。
 * @param paths XStringList 路径列表；函数复制列表内容。
 */
void XIcon_setFallbackSearchPaths(const XStringList* paths);

/**
 * @brief 设置后备主题搜索路径列表的兼容入口。
 * @param paths XStringList 路径列表；函数复制列表内容。
 */
void XIcon_setFallbackSearchPaths_2(const XStringList* paths);

/**
 * @brief      获取主题名称
 * @return 主题名称字符串
 */
XString* XIcon_themeName();

/**
 * @brief 获取当前主题名称的 UTF-8 兼容指针。
 * @return UTF-8 主题名称指针，由内部缓存持有，不得释放。
 */
const char* XIcon_themeName_2();

/**
 * @brief      设置主题名称
 * @param name 主题名称
 */
void XIcon_setThemeName(const XString* name);

/**
 * @brief 使用 UTF-8 名称设置主题名称的兼容重载。
 * @param name UTF-8 编码的主题名称；可为 NULL 清除设置。
 */
void XIcon_setThemeName_2(const char* name);

/**
 * @brief      获取后备主题名称
 * @return 后备主题名称字符串
 */
XString* XIcon_fallbackThemeName();

/**
 * @brief 获取后备主题名称的 UTF-8 兼容指针。
 * @return UTF-8 后备主题名称指针，由内部缓存持有，不得释放。
 */
const char* XIcon_fallbackThemeName_2();

/**
 * @brief      设置后备主题名称
 * @param name 后备主题名称
 */
void XIcon_setFallbackThemeName(const XString* name);

/**
 * @brief 使用 UTF-8 名称设置后备主题名称的兼容重载。
 * @param name UTF-8 编码的后备主题名称；可为 NULL 清除设置。
 */
void XIcon_setFallbackThemeName_2(const char* name);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XIcon_create
#define XIcon_create() XIcon_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XICON_H */
