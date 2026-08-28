/** @file XLayout_config.h
 * @brief XGui XLayout 布局系统模块配置文件。
 * @note  通过本文件可以逐类裁剪布局系统，仅保留实际需要的布局类型以
 *        减小嵌入式固件体积：
 *          1. XLAYOUT_ON        - 总开关（XGuiConfig.h 统一定义）；
 *          2. XLAYOUT_BOX_ON    - XBoxLayout（水平/垂直盒式布局；
 *                                对标 Qt 6.8 QBoxLayout/QHBoxLayout/
 *                                QVBoxLayout）；
 *          3. XLAYOUT_GRID_ON   - XGridLayout（行列伸缩网格布局；
 *                                对标 Qt 6.8 QGridLayout）；
 *          4. XLAYOUT_STACKED_ON - XStackedLayout 堆叠布局；
 *          5. XLAYOUT_SPACER_ON - 公开空白条目 XSpacerItem 工厂/查询
 *                                （对标 Qt 6.8 QSpacerItem）。
 *        基础条目类 XLayoutItem 与抽象基类 XLayout 随总开关 XLAYOUT_ON
 *        一起裁剪，不设独立开关。关闭某个布局类型开关后：
 *          - 对应公共头文件整体保留头文件保护壳，但类声明与全部 API
 *            被条件编译裁剪，任何引用都会触发“类型未声明”编译错误，
 *            提醒调用方同步裁剪引用（若使用灵活编译目录扫描不适合
 *            C 预处理器友好降级，统一采用此“硬裁剪”语义）；
 *          - 对应 .c 实现整段不参与编译，静态符号不会进入固件。
 *        布局系统只依赖 XContainer/XGeometry 等库内抽象，编译通过后
 *        不产生任何平台 API 调用，嵌入式可直接使用。
 */

#ifndef XLAYOUT_CONFIG_H
#define XLAYOUT_CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XLAYOUT_ON 主开关已定义 */
#include "XGuiConfig.h"

/* ========================================================================== */
/*                        模块总开关                                          */
/* ========================================================================== */
/** @brief XLayout 模块总开关；置 0 时裁剪整个布局系统公共 API 与全部
 *  子布局类型。总开关在 XGuiConfig.h 中统一定义，此处仅兜底默认值。
 */
#ifndef XLAYOUT_ON
#define XLAYOUT_ON 1
#endif

#if XLAYOUT_ON

/* ========================================================================== */
/*                        各布局类型开关                                      */
/* ========================================================================== */

/** @brief XBoxLayout 盒式布局开关；置 0 时裁剪 XBoxLayout 整类公共 API。
 *  XBoxLayout 对标 Qt 6.8 QBoxLayout，提供水平（左到右/右到左）与垂直
 *  （上到下/下到上）单行盒式布局，支持 stretch 比例、间距、边距、
 *  对齐与 heightForWidth 解算。 */
#ifndef XLAYOUT_BOX_ON
#define XLAYOUT_BOX_ON 1
#endif

/** @brief XGridLayout 网格布局开关；置 0 时裁剪 XGridLayout 整类公共 API。
 *  XGridLayout 对标 Qt 6.8 QGridLayout，提供任意行列、跨行跨列、行列
 *  伸缩、行列最小高度/宽度、行列间距与原点角。 */
#ifndef XLAYOUT_GRID_ON
#define XLAYOUT_GRID_ON 1
#endif

/** @brief XStackedLayout 堆叠布局开关；置 0 时裁剪该类公共 API。
 *  XStackedLayout 对标 Qt 6.8 QStackedLayout，提供页面插入、当前页
 *  切换、StackOne/StackAll 可见性策略以及堆叠页面尺寸协商。 */
#ifndef XLAYOUT_STACKED_ON
#define XLAYOUT_STACKED_ON 1
#endif

/** @brief 公开空白条目（XSpacerItem）工厂/查询开关。
 *  XSpacerItem 对标 Qt 6.8 QSpacerItem。置 1 时提供 XSpacerItem_create /
 *  XSpacerItem_changeSize / XSpacerItem_sizePolicy 与 XBoxLayout 的
 *  addSpacerItem / insertSpacerItem（以及 addStrut 等便利入口）。
 *  置 0 时仅裁剪这些“手动创建/修改空白”的公开 API 与对应实现；空白
 *  结构体、XSpacerItem_init / 虚表与盒式布局内部自动空白（addSpacing /
 *  addStretch / insertSpacing / insertStretch / addStrut）仍随总开关
 *  XLAYOUT_ON 编译，保证布局算法完整可用（嵌入式裁剪时推荐关闭）。
 */
#ifndef XLAYOUT_SPACER_ON
#define XLAYOUT_SPACER_ON 1
#endif

/** @brief PC 桌面扩展 API 开关（对标 QLayout 的 total* 合计尺寸、
 *  setMenuBar/menuBar、setEnabled/isEnabled、unsetContentsMargins/
 *  contentsRect、replaceWidget、closestAcceptableSize）。
 *  置 1（默认）时完整提供与 Qt 6.8 对齐的全部公开接口，适合 PC 桌面
 *  完整功能；置 0 时裁剪这些非核心扩展 API 与对应实现，布局核心
 *  （条目管理/尺寸协商/几何分配/内容边距/间距/对齐）保持不变，
 *  嵌入式裁剪时推荐关闭。 */
#ifndef XLAYOUT_TOTAL_ON
#define XLAYOUT_TOTAL_ON 1
#endif

#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLAYOUT_CONFIG_H */
