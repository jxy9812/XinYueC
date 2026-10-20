/**
 * @file XFontFt_config.h
 * @brief freetype 字体后端裁剪配置。
 * @details 由本模块的公开头 XFontFt.h 包含，产品配置（如 ESP8266 裁剪档案）
 *          通过在编译命令中预先定义同名宏即可覆盖，本文件只提供默认值。
 */

#ifndef XFONTFTCONFIG_H
#define XFONTFTCONFIG_H

/** @brief 依赖 freetype 的字体后端；桌面默认开，嵌入式裁剪置 0。
 *  @note 置 0 后 XFontFt.h 整段裁空，不解析 ft2build.h / freetype 头。 */
#ifndef XFONT_FREETYPE_ON
#define XFONT_FREETYPE_ON 1
#endif


#endif /* XFONTFTCONFIG_H */
