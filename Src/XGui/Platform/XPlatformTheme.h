/****************************************************************************
 * @file       XPlatformTheme.h
 * @brief      平台主题快照接口。
 * @details    主题名称与深色偏好由 Drive 探测，公共层不暴露系统主题类型。
 *             进程级窗口装饰策略（XPlatformThemeDecorationMode，标题栏/
 *             边框由框架自绘 CSD 还是原生窗口管理器绘制）一并宿主本模
 *             块：宿主理由见该枚举的 @note（主题头已有进程级查询先例，
 *             且不被 XPLATFORMNATIVEWINDOW_ON 门控，纯 fbdev 构建同样
 *             可查）。
 ****************************************************************************/
#ifndef XPLATFORMTHEME_H
#define XPLATFORMTHEME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XMemory.h"
#include "XString.h"
#include "XFont.h"
typedef struct XStringList XStringList;
#if XPLATFORMINTEGRATION_ON
/** @brief 色彩方案（对标 Qt::ColorScheme，数值一致）。 */
typedef enum XPlatformThemeColorScheme
{
    XPlatformThemeColorScheme_Unknown = 0, /**< 未知。 */
    XPlatformThemeColorScheme_Light = 1,   /**< 浅色。 */
    XPlatformThemeColorScheme_Dark = 2     /**< 深色。 */
} XPlatformThemeColorScheme;

typedef struct XPlatformTheme XPlatformTheme;
/** @brief 创建主题快照（XString 主版本；name 为 NULL 时用驱动探测名）。 */
XPlatformTheme* XPlatformTheme_create_ex(XMemoryType memory, const XString* name);
/** @brief 创建主题快照（UTF-8 兼容重载，转发主版本）。 */
XPlatformTheme* XPlatformTheme_create_ex_2(XMemoryType memory, const char* name);
#define XPlatformTheme_create(name) \
    XPlatformTheme_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (name))
void XPlatformTheme_destroy(XPlatformTheme* self);
/** @brief 主题名（内部借用 XString*；未设置时返回 NULL，不得释放）。 */
const XString* XPlatformTheme_name(const XPlatformTheme* self);
/** @brief 主题名（UTF-8 借用；未设置时返回 NULL，不得释放）。 */
const char* XPlatformTheme_name_2(const XPlatformTheme* self);
bool XPlatformTheme_isDark(const XPlatformTheme* self);
/** @brief 色彩方案（对标 QPlatformTheme::colorScheme）。 */
int XPlatformTheme_colorScheme(const XPlatformTheme* self);
/** @brief 设置色彩方案（XPlatformThemeColorScheme）。 */
void XPlatformTheme_setColorScheme(XPlatformTheme* self, int scheme);
/** @brief 平台默认字体快照（对标 QPlatformTheme::font）。 */
XFont XPlatformTheme_font(const XPlatformTheme* self);
/** @brief 设置平台默认字体快照。 */
void XPlatformTheme_setFont(XPlatformTheme* self, const XFont* font);
/** @brief 主题提示查询（对标 QPlatformTheme::themeHint；最小快照恒 0）。 */
int64_t XPlatformTheme_themeHint(const XPlatformTheme* self, int hint);
bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity);
/**
 * @brief 获取平台默认的图标搜索路径。
 * @param fallback 是否获取独立回退图标路径；否则获取主题根路径。
 * @param out 接收路径的字符串列表；调用者拥有列表，平台只追加副本。
 * @return 平台提供至少一条路径时返回 true，否则返回 false。
 */
bool XPlatformThemeDriver_iconSearchPaths(bool fallback, XStringList* out);
/**
 * @brief 获取平台默认的系统图标主题名称。
 * @param fallback 是否获取系统回退主题名称。
 * @param name 接收 UTF-8 名称的缓冲区。
 * @param capacity 缓冲区字节容量，包含结尾空字符。
 * @return 成功写入非空名称时返回 true，否则返回 false。
 */
bool XPlatformThemeDriver_iconThemeName(bool fallback, char* name, size_t capacity);
#endif /* XPLATFORMINTEGRATION_ON */

/* ==================== 平台窗口装饰策略（进程级） ====================
 *  命名先例对标 XPlatformDisplayCacheMode -> XPlatformDisplayCache_*：
 *  枚举类型 XPlatformThemeDecorationMode，枚举值/函数同用
 *  XPlatformThemeDecoration_ 前缀。
 *  本节不受 XPLATFORMINTEGRATION_ON 门控：装饰归属是纯进程级策略查询，
 *  实现只依赖 XGuiConfig/XSystem 等既有抽象（见各函数注），且
 *  XWindowDecoration_activeFor 等装饰判定调用点不受平台集成宏守卫——
 *  XGUI_ON=1 且显式 -DXPLATFORMINTEGRATION_ON=0 的裁剪组合同样可查，
 *  否则该合法组合会隐式声明+链接失败（评审返修）。 */

/**
 * @brief      窗口装饰策略模式（当前环境标题栏/边框由谁绘制）。
 * @details    对标 Qt 平台插件的装饰归属选择：桌面会话（X11/Win32）由
 *             原生窗口管理器按 flags 组装原生装饰，无窗口系统平台
 *             （fbdev 直写面板）由框架自绘（Qt 术语 CSD，client-side
 *             decorations）。逐值对标关系：
 *             - Auto      对标 Qt 平台插件自动选择（QPlatformIntegration
 *               按会话能力决定装饰归属；本框架即 fbdev 显示驱动探测：
 *               已注册且探测成功=无 WM 环境→框架自绘，否则→交 WM）；
 *             - Framework 对标强制 CSD（eglfs/linuxfb 无窗口系统路径：
 *               框架即合成器，标题栏/移动/改尺寸全部自绘；对应昆仑通
 *               态无 WM 定版路径；桌面经环境变量 XGUI_CSD=1 或编译默认
 *               XGUI_CSD_DEFAULT 强制时同此模式，平台原生窗口后端按窗
 *               口 CSD 抑制位折叠无边框提示以抑制原生装饰）；
 *             - System    对标交窗口管理器（桌面 QXcbWindow：flags 经
 *               _MOTIF_WM_HINTS 等机制交 WM 组装原生装饰，框架不绘制
 *               任何窗口外壳）。
 *             Auto 只回答"当前环境默认由谁画外壳"的回落口径；逐窗差别
 *             （瞬态类型永不装饰、FramelessWindowHint 等提示位、窗口是
 *             否已挂原生窗）仍由上层装饰/控件层结合窗口 flags 裁量。
 * @note       宿主于主题抽象的理由：主题头已有 themeHint 外观提示查询
 *             与 XPlatformThemeDriver_* 进程级函数先例、已有公共层 .c，
 *             且不被 XPLATFORMNATIVEWINDOW_ON 门控——纯 fbdev 裁剪原生
 *             窗的构建同样可查。实现只依赖 XGuiConfig/XSystem 等既有抽
 *             象，禁止依赖 Widget/Window 层。
 */
typedef enum XPlatformThemeDecorationMode
{
    XPlatformThemeDecoration_Auto = 0,      /**< 自动选择（按会话能力判定，
                                                 对标 Qt 平台插件自动决策；
                                                 作请求值/运行时未设定值，
                                                 仅在运行时覆盖、环境变量
                                                 与编译默认皆未决时经
                                                 effectiveMode 返回）。 */
    XPlatformThemeDecoration_System = 1,    /**< 原生窗口管理器绘制装饰
                                                 （桌面路径；框架零自绘）。 */
    XPlatformThemeDecoration_Framework = 2  /**< 框架自绘 CSD（无 WM 设备
                                                 常规路径或运行时/编译默认
                                                 强制；平台层应抑制原生
                                                 WM 装饰）。 */
} XPlatformThemeDecorationMode;

/**
 * @brief      运行时设定进程级装饰策略（最高优先级覆盖）。
 * @details    覆盖环境变量 XGUI_CSD 与编译默认 XGUI_CSD_DEFAULT（完整优
 *             先级见 XPlatformThemeDecoration_effectiveMode）。传 Auto
 *             即清除运行时覆盖、回落自动解析链。供应用壳层/调试入口在
 *             运行期切换「桌面强制框架标题栏」与「交 WM」。
 * @param      mode 请求的装饰策略；取值应为枚举成员（Auto=清除覆盖）。
 * @return     无。
 */
void XPlatformThemeDecoration_setMode(XPlatformThemeDecorationMode mode);

/**
 * @brief      查询运行时设定的装饰策略（未经解析的原始请求值）。
 * @details    返回 XPlatformThemeDecoration_setMode 最近一次设定值；
 *             从未设定或已传 Auto 清除时返回 Auto。与
 *             XPlatformThemeDecoration_effectiveMode（解析后归属）互补。
 * @return     运行时设定值；未设定为 Auto。
 */
XPlatformThemeDecorationMode XPlatformThemeDecoration_mode(void);

/**
 * @brief      查询当前进程的窗口装饰归属（按优先级链解析结论）。
 * @details    优先级：运行时 setMode > 环境变量 XGUI_CSD（"1"→
 *             Framework、"0"→System；经 XSystem_environment 只在首次调
 *             用读取并缓存——读一次缓存语义，进程内决策不随环境翻转，
 *             原 XWindowDecoration 的 xwd_forceMode 逻辑迁入）> 编译默
 *             认 XGUI_CSD_DEFAULT（非 0=桌面强制框架自绘）> Auto（回落
 *             值：由调用方按 fbdev 显示驱动注册与探测结果裁量，探测成
 *             功=无 WM 环境→框架自绘，否则→交 WM；参见
 *             XWindowDecoration_activeFor 的自动分支）。
 * @return     解析后的装饰归属；运行时覆盖、环境变量与编译默认皆未决
 *             时返回 Auto，否则返回 System 或 Framework。
 * @note       环境变量缓存为一次写入的只读快照，本函数无其它副作用；
 *             逐窗装饰判定仍以上层结合窗口 flags 的裁定为准。
 */
XPlatformThemeDecorationMode XPlatformThemeDecoration_effectiveMode(void);
#ifdef __cplusplus
}
#endif
#endif
