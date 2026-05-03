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
#include"XCommandLineOptionGroup.h"
/**
 * @brief 应用程序属性枚举。
 */
typedef enum 
{
    XCORE_APPLICATION_ATTRIBUTE_QT_QUICK_USE_DEFAULT_SIZE_POLICY = 1, // Qt Quick 布局使用 Item 的内置大小策略
    XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS = 2, // 菜单中不显示动作（Action）的图标
    XCORE_APPLICATION_ATTRIBUTE_NATIVE_WINDOWS = 3, // 确保控件拥有原生窗口
    XCORE_APPLICATION_ATTRIBUTE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS = 4, // 确保原生控件的兄弟节点保持非原生状态
    XCORE_APPLICATION_ATTRIBUTE_PLUGIN_APPLICATION = 5, // 表明 Qt 被用于编写插件
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_MENU_BAR = 6, // 不使用原生菜单栏
    XCORE_APPLICATION_ATTRIBUTE_MAC_DONT_SWAP_CTRL_AND_META = 7, // 在 Apple 平台上，不交换 Ctrl 和 Meta 键
    XCORE_APPLICATION_ATTRIBUTE_USE_96_DPI = 8, // 强制应用程序使用 96 DPI
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_NATIVE_VIRTUAL_KEYBOARD = 9, // 禁用原生虚拟键盘
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_MENU_WINDOWS = 10, // 不使用原生菜单窗口
    XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_TOUCH_FOR_UNHANDLED_MOUSE_EVENTS = 11, // 为未处理的鼠标事件合成触摸事件
    XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_MOUSE_FOR_UNHANDLED_TOUCH_EVENTS = 12, // 为未处理的触摸事件合成鼠标事件
    XCORE_APPLICATION_ATTRIBUTE_FORCE_RASTER_WIDGETS = 14, // 强制使用光栅化控件
    XCORE_APPLICATION_ATTRIBUTE_USE_DESKTOP_OPENGL = 15, // 强制使用桌面 OpenGL
    XCORE_APPLICATION_ATTRIBUTE_USE_OPEN_GLES = 16, // 强制使用 OpenGL ES
    XCORE_APPLICATION_ATTRIBUTE_USE_SOFTWARE_OPENGL = 17, // 强制使用软件渲染的 OpenGL
    XCORE_APPLICATION_ATTRIBUTE_SHARE_OPENGL_CONTEXTS = 18, // 允许不同窗口或控件共享 OpenGL 上下文
    XCORE_APPLICATION_ATTRIBUTE_SET_PALETTE = 19, // （内部使用）指示应用程序调色板已被设置
    XCORE_APPLICATION_ATTRIBUTE_USE_STYLESHEET_PROPAGATION_IN_WIDGET_STYLES = 22, // 在控件样式中使用样式表传播
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_DIALOGS = 23, // 不使用原生对话框
    XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_MOUSE_FOR_UNHANDLED_TABLET_EVENTS = 24, // 为未处理的数位板（Tablet）事件合成鼠标事件
    XCORE_APPLICATION_ATTRIBUTE_COMPRESS_HIGH_FREQUENCY_EVENTS = 25, // 压缩高频事件
    XCORE_APPLICATION_ATTRIBUTE_DONT_CHECK_OPENGL_CONTEXT_THREAD_AFFINITY = 26, // 不检查 OpenGL 上下文的线程亲和性
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_SHADER_DISK_CACHE = 27, // 禁用着色器磁盘缓存
    XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_SHORTCUTS_IN_CONTEXT_MENUS = 28, // 上下文菜单中不显示动作（Action）的快捷键
    XCORE_APPLICATION_ATTRIBUTE_COMPRESS_TABLET_EVENTS = 29, // 压缩数位板（Tablet）事件
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_SESSION_MANAGER = 31, // 禁用会话管理器（Session Manager）
    XCORE_APPLICATION_ATTRIBUTE_COUNT // 枚举计数器
} XCoreApplicationAttribute;
XCLASS_DEFINE_BEGING(XCoreApplication)
XCLASS_DEFINE_ENUM(XCoreApplication, Notify) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XCoreApplication)
/**
 * @brief 核心应用程序类
 * 管理应用程序生命周期、事件循环和命令行解析
 */
