#ifndef XCOREAPPLICATION_H
#define XCOREAPPLICATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
#include"XBitArray.h"
#include"XString.h"
#include"XStringList.h"
#include"XEventType.h"
#include"XEventLoop.h"
#include"XCommandLineParser.h"
#include"XCommandLineOption.h"
/**
 * @brief 权限状态枚举（对标 Qt::PermissionStatus）
 */
typedef enum {
    XPERMISSION_STATUS_UNDETERMINED = 0,  ///< 未确定
    XPERMISSION_STATUS_GRANTED = 1,       ///< 已授权
    XPERMISSION_STATUS_DENIED = 2         ///< 已拒绝
} XPermissionStatus;

/**
 * @brief 权限结构体（对标 QPermission）
 */
typedef struct {
    int typeId;                    ///< 权限类型标识符
    XPermissionStatus status;      ///< 权限状态
} XPermission;

/**
 * @brief 权限请求完成回调函数类型
 * @param permission 请求完成的权限对象指针
 * @param userData 用户自定义数据指针
 */
typedef void (*XPermissionCallback)(XPermission* permission, void* userData);

/**
 * @brief 应用程序属性枚举（对标 Qt::ApplicationAttribute）
 */
typedef enum 
{
    XCORE_APPLICATION_ATTRIBUTE_QT_QUICK_USE_DEFAULT_SIZE_POLICY = 1,          ///< Qt Quick 使用默认大小策略
    XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS = 2,                  ///< 菜单中不显示图标
    XCORE_APPLICATION_ATTRIBUTE_NATIVE_WINDOWS = 3,                            ///< 使用原生窗口
    XCORE_APPLICATION_ATTRIBUTE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS = 4,        ///< 不创建原生控件兄弟
    XCORE_APPLICATION_ATTRIBUTE_PLUGIN_APPLICATION = 5,                        ///< 插件应用
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_MENU_BAR = 6,                  ///< 不使用原生菜单栏
    XCORE_APPLICATION_ATTRIBUTE_MAC_DONT_SWAP_CTRL_AND_META = 7,               ///< macOS 不交换 Ctrl 和 Meta
    XCORE_APPLICATION_ATTRIBUTE_USE_96_DPI = 8,                                ///< 使用 96 DPI
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_NATIVE_VIRTUAL_KEYBOARD = 9,            ///< 禁用原生虚拟键盘
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_MENU_WINDOWS = 10,             ///< 不使用原生菜单窗口
    XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_TOUCH_FOR_UNHANDLED_MOUSE_EVENTS = 11,  ///< 为未处理的鼠标事件合成触摸事件
    XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_MOUSE_FOR_UNHANDLED_TOUCH_EVENTS = 12,  ///< 为未处理的触摸事件合成鼠标事件
    XCORE_APPLICATION_ATTRIBUTE_FORCE_RASTER_WIDGETS = 14,                     ///< 强制使用光栅控件
    XCORE_APPLICATION_ATTRIBUTE_USE_DESKTOP_OPENGL = 15,                       ///< 使用桌面 OpenGL
    XCORE_APPLICATION_ATTRIBUTE_USE_OPEN_GLES = 16,                            ///< 使用 OpenGL ES
    XCORE_APPLICATION_ATTRIBUTE_USE_SOFTWARE_OPENGL = 17,                      ///< 使用软件 OpenGL
    XCORE_APPLICATION_ATTRIBUTE_SHARE_OPENGL_CONTEXTS = 18,                    ///< 共享 OpenGL 上下文
    XCORE_APPLICATION_ATTRIBUTE_SET_PALETTE = 19,                              ///< 设置调色板
    XCORE_APPLICATION_ATTRIBUTE_USE_STYLESHEET_PROPAGATION_IN_WIDGET_STYLES = 22,  ///< 在控件样式中使用样式表传播
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_DIALOGS = 23,                  ///< 不使用原生对话框
    XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_MOUSE_FOR_UNHANDLED_TABLET_EVENTS = 24,  ///< 为未处理的平板事件合成鼠标事件
    XCORE_APPLICATION_ATTRIBUTE_COMPRESS_HIGH_FREQUENCY_EVENTS = 25,           ///< 压缩高频事件
    XCORE_APPLICATION_ATTRIBUTE_DONT_CHECK_OPENGL_CONTEXT_THREAD_AFFINITY = 26,  ///< 不检查 OpenGL 上下文线程关联
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_SHADER_DISK_CACHE = 27,                ///< 禁用着色器磁盘缓存
    XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_SHORTCUTS_IN_CONTEXT_MENUS = 28,     ///< 上下文菜单中不显示快捷键
    XCORE_APPLICATION_ATTRIBUTE_COMPRESS_TABLET_EVENTS = 29,                   ///< 压缩平板事件
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_SESSION_MANAGER = 31,                  ///< 禁用会话管理器
    XCORE_APPLICATION_ATTRIBUTE_COUNT                                         ///< 属性总数（内部使用）
} XCoreApplicationAttribute;

