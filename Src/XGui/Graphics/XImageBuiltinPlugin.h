/******************************************************************************
 * @file       XImageBuiltinPlugin.h
 * @brief      XImageCodec 内置图像插件（对标 Qt 6.8 图像格式插件的注册方式）。
 * @details    通过 XImageIOPlugin 接口把 XImageCodec 支持的 BMP/PNG/JPEG/
 *              GIF/PPM/XBM/SVG 统一暴露给 XImagePluginRegistry，使 XImageReader 与
 *              XImageWriter 可以像加载外部插件一样按注册表发现内置算法。
 ******************************************************************************/
#ifndef XIMAGEBUILTINPLUGIN_H
#define XIMAGEBUILTINPLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XImageIOPlugin.h"

/**
 * @brief 获取内置图像插件的全局单例。
 * @details 单例由本模块静态持有，调用方不得释放；注册表会自动加入该插件。
 *          当 XIMAGEIOPLUGIN_ON 或 XIMAGECODEC_ON 关闭时返回 NULL。
 * @return 内置 XImageIOPlugin 指针；不可用时返回 NULL。
 */
XImageIOPlugin* XImageBuiltinPlugin_instance(void);

#ifdef __cplusplus
}
#endif

#endif /* XIMAGEBUILTINPLUGIN_H */
