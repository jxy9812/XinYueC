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
    XIconState_On,       /**< 开启状态 */
    XIconState_Off       /**< 关闭状态 */
} XIconState;

/**
 * @brief Qt 6.8 QIcon::ThemeIcon 的可移植枚举映射。
 * @note 前 150 项必须保持与 Qt 6.8 的声明顺序一致；NThemeIcons
 *       表示 Qt 标准项数量。其后的 Legacy 项仅用于兼容 XinYueC
 *       旧版本名称，不参与 Qt 标准序号。
 */
typedef enum XIconThemeIcon
{
    XIconThemeIcon_AddressBookNew, /**< 新建地址簿。 */
    XIconThemeIcon_ApplicationExit, /**< 退出应用程序。 */
    XIconThemeIcon_AppointmentNew, /**< 新建约会。 */
    XIconThemeIcon_CallStart, /**< 开始通话。 */
    XIconThemeIcon_CallStop, /**< 停止通话。 */
    XIconThemeIcon_ContactNew, /**< 新建联系人。 */
    XIconThemeIcon_DocumentNew, /**< 新建文档。 */
    XIconThemeIcon_DocumentOpen, /**< 打开文档。 */
    XIconThemeIcon_DocumentOpenRecent, /**< 打开最近文档。 */
    XIconThemeIcon_DocumentPageSetup, /**< 文档页面设置。 */
    XIconThemeIcon_DocumentPrint, /**< 打印文档。 */
    XIconThemeIcon_DocumentPrintPreview, /**< 打印预览。 */
    XIconThemeIcon_DocumentProperties, /**< 文档属性。 */
    XIconThemeIcon_DocumentRevert, /**< 还原文档。 */
    XIconThemeIcon_DocumentSave, /**< 保存文档。 */
    XIconThemeIcon_DocumentSaveAs, /**< 文档另存为。 */
    XIconThemeIcon_DocumentSend, /**< 发送文档。 */
    XIconThemeIcon_EditClear, /**< 清除编辑内容。 */
    XIconThemeIcon_EditCopy, /**< 复制编辑内容。 */
    XIconThemeIcon_EditCut, /**< 剪切编辑内容。 */
    XIconThemeIcon_EditDelete, /**< 删除编辑内容。 */
    XIconThemeIcon_EditFind, /**< 查找编辑内容。 */
    XIconThemeIcon_EditPaste, /**< 粘贴编辑内容。 */
    XIconThemeIcon_EditRedo, /**< 重做编辑操作。 */
    XIconThemeIcon_EditSelectAll, /**< 全选编辑内容。 */
    XIconThemeIcon_EditUndo, /**< 撤销编辑操作。 */
    XIconThemeIcon_FolderNew, /**< 新建文件夹。 */
    XIconThemeIcon_FormatIndentLess, /**< 减少缩进。 */
    XIconThemeIcon_FormatIndentMore, /**< 增加缩进。 */
    XIconThemeIcon_FormatJustifyCenter, /**< 居中对齐。 */
    XIconThemeIcon_FormatJustifyFill, /**< 两端对齐。 */
    XIconThemeIcon_FormatJustifyLeft, /**< 左对齐。 */
    XIconThemeIcon_FormatJustifyRight, /**< 右对齐。 */
    XIconThemeIcon_FormatTextDirectionLtr, /**< 从左到右文字方向。 */
    XIconThemeIcon_FormatTextDirectionRtl, /**< 从右到左文字方向。 */
    XIconThemeIcon_FormatTextBold, /**< 粗体文字。 */
    XIconThemeIcon_FormatTextItalic, /**< 斜体文字。 */
    XIconThemeIcon_FormatTextUnderline, /**< 下划线文字。 */
    XIconThemeIcon_FormatTextStrikethrough, /**< 删除线文字。 */
    XIconThemeIcon_GoDown, /**< 向下导航。 */
    XIconThemeIcon_GoHome, /**< 返回主目录。 */
    XIconThemeIcon_GoNext, /**< 下一项。 */
    XIconThemeIcon_GoPrevious, /**< 上一项。 */
    XIconThemeIcon_GoUp, /**< 向上导航。 */
    XIconThemeIcon_HelpAbout, /**< 关于帮助。 */
    XIconThemeIcon_HelpFaq, /**< 帮助常见问题。 */
    XIconThemeIcon_InsertImage, /**< 插入图像。 */
    XIconThemeIcon_InsertLink, /**< 插入链接。 */
    XIconThemeIcon_InsertText, /**< 插入文本。 */
    XIconThemeIcon_ListAdd, /**< 向列表添加项。 */
    XIconThemeIcon_ListRemove, /**< 从列表删除项。 */
    XIconThemeIcon_MailForward, /**< 转发邮件。 */
    XIconThemeIcon_MailMarkImportant, /**< 标记邮件重要。 */
    XIconThemeIcon_MailMarkRead, /**< 标记邮件已读。 */
    XIconThemeIcon_MailMarkUnread, /**< 标记邮件未读。 */
    XIconThemeIcon_MailMessageNew, /**< 新建邮件。 */
    XIconThemeIcon_MailReplyAll, /**< 回复所有收件人。 */
    XIconThemeIcon_MailReplySender, /**< 回复发件人。 */
    XIconThemeIcon_MailSend, /**< 发送邮件。 */
    XIconThemeIcon_MediaEject, /**< 弹出媒体。 */
    XIconThemeIcon_MediaPlaybackPause, /**< 暂停播放。 */
    XIconThemeIcon_MediaPlaybackStart, /**< 开始播放。 */
    XIconThemeIcon_MediaPlaybackStop, /**< 停止播放。 */
    XIconThemeIcon_MediaRecord, /**< 录制媒体。 */
    XIconThemeIcon_MediaSeekBackward, /**< 向后搜寻媒体。 */
    XIconThemeIcon_MediaSeekForward, /**< 向前搜寻媒体。 */
    XIconThemeIcon_MediaSkipBackward, /**< 跳过上一媒体项。 */
    XIconThemeIcon_MediaSkipForward, /**< 跳过下一媒体项。 */
    XIconThemeIcon_ObjectRotateLeft, /**< 向左旋转对象。 */
    XIconThemeIcon_ObjectRotateRight, /**< 向右旋转对象。 */
    XIconThemeIcon_ProcessStop, /**< 停止进程。 */
    XIconThemeIcon_SystemLockScreen, /**< 锁定屏幕。 */
    XIconThemeIcon_SystemLogOut, /**< 注销系统。 */
    XIconThemeIcon_SystemSearch, /**< 搜索系统。 */
    XIconThemeIcon_SystemReboot, /**< 重启系统。 */
    XIconThemeIcon_SystemShutdown, /**< 关闭系统。 */
    XIconThemeIcon_ToolsCheckSpelling, /**< 检查拼写。 */
    XIconThemeIcon_ViewFullscreen, /**< 全屏视图。 */
    XIconThemeIcon_ViewRefresh, /**< 刷新视图。 */
    XIconThemeIcon_ViewRestore, /**< 恢复视图。 */
    XIconThemeIcon_WindowClose, /**< 关闭窗口。 */
    XIconThemeIcon_WindowNew, /**< 新建窗口。 */
    XIconThemeIcon_ZoomFitBest, /**< 最佳适配缩放。 */
    XIconThemeIcon_ZoomIn, /**< 放大视图。 */
    XIconThemeIcon_ZoomOut, /**< 缩小视图。 */
    XIconThemeIcon_AudioCard, /**< 声卡设备。 */
    XIconThemeIcon_AudioInputMicrophone, /**< 麦克风设备。 */
    XIconThemeIcon_Battery, /**< 电池设备。 */
    XIconThemeIcon_CameraPhoto, /**< 照相机设备。 */
    XIconThemeIcon_CameraVideo, /**< 摄像机设备。 */
    XIconThemeIcon_CameraWeb, /**< 网络摄像头。 */
    XIconThemeIcon_Computer, /**< 计算机设备。 */
    XIconThemeIcon_DriveHarddisk, /**< 硬盘设备。 */
    XIconThemeIcon_DriveOptical, /**< 光盘驱动器。 */
    XIconThemeIcon_InputGaming, /**< 游戏输入设备。 */
    XIconThemeIcon_InputKeyboard, /**< 键盘设备。 */
    XIconThemeIcon_InputMouse, /**< 鼠标设备。 */
    XIconThemeIcon_InputTablet, /**< 手写板设备。 */
    XIconThemeIcon_MediaFlash, /**< 闪存媒体。 */
    XIconThemeIcon_MediaOptical, /**< 光学媒体。 */
    XIconThemeIcon_MediaTape, /**< 磁带媒体。 */
    XIconThemeIcon_MultimediaPlayer, /**< 多媒体播放器。 */
    XIconThemeIcon_NetworkWired, /**< 有线网络。 */
    XIconThemeIcon_NetworkWireless, /**< 无线网络。 */
    XIconThemeIcon_Phone, /**< 电话设备。 */
    XIconThemeIcon_Printer, /**< 打印机设备。 */
    XIconThemeIcon_Scanner, /**< 扫描仪设备。 */
    XIconThemeIcon_VideoDisplay, /**< 视频显示设备。 */
    XIconThemeIcon_AppointmentMissed, /**< 错过的约会。 */
    XIconThemeIcon_AppointmentSoon, /**< 即将到来的约会。 */
    XIconThemeIcon_AudioVolumeHigh, /**< 高音量。 */
    XIconThemeIcon_AudioVolumeLow, /**< 低音量。 */
    XIconThemeIcon_AudioVolumeMedium, /**< 中等音量。 */
    XIconThemeIcon_AudioVolumeMuted, /**< 静音音量。 */
    XIconThemeIcon_BatteryCaution, /**< 电池警告。 */
    XIconThemeIcon_BatteryLow, /**< 电量低。 */
    XIconThemeIcon_DialogError, /**< 错误对话框。 */
    XIconThemeIcon_DialogInformation, /**< 信息对话框。 */
    XIconThemeIcon_DialogPassword, /**< 密码对话框。 */
    XIconThemeIcon_DialogQuestion, /**< 问题对话框。 */
    XIconThemeIcon_DialogWarning, /**< 警告对话框。 */
    XIconThemeIcon_FolderDragAccept, /**< 文件夹接受拖放。 */
    XIconThemeIcon_FolderOpen, /**< 打开的文件夹。 */
    XIconThemeIcon_FolderVisiting, /**< 正在访问的文件夹。 */
    XIconThemeIcon_ImageLoading, /**< 图像加载中。 */
    XIconThemeIcon_ImageMissing, /**< 图像缺失。 */
    XIconThemeIcon_MailAttachment, /**< 邮件附件。 */
    XIconThemeIcon_MailUnread, /**< 未读邮件。 */
    XIconThemeIcon_MailRead, /**< 已读邮件。 */
    XIconThemeIcon_MailReplied, /**< 已回复邮件。 */
    XIconThemeIcon_MediaPlaylistRepeat, /**< 重复播放列表。 */
    XIconThemeIcon_MediaPlaylistShuffle, /**< 随机播放列表。 */
    XIconThemeIcon_NetworkOffline, /**< 网络离线。 */
    XIconThemeIcon_PrinterPrinting, /**< 打印机正在打印。 */
    XIconThemeIcon_SecurityHigh, /**< 高安全级别。 */
    XIconThemeIcon_SecurityLow, /**< 低安全级别。 */
    XIconThemeIcon_SoftwareUpdateAvailable, /**< 有可用软件更新。 */
    XIconThemeIcon_SoftwareUpdateUrgent, /**< 软件更新紧急。 */
    XIconThemeIcon_SyncError, /**< 同步错误。 */
    XIconThemeIcon_SyncSynchronizing, /**< 正在同步。 */
    XIconThemeIcon_UserAvailable, /**< 用户在线可用。 */
    XIconThemeIcon_UserOffline, /**< 用户离线。 */
    XIconThemeIcon_WeatherClear, /**< 晴朗天气。 */
    XIconThemeIcon_WeatherClearNight, /**< 晴朗夜间天气。 */
    XIconThemeIcon_WeatherFewClouds, /**< 少云天气。 */
    XIconThemeIcon_WeatherFewCloudsNight, /**< 少云夜间天气。 */
    XIconThemeIcon_WeatherFog, /**< 雾天天气。 */
    XIconThemeIcon_WeatherShowers, /**< 阵雨天气。 */
    XIconThemeIcon_WeatherSnow, /**< 降雪天气。 */
    XIconThemeIcon_WeatherStorm, /**< 暴风天气。 */
    XIconThemeIcon_NThemeIcons, /**< Qt 标准主题图标数量。 */

    /* 以下名称是旧版 XIconThemeIcon 的兼容扩展。 */
    XIconThemeIcon_DocumentClose, /**< 旧版关闭文档名称。 */
    XIconThemeIcon_DocumentPreview, /**< 旧版文档预览名称。 */
    XIconThemeIcon_DocumentEdit, /**< 旧版编辑文档名称。 */
    XIconThemeIcon_DocumentView, /**< 旧版查看文档名称。 */
    XIconThemeIcon_DocumentReload, /**< 旧版重新加载文档名称。 */
    XIconThemeIcon_Folder, /**< 旧版文件夹名称。 */
    XIconThemeIcon_User, /**< 旧版用户名称。 */
    XIconThemeIcon_UserGroup, /**< 旧版用户组名称。 */
    XIconThemeIcon_UserAdd, /**< 旧版添加用户名称。 */
    XIconThemeIcon_UserRemove, /**< 旧版删除用户名称。 */
    XIconThemeIcon_MediaPlay, /**< 旧版开始播放名称。 */
    XIconThemeIcon_MediaPause, /**< 旧版暂停播放名称。 */
    XIconThemeIcon_MediaStop, /**< 旧版停止播放名称。 */
    XIconThemeIcon_ViewList, /**< 旧版列表视图名称。 */
    XIconThemeIcon_ViewGrid, /**< 旧版网格视图名称。 */
    XIconThemeIcon_ViewDetails, /**< 旧版详细视图名称。 */
    XIconThemeIcon_ViewSidebar, /**< 旧版侧边栏名称。 */
    XIconThemeIcon_WindowMinimize, /**< 旧版最小化窗口名称。 */
    XIconThemeIcon_WindowMaximize, /**< 旧版最大化窗口名称。 */
    XIconThemeIcon_WindowRestore, /**< 旧版恢复窗口名称。 */
    XIconThemeIcon_Help, /**< 旧版帮助名称。 */
    XIconThemeIcon_PreferencesDesktop, /**< 旧版桌面首选项名称。 */
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
 * @param devicePixelRatio 设备像素比；大于 1 时向引擎请求高 DPI 资源，并根据
 *                         实际返回像素尺寸修正输出设备像素比；不大于 1 时按普通
 *                         DPI 路径返回并固定输出比例为 1。
 * @param mode             图标模式
 * @param state            图标状态
 * @param out              输出像素图指针；调用方负责释放已有内容，返回空图时保持空对象
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

/**
 * @brief      获取指定设备像素比下的图标实际逻辑尺寸
 * @param self             目标 XIcon 对象指针
 * @param width            目标逻辑宽度
 * @param height           目标逻辑高度
 * @param devicePixelRatio 设备像素比；大于 1 时先按物理尺寸查询引擎
 * @param mode             图标显示模式
 * @param state            图标状态
 * @param out              输出逻辑尺寸；无效请求或无法生成资源时为零尺寸
 * @note       对标 Qt 6.8 QIcon::actualSize() 的高 DPI 路径，结果仍以逻辑像素表示。
 */
void XIcon_actualSizeRatio(const XIcon* self, int width, int height,
                           float devicePixelRatio, XIconMode mode,
                           XIconState state, XSize* out);

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
 * @param width    目标宽度；宽高均为非负值时表示指定尺寸，负值表示使用原始尺寸/全部帧
 * @param height   目标高度；宽高均为非负值时表示指定尺寸，负值表示使用原始尺寸/全部帧
 * @param mode     图标模式
 * @param state    图标状态
 */
void XIcon_addFile(XIcon* self, const XString* fileName, int width, int height,
                   XIconMode mode, XIconState state);
/**
 * @brief 使用 UTF-8 文件名添加图标资源的兼容重载。
 * @param self 目标图标对象指针。
 * @param fileName UTF-8 编码的图标文件名。
 * @param width 目标宽度；宽高均为非负值时表示指定尺寸，负值表示使用原始尺寸/全部帧。
 * @param height 目标高度；宽高均为非负值时表示指定尺寸，负值表示使用原始尺寸/全部帧。
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
 * @brief 获取主题搜索路径列表。
 * @return 新建的搜索路径列表；调用方负责释放。用户路径为空时先取
 *         Drive 平台主题路径，再附加 Qt 兼容的 `:/icons` 内置资源目录。
 * @note 返回的系统路径和默认资源项不会写回用户显式路径存储。
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