XCLASS_DEFINE_BEGING(XCoreApplication)
XCLASS_DEFINE_ENUM(XCoreApplication, Notify) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XCoreApplication, Event),
XCLASS_DEFINE_END(XCoreApplication)

/**
 * @brief 核心应用程序类（对标 QCoreApplication）
 * 管理应用程序生命周期、事件循环和命令行解析
 */
typedef struct XCoreApplication
{
    XObject m_class;                ///< 基类 XObject
    int m_argc;                     ///< 命令行参数个数
    char** m_argv;                  ///< 命令行参数数组
    XString* m_applicationName;     ///< 应用程序名称
    XString* m_version;             ///< 应用程序版本
    XString* m_orgName;             ///< 组织名称
    XString* m_orgDomain;           ///< 组织域名
    XBitArray m_attribute;          ///< 应用程序属性位数组
    XStringList* m_paths;           ///< 库路径列表
    bool m_in_exec;                 ///< 是否正在执行事件循环
    bool m_aboutToQuitEmitted;      ///< aboutToQuit 信号是否已发出
    int m_returnCode;               ///< 退出返回码
} XCoreApplication;

/** @brief 获取全局 XCoreApplication 实例的便捷宏 */
#define xApp XCoreApplication_instance()

/* ==================== 生命周期管理 ==================== */

/**
 * @brief 初始化 XCoreApplication 虚函数表
 * @return 虚函数表指针
 */
XVtable* XCoreApplication_class_init();

/**
 * @brief 获取全局 XCoreApplication 实例
 * @return 全局实例指针，未创建时返回 NULL
 */
XCoreApplication* XCoreApplication_instance();

/**
 * @brief 创建 XCoreApplication 实例（堆分配）
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 创建的实例指针，已存在时返回已有实例
 */
XCoreApplication* XCoreApplication_create_ex(XMemoryType memory,  int argc, char** argv);

/**
 * @brief 初始化 XCoreApplication 实例（栈分配用）
 * @param app 要初始化的 XCoreApplication 指针
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 */
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);

/** @brief 删除 XCoreApplication 实例（委托给 XClass_delete_base） */
#define XCoreApplication_delete_base      XClass_delete_base

/* ==================== 应用程序元信息（对标 Qt QCoreApplication 属性） ==================== */

/**
 * @brief 设置应用程序名称（对标 QCoreApplication::setApplicationName）
 * @param applicationName 应用程序名称字符串
 */
void XCoreApplication_setApplicationName(const XString* applicationName);

/**
 * @brief 获取应用程序名称（对标 QCoreApplication::applicationName）
 * @return 应用程序名称字符串，未设置时返回 NULL
 */
const XString* XCoreApplication_applicationName(void);

/**
 * @brief 设置应用程序版本（对标 QCoreApplication::setApplicationVersion）
 * @param version 版本字符串
 */
void XCoreApplication_setApplicationVersion(const XString* version);

/**
 * @brief 获取应用程序版本（对标 QCoreApplication::applicationVersion）
 * @return 版本字符串，未设置时返回 NULL
 */
const XString* XCoreApplication_applicationVersion(void);

/**
 * @brief 设置组织名称（对标 QCoreApplication::setOrganizationName）
 * @param orgName 组织名称字符串
 */
void XCoreApplication_setOrganizationName(const XString* orgName);

/**
 * @brief 获取组织名称（对标 QCoreApplication::organizationName）
 * @return 组织名称字符串，未设置时返回 NULL
 */
const XString* XCoreApplication_organizationName(void);

/**
 * @brief 设置组织域名（对标 QCoreApplication::setOrganizationDomain）
 * @param orgDomain 组织域名字符串
 */
void XCoreApplication_setOrganizationDomain(const XString* orgDomain);

