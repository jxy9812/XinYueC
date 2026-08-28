/******************************************************************************
 * @file       XIconStyleHelper.h
 * @brief      XIcon 样式态生成内部辅助（对标 Qt 6.8 QApplicationPrivate::
 *             applyQIconStyleHelper / QCommonStyle::generatedIconPixmap）。
 * @author     XinYueC 团队
 * @note       本模块不对外暴露公共 API，仅供 XIcon 默认像素图引擎和
 *             XIconThemeEngine 在启用/禁用/选中/激活态取图路径上复用同一份
 *             样式态生成规则。
 ******************************************************************************/
#ifndef XICONSTYLEHELPER_H
#define XICONSTYLEHELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XIcon.h"
#include "XPixmap.h"

/**
 * @brief 生成当前应用调色板的缓存标识。
 * @return 调色板各颜色单元的组合哈希；应用调色板不可用时返回 0。
 * @note 用于缩放像素图缓存键，使调色板变化后禁用/选中态结果不会被旧缓存复用。
 */
uint64_t XIconStyleHelper_paletteCacheKey(void);

/**
 * @brief 按 Qt 样式助手规则生成图标的显示模式像素图。
 * @param mode  目标显示模式；Normal/Active 不会改变像素内容。
 * @param base  源像素图，未缩放或已缩放到最终物理尺寸。
 * @param out   输出像素图；调用前必须已初始化或为已释放状态。
 * @note 仅 Disabled/Selected 会实际改变像素；失败时输出空像素图。
 */
void XIconStyleHelper_apply(XIconMode mode, const XPixmap* base, XPixmap* out);

#ifdef __cplusplus
}
#endif

#endif /* XICONSTYLEHELPER_H */
