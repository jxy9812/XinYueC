/**
 * @file       HsfSettingsRemote.h
 * @brief      远端设置叶子页：运行模式 / 自动运行（含急停复位）/ 自动模式
 *             配置 / 采集配置 / 数据存储 / 登录策略。
 * @details    [2026-11-10] 本页早已实装**（不是占位）：模式切换、自动
 *             运行启停、7 行采集开关+周期（刷新/应用）、数据存储 6 项 +
 *             容量占用条 + 用量实况、登录策略 3 项 + 应用、按服务端快照
 *             回填。原先页头写的「占位/真实内容后续替换」是历史遗留，会
 *             误导接手的人。
 *             [2026-11-11] 自动运行补急停/复位（op=3/4）；新增自动模式
 *             配置卡（产水/反洗/顺洗三模式：启动设备勾选 + 搅拌电机
 *             转速/方向，应用走 0x0117 auto 节）。
 *             [2026-11-12 形态对齐 Python pages_other.py:_build_auto_mode_block]
 *             每个模式块改成竖排三段：块标题 / 行1「启动设备」**多选下拉
 *             按钮 + 勾选浮层**（公共组件 HsfMultiSelect，取代 8 个平铺
 *             复选框的 4×2 网格）/ 行2「搅拌电机」转速 + 折算 Hz 提示 +
 *             方向；两行标签列宽同基准（Python minWidth 110）。功能面
 *             （auto 节下发/回显、局部应答逐节守卫、权限闸）未变。
 */
#ifndef HSFSETTINGSREMOTE_H
#define HSFSETTINGSREMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建远端设置叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfSettingsRemote_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      远端设置页窄宽自适应（帧泵/切页/形态切换驱动，幂等）。
 * @param      page 页根控件（= create 返回值，内部强转回本页结构）。
 * @param      compact true=窄档（窗口 <HSF_COMPACT_MAX_W）：收边距+压缩策略下拉。
 * @return     无。
 * @note       2026-11-09 竖屏适配批次补齐（原「固定五卡，无窄宽分档」登记缺口）。
 */
void HsfSettingsRemote_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSETTINGSREMOTE_H */
