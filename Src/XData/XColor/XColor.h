/******************************************************************************
 * @file       XColor.h
 * @brief      XColor 颜色类（对标 Qt 6.8 QColor）
 * @author     XinYueC 团队
 * @note       提供 RGB/HSV/CMYK/HSL 色彩空间表示与转换，支持 140+ SVG 命名颜色
 ******************************************************************************/
#ifndef XCOLOR_H
#define XCOLOR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========== 颜色规格枚举（对标 Qt 6.8 QColor::Spec） ========== */

/**
 * @brief      颜色规格枚举
 * @note       表示颜色的色彩空间类型
 */
typedef enum XColor_Spec
{
    XColor_Invalid = 0,     /**< 无效颜色 */
    XColor_Rgb = 1,         /**< RGB 色彩空间 */
    XColor_Hsv = 2,         /**< HSV 色彩空间 */
    XColor_Cmyk = 3,        /**< CMYK 色彩空间 */
    XColor_Hsl = 4,         /**< HSL 色彩空间 */
    XColor_ExtendedRgb = 5  /**< 扩展 RGB 色彩空间（宽色域） */
} XColor_Spec;

/* ========== 颜色名称格式枚举（对标 Qt 6.8 QColor::NameFormat） ========== */

/**
 * @brief      颜色名称格式枚举
 * @note       指定 toHexString() 输出的格式
 */
typedef enum XColor_NameFormat
{
    XColor_HexRgb = 0,  /**< #RRGGBB 格式（6 位十六进制） */
    XColor_HexArgb = 1  /**< #AARRGGBB 格式（8 位十六进制） */
} XColor_NameFormat;

/* ========== XColor 结构体 ========== */

/**
 * @brief      XColor 颜色结构体（对标 Qt 6.8 QColor）
 * @note       使用 5 个 uint16_t 分量存储颜色数据（取值范围 0~65535）
 *             对于 Rgb 和 ExtendedRgb 规格：(a, r, g, b, 0)
 *             对于 Hsv 规格：(a, h, s, v, 0)
 *             对于 Cmyk 规格：(a, c, m, y, k)
 *             对于 Hsl 规格：(a, h, s, l, 0)
 */
typedef struct XColor
{
    XColor_Spec m_spec;     /**< 颜色规格（色彩空间类型） */
    uint16_t m_alpha;       /**< Alpha 通道值（0~65535，0=透明，65535=不透明） */
    uint16_t m_comp1;       /**< 分量1：R / H / C / H */
    uint16_t m_comp2;       /**< 分量2：G / S / M / S */
    uint16_t m_comp3;       /**< 分量3：B / V / Y / L */
    uint16_t m_comp4;       /**< 分量4：0 / 0 / K / 0 */
} XColor;

/* ========== 创建与初始化函数 ========== */

/**
 * @brief      创建一个无效的 XColor 对象
 * @return     无效的 XColor 对象（m_spec = XColor_Invalid）
 */
XColor XColor_create(void);