/**
 * @brief 获取组织域名（对标 QCoreApplication::organizationDomain）
 * @return 组织域名字符串，未设置时返回 NULL
 */
const XString* XCoreApplication_organizationDomain(void);

/* ==================== 应用程序属性（对标 Qt QCoreApplication::setAttribute / testAttribute） ==================== */

/**
 * @brief 设置应用程序属性（对标 QCoreApplication::setAttribute）
 * @param attribute 属性枚举值
 * @param on true 启用，false 禁用
 */
void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on);

/**
 * @brief 测试应用程序属性是否已设置（对标 QCoreApplication::testAttribute）
 * @param attribute 属性枚举值
 * @return true 已启用，false 未启用
 */
bool XCoreApplication_testAttribute(XCoreApplicationAttribute attribute);

/* ==================== 命令行参数（对标 Qt QCoreApplication::arguments） ==================== */

/**
 * @brief 获取命令行参数列表（对标 QCoreApplication::arguments）
 * @return 参数字符串列表，调用者负责释放
 */
XStringList* XCoreApplication_arguments(void);

/* ==================== 应用程序路径（对标 Qt QCoreApplication::applicationDirPath / applicationFilePath / applicationPid） ==================== */

/**
 * @brief 获取应用程序所在目录路径（对标 QCoreApplication::applicationDirPath）
 * @return 目录路径字符串，调用者负责释放
 */
const XString* XCoreApplication_applicationDirPath(void);

/**
 * @brief 获取应用程序可执行文件路径（对标 QCoreApplication::applicationFilePath）
 * @return 文件路径字符串，调用者负责释放
 */
const XString* XCoreApplication_applicationFilePath(void);

/**
 * @brief 获取应用程序进程 ID（对标 QCoreApplication::applicationPid）
 * @return 进程 ID
 */
int64_t XCoreApplication_applicationPid(void);

/* ==================== 事件循环控制（对标 Qt QCoreApplication::processEvents / exec / exit / quit） ==================== */

/**
 * @brief 处理等待中的事件（对标 QCoreApplication::processEvents）
 * @param flags 事件处理标志
 */
void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags);

/**
 * @brief 处理事件，最多处理指定毫秒数（对标 QCoreApplication::processEvents(QEventLoop::ProcessEventsFlags, int)）
 * @param flags 事件处理标志
 * @param maxtime 最大处理时间（毫秒）
 */
void XCoreApplication_processEventsWithMaxTime(XEventLoopProcessEventsFlags flags, int maxtime);

/* ==================== 原生事件过滤器（对标 Qt QCoreApplication::installNativeEventFilter / removeNativeEventFilter） ==================== */

/**
 * @brief 安装原生事件过滤器（对标 QCoreApplication::installNativeEventFilter）
 * @param filter 原生事件过滤器指针
 */
void XCoreApplication_installNativeEventFilter(struct XAbstractNativeEventFilter* filter);

/**
 * @brief 移除原生事件过滤器（对标 QCoreApplication::removeNativeEventFilter）
 * @param filter 原生事件过滤器指针
 */
void XCoreApplication_removeNativeEventFilter(struct XAbstractNativeEventFilter* filter);

/* ==================== 事件通知（对标 Qt QCoreApplication::notify / exit / quit / exec） ==================== */

/**
 * @brief 调用 notify 虚函数发送事件（对标 QCoreApplication::notify）
 * @param receiver 接收事件的对象指针
 * @param e 事件指针
 * @return true 事件已处理，false 未处理
 */
bool XCoreApplication_notify_base(XObject* receiver, XEvent* e);

/**
 * @brief 通知应用程序退出（对标 QCoreApplication::exit）
 * @param returnCode 退出返回码
 */
void XCoreApplication_exit(int returnCode);

/**
 * @brief 请求应用程序退出（对标 QCoreApplication::quit，调用 exit(0)）
 */
void XCoreApplication_quit();

/**
 * @brief 进入主事件循环（对标 QCoreApplication::exec）
 * @return 退出返回码
 */
int XCoreApplication_exec();

/* ==================== 事件发送/投递（对标 Qt QCoreApplication::sendEvent / postEvent / sendPostedEvents / removePostedEvents / forwardEvent） ==================== */

/**
 * @brief 直接发送事件给接收者（对标 QCoreApplication::sendEvent）
 * @param receiver 接收事件的对象指针
 * @param event 事件指针
 * @return true 事件已处理，false 未处理
 */
