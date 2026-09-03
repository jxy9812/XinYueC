/******************************************************************************
 * @file       XPlatformNativeWindow.h
 * @brief      XPlatformNativeWindow 平台原生窗口后端契约（对标 Qt 6.8
 *             QPlatformWindow / QGuiApplication 平台窗口插件链）。
 * @details    XPlatformNativeWindow 是 XWindow 公共类与 Drive 平台原生
 *             窗口后端之间的平台边界，采用「XWindow* 驱动 + 纯函数」形式：
 *             - XWindow 首次显示（XWindow_createHandle）时调用
 *               XPlatformNativeWindow_create 创建并登记真实系统窗口；
 *               XWindow_destroy 对应 XPlatformNativeWindow_destroy；
 *             - 映射/隐藏、几何、标题通过 setVisible / setGeometry /
 *               setTitle 逐项同步；setGeometry 内部按原生窗口当前几何
 *               去重，避免 X11 ConfigureNotify 回环触发无限重入；
 *             - winId() 返回真实系统窗口 id（X11 Window / Win32 HWND），
 *               XWindow_winId 在原生模式下优先透传该值；windowForWinId
 *               提供反查，供 XWindow_fromWinId 与焦点/顶层命中查询；
 *             - 事件侧提供两级泵：processPendingEvents 非阻塞处理当前
 *               全部待决原生事件，waitForEvents(msec) 先阻塞等待再处理
 *               一批；事件翻译为 XEvent 派生事件后统一经
 *               XWindowSystemInterface.handle* 注入（X11 Expose /
 *               ConfigureNotify / FocusIn / FocusOut / WM_DELETE，
 *               Win32 WM_PAINT / WM_SIZE / WM_SETFOCUS / WM_KILLFOCUS /
 *               WM_CLOSE / WM_SHOWWINDOW）；
 *             - 上屏侧 present() 把软件后备缓冲（ARGB32 预乘 Alpha）的
 *               脏区提交到真实窗口：Linux 使用 XPutImage（优先 32 位
 *               视觉直拷，必要时 24 位重排），Windows 使用 GDI BitBlt；
 *               XPlatformBackingStore 的平台后端在 native WinId 存在时
 *               自动调用本契约完成提交，调用方无需感知平台差异；
 *             - nativeConnection() 返回平台连接句柄（X11 Display* /
 *               Win32 HINSTANCE），供 XPlatformNativeInterface 的资源
 *               查询使用。
 *             公共契约头不包含任何平台 API 头；平台实现全部位于
 *             Drive/Posix/Graphics 与 Drive/windows/Graphics，另提供
 *             Drive/Unsupported/Graphics/XPlatformNativeWindow_unsupported.c 作为其余平台的
 *             空实现兜底，保证嵌入式零平台依赖可裁剪、可链接。
 * @note       模块总开关 XPLATFORMNATIVEWINDOW_ON 定义于 XGuiConfig.h；
 *             置 0 时本契约整体裁剪，XWindow 回退自增虚拟 WId 行为。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMNATIVEWINDOW_H
#define XPLATFORMNATIVEWINDOW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XTypes.h"
#include "XGeometry.h"
#include "XEvent.h"
#if XPLATFORMNATIVEWINDOW_ON

/** @brief 平台原生窗口连接类型（供 nativeConnection 区分平台）。 */
typedef enum XPlatformNativeWindowConnectionType
{
    XPlatformNativeWindowConnection_None = 0, /**< 未连接任何窗口系统。 */
    XPlatformNativeWindowConnection_X11,      /**< Linux X11（Display* 句柄）。 */
    XPlatformNativeWindowConnection_Win32     /**< Windows Win32（HINSTANCE 句柄）。 */
} XPlatformNativeWindowConnectionType;

/** @brief 窗口 id（WId）类型；XWindow.h 已定义时复用，避免重定义
 *         （本契约头不能包含 XWindow.h，因此独立提供守卫定义）。 */
#ifndef XWINDOWID_DEFINED
#define XWINDOWID_DEFINED 1
typedef uintptr_t XWindowId;
#endif

/** @brief XWindow 前向声明（公共层只持有借用指针，不引用其内部）。 */
typedef struct XWindow XWindow;
typedef struct XPixmap XPixmap;
/** @brief XString 前向声明（标题 UTF-8 字符串）。 */
typedef struct XString XString;
/** @brief XImage 前向声明（present 提交的软件帧缓冲）。 */
typedef struct XImage XImage;

/* ==================== 可用性与生命周期（平台后端提供） ==================== */