/**
 * @brief      使用整数 RGB 值创建 XColor 对象
 * @param r    红色分量（0~255）
 * @param g    绿色分量（0~255）
 * @param b    蓝色分量（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 * @return     如果 RGB 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_rgb(int r, int g, int b, int a);

/**
 * @brief      使用浮点 RGB 值创建 XColor 对象
 * @param r    红色分量（0.0~1.0）
 * @param g    绿色分量（0.0~1.0）
 * @param b    蓝色分量（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0，默认 1.0）
 * @return     如果 RGB 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_rgbF(float r, float g, float b, float a);

/**
 * @brief      使用整数值的 HSV 值创建 XColor 对象
 * @param h    色相（0~359）
 * @param s    饱和度（0~255）
 * @param v    明度（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 * @return     如果 HSV 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_hsv(int h, int s, int v, int a);

/**
 * @brief      使用浮点 HSV 值创建 XColor 对象
 * @param h    色相（0.0~359.0）
 * @param s    饱和度（0.0~1.0）
 * @param v    明度（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0，默认 1.0）
 * @return     如果 HSV 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_hsvF(float h, float s, float v, float a);

/**
 * @brief      使用整数 CMYK 值创建 XColor 对象
 * @param c    青色（0~255）
 * @param m    品红（0~255）
 * @param y    黄色（0~255）
 * @param k    黑色（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 * @return     如果 CMYK 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_cmyk(int c, int m, int y, int k, int a);

/**
 * @brief      使用浮点 CMYK 值创建 XColor 对象
 * @param c    青色（0.0~1.0）
 * @param m    品红（0.0~1.0）
 * @param y    黄色（0.0~1.0）
 * @param k    黑色（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0，默认 1.0）
 * @return     如果 CMYK 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_cmykF(float c, float m, float y, float k, float a);

/**
 * @brief      使用整数 HSL 值创建 XColor 对象
 * @param h    色相（0~359）
 * @param s    饱和度（0~255）
 * @param l    亮度（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 * @return     如果 HSL 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_hsl(int h, int s, int l, int a);

/**
 * @brief      使用浮点 HSL 值创建 XColor 对象
 * @param h    色相（0.0~359.0）
 * @param s    饱和度（0.0~1.0）
 * @param l    亮度（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0，默认 1.0）
 * @return     如果 HSL 值有效则返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_create_hslF(float h, float s, float l, float a);

/**
 * @brief      使用十六进制 RGB 值创建 XColor 对象（如 0xFF8800）
 * @param rgb  十六进制 RGB 值（如 0xFF8800），Alpha 设为 255
 * @return     对应的 XColor 对象
 */
XColor XColor_create_rgba(uint32_t rgba);

/**
 * @brief      使用十六进制 ARGB 值创建 XColor 对象（如 0x80FF8800）
 * @param argb 十六进制 ARGB 值
 * @return     对应的 XColor 对象
 */
XColor XColor_create_argb(uint32_t argb);

/**
 * @brief      XColor 初始化函数
 * @param self 目标 XColor 对象指针
 * @note       初始化为无效颜色
 */
void XColor_init(XColor* self);

/**
 * @brief      XColor 初始化函数（RGB）
 * @param self 目标 XColor 对象指针
 * @param r    红色分量（0~255）
 * @param g    绿色分量（0~255）
 * @param b    蓝色分量（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 */
void XColor_init_rgb(XColor* self, int r, int g, int b, int a);

/* ========== 查询方法 ========== */

/**
 * @brief      判断颜色是否有效
 * @param self 目标 XColor 对象指针
 * @return     有效返回 true，无效返回 false
 */
bool XColor_isValid(const XColor* self);

/**
 * @brief      获取颜色规格（色彩空间类型）
 * @param self 目标 XColor 对象指针
 * @return     颜色规格枚举值
 */
XColor_Spec XColor_spec(const XColor* self);

/**
 * @brief      将颜色转换为字符串名称（如 "#RRGGBB" 或 "#AARRGGBB"）
 * @param self   目标 XColor 对象指针
 * @param format 名称格式（HexRgb 或 HexArgb）
 * @param out    输出字符串缓冲区（至少 10 字节）
 * @return       指向 out 的指针
 */
char* XColor_toHexString(const XColor* self, XColor_NameFormat format, char* out);

/**
 * @brief      从字符串名称解析颜色（如 "#FF8800"、"red"、"lightblue"）
 * @param name 颜色名称字符串
 * @return     解析成功返回对应的 XColor 对象，否则返回无效颜色
 */
XColor XColor_fromString(const char* name);

/* ========== RGB 分量访问 ========== */

/**
 * @brief      获取 Alpha 通道值（0~255）
 * @param self 目标 XColor 对象指针
 * @return     Alpha 通道值（0~255）
 */
int XColor_alpha(const XColor* self);

/**
 * @brief      设置 Alpha 通道值
 * @param self 目标 XColor 对象指针
 * @param a    Alpha 通道值（0~255）
 */