bool XCoreApplication_sendEvent(XObject* receiver, XEvent* event);

/**
 * @brief 投递事件到事件队列（对标 QCoreApplication::postEvent）
 * @param receiver 接收事件的对象指针
 * @param event 事件指针（函数接管所有权）
 * @param priority 优先级（数值越大优先级越高）
 */
void XCoreApplication_postEvent(XObject* receiver, XEvent* event, int priority);

/**
 * @brief 尝试投递事件（对标 QCoreApplication::tryPostEvent）
 * @param receiver 接收事件的对象指针
 * @param event 事件指针
 * @param priority 优先级
 * @return true 投递成功，false 失败
 */
bool XCoreApplication_tryPostEvent(XObject* receiver, XEvent* event, int priority);

/**
 * @brief 立即发送已投递的事件（对标 QCoreApplication::sendPostedEvents）
 * @param receiver 指定接收者（NULL 表示所有对象）
 * @param eventType 事件类型（0 表示所有类型）
 */
void XCoreApplication_sendPostedEvents(XObject* receiver, XEventType eventType);

/**
 * @brief 移除已投递的事件（对标 QCoreApplication::removePostedEvents）
 * @param receiver 指定接收者（NULL 表示所有对象）
 * @param eventType 事件类型（0 表示所有类型）
 */
void XCoreApplication_removePostedEvents(XObject* receiver, XEventType eventType);

/**
 * @brief 转发事件，复制源事件的 spontaneous 状态（对标 QCoreApplication::forwardEvent）
 * @param receiver 接收事件的对象指针
 * @param event 要转发的事件指针
 * @param originatingEvent 源事件指针（用于复制 spontaneous 状态）
 * @return true 事件已处理，false 未处理
 */
bool XCoreApplication_forwardEvent(XObject* receiver, XEvent* event, XEvent* originatingEvent);

/* ==================== 事件分发器（对标 Qt QCoreApplication::eventDispatcher / setEventDispatcher） ==================== */

/**
 * @brief 获取主线程的事件分发器（对标 QCoreApplication::eventDispatcher）
 * @return 事件分发器指针
 */
XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void);

/**
 * @brief 设置主线程的事件分发器（对标 QCoreApplication::setEventDispatcher）
 * @param dispatcher 事件分发器指针
 * @note Qt 6.8: 只能在尚无分发器时设置，已有分发器时调用无效
 */
void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher);

/* ==================== 库路径管理（对标 Qt QCoreApplication::libraryPaths / setLibraryPaths / addLibraryPath / removeLibraryPath） ==================== */

/**
 * @brief 设置库路径列表（对标 QCoreApplication::setLibraryPaths）
 * @param paths 库路径列表
 */
void XCoreApplication_setLibraryPaths(const XStringList* paths);

/**
 * @brief 获取库路径列表（对标 QCoreApplication::libraryPaths）
 * @return 库路径列表指针
 */
const XStringList* XCoreApplication_libraryPaths(void);

/**
 * @brief 添加库路径（对标 QCoreApplication::addLibraryPath）
 * @param path 要添加的路径
 */
void XCoreApplication_addLibraryPath(const XString* path);

/**
 * @brief 移除库路径（对标 QCoreApplication::removeLibraryPath）
 * @param path 要移除的路径
 */
void XCoreApplication_removeLibraryPath(const XString* path);

/* ==================== 信号（对标 Qt QCoreApplication 信号） ==================== */

/**
 * @brief 发出 aboutToQuit 信号（对标 QCoreApplication::aboutToQuit）
 * @param app XCoreApplication 实例指针
 * @return 信号返回值
 */
void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app);

/**
 * @brief 发出 applicationNameChanged 信号（对标 QCoreApplication::applicationNameChanged）
 * @param app XCoreApplication 实例指针
 * @return 信号返回值
 */
void* XCoreApplication_applicationNameChanged_signal(XCoreApplication* app);

/**
 * @brief 发出 applicationVersionChanged 信号（对标 QCoreApplication::applicationVersionChanged）
 * @param app XCoreApplication 实例指针
 * @return 信号返回值
 */
void* XCoreApplication_applicationVersionChanged_signal(XCoreApplication* app);

/**
 * @brief 发出 organizationDomainChanged 信号（对标 QCoreApplication::organizationDomainChanged）
 * @param app XCoreApplication 实例指针
 * @return 信号返回值
 */
