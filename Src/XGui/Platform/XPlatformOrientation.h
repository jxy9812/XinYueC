/******************************************************************************
 * @file       XPlatformOrientation.h
 * @brief      平台显示方向契约（申请「显示方向」变更 + 回读当前方向）。
 * @details    契约格式与 XPlatformScreen / XPlatformPower 同款：本头只
 *             声明统一签名，各平台在 Drive 目录用同名函数实现（安卓
 *             Activity.setRequestedOrientation、win32 DEVMODE
 *             dmDisplayOrientation、X11 Xrandr 屏幕旋转），Unsupported
 *             哨兵安全存根兜底，链接期按平台选择——平台 API 只允许出现
 *             在 Drive 目录。
 *
 *             【与「画布旋转」是两件事，勿混谈】
 *               - 本契约管的是**显示方向**：物理显示器的朝向，变更后
 *                 系统 UI（状态栏/导航栏）随之转，窗口尺寸随之变，
 *                 应用侧表现为「收到一次 resize」。
 *               - 框架已有的「画布旋转」（XWindow_setContentRotation /
 *                 XWidget_setContentRotation）管的是**窗口内容朝向**：
 *                 应用自己在 present 侧逐像素转置，系统完全不知情，
 *                 适合「只转这一个窗口/这块画布」的场景（桌面多窗口、
 *                 弹窗内的斜置仪表盘）。
 *               两者正交：转屏时画布旋转角应为 0°（内容本来就正了）。
 *
 *             【角度口径】
 *               统一用「自横屏起顺时针的度数」：0=横屏、90=竖屏、
 *               180=倒置横屏、270=倒置竖屏——与框架画布旋转的度数
 *               口径一致，两个下拉框的档位可直接对齐。
 *               XPLATFORM_ORIENTATION_AUTO(-1) 表示「跟随系统/传感器」
 *               （交回系统决定），是本契约唯一的负值。
 *
 *             【能力诚实原则】
 *               能力位由各平台如实上报：做不到就报 0，调用方据此把控件
 *               置灰——不编造能力（见 XPlatformDisplayDriver.h 口径）。
 *               典型受限场景：
 *               - fbdev 直写面板：面板朝向是硬件/驱动属性，工业屏驱动
 *                 多数不支持 rotate，无通用原语 → 报 0；
 *               - 安卓大屏（API 36+ / 大尺寸设备）：系统会忽略应用的
 *                 方向请求 → 此时报「不支持」，由调用方降级。
 * @note       随 XGUI_ON 门控（对标 XPlatformPower）；本头只放类型与
 *             声明，实现见同名 .c 与 Drive 侧（仓库头文件结构规范）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMORIENTATION_H
#define XPLATFORMORIENTATION_H
#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

/** @brief 「跟随系统/传感器」档（交回系统决定显示方向）。
 *  @note  本契约唯一的负值；其余合法角度只有 0/90/180/270。 */
#define XPLATFORM_ORIENTATION_AUTO (-1)

/** @brief 显示方向能力位（各位独立探测，位与位之间不互斥）。 */
typedef enum {
    XPlatformOrientationCap_Request = 1u << 0, /**< 支持**申请**指定显示方向
                                               *   （0/90/180/270）。 */
    XPlatformOrientationCap_Auto    = 1u << 1, /**< 支持恢复「跟随系统/传感器」
                                               *   （AUTO 档）。 */
    XPlatformOrientationCap_Query   = 1u << 2  /**< 支持**回读当前实际**显示
                                               *   方向。 */
} XPlatformOrientationCapability;

/* ==================== 平台钩子（Drive 实现，Unsupported 哨兵兜底） ==================== */

/** @brief 本平台支持的显示方向能力位集合。
 *  @return XPlatformOrientationCapability 的按位或组合；**0=本平台不
 *          支持任何显示方向变更**（含 Activity 未就绪等未就绪态，
 *          调用方按不支持退化，无需另设 isAvailable 探针）。 */
unsigned XPlatformOrientationDriver_capabilities(void);

/** @brief 申请显示方向变更。
 *  @param degrees 目标角度（0/90/180/270），或 XPLATFORM_ORIENTATION_AUTO
 *                 表示「跟随系统/传感器」。
 *  @return true=申请已被平台受理；false=平台不支持或申请失败。
 *  @note   受理≠立即生效：系统转屏是异步的（安卓先重建 surface、桌面
 *           先改显示模式），应用侧表现为随后收到一次 resize。 */
bool XPlatformOrientationDriver_setOrientation(int degrees);

/** @brief 回读当前**实际**显示方向。
 *  @return 角度（0/90/180/270）；未知或平台不支持回读时返回 -1。
 *  @note   与「本应用最近一次申请值」（XPlatformOrientation_lastRequested）
 *          是两个概念：本函数答「屏幕现在朝哪」，后者答「我们要求过
 *          朝哪」。 */
int XPlatformOrientationDriver_currentOrientation(void);

/* ==================== 公共契约（应用层唯一可依赖面） ==================== */

/** @brief 本平台支持的显示方向能力位集合（公共转发；0=无任何能力）。 */
unsigned XPlatformOrientation_capabilities(void);

/** @brief 查询单项能力是否受支持。
 *  @param cap 能力项。
 *  @return true=支持；false=不支持（调用方据此把控件置灰）。 */
bool XPlatformOrientation_supports(XPlatformOrientationCapability cap);

/** @brief 申请显示方向变更。
 *  @param degrees 目标角度（0/90/180/270），或 XPLATFORM_ORIENTATION_AUTO
 *                 表示「跟随系统/传感器」。
 *  @return true=申请已被受理；false=参数非法、平台不支持或申请失败。
 *  @details 受理后公共层记录影子值（供界面回显）；平台若随后未真的
 *           转（部分系统忽略方向请求），实际方向以
 *           XPlatformOrientation_currentOrientation 的回读为准。 */
bool XPlatformOrientation_setOrientation(int degrees);

/** @brief 回读当前**实际**显示方向。
 *  @return 角度（0/90/180/270）；未知时返回 -1。
 *  @note   平台不支持回读时恒返回 -1——调用方应改用
 *          XPlatformOrientation_lastRequested 的影子值回显。 */
int XPlatformOrientation_currentOrientation(void);

/** @brief 回读本应用最近一次**成功受理**的方向申请（影子值）。
 *  @return 角度（0/90/180/270）或 XPLATFORM_ORIENTATION_AUTO（跟随系统）；
 *          从未申请过时返回 XPLATFORM_ORIENTATION_AUTO。
 *  @note   影子值只反映「我方申请过什么」，不保证平台真的转成了那样
 *          （系统可能忽略）；它是界面回显的权威来源。 */
int XPlatformOrientation_lastRequested(void);

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XPLATFORMORIENTATION_H */