void XColor_setAlpha(XColor* self, int a);

/**
 * @brief      获取浮点 Alpha 通道值（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点 Alpha 通道值
 */
float XColor_alphaF(const XColor* self);

/**
 * @brief      设置浮点 Alpha 通道值
 * @param self 目标 XColor 对象指针
 * @param a    浮点 Alpha 通道值（0.0~1.0）
 */
void XColor_setAlphaF(XColor* self, float a);

/**
 * @brief      获取红色分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     红色分量值
 */
int XColor_red(const XColor* self);

/**
 * @brief      获取绿色分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     绿色分量值
 */
int XColor_green(const XColor* self);

/**
 * @brief      获取蓝色分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     蓝色分量值
 */
int XColor_blue(const XColor* self);

/**
 * @brief      设置红色分量
 * @param self 目标 XColor 对象指针
 * @param r    红色分量值（0~255）
 */
void XColor_setRed(XColor* self, int r);

/**
 * @brief      设置绿色分量
 * @param self 目标 XColor 对象指针
 * @param g    绿色分量值（0~255）
 */
void XColor_setGreen(XColor* self, int g);

/**
 * @brief      设置蓝色分量
 * @param self 目标 XColor 对象指针
 * @param b    蓝色分量值（0~255）
 */
void XColor_setBlue(XColor* self, int b);

/**
 * @brief      获取浮点红色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点红色分量值
 */
float XColor_redF(const XColor* self);

/**
 * @brief      获取浮点绿色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点绿色分量值
 */
float XColor_greenF(const XColor* self);

/**
 * @brief      获取浮点蓝色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点蓝色分量值
 */
float XColor_blueF(const XColor* self);

/**
 * @brief      设置浮点红色分量
 * @param self 目标 XColor 对象指针
 * @param r    浮点红色分量值（0.0~1.0）
 */
void XColor_setRedF(XColor* self, float r);

/**
 * @brief      设置浮点绿色分量
 * @param self 目标 XColor 对象指针
 * @param g    浮点绿色分量值（0.0~1.0）
 */
void XColor_setGreenF(XColor* self, float g);

/**
 * @brief      设置浮点蓝色分量
 * @param self 目标 XColor 对象指针
 * @param b    浮点蓝色分量值（0.0~1.0）
 */
void XColor_setBlueF(XColor* self, float b);

/**
 * @brief      获取所有 RGB 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param r    输出红色分量指针（可为 NULL）
 * @param g    输出绿色分量指针（可为 NULL）
 * @param b    输出蓝色分量指针（可为 NULL）
 * @param a    输出 Alpha 通道指针（可为 NULL）
 */
void XColor_getRgb(const XColor* self, int* r, int* g, int* b, int* a);

/**
 * @brief      设置所有 RGB 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param r    红色分量（0~255）
 * @param g    绿色分量（0~255）
 * @param b    蓝色分量（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 */
void XColor_setRgb(XColor* self, int r, int g, int b, int a);

/**
 * @brief      获取所有 RGB 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param r    输出浮点红色分量指针（可为 NULL）
 * @param g    输出浮点绿色分量指针（可为 NULL）
 * @param b    输出浮点蓝色分量指针（可为 NULL）
 * @param a    输出浮点 Alpha 通道指针（可为 NULL）
 */
void XColor_getRgbF(const XColor* self, float* r, float* g, float* b, float* a);

/**
 * @brief      设置所有 RGB 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param r    浮点红色分量（0.0~1.0）
 * @param g    浮点绿色分量（0.0~1.0）
 * @param b    浮点蓝色分量（0.0~1.0）
 * @param a    浮点 Alpha 通道（0.0~1.0，默认 1.0）
 */
void XColor_setRgbF(XColor* self, float r, float g, float b, float a);

/* ========== HSV 分量访问 ========== */

/**
 * @brief      获取色相（HSV/HSL，0~359）
 * @param self 目标 XColor 对象指针
 * @return     色相值（0~359），无效颜色返回 0
 */