typedef struct XCoreApplication
{
    XObject m_class;               // 父对象
    int m_argc;                    // 命令行参数数量
    char** m_argv;                 // 命令行参数数组
    XString* m_applicationName;//应用程序名称
    XString* m_version;//应用程序版本号
    XString* m_orgName;//组织名称
    XString* m_orgDomain;//组织域名
    XBitArray m_attribute;//属性位数组
    XStringList* m_paths;//库搜索路径列表
} XCoreApplication;

#define xApp XCoreApplication_instance()
/**
 * @brief 获取应用程序类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XCoreApplication_class_init();

/**
 * @brief 获取全局应用程序实例
 * @return 全局唯一的应用程序实例
 */
XCoreApplication* XCoreApplication_instance();

/**
 * @brief 创建应用程序实例
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 新创建的应用程序实例
 */
XCoreApplication* XCoreApplication_create(int argc, char** argv);

/**
 * @brief 初始化应用程序实例
 * @param app 应用程序实例
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 */
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);

/**
 * @brief 销毁应用程序实例
 * @param app 要销毁的应用程序实例，传NULL无操作
 */
#define XCoreApplication_delete_base      XClass_delete_base
/* ==================== 应用程序元数据 (静态属性) ==================== */

/**
 * @brief 设置应用程序的名称。
 * @param applicationName 应用程序名称字符串。
 */
void XCoreApplication_setApplicationName(const XString* applicationName);

/**
 * @brief 获取应用程序的名称。
 * @return 应用程序名称字符串。
 */
const XString* XCoreApplication_applicationName(void);

/**
 * @brief 设置应用程序的版本号。
 * @param version 应用程序版本字符串。
 */
void XCoreApplication_setApplicationVersion(const XString* version);

/**
 * @brief 获取应用程序的版本号。
 * @return 应用程序版本字符串。
 */
const XString* XCoreApplication_applicationVersion(void);

/**
 * @brief 设置组织的名称。
 * @param orgName 组织名称字符串。
 */
void XCoreApplication_setOrganizationName(const XString* orgName);

/**
 * @brief 获取组织的名称。
 * @return 组织名称字符串。
 */
const XString* XCoreApplication_organizationName(void);

/**
 * @brief 设置组织的域名。
 * @param orgDomain 组织域名字符串。
 */
void XCoreApplication_setOrganizationDomain(const XString* orgDomain);

/**
 * @brief 获取组织的域名。
 * @return 组织域名字符串。
 */
const XString* XCoreApplication_organizationDomain(void);

/* ==================== 应用程序属性 ==================== */

/**
 * @brief 设置一个全局应用程序属性。
 *
 * @param attribute 要设置的属性。
 * @param on 如果为 true，则启用该属性；否则禁用。
 */
void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on);

/**
 * @brief 测试一个全局应用程序属性是否已启用。
 *
 * @param attribute 要测试的属性。
 * @return 如果属性已启用，则返回 true；否则返回 false。
 */
bool XCoreApplication_testAttribute(XCoreApplicationAttribute attribute);

/* ==================== 命令行与路径 ==================== */

/**
 * @brief 获取命令行参数列表。
 *
 * @return 参数列表。此对象由 XCore 管理，用户应自己释放。
 */
XStringList* XCoreApplication_arguments(void);

/**
 * @brief 获取应用程序可执行文件所在的目录路径。
 * @return 目录路径字符串。
 */
const XString* XCoreApplication_applicationDirPath(void);

/**
 * @brief 获取应用程序可执行文件的完整路径。
 * @return 完整文件路径字符串。
 */
const XString* XCoreApplication_applicationFilePath(void);

/**
 * @brief 获取当前应用程序进程的 PID (进程标识符)。
 * @return 进程 ID。
 */
int64_t XCoreApplication_applicationPid(void);

/**
 * @brief 处理待处理事件
 * @param flags 事件处理标志
 */