/**
 * @brief      查询当前进程是否已连接可用窗口系统。
 * @details    X11 后端在 XOpenDisplay 成功后返回 true（每进程单连接），
 *             Win32 后端只要有 GUI 消息队列即返回 true；嵌入式/无显示
 *             环境返回 false（XWindow 保持纯软件虚拟 WId 行为）。
 * @return     true 已连接；false 未连接。
 */
bool XPlatformNativeWindow_isAvailable(void);

/**
 * @brief      为窗口创建并登记真实原生窗口（首次显示前由 XWindow 调用）。
 * @details    创建原生窗口资源（X11: XCreateWindow；Win32:
 *             CreateWindowExW）并登记到进程平台注册表，但**不映射**；
 *             映射由 setVisible(true) 完成。重复创建（已登记）幂等返回
 *             true；显示连接不可用或创建失败返回 false，XWindow 保持
 *             嵌入式回退。创建成功后 XWindow_winId() 将返回真实 WId。
 * @param      window 目标窗口借用指针；可为 NULL。
 * @return     true 创建成功或已存在；false 失败或入参非法。
 */
bool XPlatformNativeWindow_create(XWindow* window);

/**
 * @brief 将现有外部原生窗口挂接到 XWindow，不取得其销毁所有权。
 * @details Linux 接受 X11 Window，Windows 接受 HWND；外部句柄由调用方负责
 *          创建和销毁，本层只登记、转发事件并在解除挂接时恢复宿主状态。
 */
bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId);

/**
 * @brief      销毁窗口对应的真实原生窗口并注销登记。
 * @details    与 XWindow_destroy 成对：普通窗口销毁原生资源，ForeignWindow
 *             只解除登记并保留宿主资源；之后 winId() 归零并按公共状态回退。
 *             未创建或已销毁时安全 no-op。
 * @param      window 目标窗口借用指针；可为 NULL。
 */
void XPlatformNativeWindow_destroy(XWindow* window);

/* ==================== 窗口属性同步（平台后端提供） ==================== */

/**
 * @brief      映射/取消映射真实原生窗口。
 * @details    X11 用 XMapWindow / XUnmapWindow（映射后 X 服务器经事件泵
 *             回送 Expose/MapNotify）；Win32 用 ShowWindow(SW_SHOW/SW_HIDE)
 *             并配合 WM_SHOWWINDOW 注入。未创建窗口时安全 no-op。
 * @param      window  目标窗口借用指针；可为 NULL。
 * @param      visible true 映射显示、false 取消映射隐藏。
 * @return     true 已请求平台显示/隐藏；false 入参非法或平台不可用。
 */
bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible);

/**
 * @brief      同步窗口几何到真实原生窗口。
 * @details    X11 用 XMoveResizeWindow；Win32 用 SetWindowPos。实现内部
 *             按原生窗口当前几何去重（相同则跳过），从源头上避免
 *             ConfigureNotify/WM_SIZE 回注造成的递归震荡。
 * @param      window   目标窗口借用指针；可为 NULL。
 * @param      geometry 新几何（设备无关像素）；可为 NULL 时忽略。
 * @return     true 已同步；false 入参非法或平台不可用。
 */
bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry);

/**
 * @brief      同步窗口标题到真实原生窗口。
 * @details    X11 用 XStoreName（UTF-8）；Win32 将 UTF-8 转 UTF-16 后
 *             SetWindowTextW。未创建窗口时安全 no-op。
 * @param      window 目标窗口借用指针；可为 NULL。
 * @param      title  新标题（UTF-8）；可为 NULL 视为空标题。
 * @return     true 已同步；false 入参非法或平台不可用。
 */
bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title);

/** @brief 启用/禁用真实窗口的键盘抓取（对标 QPlatformWindow）。 */
bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab);

/** @brief 启用/禁用真实窗口的鼠标抓取（对标 QPlatformWindow）。 */
bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab);

/** @brief 请求真实窗口激活并获得输入焦点（对标 QPlatformWindow）。 */
bool XPlatformNativeWindow_requestActivate(XWindow* window);

/**
 * @brief      抓取真实屏幕或窗口区域（对标 QScreen::grabWindow）。
 * @param      window 原生窗口 id；0 表示整个虚拟屏幕。
 * @param      x/y    抓取起点；窗口抓取时为窗口客户区坐标。
 * @param      w/h    尺寸；负值表示延伸到目标右下边界。
 * @return     新建像素图；平台不可用或抓取失败返回 NULL。
 */
XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                           int x, int y, int w, int h);

/* ==================== 原生句柄与反查（平台后端提供） ==================== */