int XColor_hue(const XColor* self);

/**
 * @brief      获取饱和度（HSV，0~255）
 * @param self 目标 XColor 对象指针
 * @return     饱和度值（0~255）
 */
int XColor_saturation(const XColor* self);

/**
 * @brief      获取 HSV 色相（0~359）
 * @param self 目标 XColor 对象指针
 * @return     HSV 色相值
 */
int XColor_hsvHue(const XColor* self);

/**
 * @brief      获取 HSV 饱和度（0~255）
 * @param self 目标 XColor 对象指针
 * @return     HSV 饱和度值
 */
int XColor_hsvSaturation(const XColor* self);

/**
 * @brief      获取 HSV 明度（0~255）
 * @param self 目标 XColor 对象指针
 * @return     明度值
 */
int XColor_value(const XColor* self);

/**
 * @brief      获取浮点色相（0.0~359.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点色相值
 */
float XColor_hueF(const XColor* self);

/**
 * @brief      获取浮点饱和度（HSV，0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点饱和度值
 */
float XColor_saturationF(const XColor* self);

/**
 * @brief      获取浮点 HSV 色相（0.0~359.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点 HSV 色相值
 */
float XColor_hsvHueF(const XColor* self);

/**
 * @brief      获取浮点 HSV 饱和度（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点 HSV 饱和度值
 */
float XColor_hsvSaturationF(const XColor* self);

/**
 * @brief      获取浮点 HSV 明度（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点明度值
 */
float XColor_valueF(const XColor* self);

/**
 * @brief      获取所有 HSV 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param h    输出色相指针（可为 NULL）
 * @param s    输出饱和度指针（可为 NULL）
 * @param v    输出明度指针（可为 NULL）
 * @param a    输出 Alpha 通道指针（可为 NULL）
 */
void XColor_getHsv(const XColor* self, int* h, int* s, int* v, int* a);

/**
 * @brief      设置所有 HSV 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param h    色相（0~359）
 * @param s    饱和度（0~255）
 * @param v    明度（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 */
void XColor_setHsv(XColor* self, int h, int s, int v, int a);

/**
 * @brief      获取所有 HSV 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param h    输出浮点色相指针（可为 NULL）
 * @param s    输出浮点饱和度指针（可为 NULL）
 * @param v    输出浮点明度指针（可为 NULL）
 * @param a    输出浮点 Alpha 指针（可为 NULL）
 */
void XColor_getHsvF(const XColor* self, float* h, float* s, float* v, float* a);

/**
 * @brief      设置所有 HSV 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param h    浮点色相（0.0~359.0）
 * @param s    浮点饱和度（0.0~1.0）
 * @param v    浮点明度（0.0~1.0）
 * @param a    浮点 Alpha（0.0~1.0，默认 1.0）
 */
void XColor_setHsvF(XColor* self, float h, float s, float v, float a);

/* ========== CMYK 分量访问 ========== */

/**
 * @brief      获取青色分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     青色分量值
 */
int XColor_cyan(const XColor* self);

/**
 * @brief      获取品红分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     品红分量值
 */
int XColor_magenta(const XColor* self);

/**
 * @brief      获取黄色分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     黄色分量值
 */
int XColor_yellow(const XColor* self);

/**
 * @brief      获取黑色分量（0~255）
 * @param self 目标 XColor 对象指针
 * @return     黑色分量值
 */
int XColor_black(const XColor* self);

/**
 * @brief      获取浮点青色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点青色分量值
 */
float XColor_cyanF(const XColor* self);

/**
 * @brief      获取浮点品红分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点品红分量值
 */
float XColor_magentaF(const XColor* self);

/**
 * @brief      获取浮点黄色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点黄色分量值
 */
float XColor_yellowF(const XColor* self);

/**
 * @brief      获取浮点黑色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点黑色分量值
 */
float XColor_blackF(const XColor* self);