void* XCoreApplication_organizationDomainChanged_signal(XCoreApplication* app);

/**
 * @brief 发出 organizationNameChanged 信号（对标 QCoreApplication::organizationNameChanged）
 * @param app XCoreApplication 实例指针
 * @return 信号返回值
 */
void* XCoreApplication_organizationNameChanged_signal(XCoreApplication* app);

/* ==================== 应用状态（对标 Qt QCoreApplication::startingUp / closingDown） ==================== */

/**
 * @brief 检查应用程序是否正在启动中（对标 QCoreApplication::startingUp）
 * @return true 正在启动，false 已启动完成
 */
bool XCoreApplication_startingUp(void);

/**
 * @brief 检查应用程序是否正在关闭中（对标 QCoreApplication::closingDown）
 * @return true 正在关闭，false 正常运行
 */
bool XCoreApplication_closingDown(void);

/* ==================== setuid 安全（对标 Qt QCoreApplication::setSetuidAllowed / isSetuidAllowed） ==================== */

/**
 * @brief 设置是否允许 setuid 运行（对标 QCoreApplication::setSetuidAllowed）
 * @param allow true 允许，false 不允许
 */
void XCoreApplication_setSetuidAllowed(bool allow);

/**
 * @brief 检查是否允许 setuid 运行（对标 QCoreApplication::isSetuidAllowed）
 * @return true 允许，false 不允许
 */
bool XCoreApplication_isSetuidAllowed(void);

/* ==================== 自发事件发送（对标 Qt QCoreApplication::sendSpontaneousEvent） ==================== */

/**
 * @brief 发送自发事件（对标 QCoreApplication::sendSpontaneousEvent）
 * @param receiver 接收事件的对象指针
 * @param event 事件指针（spontaneous 标志会被设置为 true）
 * @return true 事件已处理，false 未处理
 */
bool XCoreApplication_sendSpontaneousEvent(XObject* receiver, XEvent* event);

/* ==================== 事件压缩（对标 Qt QCoreApplication::compressEvent） ==================== */

/**
 * @brief 压缩已投递的事件（对标 QCoreApplication::compressEvent）
 * @param event 要压缩的事件指针
 * @param receiver 接收事件的对象指针
 * @param postedEvents 已投递事件列表指针
 * @return true 事件已被压缩（已删除），false 未被压缩
 * @note Qt 6.8: compressEvent 是非虚函数
 */
bool XCoreApplication_compressEvent(XEvent* event, XObject* receiver, void* postedEvents);

/* ==================== quitLock 管理（对标 Qt QCoreApplication::isQuitLockEnabled / setQuitLockEnabled） ==================== */

/**
 * @brief 检查 quitLock 是否启用（对标 QCoreApplication::isQuitLockEnabled）
 * @return true 启用，false 禁用
 */
bool XCoreApplication_isQuitLockEnabled(void);

/**
 * @brief 设置 quitLock 启用状态（对标 QCoreApplication::setQuitLockEnabled）
 * @param enabled true 启用，false 禁用
 */
void XCoreApplication_setQuitLockEnabled(bool enabled);

/* ==================== 权限系统（对标 Qt QCoreApplication::checkPermission / requestPermission） ==================== */

/**
 * @brief 检查权限状态（对标 QCoreApplication::checkPermission）
 * @param permission 权限对象指针
 * @return 权限状态枚举值
 */
XPermissionStatus XCoreApplication_checkPermission(const XPermission* permission);

/**
 * @brief 请求权限（对标 QCoreApplication::requestPermission）
 * @param permission 权限对象指针
 * @param callback 请求完成回调函数
 * @param userData 用户自定义数据指针
 */
void XCoreApplication_requestPermission(XPermission* permission, XPermissionCallback callback, void* userData);

/* ==================== 内部接口（对标 Qt QCoreApplicationPrivate 内部函数） ==================== */

/**
 * @brief 内部通知函数（对标 QCoreApplication::notifyInternal2）
 * @param receiver 接收事件的对象指针
 * @param event 事件指针
 * @return true 事件已处理，false 未处理
 * @note 用于 sendEvent / sendSpontaneousEvent 内部调用，跳过应用级事件过滤器
 */
bool XCoreApplication_notifyInternal2(XObject* receiver, XEvent* event);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XCoreApplication_create
#define XCoreApplication_create(...) XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif
