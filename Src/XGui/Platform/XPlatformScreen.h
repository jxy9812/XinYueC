/******************************************************************************
 * @file       XPlatformScreen.h
 * @brief      XPlatformScreen 屏幕信息源平台契约（对标 Qt 6.8
 *             QPlatformScreen 的逻辑 DPI/devicePixelRatio 查询面）。
 * @details    契约格式与 XPlatformNativeWindow 同款：本头只声明统一
 *             函数签名，各平台在 Drive 目录用同名函数实现（安卓 JNI
 *             DisplayMetrics、win32 GetDpiForMonitor、X11 Xft.dpi/RandR、
 *             fbdev/嵌入式面板参数），Unsupported 哨兵安全存根兜底，
 *             链接期按平台选择——平台 API 只允许出现在 Drive 目录。
 *             职责边界：平台层从各自 OS 读数并实现本契约；公共层
 *             （XScreen/WSI）只经本契约拉取，不感知平台。与 WSI 推送
 *             通道（handleScreenAdded/handleScreenGeometryChange/
 *             handleScreenLogicalDotsPerInchChange）的关系：推送管
 *             "运行期变化通知"（wm density 切换、显示器热插拔），本契约
 *             管"按需拉取"（surface 标定、触摸原点换算、最大化目标
 *             几何）——两路并存，对标 Qt 的 QWindowSystemInterface 推送
 *             + QPlatformScreen 虚函数拉取双通道。
 *             last-known-good（为什么）：密度是设备常量、原点/窗口尺寸
 *             仅在窗口化↔最大化切换时缓变；Activity/显示状态过渡期读数
 *             会抖动，实现应兜底上次成功值——失败沿旧值远好于回落 0/160
 *             （0 会把屏幕坐标当窗口本地坐标用，160/1.0 会让整套 DPI
 *             缩放静默失效，实测教训见 XSystemAndroid.c 全局引用修复
 *             注释；参照安卓实现的缓存纪律）。
 * @note       随 XGUI_ON 门控；桌面/无对应能力的平台由
 *             XPlatformScreen_unsupported.c 哨兵返回 false（调用方按
 *             自身安全退化链处理）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMSCREEN_H
#define XPLATFORMSCREEN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

#if XGUI_ON

/**
 * @brief      密度/物理 DPI 读数包（一次 queryDpi 的输出）。
 * @note       m_valid=false 表示本次为 last-known-good 兜底值（缓存来自
 *             历史成功读数）；从未成功过时 queryDpi 直接返回 false，
 *             调用方按自身安全退化链处理（安卓 160/1.0 + 密度无效位）。
 */
typedef struct XPlatformScreenDpiInfo
{
    float m_density;        /**< 设备像素比 dpr（DisplayMetrics.density 等）。 */
    int   m_densityDpi;     /**< 密度整档（诊断用，如 240/160/420）。 */
    int   m_xdpi;           /**< 物理 X DPI（0=未知）。 */
    int   m_ydpi;           /**< 物理 Y DPI（0=未知）。 */
    bool  m_valid;          /**< true=平台本次实时读数；false=缓存兜底。 */
} XPlatformScreenDpiInfo;

/**
 * @brief      本平台是否提供屏幕信息源（安卓/桌面窗口系统 true）。
 * @return     true 有实现；false Unsupported 哨兵（调用方走安全退化）。
 * @note       对标 XPlatformNativeWindow_isAvailable 的哨兵探测口径。
 */
bool XPlatformScreen_isAvailable(void);

/**
 * @brief      强制 DPI 切口：XGUI_FORCE_DPI>0 时替换平台原生 dpr。
 * @details    全平台 dpr 采集的统一切口（对标 Qt QT_SCALE_FACTOR 的
 *             编译期版，替换语义）：各平台后端取得原生 dpr 后立即调用，
 *             下游（几何÷dpr、触摸÷dpr、present 放大、XScreen 上报）
 *             自动随动且平台内自洽。宏为 0（默认）时原样返回，零开销。
 * @param      nativeDpr 平台原生设备像素比（已含平台侧钳位/吸附）。
 * @return     生效 dpr（强制值或原值）。
 */
static inline float XPlatformScreen_applyDpiOverride(float nativeDpr)
{
    /* 浮点宏值不能进 #if 表达式（floating point literal in preprocessor
       expression），改运行期比较：未强制时宏为 0，>0 判否直通。 */
    const float forced = (float)(XGUI_FORCE_DPI + 0);
    return (forced > 0.0f) ? forced : nativeDpr;
}

/**
 * @brief      查询密度/物理 DPI（核心能力；last-known-good 兜底）。
 * @param      out 输出包；不可为 NULL。
 * @return     true 读数可用（实时或缓存兜底）；false 从未成功/哨兵。
 */
bool XPlatformScreen_queryDpi(XPlatformScreenDpiInfo* out);

/**
 * @brief      查询内容视图在屏幕上的原点（last-known-good 兜底）。
 * @details    触摸屏幕坐标→窗口本地坐标换算用（不同模拟器/设备
 *             MotionEvent 坐标空间不一致，见安卓实现注释）。
 * @return     true 读数可用；false 平台无此概念/从未成功/哨兵。
 */
bool XPlatformScreen_queryOrigin(int* outX, int* outY);

/**
 * @brief      查询窗口逻辑口径尺寸（last-known-good 兜底）。
 * @details    最大化目标几何 / surface 缓冲对齐用。
 * @return     true 读数可用；false 平台无此概念/从未成功/哨兵。
 */
bool XPlatformScreen_queryFrameSize(int* outWidth, int* outHeight);

/* 契约边界说明：本头只承载【下行拉取面】（XPlatform* 前缀 = Drive 平台
   层实现，链接期选择）。屏幕的【上行推送面】（运行期变化通知：屏幕
   接入/移除/几何变化/逻辑 DPI 变化）是框架注入漏斗，实现位于公共层
   XWindowSystemInterface.c，声明与调用约定见
   Src/XGui/Window/XWindowSystemInterface.h 的 handleScreen* 四入口——
   XWindowSystemInterface_ 前缀即"框架 API、平台调用"，与 XPlatform*
   "平台 API、框架调用"互为反向，两类不得混入同一契约头（抽象必要性
   依赖此前缀纪律）。平台后端的完整屏幕业务 = 实现本头拉取面 + 在变化
   时刻调用 XWSI 推送面。 */

#endif /* XGUI_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMSCREEN_H */