/**
 * @brief      获取所有 CMYK 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param c    输出青色指针（可为 NULL）
 * @param m    输出品红指针（可为 NULL）
 * @param y    输出黄色指针（可为 NULL）
 * @param k    输出黑色指针（可为 NULL）
 * @param a    输出 Alpha 指针（可为 NULL）
 */
void XColor_getCmyk(const XColor* self, int* c, int* m, int* y, int* k, int* a);

/**
 * @brief      设置所有 CMYK 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param c    青色（0~255）
 * @param m    品红（0~255）
 * @param y    黄色（0~255）
 * @param k    黑色（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 */
void XColor_setCmyk(XColor* self, int c, int m, int y, int k, int a);

/**
 * @brief      获取所有 CMYK 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param c    输出浮点青色指针（可为 NULL）
 * @param m    输出浮点品红指针（可为 NULL）
 * @param y    输出浮点黄色指针（可为 NULL）
 * @param k    输出浮点黑色指针（可为 NULL）
 * @param a    输出浮点 Alpha 指针（可为 NULL）
 */
void XColor_getCmykF(const XColor* self, float* c, float* m, float* y, float* k, float* a);

/**
 * @brief      设置所有 CMYK 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param c    浮点青色（0.0~1.0）
 * @param m    浮点品红（0.0~1.0）
 * @param y    浮点黄色（0.0~1.0）
 * @param k    浮点黑色（0.0~1.0）
 * @param a    浮点 Alpha（0.0~1.0，默认 1.0）
 */
void XColor_setCmykF(XColor* self, float c, float m, float y, float k, float a);

/* ========== HSL 分量访问 ========== */

/**
 * @brief      获取 HSL 色相（0~359）
 * @param self 目标 XColor 对象指针
 * @return     HSL 色相值
 */
int XColor_hslHue(const XColor* self);

/**
 * @brief      获取 HSL 饱和度（0~255）
 * @param self 目标 XColor 对象指针
 * @return     HSL 饱和度值
 */
int XColor_hslSaturation(const XColor* self);

/**
 * @brief      获取 HSL 亮度（0~255）
 * @param self 目标 XColor 对象指针
 * @return     亮度值
 */
int XColor_lightness(const XColor* self);

/**
 * @brief      获取浮点 HSL 色相（0.0~359.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点 HSL 色相值
 */
float XColor_hslHueF(const XColor* self);

/**
 * @brief      获取浮点 HSL 饱和度（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点 HSL 饱和度值
 */
float XColor_hslSaturationF(const XColor* self);

/**
 * @brief      获取浮点 HSL 亮度（0.0~1.0）
 * @param self 目标 XColor 对象指针
 * @return     浮点亮度值
 */
float XColor_lightnessF(const XColor* self);

/**
 * @brief      获取所有 HSL 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param h    输出色相指针（可为 NULL）
 * @param s    输出饱和度指针（可为 NULL）
 * @param l    输出亮度指针（可为 NULL）
 * @param a    输出 Alpha 指针（可为 NULL）
 */
void XColor_getHsl(const XColor* self, int* h, int* s, int* l, int* a);

/**
 * @brief      设置所有 HSL 分量（整数）
 * @param self 目标 XColor 对象指针
 * @param h    色相（0~359）
 * @param s    饱和度（0~255）
 * @param l    亮度（0~255）
 * @param a    Alpha 通道（0~255，默认 255）
 */
void XColor_setHsl(XColor* self, int h, int s, int l, int a);

/**
 * @brief      获取所有 HSL 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param h    输出浮点色相指针（可为 NULL）
 * @param s    输出浮点饱和度指针（可为 NULL）
 * @param l    输出浮点亮度指针（可为 NULL）
 * @param a    输出浮点 Alpha 指针（可为 NULL）
 */
void XColor_getHslF(const XColor* self, float* h, float* s, float* l, float* a);

/**
 * @brief      设置所有 HSL 分量（浮点）
 * @param self 目标 XColor 对象指针
 * @param h    浮点色相（0.0~359.0）
 * @param s    浮点饱和度（0.0~1.0）
 * @param l    浮点亮度（0.0~1.0）
 * @param a    浮点 Alpha（0.0~1.0，默认 1.0）
 */
