/******************************************************************************
 * @file       XStyle_Protected.h
 * @brief      嵌入式样式受保护面（对标 Qt 6.8 QStyle 的 protected/private 面）。
 * @details    Qt 中 QStyle::setName 为 \internal 私有接口
 *             （qtbase/src/widgets/styles/qstyle.cpp:403-407 标注
 *             "\internal Set the style name"），不属于公开 API；X 侧
 *             把它从公开头 XStyle.h 下沉到本保护面，仅供样式实现
 *             （XCommonStyle/XFusionStyle/XWindowsStyle/XStyleSheetStyle）
 *             内部写入样式键使用，公开调用方不得直接依赖。
 *             公开可见的部分只有 XStyle_name()（读样式键）。
 * @note       本头不对外承诺 ABI 稳定性；随样式实现演进可增删。
 ******************************************************************************/
#ifndef XSTYLE_PROTECTED_H
#define XSTYLE_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "XGuiConfig.h"

#if XSTYLE_ON

typedef struct XStyle XStyle; /**< 前置声明（接口签名用）。 */

/**
 * @brief 写入样式键（对标 QStyle::setName，Qt 侧为 \internal 私有接口）。
 *
 *        Qt 侧由 QStyleFactory 建样式时调用（qstylefactory.cpp:78-80
 *        `ret->setName(style)`），键用于 QStyleFactory::create() 回环。
 *        X 侧无工厂层，内置样式在各自 init 中直接写入 "fusion"/
 *        "windows"；本接口为受保护面，供样式实现内部调用。
 *
 * @param self 目标样式指针；可为 NULL（无操作）。
 * @param name 样式键（借用静态串，不转移所有权；NULL=清除）。
 * @return 无返回值。
 */
void XStyle_setName(XStyle* self, const char* name);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSTYLE_PROTECTED_H */