void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags);
/**
 * @brief 处理事件，但最多只运行指定的毫秒数。
 *
 * @param flags 事件处理标志。
 * @param maxtime 最大处理时间（毫秒）。
 */
void XCoreApplication_processEventsWithMaxTime(XEventLoopProcessEventsFlags flags, int maxtime);

/**
 * @brief 安装原生事件过滤器。
 * @param filter 过滤器对象。
 */
void XCoreApplication_installNativeEventFilter(struct XAbstractNativeEventFilter* filter);

/**
 * @brief 移除原生事件过滤器。
 * @param filter 过滤器对象。
 */
void XCoreApplication_removeNativeEventFilter(struct XAbstractNativeEventFilter* filter);

bool XCoreApplication_notify_base(XObject* receiver, XEvent* e);
void XCoreApplication_exit(int returnCode);
/**
 * @brief 退出应用程序
 */
void XCoreApplication_quit();
/**
 * @brief 启动应用程序事件循环
 * @return 退出码
 */
int XCoreApplication_exec();
/**
 * @brief 向指定接收者发送一个事件（立即处理）。
 *
 * @param receiver 事件接收者的指针（通常是一个 XObject*）。
 * @param event 要发送的事件（XEvent*）。
 * @return 如果事件被成功处理，则返回 true；否则返回 false。
 */
bool XCoreApplication_sendEvent(XObject* receiver, XEvent* event);
/**
 * @brief 向指定接收者投递一个事件（稍后处理）。
 *
 * @param receiver 事件接收者的指针。
 * @param event 要投递的事件。
 * @param priority 事件优先级。
 */
void XCoreApplication_postEvent(XObject* receiver, XEvent* event, int priority);
void XCoreApplication_tryPostEvent(XObject* receiver, XEvent* event, int priority);
/**
 * @brief 立即强制发送所有已投递的事件。
 *
 * @param receiver 如果为 NULL，则发送所有接收者的事件；否则只发送给指定接收者。
 * @param eventType 如果为 0，则发送所有类型的事件；否则只发送指定类型的事件。
 */
void XCoreApplication_sendPostedEvents(XObject* receiver, XEventType eventType);
/**
 * @brief 移除指定接收者的所有已投递事件。
 *
 * @param receiver 事件接收者的指针。
 * @param eventType 如果为 0，则移除所有类型的事件；否则只移除指定类型的事件。
 */
void XCoreApplication_removePostedEvents(XObject* receiver, XEventType eventType);
/* ==================== 事件分发器 ==================== */

/**
 * @brief 获取当前的应用程序事件分发器。
 * @return 事件分发器句柄。
 */
XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void);

/**
 * @brief 设置应用程序的事件分发器。
 * @param dispatcher 新的事件分发器句柄。
 */
void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher);

/* ==================== 库路径管理 ==================== */

/**
 * @brief 设置库搜索路径列表。
 * @param paths 路径列表。
 */
void XCoreApplication_setLibraryPaths(const XStringList* paths);

/**
 * @brief 获取当前的库搜索路径列表。
 * @return 路径列表。
 */
const XStringList* XCoreApplication_libraryPaths(void);

/**
 * @brief 向库搜索路径列表中添加一个新路径。
 * @param path 要添加的路径。
 */
void XCoreApplication_addLibraryPath(const XString* path);

/**
 * @brief 从库搜索路径列表中移除一个路径。
 * @param path 要移除的路径。
 */
void XCoreApplication_removeLibraryPath(const XString* path);


/**
 * @brief 获取即将退出的信号
 * @param app 应用程序实例
 * @return 信号指针
 */
void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app);

void* XCoreApplication_applicationNameChanged_signal(XCoreApplication* app);

void* XCoreApplication_applicationVersionChanged_signal(XCoreApplication* app);

void* XCoreApplication_organizationDomainChanged_signal(XCoreApplication* app);

void* XCoreApplication_organizationNameChanged_signal(XCoreApplication* app);
#ifdef __cplusplus
}
#endif
#endif