void XColor_setHslF(XColor* self, float h, float s, float l, float a);

/* ========== 转换与工具函数 ========== */

/**
 * @brief      将颜色转换为 RGB 色彩空间（如果还不是 RGB）
 * @param self 目标 XColor 对象指针
 * @param out  输出颜色对象指针
 */
void XColor_toRgb(const XColor* self, XColor* out);

/**
 * @brief      将颜色转换为 HSV 色彩空间
 * @param self 目标 XColor 对象指针
 * @param out  输出颜色对象指针
 */
void XColor_toHsv(const XColor* self, XColor* out);

/**
 * @brief      将颜色转换为 CMYK 色彩空间
 * @param self 目标 XColor 对象指针
 * @param out  输出颜色对象指针
 */
void XColor_toCmyk(const XColor* self, XColor* out);

/**
 * @brief      将颜色转换为 HSL 色彩空间
 * @param self 目标 XColor 对象指针
 * @param out  输出颜色对象指针
 */
void XColor_toHsl(const XColor* self, XColor* out);

/**
 * @brief      获取 32 位 ARGB 值
 * @param self 目标 XColor 对象指针
 * @return     32 位 ARGB 值（AARRGGBB 格式）
 */
uint32_t XColor_rgba(const XColor* self);

/**
 * @brief      获取 32 位 RGB 值（Alpha 忽略）
 * @param self 目标 XColor 对象指针
 * @return     32 位 RGB 值（0x00RRGGBB 格式）
 */
uint32_t XColor_rgb(const XColor* self);

/**
 * @brief      设置 32 位 ARGB 值
 * @param self  目标 XColor 对象指针
 * @param argb  32 位 ARGB 值
 */
void XColor_setRgba(XColor* self, uint32_t argb);

/**
 * @brief      设置 32 位 RGB 值（Alpha 设为 255）
 * @param self 目标 XColor 对象指针
 * @param rgb  32 位 RGB 值
 */
void XColor_setRgb_uint32(XColor* self, uint32_t rgb);

/**
 * @brief      比较两个颜色是否相等
 * @param a    颜色 A
 * @param b    颜色 B
 * @return     相等返回 true，否则返回 false
 */
bool XColor_equals(const XColor* a, const XColor* b);

/**
 * @brief      获取所有 SVG 命名颜色名称列表
 * @param outCount 输出颜色数量指针
 * @return     颜色名称字符串数组（以 NULL 结尾），由调用者 free
 */
const char** XColor_colorNames(int* outCount);

/**
 * @brief      按名称获取 SVG 命名颜色
 * @param name 颜色名称（不区分大小写）
 * @return     对应的颜色，未找到返回无效颜色
 */
XColor XColor_fromName(const char* name);

/* ========== 预定义常用颜色常量 ========== */

#define XColor_White       XColor_create_rgb(255, 255, 255, 255)
#define XColor_Black       XColor_create_rgb(0, 0, 0, 255)
#define XColor_Red         XColor_create_rgb(255, 0, 0, 255)
#define XColor_Green       XColor_create_rgb(0, 255, 0, 255)
#define XColor_Blue        XColor_create_rgb(0, 0, 255, 255)
#define XColor_Cyan        XColor_create_rgb(0, 255, 255, 255)
#define XColor_Magenta     XColor_create_rgb(255, 0, 255, 255)
#define XColor_Yellow      XColor_create_rgb(255, 255, 0, 255)
#define XColor_Gray        XColor_create_rgb(128, 128, 128, 255)
#define XColor_DarkGray    XColor_create_rgb(64, 64, 64, 255)
#define XColor_LightGray   XColor_create_rgb(192, 192, 192, 255)
#define XColor_Transparent XColor_create_rgb(0, 0, 0, 0)

#ifdef __cplusplus
}
#endif
#endif /* XCOLOR_H */