/**
 * @brief      返回窗口的真实原生 WId。
 * @details    X11 为 X Window id（无符号长整数），Win32 为 HWND；
 *             platform 未创建或未连接到窗口系统时返回 0。
 * @param      window 目标窗口借用指针；可为 NULL。
 * @return     真实原生 WId；不可用时返回 0。
 */
XWindowId XPlatformNativeWindow_winId(const XWindow* window);

/**
 * @brief      按真实原生 WId 反查登记的 XWindow。
 * @details    平台注册表线性/表驱查找；命中返回借用指针，未命中返回
 *             NULL。供 XWindow_fromWinId 与平台层的窗口命中测试使用。
 * @param      id 真实原生 WId（X11 Window / Win32 HWND）。
 * @return     对应 XWindow 借用指针；未登记或入参非法返回 NULL。
 */
XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id);

/* ==================== 原生事件泵（平台后端提供） ==================== */

/**
 * @brief      非阻塞处理当前全部待决原生事件。
 * @details    X11 用 XPending 循环 XNextEvent；Win32 用 PeekMessage 循环
 *             PM_REMOVE。每个事件翻译后经 XWindowSystemInterface.handle*
 *             注入（同步自发投递），形成「平台事件 -> 窗口事件槽 -> 重绘
 *             -> XBackingStore flush」闭环。嵌入式/无连接时恒返回 false。
 * @return     true 本次处理并注入了至少一个事件；false 无事件或不可用。
 */
bool XPlatformNativeWindow_processPendingEvents(void);

/**
 * @brief      阻塞等待原生事件，就绪后处理一批并返回。
 * @details    X11 用 poll(XConnectionNumber, POLLIN, msec)；Win32 用
 *             MsgWaitForMultipleObjects(QS_ALLINPUT, msec)。超时/被打断
 *             返回 false；就绪则调用 processPendingEvents 的语义后返回
 *             true。用于「无系统 QAbstractEventDispatcher 事件源」的自绘
 *             主循环（XGuiApplication_waitForEvents 转发入口）。
 * @param      maxMilliseconds 最大阻塞毫秒；0 表示只做一次立即探测。
 * @return     true 就绪并注入了事件；false 超时、被打断或不可用。
 */
bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds);

/**
 * @brief      查询原生输入设备当前按下的修饰键。
 * @details    此接口是 XPlatformIntegration 键盘查询的后端契约：Linux X11
 *             从 Display 的键盘映射读取，Windows 从 GetKeyState 读取；不具备
 *             原生输入后端的嵌入式实现返回 false，调用方据此回退事件缓存。
 *             公共 GUI 层不得直接调用平台 API。
 * @param      outModifiers 成功时写入 XKeyboardModifiers；不可为 NULL。
 * @return     true 已获得即时设备状态；false 后端不可用或参数无效。
 */
bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers);

/* ==================== 上屏（平台后端提供） ==================== */

/**
 * @brief      把软件帧缓冲的脏区提交到窗口真实屏幕。
 * @details    X11：优先选择 32 位深 visual 时缓冲可直拷；否则按服务器
 *             字节序把 ARGB32 重排为 24 位后 XPutImage。Win32：把
 *             XImage 行同步进等宽 32 位自顶向下 DIB 后 BitBlt 到窗口 DC。
 *             region 为窗口本地坐标脏区；offset 为缓冲相对窗口的偏移。
 *             由 XPlatformBackingStore 平台后端在 flush 检测到真实 WId
 *             时自动调用；也可供平台层直接驱动离屏合成。
 * @param      window 目标窗口借用指针；可为 NULL。
 * @param      image  软件帧缓冲（ARGB32 预乘 Alpha）；可为 NULL。
 * @param      region 脏区集合；为 NULL/空按整张缓冲处理。
 * @param      offset 缓冲相对窗口的偏移；可为 NULL 按零点处理。
 * @return     true 至少提交了一个区域；false 入参非法或平台不可用。
 */
bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region,
                                   const XPoint* offset);

/* ==================== 原生连接（平台后端提供） ==================== */

/**
 * @brief      返回平台原生连接句柄与类型。
 * @details    X11 返回 Display*（类型 X11）；Win32 返回应用实例
 *             HINSTANCE（类型 Win32）。供 XPlatformNativeInterface 的
 *             "display"/"hinstance" 资源查询；未连接时返回 NULL。
 * @param      outType 输出连接类型；可为 NULL。
 * @return     原生连接句柄；未连接或入参非法返回 NULL。
 */
void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType);

#endif /* XPLATFORMNATIVEWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMNATIVEWINDOW_H */
